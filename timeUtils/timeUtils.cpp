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

std::string TimeUtils::timeStampToLocalString(std::time_t inTS, const char* sFormatString)
{
    std::tm tm_local;
    localtime_s(&tm_local, &inTS);
    char buffer[80];
    std::strftime(buffer, sizeof(buffer), sFormatString, &tm_local);
    return buffer;
}

std::tm TimeUtils::timeStampToTM(std::time_t inTS)
{
    std::tm tm_utc;
    gmtime_s(&tm_utc, &inTS);
    return tm_utc;
}

int TimeUtils::getDayOfWeek(int year, int month, int day)
{
    // Zeller's congruence for Gregorian calendar
    if (month < 3) {
        month += 12;
        year--;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
    // Convert from Zeller's (0=Sat, 1=Sun, ...) to standard (0=Sun, 1=Mon, ...)
    return ((h + 6) % 7);
}

int TimeUtils::getNthWeekdayOfMonth(int year, int month, int weekday, int n)
{
    // Find the first occurrence of the weekday in the month
    int firstDayOfMonth = getDayOfWeek(year, month, 1);
    int firstOccurrence = 1 + ((weekday - firstDayOfMonth + 7) % 7);
    
    // Add weeks to get to the nth occurrence
    return firstOccurrence + (n - 1) * 7;
}

bool TimeUtils::isPacificDST(int year, int month, int day)
{
    // DST in US: 2nd Sunday of March to 1st Sunday of November
    // (at 2:00 AM local time, but we simplify to date-based)
    
    if (month < 3 || month > 11) {
        return false;  // Jan, Feb, Dec - definitely PST
    }
    if (month > 3 && month < 11) {
        return true;   // Apr through Oct - definitely PDT
    }
    
    // March: DST starts on 2nd Sunday
    if (month == 3) {
        int secondSunday = getNthWeekdayOfMonth(year, 3, 0, 2);  // 0 = Sunday
        return day >= secondSunday;
    }
    
    // November: DST ends on 1st Sunday
    if (month == 11) {
        int firstSunday = getNthWeekdayOfMonth(year, 11, 0, 1);  // 0 = Sunday
        return day < firstSunday;
    }
    
    return false;
}

bool TimeUtils::isUSStockMarketOpen(std::time_t timestamp)
{
    // Convert UTC timestamp to components
    std::tm utc = timeStampToTM(timestamp);
    
    // Get date components (UTC)
    int year = utc.tm_year + 1900;
    int month = utc.tm_mon + 1;  // tm_mon is 0-based
    int day = utc.tm_mday;
    int utcHour = utc.tm_hour;
    int utcMinute = utc.tm_min;
    
    // Check if weekend (in UTC - might be slightly off at day boundaries, but close enough)
    int dayOfWeek = getDayOfWeek(year, month, day);
    if (dayOfWeek == 0 || dayOfWeek == 6) {  // Sunday or Saturday
        return false;
    }
    
    // Determine Pacific offset: PDT = UTC-7, PST = UTC-8
    int pacificOffset = isPacificDST(year, month, day) ? -7 : -8;
    
    // Convert UTC time to minutes since midnight
    int utcMinutes = utcHour * 60 + utcMinute;
    
    // Convert to Pacific time minutes (can go negative, that's okay)
    int pacificMinutes = utcMinutes + (pacificOffset * 60);
    
    // Handle day wraparound (if Pacific time is previous day)
    if (pacificMinutes < 0) {
        pacificMinutes += 24 * 60;
        // Also need to recheck day of week for previous day
        // But for simplicity, if it's very early UTC Monday, it's still Sunday Pacific = closed
        // This edge case (UTC Monday 00:00-08:00) would show as Sunday in Pacific
        dayOfWeek = (dayOfWeek + 6) % 7;  // Previous day
        if (dayOfWeek == 0 || dayOfWeek == 6) {
            return false;
        }
    }
    
    // Market hours in Pacific time: 6:30 AM to 1:00 PM
    // 6:30 AM = 6*60 + 30 = 390 minutes
    // 1:00 PM = 13*60 = 780 minutes
    const int marketOpenMinutes = 6 * 60 + 30;   // 6:30 AM
    const int marketCloseMinutes = 13 * 60;      // 1:00 PM
    
    return pacificMinutes >= marketOpenMinutes && pacificMinutes < marketCloseMinutes;
}
