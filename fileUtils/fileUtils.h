#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <ctime>

struct FileUtils
{
    static bool findTheFolder(const std::string &sName, std::filesystem::path &path);
    static bool findTheFile(const std::wstring &fileName, std::filesystem::path &path, const std::vector<std::wstring> &searchPaths = {});
    static bool getOrCreateSubFolderUsingTimestamp(const std::string &baseFolder, std::filesystem::path &outPath);
    
    // Get the session timestamp (same value used for log folder naming)
    // Initializes on first call if not already initialized
    // Use this for session IDs to enable cross-referencing with log files
    static std::time_t getSessionTimestamp();
};
