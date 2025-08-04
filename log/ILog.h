#pragma once

#include <windows.h>
#include <ctime>

enum class LogLevel : unsigned
{
    eInfo,
    eWarning,
    eError
};

struct ILog
{
    static ILog* getInterface(const char *sName = nullptr);

    virtual void setTimeOverride(bool bOverride, std::time_t timeOverride) = 0;
    virtual void enableThreadAndFileInfo(bool bEnable) = 0;
    virtual void logva(LogLevel level, const char* sFile, unsigned uLine, const char* func, const char* fmt, ...) = 0;
    virtual void shutdown() = 0;
};

#define LOG_INFO(fmt,...) ILog::getInterface()->logva(LogLevel::eInfo, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt,...) ILog::getInterface()->logva(LogLevel::eWarning, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt,...) ILog::getInterface()->logva(LogLevel::eError, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
