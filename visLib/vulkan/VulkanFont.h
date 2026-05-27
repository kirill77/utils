#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IFont.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <unordered_map>

namespace visLib {

class VulkanWindow;

// VulkanFont - rasterizes glyphs via stb_truetype into a single atlas, uploads
// to a VkImage with a VkSampler. Mirrors D3D12Font's behavior (Arial, ASCII 32..126).
class VulkanFont : public IFont
{
public:
    VulkanFont(VulkanWindow* pWindow, uint32_t fontSize);
    ~VulkanFont() override;

    VulkanFont(const VulkanFont&) = delete;
    VulkanFont& operator=(const VulkanFont&) = delete;

    // IFont
    float getFontSize() const override   { return m_fontSize; }
    float getLineHeight() const override { return m_lineHeight; }
    const GlyphInfo* getGlyphInfo(char character) const override;

    // Vulkan-specific
    VkImageView getAtlasView() const { return m_atlasView; }
    VkSampler   getSampler() const   { return m_sampler; }

private:
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want) const;

    VkDevice         m_device         = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkQueue          m_queue          = VK_NULL_HANDLE;
    uint32_t         m_queueFamily    = 0;

    VkImage         m_atlas       = VK_NULL_HANDLE;
    VkDeviceMemory  m_atlasMemory = VK_NULL_HANDLE;
    VkImageView     m_atlasView   = VK_NULL_HANDLE;
    VkSampler       m_sampler     = VK_NULL_HANDLE;

    std::unordered_map<char, GlyphInfo> m_glyphMap;
    float m_fontSize   = 0.0f;
    float m_lineHeight = 0.0f;
};

} // namespace visLib

#endif // _WIN32
