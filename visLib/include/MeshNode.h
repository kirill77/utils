#pragma once

#include "Types.h"
#include <memory>
#include <vector>

namespace visLib {

// Forward declarations
class IMesh;

// Hierarchical scene graph node
// Contains meshes and child nodes with relative transforms
// This is a value type - can be copied, moved, stored in containers
class MeshNode {
public:
    MeshNode();
    explicit MeshNode(const affine3& transform);

    // Transform accessors (local transform relative to parent)
    const affine3& getTransform() const { return m_transform; }
    void setTransform(const affine3& transform) { m_transform = transform; }

    // Mesh management
    void addMesh(std::shared_ptr<IMesh> mesh);
    const std::vector<std::shared_ptr<IMesh>>& getMeshes() const { return m_meshes; }
    void clearMeshes() { m_meshes.clear(); }

    // Child node management
    void addChild(const MeshNode& child);
    void addChild(MeshNode&& child);
    const std::vector<MeshNode>& getChildren() const { return m_children; }
    std::vector<MeshNode>& getChildren() { return m_children; }
    void clearChildren() { m_children.clear(); }

    // Utility methods
    bool isEmpty() const { return m_meshes.empty() && m_children.empty(); }
    void clear();

    // Compute combined bounding box of all meshes and children (in local space)
    box3 getBoundingBox() const;

private:
    affine3 m_transform;                            // Transform from node local space to parent space
    std::vector<std::shared_ptr<IMesh>> m_meshes;   // Meshes at this node level
    std::vector<MeshNode> m_children;               // Child nodes with relative transforms
};

} // namespace visLib
