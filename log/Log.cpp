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
    MyLog(const char *sName)
    {
        if (!sName || sName[0] == '\0')
        {
            AllocConsole();
            SetConsoleTitleA("KirillLog");
            m_outHandle = GetStdHandle(STD_OUTPUT_HANDLE);
        }
        else
        {
            m_sLogPath = createLogFileName(sName);
            FILE* fp = nullptr;
            // create the log name
            fopen_s(&fp, m_sLogPath.c_str(), "wt");
            if (fp)
            {
                fclose(fp);
            }
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
        if (m_sLogPath.size() == 0)
        {
            // Set attribute for newly written text
            {
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
        else
        {
            FILE* fp = nullptr;
            fopen_s(&fp, m_sLogPath.c_str(), "a+");
            if (fp)
            {
                fprintf(fp, "%s", finalMessage.c_str());
                fclose(fp);
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

ILog* ILog::getInterface(const char *sName)
{
    if (sName && sName[0] != '\0')
    {
        auto it = m_pLogs.find(sName);
        if (it != m_pLogs.end())
            return it->second;
        ILog *pLog = new MyLog(sName);
        m_pLogs[sName] = pLog;
        return pLog;
    }
    if (g_pInterface == nullptr)
    {
        g_pInterface = new MyLog("");
    }
    return g_pInterface;
}