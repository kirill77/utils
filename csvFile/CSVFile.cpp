#include "pch.h"
#include "CSVFile.h"
#include <fstream>
#include <sstream>
#include <iomanip>

CSVFile::CSVFile(const std::string& filename, const std::vector<std::string>& headers)
    : m_filename(filename)
    , m_headers(headers)
{
    // Open the file for writing
    m_file.open(filename, std::ios::out | std::ios::trunc);
    
    if (isValid()) {
        // Write the header row
        std::vector<std::string> escapedHeaders;
        for (const auto& header : headers) {
            escapedHeaders.push_back(escapeString(header));
        }
        
        addRow(escapedHeaders);
    }
}

CSVFile::~CSVFile()
{
    // Ensure file is properly closed
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool CSVFile::addRow(const std::vector<std::string>& values)
{
    if (!isValid()) {
        return false;
    }
    
    std::ostringstream row;
    
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            row << m_delimiter;
        }
        
        row << escapeString(values[i]);
    }
    
    return writeRow(row.str());
}

bool CSVFile::addRow(const std::vector<double>& values)
{
    if (!isValid()) {
        return false;
    }
    
    std::ostringstream row;
    row << std::fixed << std::setprecision(m_precision);
    
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            row << m_delimiter;
        }
        
        row << values[i];
    }
    
    return writeRow(row.str());
}

void CSVFile::flush()
{
    if (isValid()) {
        m_file.flush();
    }
}

bool CSVFile::isValid() const
{
    return m_file.is_open() && m_file.good();
}

void CSVFile::setDelimiter(char delimiter)
{
    m_delimiter = delimiter;
}

void CSVFile::setPrecision(int precision)
{
    m_precision = precision;
}

std::string CSVFile::escapeString(const std::string& str) const
{
    // If the string contains delimiter, newline, or quotes,
    // we need to escape it by enclosing in quotes and escaping quotes
    bool needsEscaping = false;
    
    for (char c : str) {
        if (c == m_delimiter || c == '\n' || c == '\r' || c == '"') {
            needsEscaping = true;
            break;
        }
    }
    
    if (!needsEscaping) {
        return str;
    }
    
    // Replace " with ""
    std::string escaped = str;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.replace(pos, 1, "\"\"");
        pos += 2;
    }
    
    // Enclose in quotes
    return "\"" + escaped + "\"";
}

bool CSVFile::writeRow(const std::string& row)
{
    if (!isValid()) {
        return false;
    }
    
    m_file << row << std::endl;
    
    return m_file.good();
} 