// Log.cpp : Defines the functions for the static library.
//

#include "pch.h"
#include <string>
#include <assert.h>
#include <unordered_map>
#include <mutex>
#include "utils/fileUtils/fileUtils.h"
#include "utils/timeUtils/timeUtils.h"
#include "framework.h"
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
            FILE* fp = nullptr;
            fopen_s(&fp, m_sLogPath.c_str(), "wt");
            if (fp)
            {
                fclose(fp);
            }
        }
        else
        {
            // Console logging (no path specified)
            AllocConsole();
            SetConsoleTitleA("KirillLog");
            m_outHandle = GetStdHandle(STD_OUTPUT_HANDLE);
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
        if (bEnable && m_outHandle == nullptr)
        {
            // Initialize console if enabling and not already initialized
            AllocConsole();
            SetConsoleTitleA("KirillLog");
            m_outHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        else if (!bEnable && m_outHandle != nullptr && !m_sLogPath.empty())
        {
            // Disable console output (only if we have a log file, don't disable console-only mode)
            FreeConsole();
            m_outHandle = nullptr;
        }
    }
    virtual void logva(LogLevel level, const char* sFile, unsigned uLine, const char* , const char* fmt, ...) override
    {
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

private:

    void print(LogLevel level, const char *sFile, unsigned uLine, const std::string& logMessage)
    {
        // create prefix for the message
        std::time_t currentTime = m_bTimeOverride ? m_timeOverride : std::time(nullptr);
        std::string sTime = TimeUtils::timeStampToString(currentTime);
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
            FILE* fp = nullptr;
            fopen_s(&fp, m_sLogPath.c_str(), "a+");
            if (fp)
            {
                fprintf(fp, "%s", finalMessage.c_str());
                fclose(fp);
            }
        }
        
        // Write to console if handle is initialized
        if (m_outHandle != nullptr)
        {
            // Set attribute for newly written text
            switch (level)
            {
            case LogLevel::eInfo:
                SetConsoleTextAttribute(m_outHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
                break;
            case LogLevel::eWarning:
                SetConsoleTextAttribute(m_outHandle, FOREGROUND_GREEN | FOREGROUND_RED);
                break;
            case LogLevel::eError:
                SetConsoleTextAttribute(m_outHandle, FOREGROUND_RED);
                break;
            default:
                assert(false);
            }
            DWORD OutChars;
            WriteConsoleA(m_outHandle, finalMessage.c_str(), (DWORD)finalMessage.length(), &OutChars, nullptr);
            if (level != LogLevel::eInfo)
            {
                SetConsoleTextAttribute(m_outHandle, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
            }
        }
    }

    HANDLE m_outHandle = nullptr;
    std::mutex m_mutex;
    std::string m_sLogPath;

    bool m_bTimeOverride = false;
    std::time_t m_timeOverride = 0;

    bool m_bEnableThreadAndFileInfo = true;
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
        return pLog;
    }
    if (g_pInterface)
    {
        assert(false); // why there are two calls to ILog::create()
        return g_pInterface;
    }
    g_pInterface = new MyLog(sPath.c_str(), sName);
    return g_pInterface;
}
