#pragma once

#include <vector>

namespace sparseMath {

// Approximate Minimum Degree column ordering for sparse matrices.
// Returns a column permutation that approximately minimizes fill-in
// during LU factorization.
class AmdOrdering
{
public:
    // Compute AMD ordering for a matrix in CSC format.
    // Returns a column permutation vector of size nCols.
    static std::vector<int> compute(int nCols,
                                    const std::vector<int>& colStart,
                                    const std::vector<int>& rowIdx);
};

} // namespace sparseMath
