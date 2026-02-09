#include "mqtt.h"
#include <vector>
#define MIN_DEACTIVATION_TIME_MS 15
#define MAX_ACTIVATION_TIME_MS 5
#define MAX_WHEELSET_INTERVAL_MS 2000
#define MAX_TRAIN_INTERVAL_MS 10000
#define DEBUG_CEJES
struct Event
{
  bool isB;
  unsigned long timestamp;
};
class ContadorEjes : public mqtt_device {
  int pinA;
  int pinB;
  unsigned long prevA=0;
  unsigned long prevB=0;
  unsigned long firstAon=0;
  unsigned long firstBon=0;
  int Aon=0;
  int Bon=0;
  int missedLast=-1;
  std::vector<Event> eventBuffer;
  int dirTren = -1;
  unsigned long ultimaActivacion;
public:
  const char *id;
  std::string topic;
#ifdef DEBUG_CEJES
  std::string topic_dbg;
  std::vector<std::pair<unsigned long,std::pair<bool,bool>>> events_dbg;
  std::pair<bool,bool> prev_dbg;
#endif
  ContadorEjes(const char *id, mqtt_client *client, int pinA, int pinB) : mqtt_device(client), pinA(pinA), pinB(pinB), id(id) {
    topic = std::string("cejes/") + id + "/event";
#ifdef DEBUG_CEJES
    topic_dbg = std::string("cejes/") + id + "/debug";
#endif
    provided_topics.push_back(topic);
    eventBuffer.reserve(2*4);
  }
  void setup() override {
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
  }
  void addSecuencia(bool b) {
    if (b && pinA < 0) {
      client->publish(topic.c_str(), "Reverse");
    } else if (!b && pinB < 0) {
      client->publish(topic.c_str(), "Nominal");
    } else {
      if (missedLast != b || millis() - ultimaActivacion > MAX_WHEELSET_INTERVAL_MS) {
        eventBuffer.push_back({b, millis()});
      }
      missedLast = -1;
      ultimaActivacion = millis();
    }
  }
  int getDirection(bool strict=false) {
    int dir = -1;
    unsigned long sumAB=0;
    unsigned long sumBA=0;
    unsigned long minAB=999999;
    unsigned long minBA=999999;
    int numAB=0;
    int numBA=0;

    for (size_t i = 0; i + 1 < eventBuffer.size(); ++i) {
      Event e1 = eventBuffer[i];
      Event e2 = eventBuffer[i + 1];
      unsigned long interval = e2.timestamp - e1.timestamp;
      if (interval < MAX_WHEELSET_INTERVAL_MS) {
        if (!e1.isB && e2.isB) {
          ++numAB;
          sumAB += interval;
          if (interval < minAB) minAB = interval;
        } else if (e1.isB && !e2.isB) {
          ++numBA;
          sumBA += interval;
          if (interval < minBA) minBA = interval;
        }
      }
    }
    if (numAB > 0 && numBA > 0) {
      sumAB /= numAB;
      sumBA /= numBA;
      if (strict) {
        if (sumAB < sumBA && minAB < minBA) return 1;
        else if (sumBA < sumAB && minBA < minAB) return 0;
      } else {
        if (minAB < minBA) return 1;
        else return 0;
      }
    } else if (!strict) {
      if (numAB > 0) return 1;
      else if (numBA > 0) return 0;
    }
    return -1;
  }
  int countAxles(bool dirIsB) {
    if (eventBuffer.size() == 0) return 0;

    bool expected = dirIsB;  // e.g. if dirIsB = false → expect A first
    int phase = 0;           // 0 = looking for first in pair, 1 = looking for second
    int axleCount = 0;

    for (size_t i = 0; i < eventBuffer.size(); ++i) {
      bool isB = eventBuffer[i].isB;

      if (phase == 0) {
        // Looking for first in A-B or B-A
        if (isB == expected) {
          phase = 1;  // Wait for matching pair
        } else {
          // Missed expected start — simulate it
          axleCount++;   // Count virtual axle
          // expected stays the same, try again
        }
      } else {
        // phase == 1 → waiting for second sensor
        if (isB != expected) {
          // Got the matching pair
          axleCount++;
          phase = 0;
        } else {
          // Missed the second sensor — simulate it, count axle
          axleCount++;
          // Stay in phase 1 to process current as first of next pair
          // Don't advance expected yet
        }
      }
    }

    // If we ended mid-pair, simulate last one
    if (phase == 1) {
      axleCount++;
      missedLast = !expected;
    } else {
      missedLast = -1;
    }

    return axleCount;
  }
  void processTrain()
  {
    if (eventBuffer.empty()) return;
    int dir = getDirection(true);
    if (dir < 0) dir = dirTren;
    if (dir < 0) dir = getDirection();
    if (dir < 0) {
      client->publish(topic.c_str(), "Error");
    } else {
      int num = countAxles(dir == 0);
      client->publish(topic.c_str(), ((dir == 1 ? "Nominal:" : "Reverse:")+String(num)).c_str());
    }
    eventBuffer.clear();
  }
  void loop() override {
    if (pinA >= 0 && !digitalRead(pinA)) {
      if (Aon == 0 && millis() - prevA > MIN_DEACTIVATION_TIME_MS) {
        Aon = 1;
        firstAon = millis();
      } else if (Aon == 1 && millis() - firstAon > MAX_ACTIVATION_TIME_MS) {
        Aon = 2;
        addSecuencia(false);
      }
      prevA = millis();
    } else {
      Aon = 0;
    }
    if (pinB >= 0 && !digitalRead(pinB)) {
      if (Bon == 0 && millis() - prevB > MIN_DEACTIVATION_TIME_MS) {
        Bon = 1;
        firstBon = millis();
      } else if (Bon == 1 && millis() - firstBon > MAX_ACTIVATION_TIME_MS) {
        Bon = 2;
        addSecuencia(true);
      }
      prevB = millis();
    } else {
      Bon = 0;
    }
    unsigned long timeSinceLast = millis() - ultimaActivacion;
    if (!eventBuffer.empty()) {
      if (eventBuffer.size() > 3) {
        int dir = getDirection(true);
        if (dir >= 0) {
          dirTren = dir;
          processTrain();
        }
      } else if (eventBuffer.size() >= 2 && dirTren >= 0) {
        int dir = getDirection(false);
        if (dir == dirTren) processTrain();
      }
      if (timeSinceLast > MAX_TRAIN_INTERVAL_MS) processTrain();
    }
    if (dirTren >= 0 && timeSinceLast > MAX_TRAIN_INTERVAL_MS) dirTren = -1;
#ifdef DEBUG_CEJES
    std::pair<bool,bool> state_dbg = {!digitalRead(pinA), !digitalRead(pinB)};
    if (state_dbg != prev_dbg) {
      events_dbg.push_back({micros(), state_dbg});
      prev_dbg = state_dbg;
    }
    if (events_dbg.size() > 15 || (!events_dbg.empty() && micros() - events_dbg.back().first > 15000000UL)) {
      byte buff[events_dbg.size()*5+1];
      for (int i=0; i<events_dbg.size(); i++) {
        buff[5*i] = events_dbg[i].first>>24;
        buff[5*i+1] = events_dbg[i].first>>16;
        buff[5*i+2] = events_dbg[i].first>>8;
        buff[5*i+3] = events_dbg[i].first;
        buff[5*i+4] = (events_dbg[i].second.first<<1)|events_dbg[i].second.second;
      }
      std::string payload((char*)buff, events_dbg.size()*5);
      client->publish(topic_dbg, payload);
      events_dbg.clear();
    }
#endif
  }
};
