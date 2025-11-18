#include <mosquitto.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>
#include "mqtt.h"
#include "log.h"

mosquitto *mosq;
std::string name;

std::map<std::string, std::vector<std::string>> handled_topics;
bool connected = false;
void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    if (rc == 0) {
        log("mqtt", "connected", LOG_DEBUG);
        connected = true;
        mosquitto_subscribe(mosq, nullptr, "desconexion", 1);
        mosquitto_subscribe(mosq, nullptr, "desconexion/+", 1);
        mosquitto_subscribe(mosq, nullptr, "mando/+", 2);
        mosquitto_subscribe(mosq, nullptr, "cejes/+/+/event", 2);
        mosquitto_subscribe(mosq, nullptr, "cv/+/+/state", 2);
        mosquitto_subscribe(mosq, nullptr, "bloqueo/+/state", 1);
        mosquitto_subscribe(mosq, nullptr, "bloqueo/+/ruta/+", 1);
        mosquitto_subscribe(mosq, nullptr, "signal/+/+/state", 1);
        mosquitto_subscribe(mosq, nullptr, "fec/+", 0);
    } else {
        log("mqtt", "connection failed", LOG_DEBUG);
    }
}

void send_message(const std::string &topic, const std::string &payload, int qos, bool retain)
{
    mosquitto_publish(mosq, nullptr, topic.c_str(), payload.size(), payload.c_str(), qos, retain);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    std::string topic(message->topic);
    std::string payload((const char*)message->payload, message->payloadlen);
    if (topic == "desconexion") {
        auto &tops = handled_topics[payload];
        for (auto &t : tops) {
            //mosquitto_publish(mosq, nullptr, t.c_str(), 13, "\"desconexion\"", 1, false);
            handle_message(t, "\"desconexion\"");
        }
    } else if (topic.size() > 12 && topic.substr(0, 11) == "desconexion") {
        std::string cl = topic.substr(12);
        handled_topics[cl] = split(payload, '\n');
    } else {
        handle_message(topic, payload);
    }
}

void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    for (auto &kvp : handled_topics) {
        for (const auto &t : kvp.second) {
            if (t == "") continue;
            handle_message(t, "\"desconexion\"");
        }
    }
    log("mqtt", "disconnected", LOG_ERROR);
    connected = false;
}

std::vector<std::string> split(const std::string& str, char delimiter)
{
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string token;

    while (std::getline(ss, token, delimiter)) {
        result.push_back(token);
    }

    return result;
}

void init_mqtt(const json &j)
{
    mosquitto_lib_init();

    name = j["Name"].get<std::string>();

    mosq = mosquitto_new(name.c_str(), true, nullptr);
    mosquitto_connect_callback_set(mosq, on_connect);

    mosquitto_will_set(mosq, "desconexion", name.size(), name.c_str(), 1, false);
    mosquitto_connect(mosq, j["Host"].get<std::string>().c_str(), 1883, 15);

    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    
    while (!connected) {
        mosquitto_loop(mosq, -1, 1);
    }
}

void loop_mqtt(int64_t timeout)
{
    mosquitto_loop(mosq, timeout, 1);
    if (!connected) {
        mosquitto_reconnect(mosq);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
    }
}

void exit_mqtt()
{
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
}

std::string id_to_mqtt(std::string id)
{
    for (int i=0; i<id.size(); i++) {
        if (id[i] == '/') id[i] = '_';
        else if (id[i] == ':') id[i] = '/';
    }
    return id;
}
std::string id_from_mqtt(std::string id)
{
    for (int i=0; i<id.size(); i++) {
        if (id[i] == '_') id[i] = '/';
        else if (id[i] == '/') id[i] = ':';
    }
    return id;
}
