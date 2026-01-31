// Log.cpp : Defines the functions for the static library.
//

#include <string>
#include <assert.h>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <algorithm>
#include "utils/fileUtils/fileUtils.h"
#include "utils/timeUtils/timeUtils.h"
#include "ILog.h"

static std::string createLogFileName(const char *sName)
{
    // Get the current time
    std::time_t now = std::time(nullptr);

    // Create a tm structure to hold the local time
    std::tm timeInfo;
    localtime_s(&timeInfo, &now);

    std::filesystem::path path;
    FileUtils::findTheFolder("logs", path);
    std::string sPath = path.string();
    sPath += "\\";
    sPath += sName;

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "_%Y-%m-%d_%H-%M-%S.log", &timeInfo);

    sPath += buffer;

    return sPath;
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
            FILE* pFile = nullptr;
            fopen_s(&pFile, m_sLogPath.c_str(), "wt");
            if (pFile)
            {
                fclose(pFile);
            }
        }
        else
        {
            // Console logging (no path specified)
            AllocConsole();
            SetConsoleTitleA("KirillLog");
            m_pOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
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
        if (bEnable && m_pOutHandle == nullptr)
        {
            // Initialize console if enabling and not already initialized
            AllocConsole();
            SetConsoleTitleA("KirillLog");
            m_pOutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        else if (!bEnable && m_pOutHandle != nullptr && !m_sLogPath.empty())
        {
            // Disable console output (only if we have a log file, don't disable console-only mode)
            FreeConsole();
            m_pOutHandle = nullptr;
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
            int msgSize = vsnprintf(&msg[0], msg.size(), fmt, args);
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
            char buffer[256];
            sprintf_s(buffer, "[%s](%d)[%s[%d]] ", sTime.c_str(), GetCurrentThreadId(), sFile, uLine);
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
            FILE* pFile = nullptr;
            fopen_s(&pFile, m_sLogPath.c_str(), "a+");
            if (pFile)
            {
                fprintf(pFile, "%s", finalMessage.c_str());
                fclose(pFile);
            }
        }
        
        // Write to console if handle is initialized
        if (m_pOutHandle != nullptr)
        {
            // Set attribute for newly written text
            switch (level)
            {
            case LogLevel::eVerbose:
                SetConsoleTextAttribute(m_pOutHandle, FOREGROUND_BLUE | FOREGROUND_GREEN); // Cyan for verbose
                break;
            case LogLevel::eInfo:
                SetConsoleTextAttribute(m_pOutHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
                break;
            case LogLevel::eWarning:
                SetConsoleTextAttribute(m_pOutHandle, FOREGROUND_GREEN | FOREGROUND_RED);
                break;
            case LogLevel::eError:
                SetConsoleTextAttribute(m_pOutHandle, FOREGROUND_RED);
                break;
            default:
                assert(false);
            }
            DWORD OutChars;
            WriteConsoleA(m_pOutHandle, finalMessage.c_str(), (DWORD)finalMessage.length(), &OutChars, nullptr);
            if (level != LogLevel::eInfo)
            {
                SetConsoleTextAttribute(m_pOutHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
            }
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

    HANDLE m_pOutHandle = nullptr;
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
