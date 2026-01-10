#pragma once

#include <limits>
#include "vector.h"
#include "box.h"

// Slab-based ray–AABB intersection. Returns true if the infinite ray intersects the box.
// Outputs parametric distances along the ray (tNear, tFar) where pos + t * dir hits the box slabs.
// Constraints: dir components near zero are handled; if ray is parallel to an axis and outside
// the slab on that axis the function returns false.
template <class T, int n>
inline bool intersectRayAABB(const vector<T, n>& pos,
                             const vector<T, n>& dir,
                             const box<T, n>& b,
                             T& outMin,
                             T& outMax)
{
    const T negInf = std::numeric_limits<T>::lowest();
    const T posInf = std::numeric_limits<T>::max();
    T tNear = negInf;
    T tFar  = posInf;

    for (int i = 0; i < n; ++i)
    {
        const T o = pos[i];
        const T d = dir[i];
        const T minv = b.m_mins[i];
        const T maxv = b.m_maxs[i];

        if (std::abs(d) < std::numeric_limits<T>::epsilon())
        {
            // Parallel to slab; must be within bounds
            if (o < minv || o > maxv)
                return false;
            // No update to tNear/tFar
            continue;
        }

        const T invD = T(1) / d;
        T t1 = (minv - o) * invD;
        T t2 = (maxv - o) * invD;
        if (t1 > t2) std::swap(t1, t2);

        if (t1 > tNear) tNear = t1;
        if (t2 < tFar)  tFar  = t2;
        if (tNear > tFar) return false;
        if (tFar < T(0))  return false; // box is behind the ray
    }

    outMin = tNear;
    outMax = tFar;
    return true;
}

// Compute barycentric coordinates of a point with respect to a triangle
// Returns barycentric coordinates (w0, w1, w2) where point = w0*v0 + w1*v1 + w2*v2
// For points inside the triangle: w0 + w1 + w2 = 1 and all w >= 0
// For points outside: coordinates are clamped to valid triangle boundary
template <class T>
inline vector<T, 3> computeBarycentricCoordinates(const vector<T, 3>& point,
                                                   const vector<T, 3>& v0,
                                                   const vector<T, 3>& v1,
                                                   const vector<T, 3>& v2)
{
    // Project point onto triangle plane and compute barycentric coordinates
    vector<T, 3> edge0 = v1 - v0;
    vector<T, 3> edge1 = v2 - v0;
    vector<T, 3> v0ToPoint = point - v0;
    
    T dot00 = dot(edge0, edge0);
    T dot01 = dot(edge0, edge1);
    T dot11 = dot(edge1, edge1);
    T dot20 = dot(v0ToPoint, edge0);
    T dot21 = dot(v0ToPoint, edge1);
    
    T denom = dot00 * dot11 - dot01 * dot01;
    
    // Handle degenerate triangle - return coordinates for vertex 0
    if (std::abs(denom) < std::numeric_limits<T>::epsilon())
    {
        return vector<T, 3>(T(1), T(0), T(0));
    }
    
    T invDenom = T(1) / denom;
    T u = (dot11 * dot20 - dot01 * dot21) * invDenom;  // weight for v1
    T v = (dot00 * dot21 - dot01 * dot20) * invDenom;  // weight for v2
    
    // Ensure barycentric coordinates are valid (clamp to triangle boundary)
    u = std::max(T(0), std::min(T(1), u));
    v = std::max(T(0), std::min(T(1), v));
    if (u + v > T(1))
    {
        T s = u + v;
        u /= s;
        v /= s;
    }
    
    T w = T(1) - u - v;  // weight for v0
    return vector<T, 3>(w, u, v);
}


