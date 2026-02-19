// Win32InputWindow.cpp: Shared Win32 window implementation

#ifdef _WIN32

#include "Win32InputWindow.h"
#include <windowsx.h>

namespace visLib {

Win32InputWindow::Win32InputWindow(const Win32WindowConfig& config)
    : m_inputState(std::make_unique<Win32InputState>())
{
    createWindow(config);
}

Win32InputWindow::~Win32InputWindow()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    
    // Restore original display settings if we changed them
    if (m_displayModeChanged) {
        ChangeDisplaySettings(nullptr, 0);
        m_displayModeChanged = false;
    }
}

bool Win32InputWindow::createWindow(const Win32WindowConfig& config)
{
    // Set DPI awareness
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandle(L"user32.dll");
    if (user32) {
        auto setDpiAwareness = reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiAwareness) {
            setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    }

    // Handle exclusive fullscreen: change display resolution
    if (config.exclusiveFullscreen) {
        DEVMODE devMode = {};
        devMode.dmSize = sizeof(DEVMODE);
        devMode.dmPelsWidth = config.width;
        devMode.dmPelsHeight = config.height;
        devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        
        LONG result = ChangeDisplaySettings(&devMode, CDS_FULLSCREEN);
        if (result == DISP_CHANGE_SUCCESSFUL) {
            m_displayModeChanged = true;
            m_width = config.width;
            m_height = config.height;
        } else {
            // Failed to change display mode, fall back to windowed
            m_width = config.width;
            m_height = config.height;
        }
    }
    // Handle borderless fullscreen: use desktop resolution
    else if (config.fullDesktop) {
        m_width = GetSystemMetrics(SM_CXSCREEN);
        m_height = GetSystemMetrics(SM_CYSCREEN);
    }
    // Normal windowed mode
    else {
        m_width = config.width;
        m_height = config.height;
    }

    // Register window class
    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = Win32InputWindow::WindowProc;
    windowClass.hInstance = GetModuleHandle(NULL);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"visLibWin32InputWindowClass";
    RegisterClassEx(&windowClass);

    // Determine window style
    // Exclusive fullscreen and borderless fullscreen both use WS_POPUP
    bool isBorderless = config.fullDesktop || config.exclusiveFullscreen;
    DWORD windowStyle = isBorderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    if (!config.resizable && !isBorderless) {
        windowStyle &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Calculate window rect for client area size
    RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    // Convert title to wide string
    std::wstring wideTitle(config.title.begin(), config.title.end());

    // Window position: top-left for fullscreen modes, default for windowed
    int xPos = isBorderless ? 0 : CW_USEDEFAULT;
    int yPos = isBorderless ? 0 : CW_USEDEFAULT;

    // Create window
    m_hwnd = CreateWindow(
        windowClass.lpszClassName,
        wideTitle.c_str(),
        windowStyle,
        xPos,
        yPos,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        windowClass.hInstance,
        this);

    if (!m_hwnd) {
        // Restore display settings if we changed them
        if (m_displayModeChanged) {
            ChangeDisplaySettings(nullptr, 0);
            m_displayModeChanged = false;
        }
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

void Win32InputWindow::processMessages()
{
    m_inputState->beginFrame();

    MSG msg = {};
    while (PeekMessage(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    m_inputState->endFrame();
}

void Win32InputWindow::setResizeCallback(ResizeCallback callback, void* userData)
{
    m_resizeCallback = callback;
    m_resizeUserData = userData;
}

void Win32InputWindow::setDisplayText(const std::string& text)
{
    m_displayText = text;
    // Trigger repaint to show new text
    if (m_hwnd) {
        InvalidateRect(m_hwnd, NULL, TRUE);
    }
}

void Win32InputWindow::resize(uint32_t width, uint32_t height)
{
    if (!m_hwnd || width == 0 || height == 0) {
        return;
    }

    DWORD style = static_cast<DWORD>(GetWindowLong(m_hwnd, GWL_STYLE));
    RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    AdjustWindowRect(&rect, style, FALSE);

    SetWindowPos(m_hwnd, nullptr, 0, 0,
                 rect.right - rect.left, rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Win32InputWindow::onResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return;
    }

    m_width = width;
    m_height = height;

    if (m_resizeCallback) {
        m_resizeCallback(width, height, m_resizeUserData);
    }
}

void Win32InputWindow::handleInput(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_KEYDOWN:
            m_inputState->onKeyDown(wParam);
            break;
        case WM_KEYUP:
            m_inputState->onKeyUp(wParam);
            break;
        case WM_LBUTTONDOWN:
            m_inputState->onMouseButton(Key::MouseLeft, true);
            break;
        case WM_LBUTTONUP:
            m_inputState->onMouseButton(Key::MouseLeft, false);
            break;
        case WM_RBUTTONDOWN:
            m_inputState->onMouseButton(Key::MouseRight, true);
            break;
        case WM_RBUTTONUP:
            m_inputState->onMouseButton(Key::MouseRight, false);
            break;
        case WM_MBUTTONDOWN:
            m_inputState->onMouseButton(Key::MouseMiddle, true);
            break;
        case WM_MBUTTONUP:
            m_inputState->onMouseButton(Key::MouseMiddle, false);
            break;
        case WM_XBUTTONDOWN:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                m_inputState->onMouseButton(Key::MouseX1, true);
            else
                m_inputState->onMouseButton(Key::MouseX2, true);
            break;
        case WM_XBUTTONUP:
            if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                m_inputState->onMouseButton(Key::MouseX1, false);
            else
                m_inputState->onMouseButton(Key::MouseX2, false);
            break;
        case WM_MOUSEMOVE:
            m_inputState->onMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        case WM_MOUSEWHEEL:
            m_inputState->onMouseWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA);
            break;
    }
}

LRESULT CALLBACK Win32InputWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Win32InputWindow* window = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<Win32InputWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Win32InputWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        switch (message)
        {
            case WM_DESTROY:
                window->m_closeRequested = true;
                return 0;

            case WM_SIZE:
                window->onResize(LOWORD(lParam), HIWORD(lParam));
                return 0;

            case WM_PAINT:
                if (!window->m_displayText.empty()) {
                    PAINTSTRUCT ps;
                    HDC hdc = BeginPaint(hwnd, &ps);

                    RECT rect;
                    GetClientRect(hwnd, &rect);

                    // Create large font (height ~120 pixels, ~10x default)
                    HFONT hFont = CreateFontA(
                        120, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

                    // Set up colors
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(0, 0, 0));

                    // Draw centered text
                    DrawTextA(hdc, window->m_displayText.c_str(), -1, &rect,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                    // Cleanup
                    SelectObject(hdc, hOldFont);
                    DeleteObject(hFont);

                    EndPaint(hwnd, &ps);
                    return 0;
                }
                break;  // Let DefWindowProc handle if no text

            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_MOUSEMOVE:
            case WM_MOUSEWHEEL:
                window->handleInput(message, wParam, lParam);
                return 0;
        }
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

} // namespace visLib

#endif // _WIN32
