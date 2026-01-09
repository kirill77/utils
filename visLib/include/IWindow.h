#pragma once

#include <cstdint>
#include <string>
#include <memory>

namespace visLib {

// Forward declarations
class InputState;

// Window creation configuration
struct WindowConfig {
    std::string title = "visLib Window";
    uint32_t width = 2560;
    uint32_t height = 1440;
    bool fullscreen = true;
    bool resizable = true;
    bool vsync = true;
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

    // Platform-specific native handle
    // Returns HWND on Windows, GLFWwindow* on GLFW, etc.
    // Use with caution - breaks platform independence
    virtual void* getNativeHandle() const = 0;
};

// Factory function declaration
// Implementation provided by the backend (visLib_d3d12, visLib_vulkan, etc.)
std::unique_ptr<IWindow> createWindow(const WindowConfig& config = {});

} // namespace visLib
