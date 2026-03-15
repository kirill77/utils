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

    const size_t mm = static_cast<size_t>(m);

    // ------------------------------------------------------------------
    // 1. Extract basis columns into flat column-major array.
    //    dense[col * m + row] = value
    // ------------------------------------------------------------------
    std::vector<double> dense(mm * mm, 0.0);

    // Also build temporary CSC of basis for AMD ordering
    std::vector<int> bColStart(m + 1, 0);
    std::vector<int> bRowIdx;

    for (int j = 0; j < m; ++j)
    {
        int col = basisCols[j];
        const size_t jOff = static_cast<size_t>(j) * mm;
        if (col < nOrigCols)
        {
            for (int p = colStart[col]; p < colStart[col + 1]; ++p)
            {
                int row = rowIdx[p];
                dense[jOff + row] = values[p];
                bRowIdx.push_back(row);
            }
        }
        else
        {
            int artRow = col - nOrigCols;
            double sign = (artSign.empty()) ? 1.0 : static_cast<double>(artSign[artRow]);
            dense[jOff + artRow] = sign;
            bRowIdx.push_back(artRow);
        }
        bColStart[j + 1] = static_cast<int>(bRowIdx.size());
    }

    // ------------------------------------------------------------------
    // 2. Compute AMD column ordering
    // ------------------------------------------------------------------
    std::vector<int> colOrder = AmdOrdering::compute(m, bColStart, bRowIdx);

    m_Q = colOrder;
    m_Qinv.resize(m);
    for (int j = 0; j < m; ++j)
        m_Qinv[m_Q[j]] = j;

    // Reorder columns according to Q
    std::vector<double> work(mm * mm, 0.0);
    for (int j = 0; j < m; ++j)
    {
        const size_t dstOff = static_cast<size_t>(j) * mm;
        const size_t srcOff = static_cast<size_t>(m_Q[j]) * mm;
        for (int i = 0; i < m; ++i)
            work[dstOff + i] = dense[srcOff + i];
    }

    // ------------------------------------------------------------------
    // 3. Left-looking LU with threshold partial pivoting.
    //    L (unit lower triangular) and U (upper triangular) stored in
    //    flat column-major layout for cache locality.
    // ------------------------------------------------------------------
    m_P.resize(m);
    m_Pinv.resize(m);
    std::iota(m_P.begin(), m_P.end(), 0);
    std::iota(m_Pinv.begin(), m_Pinv.end(), 0);

    std::vector<double> Lmat(mm * mm, 0.0);
    std::vector<double> Umat(mm * mm, 0.0);
    for (int j = 0; j < m; ++j)
        Lmat[static_cast<size_t>(j) * mm + j] = 1.0;

    // Reusable column workspace (avoids allocation per column)
    std::vector<double> col(m);

    for (int j = 0; j < m; ++j)
    {
        const size_t jOff = static_cast<size_t>(j) * mm;

        // Copy column j of the permuted matrix into workspace
        for (int i = 0; i < m; ++i)
            col[i] = work[jOff + m_P[i]];

        // Apply previous L columns: for k < j, col -= L[k][...] * col[k]
        for (int k = 0; k < j; ++k)
        {
            double ukj = col[k];
            if (std::fabs(ukj) > 1e-15)
            {
                const size_t kOff = static_cast<size_t>(k) * mm;
                for (int i = k + 1; i < m; ++i)
                    col[i] -= Lmat[kOff + i] * ukj;
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
            {
                const size_t kOff = static_cast<size_t>(k) * mm;
                std::swap(Lmat[kOff + j], Lmat[kOff + pivotRow]);
            }
        }

        // Store U column j (upper part including diagonal)
        for (int i = 0; i <= j; ++i)
            Umat[jOff + i] = col[i];

        // Compute L column j (below diagonal)
        double pivot = col[j];
        for (int i = j + 1; i < m; ++i)
            Lmat[jOff + i] = col[i] / pivot;
    }

    // ------------------------------------------------------------------
    // 4. Convert flat L, U to CSC sparse storage
    // ------------------------------------------------------------------
    m_lColStart.assign(m + 1, 0);
    m_lRowIdx.clear();
    m_lValues.clear();
    for (int j = 0; j < m; ++j)
    {
        const size_t jOff = static_cast<size_t>(j) * mm;
        for (int i = j + 1; i < m; ++i)
        {
            double val = Lmat[jOff + i];
            if (std::fabs(val) > 1e-15)
            {
                m_lRowIdx.push_back(i);
                m_lValues.push_back(val);
            }
        }
        m_lColStart[j + 1] = static_cast<int>(m_lRowIdx.size());
    }

    m_uColStart.assign(m + 1, 0);
    m_uRowIdx.clear();
    m_uValues.clear();
    for (int j = 0; j < m; ++j)
    {
        const size_t jOff = static_cast<size_t>(j) * mm;
        for (int i = 0; i <= j; ++i)
        {
            double val = Umat[jOff + i];
            if (std::fabs(val) > 1e-15)
            {
                m_uRowIdx.push_back(i);
                m_uValues.push_back(val);
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
    // Step 1: apply P (row permute)
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
        int pEnd = m_uColStart[j + 1];
        int pStart = m_uColStart[j];
        if (pStart == pEnd)
            continue;

        // Diagonal is last entry in column (by construction)
        double diag = m_uValues[pEnd - 1];
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
// solveLeft  (BTRAN):  B^T*y = rhs  =>  Q*U^T*E^T*L^T*P * y = rhs
//   y = P^T * L^{-T} * E^{-T} * U^{-T} * Q^T * rhs
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
    for (int j = m - 1; j >= 0; --j)
    {
        for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
            tmp[j] -= m_lValues[p] * tmp[m_lRowIdx[p]];
    }

    // Step 5: apply P^T
    for (int i = 0; i < m; ++i)
        rhs[m_Pinv[i]] = tmp[i];
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
bool SparseLU::update(int leavingRow, const std::vector<double>& enteringCol)
{
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

} // namespace sparseMath
