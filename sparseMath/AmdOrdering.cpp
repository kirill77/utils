#include "utils/sparseMath/AmdOrdering.h"

#include <algorithm>
#include <numeric>

namespace sparseMath {

// Build the symmetric sparsity pattern of A^T*A (column intersection graph),
// then greedily eliminate the minimum-degree node, updating neighbor degrees.
std::vector<int> AmdOrdering::compute(int nCols,
                                       const std::vector<int>& colStart,
                                       const std::vector<int>& rowIdx)
{
    // Build row->column lists first, then derive column adjacency.
    int nRows = 0;
    for (int j = 0; j < nCols; ++j)
    {
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
        {
            if (rowIdx[p] >= nRows)
                nRows = rowIdx[p] + 1;
        }
    }

    // row -> set of columns
    std::vector<std::vector<int>> rowToCols(nRows);
    for (int j = 0; j < nCols; ++j)
    {
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
            rowToCols[rowIdx[p]].push_back(j);
    }

    // Column adjacency using sorted vectors (better cache performance than
    // unordered_set). Duplicates are removed after construction.
    std::vector<std::vector<int>> adj(nCols);
    for (int i = 0; i < nRows; ++i)
    {
        const auto& cols = rowToCols[i];
        for (size_t a = 0; a < cols.size(); ++a)
        {
            for (size_t b = a + 1; b < cols.size(); ++b)
            {
                adj[cols[a]].push_back(cols[b]);
                adj[cols[b]].push_back(cols[a]);
            }
        }
    }

    // Sort and deduplicate adjacency lists
    for (int j = 0; j < nCols; ++j)
    {
        std::sort(adj[j].begin(), adj[j].end());
        adj[j].erase(std::unique(adj[j].begin(), adj[j].end()), adj[j].end());
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

        // Collect active neighbors
        std::vector<int> neighbors;
        neighbors.reserve(adj[best].size());
        for (int nb : adj[best])
        {
            if (!eliminated[nb])
                neighbors.push_back(nb);
        }

        // Mass elimination: make neighbors pairwise adjacent
        for (size_t a = 0; a < neighbors.size(); ++a)
        {
            for (size_t b = a + 1; b < neighbors.size(); ++b)
            {
                int u = neighbors[a];
                int v = neighbors[b];
                auto it = std::lower_bound(adj[u].begin(), adj[u].end(), v);
                if (it == adj[u].end() || *it != v)
                {
                    adj[u].insert(it, v);
                    auto it2 = std::lower_bound(adj[v].begin(), adj[v].end(), u);
                    adj[v].insert(it2, u);
                }
            }
        }

        // Remove 'best' from adjacency and update degrees
        for (int nb : neighbors)
        {
            auto it = std::lower_bound(adj[nb].begin(), adj[nb].end(), best);
            if (it != adj[nb].end() && *it == best)
                adj[nb].erase(it);

            // Recount non-eliminated neighbors for accurate degree
            int newDeg = 0;
            for (int x : adj[nb])
            {
                if (!eliminated[x])
                    ++newDeg;
            }
            degree[nb] = newDeg;
        }

        adj[best].clear();
        degree[best] = 0;
    }

    return perm;
}

} // namespace sparseMath
