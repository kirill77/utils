#include "utils/sparseMath/AmdOrdering.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace sparseMath {

// Build the symmetric sparsity pattern of A^T*A (column intersection graph),
// then greedily eliminate the minimum-degree node, updating neighbor degrees.
std::vector<int> AmdOrdering::compute(int nCols,
                                       const std::vector<int>& colStart,
                                       const std::vector<int>& rowIdx)
{
    // Build adjacency lists for the column intersection graph.
    // Two columns are adjacent if they share at least one row index.
    // We build row→column lists first, then derive column adjacency.
    int nRows = 0;
    for (int j = 0; j < nCols; ++j)
    {
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
        {
            if (rowIdx[p] >= nRows)
                nRows = rowIdx[p] + 1;
        }
    }

    // row → set of columns
    std::vector<std::vector<int>> rowToCols(nRows);
    for (int j = 0; j < nCols; ++j)
    {
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
            rowToCols[rowIdx[p]].push_back(j);
    }

    // Column adjacency (symmetric). Using unordered_set to de-duplicate.
    std::vector<std::unordered_set<int>> adj(nCols);
    for (int i = 0; i < nRows; ++i)
    {
        const auto& cols = rowToCols[i];
        for (size_t a = 0; a < cols.size(); ++a)
        {
            for (size_t b = a + 1; b < cols.size(); ++b)
            {
                adj[cols[a]].insert(cols[b]);
                adj[cols[b]].insert(cols[a]);
            }
        }
    }

    // Degree of each node in the elimination graph
    std::vector<int> degree(nCols);
    for (int j = 0; j < nCols; ++j)
        degree[j] = static_cast<int>(adj[j].size());

    std::vector<bool> eliminated(nCols, false);
    std::vector<int> perm;
    perm.reserve(nCols);

    for (int step = 0; step < nCols; ++step)
    {
        // Find the un-eliminated node with minimum degree
        int best = -1;
        int bestDeg = nCols + 1;
        for (int j = 0; j < nCols; ++j)
        {
            if (!eliminated[j] && degree[j] < bestDeg)
            {
                bestDeg = degree[j];
                best = j;
            }
        }

        perm.push_back(best);
        eliminated[best] = true;

        // Mass elimination: make neighbors of 'best' pairwise adjacent,
        // then remove 'best' from all neighbor sets.
        std::vector<int> neighbors;
        neighbors.reserve(adj[best].size());
        for (int nb : adj[best])
        {
            if (!eliminated[nb])
                neighbors.push_back(nb);
        }

        for (size_t a = 0; a < neighbors.size(); ++a)
        {
            for (size_t b = a + 1; b < neighbors.size(); ++b)
            {
                int u = neighbors[a];
                int v = neighbors[b];
                if (adj[u].find(v) == adj[u].end())
                {
                    adj[u].insert(v);
                    adj[v].insert(u);
                }
            }
        }

        // Remove 'best' from adjacency and update degrees
        for (int nb : neighbors)
        {
            adj[nb].erase(best);
            degree[nb] = 0;
            for (int x : adj[nb])
            {
                if (!eliminated[x])
                    ++degree[nb];
            }
        }

        adj[best].clear();
        degree[best] = 0;
    }

    return perm;
}

} // namespace sparseMath
