#pragma once

#include "utils/sparseMath/SparseLU.h"

#include <vector>
#include <cassert>

namespace sparseMath {

/// Exact steepest-edge pricer for the Revised Simplex method.
///
/// Maintains exact edge weights gamma_j = ||B^{-1} a_j||^2 for non-basic
/// variables using the Goldfarb-Forrest update formula.  Pricing selects
/// the variable maximizing rc_j^2 / gamma_j.
///
/// Weights are recomputed from scratch at every basis refactorization to
/// eliminate numerical drift.
///
/// Reference: Goldfarb & Forrest (1992), "Steepest-edge simplex algorithms
/// for linear programming".
class SteepestEdgePricer
{
public:
    SteepestEdgePricer() = default;

    /// Reset all weights to 1.0.
    void reset(int nTotalCols);

    /// Initialize with exact weights for an all-artificial basis (Phase I).
    /// For original columns: gamma_j = ||a_j||^2 (since B^{-1} = diag(artSign)).
    /// For artificial columns: gamma_j = 1.0.
    void initializeColumnNorms(int nCols, int nArtificials,
                               const std::vector<int>& colStart,
                               const std::vector<int>& rowIdx,
                               const std::vector<double>& values);

    /// Recompute exact weights from scratch: gamma_j = ||B^{-1} a_j||^2
    /// for every non-basic variable, using FTRAN through the current basis.
    /// Called at each refactorization to eliminate accumulated drift.
    void recomputeExactWeights(const SparseLU& lu,
                               const std::vector<bool>& isBasic,
                               int nCols, int nArtificials,
                               const std::vector<int>& colStart,
                               const std::vector<int>& rowIdx,
                               const std::vector<double>& values,
                               const std::vector<int>& artSign,
                               int nRows);

    /// Return the edge weight for column j.
    double weight(int j) const;

    /// Steepest-edge pricing score: rc^2 / gamma.  Higher is better.
    double score(double reducedCost, int j) const;

    /// Update weights after a basis pivot using the exact Goldfarb-Forrest
    /// formula.
    void updateAfterPivot(int enterCol, int leaveCol,
                          double pivotElem, double dNormSq,
                          const std::vector<double>& alphas,
                          const std::vector<double>& taus,
                          const std::vector<bool>& isBasic,
                          int nTotalCols);

private:
    std::vector<double> m_weights;
    static constexpr double MIN_WEIGHT = 1e-4;
};

} // namespace sparseMath
