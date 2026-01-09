#include "Common.h"
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <filesystem>
#include "fileUtils.h"
#include "utils/timeUtils/timeUtils.h"

bool FileUtils::findTheFolder(const std::string &sName, std::filesystem::path& _path)
{
    std::wstring buffer;
    buffer.resize(1024);
    GetModuleFileNameW(nullptr, &buffer[0], (DWORD)buffer.size());

    std::filesystem::path path = buffer;

    path.remove_filename();

    // go up the tree and find the folder
    for ( ; ; )
    {
        std::filesystem::path tmp = path;
        tmp.append(sName);
        if (std::filesystem::exists(tmp) && std::filesystem::is_directory(tmp))
        {
            _path = tmp.lexically_normal(); // Normalize the found path
            return true;
        }
        tmp = path.parent_path();
        if (tmp == path)
        {
            return false;
        }
        path = tmp;
    }
}

bool FileUtils::findTheFile(const std::wstring &fileName, std::filesystem::path &path, const std::vector<std::wstring> &searchPaths)
{
    // If no search paths provided, use default paths
    std::vector<std::wstring> paths = searchPaths;
    if (paths.empty())
    {
        // Get the executable directory
        std::wstring buffer;
        buffer.resize(1024);
        GetModuleFileNameW(nullptr, &buffer[0], (DWORD)buffer.size());
        std::filesystem::path exePath = buffer;
        exePath.remove_filename();
        exePath = exePath.lexically_normal(); // Normalize the executable path
        
        // Add default search paths (normalized to resolve .. components)
        paths.push_back(exePath.wstring());
        paths.push_back((exePath / L"..").lexically_normal().wstring());
        paths.push_back((exePath.parent_path() / L"../..").lexically_normal().wstring());
        paths.push_back((exePath.parent_path() / L"../../..").lexically_normal().wstring());
    }
    
    // Search in each path
    for (const auto &searchPath : paths)
    {
        std::filesystem::path fullPath = std::filesystem::path(searchPath) / fileName;
        fullPath = fullPath.lexically_normal(); // Normalize the full path
        if (std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath))
        {
            path = fullPath;
            return true;
        }
    }
    
    return false;
}

// Static storage for session timestamp (shared across all uses)
static std::time_t s_cachedRawTimestamp = 0;
static std::string s_cachedTimestampString;
static bool s_timestampInitialized = false;

std::time_t FileUtils::getSessionTimestamp()
{
    if (!s_timestampInitialized)
    {
        s_cachedRawTimestamp = std::time(nullptr);
        s_cachedTimestampString = TimeUtils::timeStampToString(s_cachedRawTimestamp, "%Y%m%d_%H%M%S");
        s_timestampInitialized = true;
    }
    return s_cachedRawTimestamp;
}

bool FileUtils::getOrCreateSubFolderUsingTimestamp(const std::string &baseFolder, std::filesystem::path &outPath)
{
    std::filesystem::path basePath;
    if (!findTheFolder(baseFolder, basePath)) {
        return false;
    }

    // Ensure timestamp is initialized (uses shared static storage)
    getSessionTimestamp();

    outPath = (basePath / s_cachedTimestampString).lexically_normal();
    std::error_code ec;
    std::filesystem::create_directories(outPath, ec); // ignore if already exists
    return true;
}
