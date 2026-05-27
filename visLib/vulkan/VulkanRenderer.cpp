#ifdef _WIN32

#include "VulkanRenderer.h"
#include "VulkanWindow.h"
#include <stdexcept>

namespace visLib {

namespace {

void recordClearAndTransition(VkCommandBuffer cmd, VkImage image, const float4& clearColor)
{
    // UNDEFINED → TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier toDst = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toDst.srcAccessMask       = 0;
    toDst.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image               = image;
    toDst.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkClearColorValue cc = {};
    cc.float32[0] = clearColor.x;
    cc.float32[1] = clearColor.y;
    cc.float32[2] = clearColor.z;
    cc.float32[3] = clearColor.w;
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &range);

    // TRANSFER_DST_OPTIMAL → PRESENT_SRC_KHR
    VkImageMemoryBarrier toPresent = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask       = 0;
    toPresent.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image               = image;
    toPresent.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toPresent);
}

} // namespace

VulkanRenderer::VulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config)
    : m_pWindow(pWindow)
    , m_config(config)
    , m_device(pWindow->getDevice())
    , m_queue(pWindow->getGraphicsQueue())
{
    m_pSwapchain = std::make_unique<VulkanSwapchain>(pWindow);
    initFrameResources();
}

VulkanRenderer::~VulkanRenderer()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
    destroyFrameResources();
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

    recordClearAndTransition(cmd, m_pSwapchain->getImage(m_currentImageIndex), m_config.clearColor);

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
