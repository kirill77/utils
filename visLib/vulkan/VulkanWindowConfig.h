#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <memory>

namespace visLib {

class VulkanWindow;

// Optional overrides for Vulkan entry-point loading. When set, VulkanWindow
// routes vkCreateInstance / vkCreateDevice through these function pointers
// instead of the statically-linked vulkan-1.dll entries. Used by Streamline
// to install its dispatch tables (analog of D3D12CreationOverrides).
struct VulkanCreationOverrides {
    PFN_vkGetInstanceProcAddr pfnVkGetInstanceProcAddr = nullptr;  // optional, currently unused but reserved
    PFN_vkCreateInstance      pfnVkCreateInstance      = nullptr;
    PFN_vkCreateDevice        pfnVkCreateDevice        = nullptr;
};

// Vulkan-specific window configuration. Passed alongside the platform-agnostic
// WindowConfig to the Vulkan backend factory.
struct VulkanWindowConfig {
    // Enable VK_LAYER_KHRONOS_validation and a debug-utils messenger.
    // Defaults to true under _DEBUG, false otherwise.
#ifdef _DEBUG
    bool enableValidation = true;
#else
    bool enableValidation = false;
#endif

    VulkanCreationOverrides creationOverrides;
};

// Vulkan-specific window factory. Returns the concrete VulkanWindow type so
// downstream peer factories (e.g. createVulkanRenderer) can be statically
// type-checked against the matching backend.
std::unique_ptr<VulkanWindow> createVulkanWindow(const WindowConfig& config,
                                                  const VulkanWindowConfig& vkConfig = {});

} // namespace visLib

#endif // _WIN32
