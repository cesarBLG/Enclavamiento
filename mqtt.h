#pragma once
#include <string>
#include <vector>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
void init_mqtt(const json &j);
void loop_mqtt();
void loop_mqtt(int64_t timeout);
void exit_mqtt();

extern std::string name;

void send_message(const std::string &topic, const std::string &payload, int qos=1, bool retain=false);
void handle_message(const std::string &topic, const std::string &payload);
std::string id_to_mqtt(std::string id);
std::string id_from_mqtt(std::string id);

std::vector<std::string> split(const std::string& str, char delimiter);
