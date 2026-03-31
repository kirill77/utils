#include "utils/sparseMath/RevisedSimplex.h"
#include "utils/sparseMath/SparseMathValidation.h"

#include <cassert>
#include <cmath>
#include <algorithm>
#include <limits>
#include <numeric>

namespace sparseMath {

static constexpr double INF = std::numeric_limits<double>::infinity();

// ---------------------------------------------------------------------------
// Setup methods
// ---------------------------------------------------------------------------

void RevisedSimplex::setParams(const Params& params)
{
    m_params = params;
}

void RevisedSimplex::loadProblem(int nRows, int nCols,
                                 const std::vector<int>& colStart,
                                 const std::vector<int>& rowIdx,
                                 const std::vector<double>& values)
{
    assert(nRows > 0 && "loadProblem: nRows must be positive");
    assert(nCols > 0 && "loadProblem: nCols must be positive");
    assert(static_cast<int>(colStart.size()) == nCols + 1 && "loadProblem: colStart size mismatch");
    assert(static_cast<int>(rowIdx.size()) == colStart[nCols] && "loadProblem: rowIdx size mismatch");
    assert(static_cast<int>(values.size()) == colStart[nCols] && "loadProblem: values size mismatch");

    m_nRows    = nRows;
    m_nCols    = nCols;
    m_colStart = colStart;
    m_rowIdx   = rowIdx;
    m_values   = values;
    m_hasBasis = false;
}

void RevisedSimplex::setObjective(const std::vector<double>& c)
{
    m_c = c;
}

void RevisedSimplex::setBounds(const std::vector<double>& lb,
                               const std::vector<double>& ub)
{
    assert(static_cast<int>(lb.size()) == m_nCols && "setBounds: lb size mismatch");
    assert(static_cast<int>(ub.size()) == m_nCols && "setBounds: ub size mismatch");

    m_lb = lb;
    m_ub = ub;
}

void RevisedSimplex::setRHS(const std::vector<double>& b)
{
    m_b = b;
}

void RevisedSimplex::setVariableBounds(int col, double lb, double ub)
{
    assert(col >= 0 && col < m_nCols && "setVariableBounds: col out of range");
    assert(lb <= ub && "setVariableBounds: lb > ub (inverted bounds)");

    m_lb[col] = lb;
    m_ub[col] = ub;
}

// ---------------------------------------------------------------------------
// solve - Phase I + Phase II
// ---------------------------------------------------------------------------

LPResult RevisedSimplex::solve()
{
    runValidation();

    LPResult result;

    LPStatus status = phaseI();
    if (status == LPStatus::INFEASIBLE)
    {
        result.status = LPStatus::INFEASIBLE;
        return result;
    }

    status = phaseII();
    result.status = status;

    if (status == LPStatus::OPTIMAL)
    {
        // Rebuild isBasic after phaseII may have changed the basis
        rebuildIsBasic();

        result.solution.assign(m_nCols, 0.0);

        // Non-basic variables at their bound
        for (int j = 0; j < m_nCols; ++j)
        {
            if (!m_isBasic[j])
            {
                if (m_nonBasicStatus[j] == 1)
                    result.solution[j] = m_ub[j];
                else
                    result.solution[j] = (m_lb[j] > -INF) ? m_lb[j] : 0.0;
            }
        }

        // Basic variable values (overwrites any non-basic value for basic cols)
        for (int i = 0; i < m_nRows; ++i)
        {
            int col = m_basisIndices[i];
            if (col < m_nCols)
                result.solution[col] = m_xB[i];
        }

        result.objectiveValue = 0.0;
        for (int j = 0; j < m_nCols; ++j)
            result.objectiveValue += m_c[j] * result.solution[j];
    }

    m_hasBasis = (status == LPStatus::OPTIMAL);
    return result;
}

// ---------------------------------------------------------------------------
// resolveWarmStart
// ---------------------------------------------------------------------------

LPResult RevisedSimplex::resolveWarmStart()
{
    if (!m_hasBasis)
        return solve();

    // Re-factorize with existing basis
    m_nArtificials = 0;
    m_artSign.clear();

    if (!refactorizeBasis())
        return solve();

    recomputeXB();

    // Check feasibility of current basis with new bounds
    bool bFeasible = true;
    for (int i = 0; i < m_nRows; ++i)
    {
        int col = m_basisIndices[i];
        if (col < m_nCols)
        {
            if (m_xB[i] < m_lb[col] - m_params.feasibilityTol ||
                m_xB[i] > m_ub[col] + m_params.feasibilityTol)
            {
                bFeasible = false;
                break;
            }
        }
    }

    if (!bFeasible)
        return solve();

    // Initialize non-basic status and isBasic
    rebuildIsBasic();
    m_nonBasicStatus.assign(m_nCols, 0);
    for (int j = 0; j < m_nCols; ++j)
    {
        if (!m_isBasic[j])
        {
            if (m_ub[j] < INF && m_lb[j] <= -INF)
                m_nonBasicStatus[j] = 1;
            else
                m_nonBasicStatus[j] = 0;
        }
    }

    // xB is stale after non-basic status changes — recompute from scratch.
    recomputeXB();

    LPStatus status = phaseII();

    LPResult result;
    result.status = status;

    if (status == LPStatus::OPTIMAL)
    {
        // Rebuild isBasic since phaseII changes the basis
        rebuildIsBasic();

        result.solution.assign(m_nCols, 0.0);
        for (int j = 0; j < m_nCols; ++j)
        {
            if (!m_isBasic[j])
            {
                if (m_nonBasicStatus[j] == 1)
                    result.solution[j] = m_ub[j];
                else
                    result.solution[j] = (m_lb[j] > -INF) ? m_lb[j] : 0.0;
            }
        }
        for (int i = 0; i < m_nRows; ++i)
        {
            int col = m_basisIndices[i];
            if (col < m_nCols)
                result.solution[col] = m_xB[i];
        }
        result.objectiveValue = 0.0;
        for (int j = 0; j < m_nCols; ++j)
            result.objectiveValue += m_c[j] * result.solution[j];
    }

    m_hasBasis = (status == LPStatus::OPTIMAL);
    return result;
}

// ---------------------------------------------------------------------------
// phaseI - find initial feasible basis with artificial variables
// ---------------------------------------------------------------------------

LPStatus RevisedSimplex::phaseI()
{
    m_pricingStart = 0;

    // Compute initial RHS accounting for non-basic variables at their bounds.
    m_nonBasicStatus.assign(m_nCols, 0);

    std::vector<double> rhs = m_b;

    for (int j = 0; j < m_nCols; ++j)
    {
        double val = 0.0;
        if (m_lb[j] > -INF)
            val = m_lb[j];
        else if (m_ub[j] < INF)
        {
            val = m_ub[j];
            m_nonBasicStatus[j] = 1;
        }

        // Subtract A*x_NB from rhs
        for (int p = m_colStart[j]; p < m_colStart[j + 1]; ++p)
            rhs[m_rowIdx[p]] -= m_values[p] * val;
    }

    // Add artificial variables: one per row.
    m_nArtificials = m_nRows;
    m_artSign.resize(m_nRows);
    for (int i = 0; i < m_nRows; ++i)
        m_artSign[i] = (rhs[i] >= 0.0) ? 1 : -1;

    // Basis = artificial variables (columns m_nCols .. m_nCols+m_nRows-1)
    m_basisIndices.resize(m_nRows);
    for (int i = 0; i < m_nRows; ++i)
        m_basisIndices[i] = m_nCols + i;

    // Build isBasic: original vars are all non-basic
    rebuildIsBasic();

    // Factorize basis
    if (!refactorizeBasis())
        return LPStatus::INFEASIBLE;

    // xB[i] = rhs[i] * artSign[i]  (must be >= 0 by construction)
    m_xB.resize(m_nRows);
    for (int i = 0; i < m_nRows; ++i)
    {
        m_xB[i] = rhs[i] * m_artSign[i];
        assert(m_xB[i] >= -1e-12 && "phaseI: artificial variable started negative");
    }

    // Allocate workspace vectors
    m_y.resize(m_nRows);
    m_d.resize(m_nRows);

    int iterCount = 0;
    int unboundedCount = 0;

    while (iterCount < m_params.maxIterations)
    {
        // Check if all artificials have left the basis
        bool bArtInBasis = false;
        double sumArt = 0.0;
        for (int i = 0; i < m_nRows; ++i)
        {
            if (m_basisIndices[i] >= m_nCols)
            {
                bArtInBasis = true;
                sumArt += std::fabs(m_xB[i]);
            }
        }

        if (!bArtInBasis || sumArt < m_params.feasibilityTol)
        {
            // Refactorize and recompute xB to clear accumulated numerical
            // noise before the replacement loop.  Without this the FTRAN
            // calls below use stale eta-updated factors, which can pick
            // columns that make the post-replacement basis singular.
            if (bArtInBasis)
            {
                if (!refactorizeBasis())
                    return LPStatus::INFEASIBLE;
                recomputeXB();

                // Re-check: numerical drift may have inflated sumArt
                // above the tolerance once xB is recomputed from scratch.
                double sumArtFresh = 0.0;
                for (int i = 0; i < m_nRows; ++i)
                    if (m_basisIndices[i] >= m_nCols)
                        sumArtFresh += std::fabs(m_xB[i]);
                if (sumArtFresh >= m_params.feasibilityTol)
                {
                    ++iterCount;
                    continue;   // not truly feasible yet — keep iterating
                }
            }

            // Save basis state before replacement attempt — if replacements
            // produce a near-singular basis we can fall back.
            std::vector<int> savedBasis = m_basisIndices;
            int savedNArt = m_nArtificials;
            std::vector<int> savedArtSign = m_artSign;

            // Replace any remaining artificial basis columns with original
            // columns (degenerate artificials at zero).
            std::vector<double> col(m_nRows, 0.0);
            for (int i = 0; i < m_nRows; ++i)
            {
                if (m_basisIndices[i] >= m_nCols)
                {
                    bool bReplaced = false;
                    for (int j = 0; j < m_nCols && !bReplaced; ++j)
                    {
                        if (m_isBasic[j])
                            continue;

                        // Check if column j has a nonzero in row i
                        computeColumnInBasis(j, col);
                        ftran(col);

                        if (std::fabs(col[i]) > 0.01)
                        {
                            int oldCol = m_basisIndices[i];
                            m_isBasic[oldCol] = false;
                            m_basisIndices[i] = j;
                            m_isBasic[j] = true;
                            m_xB[i] = 0.0;
                            if (!m_lu.update(i, col))
                            {
                                if (!refactorizeBasis())
                                    return LPStatus::INFEASIBLE;
                            }
                            else
                                m_pivotsSinceRefactor++;
                            bReplaced = true;
                        }
                    }
                }
            }
            // Count remaining artificials — some rows may be linearly dependent
            // and have no original column to replace the artificial.
            bool anyLeft = false;
            for (int i = 0; i < m_nRows; ++i)
                if (m_basisIndices[i] >= m_nCols) { anyLeft = true; break; }
            if (!anyLeft)
            {
                m_nArtificials = 0;
                m_artSign.clear();
            }
            rebuildIsBasic();
            if (!refactorizeBasis())
            {
                // Replacement loop produced a near-singular basis.
                // Fall back to the pre-replacement basis — artificials at
                // zero value are harmless in Phase II.
                m_basisIndices = savedBasis;
                m_nArtificials = savedNArt;
                m_artSign = savedArtSign;
                rebuildIsBasic();
                if (!refactorizeBasis())
                    return LPStatus::INFEASIBLE;
            }
            recomputeXB();
            return LPStatus::OPTIMAL;
        }

        int rc = iterate(true);
        if (rc == 1) // "optimal" for Phase I objective
        {
            // Refactorize and recompute xB to clear anti-cycling
            // perturbation noise before testing feasibility.
            if (!refactorizeBasis())
                return LPStatus::INFEASIBLE;
            recomputeXB();

            // Check if sum of artificials is zero
            double sumA = 0.0;
            for (int i = 0; i < m_nRows; ++i)
            {
                if (m_basisIndices[i] >= m_nCols)
                    sumA += std::fabs(m_xB[i]);
            }
            if (sumA < m_params.feasibilityTol)
            {
                // Save basis state before replacement attempt.
                std::vector<int> savedBasis2 = m_basisIndices;
                int savedNArt2 = m_nArtificials;
                std::vector<int> savedArtSign2 = m_artSign;

                // Replace any degenerate artificials (at value 0) with
                // original columns where possible.
                std::vector<double> col2(m_nRows, 0.0);
                for (int i = 0; i < m_nRows; ++i)
                {
                    if (m_basisIndices[i] >= m_nCols)
                    {
                        bool bReplaced = false;
                        for (int j = 0; j < m_nCols && !bReplaced; ++j)
                        {
                            if (m_isBasic[j])
                                continue;
                            computeColumnInBasis(j, col2);
                            ftran(col2);
                            if (std::fabs(col2[i]) > 0.01)
                            {
                                int oldCol = m_basisIndices[i];
                                m_isBasic[oldCol] = false;
                                m_basisIndices[i] = j;
                                m_isBasic[j] = true;
                                m_xB[i] = 0.0;
                                if (!m_lu.update(i, col2))
                                {
                                    if (!refactorizeBasis())
                                        return LPStatus::INFEASIBLE;
                                }
                                else
                                    m_pivotsSinceRefactor++;
                                bReplaced = true;
                            }
                        }
                    }
                }

                // Count remaining artificials — some rows may be linearly
                // dependent and have no original column to replace the
                // artificial.
                bool anyLeft = false;
                for (int i = 0; i < m_nRows; ++i)
                    if (m_basisIndices[i] >= m_nCols) { anyLeft = true; break; }
                if (!anyLeft)
                {
                    m_nArtificials = 0;
                    m_artSign.clear();
                }
                rebuildIsBasic();
                if (!refactorizeBasis())
                {
                    // Fall back to the pre-replacement basis.
                    m_basisIndices = savedBasis2;
                    m_nArtificials = savedNArt2;
                    m_artSign = savedArtSign2;
                    rebuildIsBasic();
                    if (!refactorizeBasis())
                        return LPStatus::INFEASIBLE;
                }
                recomputeXB();
                return LPStatus::OPTIMAL;
            }
            return LPStatus::INFEASIBLE;
        }
        if (rc == -1)
        {
            // Phase I cannot be truly unbounded (objective is bounded by 0).
            // This indicates numerical deterioration in the LU factors.
            // Refactorize and recompute xB to recover.
            if (!refactorizeBasis())
                return LPStatus::INFEASIBLE;
            recomputeXB();
            // If this keeps happening, give up
            ++unboundedCount;
            if (unboundedCount > 5)
                return LPStatus::INFEASIBLE;
        }

        ++iterCount;
        if (m_progressCallback)
        {
            ProgressInfo info;
            info.phase     = 1;
            info.iteration = iterCount;
            info.bestRC    = m_lastBestRC;
            info.pivotStep = m_lastPivotStep;
            for (int i = 0; i < m_nRows; ++i)
            {
                if (m_basisIndices[i] >= m_nCols)
                {
                    double av = std::fabs(m_xB[i]);
                    info.sumArt += av;
                    info.numArt++;
                    if (av > info.maxArt)
                        info.maxArt = av;
                }
            }
            m_progressCallback(info);
        }
    }

    return LPStatus::INFEASIBLE;
}

// ---------------------------------------------------------------------------
// phaseII - optimize from feasible basis
// ---------------------------------------------------------------------------

LPStatus RevisedSimplex::phaseII()
{
    m_pricingStart = 0;

    // Ensure workspace is allocated
    m_y.resize(m_nRows);
    m_d.resize(m_nRows);

    int iterCount = 0;
    while (iterCount < m_params.maxIterations)
    {
        int rc = iterate(false);
        if (rc == 1)
            return LPStatus::OPTIMAL;
        if (rc == -1)
            return LPStatus::UNBOUNDED;
        ++iterCount;
        if (m_progressCallback)
        {
            ProgressInfo info;
            info.phase     = 2;
            info.iteration = iterCount;
            info.bestRC    = m_lastBestRC;
            info.pivotStep = m_lastPivotStep;
            m_progressCallback(info);
        }
    }
    return LPStatus::MAX_ITERATIONS;
}

// ---------------------------------------------------------------------------
// iterate - one simplex pivot
// ---------------------------------------------------------------------------

int RevisedSimplex::iterate(bool isPhaseI)
{
    int nTotalCols = m_nCols + m_nArtificials;

    // BTRAN: compute pricing vector y = c_B^T * B^{-1}
    std::fill(m_y.begin(), m_y.end(), 0.0);
    if (isPhaseI)
    {
        // Phase I objective: minimize sum of artificial variables
        for (int i = 0; i < m_nRows; ++i)
        {
            if (m_basisIndices[i] >= m_nCols)
                m_y[i] = -1.0;
        }
    }
    else
    {
        for (int i = 0; i < m_nRows; ++i)
        {
            int col = m_basisIndices[i];
            if (col < m_nCols)
                m_y[i] = m_c[col];
        }
    }
    btran(m_y);

    // Pricing: rotating partial Dantzig — scan a section of variables starting
    // from m_pricingStart, pick the variable with the largest |reduced cost|.
    int enterCol = -1;
    double bestRC = 0.0;
    int sectionSize = std::max(nTotalCols / 4, 50);
    int scanned = 0;

    for (int idx = 0; idx < nTotalCols; ++idx)
    {
        int j = (m_pricingStart + idx) % nTotalCols;

        // Skip basic variables using O(1) lookup
        if (m_isBasic[j])
            continue;

        // In Phase II, never price artificial columns — they are inert
        // placeholders for linearly dependent rows.
        if (!isPhaseI && j >= m_nCols)
            continue;

        // Compute reduced cost: c_j - y^T * a_j
        double cj;
        if (isPhaseI)
            cj = (j >= m_nCols) ? -1.0 : 0.0;
        else
            cj = (j < m_nCols) ? m_c[j] : 0.0;

        double yTa = 0.0;
        if (j < m_nCols)
        {
            for (int p = m_colStart[j]; p < m_colStart[j + 1]; ++p)
                yTa += m_y[m_rowIdx[p]] * m_values[p];
        }
        else
        {
            int artRow = j - m_nCols;
            yTa = m_y[artRow] * static_cast<double>(m_artSign[artRow]);
        }

        double rc = cj - yTa;

        int status = (j < m_nCols) ? m_nonBasicStatus[j] : 0;

        bool bCanEnter = false;
        if (status == 0 && rc > m_params.optimalityTol)
            bCanEnter = true;
        if (status == 1 && rc < -m_params.optimalityTol)
            bCanEnter = true;

        if (bCanEnter)
        {
            double absRC = std::fabs(rc);
            if (absRC > bestRC)
            {
                bestRC = absRC;
                enterCol = j;
            }
        }

        ++scanned;
        // Early stop: once we've scanned enough candidates and found one
        if (scanned >= sectionSize && enterCol != -1)
            break;
    }

    if (enterCol == -1)
    {
        m_lastBestRC = 0.0;
        m_lastPivotStep = 0.0;
        return 1; // optimal
    }

    m_lastBestRC = bestRC;
    m_pricingStart = (enterCol + 1) % nTotalCols;

    // FTRAN: compute direction d = B^{-1} * a_q
    std::fill(m_d.begin(), m_d.end(), 0.0);
    computeColumnInBasis(enterCol, m_d);
    ftran(m_d);

    // Entering column must not already be basic
    assert(!m_isBasic[enterCol] && "iterate: entering column is already basic");
    assert(enterCol >= 0 && enterCol < nTotalCols && "iterate: enterCol out of range");

    // Determine entering variable status
    int enterStatus = (enterCol < m_nCols) ? m_nonBasicStatus[enterCol] : 0;

    double enterLB = (enterCol < m_nCols) ? m_lb[enterCol] : 0.0;
    double enterUB = (enterCol < m_nCols) ? m_ub[enterCol] : INF;
    assert(enterLB <= enterUB && "iterate: entering variable has inverted bounds");

    // Direction of movement: +1 = increase from lb, -1 = decrease from ub
    double direction = (enterStatus == 0) ? 1.0 : -1.0;
    // Flip d if entering from upper bound
    if (direction < 0.0)
    {
        for (int i = 0; i < m_nRows; ++i)
            m_d[i] = -m_d[i];
    }

    // Ratio test: find leaving variable
    double maxStep = INF;
    int leaveRow = -1;

    // Bound on entering variable itself (bound flip distance)
    if (enterLB > -INF && enterUB < INF)
    {
        maxStep = enterUB - enterLB;
        assert(maxStep >= 0.0 && "iterate: bound flip distance negative (enterUB < enterLB)");
    }

    for (int i = 0; i < m_nRows; ++i)
    {
        int bCol = m_basisIndices[i];
        double lb_i = (bCol < m_nCols) ? m_lb[bCol] : 0.0;
        double ub_i = (bCol < m_nCols) ? m_ub[bCol] : INF;

        assert(lb_i <= ub_i && "iterate: basic variable has inverted bounds");

        // Symbolic perturbation for anti-cycling: pretend each basic variable
        // is slightly interior to its bound by a unique eps_i.  This makes
        // degenerate ratios (xB == bound) produce unique positive steps
        // eps_i / d[i] instead of all collapsing to zero.
        double eps_i = m_params.feasibilityTol * (1.0 + i);

        if (m_d[i] > m_params.feasibilityTol)
        {
            // Basic variable decreases: xB[i] - step * d[i] >= lb_i
            double step = (m_xB[i] - lb_i + eps_i) / m_d[i];
            // Clamp: only if numerical drift exceeds perturbation
            if (step < 0.0)
                step = 0.0;
            if (step < maxStep)
            {
                maxStep = step;
                leaveRow = i;
            }
        }
        else if (m_d[i] < -m_params.feasibilityTol)
        {
            // Basic variable increases: xB[i] - step * d[i] <= ub_i
            double step = (m_xB[i] - ub_i - eps_i) / m_d[i];
            if (step < 0.0)
                step = 0.0;
            if (step < maxStep)
            {
                maxStep = step;
                leaveRow = i;
            }
        }
    }

    assert(maxStep >= 0.0 && "iterate: negative step — basic variable outside bounds or inverted bounds");

    m_lastPivotStep = maxStep;

    if (maxStep >= INF / 2.0)
        return -1; // unbounded

    // Perform the pivot
    if (leaveRow == -1)
    {
        // Entering variable hits its opposite bound (flip bound)
        if (enterCol < m_nCols)
            m_nonBasicStatus[enterCol] = 1 - m_nonBasicStatus[enterCol];
        for (int i = 0; i < m_nRows; ++i)
            m_xB[i] -= maxStep * m_d[i];
    }
    else
    {
        // Swap entering and leaving
        int leaveCol = m_basisIndices[leaveRow];

        for (int i = 0; i < m_nRows; ++i)
            m_xB[i] -= maxStep * m_d[i];

        // Set entering variable's value
        double enterVal;
        if (enterStatus == 0)
            enterVal = enterLB + maxStep;
        else
            enterVal = enterUB - maxStep;
        m_xB[leaveRow] = enterVal;

        // Determine leaving variable status
        if (leaveCol < m_nCols)
        {
            if (m_d[leaveRow] > 0)
                m_nonBasicStatus[leaveCol] = 0; // at lower bound
            else
                m_nonBasicStatus[leaveCol] = 1; // at upper bound
        }

        // Restore d to original (un-flipped) direction for the LU update
        if (direction < 0.0)
        {
            for (int i = 0; i < m_nRows; ++i)
                m_d[i] = -m_d[i];
        }

        assert(leaveRow >= 0 && leaveRow < m_nRows && "iterate: leaveRow out of range");
        assert(std::fabs(m_d[leaveRow]) > m_params.feasibilityTol &&
               "iterate: zero pivot element in leaving row");

        // Update basis tracking
        m_isBasic[leaveCol] = false;
        m_basisIndices[leaveRow] = enterCol;
        m_isBasic[enterCol] = true;

        if (!m_lu.update(leaveRow, m_d))
        {
            if (!refactorizeBasis())
                return -1;  // signal numerical failure
        }
        else
        {
            m_pivotsSinceRefactor++;
        }

        if (m_pivotsSinceRefactor >= m_params.refactorInterval)
        {
            if (!refactorizeBasis())
                return -1;  // signal numerical failure
            recomputeXB();
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
// ftran / btran wrappers
// ---------------------------------------------------------------------------

void RevisedSimplex::ftran(std::vector<double>& col) const
{
    m_lu.solveRight(col);
}

void RevisedSimplex::btran(std::vector<double>& row) const
{
    m_lu.solveLeft(row);
}

// ---------------------------------------------------------------------------
// recomputeXB - recompute basic variable values from scratch
// ---------------------------------------------------------------------------

void RevisedSimplex::recomputeXB()
{
    m_xB.assign(m_nRows, 0.0);

    // Start with RHS
    for (int i = 0; i < m_nRows; ++i)
        m_xB[i] = m_b[i];

    // Subtract contribution of non-basic variables
    for (int j = 0; j < m_nCols; ++j)
    {
        if (m_isBasic[j])
            continue;

        double val;
        if (m_nonBasicStatus[j] == 1)
            val = m_ub[j];
        else
            val = (m_lb[j] > -INF) ? m_lb[j] : 0.0;

        if (std::fabs(val) < 1e-15)
            continue;

        for (int p = m_colStart[j]; p < m_colStart[j + 1]; ++p)
            m_xB[m_rowIdx[p]] -= m_values[p] * val;
    }

    // Solve B * xB = rhs  =>  xB = B^{-1} * rhs
    ftran(m_xB);
}

// ---------------------------------------------------------------------------
// refactorizeBasis
// ---------------------------------------------------------------------------

bool RevisedSimplex::refactorizeBasis()
{
    m_pivotsSinceRefactor = 0;
    return m_lu.factorize(m_nRows, m_basisIndices,
                          m_colStart, m_rowIdx, m_values,
                          m_artSign, m_nCols);
}

// ---------------------------------------------------------------------------
// rebuildIsBasic
// ---------------------------------------------------------------------------

void RevisedSimplex::rebuildIsBasic()
{
    int total = m_nCols + m_nArtificials;
    m_isBasic.assign(total, false);
    for (int i = 0; i < m_nRows; ++i)
    {
        int col = m_basisIndices[i];
        assert(col >= 0 && col < total && "rebuildIsBasic: basis index out of range");
        assert(!m_isBasic[col] && "rebuildIsBasic: duplicate column in basis");
        m_isBasic[col] = true;
    }
}

// ---------------------------------------------------------------------------
// computeColumnInBasis - get column of A (or artificial) as dense vector
// ---------------------------------------------------------------------------

void RevisedSimplex::computeColumnInBasis(int col,
                                           std::vector<double>& result) const
{
    std::fill(result.begin(), result.end(), 0.0);
    if (col < m_nCols)
    {
        for (int p = m_colStart[col]; p < m_colStart[col + 1]; ++p)
            result[m_rowIdx[p]] = m_values[p];
    }
    else
    {
        int artRow = col - m_nCols;
        result[artRow] = static_cast<double>(m_artSign[artRow]);
    }
}

} // namespace sparseMath
