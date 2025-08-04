#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct FileUtils
{
    static bool findTheFolder(const std::string &sName, std::filesystem::path &path);
    static bool findFile(const std::wstring &fileName, std::filesystem::path &path, const std::vector<std::wstring> &searchPaths = {});
};
