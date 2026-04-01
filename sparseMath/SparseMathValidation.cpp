// SparseMath runtime validation.
// Adapted from SparseLUTests.cpp (14 tests) and RevisedSimplexTests.cpp (11 tests).
// Called once on first FBA solve to verify solver infrastructure.

#include "utils/sparseMath/SparseMathValidation.h"
#include "utils/sparseMath/SparseLU.h"
#include "utils/sparseMath/AmdOrdering.h"
#include "utils/sparseMath/RevisedSimplex.h"
#include "utils/sparseMath/SteepestEdgePricer.h"
#include "utils/log/ILog.h"

#include <cassert>
#include <cmath>
#include <vector>
#include <limits>

namespace {

// -----------------------------------------------------------------------
// Shared helpers
// -----------------------------------------------------------------------

constexpr double LU_TOL = 1e-10;
constexpr double LP_TOL = 1e-6;
constexpr double INF_VAL = std::numeric_limits<double>::infinity();

// Dense matrix (row-major) x vector: y = A * x
static std::vector<double> matvec(const std::vector<std::vector<double>>& A,
                                   const std::vector<double>& x)
{
    int m = static_cast<int>(A.size());
    int n = static_cast<int>(x.size());
    std::vector<double> y(m, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            y[i] += A[i][j] * x[j];
    return y;
}

// Dense matrix transpose x vector: y = A^T * x
static std::vector<double> matvecT(const std::vector<std::vector<double>>& A,
                                    const std::vector<double>& x)
{
    int m = static_cast<int>(A.size());
    int n = static_cast<int>(A[0].size());
    std::vector<double> y(n, 0.0);
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j)
            y[j] += A[i][j] * x[i];
    return y;
}

static double maxAbsError(const std::vector<double>& a, const std::vector<double>& b)
{
    double err = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        err = (std::max)(err, std::fabs(a[i] - b[i]));
    return err;
}

// -----------------------------------------------------------------------
// CSC helpers — LU tests use column-major input, simplex tests use row-major
// -----------------------------------------------------------------------

struct CSC
{
    int nRows, nCols;
    std::vector<int> colStart;
    std::vector<int> rowIdx;
    std::vector<double> values;
};

// Build CSC from dense column-major: cols[col][row].
static CSC denseToCSC_ColMajor(const std::vector<std::vector<double>>& cols, int nRows)
{
    CSC csc;
    csc.nRows = nRows;
    csc.nCols = static_cast<int>(cols.size());
    csc.colStart.resize(csc.nCols + 1, 0);
    for (int j = 0; j < csc.nCols; ++j)
    {
        for (int i = 0; i < nRows; ++i)
        {
            if (std::fabs(cols[j][i]) > 1e-15)
            {
                csc.rowIdx.push_back(i);
                csc.values.push_back(cols[j][i]);
            }
        }
        csc.colStart[j + 1] = static_cast<int>(csc.rowIdx.size());
    }
    return csc;
}

// Build CSC from dense row-major: A[row][col].
static CSC denseToCSC_RowMajor(const std::vector<std::vector<double>>& A)
{
    CSC csc;
    csc.nRows = static_cast<int>(A.size());
    csc.nCols = A.empty() ? 0 : static_cast<int>(A[0].size());
    csc.colStart.resize(csc.nCols + 1, 0);
    for (int j = 0; j < csc.nCols; ++j)
    {
        for (int i = 0; i < csc.nRows; ++i)
        {
            if (std::fabs(A[i][j]) > 1e-15)
            {
                csc.rowIdx.push_back(i);
                csc.values.push_back(A[i][j]);
            }
        }
        csc.colStart[j + 1] = static_cast<int>(csc.rowIdx.size());
    }
    return csc;
}

// LP feasibility check: Ax ~ b and lb <= x <= ub.
static bool checkFeasibility(const CSC& csc,
                              const std::vector<double>& b,
                              const std::vector<double>& lb,
                              const std::vector<double>& ub,
                              const std::vector<double>& x,
                              const char* testName)
{
    std::vector<double> Ax(csc.nRows, 0.0);
    for (int j = 0; j < csc.nCols; ++j)
        for (int p = csc.colStart[j]; p < csc.colStart[j + 1]; ++p)
            Ax[csc.rowIdx[p]] += csc.values[p] * x[j];

    for (int i = 0; i < csc.nRows; ++i)
    {
        if (std::fabs(Ax[i] - b[i]) >= LP_TOL)
        {
            LOG_ERROR("SparseMath validation FAILED [%s]: Ax != b at row %d", testName, i);
            assert(false && "SparseMath validation failed");
            return false;
        }
    }
    for (int j = 0; j < csc.nCols; ++j)
    {
        if (lb[j] > -INF_VAL && x[j] < lb[j] - LP_TOL)
        {
            LOG_ERROR("SparseMath validation FAILED [%s]: x[%d] < lb", testName, j);
            assert(false && "SparseMath validation failed");
            return false;
        }
        if (ub[j] < INF_VAL && x[j] > ub[j] + LP_TOL)
        {
            LOG_ERROR("SparseMath validation FAILED [%s]: x[%d] > ub", testName, j);
            assert(false && "SparseMath validation failed");
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------
// Macro for fail-and-return pattern
// -----------------------------------------------------------------------
#define VCHECK(cond, testName, msg) \
    do { \
        if (!(cond)) { \
            LOG_ERROR("SparseMath validation FAILED [%s]: %s", testName, msg); \
            assert(false && "SparseMath validation failed"); \
            return false; \
        } \
    } while(0)

// =======================================================================
// SparseLU Tests (14)
// =======================================================================

static bool luTest_3x3_basic()
{
    const char* name = "LU:3x3_basic";
    std::vector<std::vector<double>> B = {
        {2, 1, 0}, {1, 3, 1}, {0, 1, 2}
    };
    std::vector<std::vector<double>> cols = {
        {2, 1, 0}, {1, 3, 1}, {0, 1, 2}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);
    VCHECK(ok, name, "factorize should succeed");

    std::vector<double> rhs = {1.0, 2.0, 3.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "FTRAN: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "BTRAN: B^T*y != rhs");
    return true;
}

static bool luTest_identity()
{
    const char* name = "LU:identity";
    int n = 5;
    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) cols[i][i] = 1.0;

    CSC csc = denseToCSC_ColMajor(cols, n);
    std::vector<int> basisCols = {0, 1, 2, 3, 4};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, n);
    VCHECK(ok, name, "factorize should succeed");

    std::vector<double> rhs = {7, -3, 0, 11, -5};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(x, rhs) < LU_TOL, name, "FTRAN on identity should return rhs unchanged");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(y, rhs) < LU_TOL, name, "BTRAN on identity should return rhs unchanged");
    return true;
}

static bool luTest_eta_updates()
{
    const char* name = "LU:eta_updates";
    std::vector<std::vector<double>> B = {
        {2, 1, 0}, {1, 3, 1}, {0, 1, 2}
    };
    std::vector<std::vector<double>> cols = {
        {2, 1, 0}, {1, 3, 1}, {0, 1, 2}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);
    VCHECK(ok, name, "initial factorize failed");

    // Replace column 1 with [4, 1, 2]
    std::vector<double> newCol = {4, 1, 2};
    std::vector<double> ftranCol = newCol;
    lu.solveRight(ftranCol);
    ok = lu.update(1, ftranCol);
    VCHECK(ok, name, "eta update should succeed");

    B[0][1] = 4; B[1][1] = 1; B[2][1] = 2;

    std::vector<double> rhs = {1.0, 2.0, 3.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "FTRAN after 1 eta update: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "BTRAN after 1 eta update: B^T*y != rhs");

    // Second update: replace column 2 with [1, 0, 5]
    std::vector<double> newCol2 = {1, 0, 5};
    std::vector<double> ftranCol2 = newCol2;
    lu.solveRight(ftranCol2);
    ok = lu.update(2, ftranCol2);
    VCHECK(ok, name, "second eta update should succeed");

    B[0][2] = 1; B[1][2] = 0; B[2][2] = 5;

    x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "FTRAN after 2 eta updates: B*x != rhs");

    y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "BTRAN after 2 eta updates: B^T*y != rhs");
    return true;
}

static bool luTest_artificials()
{
    const char* name = "LU:artificials";
    std::vector<std::vector<double>> cols = {
        {3, 1}, {1, 2}
    };
    CSC csc = denseToCSC_ColMajor(cols, 2);

    std::vector<int> basisCols = {0, 3};
    std::vector<int> artSign = {1, -1};
    int nOrigCols = 2;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, nOrigCols);
    VCHECK(ok, name, "factorize with artificials should succeed");

    std::vector<std::vector<double>> B = {{3, 0}, {1, -1}};
    std::vector<double> rhs = {6.0, -2.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "FTRAN with artificials: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "BTRAN with artificials: B^T*y != rhs");
    return true;
}

static bool luTest_threshold_pivoting()
{
    const char* name = "LU:threshold_pivoting";
    std::vector<std::vector<double>> cols = {
        {1e-14, 1.0}, {1.0, 1.0}
    };
    CSC csc = denseToCSC_ColMajor(cols, 2);

    std::vector<int> basisCols = {0, 1};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 2);
    VCHECK(ok, name, "near-singular factorize should succeed with pivoting");

    std::vector<std::vector<double>> B = {{1e-14, 1.0}, {1.0, 1.0}};
    std::vector<double> rhs = {1.0, 2.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < 1e-8, name, "FTRAN near-singular: large residual");
    return true;
}

static bool luTest_singular()
{
    const char* name = "LU:singular";
    std::vector<std::vector<double>> cols = {
        {1, 2}, {2, 4}
    };
    CSC csc = denseToCSC_ColMajor(cols, 2);

    std::vector<int> basisCols = {0, 1};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 2);
    VCHECK(!ok, name, "singular matrix should fail factorization");
    return true;
}

static bool luTest_sparse_7x7()
{
    const char* name = "LU:sparse_7x7";
    int n = 7;
    std::vector<std::vector<double>> B = {
        { 1,  0,  0,  0,  0,  0,  0},
        {-1,  1,  0,  0,  0,  0,  0},
        { 0, -1,  1,  0,  0,  0,  0},
        { 0,  0, -1,  1,  0,  0,  1},
        { 0,  0,  0, -1,  1,  0,  0},
        { 0,  0,  0,  0, -1,  1,  0},
        { 0,  0,  0,  0,  0, -1,  1},
    };

    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            cols[j][i] = B[i][j];

    CSC csc = denseToCSC_ColMajor(cols, n);
    std::vector<int> basisCols(n);
    for (int j = 0; j < n; ++j) basisCols[j] = j;
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, n);
    VCHECK(ok, name, "7x7 sparse factorize should succeed");

    std::vector<double> rhs = {1, -1, 2, 0, 3, -2, 1};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "7x7 FTRAN: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "7x7 BTRAN: B^T*y != rhs");
    return true;
}

static bool luTest_many_etas_then_refactorize()
{
    const char* name = "LU:many_etas_refactorize";
    int n = 4;
    std::vector<std::vector<double>> Bdense = {
        {4, 1, 0, 0}, {1, 4, 1, 0}, {0, 1, 4, 1}, {0, 0, 1, 4}
    };
    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            cols[j][i] = Bdense[i][j];

    CSC csc = denseToCSC_ColMajor(cols, n);
    std::vector<int> basisCols = {0, 1, 2, 3};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, n);
    VCHECK(ok, name, "4x4 initial factorize failed");

    std::vector<double> newCols[] = {
        {3, 0, 1, 0}, {0, 2, 0, 1}, {1, 1, 1, 3}
    };
    int replaceRows[] = {0, 2, 3};

    for (int k = 0; k < 3; ++k)
    {
        std::vector<double> ftranCol = newCols[k];
        lu.solveRight(ftranCol);
        ok = lu.update(replaceRows[k], ftranCol);
        VCHECK(ok, name, "eta update failed");
        for (int i = 0; i < n; ++i)
            Bdense[i][replaceRows[k]] = newCols[k][i];
    }

    std::vector<double> rhs = {5, -3, 7, 1};
    std::vector<double> x_eta = rhs;
    lu.solveRight(x_eta);
    VCHECK(maxAbsError(matvec(Bdense, x_eta), rhs) < 1e-8, name, "FTRAN after 3 eta updates: large residual");

    // Refactorize from scratch
    std::vector<std::vector<double>> updatedCols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            updatedCols[j][i] = Bdense[i][j];

    CSC csc2 = denseToCSC_ColMajor(updatedCols, n);
    sparseMath::SparseLU lu2;
    ok = lu2.factorize(n, basisCols, csc2.colStart, csc2.rowIdx, csc2.values, artSign, n);
    VCHECK(ok, name, "refactorize failed");

    std::vector<double> x_fresh = rhs;
    lu2.solveRight(x_fresh);
    VCHECK(maxAbsError(x_eta, x_fresh) < 1e-8, name, "eta solution diverged from fresh factorization");
    return true;
}

static bool luTest_ftran_btran_inverse()
{
    const char* name = "LU:ftran_btran_inverse";
    std::vector<std::vector<double>> cols = {
        {5, 2, 1}, {1, 6, 2}, {0, 1, 4}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);

    std::vector<double> rhs1 = {1.0, 0.0, 0.0};
    std::vector<double> rhs2 = {0.0, 1.0, 0.0};

    std::vector<double> x = rhs1;
    lu.solveRight(x);
    std::vector<double> y = rhs2;
    lu.solveLeft(y);

    double lhs = 0, rval = 0;
    for (int i = 0; i < 3; ++i)
    {
        lhs += x[i] * rhs2[i];
        rval += rhs1[i] * y[i];
    }
    VCHECK(std::fabs(lhs - rval) < LU_TOL, name, "FTRAN/BTRAN transpose consistency failed");
    return true;
}

static bool luTest_amd_ordering()
{
    const char* name = "LU:amd_ordering";
    std::vector<std::vector<double>> cols = {
        {1, 0, 1, 0, 0}, {0, 1, 0, 1, 0}, {1, 0, 1, 0, 1},
        {0, 1, 0, 1, 0}, {0, 0, 1, 0, 1}
    };
    CSC csc = denseToCSC_ColMajor(cols, 5);

    std::vector<int> perm = sparseMath::AmdOrdering::compute(5, csc.colStart, csc.rowIdx);
    VCHECK(static_cast<int>(perm.size()) == 5, name, "AMD perm wrong size");

    std::vector<bool> seen(5, false);
    for (int j = 0; j < 5; ++j)
    {
        VCHECK(perm[j] >= 0 && perm[j] < 5, name, "AMD perm out of range");
        VCHECK(!seen[perm[j]], name, "AMD perm has duplicates");
        seen[perm[j]] = true;
    }
    return true;
}

static bool luTest_column_subset()
{
    const char* name = "LU:column_subset";
    std::vector<std::vector<double>> cols = {
        {1, 0, 1}, {2, 1, 0}, {0, 3, 1}, {1, 0, 1}, {0, 2, 1}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);

    std::vector<int> basisCols = {1, 3, 4};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 5);
    VCHECK(ok, name, "column subset factorize failed");

    std::vector<std::vector<double>> B = {{2, 1, 0}, {1, 0, 2}, {0, 1, 1}};
    std::vector<double> rhs = {3, 4, 2};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "column subset FTRAN: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "column subset BTRAN: B^T*y != rhs");
    return true;
}

static bool luTest_multiple_rhs()
{
    const char* name = "LU:multiple_rhs";
    std::vector<std::vector<double>> B = {{3, 1, 0}, {1, 4, 1}, {0, 1, 3}};
    std::vector<std::vector<double>> cols = {{3, 1, 0}, {1, 4, 1}, {0, 1, 3}};
    CSC csc = denseToCSC_ColMajor(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);

    std::vector<std::vector<double>> rhsVecs = {
        {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {100, -50, 200}, {-1e6, 1e6, -1e6}
    };

    for (size_t t = 0; t < rhsVecs.size(); ++t)
    {
        std::vector<double> x = rhsVecs[t];
        lu.solveRight(x);
        VCHECK(maxAbsError(matvec(B, x), rhsVecs[t]) < 1e-6, name, "multiple RHS FTRAN failed");

        std::vector<double> y = rhsVecs[t];
        lu.solveLeft(y);
        VCHECK(maxAbsError(matvecT(B, y), rhsVecs[t]) < 1e-6, name, "multiple RHS BTRAN failed");
    }
    return true;
}

static bool luTest_non_involution_permutation()
{
    const char* name = "LU:non_involution_perm";
    std::vector<std::vector<double>> B = {
        {1, 10, 0.001}, {1, 1, 1}, {1, 10, 10}
    };
    std::vector<std::vector<double>> cols = {
        {1, 1, 1}, {10, 1, 10}, {0.001, 1, 10}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);

    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);
    VCHECK(ok, name, "factorize should succeed");

    std::vector<double> rhs = {1.0, 2.0, 3.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    VCHECK(maxAbsError(matvec(B, x), rhs) < LU_TOL, name, "FTRAN: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    VCHECK(maxAbsError(matvecT(B, y), rhs) < LU_TOL, name, "BTRAN: B^T*y != rhs");

    // Transpose consistency for all (i,j) pairs
    for (int i = 0; i < 3; ++i)
    {
        std::vector<double> ei(3, 0.0);
        ei[i] = 1.0;
        std::vector<double> xi = ei;
        lu.solveRight(xi);

        for (int j = 0; j < 3; ++j)
        {
            std::vector<double> ej(3, 0.0);
            ej[j] = 1.0;
            std::vector<double> yj = ej;
            lu.solveLeft(yj);

            double diff = std::fabs(xi[j] - yj[i]);
            VCHECK(diff < LU_TOL, name, "FTRAN/BTRAN transpose mismatch");
        }
    }
    return true;
}

static bool luTest_solve_after_failed_factorize()
{
    const char* name = "LU:solve_after_failed_factorize";
    std::vector<std::vector<double>> cols = {
        {1, 0, 0}, {0, 1, 0}, {1, 1, 0}
    };
    CSC csc = denseToCSC_ColMajor(cols, 3);

    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values, artSign, 3);
    VCHECK(!ok, name, "should detect singularity");

    // These must not crash (solution is meaningless, but no UB).
    std::vector<double> rhs = {1.0, 2.0, 3.0};
    lu.solveRight(rhs);

    std::fill(rhs.begin(), rhs.end(), 1.0);
    lu.solveLeft(rhs);
    return true;
}

// =======================================================================
// RevisedSimplex Tests (11)
// =======================================================================

static bool simplexTest_trivial_1var()
{
    const char* name = "Simplex:trivial_1var";
    std::vector<std::vector<double>> A = {{1, 1}};
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({0, 0}, {INF_VAL, INF_VAL});
    solver.setRHS({5});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 5.0) < LP_TOL, name, "obj should be 5");
    VCHECK(std::fabs(r.solution[0] - 5.0) < LP_TOL, name, "x should be 5");
    VCHECK(std::fabs(r.solution[1] - 0.0) < LP_TOL, name, "s should be 0");
    return true;
}

static bool simplexTest_2var_textbook()
{
    const char* name = "Simplex:2var_textbook";
    std::vector<std::vector<double>> A = {
        {6, 4, 1, 0}, {1, 2, 0, 1}
    };
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(2, 4, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({5, 4, 0, 0});
    solver.setBounds({0, 0, 0, 0}, {INF_VAL, INF_VAL, INF_VAL, INF_VAL});
    solver.setRHS({24, 6});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 21.0) < LP_TOL, name, "obj should be 21");
    VCHECK(std::fabs(r.solution[0] - 3.0) < LP_TOL, name, "x1 should be 3");
    VCHECK(std::fabs(r.solution[1] - 1.5) < LP_TOL, name, "x2 should be 3/2");
    if (!checkFeasibility(csc, {24, 6}, {0, 0, 0, 0}, {INF_VAL, INF_VAL, INF_VAL, INF_VAL},
                          r.solution, name)) return false;
    return true;
}

static bool simplexTest_infeasible()
{
    const char* name = "Simplex:infeasible";
    std::vector<std::vector<double>> A = {{1, 1}};
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({10, 0}, {20, INF_VAL});
    solver.setRHS({5});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::INFEASIBLE, name, "should be infeasible");
    return true;
}

static bool simplexTest_unbounded()
{
    const char* name = "Simplex:unbounded";
    std::vector<std::vector<double>> A = {{1, -1}};
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({0, 0}, {INF_VAL, INF_VAL});
    solver.setRHS({0});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::UNBOUNDED, name, "should be unbounded");
    return true;
}

static bool simplexTest_degenerate()
{
    const char* name = "Simplex:degenerate";
    std::vector<std::vector<double>> A = {
        {1, 1, 1, 0, 0}, {1, 0, 0, 1, 0}, {0, 1, 0, 0, 1}
    };
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(3, 5, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0, 0, 0, 0});
    solver.setBounds({0, 0, 0, 0, 0}, {INF_VAL, INF_VAL, INF_VAL, INF_VAL, INF_VAL});
    solver.setRHS({1, 1, 1});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 1.0) < LP_TOL, name, "obj should be 1");
    VCHECK(std::fabs(r.solution[0] - 1.0) < LP_TOL, name, "x1 should be 1");
    if (!checkFeasibility(csc, {1, 1, 1}, {0, 0, 0, 0, 0},
                          {INF_VAL, INF_VAL, INF_VAL, INF_VAL, INF_VAL}, r.solution, name)) return false;
    return true;
}

static bool simplexTest_fixed_variable()
{
    const char* name = "Simplex:fixed_variable";
    std::vector<std::vector<double>> A = {{1, 1, 1}};
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 3, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 1, 0});
    solver.setBounds({3, 0, 0}, {3, 10, INF_VAL});
    solver.setRHS({10});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 10.0) < LP_TOL, name, "obj should be 10");
    VCHECK(std::fabs(r.solution[0] - 3.0) < LP_TOL, name, "x1 should be 3");
    VCHECK(std::fabs(r.solution[1] - 7.0) < LP_TOL, name, "x2 should be 7");
    return true;
}

static bool simplexTest_warm_start()
{
    const char* name = "Simplex:warm_start";
    std::vector<std::vector<double>> A = {
        {6, 4, 1, 0}, {1, 2, 0, 1}
    };
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(2, 4, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({5, 4, 0, 0});
    solver.setBounds({0, 0, 0, 0}, {INF_VAL, INF_VAL, INF_VAL, INF_VAL});
    solver.setRHS({24, 6});

    sparseMath::LPResult r1 = solver.solve();
    VCHECK(r1.status == sparseMath::LPStatus::OPTIMAL, name, "first solve should be optimal");
    VCHECK(std::fabs(r1.objectiveValue - 21.0) < LP_TOL, name, "first obj should be 21");

    solver.setVariableBounds(0, 0, 2);
    sparseMath::LPResult r2 = solver.resolveWarmStart();
    VCHECK(r2.status == sparseMath::LPStatus::OPTIMAL, name, "warm-start should be optimal");
    VCHECK(std::fabs(r2.objectiveValue - 18.0) < LP_TOL, name, "warm-start obj should be 18");
    VCHECK(std::fabs(r2.solution[0] - 2.0) < LP_TOL, name, "x1 should be 2");
    VCHECK(std::fabs(r2.solution[1] - 2.0) < LP_TOL, name, "x2 should be 2");
    return true;
}

static bool simplexTest_stoichiometry()
{
    const char* name = "Simplex:stoichiometry";
    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC_RowMajor(S);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 10.0) < LP_TOL, name, "obj should be 10");
    VCHECK(std::fabs(r.solution[0] - 10.0) < LP_TOL, name, "R1 should be 10");
    VCHECK(std::fabs(r.solution[5] - 10.0) < LP_TOL, name, "R6 should be 10");
    if (!checkFeasibility(csc, {0, 0, 0, 0},
                          {0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10}, r.solution, name)) return false;
    return true;
}

static bool simplexTest_warm_start_knockout()
{
    const char* name = "Simplex:warm_start_knockout";
    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC_RowMajor(S);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    sparseMath::LPResult r1 = solver.solve();
    VCHECK(r1.status == sparseMath::LPStatus::OPTIMAL, name, "initial solve should be optimal");

    solver.setVariableBounds(2, 0, 0);
    sparseMath::LPResult r2 = solver.resolveWarmStart();
    VCHECK(r2.status == sparseMath::LPStatus::OPTIMAL, name, "knockout should be optimal");
    VCHECK(std::fabs(r2.objectiveValue - 10.0) < LP_TOL, name, "knockout obj should be 10");
    VCHECK(std::fabs(r2.solution[2]) < LP_TOL, name, "R3 should be 0 (knocked out)");
    VCHECK(std::fabs(r2.solution[4]) < LP_TOL, name, "R5 should be 0");
    VCHECK(std::fabs(r2.solution[1] - 10.0) < LP_TOL, name, "R2 should be 10");
    VCHECK(std::fabs(r2.solution[3] - 10.0) < LP_TOL, name, "R4 should be 10");
    VCHECK(std::fabs(r2.solution[5] - 10.0) < LP_TOL, name, "R6 should be 10");
    if (!checkFeasibility(csc, {0, 0, 0, 0},
                          {0, 0, 0, 0, 0, 0}, {10, 10, 0, 10, 10, 10}, r2.solution, name)) return false;
    return true;
}

static bool simplexTest_iteration_limit()
{
    const char* name = "Simplex:iteration_limit";
    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC_RowMajor(S);

    sparseMath::RevisedSimplex solver;
    sparseMath::RevisedSimplex::Params params;
    params.maxIterations = 0;
    solver.setParams(params);
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status != sparseMath::LPStatus::OPTIMAL, name, "should not be optimal with 0 max iterations");
    return true;
}

static bool simplexTest_redundant_constraints()
{
    const char* name = "Simplex:redundant_constraints";
    std::vector<std::vector<double>> A = {
        { 1, 1, 1, 0, 0 },
        { 1, 0, 0, 1, 0 },
        { 2, 1, 1, 1, 0 }
    };
    CSC csc = denseToCSC_RowMajor(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(3, 5, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 1, 0, 0, 0});
    solver.setBounds({0, 0, 0, 0, 0}, {INF_VAL, INF_VAL, INF_VAL, INF_VAL, INF_VAL});
    solver.setRHS({10, 6, 16});

    sparseMath::LPResult r = solver.solve();
    VCHECK(r.status == sparseMath::LPStatus::OPTIMAL, name, "should be optimal");
    VCHECK(std::fabs(r.objectiveValue - 10.0) < LP_TOL, name, "obj should be 10");
    VCHECK(std::fabs(r.solution[0] - 6.0) < LP_TOL, name, "x1 should be 6");
    VCHECK(std::fabs(r.solution[1] - 4.0) < LP_TOL, name, "x2 should be 4");
    return true;
}

// =======================================================================
// SteepestEdgePricer Tests (4)
// =======================================================================

static bool seTest_reset_and_score()
{
    const char* name = "SE:reset_and_score";
    sparseMath::SteepestEdgePricer pricer;
    pricer.reset(5);

    for (int j = 0; j < 5; ++j)
        VCHECK(std::fabs(pricer.weight(j) - 1.0) < 1e-15, name, "initial weight should be 1.0");

    VCHECK(std::fabs(pricer.score(3.0, 0) - 9.0) < 1e-12, name, "score(3,w=1) should be 9");
    VCHECK(std::fabs(pricer.score(-2.0, 1) - 4.0) < 1e-12, name, "score(-2,w=1) should be 4");
    return true;
}

static bool seTest_column_norms()
{
    const char* name = "SE:column_norms";
    // 2 rows, 3 columns: A = [[1,2,0],[0,3,4]]
    std::vector<int> colStart = {0, 1, 3, 4};
    std::vector<int> rowIdx = {0, 0, 1, 1};
    std::vector<double> values = {1.0, 2.0, 3.0, 4.0};

    sparseMath::SteepestEdgePricer pricer;
    pricer.initializeColumnNorms(3, 2, colStart, rowIdx, values);

    // col 0: ||[1,0]||^2 = 1
    VCHECK(std::fabs(pricer.weight(0) - 1.0) < 1e-12, name, "col 0 norm wrong");
    // col 1: ||[2,3]||^2 = 13
    VCHECK(std::fabs(pricer.weight(1) - 13.0) < 1e-12, name, "col 1 norm wrong");
    // col 2: ||[0,4]||^2 = 16
    VCHECK(std::fabs(pricer.weight(2) - 16.0) < 1e-12, name, "col 2 norm wrong");
    // artificials: 1.0
    VCHECK(std::fabs(pricer.weight(3) - 1.0) < 1e-12, name, "art 0 weight wrong");
    VCHECK(std::fabs(pricer.weight(4) - 1.0) < 1e-12, name, "art 1 weight wrong");
    return true;
}

static bool seTest_leaving_weight()
{
    const char* name = "SE:leaving_weight";
    sparseMath::SteepestEdgePricer pricer;
    pricer.reset(5);

    // d_p = 2.0, ||d||^2 = 5.0
    // w_l = (1 + 5 - 4) / 4 = 0.5
    std::vector<double> alphas(5, 0.0);
    std::vector<double> taus(5, 0.0);
    std::vector<bool> isBasic = {true, false, false, false, false};
    pricer.updateAfterPivot(0, 3, 2.0, 5.0, alphas, taus, isBasic, 5);

    VCHECK(std::fabs(pricer.weight(3) - 0.5) < 1e-12, name,
           "leaving weight should be (1+||d||^2-d_p^2)/d_p^2");
    return true;
}

static bool seTest_exact_update()
{
    const char* name = "SE:exact_update";
    // Verify the Goldfarb-Forrest formula:
    //   gamma_j = gamma_j - 2*(alpha/d_p)*(tau - alpha) + (alpha/d_p)^2 * sigma
    //   sigma = ||d||^2 - 2*d_p + 1
    sparseMath::SteepestEdgePricer pricer;
    pricer.reset(4);

    // enterCol=0, leaveCol=3, d_p=2.0, ||d||^2=10.0
    // sigma = 10 - 4 + 1 = 7
    // Non-basic col 1: alpha=1.0, tau=3.0, gamma_old=1.0
    //   gamma_new = 1.0 - 2*(0.5)*(3-1) + 0.25*7 = 1 - 2 + 1.75 = 0.75
    // Non-basic col 2: alpha=0.0, tau=0.0
    //   gamma_new = 1.0 - 0 + 0 = 1.0  (unaffected)
    std::vector<double> alphas = {0, 1.0, 0.0, 0};
    std::vector<double> taus   = {0, 3.0, 0.0, 0};
    std::vector<bool> isBasic  = {true, false, false, false};
    pricer.updateAfterPivot(0, 3, 2.0, 10.0, alphas, taus, isBasic, 4);

    VCHECK(std::fabs(pricer.weight(1) - 0.75) < 1e-12, name,
           "col 1 exact SE update wrong");
    VCHECK(std::fabs(pricer.weight(2) - 1.0) < 1e-12, name,
           "col 2 should be unaffected (alpha=0)");
    return true;
}

#undef VCHECK

} // anonymous namespace

// =======================================================================
// Public API
// =======================================================================

bool sparseMath::runValidation()
{
    static bool s_alreadyRun = false;
    if (s_alreadyRun)
        return true;
    s_alreadyRun = true;

    LOG_INFO("SparseMath: running solver validation (29 tests)...");  // 14 LU + 11 Simplex + 4 SE

    struct TestEntry { bool (*fn)(); const char* name; };
    TestEntry tests[] = {
        // SparseLU (14)
        { luTest_3x3_basic,                   "LU:3x3_basic" },
        { luTest_identity,                    "LU:identity" },
        { luTest_eta_updates,                 "LU:eta_updates" },
        { luTest_artificials,                 "LU:artificials" },
        { luTest_threshold_pivoting,          "LU:threshold_pivoting" },
        { luTest_singular,                    "LU:singular" },
        { luTest_sparse_7x7,                  "LU:sparse_7x7" },
        { luTest_many_etas_then_refactorize,  "LU:many_etas_refactorize" },
        { luTest_ftran_btran_inverse,         "LU:ftran_btran_inverse" },
        { luTest_amd_ordering,                "LU:amd_ordering" },
        { luTest_column_subset,               "LU:column_subset" },
        { luTest_multiple_rhs,                "LU:multiple_rhs" },
        { luTest_non_involution_permutation,  "LU:non_involution_perm" },
        { luTest_solve_after_failed_factorize,"LU:solve_after_failed_factorize" },
        // RevisedSimplex (11)
        { simplexTest_trivial_1var,           "Simplex:trivial_1var" },
        { simplexTest_2var_textbook,          "Simplex:2var_textbook" },
        { simplexTest_infeasible,             "Simplex:infeasible" },
        { simplexTest_unbounded,              "Simplex:unbounded" },
        { simplexTest_degenerate,             "Simplex:degenerate" },
        { simplexTest_fixed_variable,         "Simplex:fixed_variable" },
        { simplexTest_warm_start,             "Simplex:warm_start" },
        { simplexTest_stoichiometry,          "Simplex:stoichiometry" },
        { simplexTest_warm_start_knockout,    "Simplex:warm_start_knockout" },
        { simplexTest_iteration_limit,        "Simplex:iteration_limit" },
        { simplexTest_redundant_constraints,  "Simplex:redundant_constraints" },
        // SteepestEdgePricer (4)
        { seTest_reset_and_score,             "SE:reset_and_score" },
        { seTest_column_norms,                "SE:column_norms" },
        { seTest_leaving_weight,              "SE:leaving_weight" },
        { seTest_exact_update,                "SE:exact_update" },
    };

    int total = static_cast<int>(std::size(tests));
    for (int i = 0; i < total; ++i)
    {
        if (!tests[i].fn())
        {
            LOG_ERROR("SparseMath validation FAILED at test %d/%d: %s", i + 1, total, tests[i].name);
            return false;
        }
    }

    LOG_INFO("SparseMath: all %d validation tests passed.", total);
    return true;
}
