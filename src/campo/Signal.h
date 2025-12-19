#include <enclavamiento.h>
class Signal : public mqtt_device
{
    int pinRojo;
    int pinVerde;
    estado_señal estado;
public:
    const char *id;
    std::string topic;
    Signal(const char *id, mqtt_client *client, int pinRojo, int pinVerde) : mqtt_device(client), pinRojo(pinRojo), pinVerde(pinVerde), estado({Aspecto::Parada, Aspecto::Parada, false}), id(id)
    {
      topic = std::string("signal/")+id+"/state";
    }
    void msg_callback(const char *topic, const char *payload) override
    {
      if (strcmp(topic, this->topic.c_str()) != 0) return;
      estado = json::parse(payload);
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
        switch (estado.aspecto)
        {
            default:
                digitalWrite(pinRojo, HIGH);
                digitalWrite(pinVerde, HIGH);
                break;
            case Aspecto::RebaseAutorizado:
                digitalWrite(pinVerde, HIGH);
                digitalWrite(pinRojo, (millis()/500)%2 ? HIGH : LOW);
                break;
            case Aspecto::ParadaDiferida:
                digitalWrite(pinVerde, HIGH);
                digitalWrite(pinRojo, (millis()/500)%2 ? HIGH : LOW);
                break;
            case Aspecto::Precaucion:
                digitalWrite(pinVerde, LOW);//(millis()/500)%2 ? HIGH : LOW);
                digitalWrite(pinRojo, LOW);
                break;
            case Aspecto::ViaLibre:
                digitalWrite(pinRojo, LOW);
                digitalWrite(pinVerde, LOW);
                break;
        }
    }
};
