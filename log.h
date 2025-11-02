#pragma once
#include <string>
enum LogLevel
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
};
void log(const std::string &msg, LogLevel level=LOG_INFO);
void log(const std::string &id, const std::string &msg, LogLevel level=LOG_INFO);