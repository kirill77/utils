#pragma once

#include "MeshNode.h"
#include <memory>

namespace visLib {

// Abstract interface for objects that can be visualized
// Implement this interface to make any object renderable by IRenderer
class IVisObject {
public:
    virtual ~IVisObject() = default;

    // Called by the renderer to get the current visual representation
    // The implementation should update and return the mesh node hierarchy
    // Returns: Root MeshNode containing all geometry for this object
    MeshNode updateMeshNode()
    {
        m_cachedMeshNode = onUpdateMeshNode();
        return m_cachedMeshNode;
    }

    // Get the last cached mesh node (without triggering an update)
    MeshNode getMeshNode() const
    {
        return m_cachedMeshNode;
    }

protected:
    // Override this method to provide the visual representation
    // Called by updateMeshNode() to refresh the cached mesh hierarchy
    virtual MeshNode onUpdateMeshNode() = 0;

private:
    MeshNode m_cachedMeshNode;
};

} // namespace visLib
