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

    // Column adjacency using unordered_set — O(1) insert, no sort/dedup needed.
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

    // Degree of each node = adjacency size
    std::vector<int> degree(nCols);
    for (int j = 0; j < nCols; ++j)
        degree[j] = static_cast<int>(adj[j].size());

    // Doubly-linked-list bucket queue for O(1) extract-min
    std::vector<int> headOfBucket(nCols, -1);
    std::vector<int> nextInBucket(nCols, -1);
    std::vector<int> prevInBucket(nCols, -1);

    // Insert all nodes into their degree buckets
    for (int j = 0; j < nCols; ++j)
    {
        int d = degree[j];
        if (headOfBucket[d] != -1)
            prevInBucket[headOfBucket[d]] = j;
        nextInBucket[j] = headOfBucket[d];
        prevInBucket[j] = -1;
        headOfBucket[d] = j;
    }

    int minBucket = 0;
    while (minBucket < nCols && headOfBucket[minBucket] == -1)
        ++minBucket;

    std::vector<bool> eliminated(nCols, false);
    std::vector<int> perm;
    perm.reserve(nCols);

    auto removeFromBucket = [&](int node)
    {
        int d = degree[node];
        if (prevInBucket[node] != -1)
            nextInBucket[prevInBucket[node]] = nextInBucket[node];
        else
            headOfBucket[d] = nextInBucket[node];
        if (nextInBucket[node] != -1)
            prevInBucket[nextInBucket[node]] = prevInBucket[node];
        nextInBucket[node] = -1;
        prevInBucket[node] = -1;
    };

    auto insertIntoBucket = [&](int node, int d)
    {
        degree[node] = d;
        if (headOfBucket[d] != -1)
            prevInBucket[headOfBucket[d]] = node;
        nextInBucket[node] = headOfBucket[d];
        prevInBucket[node] = -1;
        headOfBucket[d] = node;
    };

    for (int step = 0; step < nCols; ++step)
    {
        // Advance minBucket to first non-empty bucket
        while (minBucket < nCols && headOfBucket[minBucket] == -1)
            ++minBucket;

        int best = headOfBucket[minBucket];
        removeFromBucket(best);

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
                adj[u].insert(v);
                adj[v].insert(u);
            }
        }

        // Remove 'best' from adjacency and update degrees
        for (int nb : neighbors)
        {
            adj[nb].erase(best);

            int newDeg = static_cast<int>(adj[nb].size());
            if (newDeg != degree[nb])
            {
                removeFromBucket(nb);
                insertIntoBucket(nb, newDeg);
                if (newDeg < minBucket)
                    minBucket = newDeg;
            }
        }

        adj[best].clear();
        degree[best] = 0;
    }

    return perm;
}

} // namespace sparseMath
