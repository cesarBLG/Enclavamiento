#pragma once
#include "mqtt_client.h"
#include <vector>
#include <mdns.h>
#include <WiFiClient.h>
#include <string_view>
class mqtt_client;
class mqtt_device
{
  protected:
  mqtt_client *client;
  public:
  std::vector<std::string> provided_topics;
  mqtt_device(mqtt_client *client) : client(client) {}
  virtual ~mqtt_device()=default;
  virtual void setup()=0;
  virtual void loop()=0;
  virtual void msg_callback(const std::string_view topic, const std::string_view payload) {}
};
struct mqtt_subscribe
{
  std::string topic;
  int qos;
};
class mqtt_client
{
  esp_mqtt_client_config_t mqtt_cfg = {};
  esp_mqtt_client_handle_t mqtt;
  char *client_id;
  public:
  bool connected = false;
  std::vector<mqtt_device*> device_list;
  //vector<mqtt_client_components> client_components;
  std::vector<mqtt_subscribe> subscribed_topics;
  bool gestor_conectado = true;
  mqtt_client()
  {
    
  }
  void begin(char *client_id)
  {
    this->client_id = client_id;
    char *address = nullptr;
    
    WiFiClient client;
    if (client.connect("10.194.70.1", 1883)) {
      address = "mqtt://10.194.70.1:1883";
      client.stop();
    } else {
      address = get_broker_address();
    }

    while (!address) {
      delay(10000);
      address = get_broker_address();
    }

    Serial.println(address);

    mqtt_cfg.broker.address.uri = address;
    mqtt_cfg.credentials.client_id = client_id;
    mqtt_cfg.session.last_will.topic = "desconexion";
    mqtt_cfg.session.last_will.msg = client_id;
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.keepalive = 8;
    mqtt = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, mqtt_event_handler, this);
    esp_mqtt_client_start(mqtt);
  }
  char *get_broker_address()
  {
    struct esp_ip4_addr addr;
    addr.addr = 0;

    esp_err_t err = mdns_query_a("nodered", 5000,  &addr);
    
    if(err){
      if(err == ESP_ERR_NOT_FOUND){
          Serial.println("Host was not found!");
          return nullptr;
      }
      Serial.println("Query Failed");
      return nullptr;
    }

    char *ip_str = new char[16+7];
    strcpy(ip_str, "mqtt://");
    snprintf(ip_str+7, 16, IPSTR, IP2STR(&addr));
    return ip_str;
  }
  void send_all_devices(const std::string_view topic, const std::string_view payload)
  {
    for (auto *device : device_list)
    {
        device->msg_callback(topic, payload);
    }
  }
  void handle_event(esp_event_base_t event_base, int32_t event_id, void *event_data)
  {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id)
    {
      case MQTT_EVENT_CONNECTED:
        connected = true;
        Serial.println("mqtt: connected");
        for (int i=0; i<subscribed_topics.size(); i++)
        {
          esp_mqtt_client_subscribe_single(mqtt, subscribed_topics[i].topic.c_str(), subscribed_topics[i].qos);
        }
        {
          std::string own_topics;
          
          for (auto *device : device_list)
          {
            for (int i=0; i<device->provided_topics.size(); i++)
            {
              if (own_topics.length() > 0) own_topics += '\n';
              own_topics += device->provided_topics[i];
            }
          }
          std::string desconex("desconexion/");
          desconex += client_id;
          esp_mqtt_client_enqueue(mqtt, desconex.c_str(), own_topics.c_str(), 0, 0, 1, true);
        }
        esp_mqtt_client_subscribe_single(mqtt, "gestor_conexion", 0);
        break;
      case MQTT_EVENT_DISCONNECTED:
        connected = false;
        Serial.println("mqtt: disconnected");
        for (int i=0; i<subscribed_topics.size(); i++)
        {
          send_all_devices(subscribed_topics[i].topic, "\"desconexion\"");
        }
        break;
      case MQTT_EVENT_SUBSCRIBED:
        break;
      case MQTT_EVENT_UNSUBSCRIBED:
        break;
      case MQTT_EVENT_PUBLISHED:
        break;
      case MQTT_EVENT_DATA:
      {
        std::string topic(event->topic, event->topic_len);
        std::string payload(event->data, event->data_len);
        /*Serial.print(topic.c_str());
        Serial.print('=');
        Serial.println(payload.c_str());*/
        if (topic == "gestor_conexion")
        {
          gestor_conectado = payload == "on";
          if (!gestor_conectado)
          {
            for (int i=0; i<subscribed_topics.size(); i++)
            {
              send_all_devices(subscribed_topics[i].topic, "\"desconexion\"");
            }
          }
        }
        else if (gestor_conectado)
        {
          send_all_devices(topic, payload);
        }
        break;
      }
      case MQTT_EVENT_ERROR:
        break;
      default:
        break;
    }
  }
  static void mqtt_event_handler(void *client, esp_event_base_t event_base, int32_t event_id, void *event_data)
  {
    ((mqtt_client*)client)->handle_event(event_base, event_id, event_data);
  }
  void publish(const std::string_view topic, const std::string_view payload)
  {
    if (connected) esp_mqtt_client_enqueue(mqtt, topic.data(), payload.data(), payload.size(), 0, 0, true);
  }
  void subscribe(const char *topic)
  {
    subscribed_topics.push_back({topic, 0});
    if (connected) esp_mqtt_client_subscribe_single(mqtt, topic, 0);
  }
};
