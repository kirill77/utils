#include "utils/sparseMath/AmdOrdering.h"

#include <algorithm>
#include <numeric>

namespace sparseMath {

std::vector<int> AmdOrdering::compute(int nCols,
                                       const std::vector<int>& colStart,
                                       const std::vector<int>& rowIdx)
{
    // Build row->column lists first, then derive column adjacency.
    int nRows = 0;
    for (int j = 0; j < nCols; ++j)
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
            if (rowIdx[p] >= nRows)
                nRows = rowIdx[p] + 1;

    std::vector<std::vector<int>> rowToCols(nRows);
    for (int j = 0; j < nCols; ++j)
        for (int p = colStart[j]; p < colStart[j + 1]; ++p)
            rowToCols[rowIdx[p]].push_back(j);

    // Build column adjacency via sorted edge list — cache-friendly.
    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < nRows; ++i)
    {
        const auto& cols = rowToCols[i];
        for (size_t a = 0; a < cols.size(); ++a)
            for (size_t b = a + 1; b < cols.size(); ++b)
            {
                edges.push_back({cols[a], cols[b]});
                edges.push_back({cols[b], cols[a]});
            }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());

    std::vector<std::vector<int>> adj(nCols);
    for (auto& e : edges)
        adj[e.first].push_back(e.second);
    // Each adj[u] is sorted since edges were sorted by (first, second).

    edges.clear();
    edges.shrink_to_fit();

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

    // Reusable buffers (avoid per-iteration heap allocations)
    std::vector<int> neighbors;
    std::vector<int> mergeBuffer;

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
        neighbors.clear();
        neighbors.reserve(adj[best].size());
        for (int nb : adj[best])
        {
            if (!eliminated[nb])
                neighbors.push_back(nb);
        }

        // Sort neighbors for efficient merge below
        std::sort(neighbors.begin(), neighbors.end());

        // Mass elimination: make neighbors pairwise adjacent, remove 'best',
        // and update degrees — all in a single sorted-merge pass per neighbor.
        for (int u : neighbors)
        {
            // Merge adj[u] with neighbors, excluding 'best' and 'u'
            mergeBuffer.clear();
            mergeBuffer.reserve(adj[u].size() + neighbors.size());

            size_t ai = 0, ni = 0;
            while (ai < adj[u].size() && ni < neighbors.size())
            {
                int av = adj[u][ai];
                int nv = neighbors[ni];
                if (av == best) { ++ai; continue; }
                if (nv == u)    { ++ni; continue; }

                if (av < nv)      { mergeBuffer.push_back(av); ++ai; }
                else if (av > nv) { mergeBuffer.push_back(nv); ++ni; }
                else              { mergeBuffer.push_back(av); ++ai; ++ni; }
            }
            while (ai < adj[u].size())
            {
                if (adj[u][ai] != best) mergeBuffer.push_back(adj[u][ai]);
                ++ai;
            }
            while (ni < neighbors.size())
            {
                if (neighbors[ni] != u) mergeBuffer.push_back(neighbors[ni]);
                ++ni;
            }

            adj[u].swap(mergeBuffer);

            int newDeg = static_cast<int>(adj[u].size());
            if (newDeg != degree[u])
            {
                removeFromBucket(u);
                insertIntoBucket(u, newDeg);
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
