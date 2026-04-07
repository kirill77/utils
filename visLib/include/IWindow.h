#pragma once

#include <cstdint>
#include <string>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dxgi.h>
#include <d3d12.h>
#endif

namespace visLib {

// Forward declarations
class InputState;

#ifdef _WIN32
// Optional overrides for D3D12/DXGI creation functions.
// When set, these are used instead of the standard Windows APIs, allowing
// an interposer (e.g., Streamline) to proxy device and factory creation.
struct D3D12CreationOverrides {
    using FnCreateDXGIFactory2 = HRESULT(WINAPI*)(UINT Flags, REFIID riid, void** ppFactory);
    using FnD3D12CreateDevice  = HRESULT(WINAPI*)(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

    FnCreateDXGIFactory2 pfnCreateDXGIFactory2 = nullptr;
    FnD3D12CreateDevice  pfnD3D12CreateDevice  = nullptr;
};
#endif

// Window creation configuration
struct WindowConfig {
    std::string title = "visLib Window";
    uint32_t width = 2560;
    uint32_t height = 1440;
    bool borderless = false;          // Use WS_POPUP (no title bar / borders) at the given width x height
    bool fullDesktop = true;          // Borderless fullscreen at desktop resolution
    bool exclusiveFullscreen = false;  // Exclusive fullscreen (changes display resolution)
    bool resizable = true;
    bool vsync = true;

    // VR mode: If true, attempts to create a VR window using OpenXR.
    // Falls back to desktop window if VR is not available.
    // When VR is active, width/height are ignored (headset resolution is used).
    bool preferVR = false;

#ifdef _WIN32
    D3D12CreationOverrides d3d12Overrides;
#endif
};

// Abstract window interface
// Provides platform-independent window management and input handling
class IWindow {
public:
    virtual ~IWindow() = default;

    // Window state
    virtual bool isOpen() const = 0;
    virtual void close() = 0;

    // Window dimensions (in pixels)
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    // Resize the window client area to the given dimensions.
    // Triggers swap chain / back buffer resize internally.
    virtual void resize(uint32_t width, uint32_t height) = 0;

    // Aspect ratio (width / height)
    float getAspectRatio() const {
        uint32_t h = getHeight();
        return (h > 0) ? static_cast<float>(getWidth()) / static_cast<float>(h) : 1.0f;
    }

    // Process pending window events (input, resize, close, etc.)
    // Call once per frame before rendering
    virtual void processEvents() = 0;

    // Get current input state
    virtual const InputState& getInputState() const = 0;

    // Focus-loss tracking: returns true if the window lost focus since the last reset.
    // Used to detect when the user switches away from the test window.
    virtual bool wasFocusLost() const { return false; }
    virtual void resetFocusLost() {}

    // Platform-specific native handle
    // Returns HWND on Windows, GLFWwindow* on GLFW, etc.
    // Use with caution - breaks platform independence
    virtual void* getNativeHandle() const = 0;
};

// Factory function declaration
// Implementation provided by the backend (visLib_d3d12, visLib_vulkan, etc.)
std::unique_ptr<IWindow> createWindow(const WindowConfig& config = {});

} // namespace visLib
