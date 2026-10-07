// Log.cpp : Defines the functions for the static library.
//

#include <string>
#include <assert.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include "utils/fileUtils/fileUtils.h"
#include "utils/timeUtils/timeUtils.h"
#include "ILog.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace
{
// Console output: a Win32 console with text attributes on Windows, stdout
// (ANSI-coloured when it is a terminal) elsewhere.
#ifdef _WIN32
void openConsole()
{
    AllocConsole();
    SetConsoleTitleA("KirillLog");
}

void closeConsole()
{
    FreeConsole();
}

void writeConsole(LogLevel level, const std::string& sMessage)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    const WORD white = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
    WORD attribute = white;
    switch (level)
    {
    case LogLevel::eVerbose: attribute = FOREGROUND_BLUE | FOREGROUND_GREEN; break; // cyan
    case LogLevel::eInfo:    attribute = white; break;
    case LogLevel::eWarning: attribute = FOREGROUND_GREEN | FOREGROUND_RED; break;  // yellow
    case LogLevel::eError:   attribute = FOREGROUND_RED; break;
    default: assert(false);
    }
    SetConsoleTextAttribute(hOut, attribute);
    DWORD outChars;
    WriteConsoleA(hOut, sMessage.c_str(), (DWORD)sMessage.length(), &outChars, nullptr);
    if (level != LogLevel::eInfo)
    {
        SetConsoleTextAttribute(hOut, white);
    }
}

unsigned long currentThreadId()
{
    return GetCurrentThreadId();
}
#else
void openConsole()
{
}

void closeConsole()
{
}

void writeConsole(LogLevel level, const std::string& sMessage)
{
    static const bool bColor = isatty(STDOUT_FILENO);
    const char* sColor = "";
    switch (level)
    {
    case LogLevel::eVerbose: sColor = "\033[36m"; break; // cyan
    case LogLevel::eInfo:    sColor = ""; break;
    case LogLevel::eWarning: sColor = "\033[33m"; break; // yellow
    case LogLevel::eError:   sColor = "\033[31m"; break; // red
    default: assert(false);
    }
    if (bColor && sColor[0] != '\0')
    {
        std::fprintf(stdout, "%s%s\033[0m", sColor, sMessage.c_str());
    }
    else
    {
        std::fputs(sMessage.c_str(), stdout);
    }
    std::fflush(stdout);
}

unsigned long currentThreadId()
{
    return (unsigned long)syscall(SYS_gettid);
}
#endif

// Truncates (or creates) the file at sPath.
void createEmptyFile(const std::string& sPath)
{
    std::ofstream file(sPath, std::ios::trunc);
}
}

static std::string createLogFileName(const char *sName)
{
    // Get the current time
    std::time_t now = std::time(nullptr);

    std::tm timeInfo = TimeUtils::timeStampToLocalTM(now);

    std::filesystem::path path;
    FileUtils::findTheFolder("logs", path);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "_%Y-%m-%d_%H-%M-%S.log", &timeInfo);

    return (path / (std::string(sName) + buffer)).string();
}

struct MyLog : public ILog
{
    MyLog(const char *sPath, const char *sName = nullptr)
    {
        // Determine the log path: direct path takes priority over generated name
        if (sPath && sPath[0] != '\0')
        {
            m_sLogPath = sPath;
        }
        else if (sName && sName[0] != '\0')
        {
            m_sLogPath = createLogFileName(sName);
        }
        
        // If we have a file path, create the log file
        if (!m_sLogPath.empty())
        {
            createEmptyFile(m_sLogPath);
        }
        else
        {
            // Console logging (no path specified)
            openConsole();
            m_bConsoleOutput = true;
        }
    }

    virtual void setTimeOverride(bool bOverride, std::time_t timeOverride) override
    {
        m_bTimeOverride = bOverride;
        m_timeOverride = timeOverride;
    }
    virtual void enableThreadAndFileInfo(bool bEnable) override
    {
        m_bEnableThreadAndFileInfo = bEnable;
    }
    virtual void enableConsoleOutput(bool bEnable) override
    {
        if (bEnable && !m_bConsoleOutput)
        {
            // Initialize console if enabling and not already initialized
            openConsole();
            m_bConsoleOutput = true;
        }
        else if (!bEnable && m_bConsoleOutput && !m_sLogPath.empty())
        {
            // Disable console output (only if we have a log file, don't disable console-only mode)
            closeConsole();
            m_bConsoleOutput = false;
        }
    }
    
	virtual void setLogLevel(LogLevel minLevel) override
	{
		m_minLogLevel = minLevel;
	}
	
	virtual LogLevel getLogLevel() const override
	{
		return m_minLogLevel;
	}
    
    virtual void logva(LogLevel level, const char* sFile, unsigned uLine, const char* , const char* fmt, ...) override
    {
        // Filter messages below the minimum log level
        // Note: Reading m_minLogLevel without a lock is safe because:
        // 1. Reading an enum is atomic on all modern architectures
        // 2. Worst case: we read a stale value and filter one message incorrectly
        // 3. Performance: avoiding mutex overhead on every log call is critical
        if (static_cast<unsigned>(level) < static_cast<unsigned>(m_minLogLevel))
        {
            return;  // Skip this message
        }
        
        va_list args;
        va_start(args, fmt);
        std::string msg;
        msg.resize(128);

        for ( ; ; )
        {
            // vsnprintf consumes the va_list it is given, so each attempt formats from a fresh copy.
            va_list argsCopy;
            va_copy(argsCopy, args);
            int msgSize = vsnprintf(&msg[0], msg.size(), fmt, argsCopy);
            va_end(argsCopy);
            if (msgSize > 0 && msgSize < msg.size() - 5)
            {
                msg.resize(msgSize);
                break;
            }
            if (msg.size() > 10000)
            {
                assert(false); // something wrong
                msg.resize(msgSize);
                break;
            }
            msg.resize(msg.size() * 2);
        }

        va_end(args);

        msg.push_back('\n');
        print(level, sFile, uLine, msg);
    }
    virtual void shutdown()
    {
    }

    virtual std::string getLogPath() const override
    {
        return m_sLogPath;
    }

    virtual void switchLogPath(const std::string& newPath) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool bNewFile = !std::filesystem::exists(newPath);
        m_sLogPath = newPath;
        if (bNewFile)
        {
            // Create the new log file only if it doesn't already exist
            createEmptyFile(m_sLogPath);
        }
    }

    // Observer pattern implementation
    virtual void addObserver(std::shared_ptr<ILogObserver> pObserver) override
    {
        if (!pObserver) return;
        std::lock_guard<std::mutex> lock(m_observerMutex);
        m_observers.push_back(pObserver);
    }

    virtual void removeObserver(std::shared_ptr<ILogObserver> pObserver) override
    {
        if (!pObserver) return;
        std::lock_guard<std::mutex> lock(m_observerMutex);
        m_observers.erase(
            std::remove_if(m_observers.begin(), m_observers.end(),
                [&pObserver](const std::weak_ptr<ILogObserver>& wp) {
                    auto sp = wp.lock();
                    return !sp || sp == pObserver;
                }),
            m_observers.end()
        );
    }

private:

    void print(LogLevel level, const char *sFile, unsigned uLine, const std::string& logMessage)
    {
        // create prefix for the message
        std::time_t currentTime = m_bTimeOverride ? m_timeOverride : std::time(nullptr);
        std::string sTime = TimeUtils::timeStampToLocalString(currentTime);
        std::string finalMessage;

        if (m_bEnableThreadAndFileInfo)
        {
            const char* levelPrefix = "";
            if (level == LogLevel::eWarning) levelPrefix = "WARN: ";
            else if (level == LogLevel::eError) levelPrefix = "ERROR: ";

            char buffer[256];
            std::snprintf(buffer, sizeof(buffer), "[%s](%lu)[%s[%u]] %s", sTime.c_str(), currentThreadId(), sFile, uLine, levelPrefix);
            finalMessage = std::string(buffer) + logMessage;
        }
        else
        {
            finalMessage = logMessage;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Write to file if path is specified
        if (m_sLogPath.size() > 0)
        {
            std::ofstream file(m_sLogPath, std::ios::app);
            file << finalMessage;
        }

        if (m_bConsoleOutput)
        {
            writeConsole(level, finalMessage);
        }

        // Notify observers (with automatic cleanup of expired weak_ptrs)
        notifyObservers(level, logMessage, sTime);
    }

    void notifyObservers(LogLevel level, const std::string& message, const std::string& timestamp)
    {
        std::lock_guard<std::mutex> lock(m_observerMutex);
        
        // Notify all valid observers and remove expired ones
        auto it = m_observers.begin();
        while (it != m_observers.end())
        {
            if (auto pObserver = it->lock())
            {
                // Observer is still alive, notify it
                pObserver->onLogMessage(level, message, timestamp);
                ++it;
            }
            else
            {
                // Observer has been destroyed, remove from list
                it = m_observers.erase(it);
            }
        }
    }

    bool m_bConsoleOutput = false;
    mutable std::mutex m_mutex;
    std::string m_sLogPath;

    bool m_bTimeOverride = false;
    std::time_t m_timeOverride = 0;

    bool m_bEnableThreadAndFileInfo = true;
    
    // Log level filtering (default: show all messages)
    LogLevel m_minLogLevel = LogLevel::eInfo;

    // Observer pattern members
    std::vector<std::weak_ptr<ILogObserver>> m_observers;
    std::mutex m_observerMutex;
};

static std::unordered_map<std::string, ILog*> m_pLogs;
static ILog* g_pInterface = nullptr;
static std::mutex g_logMapMutex;

ILog* ILog::getInterface(const char *sName)
{
    std::lock_guard<std::mutex> lock(g_logMapMutex);
    
    if (sName && sName[0] != '\0')
    {
        auto it = m_pLogs.find(sName);
        if (it != m_pLogs.end())
            return it->second;
        ILog *pLog = new MyLog(nullptr, sName);
        m_pLogs[sName] = pLog;
        // Also set as default interface so getInterface() without name returns this log
        if (!g_pInterface)
        {
            g_pInterface = pLog;
        }
        return pLog;
    }
    if (g_pInterface == nullptr)
    {
        g_pInterface = new MyLog(nullptr, nullptr);
    }
    return g_pInterface;
}

ILog* ILog::create(const std::string &sPath, const char *sName)
{
    std::lock_guard<std::mutex> lock(g_logMapMutex);

    if (sName && sName[0] != '\0')
    {
        // Check if a log with this key already exists
        auto it = m_pLogs.find(sName);
        if (it != m_pLogs.end())
        {
            assert(false); // why there are two calls to ILog::create()
            return it->second;
        }
        // Create new log instance using the unified constructor
        ILog* pLog = new MyLog(sPath.c_str(), sName);
        m_pLogs[sName] = pLog;
        // Also set as default interface so getInterface() without name returns this log
        if (!g_pInterface)
        {
            g_pInterface = pLog;
        }
        return pLog;
    }
    if (g_pInterface)
    {
        assert(false); // why there are two calls to ILog::create()
        return g_pInterface;
    }
    g_pInterface = new MyLog(sPath.c_str(), nullptr);
    return g_pInterface;
}
