#pragma once

#ifdef _WIN32

#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <vector>
#include <cstdint>

namespace visLib {

// VulkanSwapchain - Manages a VkSwapchainKHR plus per-image views.
// The present mode is derived from vsyncInterval (see pickPresentMode in the
// .cpp): 0 -> IMMEDIATE/MAILBOX (uncapped), >=1 -> FIFO (vsync). recreate()
// rebuilds the swapchain when the present mode needs to change.
class VulkanSwapchain
{
public:
    // Constructed from raw Vulkan primitives (not a VulkanWindow) so this
    // internal/ class stays free of any dependency on the parent vulkan/ dir.
    // The caller (VulkanRenderer) pulls these out of its VulkanWindow.
    //   overrides:      SL interposer hooks; the swapchain-relevant entries are
    //                   resolved against the static vulkan-1 entries when null.
    //   fallbackWidth/Height: window client size, used only when the surface
    //                   reports an undefined currentExtent (rare on Win32).
    //   vsyncInterval mirrors RendererConfig::vsyncInterval: 0 = no wait,
    //                   1 = every vblank. (>=2 has no Vulkan present-mode
    //                   equivalent and is gated out before reaching here; it
    //                   falls back to FIFO if it ever does.)
    VulkanSwapchain(VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    VkSurfaceKHR surface,
                    const VulkanCreationOverrides& overrides,
                    uint32_t fallbackWidth,
                    uint32_t fallbackHeight,
                    int vsyncInterval);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Tear down and rebuild the swapchain + image views with a new present
    // mode. The caller owns anything that references the old image views
    // (e.g. framebuffers) and must rebuild those afterwards, and must ensure
    // the device is idle first.
    void recreate(int vsyncInterval);

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

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    VkSurfaceKHR     m_surface        = VK_NULL_HANDLE;
    uint32_t         m_fallbackWidth  = 0;
    uint32_t         m_fallbackHeight = 0;

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
    int                      m_vsyncInterval = 1;
};

} // namespace visLib

#endif // _WIN32
