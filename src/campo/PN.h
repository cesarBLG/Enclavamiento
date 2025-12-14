#include <HardwareSerial.h>
class SPPN : public mqtt_device
{
    bool cierre = false;
    bool envioPendiente = false;
    unsigned long pendienteAck = 0;
    bool comprobacionSoneria = false;
    bool averiaSoneria = false;
    unsigned long ultimaComprobacionSoneria = 0;
    int pinLuz1;
    int pinLuz2;
    int pinTx;
    int pinRx;

    const char *id;
    std::string topic;
    std::string topicComprobacion;

    HardwareSerial serial;
    std::vector<byte> serialBuffer;
public:
    SPPN(const char *id, mqtt_client *client, int pin1, int pin2, int pinRx, int pinTx) : mqtt_device(client), id(id), pinLuz1(pin1), pinLuz2(pin2), serial(2), pinRx(pinRx), pinTx(pinTx)
    {
        topic = std::string("pn/")+id+"/cierre";
        topicComprobacion = std::string("pn/")+id+"/comprobacion";
        provided_topics.push_back(topicComprobacion);
    }
    void setup() override
    {
        serial.begin(9600, SERIAL_8N1, pinRx, pinTx);
        pinMode(pinLuz1, OUTPUT);
        digitalWrite(pinLuz1, HIGH);
        pinMode(pinLuz2, OUTPUT);
        digitalWrite(pinLuz2, HIGH);

        serial.write(0x7E);
        serial.write(3);
        serial.write(0x31);
        serial.write(30);
        serial.write(0xEF);
        pendienteAck = millis();
        client->subscribe(topic.c_str(), 1);
    }
    void loop() override
    {
        if (cierre) {
            digitalWrite(pinLuz1, (millis()/500)%2);
            digitalWrite(pinLuz2, ((millis()/500)%2) == 0);
        } else {
            digitalWrite(pinLuz1, HIGH);
            digitalWrite(pinLuz2, HIGH);
        }
        if (cierre && !comprobacionSoneria && !pendienteAck && !averiaSoneria) {
            serial.write(0x7E);
            serial.write(4);
            serial.write(0x33);
            serial.write(0x00);
            serial.write(0x01);
            serial.write(0xEF);
            pendienteAck = millis();
            envioPendiente = false;
        }
        if (!cierre && envioPendiente && !pendienteAck) {
            serial.write(0x7E);
            serial.write(2);
            serial.write(0x0E);
            serial.write(0xEF);
            comprobacionSoneria = false;
            sendState();
            pendienteAck = millis();
            envioPendiente = false;
        }
        if (!pendienteAck && millis() - ultimaComprobacionSoneria > 20000) {
            ultimaComprobacionSoneria = millis();
            serial.write(0x7E);
            serial.write(2);
            serial.write(0x10);
            serial.write(0xEF);
            pendienteAck = millis();
        }
        if (pendienteAck && millis() - pendienteAck > 5000) {
            pendienteAck = 0;
        }
        while (serial.available())
        {
            serialBuffer.push_back(serial.read());
            if (serialBuffer[0] != 0x7E) serialBuffer.clear();
            if (serialBuffer.size() > 1 && serialBuffer.size() >= serialBuffer[1] + 2) {
                if (serialBuffer.back() == 0xEF) {
                    if (serialBuffer[1] == 2 && serialBuffer[2] == 0) {
                        pendienteAck = 0;
                        if (!averiaSoneria && cierre) {
                            comprobacionSoneria = true;
                            sendState();
                        }
                    }
                    if (serialBuffer[1] == 3 && serialBuffer[2] == 0x10) {
                        averiaSoneria = serialBuffer[3] != 0 && serialBuffer[3] != 1;
                        if (comprobacionSoneria && averiaSoneria) {
                            comprobacionSoneria = false;
                            sendState();
                        }
                    }
                }
                serialBuffer.clear();
            }
        }
    }
    void msg_callback(const char *topic, const char *payload) override
    {
        if (strcmp(topic, this->topic.c_str()) != 0) return;
        setCierre(!strcmp(payload, "true"));
    }
    void sendState()
    {
        client->publish(topicComprobacion.c_str(), comprobacionSoneria ? "true" : "false", 1);
    }
    void setCierre(bool newCierre)
    {
        cierre = newCierre;
        envioPendiente = true;
    }
};
