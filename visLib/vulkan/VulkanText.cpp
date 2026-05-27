#ifdef _WIN32

#include "VulkanText.h"
#include <stdexcept>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace visLib {

//------------------------------------------------------------------------------
// VulkanTextLine
//------------------------------------------------------------------------------

VulkanTextLine::VulkanTextLine()
    : m_color(1.0f, 1.0f, 1.0f, 1.0f)
    , m_createTime(std::time(nullptr))
{
}

int VulkanTextLine::printf(const char* format, ...)
{
    if (!format) return 0;
    va_list args; va_start(args, format);
    va_list copy; va_copy(copy, args);
    int required = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (required < 0) { va_end(args); return 0; }
    std::vector<char> buf(required + 1);
    int written = vsnprintf(buf.data(), buf.size(), format, args);
    va_end(args);
    if (written < 0) return 0;
    m_text = std::string(buf.data());
    return written;
}

void VulkanTextLine::setColor(const float4& color)
{
    m_color.x = std::max(0.0f, std::min(1.0f, color.x));
    m_color.y = std::max(0.0f, std::min(1.0f, color.y));
    m_color.z = std::max(0.0f, std::min(1.0f, color.z));
    m_color.w = std::max(0.0f, std::min(1.0f, color.w));
}

//------------------------------------------------------------------------------
// VulkanText
//------------------------------------------------------------------------------

VulkanText::VulkanText(VkDevice device, VkPhysicalDevice physicalDevice,
                        std::shared_ptr<VulkanFont> font,
                        VkDescriptorPool descriptorPool,
                        VkDescriptorSetLayout descriptorSetLayout)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_pFont(std::move(font))
    , m_descriptorPool(descriptorPool)
    , m_descriptorSetLayout(descriptorSetLayout)
{
    // --- Allocate TextParams UBO (host-visible). ---
    const VkDeviceSize paramsSize = sizeof(TextParams);
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size  = paramsSize;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &m_paramsBuffer) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer (TextParams) failed");
    }

    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(m_device, m_paramsBuffer, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findHostVisibleMemoryType(req.memoryTypeBits);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_paramsMemory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (TextParams) failed");
    }
    vkBindBufferMemory(m_device, m_paramsBuffer, m_paramsMemory, 0);
    vkMapMemory(m_device, m_paramsMemory, 0, paramsSize, 0, &m_paramsMapped);

    // --- Allocate descriptor set from shared pool. ---
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool     = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_descriptorSetLayout;
    if (vkAllocateDescriptorSets(m_device, &dsai, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (text) failed");
    }

    // Write: binding 0 = TextParams UBO, binding 1 = font atlas combined image sampler.
    VkDescriptorBufferInfo dbi = {};
    dbi.buffer = m_paramsBuffer;
    dbi.offset = 0;
    dbi.range  = paramsSize;

    VkDescriptorImageInfo dii = {};
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    dii.imageView   = m_pFont->getAtlasView();
    dii.sampler     = m_pFont->getSampler();

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_descriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &dbi;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_descriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo      = &dii;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
}

VulkanText::~VulkanText()
{
    if (m_paramsMapped) {
        vkUnmapMemory(m_device, m_paramsMemory);
        m_paramsMapped = nullptr;
    }
    if (m_paramsBuffer) vkDestroyBuffer(m_device, m_paramsBuffer, nullptr);
    if (m_paramsMemory) vkFreeMemory(m_device, m_paramsMemory, nullptr);

    if (m_vertexBuffer) vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    if (m_vertexMemory) vkFreeMemory(m_device, m_vertexMemory, nullptr);
    if (m_indexBuffer)  vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    if (m_indexMemory)  vkFreeMemory(m_device, m_indexMemory, nullptr);

    // Descriptor set is freed when the pool is destroyed by VulkanRenderer.
}

std::shared_ptr<TextLine> VulkanText::createLine()
{
    auto line = std::make_shared<VulkanTextLine>();
    line->setColor(m_defaultColor);
    m_lines.push_back(line);
    return line;
}

bool VulkanText::isExpired(const std::shared_ptr<VulkanTextLine>& line) const
{
    uint32_t lifetime = line->getLifetime();
    if (line.use_count() == 1 && lifetime == 0) lifetime = 5;
    if (lifetime == 0) return false;
    return (std::time(nullptr) - line->getCreateTime()) >= static_cast<std::time_t>(lifetime);
}

void VulkanText::generateQuads(std::vector<TextVertex>& verts,
                                std::vector<uint16_t>& indices)
{
    verts.clear();
    indices.clear();

    m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(),
                                  [this](const auto& l){ return isExpired(l); }),
                  m_lines.end());

    if (m_lines.empty() || !m_pFont) return;

    float currentX = m_position.x;
    float currentY = m_position.y;
    float lineHeight = m_pFont->getLineHeight();
    uint16_t vidx = 0;

    for (const auto& pLine : m_lines) {
        if (!pLine || pLine->isEmpty()) continue;
        currentX = m_position.x;

        for (char c : pLine->getText()) {
            const GlyphInfo* g = m_pFont->getGlyphInfo(c);
            if (!g) continue;

            float gx = currentX + g->bearing.x;
            float gy = currentY + lineHeight + g->bearing.y;
            float gw = g->size.x;
            float gh = g->size.y;

            TextVertex q[4];
            q[0].position = float2(gx, gy);                       q[0].texCoord = g->texCoordMin;
            q[1].position = float2(gx + gw, gy);                  q[1].texCoord = float2(g->texCoordMax.x, g->texCoordMin.y);
            q[2].position = float2(gx + gw, gy + gh);             q[2].texCoord = g->texCoordMax;
            q[3].position = float2(gx, gy + gh);                  q[3].texCoord = float2(g->texCoordMin.x, g->texCoordMax.y);

            for (int i = 0; i < 4; ++i) verts.push_back(q[i]);
            indices.push_back(vidx + 0);
            indices.push_back(vidx + 1);
            indices.push_back(vidx + 2);
            indices.push_back(vidx + 0);
            indices.push_back(vidx + 2);
            indices.push_back(vidx + 3);
            vidx += 4;

            currentX += g->advance;
        }
        currentY += lineHeight;
    }
}

uint32_t VulkanText::findHostVisibleMemoryType(uint32_t typeBits) const
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
    throw std::runtime_error("No host-visible coherent memory type (text)");
}

void VulkanText::destroyBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& size)
{
    if (buf) { vkDestroyBuffer(m_device, buf, nullptr); buf = VK_NULL_HANDLE; }
    if (mem) { vkFreeMemory(m_device, mem, nullptr);    mem = VK_NULL_HANDLE; }
    size = 0;
}

void VulkanText::recreateBuffer(VkBuffer& buf, VkDeviceMemory& mem, VkDeviceSize& size,
                                  VkDeviceSize newSize, VkBufferUsageFlags usage)
{
    destroyBuffer(buf, mem, size);

    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size  = newSize;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &buf) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer (text dynamic) failed");
    }
    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(m_device, buf, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findHostVisibleMemoryType(req.memoryTypeBits);
    if (vkAllocateMemory(m_device, &ai, nullptr, &mem) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (text dynamic) failed");
    }
    vkBindBufferMemory(m_device, buf, mem, 0);
    size = newSize;
}

void VulkanText::updateBuffers(const std::vector<TextVertex>& verts,
                                const std::vector<uint16_t>& indices,
                                float2 screenSize)
{
    // TextParams (uses the default color of this VulkanText for now — matches D3D12Text).
    TextParams params = {};
    params.textColor  = m_defaultColor;
    params.screenSize = screenSize;
    std::memcpy(m_paramsMapped, &params, sizeof(params));

    const VkDeviceSize vbSize = verts.size() * sizeof(TextVertex);
    const VkDeviceSize ibSize = indices.size() * sizeof(uint16_t);

    if (vbSize > m_vertexBufferSize) {
        recreateBuffer(m_vertexBuffer, m_vertexMemory, m_vertexBufferSize,
                        vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    }
    if (ibSize > m_indexBufferSize) {
        recreateBuffer(m_indexBuffer, m_indexMemory, m_indexBufferSize,
                        ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    if (vbSize > 0) {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_vertexMemory, 0, vbSize, 0, &mapped);
        std::memcpy(mapped, verts.data(), static_cast<size_t>(vbSize));
        vkUnmapMemory(m_device, m_vertexMemory);
    }
    if (ibSize > 0) {
        void* mapped = nullptr;
        vkMapMemory(m_device, m_indexMemory, 0, ibSize, 0, &mapped);
        std::memcpy(mapped, indices.data(), static_cast<size_t>(ibSize));
        vkUnmapMemory(m_device, m_indexMemory);
    }
    m_indexCount = static_cast<uint32_t>(indices.size());
}

void VulkanText::render(VkCommandBuffer cmd,
                         VkPipeline      textPipeline,
                         VkPipelineLayout textPipelineLayout,
                         VkExtent2D       extent)
{
    if (m_lines.empty() || !m_pFont) return;

    std::vector<TextVertex> verts;
    std::vector<uint16_t>   indices;
    generateQuads(verts, indices);
    if (indices.empty()) return;

    float2 screenSize(static_cast<float>(extent.width), static_cast<float>(extent.height));
    updateBuffers(verts, indices, screenSize);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipelineLayout,
                              0, 1, &m_descriptorSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cmd, m_indexCount, 1, 0, 0, 0);
}

} // namespace visLib

#endif // _WIN32
