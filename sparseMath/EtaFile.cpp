#include "utils/sparseMath/EtaFile.h"

#include <cassert>
#include <cmath>

namespace sparseMath {

void EtaFile::addUpdate(int pivotRow, const std::vector<double>& enteringColDense, int m)
{
    assert(pivotRow >= 0 && pivotRow < m && "addUpdate: pivotRow out of range");
    assert(std::fabs(enteringColDense[pivotRow]) > 1e-15 &&
           "addUpdate: zero pivot value");

    EtaVector eta;
    eta.pivotRow = pivotRow;
    eta.pivotValue = enteringColDense[pivotRow];

    for (int i = 0; i < m; ++i)
    {
        if (i != pivotRow && std::fabs(enteringColDense[i]) > 1e-15)
        {
            eta.indices.push_back(i);
            eta.values.push_back(enteringColDense[i]);
        }
    }
    m_etas.push_back(std::move(eta));
}

void EtaFile::applyForward(std::vector<double>& rhs) const
{
    // Apply E_1^{-1}, E_2^{-1}, ..., E_k^{-1} in forward order (FTRAN).
    // E^{-1} * x:
    //   x[pivotRow] = x[pivotRow] / pivotValue
    //   x[i] -= eta[i] * x[pivotRow]   for off-pivot i
    for (const auto& eta : m_etas)
    {
        double yPivot = rhs[eta.pivotRow] / eta.pivotValue;
        rhs[eta.pivotRow] = yPivot;

        for (size_t k = 0; k < eta.indices.size(); ++k)
            rhs[eta.indices[k]] -= eta.values[k] * yPivot;
    }
}

void EtaFile::applyBackward(std::vector<double>& rhs) const
{
    // Apply E_k^{-T}, E_{k-1}^{-T}, ..., E_1^{-T} in reverse order (BTRAN).
    // E^{-T} * x:
    //   x[pivotRow] = (x[pivotRow] - sum(eta[i]*x[i] for off-pivot)) / pivotValue
    //   x[i] unchanged   for off-pivot i
    for (int j = static_cast<int>(m_etas.size()) - 1; j >= 0; --j)
    {
        const auto& eta = m_etas[j];
        double sum = 0.0;
        for (size_t k = 0; k < eta.indices.size(); ++k)
            sum += eta.values[k] * rhs[eta.indices[k]];

        rhs[eta.pivotRow] = (rhs[eta.pivotRow] - sum) / eta.pivotValue;
    }
}

} // namespace sparseMath
