#include "utils/sparseMath/SparseLU.h"
#include "utils/sparseMath/AmdOrdering.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace sparseMath {

// ---------------------------------------------------------------------------
// factorize
// ---------------------------------------------------------------------------
bool SparseLU::factorize(int m,
                          const std::vector<int>& basisCols,
                          const std::vector<int>& colStart,
                          const std::vector<int>& rowIdx,
                          const std::vector<double>& values,
                          const std::vector<int>& artSign,
                          int nOrigCols)
{
    m_dim = m;
    m_etaFile.clear();

    // ------------------------------------------------------------------
    // 1. Extract basis columns into a dense column-major workspace.
    //    basisCol[j] is the j-th basis column, stored densely.
    // ------------------------------------------------------------------
    std::vector<std::vector<double>> denseCol(m, std::vector<double>(m, 0.0));
    // Build temporary CSC of basis for AMD
    std::vector<int> bColStart(m + 1, 0);
    std::vector<int> bRowIdx;
    std::vector<double> bValues;

    for (int j = 0; j < m; ++j)
    {
        int col = basisCols[j];
        if (col < nOrigCols)
        {
            for (int p = colStart[col]; p < colStart[col + 1]; ++p)
            {
                int row = rowIdx[p];
                double val = values[p];
                denseCol[j][row] = val;
                bRowIdx.push_back(row);
                bValues.push_back(val);
            }
        }
        else
        {
            // Artificial variable column: single entry
            int artRow = col - nOrigCols;
            double sign = (artSign.empty()) ? 1.0 : static_cast<double>(artSign[artRow]);
            denseCol[j][artRow] = sign;
            bRowIdx.push_back(artRow);
            bValues.push_back(sign);
        }
        bColStart[j + 1] = static_cast<int>(bRowIdx.size());
    }

    // ------------------------------------------------------------------
    // 2. Compute AMD column ordering
    // ------------------------------------------------------------------
    std::vector<int> colOrder = AmdOrdering::compute(m, bColStart, bRowIdx);

    // Q[new] = old column index in basis
    m_Q = colOrder;
    m_Qinv.resize(m);
    for (int j = 0; j < m; ++j)
        m_Qinv[m_Q[j]] = j;

    // Reorder columns according to Q
    std::vector<std::vector<double>> work(m, std::vector<double>(m, 0.0));
    for (int j = 0; j < m; ++j)
        work[j] = denseCol[m_Q[j]];

    // ------------------------------------------------------------------
    // 3. Gilbert-Peierls left-looking LU with partial (threshold) pivoting
    // ------------------------------------------------------------------
    // Row permutation built during factorization
    m_P.resize(m);
    m_Pinv.resize(m);
    std::iota(m_P.begin(), m_P.end(), 0);
    std::iota(m_Pinv.begin(), m_Pinv.end(), 0);

    // Dense L and U stored temporarily
    // L is unit lower triangular (m x m), U is upper triangular (m x m)
    std::vector<std::vector<double>> L(m, std::vector<double>(m, 0.0));
    std::vector<std::vector<double>> U(m, std::vector<double>(m, 0.0));
    for (int j = 0; j < m; ++j)
        L[j][j] = 1.0;

    for (int j = 0; j < m; ++j)
    {
        // Copy column j of the permuted matrix into a workspace
        std::vector<double> col(m);
        for (int i = 0; i < m; ++i)
            col[i] = work[j][m_P[i]];

        // Apply previous L columns: for k < j, col -= L[k][...] * col[k]
        for (int k = 0; k < j; ++k)
        {
            double ukj = col[k];
            if (std::fabs(ukj) > 1e-15)
            {
                for (int i = k + 1; i < m; ++i)
                    col[i] -= L[k][i] * ukj;
            }
        }

        // Threshold partial pivoting: among rows j..m-1, pick the one
        // with max |col[i]|; swap if |col[j]| < threshold * max.
        int pivotRow = j;
        double maxAbs = std::fabs(col[j]);
        for (int i = j + 1; i < m; ++i)
        {
            double a = std::fabs(col[i]);
            if (a > maxAbs)
            {
                maxAbs = a;
                pivotRow = i;
            }
        }

        if (maxAbs < SINGULAR_TOL)
            return false;

        if (pivotRow != j && std::fabs(col[j]) < PIVOT_THRESHOLD * maxAbs)
        {
            // Swap rows j and pivotRow in col
            std::swap(col[j], col[pivotRow]);

            // Swap in P/Pinv
            int oldJ = m_P[j];
            int oldP = m_P[pivotRow];
            m_P[j] = oldP;
            m_P[pivotRow] = oldJ;
            m_Pinv[oldP] = j;
            m_Pinv[oldJ] = pivotRow;

            // Swap corresponding rows in previous L columns
            for (int k = 0; k < j; ++k)
                std::swap(L[k][j], L[k][pivotRow]);
        }

        // Store U row j
        U[j][j] = col[j];
        for (int k = 0; k < j; ++k)
            U[k][j] = col[k];

        // Compute L column j (below diagonal)
        double pivot = col[j];
        for (int i = j + 1; i < m; ++i)
            L[j][i] = col[i] / pivot;
    }

    // ------------------------------------------------------------------
    // 4. Convert dense L, U to CSC sparse storage
    // ------------------------------------------------------------------
    m_lColStart.assign(m + 1, 0);
    m_lRowIdx.clear();
    m_lValues.clear();
    for (int j = 0; j < m; ++j)
    {
        for (int i = j + 1; i < m; ++i)
        {
            if (std::fabs(L[j][i]) > 1e-15)
            {
                m_lRowIdx.push_back(i);
                m_lValues.push_back(L[j][i]);
            }
        }
        m_lColStart[j + 1] = static_cast<int>(m_lRowIdx.size());
    }

    m_uColStart.assign(m + 1, 0);
    m_uRowIdx.clear();
    m_uValues.clear();
    for (int j = 0; j < m; ++j)
    {
        for (int i = 0; i <= j; ++i)
        {
            if (std::fabs(U[i][j]) > 1e-15)
            {
                m_uRowIdx.push_back(i);
                m_uValues.push_back(U[i][j]);
            }
        }
        m_uColStart[j + 1] = static_cast<int>(m_uRowIdx.size());
    }

    return true;
}

// ---------------------------------------------------------------------------
// solveRight  (FTRAN):  B*x = rhs  =>  P*L*E*U*Q^T * x = rhs
//   x = Q * U^{-1} * E^{-1} * L^{-1} * P^T * rhs
// ---------------------------------------------------------------------------
void SparseLU::solveRight(std::vector<double>& rhs) const
{
    int m = m_dim;
    // Step 1: apply P^T (row un-permute)
    std::vector<double> tmp(m);
    for (int i = 0; i < m; ++i)
        tmp[i] = rhs[m_P[i]];

    // Step 2: L forward-substitution (L is unit lower triangular, stored without diagonal)
    for (int j = 0; j < m; ++j)
    {
        double xj = tmp[j];
        if (std::fabs(xj) > 1e-15)
        {
            for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
                tmp[m_lRowIdx[p]] -= m_lValues[p] * xj;
        }
    }

    // Step 3: apply eta file forward
    m_etaFile.applyForward(tmp);

    // Step 4: U back-substitution (U is upper triangular, CSC)
    for (int j = m - 1; j >= 0; --j)
    {
        // Find diagonal entry (last in column j)
        int pEnd = m_uColStart[j + 1];
        int pStart = m_uColStart[j];
        if (pStart == pEnd)
            continue;

        double diag = m_uValues[pEnd - 1]; // diagonal is last entry in column
        tmp[j] /= diag;
        double xj = tmp[j];
        if (std::fabs(xj) > 1e-15)
        {
            for (int p = pStart; p < pEnd - 1; ++p)
                tmp[m_uRowIdx[p]] -= m_uValues[p] * xj;
        }
    }

    // Step 5: apply Q (column permute)
    for (int i = 0; i < m; ++i)
        rhs[m_Q[i]] = tmp[i];
}

// ---------------------------------------------------------------------------
// solveLeft  (BTRAN):  B^T*y = rhs  =>  Q*U^T*E^T*L^T*P^T * y = rhs
//   y = P * L^{-T} * E^{-T} * U^{-T} * Q^T * rhs
// ---------------------------------------------------------------------------
void SparseLU::solveLeft(std::vector<double>& rhs) const
{
    int m = m_dim;

    // Step 1: apply Q^T (un-permute columns)
    std::vector<double> tmp(m);
    for (int i = 0; i < m; ++i)
        tmp[i] = rhs[m_Q[i]];

    // Step 2: U^T forward-substitution (U^T is lower triangular)
    for (int j = 0; j < m; ++j)
    {
        int pStart = m_uColStart[j];
        int pEnd = m_uColStart[j + 1];
        if (pStart == pEnd)
            continue;

        // Off-diagonals of column j are rows < j
        for (int p = pStart; p < pEnd - 1; ++p)
            tmp[j] -= m_uValues[p] * tmp[m_uRowIdx[p]];

        double diag = m_uValues[pEnd - 1];
        tmp[j] /= diag;
    }

    // Step 3: apply eta file backward
    m_etaFile.applyBackward(tmp);

    // Step 4: L^T back-substitution (L^T is upper triangular, unit diagonal)
    // L^T_{jk} = L_{kj}; entries with k > j stored in CSC column j of L.
    for (int j = m - 1; j >= 0; --j)
    {
        for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
            tmp[j] -= m_lValues[p] * tmp[m_lRowIdx[p]];
    }

    // Step 5: apply P
    for (int i = 0; i < m; ++i)
        rhs[m_Pinv[i]] = tmp[i];
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
bool SparseLU::update(int leavingRow, const std::vector<double>& enteringCol)
{
    // enteringCol is the FTRAN'd entering column (in basis-row space).
    // The pivot element is enteringCol[leavingRow].
    if (std::fabs(enteringCol[leavingRow]) < SINGULAR_TOL)
        return false;

    m_etaFile.addUpdate(leavingRow, enteringCol, m_dim);
    return true;
}

// ---------------------------------------------------------------------------
// nnz
// ---------------------------------------------------------------------------
int SparseLU::nnz() const
{
    return static_cast<int>(m_lRowIdx.size() + m_uRowIdx.size());
}

// ---------------------------------------------------------------------------
// permute helpers
// ---------------------------------------------------------------------------
void SparseLU::permuteForward(std::vector<double>& x,
                               const std::vector<int>& perm) const
{
    int n = static_cast<int>(x.size());
    std::vector<double> tmp(n);
    for (int i = 0; i < n; ++i)
        tmp[i] = x[perm[i]];
    x = std::move(tmp);
}

void SparseLU::permuteBackward(std::vector<double>& x,
                                const std::vector<int>& invPerm) const
{
    int n = static_cast<int>(x.size());
    std::vector<double> tmp(n);
    for (int i = 0; i < n; ++i)
        tmp[invPerm[i]] = x[i];
    x = std::move(tmp);
}

} // namespace sparseMath
