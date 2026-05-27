#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <memory>

namespace visLib {

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
};

// Vulkan-specific window factory. Use this (instead of createWindow) when the
// caller wants to force the Vulkan backend.
std::unique_ptr<IWindow> createVulkanWindow(const WindowConfig& config,
                                             const VulkanWindowConfig& vkConfig = {});

} // namespace visLib

#endif // _WIN32
