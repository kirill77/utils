// timeUtils.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include "TimeUtils.h"

std::string TimeUtils::timeStampToString(std::time_t inTS, const char* sFormatString)
{
    std::tm tmpTM = timeStampToTM(inTS);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), sFormatString, &tmpTM);
    return buffer;
}
std::tm TimeUtils::timeStampToTM(std::time_t inTS)
{
    std::tm tm_utc;
    gmtime_s(&tm_utc, &inTS);
    return tm_utc;
}
