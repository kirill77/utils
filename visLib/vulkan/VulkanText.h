#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IText.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include "VulkanFont.h"
#include <vector>
#include <ctime>

namespace visLib {

class VulkanTextLine : public TextLine
{
public:
    VulkanTextLine();
    ~VulkanTextLine() override = default;

    int printf(const char* format, ...) override;
    const std::string& getText() const override { return m_text; }
    bool isEmpty() const override               { return m_text.empty(); }
    void setColor(const float4& color) override;
    const float4& getColor() const override     { return m_color; }
    void setLifetime(uint32_t seconds) override { m_lifetimeSec = seconds; }
    uint32_t getLifetime() const override       { return m_lifetimeSec; }

    std::time_t getCreateTime() const { return m_createTime; }

private:
    std::string m_text;
    float4      m_color;
    uint32_t    m_lifetimeSec = 0;
    std::time_t m_createTime;
};

// VulkanText - IText implementation. Owns a per-instance descriptor set
// (TextParams UBO + font atlas sampler) and dynamic vertex/index buffers.
// The text pipeline + descriptor set layout + pool live on VulkanRenderer
// and are passed in at construction.
class VulkanText : public IText
{
public:
    VulkanText(VkDevice device, VkPhysicalDevice physicalDevice,
                std::shared_ptr<VulkanFont> font,
                VkDescriptorPool descriptorPool,
                VkDescriptorSetLayout descriptorSetLayout);
    ~VulkanText() override;

    VulkanText(const VulkanText&) = delete;
    VulkanText& operator=(const VulkanText&) = delete;

    // IText
    void setPosition(const float2& position) override { m_position = position; }
    std::shared_ptr<TextLine> createLine() override;
    void setDefaultColor(const float4& color) override { m_defaultColor = color; }
    std::shared_ptr<IFont> getFont() const override   { return m_pFont; }

    // Called from VulkanRenderer::render() (inside the render pass, after the
    // mesh draws). Generates quads, updates buffers, binds descriptor set, draws.
    void render(VkCommandBuffer cmd,
                VkPipeline      textPipeline,
                VkPipelineLayout textPipelineLayout,
                VkExtent2D       extent);

private:
    struct TextVertex {
        float2 position;
        float2 texCoord;
    };
    struct TextParams {
        float4 textColor;
        float2 screenSize;
        float2 padding;
    };

    bool isExpired(const std::shared_ptr<VulkanTextLine>& line) const;
    void generateQuads(std::vector<TextVertex>& verts,
                       std::vector<uint16_t>& indices);
    void updateBuffers(const std::vector<TextVertex>& verts,
                       const std::vector<uint16_t>& indices,
                       float2 screenSize);
    uint32_t findHostVisibleMemoryType(uint32_t typeBits) const;
    void destroyBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& size);
    void recreateBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& size,
                         VkDeviceSize newSize, VkBufferUsageFlags usage);

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    std::shared_ptr<VulkanFont> m_pFont;

    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;  // shared, not owned
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;  // shared, not owned
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;

    VkBuffer       m_paramsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_paramsMemory = VK_NULL_HANDLE;
    void*          m_paramsMapped = nullptr;

    VkBuffer       m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkDeviceSize   m_vertexBufferSize = 0;

    VkBuffer       m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    VkDeviceSize   m_indexBufferSize = 0;

    uint32_t m_indexCount = 0;

    float2 m_position;
    float4 m_defaultColor = float4(1.0f, 1.0f, 1.0f, 1.0f);
    std::vector<std::shared_ptr<VulkanTextLine>> m_lines;
};

} // namespace visLib

#endif // _WIN32
