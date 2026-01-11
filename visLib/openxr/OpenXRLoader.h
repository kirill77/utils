// OpenXRLoader.h: Dynamic loader for OpenXR runtime
// Loads openxr_loader.dll at runtime to avoid build-time dependencies

#pragma once

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>  // Must be before OpenXR for D3D12 type definitions

// Include OpenXR types (headers only, no lib dependency)
#define XR_USE_GRAPHICS_API_D3D12
#include "utils/openXR/include/openxr/openxr.h"
#include "utils/openXR/include/openxr/openxr_platform.h"

#include <string>

namespace visLib {
namespace openxr {

// OpenXRLoader: Dynamically loads openxr_loader.dll and resolves function pointers
// Allows graceful fallback to desktop rendering if OpenXR is not available
class OpenXRLoader
{
public:
    OpenXRLoader() = default;
    ~OpenXRLoader();

    // Non-copyable
    OpenXRLoader(const OpenXRLoader&) = delete;
    OpenXRLoader& operator=(const OpenXRLoader&) = delete;

    // Attempts to load OpenXR. Returns true if successful.
    // If false, call getLastError() for details.
    bool tryLoad();

    // Unload the DLL and reset all function pointers
    void unload();

    // Check if OpenXR is loaded and ready
    bool isLoaded() const { return m_hModule != nullptr; }

    // Get error message from last failed operation
    const std::string& getLastError() const { return m_lastError; }

    // ===== Core OpenXR Function Pointers =====
    // These are populated after successful tryLoad()

    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateInstance xrCreateInstance = nullptr;
    PFN_xrDestroyInstance xrDestroyInstance = nullptr;
    PFN_xrGetInstanceProperties xrGetInstanceProperties = nullptr;
    PFN_xrGetSystem xrGetSystem = nullptr;
    PFN_xrGetSystemProperties xrGetSystemProperties = nullptr;
    PFN_xrEnumerateViewConfigurations xrEnumerateViewConfigurations = nullptr;
    PFN_xrEnumerateViewConfigurationViews xrEnumerateViewConfigurationViews = nullptr;
    PFN_xrCreateSession xrCreateSession = nullptr;
    PFN_xrDestroySession xrDestroySession = nullptr;
    PFN_xrBeginSession xrBeginSession = nullptr;
    PFN_xrEndSession xrEndSession = nullptr;
    PFN_xrRequestExitSession xrRequestExitSession = nullptr;
    PFN_xrWaitFrame xrWaitFrame = nullptr;
    PFN_xrBeginFrame xrBeginFrame = nullptr;
    PFN_xrEndFrame xrEndFrame = nullptr;
    PFN_xrLocateViews xrLocateViews = nullptr;
    PFN_xrCreateSwapchain xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages xrEnumerateSwapchainImages = nullptr;
    PFN_xrEnumerateSwapchainFormats xrEnumerateSwapchainFormats = nullptr;
    PFN_xrAcquireSwapchainImage xrAcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage xrWaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage xrReleaseSwapchainImage = nullptr;
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
    PFN_xrDestroySpace xrDestroySpace = nullptr;
    PFN_xrPollEvent xrPollEvent = nullptr;
    PFN_xrResultToString xrResultToString = nullptr;

    // D3D12 extension function
    PFN_xrGetD3D12GraphicsRequirementsKHR xrGetD3D12GraphicsRequirementsKHR = nullptr;

    // Helper to resolve additional functions via xrGetInstanceProcAddr
    template<typename T>
    bool getInstanceProcAddr(XrInstance instance, const char* name, T* funcPtr) {
        if (!xrGetInstanceProcAddr) return false;
        XrResult result = xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(funcPtr));
        return XR_SUCCEEDED(result);
    }

private:
    HMODULE m_hModule = nullptr;
    std::string m_lastError;

    // Helper to resolve a function pointer from the DLL
    template<typename T>
    bool resolveFunction(const char* name, T* funcPtr);
};

// Global accessor for the OpenXR loader singleton
// Returns nullptr if OpenXR is not available
OpenXRLoader* getOpenXRLoader();

// Attempt to initialize OpenXR globally
// Returns true if OpenXR is available and loaded
bool tryInitializeOpenXR();

} // namespace openxr
} // namespace visLib

#endif // _WIN32
