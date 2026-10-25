#include <ESP32Servo.h>
#include <enclavamiento.h>
#include "mqtt.h"
class Semaforica : public mqtt_device
{
    Servo servo;
    int pin;
    int pinTestigo;
    int pinFarol;
    bool farol;
    Aspecto aspecto;
public:
    const char *id;
    std::string topic;
    Semaforica(const char *id, mqtt_client *client, int pin, int pinTestigo=-1, int pinFarol=-1) : mqtt_device(client), pin(pin), pinTestigo(pinTestigo), pinFarol(pinFarol), farol(false), aspecto(Aspecto::Parada), id(id)
    {
        topic = std::string("signal/")+id+"/state";
    }
    void msg_callback(const std::string_view topic, const std::string_view payload) override
    {
        if (this->topic == topic) {
            estado_señal e = json::parse(payload);
            if (aspecto != e.aspecto.aspecto) {
                aspecto = e.aspecto.aspecto;
                update_semaforo();
            }
        } else if (topic == "luz") {
            if (payload == "día") farol = false;
            else if (payload == "noche") farol = true;
        }
    }
    void setup() override
    {
        servo.attach(pin, 1000, 2000);
        client->subscribe(topic.c_str());
        client->subscribe("luz");
        update_semaforo();
        if (pinFarol >= 0) {
            pinMode(pinFarol, OUTPUT);
            digitalWrite(pinFarol, LOW);
        }
        if (pinTestigo >= 0) {
            pinMode(pinTestigo, OUTPUT);
            digitalWrite(pinTestigo, HIGH);
        }
    }
    void loop() override
    {
        if (pinFarol >= 0) {
            if (!farol) {
                digitalWrite(pinFarol, LOW);
            } else {
                switch (aspecto)
                {
                    default:
                        digitalWrite(pinFarol, HIGH);
                        break;
                    case Aspecto::RebaseAutorizado:
                        digitalWrite(pinFarol, (millis()/500)%2 ? HIGH : LOW);
                        break;
                }
            }
        }
    }
    void update_semaforo()
    {
        switch (aspecto)
        {
            default:
                servo.write(90);
                break;
            case Aspecto::RebaseAutorizado:
                servo.write(45);
                break;
            case Aspecto::Precaucion:
                servo.write(135);
                break;
            case Aspecto::ViaLibre:
                servo.write(180);
                break;
        }
    }
};
