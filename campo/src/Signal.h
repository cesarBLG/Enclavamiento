#include <enclavamiento.h>
#include "mqtt.h"
class Signal : public mqtt_device
{
    int pinRojo;
    int pinVerde;
    Aspecto aspecto;
public:
    const char *id;
    std::string topic;
    Signal(const char *id, mqtt_client *client, int pinRojo, int pinVerde) : mqtt_device(client), pinRojo(pinRojo), pinVerde(pinVerde), aspecto(Aspecto::Parada), id(id)
    {
        topic = std::string("signal/")+id+"/state";
    }
    void msg_callback(const std::string_view topic, const std::string_view payload) override
    {
        if (this->topic != topic) return;
        estado_señal e = json::parse(payload);
        aspecto = e.aspecto.aspecto;
    }
    void setup() override
    {
        pinMode(pinRojo, OUTPUT);
        pinMode(pinVerde, OUTPUT);
        digitalWrite(pinRojo, HIGH);
        digitalWrite(pinVerde, HIGH);
        client->subscribe(topic.c_str());
    }
    void loop() override
    {
        switch (aspecto)
        {
            default:
                digitalWrite(pinRojo, HIGH);
                digitalWrite(pinVerde, HIGH);
                break;
            case Aspecto::RebaseAutorizadoDestellos:
            case Aspecto::RebaseAutorizado:
                digitalWrite(pinVerde, HIGH);
                digitalWrite(pinRojo, (millis()/500)%2 ? HIGH : LOW);
                break;
            case Aspecto::Precaucion:
                digitalWrite(pinVerde, (millis()/500)%2 ? HIGH : LOW);
                digitalWrite(pinRojo, LOW);
                break;
            case Aspecto::ViaLibre:
                digitalWrite(pinRojo, LOW);
                digitalWrite(pinVerde, LOW);
                break;
        }
    }
};
