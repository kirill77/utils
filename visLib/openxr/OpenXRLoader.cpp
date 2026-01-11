// OpenXRLoader.cpp: Dynamic loader for OpenXR runtime

#ifdef _WIN32

#include "OpenXRLoader.h"
#include "utils/fileUtils/fileUtils.h"
#include <sstream>
#include <filesystem>

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

    // First, try to find the DLL via FileUtils (searches from repo root upwards)
    std::filesystem::path foundPath;
    if (FileUtils::findTheFile(L"src/utils/openXR/native/x64/release/bin/openxr_loader.dll", foundPath)) {
        m_hModule = LoadLibraryW(foundPath.c_str());
    }

    // Fallback: try current directory / PATH
    if (!m_hModule) {
        m_hModule = LoadLibraryA("openxr_loader.dll");
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
