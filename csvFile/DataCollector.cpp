#include "DataCollector.h"
#include <fstream>
#include <iomanip>

DataCollector::DataCollector(const std::string& outputFile, const std::string& sRowVariableName)
    : m_outputFile(outputFile)
    , m_sRowVariableName(sRowVariableName)
{
}

DataCollector::~DataCollector()
{
    if (m_hasCurrentRow || m_rows.size() > m_savedRowCount)
        save();
}

void DataCollector::notifyNewRow(double fValue)
{
    if (m_hasCurrentRow)
        finalizeCurrentRow();

    // Start new row: slot 0 = row variable, slots 1..N = columns (filled with inherited values)
    m_currentRow.clear();
    m_currentRow.reserve(1 + m_columnOrder.size());
    m_currentRow.push_back(fValue);
    for (size_t i = 0; i < m_columnOrder.size(); ++i)
        m_currentRow.push_back(m_lastValues[i]);

    m_hasCurrentRow = true;
}

void DataCollector::notifyColumnValue(const std::string& sName, double fValue)
{
    if (!m_hasCurrentRow)
        return;

    auto it = m_columnIndex.find(sName);
    if (it == m_columnIndex.end()) {
        // New column discovered
        size_t idx = m_columnOrder.size();
        m_columnOrder.push_back(sName);
        m_columnIndex[sName] = idx;
        m_lastValues.push_back(0.0);

        // Expand all existing finalized rows with 0.0 for this new column
        for (auto& row : m_rows)
            row.push_back(0.0);

        // Expand current row
        m_currentRow.push_back(fValue);
    } else {
        // Slot index in m_currentRow is columnIndex + 1 (slot 0 is the row variable)
        m_currentRow[it->second + 1] = fValue;
    }
}

void DataCollector::save()
{
    if (m_hasCurrentRow)
        finalizeCurrentRow();

    if (m_rows.empty())
        return;

    bool newColumns = (m_columnOrder.size() != m_columnCountAtLastSave);

    if (newColumns || m_savedRowCount == 0) {
        writeAllRows();
    } else if (m_rows.size() > m_savedRowCount) {
        appendRows(m_savedRowCount);
    }

    m_savedRowCount         = m_rows.size();
    m_columnCountAtLastSave = m_columnOrder.size();
}

// --- private ---

void DataCollector::finalizeCurrentRow()
{
    if (!m_hasCurrentRow)
        return;

    // Update inheritance vector
    for (size_t i = 0; i < m_columnOrder.size(); ++i)
        m_lastValues[i] = m_currentRow[i + 1];

    m_rows.push_back(std::move(m_currentRow));
    m_hasCurrentRow = false;
}

void DataCollector::writeAllRows()
{
    std::ofstream f(m_outputFile, std::ios::trunc);
    if (!f.is_open())
        return;

    f << std::fixed << std::setprecision(m_precision);

    // Header
    f << m_sRowVariableName;
    for (const auto& col : m_columnOrder)
        f << ',' << col;
    f << '\n';

    // Data
    for (const auto& row : m_rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) f << ',';
            f << row[i];
        }
        f << '\n';
    }
}

void DataCollector::appendRows(size_t fromRow)
{
    std::ofstream f(m_outputFile, std::ios::app);
    if (!f.is_open())
        return;

    f << std::fixed << std::setprecision(m_precision);

    for (size_t r = fromRow; r < m_rows.size(); ++r) {
        const auto& row = m_rows[r];
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) f << ',';
            f << row[i];
        }
        f << '\n';
    }
}
