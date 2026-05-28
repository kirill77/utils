#pragma once

#ifdef _WIN32

#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <vector>
#include <cstdint>

namespace visLib {

class VulkanWindow;

// VulkanSwapchain - Manages a VkSwapchainKHR plus per-image views.
// Step 2b scope: create once, no resize handling yet.
class VulkanSwapchain
{
public:
    explicit VulkanSwapchain(VulkanWindow* pWindow);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Acquire next swapchain image. Returns the image index (caller passes
    // outResult to inspect VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR).
    uint32_t acquireNextImage(VkSemaphore signalSemaphore, VkResult& outResult);

    // Present the given image. Returns the VkResult of vkQueuePresentKHR.
    VkResult present(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore);

    VkSwapchainKHR get() const         { return m_swapchain; }
    VkImage       getImage(uint32_t i) const { return m_images[i]; }
    VkImageView   getImageView(uint32_t i) const { return m_imageViews[i]; }
    VkFormat      getFormat() const    { return m_format; }
    VkExtent2D    getExtent() const    { return m_extent; }
    uint32_t      getImageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    void create();
    void destroy();

    VulkanWindow* m_pWindow = nullptr;
    VkDevice      m_device  = VK_NULL_HANDLE;

    // Function-pointer overrides captured from the window. When non-null,
    // we route through SL's interposed wrappers so SL can install its
    // VK_NV_low_latency2 hooks on this swapchain.
    PFN_vkCreateSwapchainKHR    m_pfnCreateSwapchain    = nullptr;
    PFN_vkDestroySwapchainKHR   m_pfnDestroySwapchain   = nullptr;
    PFN_vkGetSwapchainImagesKHR m_pfnGetSwapchainImages = nullptr;
    PFN_vkAcquireNextImageKHR   m_pfnAcquireNextImage   = nullptr;
    PFN_vkQueuePresentKHR       m_pfnQueuePresent       = nullptr;

    VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_images;
    std::vector<VkImageView> m_imageViews;
    VkFormat                 m_format = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_extent = { 0, 0 };
};

} // namespace visLib

#endif // _WIN32
