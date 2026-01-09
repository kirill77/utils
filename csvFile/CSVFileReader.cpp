#include "Common.h"
#include "CSVFileReader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

CSVFileReader::CSVFileReader(const std::string& filename)
    : m_filename(filename)
{
    // Open the file for reading
    m_file.open(filename, std::ios::in);
    
    if (isValid()) {
        // Read headers if file is valid
        readHeaders();
        // Store position after headers for reset functionality
        m_dataStartPos = m_file.tellg();
    }
}

CSVFileReader::~CSVFileReader()
{
    // Ensure file is properly closed
    if (m_file.is_open()) {
        m_file.close();
    }
}

const std::vector<std::string>& CSVFileReader::getHeaders() const
{
    return m_headers;
}

bool CSVFileReader::readRow(std::vector<std::string>& values)
{
    values.clear();
    
    if (!isValid() || isEndOfFile()) {
        return false;
    }
    
    std::string line;
    if (!std::getline(m_file, line)) {
        return false;
    }
    
    // Handle empty lines
    if (line.empty()) {
        return readRow(values); // Try next line
    }
    
    bool success = parseLine(line, values);
    if (success) {
        m_currentRow++;
    }
    
    return success;
}

bool CSVFileReader::readRowAsNumbers(std::vector<double>& values)
{
    std::vector<std::string> stringValues;
    if (!readRow(stringValues)) {
        return false;
    }
    
    values.clear();
    values.reserve(stringValues.size());
    
    for (const auto& str : stringValues) {
        double value;
        if (!stringToDouble(str, value)) {
            values.clear();
            return false;
        }
        values.push_back(value);
    }
    
    return true;
}

size_t CSVFileReader::readAllRows(std::vector<std::vector<std::string>>& rows)
{
    rows.clear();
    std::vector<std::string> rowData;
    size_t count = 0;
    
    while (readRow(rowData)) {
        rows.push_back(rowData);
        count++;
    }
    
    return count;
}

size_t CSVFileReader::readAllRowsAsNumbers(std::vector<std::vector<double>>& rows)
{
    rows.clear();
    std::vector<double> rowData;
    size_t count = 0;
    
    while (readRowAsNumbers(rowData)) {
        rows.push_back(rowData);
        count++;
    }
    
    return count;
}

bool CSVFileReader::isValid() const
{
    return m_file.is_open() && m_file.good();
}

bool CSVFileReader::isEndOfFile() const
{
    return m_file.eof();
}

bool CSVFileReader::reset()
{
    if (!isValid()) {
        return false;
    }
    
    m_file.clear(); // Clear any error flags
    m_file.seekg(m_dataStartPos);
    m_currentRow = 0;
    
    return m_file.good();
}

size_t CSVFileReader::getColumnCount() const
{
    return m_headers.size();
}

void CSVFileReader::setDelimiter(char delimiter)
{
    m_delimiter = delimiter;
}

bool CSVFileReader::parseLine(const std::string& line, std::vector<std::string>& fields)
{
    fields.clear();
    
    if (line.empty()) {
        return true; // Empty line is valid, just no fields
    }
    
    std::string field;
    bool inQuotes = false;
    bool escapeNext = false;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (escapeNext) {
            field += c;
            escapeNext = false;
            continue;
        }
        
        if (c == '"') {
            if (inQuotes) {
                // Check if this is an escaped quote
                if (i + 1 < line.length() && line[i + 1] == '"') {
                    field += '"';
                    ++i; // Skip the next quote
                } else {
                    // End of quoted field
                    inQuotes = false;
                }
            } else {
                // Start of quoted field
                inQuotes = true;
            }
        } else if (c == m_delimiter && !inQuotes) {
            // End of field
            fields.push_back(unescapeField(field));
            field.clear();
        } else {
            field += c;
        }
    }
    
    // Add the last field
    fields.push_back(unescapeField(field));
    
    return true;
}

std::string CSVFileReader::unescapeField(const std::string& field) const
{
    if (field.empty()) {
        return field;
    }
    
    std::string result = field;
    
    // Remove surrounding quotes if present
    if (result.length() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.length() - 2);
        
        // Replace escaped quotes ("") with single quotes (")
        size_t pos = 0;
        while ((pos = result.find("\"\"", pos)) != std::string::npos) {
            result.replace(pos, 2, "\"");
            pos += 1;
        }
    }
    
    return result;
}

bool CSVFileReader::readHeaders()
{
    if (!isValid()) {
        return false;
    }
    
    std::string line;
    if (!std::getline(m_file, line)) {
        return false;
    }
    
    return parseLine(line, m_headers);
}

bool CSVFileReader::stringToDouble(const std::string& str, double& value) const
{
    if (str.empty()) {
        value = 0.0;
        return true;
    }
    
    // Trim whitespace
    std::string trimmed = str;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), trimmed.end());
    
    if (trimmed.empty()) {
        value = 0.0;
        return true;
    }
    
    try {
        size_t processed = 0;
        value = std::stod(trimmed, &processed);
        
        // Check if entire string was processed
        return processed == trimmed.length();
    } catch (const std::exception&) {
        return false;
    }
}