#include "log.h"
#include "mqtt.h"
#include <iostream>
void log(const std::string &msg, LogLevel level)
{
    send_message("log", msg);
    std::cout<<msg<<std::endl;
}
void log(const std::string &id, const std::string &msg, LogLevel level)
{
    log(id+": "+msg, level);
}