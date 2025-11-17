#include "Parameters.h"
#include "utils/log/ILog.h"
#include "utils/fileUtils/fileUtils.h"
#include "utils/csvFile/CSVFileReader.h"
#include "utils/csvFile/CSVFileWriter.h"
#include <filesystem>

Parameters::Parameters(const std::filesystem::path& filePath)
    : m_filePath(filePath)
{
    LOG_INFO("Parameters: Created with file path: %s", filePath.string().c_str());
}

Parameters::~Parameters() {
    // Auto-save on destruction if dirty
    if (m_isDirty) {
        save();
    }
}

// Type-specific setters
void Parameters::setInt(const std::string& key, int value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.insert_or_assign(key, ParamValue(value));
    m_isDirty = true;
    m_revision++;
}

void Parameters::setDouble(const std::string& key, double value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.insert_or_assign(key, ParamValue(value));
    m_isDirty = true;
    m_revision++;
}

void Parameters::setBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.insert_or_assign(key, ParamValue(value));
    m_isDirty = true;
    m_revision++;
}

void Parameters::setString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.insert_or_assign(key, ParamValue(value));
    m_isDirty = true;
    m_revision++;
}

// Type-specific getters with defaults
int Parameters::getInt(const std::string& key, int defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return defaultValue;
    }
    
    if (it->second.getType() != ParamType::INT) {
        LOG_ERROR("Parameters: Type mismatch for key '%s': expected INT, got %s",
                  key.c_str(), it->second.getTypeName().c_str());
        return defaultValue;
    }
    
    try {
        return it->second.asInt();
    } catch (const std::exception& e) {
        LOG_ERROR("Parameters: Failed to get INT value for key '%s': %s", key.c_str(), e.what());
        return defaultValue;
    }
}

double Parameters::getDouble(const std::string& key, double defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return defaultValue;
    }
    
    if (it->second.getType() != ParamType::DOUBLE) {
        LOG_ERROR("Parameters: Type mismatch for key '%s': expected DOUBLE, got %s",
                  key.c_str(), it->second.getTypeName().c_str());
        return defaultValue;
    }
    
    try {
        return it->second.asDouble();
    } catch (const std::exception& e) {
        LOG_ERROR("Parameters: Failed to get DOUBLE value for key '%s': %s", key.c_str(), e.what());
        return defaultValue;
    }
}

bool Parameters::getBool(const std::string& key, bool defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return defaultValue;
    }
    
    if (it->second.getType() != ParamType::BOOL) {
        LOG_ERROR("Parameters: Type mismatch for key '%s': expected BOOL, got %s",
                  key.c_str(), it->second.getTypeName().c_str());
        return defaultValue;
    }
    
    try {
        return it->second.asBool();
    } catch (const std::exception& e) {
        LOG_ERROR("Parameters: Failed to get BOOL value for key '%s': %s", key.c_str(), e.what());
        return defaultValue;
    }
}

std::string Parameters::getString(const std::string& key, const std::string& defaultValue) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        return defaultValue;
    }
    
    if (it->second.getType() != ParamType::STRING) {
        LOG_ERROR("Parameters: Type mismatch for key '%s': expected STRING, got %s",
                  key.c_str(), it->second.getTypeName().c_str());
        return defaultValue;
    }
    
    try {
        return it->second.asString();
    } catch (const std::exception& e) {
        LOG_ERROR("Parameters: Failed to get STRING value for key '%s': %s", key.c_str(), e.what());
        return defaultValue;
    }
}

// Check if key exists
bool Parameters::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.find(key) != m_data.end();
}

// Get type of a parameter
ParamType Parameters::getType(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_data.find(key);
    if (it == m_data.end()) {
        throw std::invalid_argument("Parameter key not found: " + key);
    }
    
    return it->second.getType();
}

// Get all keys
std::vector<std::string> Parameters::getKeys() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<std::string> keys;
    keys.reserve(m_data.size());
    
    for (const auto& [key, value] : m_data) {
        keys.push_back(key);
    }
    
    return keys;
}

// Load from CSV file
bool Parameters::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if file exists
    if (!std::filesystem::exists(m_filePath)) {
        LOG_INFO("Parameters: File does not exist: %s - starting with empty parameters",
                 m_filePath.string().c_str());
        return false;
    }
    
    // Open CSV file for reading
    CSVFileReader reader(m_filePath.string());
    if (!reader.isValid()) {
        LOG_ERROR("Parameters: Failed to open file for reading: %s", m_filePath.string().c_str());
        return false;
    }
    
    // Read parameters from CSV
    // Expected format: Key, Type, Value
    std::vector<std::string> row;
    size_t loadedCount = 0;
    
    while (reader.readRow(row)) {
        // Expect 3 columns: Key, Type, Value
        if (row.size() < 3) {
            LOG_WARN("Parameters: Invalid row with %zu columns (expected 3), skipping", row.size());
            continue;
        }
        
        const std::string& key = row[0];
        const std::string& typeStr = row[1];
        const std::string& valueStr = row[2];
        
        if (key.empty()) {
            LOG_WARN("Parameters: Empty key in CSV, skipping");
            continue;
        }
        
        try {
            ParamType type = ParamValue::stringToType(typeStr);
            m_data.insert_or_assign(key, ParamValue(valueStr, type));
            loadedCount++;
        } catch (const std::exception& e) {
            LOG_ERROR("Parameters: Failed to parse parameter '%s': %s", key.c_str(), e.what());
            continue;
        }
    }
    
    // Reset dirty flag after successful load
    m_isDirty = false;
    m_lastSavedRevision = m_revision;
    
    LOG_INFO("Parameters: Loaded %zu parameters from %s", loadedCount, m_filePath.string().c_str());
    return true;
}

// Save to CSV file
bool Parameters::save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Check if dirty
    if (!m_isDirty && m_revision == m_lastSavedRevision) {
        LOG_INFO("Parameters: Skipping save - no changes (revision %llu)", m_revision);
        return false;
    }
    
    // Create parent directory if it doesn't exist
    std::filesystem::path parentPath = m_filePath.parent_path();
    if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
        std::error_code ec;
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            LOG_ERROR("Parameters: Failed to create directory %s: %s",
                      parentPath.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    
    // Create CSV writer with headers
    CSVFileWriter writer(m_filePath.string(), {"Key", "Type", "Value"});
    if (!writer.isValid()) {
        LOG_ERROR("Parameters: Failed to open file for writing: %s", m_filePath.string().c_str());
        return false;
    }
    
    // Write each parameter
    for (const auto& [key, value] : m_data) {
        writer.addRow({
            key,
            value.getTypeName(),
            value.toString()
        });
    }
    
    // Update state
    m_isDirty = false;
    m_lastSavedRevision = m_revision;
    
    LOG_INFO("Parameters: Saved %zu parameters to %s (revision %llu)",
             m_data.size(), m_filePath.string().c_str(), m_revision);
    return true;
}

