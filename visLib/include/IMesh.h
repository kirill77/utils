#pragma once

#include "Types.h"
#include <vector>
#include <cstdint>

namespace visLib {

// Vertex data structure for mesh geometry
// Contains position only; future versions may add normals, UVs, colors
struct Vertex {
    float3 position;

    Vertex() : position(0.0f, 0.0f, 0.0f) {}
    explicit Vertex(const float3& pos) : position(pos) {}
    Vertex(float x, float y, float z) : position(x, y, z) {}
};

// Abstract mesh interface
// Represents a triangle mesh stored on the GPU
class IMesh {
public:
    virtual ~IMesh() = default;

    // Upload geometry data to the GPU
    // vertices: array of vertex positions
    // triangles: array of triangle indices (3 indices per triangle, referencing vertices)
    virtual void setGeometry(
        const std::vector<Vertex>& vertices,
        const std::vector<int3>& triangles) = 0;

    // Query mesh properties
    virtual uint32_t getVertexCount() const = 0;
    virtual uint32_t getTriangleCount() const = 0;
    virtual uint32_t getIndexCount() const = 0;

    // Get axis-aligned bounding box (computed from vertices)
    virtual const box3& getBoundingBox() const = 0;

    // Check if mesh has valid geometry
    virtual bool isEmpty() const = 0;
};

} // namespace visLib
