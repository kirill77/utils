#include "utils/sparseMath/EtaFile.h"

#include <cmath>

namespace sparseMath {

void EtaFile::addUpdate(int pivotRow, const std::vector<double>& enteringColDense, int m)
{
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
    // Apply eta vectors in forward order (FTRAN, between L-solve and U-solve).
    // Each eta represents: E_k = I + (eta_col - e_{pivotRow}) * e_{pivotRow}^T
    // Solving E_k * x = rhs:
    //   x[pivotRow] = (rhs[pivotRow] - sum(eta[i]*rhs[i] for off-pivot)) / pivotValue
    //   x[i] = rhs[i]  for i != pivotRow
    for (const auto& eta : m_etas)
    {
        double sum = 0.0;
        for (size_t k = 0; k < eta.indices.size(); ++k)
            sum += eta.values[k] * rhs[eta.indices[k]];

        rhs[eta.pivotRow] = (rhs[eta.pivotRow] - sum) / eta.pivotValue;
    }
}

void EtaFile::applyBackward(std::vector<double>& rhs) const
{
    // Apply eta vectors in reverse order (BTRAN).
    // E_k^T * y = rhs:
    //   y[pivotRow] = rhs[pivotRow] / pivotValue
    //   y[i] = rhs[i] - eta[i] * y[pivotRow]  for off-pivot i
    for (int j = static_cast<int>(m_etas.size()) - 1; j >= 0; --j)
    {
        const auto& eta = m_etas[j];
        double yPivot = rhs[eta.pivotRow] / eta.pivotValue;
        rhs[eta.pivotRow] = yPivot;

        for (size_t k = 0; k < eta.indices.size(); ++k)
            rhs[eta.indices[k]] -= eta.values[k] * yPivot;
    }
}

} // namespace sparseMath
