// SparseLU unit tests.
// Standalone console exe — no test framework, just assert().
// Build as x64|Debug to keep asserts enabled.

#include "utils/sparseMath/SparseLU.h"
#include "utils/sparseMath/AmdOrdering.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr double TOL = 1e-10;

static void check(bool cond, const char* msg, int line)
{
    if (!cond)
    {
        printf("FAIL line %d: %s\n", line, msg);
        assert(false);
    }
}
#define CHECK(cond, msg) check((cond), (msg), __LINE__)

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

// Dense matrix (row-major) × vector: y = A * x
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

// Dense matrix transpose × vector: y = A^T * x
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
        err = std::max(err, std::fabs(a[i] - b[i]));
    return err;
}

// Build CSC from dense column-major matrix.
// dense[col][row] — each inner vector is one column.
struct CSC
{
    int nRows, nCols;
    std::vector<int> colStart;
    std::vector<int> rowIdx;
    std::vector<double> values;
};

static CSC denseToCSC(const std::vector<std::vector<double>>& cols, int nRows)
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

// -----------------------------------------------------------------------
// Test 1: 3×3 dense system — factorize, FTRAN, BTRAN
// -----------------------------------------------------------------------
static void test_3x3_basic()
{
    printf("test_3x3_basic... ");

    // B = [2  1  0]
    //     [1  3  1]
    //     [0  1  2]
    std::vector<std::vector<double>> B = {
        {2, 1, 0},
        {1, 3, 1},
        {0, 1, 2}
    };

    // Store as columns for CSC
    std::vector<std::vector<double>> cols = {
        {2, 1, 0},  // col 0
        {1, 3, 1},  // col 1
        {0, 1, 2},  // col 2
    };
    CSC csc = denseToCSC(cols, 3);

    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, 3);
    CHECK(ok, "factorize should succeed");

    // FTRAN: solve B*x = rhs
    std::vector<double> rhs = {1.0, 2.0, 3.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);

    // Verify: B*x should equal rhs
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "FTRAN: B*x != rhs");

    // BTRAN: solve B^T*y = rhs
    std::vector<double> y = rhs;
    lu.solveLeft(y);

    // Verify: B^T*y should equal rhs
    std::vector<double> BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "BTRAN: B^T*y != rhs");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 2: identity matrix
// -----------------------------------------------------------------------
static void test_identity()
{
    printf("test_identity... ");

    int n = 5;
    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        cols[i][i] = 1.0;

    CSC csc = denseToCSC(cols, n);
    std::vector<int> basisCols = {0, 1, 2, 3, 4};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, n);
    CHECK(ok, "identity factorize should succeed");

    std::vector<double> rhs = {7, -3, 0, 11, -5};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    double err = maxAbsError(x, rhs);
    CHECK(err < TOL, "FTRAN on identity should return rhs unchanged");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    err = maxAbsError(y, rhs);
    CHECK(err < TOL, "BTRAN on identity should return rhs unchanged");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 3: eta updates — factorize, do column replacements, verify solves
// -----------------------------------------------------------------------
static void test_eta_updates()
{
    printf("test_eta_updates... ");

    // Start with B0 = [2 1 0]
    //                  [1 3 1]
    //                  [0 1 2]
    std::vector<std::vector<double>> B = {
        {2, 1, 0},
        {1, 3, 1},
        {0, 1, 2}
    };
    std::vector<std::vector<double>> cols = {
        {2, 1, 0},
        {1, 3, 1},
        {0, 1, 2},
    };
    CSC csc = denseToCSC(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, 3);
    CHECK(ok, "initial factorize failed");

    // Simulate replacing column 1 with new column [4, 1, 2]
    // New B1 = [2 4 0]
    //          [1 1 1]
    //          [0 2 2]
    std::vector<double> newCol = {4, 1, 2};

    // FTRAN the new column to get the representation in current basis
    std::vector<double> ftranCol = newCol;
    lu.solveRight(ftranCol);

    // Update: column 1 leaves (row 1 in basis), new column enters
    ok = lu.update(1, ftranCol);
    CHECK(ok, "eta update should succeed");

    // Update our dense B to match
    B[0][1] = 4; B[1][1] = 1; B[2][1] = 2;

    // Verify FTRAN with updated factorization
    std::vector<double> rhs = {1.0, 2.0, 3.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "FTRAN after 1 eta update: B*x != rhs");

    // Verify BTRAN
    std::vector<double> y = rhs;
    lu.solveLeft(y);
    std::vector<double> BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "BTRAN after 1 eta update: B^T*y != rhs");

    // Do a second update: replace column 2 with [1, 0, 5]
    // New B2 = [2 4 1]
    //          [1 1 0]
    //          [0 2 5]
    std::vector<double> newCol2 = {1, 0, 5};
    std::vector<double> ftranCol2 = newCol2;
    lu.solveRight(ftranCol2);
    ok = lu.update(2, ftranCol2);
    CHECK(ok, "second eta update should succeed");

    B[0][2] = 1; B[1][2] = 0; B[2][2] = 5;

    // Verify FTRAN
    x = rhs;
    lu.solveRight(x);
    Bx = matvec(B, x);
    err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "FTRAN after 2 eta updates: B*x != rhs");

    // Verify BTRAN
    y = rhs;
    lu.solveLeft(y);
    BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "BTRAN after 2 eta updates: B^T*y != rhs");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 4: artificial variable columns
// -----------------------------------------------------------------------
static void test_artificials()
{
    printf("test_artificials... ");

    // Basis: 2 original columns + 1 artificial
    // Original cols: col0 = [3, 1], col1 = [1, 2]
    // Artificial for row 0 with sign +1, row 1 with sign -1
    // Basis = [col0, artificial_row1]  →  [3  0]
    //                                     [1 -1]
    // basisCols = {0, nOrigCols+1}  where nOrigCols=2, so artificial is col index 3

    std::vector<std::vector<double>> cols = {
        {3, 1},   // col 0
        {1, 2},   // col 1
    };
    CSC csc = denseToCSC(cols, 2);

    std::vector<int> basisCols = {0, 3};  // col 0 is original, col 3 = artificial row 1
    std::vector<int> artSign = {1, -1};   // row 0: +1, row 1: -1
    int nOrigCols = 2;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, nOrigCols);
    CHECK(ok, "factorize with artificials should succeed");

    // B = [3   0]
    //     [1  -1]
    std::vector<std::vector<double>> B = {
        {3,  0},
        {1, -1}
    };

    std::vector<double> rhs = {6.0, -2.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "FTRAN with artificials: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    std::vector<double> BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "BTRAN with artificials: B^T*y != rhs");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 5: near-singular matrix (tests threshold pivoting)
// -----------------------------------------------------------------------
static void test_threshold_pivoting()
{
    printf("test_threshold_pivoting... ");

    // B = [1e-14  1]
    //     [1      1]
    // Without pivoting, using 1e-14 as pivot would amplify errors.
    // Threshold pivoting should swap rows.
    std::vector<std::vector<double>> cols = {
        {1e-14, 1.0},
        {1.0,   1.0},
    };
    CSC csc = denseToCSC(cols, 2);

    std::vector<int> basisCols = {0, 1};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, 2);
    CHECK(ok, "near-singular factorize should succeed with pivoting");

    std::vector<std::vector<double>> B = {
        {1e-14, 1.0},
        {1.0,   1.0}
    };

    std::vector<double> rhs = {1.0, 2.0};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < 1e-8, "FTRAN near-singular: large residual");

    printf("OK (residual = %.2e)\n", err);
}

// -----------------------------------------------------------------------
// Test 6: truly singular matrix should fail
// -----------------------------------------------------------------------
static void test_singular()
{
    printf("test_singular... ");

    // B = [1 2]
    //     [2 4]  — rank 1
    std::vector<std::vector<double>> cols = {
        {1, 2},
        {2, 4},
    };
    CSC csc = denseToCSC(cols, 2);

    std::vector<int> basisCols = {0, 1};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(2, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, 2);
    CHECK(!ok, "singular matrix should fail factorization");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 7: larger sparse system (stoichiometry-like)
// -----------------------------------------------------------------------
static void test_sparse_7x7()
{
    printf("test_sparse_7x7... ");

    // 7×7 sparse matrix resembling a stoichiometric submatrix.
    // Each column has 2-3 nonzeros.
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

    // Build CSC from row-major B
    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            cols[j][i] = B[i][j];

    CSC csc = denseToCSC(cols, n);
    std::vector<int> basisCols(n);
    for (int j = 0; j < n; ++j)
        basisCols[j] = j;
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, n);
    CHECK(ok, "7x7 sparse factorize should succeed");

    std::vector<double> rhs = {1, -1, 2, 0, 3, -2, 1};

    // FTRAN
    std::vector<double> x = rhs;
    lu.solveRight(x);
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "7x7 FTRAN: B*x != rhs");

    // BTRAN
    std::vector<double> y = rhs;
    lu.solveLeft(y);
    std::vector<double> BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "7x7 BTRAN: B^T*y != rhs");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 8: multiple eta updates then refactorize — values should match
// -----------------------------------------------------------------------
static void test_many_etas_then_refactorize()
{
    printf("test_many_etas_then_refactorize... ");

    // Start with 4×4 matrix, do several column replacements via eta,
    // then refactorize from scratch and compare solutions.
    int n = 4;
    // B = [4 1 0 0]
    //     [1 4 1 0]
    //     [0 1 4 1]
    //     [0 0 1 4]
    std::vector<std::vector<double>> Bdense = {
        {4, 1, 0, 0},
        {1, 4, 1, 0},
        {0, 1, 4, 1},
        {0, 0, 1, 4}
    };
    std::vector<std::vector<double>> cols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            cols[j][i] = Bdense[i][j];

    CSC csc = denseToCSC(cols, n);
    std::vector<int> basisCols = {0, 1, 2, 3};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(n, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, n);
    CHECK(ok, "4x4 initial factorize failed");

    // Replacement columns
    std::vector<double> newCols[] = {
        {3, 0, 1, 0},  // replace col 0
        {0, 2, 0, 1},  // replace col 2
        {1, 1, 1, 3},  // replace col 3
    };
    int replaceRows[] = {0, 2, 3};

    for (int k = 0; k < 3; ++k)
    {
        std::vector<double> ftranCol = newCols[k];
        lu.solveRight(ftranCol);
        ok = lu.update(replaceRows[k], ftranCol);
        CHECK(ok, "eta update failed");

        // Update Bdense
        for (int i = 0; i < n; ++i)
            Bdense[i][replaceRows[k]] = newCols[k][i];
    }

    // Solve with eta-updated factorization
    std::vector<double> rhs = {5, -3, 7, 1};
    std::vector<double> x_eta = rhs;
    lu.solveRight(x_eta);

    // Verify against dense multiply
    std::vector<double> Bx = matvec(Bdense, x_eta);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < 1e-8, "FTRAN after 3 eta updates: large residual");

    // Now refactorize from scratch with the updated matrix
    // and solve again — solutions should match
    std::vector<std::vector<double>> updatedCols(n, std::vector<double>(n, 0.0));
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            updatedCols[j][i] = Bdense[i][j];

    CSC csc2 = denseToCSC(updatedCols, n);
    sparseMath::SparseLU lu2;
    ok = lu2.factorize(n, basisCols, csc2.colStart, csc2.rowIdx, csc2.values,
                       artSign, n);
    CHECK(ok, "refactorize failed");

    std::vector<double> x_fresh = rhs;
    lu2.solveRight(x_fresh);

    double diff = maxAbsError(x_eta, x_fresh);
    CHECK(diff < 1e-8, "eta solution diverged from fresh factorization");

    printf("OK (eta vs fresh diff = %.2e)\n", diff);
}

// -----------------------------------------------------------------------
// Test 9: FTRAN then BTRAN are inverses of each other
// -----------------------------------------------------------------------
static void test_ftran_btran_inverse()
{
    printf("test_ftran_btran_inverse... ");

    // For any matrix B: if FTRAN solves B*x = e_k, then BTRAN on x
    // should give B^{-T}*x. Instead, verify the simpler property:
    //   x^T * (B * z) == (B^T * x)^T * z  for random x, z
    // via: FTRAN(rhs1) dot rhs2 == rhs1 dot BTRAN(rhs2)
    // This verifies FTRAN and BTRAN are transposes of each other.

    std::vector<std::vector<double>> cols = {
        {5, 2, 1},
        {1, 6, 2},
        {0, 1, 4},
    };
    CSC csc = denseToCSC(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values,
                 artSign, 3);

    std::vector<double> rhs1 = {1.0, 0.0, 0.0};
    std::vector<double> rhs2 = {0.0, 1.0, 0.0};

    std::vector<double> x = rhs1;
    lu.solveRight(x);  // x = B^{-1} * rhs1

    std::vector<double> y = rhs2;
    lu.solveLeft(y);   // y = B^{-T} * rhs2

    // Check: x . rhs2 == rhs1 . y
    // i.e., (B^{-1} rhs1)^T rhs2 == rhs1^T (B^{-T} rhs2)
    double lhs = 0, rval = 0;
    for (int i = 0; i < 3; ++i)
    {
        lhs += x[i] * rhs2[i];
        rval += rhs1[i] * y[i];
    }
    double err = std::fabs(lhs - rval);
    CHECK(err < TOL, "FTRAN/BTRAN transpose consistency failed");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 10: AMD ordering — verify it produces a valid permutation
// -----------------------------------------------------------------------
static void test_amd_ordering()
{
    printf("test_amd_ordering... ");

    // 5×5 sparse matrix
    std::vector<std::vector<double>> cols = {
        {1, 0, 1, 0, 0},
        {0, 1, 0, 1, 0},
        {1, 0, 1, 0, 1},
        {0, 1, 0, 1, 0},
        {0, 0, 1, 0, 1},
    };
    CSC csc = denseToCSC(cols, 5);

    std::vector<int> perm = sparseMath::AmdOrdering::compute(5, csc.colStart, csc.rowIdx);

    CHECK(static_cast<int>(perm.size()) == 5, "AMD perm wrong size");

    // Check it's a valid permutation
    std::vector<bool> seen(5, false);
    for (int j = 0; j < 5; ++j)
    {
        CHECK(perm[j] >= 0 && perm[j] < 5, "AMD perm out of range");
        CHECK(!seen[perm[j]], "AMD perm has duplicates");
        seen[perm[j]] = true;
    }

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 11: column subset — factorize a basis from a larger matrix
// -----------------------------------------------------------------------
static void test_column_subset()
{
    printf("test_column_subset... ");

    // 3×5 matrix A, pick columns {1, 3, 4} as basis
    //     col0  col1  col2  col3  col4
    // A = [ 1    2     0    1     0 ]
    //     [ 0    1     3    0     2 ]
    //     [ 1    0     1    1     1 ]
    std::vector<std::vector<double>> cols = {
        {1, 0, 1},  // col 0
        {2, 1, 0},  // col 1
        {0, 3, 1},  // col 2
        {1, 0, 1},  // col 3
        {0, 2, 1},  // col 4
    };
    CSC csc = denseToCSC(cols, 3);

    std::vector<int> basisCols = {1, 3, 4};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    bool ok = lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values,
                           artSign, 5);
    CHECK(ok, "column subset factorize failed");

    // Basis matrix B = [cols[1] cols[3] cols[4]] =
    //   [2 1 0]
    //   [1 0 2]
    //   [0 1 1]
    std::vector<std::vector<double>> B = {
        {2, 1, 0},
        {1, 0, 2},
        {0, 1, 1}
    };

    std::vector<double> rhs = {3, 4, 2};
    std::vector<double> x = rhs;
    lu.solveRight(x);
    std::vector<double> Bx = matvec(B, x);
    double err = maxAbsError(Bx, rhs);
    CHECK(err < TOL, "column subset FTRAN: B*x != rhs");

    std::vector<double> y = rhs;
    lu.solveLeft(y);
    std::vector<double> BTy = matvecT(B, y);
    err = maxAbsError(BTy, rhs);
    CHECK(err < TOL, "column subset BTRAN: B^T*y != rhs");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 12: multiple RHS — same factorization, different right-hand sides
// -----------------------------------------------------------------------
static void test_multiple_rhs()
{
    printf("test_multiple_rhs... ");

    std::vector<std::vector<double>> B = {
        {3, 1, 0},
        {1, 4, 1},
        {0, 1, 3}
    };
    std::vector<std::vector<double>> cols = {
        {3, 1, 0},
        {1, 4, 1},
        {0, 1, 3},
    };
    CSC csc = denseToCSC(cols, 3);
    std::vector<int> basisCols = {0, 1, 2};
    std::vector<int> artSign;

    sparseMath::SparseLU lu;
    lu.factorize(3, basisCols, csc.colStart, csc.rowIdx, csc.values,
                 artSign, 3);

    // Solve with many different RHS vectors
    std::vector<std::vector<double>> rhsVecs = {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
        {1, 1, 1},
        {100, -50, 200},
        {-1e6, 1e6, -1e6},
    };

    for (size_t t = 0; t < rhsVecs.size(); ++t)
    {
        std::vector<double> x = rhsVecs[t];
        lu.solveRight(x);
        std::vector<double> Bx = matvec(B, x);
        double err = maxAbsError(Bx, rhsVecs[t]);
        CHECK(err < 1e-6, "multiple RHS FTRAN failed");

        std::vector<double> y = rhsVecs[t];
        lu.solveLeft(y);
        std::vector<double> BTy = matvecT(B, y);
        err = maxAbsError(BTy, rhsVecs[t]);
        CHECK(err < 1e-6, "multiple RHS BTRAN failed");
    }

    printf("OK (%d RHS vectors)\n", static_cast<int>(rhsVecs.size()));
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main()
{
    printf("=== SparseLU Tests ===\n\n");

    test_3x3_basic();
    test_identity();
    test_eta_updates();
    test_artificials();
    test_threshold_pivoting();
    test_singular();
    test_sparse_7x7();
    test_many_etas_then_refactorize();
    test_ftran_btran_inverse();
    test_amd_ordering();
    test_column_subset();
    test_multiple_rhs();

    printf("\n=== All tests passed ===\n");
    return 0;
}
