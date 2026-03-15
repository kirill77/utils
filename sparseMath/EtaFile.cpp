#include "utils/sparseMath/EtaFile.h"

#include <cassert>
#include <cmath>

namespace sparseMath {

void EtaFile::clear()
{
    m_indices.clear();
    m_values.clear();
    m_starts.clear();
    m_starts.push_back(0);
    m_pivotRows.clear();
    m_pivotValues.clear();
}

void EtaFile::addUpdate(int pivotRow, const std::vector<double>& enteringColDense, int m)
{
    assert(pivotRow >= 0 && pivotRow < m && "addUpdate: pivotRow out of range");
    assert(std::fabs(enteringColDense[pivotRow]) > 1e-15 &&
           "addUpdate: zero pivot value");

    if (m_starts.empty())
        m_starts.push_back(0);

    m_pivotRows.push_back(pivotRow);
    m_pivotValues.push_back(enteringColDense[pivotRow]);

    for (int i = 0; i < m; ++i)
    {
        if (i != pivotRow && std::fabs(enteringColDense[i]) > 1e-15)
        {
            m_indices.push_back(i);
            m_values.push_back(enteringColDense[i]);
        }
    }
    m_starts.push_back(static_cast<int>(m_indices.size()));
}

void EtaFile::applyForward(std::vector<double>& rhs) const
{
    int n = count();
    for (int k = 0; k < n; ++k)
    {
        int pivotRow = m_pivotRows[k];
        double yPivot = rhs[pivotRow] / m_pivotValues[k];
        rhs[pivotRow] = yPivot;

        int start = m_starts[k];
        int end   = m_starts[k + 1];
        for (int p = start; p < end; ++p)
            rhs[m_indices[p]] -= m_values[p] * yPivot;
    }
}

void EtaFile::applyBackward(std::vector<double>& rhs) const
{
    for (int k = count() - 1; k >= 0; --k)
    {
        int pivotRow = m_pivotRows[k];
        double sum = 0.0;

        int start = m_starts[k];
        int end   = m_starts[k + 1];
        for (int p = start; p < end; ++p)
            sum += m_values[p] * rhs[m_indices[p]];

        rhs[pivotRow] = (rhs[pivotRow] - sum) / m_pivotValues[k];
    }
}

} // namespace sparseMath
