#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR

#include <Windows.h>
#include <vulkan/vulkan.h>

#pragma comment(lib, "vulkan-1.lib")

// Platform-gated Vulkan include shared by the vulkan/ backend. Defines the
// Win32 surface platform before pulling <vulkan/vulkan.h> so PFN_vk* typedefs
// (incl. PFN_vkCreateWin32SurfaceKHR) are available to both the public
// VulkanWindowConfig.h (for VulkanCreationOverrides) and the internal/ classes.

#endif // _WIN32
