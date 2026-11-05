#ifdef _WIN32

#include "VulkanWindow.h"
#include <stdexcept>
#include <vector>
#include <cstring>
#include <cstdio>

namespace visLib {

namespace {

const char* kKhronosValidationLayer = "VK_LAYER_KHRONOS_validation";

// Minimum Khronos validation-layer spec version we'll enable alongside DLSS-G
// Frame Generation. DLSS-G's snippet (nvngx_dlssg) chains a
// VkCuModuleTexturingModeCreateInfoNVX onto vkCreateCuModuleNVX's pNext, which
// was added in VK_NVX_binary_import revision 2 (Khronos-published 2024-11-04).
// Older validation layers (e.g. Vulkan SDK 1.3.296) don't recognize the struct,
// flag "VkCuModuleCreateInfoNVX-pNext must be NULL", mishandle the chain, and
// crash FG deep inside NVSDK_NGX_VULKAN_EvaluateFeature. Rev 2 ships in the
// 1.4.x SDK line, so we require >= 1.4.0 and otherwise skip validation rather
// than risk the crash. (Conservative — a late-1.3.x layer may also carry rev 2;
// lower this if you confirm a specific older layer is safe.)
// Note: VK_API_VERSION_1_4 isn't defined by the 1.3.296 headers this builds
// against, so spell the threshold out with VK_MAKE_API_VERSION.
constexpr uint32_t kMinValidationLayerSpecVersion = VK_MAKE_API_VERSION(0, 1, 4, 0);

// Spec version (VK_MAKE_API_VERSION-encoded) of the named instance layer, or 0
// if the layer isn't present.
uint32_t instanceLayerSpecVersion(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers) {
        if (strcmp(l.layerName, name) == 0) return l.specVersion;
    }
    return 0;
}

// pUserData is the window's IValidationSink (or null). The window keeps the
// sink alive until the messenger and the instance are destroyed.
VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void* pUserData)
{
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }
    const char* pText = pData->pMessage ? pData->pMessage : "";
    auto* pSink = static_cast<IValidationSink*>(pUserData);
    if (!pSink) {
        char buf[1024];
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[VK] %s\n", pText);
        OutputDebugStringA(buf);
        return VK_FALSE;
    }
    // Message name (a VUID for most validation messages), else the ID number.
    char idBuf[32];
    const char* pId = pData->pMessageIdName;
    if (!pId || !*pId) {
        _snprintf_s(idBuf, sizeof(idBuf), _TRUNCATE, "0x%08x", static_cast<uint32_t>(pData->messageIdNumber));
        pId = idBuf;
    }
    pSink->onMessage(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                         ? ValidationSeverity::Error : ValidationSeverity::Warning,
                     pId, pText);
    return VK_FALSE;
}

std::string versionString(uint32_t v)
{
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) +
           "." + std::to_string(VK_API_VERSION_PATCH(v));
}

} // namespace

VulkanWindow::VulkanWindow(const WindowConfig& config, const VulkanWindowConfig& vkConfig)
    : m_overrides(vkConfig.creationOverrides)
    , m_pValidationSink(vkConfig.pValidationSink)
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
        auto fnWaitIdle = m_overrides.pfnVkDeviceWaitIdle
            ? m_overrides.pfnVkDeviceWaitIdle
            : &vkDeviceWaitIdle;
        fnWaitIdle(m_device);
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE && m_instance != VK_NULL_HANDLE) {
        auto fnDestroySurface = m_overrides.pfnVkDestroySurfaceKHR
            ? m_overrides.pfnVkDestroySurfaceKHR
            : &vkDestroySurfaceKHR;
        fnDestroySurface(m_instance, m_surface, nullptr);
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
    const uint32_t valLayerVer =
        vkConfig.enableValidation ? instanceLayerSpecVersion(kKhronosValidationLayer) : 0;
    const std::string valLayerVerStr = valLayerVer ? versionString(valLayerVer) : "";
    auto reportStatus = [&](ValidationLayerStatus status) {
        if (m_pValidationSink) m_pValidationSink->onLayerStatus(status, valLayerVerStr.c_str());
    };
    if (vkConfig.enableValidation && valLayerVer >= kMinValidationLayerSpecVersion) {
        instanceLayers.push_back(kKhronosValidationLayer);
        instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        wantDebugUtils = true;
        fprintf(stderr, "[visLib] Vulkan validation enabled (VK_LAYER_KHRONOS_validation %s).\n",
                valLayerVerStr.c_str());
    } else if (vkConfig.enableValidation && valLayerVer == 0) {
        fprintf(stderr, "[visLib] Vulkan validation requested but VK_LAYER_KHRONOS_validation not found; continuing without it.\n");
        reportStatus(ValidationLayerStatus::Missing);
    } else if (vkConfig.enableValidation) {
        fprintf(stderr,
                "[visLib] Skipping VK_LAYER_KHRONOS_validation: layer %s is older than required %s. "
                "Older layers mishandle DLSS-G's VK_NVX_binary_import rev2 pNext and crash Frame Generation; "
                "install Vulkan SDK >= 1.4.x to enable validation.\n",
                valLayerVerStr.c_str(), versionString(kMinValidationLayerSpecVersion).c_str());
        reportStatus(ValidationLayerStatus::TooOld);
    }

    // Chained onto the instance create info too, so messages from
    // vkCreateInstance and vkDestroyInstance are caught.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                  | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &debugMessengerCallback;
    messengerInfo.pUserData       = m_pValidationSink.get();

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
    if (wantDebugUtils) {
        instInfo.pNext = &messengerInfo;
    }

    // Route through the SL proxy if provided so SL installs its dispatch
    // tables in the resulting VkInstance; the per-handle dispatch design of
    // Vulkan means subsequent vkXxx calls via the static-link loader still
    // invoke SL's hooks once it owns the VkInstance.
    auto fnCreateInstance = m_overrides.pfnVkCreateInstance
        ? m_overrides.pfnVkCreateInstance
        : &vkCreateInstance;
    if (fnCreateInstance(&instInfo, nullptr, &m_instance) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateInstance failed");
    }

    if (wantDebugUtils) {
        auto pfn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        const bool ok = pfn && pfn(m_instance, &messengerInfo, nullptr, &m_debugMessenger) == VK_SUCCESS;
        if (!ok) {
            fprintf(stderr, "[visLib] vkCreateDebugUtilsMessengerEXT failed; validation messages are not captured.\n");
        }
        reportStatus(ok ? ValidationLayerStatus::Active : ValidationLayerStatus::Failed);
    }

    // --- Surface ---
    VkWin32SurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sci.hinstance = GetModuleHandleW(nullptr);
    sci.hwnd      = m_window->getHandle();
    auto fnCreateSurface = m_overrides.pfnVkCreateWin32SurfaceKHR
        ? m_overrides.pfnVkCreateWin32SurfaceKHR
        : &vkCreateWin32SurfaceKHR;
    if (fnCreateSurface(m_instance, &sci, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateWin32SurfaceKHR failed");
    }

    // --- Physical device + queue family selection ---
    // Route enumeration through SL's wrapper when present so its
    // instanceDeviceMap[physDev] = instance bookkeeping is populated;
    // without that, SL's vkCreateDevice crashes looking up a null instance.
    auto fnEnum = m_overrides.pfnVkEnumeratePhysicalDevices
        ? m_overrides.pfnVkEnumeratePhysicalDevices
        : &vkEnumeratePhysicalDevices;
    uint32_t deviceCount = 0;
    fnEnum(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("No Vulkan physical devices");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    fnEnum(m_instance, &deviceCount, devices.data());

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

    // Query supported features (1.0 + 1.2 + 1.3) and request the subset we use
    // plus the bits Streamline's Vulkan plugins need. SL requires at minimum
    // VkPhysicalDeviceVulkan13Features::privateData when sl.interposer is in
    // the chain — without it, SL crashes during plugin startup. Enabling the
    // related features (dynamicRendering, synchronization2, timelineSemaphore,
    // bufferDeviceAddress) is cheap and covers the common SL plugin set.
    VkPhysicalDeviceVulkan13Features supported13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features supported12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    supported12.pNext = &supported13;
    VkPhysicalDeviceFeatures2 supported2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    supported2.pNext = &supported12;
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported2);

    VkPhysicalDeviceVulkan13Features want13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    want13.privateData       = supported13.privateData;
    want13.dynamicRendering  = supported13.dynamicRendering;
    want13.synchronization2  = supported13.synchronization2;

    VkPhysicalDeviceVulkan12Features want12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    want12.timelineSemaphore   = supported12.timelineSemaphore;
    want12.bufferDeviceAddress = supported12.bufferDeviceAddress;
    // hostQueryReset lets VulkanQuery invalidate a slot's query results on the
    // CPU when it recycles the slot (vkResetQueryPool). Without it, recycled
    // timestamp slots keep their previous values *available* in the pool, so a
    // poll that races ahead of the deferred GPU-side reset reads stale data and
    // GPU times get attributed to the wrong frame (see VulkanQuery.cpp).
    want12.hostQueryReset      = supported12.hostQueryReset;
    want12.pNext = &want13;

    VkPhysicalDeviceFeatures2 want2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    want2.features.pipelineStatisticsQuery = supported2.features.pipelineStatisticsQuery;
    want2.features.samplerAnisotropy       = supported2.features.samplerAnisotropy;
    want2.pNext = &want12;

    VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = deviceExtensions;
    // pEnabledFeatures must be NULL when pNext points at VkPhysicalDeviceFeatures2.
    dci.pNext                   = &want2;

    auto fnCreateDevice = m_overrides.pfnVkCreateDevice
        ? m_overrides.pfnVkCreateDevice
        : &vkCreateDevice;
    if (fnCreateDevice(m_physicalDevice, &dci, nullptr, &m_device) != VK_SUCCESS) {
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
