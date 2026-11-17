#include <enclavamiento.h>
#include "mqtt.h"
class Signal : public mqtt_device
{
    int pinRojo;
    int pinVerde;
    unsigned int focos_mask;
    Aspecto aspecto = Aspecto::Parada;
public:
    const char *id;
    std::string topic;
    std::string topic_field;
    Signal(const char *id, mqtt_client *client, int pinRojo, int pinVerde) : mqtt_device(client), pinRojo(pinRojo), pinVerde(pinVerde), aspecto(Aspecto::Parada), id(id)
    {
        topic = std::string("signal/")+id+"/state";
        topic_field = std::string("signal/")+id+"/field_state";
        provided_topics.push_back(topic_field);
    }
    void msg_callback(const std::string_view topic, const std::string_view payload) override
    {
        if (this->topic != topic) return;
        estado_señal e = json::parse(payload);
        focos_mask = e.focos_mandados_mask;
        aspecto = e.aspecto.aspecto;
        client->publish(topic_field, std::to_string(focos_mask));
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
        if (aspecto == Aspecto::Parada) focos_mask = ((int)EstadoFocoSeñal::Encendido)<<(2*(int)FocoSeñal::R);
        for (auto foco : {FocoSeñal::R, FocoSeñal::V}) {
            auto est = (EstadoFocoSeñal)((focos_mask>>(2*(int)foco)) & 3);
            bool on = est == EstadoFocoSeñal::Encendido || (est == EstadoFocoSeñal::Intermitente && (millis()/500)%2);
            if (foco == FocoSeñal::R) digitalWrite(pinRojo, on);
            else if (foco == FocoSeñal::V) digitalWrite(pinVerde, !on);
        }
    }
};
