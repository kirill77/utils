#pragma once

#include <vector>

namespace sparseMath {

// Forrest-Tomlin eta vectors accumulated between LU refactorizations.
// Each eta vector records one basis column replacement (one simplex pivot).
//
// Storage uses flat concatenated arrays instead of per-eta heap allocations.
class EtaFile
{
public:
    void addUpdate(int pivotRow, const std::vector<double>& enteringColDense, int m);
    void applyForward(std::vector<double>& rhs) const;
    void applyBackward(std::vector<double>& rhs) const;
    int  count() const { return static_cast<int>(m_pivotRows.size()); }
    void clear();

private:
    std::vector<int>    m_indices;     // all eta indices concatenated
    std::vector<double> m_values;      // all eta values concatenated
    std::vector<int>    m_starts;      // start offset per eta (size = count+1)
    std::vector<int>    m_pivotRows;   // pivot row per eta
    std::vector<double> m_pivotValues; // pivot value per eta
};

} // namespace sparseMath
