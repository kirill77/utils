/**
 * math.cpp - Static library implementation file
 * 
 * This file exists because Visual Studio static library projects require
 * at least one .cpp file to compile. All math utilities in this module
 * are header-only for performance (inlining, templates):
 * 
 *   - vector.h    : Vector types (float2, float3, double3, etc.)
 *   - matrix.h    : Matrix operations
 *   - quat.h      : Quaternion math
 *   - affine.h    : Affine transformations
 *   - box.h       : Bounding box utilities
 *   - intersections.h : Geometric intersection tests
 * 
 * If you need to add compiled (non-inline) math functions in the future,
 * implement them here to avoid header bloat and reduce compile times.
 */
