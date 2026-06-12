#ifdef _WIN32

#include "VulkanRenderer.h"
#include "VulkanWindow.h"
#include "VulkanMesh.h"
#include "VulkanFont.h"
#include "VulkanQuery.h"
#include "VulkanText.h"
#include "utils/visLib/include/IVisObject.h"
#include "utils/visLib/common/QRCode.h"
#include "utils/visLib/vulkan/internal/g_EmbeddedSpirvShaders.h"
#include "utils/log/ILog.h"
#include <stdexcept>
#include <cstring>

namespace visLib {

namespace {

struct TransformCB {
    float view[16];
    float projection[16];
};

struct PixelParamsCB {
    uint32_t iterationCount;
    uint32_t qrSize;
    uint32_t _padding[2];
    uint32_t qrData[16];
};

struct MeshPushConstants {
    float    world[16];
    uint32_t iterationCount;
};

void writeMatrix(float (&dst)[16], const float4x4& m) {
    // Row-major copy; DXC's -fvk-use-dx-layout + row_major in HLSL expects this layout.
    dst[ 0] = m.row0.x; dst[ 1] = m.row0.y; dst[ 2] = m.row0.z; dst[ 3] = m.row0.w;
    dst[ 4] = m.row1.x; dst[ 5] = m.row1.y; dst[ 6] = m.row1.z; dst[ 7] = m.row1.w;
    dst[ 8] = m.row2.x; dst[ 9] = m.row2.y; dst[10] = m.row2.z; dst[11] = m.row2.w;
    dst[12] = m.row3.x; dst[13] = m.row3.y; dst[14] = m.row3.z; dst[15] = m.row3.w;
}

// Map RendererConfig::pixelShader (a D3D12-style HLSL filename) to the
// embedded SPIR-V module name. Only the QR-code scene shader is ported to
// SPIR-V — which is the only pixel shader the slVerdict matrix ever selects
// (RendererApp sets "QRCodePixelShader"). Anything else has no Vulkan port,
// so we warn and fall back rather than silently ignoring the config.
std::string toSpirvPixelShaderName(const std::string& configName) {
    if (configName == "QRCodePixelShader" || configName == "QRCodePS") {
        return "QRCodePS";
    }
    LOG_WARN("Vulkan backend has no SPIR-V port of pixel shader '%s'; using QRCodePS",
             configName.c_str());
    return "QRCodePS";
}

VkShaderModule createShaderModule(VkDevice device, const std::string& name) {
    auto [code, byteSize] = EmbeddedSpirvShaders::getShader(name);
    if (!code) throw std::runtime_error("Missing embedded SPIR-V: " + name);
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = byteSize;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule failed for " + name);
    }
    return m;
}

uint32_t findHostVisibleMemoryType(VkPhysicalDevice pd, uint32_t typeBits) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pd, &props);
    const VkMemoryPropertyFlags want =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    throw std::runtime_error("No host-visible coherent memory type (renderer)");
}

uint32_t findDeviceLocalMemoryType(VkPhysicalDevice pd, uint32_t typeBits) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(pd, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
            return i;
        }
    }
    throw std::runtime_error("No device-local memory type (renderer depth)");
}

void createHostUbo(VkDevice device, VkPhysicalDevice pd, VkDeviceSize size,
                    VkBuffer& outBuf, VkDeviceMemory& outMem, void*& outMapped)
{
    VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size  = size;
    bci.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &outBuf) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer (UBO) failed");
    }
    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(device, outBuf, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findHostVisibleMemoryType(pd, req.memoryTypeBits);
    if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (UBO) failed");
    }
    vkBindBufferMemory(device, outBuf, outMem, 0);
    vkMapMemory(device, outMem, 0, size, 0, &outMapped);
}

} // namespace

VulkanRenderer::VulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config)
    : m_pWindow(pWindow)
    , m_config(config)
    , m_device(pWindow->getDevice())
    , m_queue(pWindow->getGraphicsQueue())
{
    m_pSwapchain = std::make_unique<VulkanSwapchain>(
        pWindow->getPhysicalDevice(), pWindow->getDevice(), pWindow->getSurface(),
        pWindow->getOverrides(), pWindow->getWidth(), pWindow->getHeight(),
        m_config.vsyncInterval);
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createDescriptorResources();
    createUniformBuffers();
    createMeshPipeline();
    createTextPipeline();
    initFrameResources();
}

VulkanRenderer::~VulkanRenderer()
{
    if (m_device != VK_NULL_HANDLE) {
        const auto& ov = m_pWindow->getOverrides();
        auto fnWaitIdle = ov.pfnVkDeviceWaitIdle ? ov.pfnVkDeviceWaitIdle : &vkDeviceWaitIdle;
        fnWaitIdle(m_device);
    }
    destroyFrameResources();
    destroyPipelineResources();
    m_pSwapchain.reset();
}

void VulkanRenderer::initFrameResources()
{
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_pWindow->getGraphicsQueueFamily();
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateCommandPool failed");
    }

    m_commandBuffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    m_imageAvailable.resize(kFramesInFlight);
    m_renderFinished.resize(kFramesInFlight);
    m_inFlightFences.resize(kFramesInFlight);
    VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(m_device, &si, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &si, nullptr, &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence    (m_device, &fi, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Per-frame sync primitive creation failed");
        }
    }
}

void VulkanRenderer::destroyFrameResources()
{
    for (auto s : m_imageAvailable) if (s) vkDestroySemaphore(m_device, s, nullptr);
    for (auto s : m_renderFinished) if (s) vkDestroySemaphore(m_device, s, nullptr);
    for (auto f : m_inFlightFences) if (f) vkDestroyFence    (m_device, f, nullptr);
    m_imageAvailable.clear();
    m_renderFinished.clear();
    m_inFlightFences.clear();
    if (m_commandPool) { vkDestroyCommandPool(m_device, m_commandPool, nullptr); m_commandPool = VK_NULL_HANDLE; }
    m_commandBuffers.clear();
}

void VulkanRenderer::createRenderPass()
{
    // Pick a depth format up front so the render pass and the depth image
    // (created right after) agree. D32_SFLOAT is required by spec to be
    // supported as a depth attachment; no stencil, which matches what DLSS-G
    // wants for the kBufferTypeDepth tag (it only reads depth values).
    m_depthFormat = VK_FORMAT_D32_SFLOAT;

    VkAttachmentDescription attachments[2] = {};

    VkAttachmentDescription& color = attachments[0];
    color.format        = m_pSwapchain->getFormat();
    color.samples       = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription& depth = attachments[1];
    depth.format        = m_depthFormat;
    depth.samples       = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // STORE so the contents survive past EndRenderPass — SL Frame Generation
    // reads the depth attachment when it interpolates frames at present time.
    depth.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp= VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Leave the image in a layout SL can barrier-transition from when it
    // tags this resource. SL manages its own input layouts; we just need a
    // sensible "after-pass" steady state.
    depth.finalLayout   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    // Depth writes happen at EARLY_FRAGMENT_TESTS / LATE_FRAGMENT_TESTS.
    // Include those stages alongside the color-attachment-output stage so the
    // implicit layout transitions for both attachments are ordered correctly.
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments    = attachments;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;
    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateRenderPass failed");
    }
}

void VulkanRenderer::createDepthResources()
{
    VkExtent2D extent = m_pSwapchain->getExtent();
    m_depthExtent = extent;

    // SAMPLED_BIT lets SL bind the image to a descriptor when it needs depth
    // for FG interpolation; DEPTH_STENCIL_ATTACHMENT_BIT is what we attach it
    // as during the render pass.
    m_depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                   VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = m_depthFormat;
    ici.extent        = { extent.width, extent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = m_depthUsage;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &m_depthImage) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage (depth) failed");
    }

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(m_device, m_depthImage, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = findDeviceLocalMemoryType(m_pWindow->getPhysicalDevice(), req.memoryTypeBits);
    if (vkAllocateMemory(m_device, &ai, nullptr, &m_depthMemory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory (depth) failed");
    }
    vkBindImageMemory(m_device, m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image    = m_depthImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format   = m_depthFormat;
    vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;
    if (vkCreateImageView(m_device, &vci, nullptr, &m_depthView) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView (depth) failed");
    }
}

void VulkanRenderer::destroyDepthResources()
{
    if (m_depthView)   { vkDestroyImageView(m_device, m_depthView,  nullptr); m_depthView   = VK_NULL_HANDLE; }
    if (m_depthImage)  { vkDestroyImage    (m_device, m_depthImage, nullptr); m_depthImage  = VK_NULL_HANDLE; }
    if (m_depthMemory) { vkFreeMemory      (m_device, m_depthMemory, nullptr); m_depthMemory = VK_NULL_HANDLE; }
    m_depthExtent = { 0, 0 };
    m_depthUsage  = 0;
    m_depthFormat = VK_FORMAT_UNDEFINED;
}

void VulkanRenderer::createFramebuffers()
{
    VkExtent2D extent = m_pSwapchain->getExtent();
    uint32_t count    = m_pSwapchain->getImageCount();
    m_framebuffers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageView attach[2] = { m_pSwapchain->getImageView(i), m_depthView };
        VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbi.renderPass      = m_renderPass;
        fbi.attachmentCount = 2;
        fbi.pAttachments    = attach;
        fbi.width           = extent.width;
        fbi.height          = extent.height;
        fbi.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbi, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }
}

void VulkanRenderer::createDescriptorResources()
{
    // Pool sized for: one mesh set (2 UBOs) + up to 8 text instances (8 UBOs + 8 samplers).
    VkDescriptorPoolSize sizes[2] = {};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = 2 + 8;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = 8;

    VkDescriptorPoolCreateInfo pci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pci.maxSets       = 1 + 8;
    pci.poolSizeCount = 2;
    pci.pPoolSizes    = sizes;
    if (vkCreateDescriptorPool(m_device, &pci, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    // Mesh DSL: binding 0 = TransformCB (VS), binding 1 = PixelParams (FS).
    VkDescriptorSetLayoutBinding meshBindings[2] = {};
    meshBindings[0].binding         = 0;
    meshBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    meshBindings[0].descriptorCount = 1;
    meshBindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
    meshBindings[1].binding         = 1;
    meshBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    meshBindings[1].descriptorCount = 1;
    meshBindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dslci.bindingCount = 2;
    dslci.pBindings    = meshBindings;
    if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_meshDSL) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (mesh) failed");
    }

    // Text DSL: binding 0 = TextParams (VS+FS), binding 1 = font atlas combined image sampler (FS).
    VkDescriptorSetLayoutBinding textBindings[2] = {};
    textBindings[0].binding         = 0;
    textBindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    textBindings[0].descriptorCount = 1;
    textBindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    textBindings[1].binding         = 1;
    textBindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    textBindings[1].descriptorCount = 1;
    textBindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    dslci.bindingCount = 2;
    dslci.pBindings    = textBindings;
    if (vkCreateDescriptorSetLayout(m_device, &dslci, nullptr, &m_textDSL) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout (text) failed");
    }
}

void VulkanRenderer::createUniformBuffers()
{
    VkPhysicalDevice pd = m_pWindow->getPhysicalDevice();
    createHostUbo(m_device, pd, sizeof(TransformCB),
                   m_transformBuffer, m_transformMemory, m_transformMapped);
    createHostUbo(m_device, pd, sizeof(PixelParamsCB),
                   m_pixelParamsBuffer, m_pixelParamsMemory, m_pixelParamsMapped);

    // Initialize PixelParams once: matches D3D12 initialization in D3D12Renderer.
    PixelParamsCB pp = {};
    pp.iterationCount = m_config.pixelShaderIterations;  // legacy, unused
    pp.qrSize         = QRCode::SIZE;
    if (!m_config.qrCodeText.empty()) {
        QRCode qr = QRCode::encode(m_config.qrCodeText);
        auto packed = qr.pack();
        for (int i = 0; i < QRCode::PACKED_UINT32S; ++i) pp.qrData[i] = packed[i];
    }
    std::memcpy(m_pixelParamsMapped, &pp, sizeof(pp));

    // Allocate the persistent mesh descriptor set and write both bindings.
    VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsai.descriptorPool     = m_descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts        = &m_meshDSL;
    if (vkAllocateDescriptorSets(m_device, &dsai, &m_meshDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets (mesh) failed");
    }

    VkDescriptorBufferInfo b0 = { m_transformBuffer,   0, sizeof(TransformCB) };
    VkDescriptorBufferInfo b1 = { m_pixelParamsBuffer, 0, sizeof(PixelParamsCB) };
    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = m_meshDescriptorSet;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &b0;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = m_meshDescriptorSet;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo     = &b1;
    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
}

void VulkanRenderer::createMeshPipeline()
{
    VkPushConstantRange pcRange = {};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.offset     = 0;
    pcRange.size       = sizeof(MeshPushConstants);

    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &m_meshDSL;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_meshPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout (mesh) failed");
    }

    VkShaderModule vs = createShaderModule(m_device, "MeshVS");
    VkShaderModule ps = createShaderModule(m_device, toSpirvPixelShaderName(m_config.pixelShader));

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName  = "main";

    // Vertex input: matches visLib::Vertex { float3 position; float2 uv; }.
    VkVertexInputBindingDescription binding = {};
    binding.binding   = 0;
    binding.stride    = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0;
    attrs[0].binding  = 0;
    attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset   = 0;
    attrs[1].location = 1;
    attrs[1].binding  = 0;
    attrs[1].format   = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset   = sizeof(float3);

    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = m_config.wireframeMode ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_BACK_BIT;
    // D3D12 treats CW (in its Y-down screen space) as front and culls back
    // (FrontCounterClockwise=FALSE). We apply a projection Y-flip in
    // updateTransformCB so NDC Y matches D3D12 — but that flip also negates the
    // framebuffer-space signed area, so the camera-facing triangles that D3D12
    // calls CW remain CW in Vulkan's framebuffer space here. Empirically,
    // FRONT_FACE_COUNTER_CLOCKWISE culled the entire (visible) scene; the
    // camera-facing faces are CW, so front must be CLOCKWISE to match D3D12.
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    // Mesh depth/stencil: mirror the D3D12 default (DepthEnable=TRUE,
    // DepthWriteMask=ALL, DepthFunc=LESS, StencilEnable=FALSE).
    VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dss.depthTestEnable       = VK_TRUE;
    dss.depthWriteEnable      = VK_TRUE;
    dss.depthCompareOp        = VK_COMPARE_OP_LESS;
    dss.depthBoundsTestEnable = VK_FALSE;
    dss.stencilTestEnable     = VK_FALSE;

    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = &dss;
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &ds;
    gpci.layout              = m_meshPipelineLayout;
    gpci.renderPass          = m_renderPass;

    VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_meshPipeline);
    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, ps, nullptr);
    if (r != VK_SUCCESS) throw std::runtime_error("vkCreateGraphicsPipelines (mesh) failed");
}

void VulkanRenderer::createTextPipeline()
{
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts    = &m_textDSL;
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_textPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout (text) failed");
    }

    VkShaderModule vs = createShaderModule(m_device, "TextVS");
    VkShaderModule ps = createShaderModule(m_device, "TextPS");

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName  = "main";

    // Text vertex: float2 position, float2 texCoord — 16 bytes.
    VkVertexInputBindingDescription binding = {};
    binding.binding   = 0;
    binding.stride    = sizeof(float2) * 2;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = sizeof(float2);

    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions    = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dyn[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dyn;

    // Text depth/stencil: disabled — text is an overlay drawn after the mesh
    // pass. Mirrors the D3D12 text PSO (DepthEnable=FALSE, WriteMask=ZERO,
    // Func=ALWAYS, StencilEnable=FALSE). Required as an explicit struct now
    // that the render pass declares a depth attachment.
    VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    dss.depthTestEnable       = VK_FALSE;
    dss.depthWriteEnable      = VK_FALSE;
    dss.depthCompareOp        = VK_COMPARE_OP_ALWAYS;
    dss.depthBoundsTestEnable = VK_FALSE;
    dss.stencilTestEnable     = VK_FALSE;

    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pDepthStencilState  = &dss;
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &ds;
    gpci.layout              = m_textPipelineLayout;
    gpci.renderPass          = m_renderPass;

    VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_textPipeline);
    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, ps, nullptr);
    if (r != VK_SUCCESS) throw std::runtime_error("vkCreateGraphicsPipelines (text) failed");
}

void VulkanRenderer::destroyPipelineResources()
{
    if (m_textPipeline)       vkDestroyPipeline(m_device, m_textPipeline, nullptr);
    if (m_textPipelineLayout) vkDestroyPipelineLayout(m_device, m_textPipelineLayout, nullptr);
    if (m_textDSL)            vkDestroyDescriptorSetLayout(m_device, m_textDSL, nullptr);
    if (m_meshPipeline)       vkDestroyPipeline(m_device, m_meshPipeline, nullptr);
    if (m_meshPipelineLayout) vkDestroyPipelineLayout(m_device, m_meshPipelineLayout, nullptr);
    if (m_meshDSL)            vkDestroyDescriptorSetLayout(m_device, m_meshDSL, nullptr);

    if (m_transformMapped) { vkUnmapMemory(m_device, m_transformMemory); m_transformMapped = nullptr; }
    if (m_transformBuffer) vkDestroyBuffer(m_device, m_transformBuffer, nullptr);
    if (m_transformMemory) vkFreeMemory(m_device, m_transformMemory, nullptr);
    if (m_pixelParamsMapped) { vkUnmapMemory(m_device, m_pixelParamsMemory); m_pixelParamsMapped = nullptr; }
    if (m_pixelParamsBuffer) vkDestroyBuffer(m_device, m_pixelParamsBuffer, nullptr);
    if (m_pixelParamsMemory) vkFreeMemory(m_device, m_pixelParamsMemory, nullptr);

    if (m_descriptorPool)     vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);

    for (auto fb : m_framebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();
    // Framebuffers reference m_depthView, so destroy them first, then drop
    // the depth image/memory/view, then the render pass that declared the
    // depth attachment description.
    destroyDepthResources();
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    m_textPipeline       = VK_NULL_HANDLE;
    m_textPipelineLayout = VK_NULL_HANDLE;
    m_textDSL            = VK_NULL_HANDLE;
    m_meshPipeline       = VK_NULL_HANDLE;
    m_meshPipelineLayout = VK_NULL_HANDLE;
    m_meshDSL            = VK_NULL_HANDLE;
    m_transformBuffer    = VK_NULL_HANDLE;
    m_transformMemory    = VK_NULL_HANDLE;
    m_pixelParamsBuffer  = VK_NULL_HANDLE;
    m_pixelParamsMemory  = VK_NULL_HANDLE;
    m_descriptorPool     = VK_NULL_HANDLE;
    m_renderPass         = VK_NULL_HANDLE;
}

void VulkanRenderer::updateTransformCB()
{
    VkExtent2D extent = m_pSwapchain->getExtent();
    if (extent.height > 0) {
        m_camera.setAspectRatio(float(extent.width) / float(extent.height));
    }
    TransformCB cb = {};
    writeMatrix(cb.view, m_camera.getViewMatrix());

    // visLib's Camera builds a D3D12-style projection (+Y up in clip space).
    // Vulkan NDC has +Y down, which would render geometry upside-down AND
    // reverse winding (back-culling our front-faces). Flip the Y row so the
    // post-divide NDC Y matches D3D12's, keeping rasterization identical
    // across backends.
    float4x4 proj = m_camera.getProjectionMatrix();
    proj.row1.y = -proj.row1.y;
    writeMatrix(cb.projection, proj);

    std::memcpy(m_transformMapped, &cb, sizeof(cb));
}

void VulkanRenderer::renderMeshNode(VkCommandBuffer cmd, const MeshNode& node,
                                     const affine3& parentTransform,
                                     box3& outBounds, bool& hasBounds)
{
    affine3 worldTransform = node.getTransform() * parentTransform;

    const auto& meshes = node.getMeshes();
    for (const auto& pMesh : meshes) {
        if (!pMesh) continue;
        auto pVkMesh = std::dynamic_pointer_cast<VulkanMesh>(pMesh);
        if (!pVkMesh || pVkMesh->isEmpty()) continue;

        // World matrix push constant. The shader reads `matrix World` with
        // HLSL's default column-major storage (no -Zpr / row_major), so the
        // translation must land in memory column 3 (indices 12..15) for
        // mul(World, pos) to translate xyz — exactly as the D3D12 path packs
        // its XMMATRIX (D3D12Renderer.cpp). The previous layout put the
        // translation in the bottom row (indices 3/7/11) and zeros in column 3,
        // which dropped the translation from xyz and corrupted w, clipping every
        // mesh off-screen on Vulkan. Pack byte-identical to D3D12 instead.
        MeshPushConstants pc = {};
        const auto& m = worldTransform.m_linear;
        const auto& t = worldTransform.m_translation;
        pc.world[ 0] = m.m00; pc.world[ 1] = m.m01; pc.world[ 2] = m.m02; pc.world[ 3] = 0.0f;
        pc.world[ 4] = m.m10; pc.world[ 5] = m.m11; pc.world[ 6] = m.m12; pc.world[ 7] = 0.0f;
        pc.world[ 8] = m.m20; pc.world[ 9] = m.m21; pc.world[10] = m.m22; pc.world[11] = 0.0f;
        pc.world[12] = t.x;   pc.world[13] = t.y;   pc.world[14] = t.z;   pc.world[15] = 1.0f;
        pc.iterationCount = node.getPixelShaderIterations();

        vkCmdPushConstants(cmd, m_meshPipelineLayout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(pc), &pc);

        VkBuffer vbuf = pVkMesh->getVertexBuffer();
        VkBuffer ibuf = pVkMesh->getIndexBuffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &offset);
        vkCmdBindIndexBuffer(cmd, ibuf, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, pVkMesh->getIndexCount(), 1, 0, 0, 0);

        const box3& localBounds = pVkMesh->getBoundingBox();
        if (!localBounds.isempty()) {
            box3 wb = localBounds * worldTransform;
            if (hasBounds) outBounds = outBounds | wb;
            else           { outBounds = wb; hasBounds = true; }
        }
    }

    for (const auto& child : node.getChildren()) {
        renderMeshNode(cmd, child, worldTransform, outBounds, hasBounds);
    }
}

box3 VulkanRenderer::render(IQuery* query)
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_frameSlot], VK_TRUE, UINT64_MAX);
    vkResetFences  (m_device, 1, &m_inFlightFences[m_frameSlot]);

    VkResult acquireResult = VK_SUCCESS;
    m_currentImageIndex = m_pSwapchain->acquireNextImage(m_imageAvailable[m_frameSlot], acquireResult);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    VkCommandBuffer cmd = m_commandBuffers[m_frameSlot];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Query begin must happen outside the render pass (for query pool reset).
    VulkanQuery* pVkQuery = dynamic_cast<VulkanQuery*>(query);
    if (pVkQuery) pVkQuery->beginInternal(cmd, m_frameIndex);

    updateTransformCB();

    VkClearValue clearValues[2] = {};
    clearValues[0].color.float32[0] = m_config.clearColor.x;
    clearValues[0].color.float32[1] = m_config.clearColor.y;
    clearValues[0].color.float32[2] = m_config.clearColor.z;
    clearValues[0].color.float32[3] = m_config.clearColor.w;
    clearValues[1].depthStencil.depth   = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    VkExtent2D extent = m_pSwapchain->getExtent();

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass        = m_renderPass;
    rpBegin.framebuffer       = m_framebuffers[m_currentImageIndex];
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount   = 2;
    rpBegin.pClearValues      = clearValues;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = { 0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, extent };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor (cmd, 0, 1, &scissor);

    // Mesh pass: bind pipeline + per-frame descriptor set, iterate scene objects.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipelineLayout,
                              0, 1, &m_meshDescriptorSet, 0, nullptr);

    box3 sceneBounds;
    sceneBounds.m_mins = float3( FLT_MAX,  FLT_MAX,  FLT_MAX);
    sceneBounds.m_maxs = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool hasBounds = false;

    for (auto it = m_objects.begin(); it != m_objects.end(); ) {
        auto pObject = it->lock();
        if (!pObject) { it = m_objects.erase(it); continue; }
        auto node = pObject->updateMeshNode();
        if (!node.isEmpty()) {
            renderMeshNode(cmd, node, affine3::identity(), sceneBounds, hasBounds);
        }
        ++it;
    }

    // Text pass on top.
    for (auto it = m_textObjects.begin(); it != m_textObjects.end(); ) {
        auto pText = it->lock();
        if (!pText) { it = m_textObjects.erase(it); continue; }
        pText->render(cmd, m_textPipeline, m_textPipelineLayout, extent);
        ++it;
    }

    vkCmdEndRenderPass(cmd);

    if (pVkQuery) pVkQuery->endInternal(cmd);

    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount   = 1;
    submit.pWaitSemaphores      = &m_imageAvailable[m_frameSlot];
    submit.pWaitDstStageMask    = &waitStage;
    submit.commandBufferCount   = 1;
    submit.pCommandBuffers      = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores    = &m_renderFinished[m_frameSlot];
    if (vkQueueSubmit(m_queue, 1, &submit, m_inFlightFences[m_frameSlot]) != VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed");
    }

    if (!hasBounds) {
        sceneBounds.m_mins = float3(0.0f, 0.0f, 0.0f);
        sceneBounds.m_maxs = float3(0.0f, 0.0f, 0.0f);
    }
    return sceneBounds;
}

void VulkanRenderer::present()
{
    m_pSwapchain->present(m_queue, m_currentImageIndex, m_renderFinished[m_frameSlot]);
    m_frameIndex++;
    m_frameSlot = (m_frameSlot + 1) % kFramesInFlight;
}

void VulkanRenderer::flush()
{
    if (m_device != VK_NULL_HANDLE) {
        const auto& ov = m_pWindow->getOverrides();
        auto fnWaitIdle = ov.pfnVkDeviceWaitIdle ? ov.pfnVkDeviceWaitIdle : &vkDeviceWaitIdle;
        fnWaitIdle(m_device);
    }
}

void VulkanRenderer::setConfig(const RendererConfig& config)
{
    const bool vsyncChanged = (config.vsyncInterval != m_config.vsyncInterval);
    m_config = config;
    if (vsyncChanged) {
        recreateSwapchain();
    }
}

void VulkanRenderer::recreateSwapchain()
{
    // Drain the device before touching swapchain-bound resources. In the
    // normal slVerdict flow this never fires: RendererApp seeds the renderer
    // config with the test's vsyncInterval before construction, so the first
    // swapchain already has the right present mode and the beginTest setConfig
    // is a no-op. This path exists to honor the IRenderer contract if vsync is
    // changed after construction. (Note: SL's VK_NV_low_latency2 hooks are
    // installed against the swapchain in configureReflexAfterSwapchain, so a
    // post-Reflex recreate would need Reflex reconfigured — hence we rely on
    // the construction-time seed rather than this path in practice.)
    flush();

    for (auto fb : m_framebuffers) {
        if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_framebuffers.clear();

    m_pSwapchain->recreate(m_config.vsyncInterval);

    // Depth resources are sized to the (unchanged) extent and the render pass
    // references the (unchanged) surface format, so only the framebuffers —
    // which bind the new swapchain image views — need rebuilding.
    createFramebuffers();
}

std::shared_ptr<IMesh> VulkanRenderer::createMesh()
{
    return std::make_shared<VulkanMesh>(m_device, m_pWindow->getPhysicalDevice());
}

std::shared_ptr<IFont> VulkanRenderer::createFont(uint32_t fontSize)
{
    return std::make_shared<VulkanFont>(m_pWindow, fontSize);
}

std::shared_ptr<IText> VulkanRenderer::createText(std::shared_ptr<IFont> font)
{
    auto vkFont = std::dynamic_pointer_cast<VulkanFont>(font);
    if (!vkFont) throw std::runtime_error("VulkanRenderer::createText: font must come from this renderer");
    auto text = std::make_shared<VulkanText>(m_device, m_pWindow->getPhysicalDevice(),
                                              vkFont, m_descriptorPool, m_textDSL);
    m_textObjects.push_back(text);
    return text;
}

void VulkanRenderer::addObject(std::weak_ptr<IVisObject> object) { m_objects.push_back(object); }

void VulkanRenderer::removeObject(std::weak_ptr<IVisObject> object)
{
    auto p = object.lock();
    if (!p) return;
    m_objects.erase(std::remove_if(m_objects.begin(), m_objects.end(),
        [&p](const std::weak_ptr<IVisObject>& w){ auto q = w.lock(); return !q || q == p; }),
        m_objects.end());
}

void VulkanRenderer::clearObjects() { m_objects.clear(); }

std::shared_ptr<IQuery> VulkanRenderer::createQuery(QueryCapability capabilities, uint32_t slotCount)
{
    return std::make_shared<VulkanQuery>(m_pWindow, capabilities, slotCount);
}

IWindow* VulkanRenderer::getWindow() const { return m_pWindow; }

std::shared_ptr<IRenderer> createVulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config)
{
    return std::make_shared<VulkanRenderer>(pWindow, config);
}

} // namespace visLib

#endif // _WIN32
