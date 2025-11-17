#pragma once

#include <windows.h>
#include <ctime>
#include <string>
#include <memory>

enum class LogLevel : unsigned
{
    eVerbose,   // Most detailed logging (lowest priority)
    eInfo,
    eWarning,
    eError
};

// Observer interface for log messages
struct ILogObserver
{
    virtual ~ILogObserver() = default;
    virtual void onLogMessage(LogLevel level, const std::string& message, const std::string& timestamp) = 0;
};

struct ILog
{
    static ILog* getInterface(const char *sName = nullptr);
    static ILog* create(const std::string &sPath, const char *sName = nullptr);

    virtual void setTimeOverride(bool bOverride, std::time_t timeOverride) = 0;
    virtual void enableThreadAndFileInfo(bool bEnable) = 0;
    virtual void enableConsoleOutput(bool bEnable) = 0;
    virtual void logva(LogLevel level, const char* sFile, unsigned uLine, const char* func, const char* fmt, ...) = 0;
    virtual void shutdown() = 0;

    // Log level filtering
    virtual void setLogLevel(LogLevel minLevel) = 0;
    virtual LogLevel getLogLevel() const = 0;

    // Observer pattern methods
    virtual void addObserver(std::shared_ptr<ILogObserver> pObserver) = 0;
    virtual void removeObserver(std::shared_ptr<ILogObserver> pObserver) = 0;
};

#define LOG_VERBOSE(fmt,...) ILog::getInterface()->logva(LogLevel::eVerbose, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt,...) ILog::getInterface()->logva(LogLevel::eInfo, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt,...) ILog::getInterface()->logva(LogLevel::eWarning, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt,...) ILog::getInterface()->logva(LogLevel::eError, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
