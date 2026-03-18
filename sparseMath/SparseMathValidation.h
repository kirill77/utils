#pragma once

namespace sparseMath {
    // Runs all SparseLU and RevisedSimplex validation tests.
    // Returns true if all pass. Returns early (true) if already run.
    bool runValidation();
}
