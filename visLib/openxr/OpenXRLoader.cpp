// OpenXRLoader.cpp: Dynamic loader for OpenXR runtime

#ifdef _WIN32

#include "OpenXRLoader.h"
#include <sstream>

namespace visLib {
namespace openxr {

// Singleton instance
static std::unique_ptr<OpenXRLoader> g_openXRLoader;
static bool g_initAttempted = false;

OpenXRLoader::~OpenXRLoader()
{
    unload();
}

template<typename T>
bool OpenXRLoader::resolveFunction(const char* name, T* funcPtr)
{
    *funcPtr = reinterpret_cast<T>(GetProcAddress(m_hModule, name));
    if (!*funcPtr) {
        std::ostringstream oss;
        oss << "Failed to resolve function: " << name;
        m_lastError = oss.str();
        return false;
    }
    return true;
}

bool OpenXRLoader::tryLoad()
{
    if (m_hModule) {
        return true; // Already loaded
    }

    // Try to load openxr_loader.dll from various locations
    const char* dllPaths[] = {
        "openxr_loader.dll",           // Current directory / PATH
        "openxr_loader_x64.dll",       // Alternative naming
        nullptr
    };

    for (const char** path = dllPaths; *path != nullptr; ++path) {
        m_hModule = LoadLibraryA(*path);
        if (m_hModule) break;
    }

    if (!m_hModule) {
        m_lastError = "Could not load openxr_loader.dll. VR will not be available.";
        return false;
    }

    // Resolve the core bootstrap function
    if (!resolveFunction("xrGetInstanceProcAddr", &xrGetInstanceProcAddr)) {
        unload();
        return false;
    }

    // Resolve global functions (don't require an instance)
    if (!resolveFunction("xrEnumerateInstanceExtensionProperties", &xrEnumerateInstanceExtensionProperties)) {
        unload();
        return false;
    }

    if (!resolveFunction("xrCreateInstance", &xrCreateInstance)) {
        unload();
        return false;
    }

    // The remaining functions will be resolved via xrGetInstanceProcAddr after instance creation
    // For now, resolve the ones that work without an instance

    m_lastError.clear();
    return true;
}

void OpenXRLoader::unload()
{
    if (m_hModule) {
        FreeLibrary(m_hModule);
        m_hModule = nullptr;
    }

    // Reset all function pointers
    xrGetInstanceProcAddr = nullptr;
    xrEnumerateInstanceExtensionProperties = nullptr;
    xrCreateInstance = nullptr;
    xrDestroyInstance = nullptr;
    xrGetInstanceProperties = nullptr;
    xrGetSystem = nullptr;
    xrGetSystemProperties = nullptr;
    xrEnumerateViewConfigurations = nullptr;
    xrEnumerateViewConfigurationViews = nullptr;
    xrCreateSession = nullptr;
    xrDestroySession = nullptr;
    xrBeginSession = nullptr;
    xrEndSession = nullptr;
    xrRequestExitSession = nullptr;
    xrWaitFrame = nullptr;
    xrBeginFrame = nullptr;
    xrEndFrame = nullptr;
    xrLocateViews = nullptr;
    xrCreateSwapchain = nullptr;
    xrDestroySwapchain = nullptr;
    xrEnumerateSwapchainImages = nullptr;
    xrEnumerateSwapchainFormats = nullptr;
    xrAcquireSwapchainImage = nullptr;
    xrWaitSwapchainImage = nullptr;
    xrReleaseSwapchainImage = nullptr;
    xrCreateReferenceSpace = nullptr;
    xrDestroySpace = nullptr;
    xrPollEvent = nullptr;
    xrResultToString = nullptr;
    xrGetD3D12GraphicsRequirementsKHR = nullptr;
}

OpenXRLoader* getOpenXRLoader()
{
    return g_openXRLoader.get();
}

bool tryInitializeOpenXR()
{
    if (g_initAttempted) {
        return g_openXRLoader != nullptr && g_openXRLoader->isLoaded();
    }

    g_initAttempted = true;
    g_openXRLoader = std::make_unique<OpenXRLoader>();

    if (!g_openXRLoader->tryLoad()) {
        g_openXRLoader.reset();
        return false;
    }

    return true;
}

} // namespace openxr
} // namespace visLib

#endif // _WIN32
