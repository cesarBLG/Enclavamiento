#include <mosquitto.h>
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
mosquitto *mosq;
std::map<std::string, std::vector<std::string>> handled_topics;
bool connected;
void on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    if (rc == 0) {
        std::cout<<"mqtt: connected"<<std::endl;
        connected = true;
        mosquitto_subscribe(mosq, nullptr, "desconexion", 0);
        mosquitto_subscribe(mosq, nullptr, "desconexion/+", 0);
        mosquitto_publish(mosq, nullptr, "gestor_conexion", 0, nullptr, 0, true);
        mosquitto_publish(mosq, nullptr, "gestor_conexion", 2, "on", 0, false);
    } else {
        std::cout<<"mqtt: connection failed"<<std::endl;
    }
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

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    std::string topic(message->topic);
    std::string payload((const char*)message->payload, message->payloadlen);
    if (topic == "desconexion") {
        auto &tops = handled_topics[payload];
        for (auto &t : tops) {
            mosquitto_publish(mosq, nullptr, t.c_str(), 13, "\"desconexion\"", 0, false);
        }
    } else if (topic.size() > 12 && topic.substr(0, 11) == "desconexion") {
        std::string cl = topic.substr(12);
        handled_topics[cl] = split(payload, '\n');
    }
}

void on_disconnect(struct mosquitto *mosq, void *obj, int rc)
{
    connected = false;
}

int main(int argc, char **argv)
{
    mosquitto_lib_init();

    mosq = mosquitto_new("panel", true, nullptr);
    mosquitto_connect_callback_set(mosq, on_connect);

    mosquitto_will_set(mosq, "gestor_conexion", 3, "off", 0, true);
    while (mosquitto_connect(mosq, argc > 1 ? argv[1] : "127.0.0.1", 1883, 15) != MOSQ_ERR_SUCCESS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    mosquitto_message_callback_set(mosq, on_message);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);

    while (!connected) {
        mosquitto_loop(mosq, -1, 1);
    }

    using clock = std::chrono::steady_clock;
    auto last_publish = clock::now();
    for (;;) {
        auto now = clock::now();
        if (now >= last_publish + std::chrono::seconds(10)) {
            mosquitto_publish(mosq, nullptr, "gestor_conexion", 2, "on", 0, false);
            last_publish = now;
        }
        mosquitto_loop(mosq, -1, 1);
        if (!connected) {
            mosquitto_reconnect(mosq);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}
