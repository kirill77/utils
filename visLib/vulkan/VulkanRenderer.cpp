#ifdef _WIN32

#include "VulkanRenderer.h"
#include "VulkanWindow.h"
#include "internal/g_EmbeddedSpirvShaders.h"
#include <stdexcept>

namespace visLib {

namespace {

VkShaderModule createShaderModule(VkDevice device, const std::string& name)
{
    auto [code, byteSize] = EmbeddedSpirvShaders::getShader(name);
    if (!code) {
        throw std::runtime_error("EmbeddedSpirvShaders::getShader missing: " + name);
    }
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = byteSize;
    ci.pCode    = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule failed for " + name);
    }
    return m;
}

} // namespace

VulkanRenderer::VulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config)
    : m_pWindow(pWindow)
    , m_config(config)
    , m_device(pWindow->getDevice())
    , m_queue(pWindow->getGraphicsQueue())
{
    m_pSwapchain = std::make_unique<VulkanSwapchain>(pWindow);
    createRenderPass();
    createFramebuffers();
    createPipeline();
    initFrameResources();
}

VulkanRenderer::~VulkanRenderer()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    destroyFrameResources();
    destroyPipelineResources();
    m_pSwapchain.reset();
}

void VulkanRenderer::createRenderPass()
{
    VkAttachmentDescription color = {};
    color.format         = m_pSwapchain->getFormat();
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // External -> subpass 0: gate color writes on image acquisition.
    VkSubpassDependency dep = {};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &color;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateRenderPass failed");
    }
}

void VulkanRenderer::createFramebuffers()
{
    VkExtent2D extent = m_pSwapchain->getExtent();
    uint32_t count = m_pSwapchain->getImageCount();
    m_framebuffers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageView attach = m_pSwapchain->getImageView(i);
        VkFramebufferCreateInfo fbi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbi.renderPass      = m_renderPass;
        fbi.attachmentCount = 1;
        fbi.pAttachments    = &attach;
        fbi.width           = extent.width;
        fbi.height          = extent.height;
        fbi.layers          = 1;
        if (vkCreateFramebuffer(m_device, &fbi, nullptr, &m_framebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }
}

void VulkanRenderer::createPipeline()
{
    // Empty pipeline layout — no descriptors, no push constants yet.
    VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    if (vkCreatePipelineLayout(m_device, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }

    VkShaderModule vs = createShaderModule(m_device, "TriangleVS");
    VkShaderModule ps = createShaderModule(m_device, "TrianglePS");

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName  = "main";

    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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

    VkGraphicsPipelineCreateInfo gpci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gpci.stageCount          = 2;
    gpci.pStages             = stages;
    gpci.pVertexInputState   = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState      = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState   = &ms;
    gpci.pColorBlendState    = &cb;
    gpci.pDynamicState       = &ds;
    gpci.layout              = m_pipelineLayout;
    gpci.renderPass          = m_renderPass;
    gpci.subpass             = 0;

    VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpci, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, ps, nullptr);
    if (r != VK_SUCCESS) {
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }
}

void VulkanRenderer::destroyPipelineResources()
{
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    for (auto fb : m_framebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, fb, nullptr);
    }
    m_framebuffers.clear();
    if (m_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
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

    VkSemaphoreCreateInfo semInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo   = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_imageAvailable[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &semInfo, nullptr, &m_renderFinished[i]) != VK_SUCCESS ||
            vkCreateFence    (m_device, &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create per-frame sync primitives");
        }
    }
}

void VulkanRenderer::destroyFrameResources()
{
    for (auto s : m_imageAvailable) if (s != VK_NULL_HANDLE) vkDestroySemaphore(m_device, s, nullptr);
    for (auto s : m_renderFinished) if (s != VK_NULL_HANDLE) vkDestroySemaphore(m_device, s, nullptr);
    for (auto f : m_inFlightFences) if (f != VK_NULL_HANDLE) vkDestroyFence    (m_device, f, nullptr);
    m_imageAvailable.clear();
    m_renderFinished.clear();
    m_inFlightFences.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    m_commandBuffers.clear();
}

box3 VulkanRenderer::render(IQuery* /*query*/)
{
    vkWaitForFences(m_device, 1, &m_inFlightFences[m_frameSlot], VK_TRUE, UINT64_MAX);
    vkResetFences  (m_device, 1, &m_inFlightFences[m_frameSlot]);

    VkResult acquireResult = VK_SUCCESS;
    m_currentImageIndex = m_pSwapchain->acquireNextImage(m_imageAvailable[m_frameSlot], acquireResult);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        // 2b: no resize/recreate handling. Surface the error.
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    VkCommandBuffer cmd = m_commandBuffers[m_frameSlot];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clearValue = {};
    clearValue.color.float32[0] = m_config.clearColor.x;
    clearValue.color.float32[1] = m_config.clearColor.y;
    clearValue.color.float32[2] = m_config.clearColor.z;
    clearValue.color.float32[3] = m_config.clearColor.w;

    VkExtent2D extent = m_pSwapchain->getExtent();

    VkRenderPassBeginInfo rpBegin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpBegin.renderPass        = m_renderPass;
    rpBegin.framebuffer       = m_framebuffers[m_currentImageIndex];
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = extent;
    rpBegin.clearValueCount   = 1;
    rpBegin.pClearValues      = &clearValue;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = { { 0, 0 }, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

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

    // No scene content yet; return empty bounds.
    box3 empty;
    empty.m_mins = float3(0.0f, 0.0f, 0.0f);
    empty.m_maxs = float3(0.0f, 0.0f, 0.0f);
    return empty;
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
        vkDeviceWaitIdle(m_device);
    }
}

std::shared_ptr<IMesh> VulkanRenderer::createMesh()
{
    throw std::runtime_error("VulkanRenderer::createMesh: not implemented yet (step 2b is clear-to-color only)");
}

std::shared_ptr<IFont> VulkanRenderer::createFont(uint32_t /*fontSize*/)
{
    throw std::runtime_error("VulkanRenderer::createFont: not implemented yet");
}

std::shared_ptr<IText> VulkanRenderer::createText(std::shared_ptr<IFont> /*font*/)
{
    throw std::runtime_error("VulkanRenderer::createText: not implemented yet");
}

void VulkanRenderer::addObject(std::weak_ptr<IVisObject> object)    { m_objects.push_back(object); }
void VulkanRenderer::removeObject(std::weak_ptr<IVisObject> /*o*/)  { /* no-op for 2b */ }
void VulkanRenderer::clearObjects()                                  { m_objects.clear(); }

std::shared_ptr<IQuery> VulkanRenderer::createQuery(QueryCapability /*capabilities*/, uint32_t /*slotCount*/)
{
    throw std::runtime_error("VulkanRenderer::createQuery: not implemented yet");
}

IWindow* VulkanRenderer::getWindow() const
{
    return m_pWindow;
}

std::shared_ptr<IRenderer> createVulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config)
{
    return std::make_shared<VulkanRenderer>(pWindow, config);
}

} // namespace visLib

#endif // _WIN32
