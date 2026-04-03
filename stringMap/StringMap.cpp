#include "utils/stringMap/StringMap.h"

#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------

void StringMap::set(const std::string& key, const std::string& value)
{
    m_data[key] = value;
}

std::string StringMap::get(const std::string& key, const std::string& defaultValue) const
{
    auto it = m_data.find(key);
    return (it != m_data.end()) ? it->second : defaultValue;
}

bool StringMap::has(const std::string& key) const
{
    return m_data.count(key) > 0;
}

void StringMap::remove(const std::string& key)
{
    m_data.erase(key);
}

// ---------------------------------------------------------------------------
// Typed convenience
// ---------------------------------------------------------------------------

void StringMap::setDouble(const std::string& key, double value)
{
    m_data[key] = std::to_string(value);
}

double StringMap::getDouble(const std::string& key, double defaultValue) const
{
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    try { return std::stod(it->second); }
    catch (...) { return defaultValue; }
}

void StringMap::setInt(const std::string& key, int value)
{
    m_data[key] = std::to_string(value);
}

int StringMap::getInt(const std::string& key, int defaultValue) const
{
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    try { return std::stoi(it->second); }
    catch (...) { return defaultValue; }
}

void StringMap::setBool(const std::string& key, bool value)
{
    m_data[key] = value ? "true" : "false";
}

bool StringMap::getBool(const std::string& key, bool defaultValue) const
{
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    return it->second == "true";
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

bool StringMap::save(const std::string& path) const
{
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "// Written by StringMap\n";
    for (const auto& [key, value] : m_data) {
        file << key << '=' << escapeValue(value) << '\n';
    }
    return file.good();
}

StringMap StringMap::load(const std::string& path)
{
    StringMap result;
    std::ifstream file(path);
    if (!file.is_open()) return result;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        // Strip // comments
        auto commentPos = line.find("//");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        if (line.empty()) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = unescapeValue(line.substr(eq + 1));
        result.m_data[key] = value;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Escaping
// ---------------------------------------------------------------------------

std::string StringMap::escapeValue(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

std::string StringMap::unescapeValue(const std::string& escaped)
{
    std::string out;
    out.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
        if (escaped[i] == '\\' && i + 1 < escaped.size()) {
            switch (escaped[i + 1]) {
                case '\\': out += '\\'; ++i; break;
                case 'n':  out += '\n'; ++i; break;
                case 'r':  out += '\r'; ++i; break;
                default:   out += escaped[i]; break;
            }
        } else {
            out += escaped[i];
        }
    }
    return out;
}
