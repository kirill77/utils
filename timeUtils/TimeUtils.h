#pragma once

#include <ctime>
#include <string>

struct TimeUtils
{
    static std::string timeStampToString(std::time_t timeT, const char* sFormatString = "%Y%m%d-%H:%M:%S");
    static std::tm timeStampToTM(std::time_t timeT);
};