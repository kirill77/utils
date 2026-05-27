#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/common/Win32InputWindow.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include "utils/visLib/vulkan/VulkanWindowConfig.h"
#include <memory>
#include <string>

namespace visLib {

// VulkanWindow - Win32/Vulkan implementation of IWindow.
// Owns a Win32 input window plus a VkInstance / VkSurfaceKHR / VkDevice.
// Step 2a scope: no swapchain, no renderer integration.
class VulkanWindow : public IWindow
{
public:
    VulkanWindow(const WindowConfig& config, const VulkanWindowConfig& vkConfig = {});
    ~VulkanWindow() override;

    VulkanWindow(const VulkanWindow&) = delete;
    VulkanWindow& operator=(const VulkanWindow&) = delete;

    // IWindow interface
    bool isOpen() const override;
    void close() override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;
    void resize(uint32_t width, uint32_t height) override;
    void processEvents() override;
    const InputState& getInputState() const override;
    bool wasFocusLost() const override;
    void resetFocusLost() override;
    void* getNativeHandle() const override;

    // Vulkan accessors (used by VulkanRenderer in later step)
    VkInstance       getInstance() const         { return m_instance; }
    VkSurfaceKHR     getSurface() const          { return m_surface; }
    VkPhysicalDevice getPhysicalDevice() const   { return m_physicalDevice; }
    VkDevice         getDevice() const           { return m_device; }
    uint32_t         getGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    VkQueue          getGraphicsQueue() const    { return m_graphicsQueue; }

    // Adapter name picked at device creation, for diagnostics.
    const std::string& getAdapterName() const    { return m_adapterName; }

private:
    void initVulkan(const VulkanWindowConfig& vkConfig);

    std::unique_ptr<Win32InputWindow> m_window;
    bool m_isOpen = false;

    // Captured from VulkanWindowConfig so initVulkan can route entry-points
    // through SL when overrides are present.
    VulkanCreationOverrides m_overrides;

    VkInstance               m_instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger  = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface         = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice  = VK_NULL_HANDLE;
    VkDevice                 m_device          = VK_NULL_HANDLE;
    uint32_t                 m_graphicsQueueFamily = 0;
    VkQueue                  m_graphicsQueue   = VK_NULL_HANDLE;
    std::string              m_adapterName;
};

} // namespace visLib

#endif // _WIN32
