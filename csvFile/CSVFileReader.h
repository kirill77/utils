#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <sstream>
#include <variant>

/**
 * @class CSVFileReader
 * @brief A class for easily reading data from CSV files
 * 
 * Allows for reading headers and rows of data from a CSV file with proper
 * parsing and handling of various data types.
 */
class CSVFileReader
{
public:
    /**
     * @brief Constructor that opens a file for reading
     * @param filename The path to the CSV file to read
     */
    explicit CSVFileReader(const std::string& filename);
    
    /**
     * @brief Destructor that ensures the file is properly closed
     */
    ~CSVFileReader();
    
    /**
     * @brief Get the headers from the CSV file
     * @return Vector of header strings, empty if no headers or file invalid
     */
    const std::vector<std::string>& getHeaders() const;
    
    /**
     * @brief Read the next row from the CSV file as strings
     * @param values Output vector to store the row values
     * @return true if a row was successfully read, false if EOF or error
     */
    bool readRow(std::vector<std::string>& values);
    
    /**
     * @brief Read the next row from the CSV file and convert to doubles
     * @param values Output vector to store the row values as doubles
     * @return true if a row was successfully read and converted, false otherwise
     */
    bool readRowAsNumbers(std::vector<double>& values);
    
    /**
     * @brief Read all remaining rows from the CSV file as strings
     * @param rows Output vector to store all rows
     * @return Number of rows read
     */
    size_t readAllRows(std::vector<std::vector<std::string>>& rows);
    
    /**
     * @brief Read all remaining rows from the CSV file and convert to doubles
     * @param rows Output vector to store all rows as doubles
     * @return Number of rows read and successfully converted
     */
    size_t readAllRowsAsNumbers(std::vector<std::vector<double>>& rows);
    
    /**
     * @brief Check if file is open and valid for reading
     * @return true if the file is open and valid, false otherwise
     */
    bool isValid() const;
    
    /**
     * @brief Check if we've reached the end of the file
     * @return true if at end of file, false otherwise
     */
    bool isEndOfFile() const;
    
    /**
     * @brief Reset file position to beginning (after headers)
     * @return true if successful, false otherwise
     */
    bool reset();
    
    /**
     * @brief Get the number of columns in the CSV file
     * @return Number of columns based on header count
     */
    size_t getColumnCount() const;
    
    /**
     * @brief Set the delimiter character (default is ',')
     * @param delimiter The character to use as delimiter
     */
    void setDelimiter(char delimiter);
    
    /**
     * @brief Get the filename of this CSV file
     * @return The path to the CSV file
     */
    const std::string& getFilename() const { return m_filename; }
    
    /**
     * @brief Get the current row number (1-based, excluding header)
     * @return Current row number
     */
    size_t getCurrentRowNumber() const { return m_currentRow; }

private:
    std::string m_filename;                ///< Path to the CSV file
    std::ifstream m_file;                  ///< File stream for reading
    std::vector<std::string> m_headers;    ///< Column headers
    char m_delimiter = ',';                ///< Delimiter character
    size_t m_currentRow = 0;               ///< Current row number (0-based)
    std::streampos m_dataStartPos;         ///< Position where data starts (after headers)
    
    /**
     * @brief Parse a CSV line into individual fields
     * @param line The line to parse
     * @param fields Output vector to store parsed fields
     * @return true if parsing was successful, false otherwise
     */
    bool parseLine(const std::string& line, std::vector<std::string>& fields);
    
    /**
     * @brief Unescape a CSV field (remove quotes and handle escaped quotes)
     * @param field The field to unescape
     * @return The unescaped field
     */
    std::string unescapeField(const std::string& field) const;
    
    /**
     * @brief Read the headers from the file
     * @return true if headers were successfully read, false otherwise
     */
    bool readHeaders();
    
    /**
     * @brief Convert a string to double with error checking
     * @param str The string to convert
     * @param value Output parameter for the converted value
     * @return true if conversion was successful, false otherwise
     */
    bool stringToDouble(const std::string& str, double& value) const;
};