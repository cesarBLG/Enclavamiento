#include <enclavamiento.h>
#include "Mando.h"
#include "mqtt.h"
class Aguja : public mqtt_device
{
  int pinMotorN;
  int pinMotorI;
  EntradaPulsador comprobacionN;
  EntradaPulsador comprobacionI;
  int comprobacion;
  unsigned long ultimaComprobacion;
  int mando;
  unsigned long tiempoMando;
public:
  const char *id;
  std::string topic_mando;
  std::string topic_comprobacion;
  Aguja(const char *id, mqtt_client *client, int pinMotorN, int pinMotorI, int pinComprobacionN, int pinComprobacionI) : mqtt_device(client), id(id), pinMotorN(pinMotorN), pinMotorI(pinMotorI), comprobacionN(pinComprobacionN), comprobacionI(pinComprobacionI), comprobacion(-1), mando(-1)
  {
    topic_mando = std::string("aguja/")+id+"/mando";
    topic_comprobacion = std::string("aguja/")+id+"/comprobacion";
    provided_topics.push_back(topic_comprobacion);
  }
  void msg_callback(const std::string_view topic, const std::string_view payload) override
  {
    if (topic != topic_mando) return;
    if (payload == "0" && comprobacion != 0) {
      digitalWrite(pinMotorN, HIGH);
      digitalWrite(pinMotorI, LOW);
      mando = 0;
      tiempoMando = millis();
      update_comprobacion();
    } else if (payload == "1" && comprobacion != 1) {
      digitalWrite(pinMotorN, LOW);
      digitalWrite(pinMotorI, HIGH);
      mando = 1;
      tiempoMando = millis();
      update_comprobacion();
    }

  }
  void setup() override
  {
    pinMode(pinMotorN, OUTPUT);
    pinMode(pinMotorI, OUTPUT);
    digitalWrite(pinMotorN, LOW);
    digitalWrite(pinMotorI, LOW);
    comprobacionN.setup();
    comprobacionI.setup();
    client->subscribe(topic_mando.c_str());
  }
  void loop() override
  {
    comprobacionN.update();
    comprobacionI.update();
    update_comprobacion();
    if (mando >= 0 && millis() - tiempoMando > 2000) {
      digitalWrite(pinMotorN, LOW);
      digitalWrite(pinMotorI, LOW);
      mando = -1;
    }
  }
  void update_comprobacion()
  {
    int comp = -1;
    if (comprobacionN.down() && !comprobacionI.down()) comp = 0;
    else if (!comprobacionN.down() && comprobacionI.down()) comp = 1;
    if (mando >= 0 && mando != comp) comp = -1;
    if (comprobacion != comp || millis() - ultimaComprobacion > 30000) {
      comprobacion = comp;
      client->publish(topic_comprobacion, std::to_string(comprobacion));
      ultimaComprobacion = millis();
    }
  }
};
