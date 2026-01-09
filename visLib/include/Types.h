#pragma once

// visLib Types - Re-exports common math types for the visualization library
// This provides a stable API that doesn't depend on the specific math library used internally.

#include "geometry/vectors/vector.h"
#include "geometry/vectors/matrix.h"
#include "geometry/vectors/affine.h"
#include "geometry/vectors/box.h"

namespace visLib {

// Vector types
using float2 = ::float2;
using float3 = ::float3;
using float4 = ::float4;

using int2 = ::int2;
using int3 = ::int3;
using int4 = ::int4;

using uint2 = ::uint2;
using uint3 = ::uint3;
using uint4 = ::uint4;

// Matrix types
using float2x2 = ::float2x2;
using float3x3 = ::float3x3;
using float4x4 = ::float4x4;

// Affine transform types
using affine2 = ::affine2;
using affine3 = ::affine3;

// Bounding box types
using box2 = ::box2;
using box3 = ::box3;

} // namespace visLib
