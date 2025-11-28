#pragma once

#include <ctime>
#include <string>

struct TimeUtils
{
    static std::string timeStampToString(std::time_t timeT, const char* sFormatString = "%Y%m%d-%H:%M:%S");
    static std::string timeStampToLocalString(std::time_t timeT, const char* sFormatString = "%Y%m%d-%H:%M:%S");
    static std::tm timeStampToTM(std::time_t timeT);
    
    // Returns true if US stock market is open
    // Regular hours: 6:30 AM - 1:00 PM Pacific (9:30 AM - 4:00 PM Eastern)
    // Returns false on weekends (Saturday/Sunday)
    // Properly handles Pacific Daylight Time (PDT) vs Pacific Standard Time (PST)
    static bool isUSStockMarketOpen(std::time_t timestamp);
    
private:
    // Helper to determine if a date falls within Pacific Daylight Time
    // PDT: 2nd Sunday of March 2:00 AM to 1st Sunday of November 2:00 AM
    static bool isPacificDST(int year, int month, int day);
    
    // Get the day of week (0 = Sunday, 6 = Saturday) for a given date
    static int getDayOfWeek(int year, int month, int day);
    
    // Get the Nth occurrence of a weekday in a month (e.g., 2nd Sunday)
    // weekday: 0 = Sunday, 6 = Saturday
    // n: 1 = first, 2 = second, etc.
    static int getNthWeekdayOfMonth(int year, int month, int weekday, int n);
};