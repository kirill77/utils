#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>

/**
 * @class CSVFile
 * @brief A class for easily writing data to CSV files during simulation
 * 
 * Allows for writing headers and rows of data to a CSV file with proper
 * formatting and handling of various data types.
 */
class CSVFile
{
public:
    /**
     * @brief Constructor that opens a file for writing and adds headers
     * @param filename The path to the CSV file to create or overwrite
     * @param headers The column headers for the CSV file
     */
    CSVFile(const std::string& filename, const std::vector<std::string>& headers);
    
    /**
     * @brief Destructor that ensures the file is properly closed
     */
    ~CSVFile();
    
    /**
     * @brief Add a row of string data to the CSV file
     * @param values The values to add as a row
     * @return true if the operation succeeded, false otherwise
     */
    bool addRow(const std::vector<std::string>& values);
    
    /**
     * @brief Add a row of numeric (double) data to the CSV file
     * @param values The values to add as a row
     * @return true if the operation succeeded, false otherwise
     */
    bool addRow(const std::vector<double>& values);
    
    /**
     * @brief Add a mixed row of data to the CSV file
     * @param values The values to add as a row
     * @return true if the operation succeeded, false otherwise
     */
    template<typename... Args>
    bool addMixedRow(const Args&... args);
    
    /**
     * @brief Flush data to disk immediately
     */
    void flush();
    
    /**
     * @brief Check if file is open and valid
     * @return true if the file is open and valid, false otherwise
     */
    bool isValid() const;
    
    /**
     * @brief Set the delimiter character (default is ',')
     * @param delimiter The character to use as delimiter
     */
    void setDelimiter(char delimiter);
    
    /**
     * @brief Set the precision for floating-point numbers
     * @param precision The number of decimal places to show
     */
    void setPrecision(int precision);
    
    /**
     * @brief Get the filename of this CSV file
     * @return The path to the CSV file
     */
    const std::string& getFilename() const { return m_filename; }

private:
    std::string m_filename;                ///< Path to the CSV file
    std::ofstream m_file;                  ///< File stream for writing
    std::vector<std::string> m_headers;    ///< Column headers
    char m_delimiter = ',';                ///< Delimiter character
    int m_precision = 6;                   ///< Decimal precision for floating-point values
    
    /**
     * @brief Escape a string to ensure proper CSV formatting
     * @param str The string to escape
     * @return The escaped string
     */
    std::string escapeString(const std::string& str) const;
    
    /**
     * @brief Write a row to the CSV file
     * @param row The row data as a string
     * @return true if successful, false otherwise
     */
    bool writeRow(const std::string& row);
};

// Template implementation
template<typename... Args>
bool CSVFile::addMixedRow(const Args&... args)
{
    std::ostringstream row;
    int idx = 0;
    
    // Function to process each argument
    auto processArg = [&](const auto& arg) {
        if (idx > 0) {
            row << m_delimiter;
        }
        
        using ArgType = std::decay_t<decltype(arg)>;
        
        if constexpr (std::is_same_v<ArgType, std::string> || 
                      std::is_same_v<ArgType, const char*>) {
            row << escapeString(arg);
        }
        else if constexpr (std::is_floating_point_v<ArgType>) {
            row << std::fixed << std::setprecision(m_precision) << arg;
        }
        else {
            row << arg;
        }
        
        idx++;
    };
    
    // Process all arguments
    (processArg(args), ...);
    
    return writeRow(row.str());
} 