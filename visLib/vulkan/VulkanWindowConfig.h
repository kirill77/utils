#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <memory>

namespace visLib {

class VulkanWindow;

// VulkanCreationOverrides moved to internal/VulkanCommon.h (included above) so
// VulkanSwapchain can use it without a circular directory dependency.

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
