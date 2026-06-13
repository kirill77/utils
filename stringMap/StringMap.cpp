#include "utils/stringMap/StringMap.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    writeTo(file);
    return file.good();
}

StringMap StringMap::load(const std::string& path)
{
    StringMap result;
    std::ifstream file(path);
    if (!file.is_open()) return result;
    result.parseFrom(file);
    return result;
}

std::string StringMap::toString() const
{
    std::ostringstream out;
    writeTo(out);
    return out.str();
}

StringMap StringMap::parseString(const std::string& text)
{
    StringMap result;
    std::istringstream in(text);
    result.parseFrom(in);
    return result;
}

void StringMap::writeTo(std::ostream& out) const
{
    for (const auto& [key, value] : m_data) {
        out << key << '=' << escapeValue(value) << '\n';
    }
}

void StringMap::parseFrom(std::istream& in)
{
    std::string line;
    while (std::getline(in, line)) {
        // getline splits on '\n'; CRLF input leaves a trailing '\r' that would
        // otherwise be kept in the value (e.g. "vulkan\r"). Real '\r' in a value
        // arrives escaped as "\r", so dropping a bare trailing one is safe.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = unescapeValue(line.substr(eq + 1));
        m_data[key] = value;
    }
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

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------

bool StringMap::parseArgs(int argc, char** argv,
                          const std::vector<std::string>& acceptedKeys,
                          const std::string& usage,
                          const std::vector<std::string>& flagKeys)
{
    auto isAccepted = [&](const std::string& key) {
        return std::find(acceptedKeys.begin(), acceptedKeys.end(), key) != acceptedKeys.end();
    };
    auto isFlag = [&](const std::string& key) {
        return std::find(flagKeys.begin(), flagKeys.end(), key) != flagKeys.end();
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];

        if (a == "--help" || a == "-h")
        {
            std::printf("%s", usage.c_str());
            std::exit(0);
        }

        if (a.size() < 3 || a[0] != '-' || a[1] != '-')
        {
            std::fprintf(stderr, "Expected --flag, got '%s'\n", a.c_str());
            return false;
        }

        std::string key = a.substr(2);
        std::string value;
        bool hasInlineValue = false;

        // Support --key=value form
        const auto eq = key.find('=');
        if (eq != std::string::npos)
        {
            value = key.substr(eq + 1);
            key   = key.substr(0, eq);
            hasInlineValue = true;
        }

        if (isFlag(key))
        {
            if (hasInlineValue)
            {
                std::fprintf(stderr, "Flag --%s does not take a value\n", key.c_str());
                return false;
            }
            set(key, "1");
            continue;
        }

        if (!isAccepted(key))
        {
            std::fprintf(stderr, "Unknown flag: --%s\n", key.c_str());
            return false;
        }

        if (!hasInlineValue)
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "Missing value for --%s\n", key.c_str());
                return false;
            }
            value = argv[++i];
        }

        set(key, value);
    }
    return true;
}
