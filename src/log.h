#pragma once
#include <string>
#include "id_elemento.h"
enum LogLevel
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
};
void log(const std::string &msg, LogLevel level=LOG_INFO);
void log(const std::string &id, const std::string &msg, LogLevel level=LOG_INFO);
void log(const id_elemento &id, const std::string &msg, LogLevel level=LOG_INFO);
