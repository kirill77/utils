#include "utils/sparseMath/SparseLU.h"
#include "utils/sparseMath/AmdOrdering.h"

#include <algorithm>
#include <cassert>
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
    assert(m > 0 && "factorize: dimension must be positive");
    assert(static_cast<int>(basisCols.size()) >= m && "factorize: not enough basis columns");

    m_dim = m;
    m_etaFile.clear();
    m_tmp.resize(m);

    // ------------------------------------------------------------------
    // 1. Build basis CSC with values (structure + values in one pass)
    // ------------------------------------------------------------------
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
                bRowIdx.push_back(rowIdx[p]);
                bValues.push_back(values[p]);
            }
        }
        else
        {
            int artRow = col - nOrigCols;
            double sign = (artSign.empty()) ? 1.0 : static_cast<double>(artSign[artRow]);
            bRowIdx.push_back(artRow);
            bValues.push_back(sign);
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

    // ------------------------------------------------------------------
    // 3. Left-looking LU with Gilbert-Peierls DFS and threshold pivoting.
    //    L and U are built directly as CSC during factorization.
    // ------------------------------------------------------------------
    m_P.resize(m);
    m_Pinv.resize(m);
    std::iota(m_P.begin(), m_P.end(), 0);
    std::iota(m_Pinv.begin(), m_Pinv.end(), 0);

    m_lColStart.assign(m + 1, 0);
    m_lRowIdx.clear();
    m_lValues.clear();
    m_uColStart.assign(m + 1, 0);
    m_uRowIdx.clear();
    m_uValues.clear();

    // Reserve estimated nnz for L and U to reduce reallocations
    int inputNnz = static_cast<int>(bRowIdx.size());
    int estNnz = std::max(inputNnz * 3, m);
    m_lRowIdx.reserve(estNnz);
    m_lValues.reserve(estNnz);
    m_uRowIdx.reserve(estNnz);
    m_uValues.reserve(estNnz);

    // Row-to-L-position index for O(nnz_row) row swaps during pivoting
    std::vector<std::vector<int>> rowToLPos(m);

    // Gilbert-Peierls DFS workspace
    std::vector<int> topOrder;
    std::vector<bool> visited(m, false);
    std::vector<int> dfsStack;
    std::vector<int> dfsPos(m);

    // Reusable dense column workspace
    std::vector<double> col(m);

    for (int j = 0; j < m; ++j)
    {
        // Scatter basis column Q[j] into workspace using Pinv mapping
        std::fill(col.begin(), col.end(), 0.0);
        int bj = m_Q[j];
        for (int p = bColStart[bj]; p < bColStart[bj + 1]; ++p)
            col[m_Pinv[bRowIdx[p]]] = bValues[p];

        // Gilbert-Peierls DFS: find reach of b_j in L (columns 0..j-1).
        // Produces topOrder in postorder; iterate in reverse for forward-sub.
        topOrder.clear();
        for (int p = bColStart[bj]; p < bColStart[bj + 1]; ++p)
        {
            int row = m_Pinv[bRowIdx[p]];
            if (row < j && !visited[row])
            {
                visited[row] = true;
                dfsPos[row] = m_lColStart[row];
                dfsStack.push_back(row);

                while (!dfsStack.empty())
                {
                    int k = dfsStack.back();
                    bool pushed = false;
                    while (dfsPos[k] < m_lColStart[k + 1])
                    {
                        int child = m_lRowIdx[dfsPos[k]];
                        ++dfsPos[k];
                        if (child < j && !visited[child])
                        {
                            visited[child] = true;
                            dfsPos[child] = m_lColStart[child];
                            dfsStack.push_back(child);
                            pushed = true;
                            break;
                        }
                    }
                    if (!pushed)
                    {
                        topOrder.push_back(k);
                        dfsStack.pop_back();
                    }
                }
            }
        }

        // Clear visited flags (only touched columns)
        for (int k : topOrder)
            visited[k] = false;

        // Apply L columns in topological order (reverse of postorder)
        for (int idx = static_cast<int>(topOrder.size()) - 1; idx >= 0; --idx)
        {
            int k = topOrder[idx];
            double ukj = col[k];
            if (std::fabs(ukj) > 1e-15)
            {
                for (int p = m_lColStart[k]; p < m_lColStart[k + 1]; ++p)
                    col[m_lRowIdx[p]] -= m_lValues[p] * ukj;
            }
        }

        // Threshold partial pivoting: among rows j..m-1, pick max |col[i]|
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
        {
            m_dim = 0;  // reset so solveRight/solveLeft are safe no-ops
            return false;
        }

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

            // Swap rows j <-> pivotRow in L using row-position index
            for (int pos : rowToLPos[j])
                m_lRowIdx[pos] = pivotRow;
            for (int pos : rowToLPos[pivotRow])
                m_lRowIdx[pos] = j;
            std::swap(rowToLPos[j], rowToLPos[pivotRow]);
        }

        // Store U column j: rows 0..j (upper triangular including diagonal)
        for (int i = 0; i <= j; ++i)
        {
            if (std::fabs(col[i]) > 1e-15)
            {
                m_uRowIdx.push_back(i);
                m_uValues.push_back(col[i]);
            }
        }
        m_uColStart[j + 1] = static_cast<int>(m_uRowIdx.size());

        // Store L column j: rows j+1..m-1 (strictly lower, unit diagonal not stored)
        double pivot = col[j];
        for (int i = j + 1; i < m; ++i)
        {
            double lij = col[i] / pivot;
            if (std::fabs(lij) > 1e-15)
            {
                int pos = static_cast<int>(m_lRowIdx.size());
                m_lRowIdx.push_back(i);
                m_lValues.push_back(lij);
                rowToLPos[i].push_back(pos);
            }
        }
        m_lColStart[j + 1] = static_cast<int>(m_lRowIdx.size());
    }

    return true;
}

// ---------------------------------------------------------------------------
// solveRight  (FTRAN):  B_new * x = rhs
//   B_new = B * E_1 * ... * E_k,  B = P*L*U*Q^T
//   x = E_k^{-1} * ... * E_1^{-1} * Q * U^{-1} * L^{-1} * P^T * rhs
// ---------------------------------------------------------------------------
void SparseLU::solveRight(std::vector<double>& rhs) const
{
    int m = m_dim;
    // Step 1: apply P^T (row permute into internal order)
    for (int i = 0; i < m; ++i)
        m_tmp[i] = rhs[m_P[i]];

    // Step 2: L forward-substitution (L is unit lower triangular, stored without diagonal)
    for (int j = 0; j < m; ++j)
    {
        double xj = m_tmp[j];
        if (std::fabs(xj) > 1e-15)
        {
            for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
                m_tmp[m_lRowIdx[p]] -= m_lValues[p] * xj;
        }
    }

    // Step 3: U back-substitution (U is upper triangular, CSC)
    for (int j = m - 1; j >= 0; --j)
    {
        int pEnd = m_uColStart[j + 1];
        int pStart = m_uColStart[j];
        if (pStart == pEnd)
            continue;

        // Diagonal is last entry in column (by construction: stored i=0..j)
        assert(m_uRowIdx[pEnd - 1] == j && "solveRight: U diagonal not in expected position");
        double diag = m_uValues[pEnd - 1];
        assert(std::fabs(diag) > 1e-15 && "solveRight: zero diagonal in U");
        m_tmp[j] /= diag;
        double xj = m_tmp[j];
        if (std::fabs(xj) > 1e-15)
        {
            for (int p = pStart; p < pEnd - 1; ++p)
                m_tmp[m_uRowIdx[p]] -= m_uValues[p] * xj;
        }
    }

    // Step 4: apply Q (column permute into basis-position space)
    for (int i = 0; i < m; ++i)
        rhs[m_Q[i]] = m_tmp[i];

    // Step 5: apply eta updates E_1^{-1}, ..., E_k^{-1} (in basis-position space)
    m_etaFile.applyForward(rhs);
}

// ---------------------------------------------------------------------------
// solveLeft  (BTRAN):  B_new^T * y = rhs
//   B_new = B * E_1 * ... * E_k,  B = P*L*U*Q^T
//   y = P * L^{-T} * U^{-T} * Q^T * E_1^{-T} * ... * E_k^{-T} * rhs
// ---------------------------------------------------------------------------
void SparseLU::solveLeft(std::vector<double>& rhs) const
{
    int m = m_dim;

    // Step 1: apply eta updates E_k^{-T}, ..., E_1^{-T} (in basis-position space)
    m_etaFile.applyBackward(rhs);

    // Step 2: apply Q^T (un-permute columns into internal order)
    for (int i = 0; i < m; ++i)
        m_tmp[i] = rhs[m_Q[i]];

    // Step 3: U^T forward-substitution (U^T is lower triangular)
    for (int j = 0; j < m; ++j)
    {
        int pStart = m_uColStart[j];
        int pEnd = m_uColStart[j + 1];
        if (pStart == pEnd)
            continue;

        // Off-diagonals of column j are rows < j
        for (int p = pStart; p < pEnd - 1; ++p)
            m_tmp[j] -= m_uValues[p] * m_tmp[m_uRowIdx[p]];

        assert(m_uRowIdx[pEnd - 1] == j && "solveLeft: U diagonal not in expected position");
        double diag = m_uValues[pEnd - 1];
        assert(std::fabs(diag) > 1e-15 && "solveLeft: zero diagonal in U");
        m_tmp[j] /= diag;
    }

    // Step 4: L^T back-substitution (L^T is upper triangular, unit diagonal)
    for (int j = m - 1; j >= 0; --j)
    {
        for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
            m_tmp[j] -= m_lValues[p] * m_tmp[m_lRowIdx[p]];
    }

    // Step 5: apply P (scatter from internal to original row order)
    for (int i = 0; i < m; ++i)
        rhs[m_P[i]] = m_tmp[i];
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------
bool SparseLU::update(int leavingRow, const std::vector<double>& enteringCol)
{
    assert(leavingRow >= 0 && leavingRow < m_dim && "update: leavingRow out of range");
    assert(static_cast<int>(enteringCol.size()) >= m_dim && "update: enteringCol too small");

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
