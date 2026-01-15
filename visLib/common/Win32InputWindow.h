// Win32InputWindow.h: Shared Win32 window for keyboard/mouse input
// Used by both D3D12Window (desktop) and OpenXRWindow (VR companion)

#pragma once

#ifdef _WIN32

#include "Win32InputState.h"
#include <memory>
#include <string>

namespace visLib {

// Configuration for Win32InputWindow
struct Win32WindowConfig {
    std::string title = "Window";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool resizable = true;
    bool fullscreen = false;
};

// Win32InputWindow: Creates a Win32 window and handles input via messages
// Owns the Win32InputState and provides access to it
class Win32InputWindow {
public:
    explicit Win32InputWindow(const Win32WindowConfig& config);
    ~Win32InputWindow();

    // Non-copyable, non-movable (due to HWND user data pointer)
    Win32InputWindow(const Win32InputWindow&) = delete;
    Win32InputWindow& operator=(const Win32InputWindow&) = delete;
    Win32InputWindow(Win32InputWindow&&) = delete;
    Win32InputWindow& operator=(Win32InputWindow&&) = delete;

    // Check if window was created successfully
    bool isValid() const { return m_hwnd != nullptr; }

    // Check if close was requested (WM_DESTROY received)
    bool isCloseRequested() const { return m_closeRequested; }

    // Process pending window messages (call once per frame)
    void processMessages();

    // Access to input state
    const Win32InputState& getInputState() const { return *m_inputState; }
    Win32InputState& getInputState() { return *m_inputState; }

    // Window properties
    HWND getHandle() const { return m_hwnd; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }

    // Called when window is resized (can be overridden via callback)
    using ResizeCallback = void(*)(uint32_t width, uint32_t height, void* userData);
    void setResizeCallback(ResizeCallback callback, void* userData);

private:
    bool createWindow(const Win32WindowConfig& config);
    void handleInput(UINT message, WPARAM wParam, LPARAM lParam);
    void onResize(uint32_t width, uint32_t height);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_closeRequested = false;

    std::unique_ptr<Win32InputState> m_inputState;

    // Optional resize callback
    ResizeCallback m_resizeCallback = nullptr;
    void* m_resizeUserData = nullptr;
};

} // namespace visLib

#endif // _WIN32
