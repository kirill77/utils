#pragma once

#include "utils/sparseMath/SparseLU.h"
#include "utils/sparseMath/SteepestEdgePricer.h"

#include <vector>
#include <cstddef>
#include <functional>

namespace sparseMath {

enum class LPStatus
{
    OPTIMAL,
    INFEASIBLE,
    UNBOUNDED,
    MAX_ITERATIONS
};

struct LPResult
{
    LPStatus status = LPStatus::INFEASIBLE;
    double objectiveValue = 0.0;
    std::vector<double> solution;
};

/// Diagnostic snapshot passed to the progress callback each reporting cycle.
struct ProgressInfo
{
    int phase        = 0;   ///< 1 = Phase I (feasibility), 2 = Phase II (optimality)
    int iteration    = 0;   ///< Iteration count within this phase
    double sumArt    = 0.0; ///< Sum of |artificial variable values| (Phase I only)
    int    numArt    = 0;   ///< Number of artificial variables still in basis (Phase I only)
    double maxArt    = 0.0; ///< Largest |artificial value| (Phase I only)
    double bestRC    = 0.0; ///< Largest |reduced cost| chosen by pricing
    double pivotStep = 0.0; ///< Step length from ratio test (0 = degenerate pivot)
    double maxWeight = 0.0; ///< Largest Devex weight among non-basic variables
    double avgWeight = 0.0; ///< Average Devex weight among non-basic variables
};

// Sparse LP solver: maximize c^T x  subject to  Ax = b,  lb <= x <= ub.
//
// The matrix A is stored in compressed sparse column (CSC) format.
// Supports warm-start from a previous basis for efficient re-solves
// after bound changes (e.g. sequential knockout simulations).
class RevisedSimplex
{
public:
    struct Params
    {
        double feasibilityTol = 1e-8;
        double optimalityTol  = 1e-8;
        int    maxIterations     = 100000;
        int    refactorInterval  = 50;
        int    progressInterval  = 100;  ///< Call progress callback every N iterations (0 = never)
        bool   bUseSteepestEdge = true; ///< Use steepest-edge pricing in Phase I (false = Dantzig)
    };

    using ProgressCallback = std::function<void(const ProgressInfo& info)>;

    RevisedSimplex() = default;

    void setProgressCallback(ProgressCallback cb) { m_progressCallback = std::move(cb); }

    void setParams(const Params& params);

    // Define the LP dimensions and sparse constraint matrix (CSC format).
    //   nRows    – number of equality constraints (rows of A)
    //   nCols    – number of variables (columns of A)
    //   colStart – CSC column pointer array, size nCols+1
    //   rowIdx   – CSC row index array, size colStart[nCols]
    //   values   – CSC value array,     size colStart[nCols]
    void loadProblem(int nRows, int nCols,
                     const std::vector<int>& colStart,
                     const std::vector<int>& rowIdx,
                     const std::vector<double>& values);

    void setObjective(const std::vector<double>& c);
    void setBounds(const std::vector<double>& lb,
                   const std::vector<double>& ub);
    void setRHS(const std::vector<double>& b);

    // Update bounds for a single variable without rebuilding the problem.
    void setVariableBounds(int col, double lb, double ub);

    int getNumCols() const { return m_nCols; }

    LPResult solve();

    // Warm-start: re-solve after bound changes, reusing the current basis.
    LPResult resolveWarmStart();

private:
    // Phase I: find an initial feasible basis using artificial variables.
    // Returns OPTIMAL if feasible basis found, INFEASIBLE otherwise.
    LPStatus phaseI();

    // Phase II: optimize from a feasible basis to optimality.
    // Returns OPTIMAL, UNBOUNDED, or MAX_ITERATIONS.
    LPStatus phaseII();

    // One simplex iteration: pricing, FTRAN, ratio test, basis update.
    // Returns 0 = pivot done, 1 = optimal, -1 = unbounded, -2 = degenerate cycle guard.
    int iterate(bool isPhaseI);

    void ftran(std::vector<double>& col) const;
    void btran(std::vector<double>& row) const;

    void recomputeXB();
    bool refactorizeBasis();

    void computeColumnInBasis(int col, std::vector<double>& result) const;
    void rebuildIsBasic();

    // Replace degenerate artificial basis columns (value ~0) with original
    // columns where possible.  Uses BTRAN per row + sparse dot products
    // to avoid brute-force FTRAN of every candidate column.
    void replaceArtificials();

    Params m_params;

    int m_nRows = 0;
    int m_nCols = 0;

    // CSC storage for constraint matrix A
    std::vector<int>    m_colStart;
    std::vector<int>    m_rowIdx;
    std::vector<double> m_values;

    std::vector<double> m_c;   // objective coefficients
    std::vector<double> m_lb;  // lower bounds
    std::vector<double> m_ub;  // upper bounds
    std::vector<double> m_b;   // RHS of equality constraints

    // Basis tracking
    std::vector<int> m_basisIndices;   // column index for each basis position
    bool m_hasBasis = false;

    // Revised simplex working state
    sparseMath::SparseLU m_lu;
    std::vector<double>  m_xB;              // basic variable values
    std::vector<int>     m_nonBasicStatus;  // 0 = at lower, 1 = at upper
    std::vector<bool>    m_isBasic;         // true for basic variables
    std::vector<double>  m_y;               // pricing vector workspace
    std::vector<double>  m_d;               // entering column workspace

    int m_pivotsSinceRefactor = 0;
    int m_pricingStart = 0;  // rotating start for partial Dantzig pricing
    ProgressCallback m_progressCallback;

    // Exact steepest-edge pricer (Goldfarb-Forrest)
    SteepestEdgePricer m_se;
    std::vector<double> m_rho;   // pivot row workspace: B^{-T} e_p
    std::vector<double> m_pi;    // SE workspace: B^{-T} d

    // Pricing mode: true = Dantzig (|rc|), false = steepest-edge (rc^2/gamma).
    // Set from Params::bUseSteepestEdge at solve() start; forced true for Phase II.
    bool m_bDantzigMode = false;

    // Last-iterate diagnostics (populated by iterate() for the callback)
    double m_lastBestRC    = 0.0;
    double m_lastPivotStep = 0.0;

    // Phase I artificial variable support
    std::vector<int>    m_artSign;       // +1 or -1 per row
    int                 m_nArtificials = 0;
};

} // namespace sparseMath
