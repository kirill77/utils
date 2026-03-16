// RevisedSimplex unit tests.
// Standalone console exe — no test framework, just assert().
// Build as x64|Debug to keep asserts enabled.

#include "utils/sparseMath/RevisedSimplex.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <limits>

static constexpr double TOL = 1e-6;
static constexpr double INF = std::numeric_limits<double>::infinity();

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

// CSC (compressed sparse column) representation.
struct CSC
{
    int nRows, nCols;
    std::vector<int> colStart;
    std::vector<int> rowIdx;
    std::vector<double> values;
};

// Build CSC from dense row-major matrix A[row][col].
static CSC denseToCSC(const std::vector<std::vector<double>>& A)
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

// Verify solution feasibility: Ax ~ b and lb <= x <= ub.
static void checkFeasibility(const CSC& csc,
                              const std::vector<double>& b,
                              const std::vector<double>& lb,
                              const std::vector<double>& ub,
                              const std::vector<double>& x)
{
    std::vector<double> Ax(csc.nRows, 0.0);
    for (int j = 0; j < csc.nCols; ++j)
        for (int p = csc.colStart[j]; p < csc.colStart[j + 1]; ++p)
            Ax[csc.rowIdx[p]] += csc.values[p] * x[j];

    for (int i = 0; i < csc.nRows; ++i)
        CHECK(std::fabs(Ax[i] - b[i]) < TOL, "Ax != b: constraint violated");

    for (int j = 0; j < csc.nCols; ++j)
    {
        if (lb[j] > -INF)
            CHECK(x[j] >= lb[j] - TOL, "x < lb: lower bound violated");
        if (ub[j] < INF)
            CHECK(x[j] <= ub[j] + TOL, "x > ub: upper bound violated");
    }
}

// -----------------------------------------------------------------------
// Test 1: Trivial 1-variable LP
//   max x  s.t.  x + s = 5,  x,s >= 0
//   Expected: x=5, s=0, obj=5
// -----------------------------------------------------------------------
static void test_trivial_1var()
{
    printf("test_trivial_1var... ");

    std::vector<std::vector<double>> A = {{1, 1}};
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({0, 0}, {INF, INF});
    solver.setRHS({5});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 5.0) < TOL, "obj should be 5");
    CHECK(std::fabs(r.solution[0] - 5.0) < TOL, "x should be 5");
    CHECK(std::fabs(r.solution[1] - 0.0) < TOL, "s should be 0");

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 2: Classic 2-variable textbook LP
//   max 5x1 + 4x2
//   s.t. 6x1 + 4x2 <= 24   (+ s1)
//         x1 + 2x2 <= 6    (+ s2)
//        x1, x2, s1, s2 >= 0
//   Optimal: x1=3, x2=3/2, obj=21
// -----------------------------------------------------------------------
static void test_2var_textbook()
{
    printf("test_2var_textbook... ");

    std::vector<std::vector<double>> A = {
        {6, 4, 1, 0},
        {1, 2, 0, 1}
    };
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(2, 4, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({5, 4, 0, 0});
    solver.setBounds({0, 0, 0, 0}, {INF, INF, INF, INF});
    solver.setRHS({24, 6});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 21.0) < TOL, "obj should be 21");
    CHECK(std::fabs(r.solution[0] - 3.0) < TOL, "x1 should be 3");
    CHECK(std::fabs(r.solution[1] - 1.5) < TOL, "x2 should be 3/2");
    checkFeasibility(csc, {24, 6}, {0, 0, 0, 0}, {INF, INF, INF, INF},
                     r.solution);

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 3: Infeasible LP
//   max x1  s.t.  x1 + s = 5,  x1 in [10, 20],  s >= 0
//   x1 >= 10 but x1 + s = 5, s >= 0  =>  x1 <= 5.  Contradiction.
// -----------------------------------------------------------------------
static void test_infeasible()
{
    printf("test_infeasible... ");

    std::vector<std::vector<double>> A = {{1, 1}};
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({10, 0}, {20, INF});
    solver.setRHS({5});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::INFEASIBLE, "should be infeasible");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 4: Unbounded LP
//   max x1  s.t.  x1 - x2 = 0,  x1,x2 >= 0
//   x1 = x2, both can grow without limit.
// -----------------------------------------------------------------------
static void test_unbounded()
{
    printf("test_unbounded... ");

    std::vector<std::vector<double>> A = {{1, -1}};
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 2, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0});
    solver.setBounds({0, 0}, {INF, INF});
    solver.setRHS({0});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::UNBOUNDED, "should be unbounded");

    printf("OK\n");
}

// -----------------------------------------------------------------------
// Test 5: Degenerate LP (multiple active constraints at the vertex)
//   max x1
//   s.t.  x1 + x2 + s1          = 1
//         x1           + s2      = 1
//              x2           + s3 = 1
//   All >= 0.  Optimal: x1=1, x2=0 with s1=0 (degenerate), s2=0, s3=1.
// -----------------------------------------------------------------------
static void test_degenerate()
{
    printf("test_degenerate... ");

    std::vector<std::vector<double>> A = {
        {1, 1, 1, 0, 0},
        {1, 0, 0, 1, 0},
        {0, 1, 0, 0, 1}
    };
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(3, 5, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 0, 0, 0, 0});
    solver.setBounds({0, 0, 0, 0, 0}, {INF, INF, INF, INF, INF});
    solver.setRHS({1, 1, 1});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 1.0) < TOL, "obj should be 1");
    CHECK(std::fabs(r.solution[0] - 1.0) < TOL, "x1 should be 1");
    checkFeasibility(csc, {1, 1, 1}, {0, 0, 0, 0, 0},
                     {INF, INF, INF, INF, INF}, r.solution);

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 6: Fixed variable (lb == ub)
//   max x1 + x2  s.t.  x1 + x2 + s = 10
//   x1 in [3, 3] (fixed), x2 in [0, 10], s >= 0
//   Expected: x1=3, x2=7, obj=10
// -----------------------------------------------------------------------
static void test_fixed_variable()
{
    printf("test_fixed_variable... ");

    std::vector<std::vector<double>> A = {{1, 1, 1}};
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(1, 3, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 1, 0});
    solver.setBounds({3, 0, 0}, {3, 10, INF});
    solver.setRHS({10});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 10.0) < TOL, "obj should be 10");
    CHECK(std::fabs(r.solution[0] - 3.0) < TOL, "x1 should be 3");
    CHECK(std::fabs(r.solution[1] - 7.0) < TOL, "x2 should be 7");

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 7: Warm-start after bound change
//   Start with the textbook LP (test 2), solve, then tighten x1 <= 2,
//   resolveWarmStart.
//   New optimal: x1=2, x2=2, obj=18
// -----------------------------------------------------------------------
static void test_warm_start()
{
    printf("test_warm_start... ");

    std::vector<std::vector<double>> A = {
        {6, 4, 1, 0},
        {1, 2, 0, 1}
    };
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(2, 4, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({5, 4, 0, 0});
    solver.setBounds({0, 0, 0, 0}, {INF, INF, INF, INF});
    solver.setRHS({24, 6});

    // First solve
    sparseMath::LPResult r1 = solver.solve();
    CHECK(r1.status == sparseMath::LPStatus::OPTIMAL, "first solve should be optimal");
    CHECK(std::fabs(r1.objectiveValue - 21.0) < TOL, "first obj should be 21");

    // Tighten x1 <= 2
    solver.setVariableBounds(0, 0, 2);

    // Warm-start re-solve
    sparseMath::LPResult r2 = solver.resolveWarmStart();
    CHECK(r2.status == sparseMath::LPStatus::OPTIMAL, "warm-start should be optimal");
    CHECK(std::fabs(r2.objectiveValue - 18.0) < TOL, "warm-start obj should be 18");
    CHECK(std::fabs(r2.solution[0] - 2.0) < TOL, "x1 should be 2");
    CHECK(std::fabs(r2.solution[1] - 2.0) < TOL, "x2 should be 2");

    printf("OK (obj = %.4f)\n", r2.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 8: Stoichiometry-like LP (small metabolic network)
//   4 metabolites (A, B, C, D), 6 reactions:
//     R1: -> A       R2: A -> B     R3: A -> C
//     R4: B -> D     R5: C -> D     R6: D ->
//   Steady-state: Sv = 0.  Maximize R6 (biomass).
//   All fluxes in [0, 10].
//   Expected: R1=10, R6=10, obj=10
// -----------------------------------------------------------------------
static void test_stoichiometry()
{
    printf("test_stoichiometry... ");

    //         R1  R2  R3  R4  R5  R6
    // A:   [  1  -1  -1   0   0   0 ]
    // B:   [  0   1   0  -1   0   0 ]
    // C:   [  0   0   1   0  -1   0 ]
    // D:   [  0   0   0   1   1  -1 ]
    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC(S);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 10.0) < TOL, "obj should be 10");
    CHECK(std::fabs(r.solution[0] - 10.0) < TOL, "R1 should be 10");
    CHECK(std::fabs(r.solution[5] - 10.0) < TOL, "R6 should be 10");
    checkFeasibility(csc, {0, 0, 0, 0},
                     {0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10},
                     r.solution);

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 9: Warm-start knockout simulation
//   Solve the metabolic network, then knock out R3 (set ub=0).
//   All flux redirects through R2->R4 path.
//   Expected: R3=0, R5=0, R2=10, R4=10, R6=10, obj=10
// -----------------------------------------------------------------------
static void test_warm_start_knockout()
{
    printf("test_warm_start_knockout... ");

    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC(S);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    // Initial solve
    sparseMath::LPResult r1 = solver.solve();
    CHECK(r1.status == sparseMath::LPStatus::OPTIMAL, "initial solve should be optimal");
    CHECK(std::fabs(r1.objectiveValue - 10.0) < TOL, "initial obj should be 10");

    // Knockout R3 (column 2)
    solver.setVariableBounds(2, 0, 0);

    // Warm-start re-solve
    sparseMath::LPResult r2 = solver.resolveWarmStart();
    CHECK(r2.status == sparseMath::LPStatus::OPTIMAL, "knockout should be optimal");
    CHECK(std::fabs(r2.objectiveValue - 10.0) < TOL, "knockout obj should be 10");
    CHECK(std::fabs(r2.solution[2]) < TOL, "R3 should be 0 (knocked out)");
    CHECK(std::fabs(r2.solution[4]) < TOL, "R5 should be 0 (no C produced)");
    CHECK(std::fabs(r2.solution[1] - 10.0) < TOL, "R2 should be 10");
    CHECK(std::fabs(r2.solution[3] - 10.0) < TOL, "R4 should be 10");
    CHECK(std::fabs(r2.solution[5] - 10.0) < TOL, "R6 should be 10");
    checkFeasibility(csc, {0, 0, 0, 0},
                     {0, 0, 0, 0, 0, 0}, {10, 10, 0, 10, 10, 10},
                     r2.solution);

    printf("OK (obj = %.4f)\n", r2.objectiveValue);
}

// -----------------------------------------------------------------------
// Test 10: Iteration limit
//   Use the stoichiometry LP with maxIterations = 0.
//   Solver should not find optimal (proves limit is respected).
// -----------------------------------------------------------------------
static void test_iteration_limit()
{
    printf("test_iteration_limit... ");

    std::vector<std::vector<double>> S = {
        { 1, -1, -1,  0,  0,  0},
        { 0,  1,  0, -1,  0,  0},
        { 0,  0,  1,  0, -1,  0},
        { 0,  0,  0,  1,  1, -1}
    };
    CSC csc = denseToCSC(S);

    sparseMath::RevisedSimplex solver;
    sparseMath::RevisedSimplex::Params params;
    params.maxIterations = 0;
    solver.setParams(params);
    solver.loadProblem(4, 6, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({0, 0, 0, 0, 0, 1});
    solver.setBounds({0, 0, 0, 0, 0, 0}, {10, 10, 10, 10, 10, 10});
    solver.setRHS({0, 0, 0, 0});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status != sparseMath::LPStatus::OPTIMAL,
          "should not be optimal with 0 max iterations");

    printf("OK (status = %s)\n",
           r.status == sparseMath::LPStatus::INFEASIBLE ? "INFEASIBLE" :
           r.status == sparseMath::LPStatus::MAX_ITERATIONS ? "MAX_ITERATIONS" :
           r.status == sparseMath::LPStatus::UNBOUNDED ? "UNBOUNDED" : "?");
}

// -----------------------------------------------------------------------
// Test 11: Redundant constraints (linearly dependent row)
//   max x1 + x2
//   s.t.  x1 + x2 + s1               = 10   (row 0)
//         x1           + s2           = 6    (row 1)
//        2x1 + x2 + s1 + s2          = 16   (row 2 = row0 + row1, redundant)
//   x1,x2,s1,s2,s3 >= 0   (s3 is slack for row 2 but row is exact)
//   6 columns total: x1, x2, s1, s2, s3_unused, plus the 3-slack structure.
//
//   Phase I cannot replace the artificial for the redundant row.
//   Before the fix this assert-crashes in rebuildIsBasic().
//   Expected: x1=6, x2=4, obj=10
// -----------------------------------------------------------------------
static void test_redundant_constraints()
{
    printf("test_redundant_constraints... ");

    //         x1  x2  s1  s2  s3
    // row0: [  1   1   1   0   0 ]  = 10
    // row1: [  1   0   0   1   0 ]  = 6
    // row2: [  2   1   1   1   0 ]  = 16  (row0 + row1)
    std::vector<std::vector<double>> A = {
        { 1, 1, 1, 0, 0 },
        { 1, 0, 0, 1, 0 },
        { 2, 1, 1, 1, 0 }
    };
    CSC csc = denseToCSC(A);

    sparseMath::RevisedSimplex solver;
    solver.loadProblem(3, 5, csc.colStart, csc.rowIdx, csc.values);
    solver.setObjective({1, 1, 0, 0, 0});
    solver.setBounds({0, 0, 0, 0, 0}, {INF, INF, INF, INF, INF});
    solver.setRHS({10, 6, 16});

    sparseMath::LPResult r = solver.solve();
    CHECK(r.status == sparseMath::LPStatus::OPTIMAL, "should be optimal");
    CHECK(std::fabs(r.objectiveValue - 10.0) < TOL, "obj should be 10");
    CHECK(std::fabs(r.solution[0] - 6.0) < TOL, "x1 should be 6");
    CHECK(std::fabs(r.solution[1] - 4.0) < TOL, "x2 should be 4");

    printf("OK (obj = %.4f)\n", r.objectiveValue);
}

// -----------------------------------------------------------------------
// main
// -----------------------------------------------------------------------
int main()
{
    printf("=== RevisedSimplex Tests ===\n\n");

    test_trivial_1var();
    test_2var_textbook();
    test_infeasible();
    test_unbounded();
    test_degenerate();
    test_fixed_variable();
    test_warm_start();
    test_stoichiometry();
    test_warm_start_knockout();
    test_iteration_limit();
    test_redundant_constraints();

    printf("\n=== All %d tests passed ===\n", 11);
    return 0;
}
