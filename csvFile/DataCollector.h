#pragma once

#include <string>
#include <vector>
#include <unordered_map>

/**
 * @class DataCollector
 * @brief Generic CSV data accumulator — knows nothing about what data it collects.
 *
 * Callers drive rows via notifyNewRow() and push named column values via
 * notifyColumnValue(). Columns are discovered dynamically in first-seen order.
 * Missing values inherit from the previous row. Call save() periodically or
 * let the destructor handle it.
 */
class DataCollector
{
public:
    DataCollector(const std::string& outputFile, const std::string& sRowVariableName);
    ~DataCollector();

    void notifyNewRow(double fValue);
    void notifyColumnValue(const std::string& sName, double fValue);
    void save();

    size_t getRowCount() const { return m_rows.size() + (m_hasCurrentRow ? 1 : 0); }

private:
    void finalizeCurrentRow();
    void writeAllRows();
    void appendRows(size_t fromRow);

    std::string m_outputFile;
    std::string m_sRowVariableName;

    // Column tracking (excludes the row variable, which is always column 0)
    std::vector<std::string>                 m_columnOrder;
    std::unordered_map<std::string, size_t>  m_columnIndex;

    // Buffered data — each inner vector is [rowValue, col0, col1, ...]
    std::vector<std::vector<double>> m_rows;

    // Current (in-progress) row
    std::vector<double> m_currentRow;
    bool                m_hasCurrentRow = false;

    // Inheritance: last known value per column
    std::vector<double> m_lastValues;

    // Save bookkeeping
    size_t m_savedRowCount          = 0;
    size_t m_columnCountAtLastSave  = 0;
    int    m_precision              = 6;
};
