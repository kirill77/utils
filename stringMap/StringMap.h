#pragma once

#include <string>
#include <map>

// A flat string key-value map with file I/O.
//
// File format (.txt):
//   // Written by StringMap
//   key=value
//   key2=value2
//
// Lines starting with or containing '//' are treated as comments;
// everything from '//' to end of line is stripped before parsing.
//
// Escaping (values only):
//   backslash  -> \\
//   newline    -> \n
//   carriage return -> \r
//
// Keys must not contain '=' or newlines.
// On read, the first '=' splits key from value.

class StringMap
{
public:
    StringMap() = default;

    // --- Access ---

    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key, const std::string& defaultValue = "") const;
    bool has(const std::string& key) const;
    void remove(const std::string& key);

    const std::map<std::string, std::string>& data() const { return m_data; }

    bool empty() const { return m_data.empty(); }
    size_t size() const { return m_data.size(); }

    // --- Typed convenience ---

    void setDouble(const std::string& key, double value);
    double getDouble(const std::string& key, double defaultValue = 0.0) const;

    void setInt(const std::string& key, int value);
    int getInt(const std::string& key, int defaultValue = 0) const;

    void setBool(const std::string& key, bool value);
    bool getBool(const std::string& key, bool defaultValue = false) const;

    // --- File I/O ---

    bool save(const std::string& path) const;
    static StringMap load(const std::string& path);

private:
    static std::string escapeValue(const std::string& raw);
    static std::string unescapeValue(const std::string& escaped);

    std::map<std::string, std::string> m_data;
};
