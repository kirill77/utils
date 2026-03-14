#pragma once

#include <vector>

namespace sparseMath {

// Forrest-Tomlin eta vectors accumulated between LU refactorizations.
// Each eta vector records one basis column replacement (one simplex pivot).
class EtaFile
{
public:
    void addUpdate(int pivotRow, const std::vector<double>& enteringColDense, int m);
    void applyForward(std::vector<double>& rhs) const;
    void applyBackward(std::vector<double>& rhs) const;
    int  count() const { return static_cast<int>(m_etas.size()); }
    void clear() { m_etas.clear(); }

private:
    struct EtaVector
    {
        int pivotRow;
        std::vector<int>    indices;
        std::vector<double> values;
        double pivotValue;
    };

    std::vector<EtaVector> m_etas;
};

} // namespace sparseMath
