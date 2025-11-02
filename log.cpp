#include "log.h"
#include <iostream>
void log(const std::string &msg, LogLevel level)
{
    std::cout<<msg<<std::endl;
}
void log(const std::string &id, const std::string &msg, LogLevel level)
{
    std::cout<<id<<": "<<msg<<std::endl;
}