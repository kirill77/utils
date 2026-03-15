#pragma once

#include "utils/sparseMath/EtaFile.h"

#include <vector>

namespace sparseMath {

// Sparse LU factorization with Forrest-Tomlin column updates.
// Uses AMD ordering for fill reduction and Gilbert-Peierls left-looking
// factorization with Markowitz threshold pivoting.
class SparseLU
{
public:
    // Factorize the basis matrix B (m x m) extracted from a CSC constraint
    // matrix A by selecting columns listed in basisCols.
    //   m          – number of rows (basis dimension)
    //   basisCols  – column indices into A that form the basis (size m)
    //   colStart, rowIdx, values – full CSC of A
    //   artSign    – sign of artificial variable per row (+1 or -1);
    //                empty if no artificials are present
    //   nOrigCols  – number of original (non-artificial) columns in A
    // Returns false if the basis is numerically singular.
    bool factorize(int m,
                   const std::vector<int>& basisCols,
                   const std::vector<int>& colStart,
                   const std::vector<int>& rowIdx,
                   const std::vector<double>& values,
                   const std::vector<int>& artSign,
                   int nOrigCols);

    // FTRAN: solve B*x = rhs in-place.
    void solveRight(std::vector<double>& rhs) const;

    // BTRAN: solve B^T*y = rhs in-place.
    void solveLeft(std::vector<double>& rhs) const;

    // Forrest-Tomlin column update after a basis swap.
    // Returns false if the pivot is too small (caller should refactorize).
    bool update(int leavingRow, const std::vector<double>& enteringCol);

    int nnz() const;

    int etaCount() const { return m_etaFile.count(); }

private:
    int m_dim = 0;

    // L factor in CSC (unit lower triangular, diagonal = 1 not stored)
    std::vector<int>    m_lColStart;
    std::vector<int>    m_lRowIdx;
    std::vector<double> m_lValues;

    // U factor in CSC (upper triangular, diagonal stored)
    std::vector<int>    m_uColStart;
    std::vector<int>    m_uRowIdx;
    std::vector<double> m_uValues;

    // Row permutation P: P[new] = old,  Pinv[old] = new
    std::vector<int> m_P;
    std::vector<int> m_Pinv;

    // Column permutation Q: Q[new] = old,  Qinv[old] = new
    std::vector<int> m_Q;
    std::vector<int> m_Qinv;

    EtaFile m_etaFile;

    mutable std::vector<double> m_tmp; // reusable workspace for solveRight/solveLeft

    static constexpr double PIVOT_THRESHOLD = 0.1;
    static constexpr double SINGULAR_TOL    = 1e-13;
};

} // namespace sparseMath
