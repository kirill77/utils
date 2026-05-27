#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IMesh.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"

namespace visLib {

// VulkanMesh - Vulkan implementation of IMesh.
// Backing storage is HOST_VISIBLE | HOST_COHERENT — the same simplification
// D3D12Mesh uses (UPLOAD heap, CPU-visible). Sufficient for visLib's test
// workload; a future DEVICE_LOCAL + staging variant can be added if needed.
class VulkanMesh : public IMesh
{
public:
    VulkanMesh(VkDevice device, VkPhysicalDevice physicalDevice);
    ~VulkanMesh() override;

    VulkanMesh(const VulkanMesh&) = delete;
    VulkanMesh& operator=(const VulkanMesh&) = delete;

    // IMesh
    void setGeometry(const std::vector<Vertex>& vertices,
                     const std::vector<int3>& triangles) override;
    uint32_t getVertexCount() const override   { return m_vertexCount; }
    uint32_t getTriangleCount() const override { return m_indexCount / 3; }
    uint32_t getIndexCount() const override    { return m_indexCount; }
    const box3& getBoundingBox() const override { return m_boundingBox; }
    bool isEmpty() const override              { return m_indexCount == 0; }

    // Vulkan-specific accessors (used by VulkanRenderer to issue draws).
    VkBuffer getVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer getIndexBuffer() const  { return m_indexBuffer; }

private:
    struct Buffer {
        VkBuffer        buffer = VK_NULL_HANDLE;
        VkDeviceMemory  memory = VK_NULL_HANDLE;
        VkDeviceSize    size   = 0;
    };

    void createOrUpdate(Buffer& buf, VkBufferUsageFlags usage,
                        const void* data, VkDeviceSize byteSize);
    void destroy(Buffer& buf);

    uint32_t findHostVisibleMemoryType(uint32_t typeBits) const;

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;

    Buffer   m_vbuf;
    Buffer   m_ibuf;
    uint32_t m_vertexCount = 0;
    uint32_t m_indexCount  = 0;
    box3     m_boundingBox;
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;  // mirrors m_vbuf.buffer for accessor
    VkBuffer m_indexBuffer  = VK_NULL_HANDLE;
};

} // namespace visLib

#endif // _WIN32
