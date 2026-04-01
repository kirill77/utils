#include "utils/sparseMath/SteepestEdgePricer.h"

#include <cmath>
#include <algorithm>

namespace sparseMath {

void SteepestEdgePricer::reset(int nTotalCols)
{
    m_weights.assign(nTotalCols, 1.0);
}

void SteepestEdgePricer::initializeColumnNorms(
    int nCols, int nArtificials,
    const std::vector<int>& colStart,
    const std::vector<int>& rowIdx,
    const std::vector<double>& values)
{
    int nTotal = nCols + nArtificials;
    m_weights.resize(nTotal);

    // Original columns: gamma_j = ||a_j||^2
    // (Exact for all-artificial basis where B^{-1} = diag(artSign), artSign^2 = 1)
    for (int j = 0; j < nCols; ++j)
    {
        double normSq = 0.0;
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
            normSq += values[p] * values[p];
        m_weights[j] = std::max(MIN_WEIGHT, normSq);
    }

    // Artificial columns: single nonzero of +/-1, so gamma = 1.0
    for (int j = nCols; j < nTotal; ++j)
        m_weights[j] = 1.0;
}

void SteepestEdgePricer::recomputeExactWeights(
    const SparseLU& lu,
    const std::vector<bool>& isBasic,
    int nCols, int nArtificials,
    const std::vector<int>& colStart,
    const std::vector<int>& rowIdx,
    const std::vector<double>& values,
    const std::vector<int>& artSign,
    int nRows)
{
    int nTotal = nCols + nArtificials;
    m_weights.resize(nTotal);

    std::vector<double> col(nRows);

    for (int j = 0; j < nTotal; ++j)
    {
        if (isBasic[j])
        {
            m_weights[j] = 1.0;  // placeholder for basic vars (unused)
            continue;
        }

        // Build column a_j
        std::fill(col.begin(), col.end(), 0.0);
        if (j < nCols)
        {
            for (int p = colStart[j]; p < colStart[j + 1]; ++p)
                col[rowIdx[p]] = values[p];
        }
        else
        {
            int artRow = j - nCols;
            col[artRow] = artSign.empty() ? 1.0 : static_cast<double>(artSign[artRow]);
        }

        // FTRAN: col = B^{-1} a_j
        lu.solveRight(col);

        // gamma_j = ||B^{-1} a_j||^2
        double normSq = 0.0;
        for (int i = 0; i < nRows; ++i)
            normSq += col[i] * col[i];

        m_weights[j] = std::max(MIN_WEIGHT, normSq);
    }
}

double SteepestEdgePricer::weight(int j) const
{
    assert(j >= 0 && j < static_cast<int>(m_weights.size()));
    return m_weights[j];
}

double SteepestEdgePricer::score(double reducedCost, int j) const
{
    assert(j >= 0 && j < static_cast<int>(m_weights.size()));
    return reducedCost * reducedCost / m_weights[j];
}

void SteepestEdgePricer::updateAfterPivot(
    int enterCol, int leaveCol,
    double pivotElem, double dNormSq,
    const std::vector<double>& alphas,
    const std::vector<double>& taus,
    const std::vector<bool>& isBasic,
    int nTotalCols)
{
    double d_p  = pivotElem;
    double d_p2 = d_p * d_p;

    // sigma = ||d - e_p||^2 = ||d||^2 - 2*d_p + 1
    double sigma = dNormSq - 2.0 * d_p + 1.0;

    // Leaving variable: exact weight
    //   gamma_l = (1 + ||d||^2 - d_p^2) / d_p^2
    double rawLeaveW = (1.0 + dNormSq - d_p2) / d_p2;
    m_weights[leaveCol] = std::max(MIN_WEIGHT, rawLeaveW);

    // Goldfarb-Forrest update for all other non-basic variables:
    //   gamma_j_new = gamma_j - 2*(alpha_j/d_p)*(tau_j - alpha_j)
    //                 + (alpha_j/d_p)^2 * sigma
    for (int j = 0; j < nTotalCols; ++j)
    {
        if (isBasic[j] || j == leaveCol)
            continue;

        double a_over_dp = alphas[j] / d_p;
        double raw = m_weights[j]
                   - 2.0 * a_over_dp * (taus[j] - alphas[j])
                   + a_over_dp * a_over_dp * sigma;

        m_weights[j] = std::max(MIN_WEIGHT, raw);
    }
}

} // namespace sparseMath
