#ifdef _WIN32

#include "VulkanSwapchain.h"
#include "utils/visLib/vulkan/VulkanWindow.h"
#include <stdexcept>
#include <algorithm>

namespace visLib {

namespace {

VkSurfaceFormatKHR pickSurfaceFormat(VkPhysicalDevice pd, VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &count, formats.data());

    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
                           : formats[0];
}

// Map RendererConfig::vsyncInterval to a Vulkan present mode, validated
// against what the surface actually supports. FIFO is the one mode Vulkan
// guarantees, so it's always the safe fallback.
//   0  -> uncapped: IMMEDIATE (tearing, matches DXGI syncInterval 0) if
//         available, else MAILBOX (uncapped, no tearing), else FIFO.
//   >=1 -> FIFO (vsync, present every vblank). DXGI's "every Nth vblank"
//         for N>=2 has no native Vulkan analog and is gated out upstream;
//         if one slips through it lands on FIFO here.
VkPresentModeKHR pickPresentMode(VkPhysicalDevice pd, VkSurfaceKHR surface, int vsyncInterval)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &count, modes.data());

    auto has = [&](VkPresentModeKHR m) {
        return std::find(modes.begin(), modes.end(), m) != modes.end();
    };

    if (vsyncInterval <= 0) {
        if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (has(VK_PRESENT_MODE_MAILBOX_KHR))   return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;  // always supported per spec
}

} // namespace

VulkanSwapchain::VulkanSwapchain(VulkanWindow* pWindow, int vsyncInterval)
    : m_pWindow(pWindow)
    , m_device(pWindow->getDevice())
    , m_vsyncInterval(vsyncInterval)
{
    const auto& ov = pWindow->getOverrides();
    m_pfnCreateSwapchain    = ov.pfnVkCreateSwapchainKHR    ? ov.pfnVkCreateSwapchainKHR    : &vkCreateSwapchainKHR;
    m_pfnDestroySwapchain   = ov.pfnVkDestroySwapchainKHR   ? ov.pfnVkDestroySwapchainKHR   : &vkDestroySwapchainKHR;
    m_pfnGetSwapchainImages = ov.pfnVkGetSwapchainImagesKHR ? ov.pfnVkGetSwapchainImagesKHR : &vkGetSwapchainImagesKHR;
    m_pfnAcquireNextImage   = ov.pfnVkAcquireNextImageKHR   ? ov.pfnVkAcquireNextImageKHR   : &vkAcquireNextImageKHR;
    m_pfnQueuePresent       = ov.pfnVkQueuePresentKHR       ? ov.pfnVkQueuePresentKHR       : &vkQueuePresentKHR;
    create();
}

VulkanSwapchain::~VulkanSwapchain()
{
    destroy();
}

void VulkanSwapchain::create()
{
    VkPhysicalDevice pd = m_pWindow->getPhysicalDevice();
    VkSurfaceKHR surface = m_pWindow->getSurface();

    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps);

    VkSurfaceFormatKHR surfaceFormat = pickSurfaceFormat(pd, surface);
    m_format = surfaceFormat.format;

    // Image count: prefer 3, clamped to driver limits.
    uint32_t imageCount = 3;
    if (imageCount < caps.minImageCount) imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    // Extent: use current surface extent; if undefined (0xFFFFFFFF), use the window size.
    if (caps.currentExtent.width != UINT32_MAX) {
        m_extent = caps.currentExtent;
    } else {
        m_extent.width  = std::clamp<uint32_t>(m_pWindow->getWidth(),
                                                caps.minImageExtent.width,  caps.maxImageExtent.width);
        m_extent.height = std::clamp<uint32_t>(m_pWindow->getHeight(),
                                                caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    ci.surface          = surface;
    ci.minImageCount    = imageCount;
    ci.imageFormat      = surfaceFormat.format;
    ci.imageColorSpace  = surfaceFormat.colorSpace;
    ci.imageExtent      = m_extent;
    ci.imageArrayLayers = 1;
    // We need TRANSFER_DST for vkCmdClearColorImage; COLOR_ATTACHMENT for future shader path.
    ci.imageUsage       = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = pickPresentMode(pd, surface, m_vsyncInterval);
    ci.clipped          = VK_TRUE;
    ci.oldSwapchain     = VK_NULL_HANDLE;

    if (m_pfnCreateSwapchain(m_device, &ci, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed");
    }

    uint32_t count = 0;
    m_pfnGetSwapchainImages(m_device, m_swapchain, &count, nullptr);
    m_images.resize(count);
    m_pfnGetSwapchainImages(m_device, m_swapchain, &count, m_images.data());

    m_imageViews.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vci.image    = m_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = m_format;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(m_device, &vci, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView failed");
        }
    }
}

void VulkanSwapchain::destroy()
{
    for (auto v : m_imageViews) {
        if (v != VK_NULL_HANDLE) vkDestroyImageView(m_device, v, nullptr);
    }
    m_imageViews.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        m_pfnDestroySwapchain(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanSwapchain::recreate(int vsyncInterval)
{
    m_vsyncInterval = vsyncInterval;
    destroy();
    create();
}

uint32_t VulkanSwapchain::acquireNextImage(VkSemaphore signalSemaphore, VkResult& outResult)
{
    uint32_t index = 0;
    outResult = m_pfnAcquireNextImage(m_device, m_swapchain, UINT64_MAX,
                                       signalSemaphore, VK_NULL_HANDLE, &index);
    return index;
}

VkResult VulkanSwapchain::present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore)
{
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = (waitSemaphore != VK_NULL_HANDLE) ? 1 : 0;
    pi.pWaitSemaphores    = (waitSemaphore != VK_NULL_HANDLE) ? &waitSemaphore : nullptr;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &imageIndex;
    return m_pfnQueuePresent(queue, &pi);
}

} // namespace visLib

#endif // _WIN32
