#ifdef _WIN32

#include "VulkanWindow.h"
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdio>

namespace visLib {

namespace {

const char* kKhronosValidationLayer = "VK_LAYER_KHRONOS_validation";

bool hasInstanceLayer(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers) {
        if (strcmp(l.layerName, name) == 0) return true;
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* /*userData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        char buf[1024];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                    "[VK] %s\n", pData->pMessage ? pData->pMessage : "(null)");
        OutputDebugStringA(buf);
    }
    return VK_FALSE;
}

} // namespace

VulkanWindow::VulkanWindow(const WindowConfig& config, const VulkanWindowConfig& vkConfig)
{
    Win32WindowConfig winConfig;
    winConfig.title = config.title;
    winConfig.width = config.width;
    winConfig.height = config.height;
    winConfig.resizable = config.resizable;
    winConfig.borderless = config.borderless;
    winConfig.fullDesktop = config.fullDesktop;
    winConfig.exclusiveFullscreen = config.exclusiveFullscreen;

    m_window = std::make_unique<Win32InputWindow>(winConfig);
    if (!m_window->isValid()) {
        throw std::runtime_error("Failed to create window");
    }

    initVulkan(vkConfig);

    m_isOpen = true;
}

VulkanWindow::~VulkanWindow()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_debugMessenger != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        auto pfn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (pfn) pfn(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

bool VulkanWindow::isOpen() const             { return m_isOpen && !m_window->isCloseRequested(); }
void VulkanWindow::close()                    { m_isOpen = false; }
uint32_t VulkanWindow::getWidth() const       { return m_window->getWidth(); }
uint32_t VulkanWindow::getHeight() const      { return m_window->getHeight(); }
void VulkanWindow::resize(uint32_t w, uint32_t h) { m_window->resize(w, h); }
void VulkanWindow::processEvents()            { m_window->processMessages(); }
const InputState& VulkanWindow::getInputState() const { return m_window->getInputState(); }
bool VulkanWindow::wasFocusLost() const       { return m_window->wasFocusLost(); }
void VulkanWindow::resetFocusLost()           { m_window->resetFocusLost(); }
void* VulkanWindow::getNativeHandle() const   { return m_window->getHandle(); }

void VulkanWindow::initVulkan(const VulkanWindowConfig& vkConfig)
{
    // --- Instance ---
    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    std::vector<const char*> instanceLayers;
    bool wantDebugUtils = false;
    if (vkConfig.enableValidation && hasInstanceLayer(kKhronosValidationLayer)) {
        instanceLayers.push_back(kKhronosValidationLayer);
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        wantDebugUtils = true;
    }

    VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName   = "visLib";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName        = "visLib";
    appInfo.engineVersion      = 1;
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instInfo.pApplicationInfo        = &appInfo;
    instInfo.enabledLayerCount       = static_cast<uint32_t>(instanceLayers.size());
    instInfo.ppEnabledLayerNames     = instanceLayers.data();
    instInfo.enabledExtensionCount   = static_cast<uint32_t>(instanceExtensions.size());
    instInfo.ppEnabledExtensionNames = instanceExtensions.data();

    if (vkCreateInstance(&instInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateInstance failed");
    }

    if (wantDebugUtils) {
        auto pfn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (pfn) {
            VkDebugUtilsMessengerCreateInfoEXT mi = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = &debugMessengerCallback;
            pfn(m_instance, &mi, nullptr, &m_debugMessenger);
        }
    }

    // --- Surface ---
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd      = m_window->getHandle();
    if (vkCreateWin32SurfaceKHR(m_instance, &sci, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateWin32SurfaceKHR failed");
    }

    // --- Physical device + queue family selection ---
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    auto pickQueueFamily = [&](VkPhysicalDevice pd, uint32_t& outFamily) -> bool {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qprops.data());
        for (uint32_t i = 0; i < qCount; ++i) {
            if (!(qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, m_surface, &presentSupport);
            if (presentSupport) { outFamily = i; return true; }
        }
        return false;
    };

    // Prefer discrete GPU
    for (auto pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
        uint32_t fam;
        if (pickQueueFamily(pd, fam)) {
            m_physicalDevice = pd;
            m_graphicsQueueFamily = fam;
            m_adapterName = props.deviceName;
            break;
        }
    }
    // Fallback: any device with a graphics+present family
    if (m_physicalDevice == VK_NULL_HANDLE) {
        for (auto pd : devices) {
            uint32_t fam;
            if (pickQueueFamily(pd, fam)) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(pd, &props);
                m_physicalDevice = pd;
                m_graphicsQueueFamily = fam;
                m_adapterName = props.deviceName;
                break;
            }
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("No suitable Vulkan physical device");
    }

    // --- Logical device ---
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = m_graphicsQueueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &queuePriority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = deviceExtensions;

    if (vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDevice failed");
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
}

std::unique_ptr<VulkanWindow> createVulkanWindow(const WindowConfig& config,
                                                  const VulkanWindowConfig& vkConfig)
{
    return std::make_unique<VulkanWindow>(config, vkConfig);
}

} // namespace visLib

#endif // _WIN32
