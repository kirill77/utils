#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct FileUtils
{
    static bool findTheFolder(const std::string &sName, std::filesystem::path &path);
    static bool findTheFile(const std::wstring &fileName, std::filesystem::path &path, const std::vector<std::wstring> &searchPaths = {});
    static bool getOrCreateSubFolderUsingTimestamp(const std::string &baseFolder, std::filesystem::path &outPath);
};
