#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR

#include <Windows.h>
#include <vulkan/vulkan.h>

#pragma comment(lib, "vulkan-1.lib")

namespace visLib {

// Optional overrides for Vulkan entry-point loading. When set, VulkanWindow /
// VulkanSwapchain / VulkanRenderer route these calls through SL's interposed
// wrappers instead of the statically-linked vulkan-1.dll entries. Used by
// Streamline to install its dispatch tables and its VK_NV_low_latency2
// overlay on the swapchain (analog of D3D12CreationOverrides).
//
// The swapchain / surface / present / wait set are the eight mandatory
// Vulkan hooks SL declares in streamline/include/sl_hooks.h. They must be
// installed together: mixing some entries through SL and some through
// static-link desyncs SL's internal swapchain->device->instance map and
// crashes Reflex's swapchain-bound entries.
//
// Lives here (internal/VulkanCommon.h) rather than in VulkanWindowConfig.h so
// that VulkanSwapchain (also in internal/) can accept it without taking a
// dependency on the parent vulkan/ directory, which scanIncludes.py flags as
// a circular directory dependency.
struct VulkanCreationOverrides {
    // Instance / device creation set.
    PFN_vkGetInstanceProcAddr      pfnVkGetInstanceProcAddr      = nullptr;  // reserved for future dispatch refactor
    PFN_vkCreateInstance           pfnVkCreateInstance           = nullptr;
    PFN_vkCreateDevice             pfnVkCreateDevice             = nullptr;
    // Streamline tracks instance<->physicalDevice mapping inside its
    // vkEnumeratePhysicalDevices wrapper; bypassing that breaks SL's
    // vkCreateDevice path. Route enumeration through SL when present.
    PFN_vkEnumeratePhysicalDevices pfnVkEnumeratePhysicalDevices = nullptr;

    // Surface / swapchain / present / wait set. Required for SL to install
    // VK_NV_low_latency2 hooks on the swapchain; without this set, Reflex
    // setOptions and the per-frame markers crash the driver.
    PFN_vkCreateWin32SurfaceKHR    pfnVkCreateWin32SurfaceKHR    = nullptr;
    PFN_vkDestroySurfaceKHR        pfnVkDestroySurfaceKHR        = nullptr;
    PFN_vkCreateSwapchainKHR       pfnVkCreateSwapchainKHR       = nullptr;
    PFN_vkDestroySwapchainKHR      pfnVkDestroySwapchainKHR      = nullptr;
    PFN_vkGetSwapchainImagesKHR    pfnVkGetSwapchainImagesKHR    = nullptr;
    PFN_vkAcquireNextImageKHR      pfnVkAcquireNextImageKHR      = nullptr;
    PFN_vkQueuePresentKHR          pfnVkQueuePresentKHR          = nullptr;
    PFN_vkDeviceWaitIdle           pfnVkDeviceWaitIdle           = nullptr;
};

} // namespace visLib

#endif // _WIN32
