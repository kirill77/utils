#ifdef _WIN32

#include "VulkanFont.h"
#include "VulkanWindow.h"
// stb_truetype implementation is defined in D3D12Font.cpp — here we only declare.
#include "utils/stb/stb_truetype.h"
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <cstring>

namespace visLib {

namespace {
constexpr int kAtlasWidth  = 1024;
constexpr int kAtlasHeight = 1024;
constexpr int kFirstChar   = 32;
constexpr int kCharCount   = 95;
} // namespace

VulkanFont::VulkanFont(VulkanWindow* pWindow, uint32_t fontSize)
    : m_device(pWindow->getDevice())
    , m_physicalDevice(pWindow->getPhysicalDevice())
    , m_queue(pWindow->getGraphicsQueue())
    , m_queueFamily(pWindow->getGraphicsQueueFamily())
    , m_fontSize(static_cast<float>(fontSize))
{
    // --- Load and rasterize font into a single A8 atlas (matches D3D12Font). ---
    FILE* f = nullptr;
    if (fopen_s(&f, "C:/Windows/Fonts/arial.ttf", "rb") != 0 || !f) {
        throw std::runtime_error("Failed to open C:/Windows/Fonts/arial.ttf");
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> fontBuf(fileSize);
    fread(fontBuf.data(), 1, fileSize, f);
    fclose(f);

    stbtt_fontinfo info = {};
    if (!stbtt_InitFont(&info, fontBuf.data(), 0)) {
        throw std::runtime_error("stbtt_InitFont failed");
    }
    float scale = stbtt_ScaleForPixelHeight(&info, m_fontSize);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    m_lineHeight = scale * (ascent - descent + lineGap);

    std::vector<unsigned char> atlas(kAtlasWidth * kAtlasHeight, 0);
    stbtt_pack_context pack;
    if (!stbtt_PackBegin(&pack, atlas.data(), kAtlasWidth, kAtlasHeight, 0, 1, nullptr)) {
        throw std::runtime_error("stbtt_PackBegin failed");
    }
    std::vector<stbtt_packedchar> packed(kCharCount);
    if (!stbtt_PackFontRange(&pack, fontBuf.data(), 0, m_fontSize, kFirstChar, kCharCount, packed.data())) {
        stbtt_PackEnd(&pack);
        throw std::runtime_error("stbtt_PackFontRange failed");
    }
    stbtt_PackEnd(&pack);

    // Expand A8 -> RGBA8 (R=G=B=255, A=alpha) to match the D3D12 atlas layout that
    // the text pixel shader expects.
    std::vector<unsigned char> rgba(kAtlasWidth * kAtlasHeight * 4);
    for (int i = 0; i < kAtlasWidth * kAtlasHeight; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = atlas[i];
    }

    // Build glyph map.
    for (int i = 0; i < kCharCount; ++i) {
        char c = static_cast<char>(kFirstChar + i);
        const auto& p = packed[i];
        GlyphInfo g;
        g.texCoordMin = float2(p.x0 / float(kAtlasWidth), p.y0 / float(kAtlasHeight));
        g.texCoordMax = float2(p.x1 / float(kAtlasWidth), p.y1 / float(kAtlasHeight));
        g.size        = float2(float(p.x1 - p.x0), float(p.y1 - p.y0));
        g.bearing     = float2(p.xoff, p.yoff);
        g.advance     = p.xadvance;
        m_glyphMap[c] = g;
    }

    // --- Allocate VkImage + VkDeviceMemory in DEVICE_LOCAL memory. ---
    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.format      = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent      = { static_cast<uint32_t>(kAtlasWidth), static_cast<uint32_t>(kAtlasHeight), 1 };
    ici.mipLevels   = 1;
    ici.arrayLayers = 1;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &m_atlas) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage (font atlas) failed");
    }

    VkMemoryRequirements imgReq = {};
    vkGetImageMemoryRequirements(m_device, m_atlas, &imgReq);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = imgReq.size;
    ai.memoryTypeIndex = findMemoryType(imgReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_atlasMemory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (font atlas) failed");
    }
    vkBindImageMemory(m_device, m_atlas, m_atlasMemory, 0);

    // --- Stage upload via a HOST_VISIBLE buffer + one-shot transfer cmd buffer. ---
    const VkDeviceSize uploadSize = rgba.size();
    VkBuffer       stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size  = uploadSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &stagingBuf) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer (staging) failed");
    }
    VkMemoryRequirements bufReq = {};
    vkGetBufferMemoryRequirements(m_device, stagingBuf, &bufReq);
    ai.allocationSize  = bufReq.size;
    ai.memoryTypeIndex = findMemoryType(bufReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(m_device, &ai, nullptr, &stagingMem) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (staging) failed");
    }
    vkBindBufferMemory(m_device, stagingBuf, stagingMem, 0);

    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMem, 0, uploadSize, 0, &mapped);
    std::memcpy(mapped, rgba.data(), static_cast<size_t>(uploadSize));
    vkUnmapMemory(m_device, stagingMem);

    // One-shot transfer command buffer.
    VkCommandPool tempPool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cpci.queueFamilyIndex = m_queueFamily;
    vkCreateCommandPool(m_device, &cpci, nullptr, &tempPool);

    VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbai.commandPool = tempPool;
    cbai.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cbai, &cmd);

    VkCommandBufferBeginInfo cbbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);

    // UNDEFINED -> TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier toDst = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.image         = m_atlas;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region = {};
    region.bufferOffset      = 0;
    region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent       = { static_cast<uint32_t>(kAtlasWidth),
                                 static_cast<uint32_t>(kAtlasHeight), 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuf, m_atlas,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);

    vkDestroyCommandPool(m_device, tempPool, nullptr);
    vkDestroyBuffer(m_device, stagingBuf, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    // --- Image view + sampler. ---
    VkImageViewCreateInfo ivci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ivci.image    = m_atlas;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format   = VK_FORMAT_R8G8B8A8_UNORM;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(m_device, &ivci, nullptr, &m_atlasView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (font atlas) failed");
    }

    VkSamplerCreateInfo sci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter   = VK_FILTER_LINEAR;
    sci.minFilter   = VK_FILTER_LINEAR;
    sci.mipmapMode  = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.borderColor  = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if (vkCreateSampler(m_device, &sci, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler (font) failed");
    }
}

VulkanFont::~VulkanFont()
{
    if (m_sampler     != VK_NULL_HANDLE) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_atlasView   != VK_NULL_HANDLE) vkDestroyImageView(m_device, m_atlasView, nullptr);
    if (m_atlas       != VK_NULL_HANDLE) vkDestroyImage(m_device, m_atlas, nullptr);
    if (m_atlasMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, m_atlasMemory, nullptr);
}

const GlyphInfo* VulkanFont::getGlyphInfo(char character) const
{
    auto it = m_glyphMap.find(character);
    return (it != m_glyphMap.end()) ? &it->second : nullptr;
}

uint32_t VulkanFont::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags want) const
{
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    throw std::runtime_error("No matching memory type for font allocation");
}

} // namespace visLib

#endif // _WIN32
