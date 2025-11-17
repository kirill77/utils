#pragma once

#include "ParamValue.h"
#include <string>
#include <map>
#include <vector>
#include <filesystem>
#include <mutex>
#include <cstdint>

// Type-aware parameter storage with CSV persistence
// Thread-safe parameter management with automatic dirty tracking
class Parameters {
public:
    explicit Parameters(const std::filesystem::path& filePath);
    ~Parameters();
    
    // Delete copy/move constructors and assignment operators
    Parameters(const Parameters&) = delete;
    Parameters& operator=(const Parameters&) = delete;
    Parameters(Parameters&&) = delete;
    Parameters& operator=(Parameters&&) = delete;
    
    // Type-specific setters
    void setInt(const std::string& key, int value);
    void setDouble(const std::string& key, double value);
    void setBool(const std::string& key, bool value);
    void setString(const std::string& key, const std::string& value);
    
    // Type-specific getters with defaults
    int getInt(const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    
    // Check if key exists
    bool has(const std::string& key) const;
    
    // Get type of a parameter (throws if key doesn't exist)
    ParamType getType(const std::string& key) const;
    
    // Get all keys
    std::vector<std::string> getKeys() const;
    
    // Persistence
    bool load();  // Load from CSV file
    bool save();  // Save to CSV file (only if dirty)
    
    // Change tracking
    uint64_t getRevision() const { return m_revision; }
    bool isDirty() const { return m_isDirty; }
    
private:
    std::filesystem::path m_filePath;
    std::map<std::string, ParamValue> m_data;
    mutable std::mutex m_mutex;
    uint64_t m_revision = 0;
    bool m_isDirty = false;
    uint64_t m_lastSavedRevision = 0;
};

