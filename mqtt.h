#pragma once
#include <string>
#include <vector>
void init_mqtt(std::string name);
void loop_mqtt();
void loop_mqtt(int64_t timeout);
void exit_mqtt();

void send_message(const std::string &topic, const std::string &payload, int qos=1, bool retain=false);
void handle_message(const std::string &topic, const std::string &payload);

std::vector<std::string> split(const std::string& str, char delimiter);