#ifdef _WIN32

#include "VulkanMesh.h"
#include <stdexcept>
#include <cstring>

namespace visLib {

VulkanMesh::VulkanMesh(VkDevice device, VkPhysicalDevice physicalDevice)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
{
}

VulkanMesh::~VulkanMesh()
{
    destroy(m_vbuf);
    destroy(m_ibuf);
}

uint32_t VulkanMesh::findHostVisibleMemoryType(uint32_t typeBits) const
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props);
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    throw std::runtime_error("No host-visible coherent memory type found");
}

void VulkanMesh::destroy(Buffer& buf)
{
    if (buf.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, buf.buffer, nullptr);
        buf.buffer = VK_NULL_HANDLE;
    }
    if (buf.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, buf.memory, nullptr);
        buf.memory = VK_NULL_HANDLE;
    }
    buf.size = 0;
}

void VulkanMesh::createOrUpdate(Buffer& buf, VkBufferUsageFlags usage,
                                 const void* data, VkDeviceSize byteSize)
{
    // Reuse if same size; otherwise recreate.
    if (buf.buffer != VK_NULL_HANDLE && buf.size != byteSize) {
        destroy(buf);
    }

    if (buf.buffer == VK_NULL_HANDLE) {
        VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ci.size        = byteSize;
        ci.usage       = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &ci, nullptr, &buf.buffer) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateBuffer failed");
        }

        VkMemoryRequirements req = {};
        vkGetBufferMemoryRequirements(m_device, buf.buffer, &req);

        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = findHostVisibleMemoryType(req.memoryTypeBits);
        if (vkAllocateMemory(m_device, &ai, nullptr, &buf.memory) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateMemory failed for mesh buffer");
        }
        if (vkBindBufferMemory(m_device, buf.buffer, buf.memory, 0) != VK_SUCCESS) {
            throw std::runtime_error("vkBindBufferMemory failed");
        }
        buf.size = byteSize;
    }

    void* mapped = nullptr;
    if (vkMapMemory(m_device, buf.memory, 0, byteSize, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("vkMapMemory failed for mesh upload");
    }
    std::memcpy(mapped, data, static_cast<size_t>(byteSize));
    vkUnmapMemory(m_device, buf.memory);
}

void VulkanMesh::setGeometry(const std::vector<Vertex>& vertices,
                              const std::vector<int3>& triangles)
{
    // Bounding box (matches D3D12Mesh).
    if (!vertices.empty()) {
        float3 mn = vertices[0].position;
        float3 mx = vertices[0].position;
        for (size_t i = 1; i < vertices.size(); ++i) {
            const float3& p = vertices[i].position;
            mn = min(mn, p);
            mx = max(mx, p);
        }
        m_boundingBox = box3(mn, mx);
    } else {
        m_boundingBox = box3::empty();
    }

    m_vertexCount = static_cast<uint32_t>(vertices.size());

    const VkDeviceSize vbSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
    if (vbSize > 0) {
        createOrUpdate(m_vbuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data(), vbSize);
    } else {
        destroy(m_vbuf);
    }
    m_vertexBuffer = m_vbuf.buffer;

    // int3 -> uint32_t index list.
    std::vector<uint32_t> indices;
    indices.reserve(triangles.size() * 3);
    for (const auto& tri : triangles) {
        indices.push_back(static_cast<uint32_t>(tri.x));
        indices.push_back(static_cast<uint32_t>(tri.y));
        indices.push_back(static_cast<uint32_t>(tri.z));
    }
    m_indexCount = static_cast<uint32_t>(indices.size());

    const VkDeviceSize ibSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
    if (ibSize > 0) {
        createOrUpdate(m_ibuf, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices.data(), ibSize);
    } else {
        destroy(m_ibuf);
    }
    m_indexBuffer = m_ibuf.buffer;
}

} // namespace visLib

#endif // _WIN32
