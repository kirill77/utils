#include "pch.h"
#include <stdio.h>
#include <stdarg.h>
#include <string>
#include <filesystem>
#include "framework.h"
#include "fileUtils.h"

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
            _path = tmp;
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

bool FileUtils::findFile(const std::wstring &fileName, std::filesystem::path &path, const std::vector<std::wstring> &searchPaths)
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
        
        // Add default search paths
        paths.push_back(exePath.wstring());
        paths.push_back((exePath / L"..").wstring());
        paths.push_back((exePath.parent_path() / L"../..").wstring());
    }
    
    // Search in each path
    for (const auto &searchPath : paths)
    {
        std::filesystem::path fullPath = std::filesystem::path(searchPath) / fileName;
        if (std::filesystem::exists(fullPath) && std::filesystem::is_regular_file(fullPath))
        {
            path = fullPath;
            return true;
        }
    }
    
    return false;
}
