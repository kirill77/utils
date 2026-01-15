// OpenXRWindow.cpp: IWindow implementation for VR headsets

#ifdef _WIN32

#include "OpenXRWindow.h"
#include "OpenXRLoader.h"
#include <dxgi1_6.h>
#include <windowsx.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace visLib {
namespace openxr {

OpenXRWindow::OpenXRWindow(const WindowConfig& config)
    : m_inputState(std::make_unique<D3D12InputState>())
{
    // Create companion desktop window for input
    if (!createCompanionWindow(config)) {
        m_lastError = "Failed to create companion window";
        return;
    }

    // Try to initialize OpenXR
    if (!tryInitializeOpenXR()) {
        m_lastError = "OpenXR not available: " + (getOpenXRLoader() ? getOpenXRLoader()->getLastError() : "loader failed");
        return;
    }

    // Initialize D3D12 (needed for OpenXR graphics binding)
    if (!initializeD3D12()) {
        return;
    }

    // Initialize OpenXR session
    if (!initializeOpenXR()) {
        return;
    }

    m_isOpen = true;
    m_vrReady = true;
}

OpenXRWindow::~OpenXRWindow()
{
    close();
    
    // Destroy companion window
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool OpenXRWindow::initializeD3D12()
{
    HRESULT hr;

    // Enable debug layer in debug builds
#ifdef _DEBUG
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    // Create DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        m_lastError = "Failed to create DXGI factory";
        return false;
    }

    // Find a suitable adapter
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                          IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        // Try to create device
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
        if (SUCCEEDED(hr)) break;
    }

    if (!m_device) {
        m_lastError = "Failed to create D3D12 device";
        return false;
    }

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue));
    if (FAILED(hr)) {
        m_lastError = "Failed to create command queue";
        return false;
    }

    return true;
}

bool OpenXRWindow::initializeOpenXR()
{
    m_session = std::make_unique<OpenXRSession>();

    if (!m_session->initialize(m_device.Get(), m_commandQueue.Get())) {
        m_lastError = "OpenXR session failed: " + m_session->getLastError();
        m_session.reset();
        return false;
    }

    return true;
}

bool OpenXRWindow::isOpen() const
{
    return m_isOpen;
}

void OpenXRWindow::close()
{
    m_isOpen = false;

    if (m_session) {
        m_session->shutdown();
        m_session.reset();
    }

    m_commandQueue.Reset();
    m_device.Reset();
}

uint32_t OpenXRWindow::getWidth() const
{
    if (m_session) {
        return m_session->getRenderWidth();
    }
    return 1920;  // Fallback
}

uint32_t OpenXRWindow::getHeight() const
{
    if (m_session) {
        return m_session->getRenderHeight();
    }
    return 1080;  // Fallback
}

void OpenXRWindow::processEvents()
{
    // Begin new input frame
    if (m_inputState) {
        m_inputState->beginFrame();
    }
    
    // Process companion window messages (keyboard, mouse input)
    MSG msg = {};
    while (PeekMessage(&msg, m_hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // End input frame
    if (m_inputState) {
        m_inputState->endFrame();
    }

    // Poll OpenXR events (session state changes, etc.)
    if (m_session) {
        if (!m_session->pollEvents()) {
            m_isOpen = false;
        }
    }
}

const InputState& OpenXRWindow::getInputState() const
{
    return *m_inputState;
}

void* OpenXRWindow::getNativeHandle() const
{
    return m_hwnd;
}

bool OpenXRWindow::createCompanionWindow(const WindowConfig& config)
{
    // Register window class
    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = OpenXRWindow::WindowProc;
    windowClass.hInstance = GetModuleHandle(NULL);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"OpenXRCompanionWindowClass";
    RegisterClassEx(&windowClass);

    // Window style - simple, non-resizable
    DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    // Use smaller size for companion window
    m_windowWidth = 640;
    m_windowHeight = 480;

    // Calculate window rect for client area size
    RECT windowRect = { 0, 0, static_cast<LONG>(m_windowWidth), static_cast<LONG>(m_windowHeight) };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    // Build title with VR indicator
    std::string title = config.title + " [VR]";
    std::wstring wideTitle(title.begin(), title.end());

    // Create window
    m_hwnd = CreateWindow(
        windowClass.lpszClassName,
        wideTitle.c_str(),
        windowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        windowClass.hInstance,
        this);

    if (!m_hwnd) {
        return false;
    }

    // Show the window
    ShowWindow(m_hwnd, SW_SHOW);
    
    return true;
}

void OpenXRWindow::handleInput(UINT message, WPARAM wParam, LPARAM lParam)
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

LRESULT CALLBACK OpenXRWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    OpenXRWindow* window = nullptr;
    
    if (message == WM_NCCREATE) {
        CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<OpenXRWindow*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    else {
        window = reinterpret_cast<OpenXRWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window) {
        switch (message)
        {
            case WM_DESTROY:
                window->m_isOpen = false;
                return 0;
                
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

} // namespace openxr
} // namespace visLib

#endif // _WIN32
