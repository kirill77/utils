#include "utils/visLib/include/MeshNode.h"
#include "utils/visLib/include/IMesh.h"

namespace visLib {

MeshNode::MeshNode()
    : m_transform(affine3::identity())
{
}

MeshNode::MeshNode(const affine3& transform)
    : m_transform(transform)
{
}

void MeshNode::addMesh(std::shared_ptr<IMesh> mesh)
{
    if (mesh) {
        m_meshes.push_back(std::move(mesh));
    }
}

void MeshNode::addChild(const MeshNode& child)
{
    m_children.push_back(child);
}

void MeshNode::addChild(MeshNode&& child)
{
    m_children.push_back(std::move(child));
}

void MeshNode::clear()
{
    m_meshes.clear();
    m_children.clear();
}

box3 MeshNode::getBoundingBox() const
{
    box3 result = box3::empty();

    // Accumulate bounding boxes from meshes at this level
    // Transform by this node's transform to get bounds in parent space
    for (const auto& mesh : m_meshes) {
        if (mesh && !mesh->isEmpty()) {
            const box3& meshBounds = mesh->getBoundingBox();
            if (!meshBounds.isempty()) {
                box3 transformedBounds = meshBounds * m_transform;
                result = result | transformedBounds;
            }
        }
    }

    // Recursively accumulate from children
    // Child bounds are already in child's parent space (our local space),
    // so transform them by this node's transform to get bounds in our parent space
    for (const auto& child : m_children) {
        box3 childBounds = child.getBoundingBox();
        if (!childBounds.isempty()) {
            box3 transformedBounds = childBounds * m_transform;
            result = result | transformedBounds;
        }
    }

    return result;
}

} // namespace visLib
