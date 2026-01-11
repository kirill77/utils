#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Window.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include <stdexcept>

namespace visLib {
namespace d3d12 {

D3D12Window::D3D12Window(const WindowConfig& config)
    : m_inputState(std::make_unique<D3D12InputState>())
{
    if (!createWindowAndDevice(config))
    {
        throw std::runtime_error("Failed to create window and D3D12 device");
    }
    m_isOpen = true;
}

D3D12Window::~D3D12Window()
{
    // Release DirectX resources
    m_pSwapChain.reset();
    m_device.Reset();
    
    // Destroy window
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool D3D12Window::isOpen() const
{
    return m_isOpen;
}

void D3D12Window::close()
{
    m_isOpen = false;
}

uint32_t D3D12Window::getWidth() const
{
    return m_width;
}

uint32_t D3D12Window::getHeight() const
{
    return m_height;
}

void D3D12Window::processEvents()
{
    // Begin new input frame
    m_inputState->beginFrame();
    
    // Process all pending Windows messages
    MSG msg = {};
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // End input frame
    m_inputState->endFrame();
}

const InputState& D3D12Window::getInputState() const
{
    return *m_inputState;
}

void* D3D12Window::getNativeHandle() const
{
    return m_hwnd;
}

bool D3D12Window::createWindowAndDevice(const WindowConfig& config)
{
    // Disable Windows DPI scaling
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandle(L"user32.dll");
    if (user32)
    {
        SetProcessDpiAwarenessContextProc setProcessDpiAwarenessContext = 
            (SetProcessDpiAwarenessContextProc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setProcessDpiAwarenessContext)
        {
            setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        else
        {
            SetProcessDPIAware();
        }
    }

    // Determine window size
    if (config.fullscreen)
    {
        m_width = GetSystemMetrics(SM_CXSCREEN);
        m_height = GetSystemMetrics(SM_CYSCREEN);
    }
    else
    {
        m_width = config.width;
        m_height = config.height;
    }

    // Register window class
    WNDCLASSEX windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = D3D12Window::WindowProc;
    windowClass.hInstance = GetModuleHandle(NULL);
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"visLibWindowClass";
    RegisterClassEx(&windowClass);

    // Determine window style
    DWORD windowStyle = config.fullscreen ? WS_POPUP : (WS_OVERLAPPEDWINDOW);
    if (!config.resizable && !config.fullscreen)
    {
        windowStyle &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    // Calculate window rect for client area size
    RECT windowRect = { 0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height) };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    // Convert title to wide string
    std::wstring wideTitle(config.title.begin(), config.title.end());

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

    if (!m_hwnd)
    {
        return false;
    }

    // Show the window
    ShowWindow(m_hwnd, SW_SHOW);

    // Initialize DirectX
    if (!initDirectX())
    {
        return false;
    }

    return true;
}

bool D3D12Window::initDirectX()
{
    UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
    // Enable debug layer
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // Create DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory6> dxgiFactory;
    ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

    // Create D3D12 device
    Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
    for (UINT adapterIndex = 0; 
         DXGI_ERROR_NOT_FOUND != dxgiFactory->EnumAdapters1(adapterIndex, &hardwareAdapter); 
         ++adapterIndex)
    {
        DXGI_ADAPTER_DESC1 adapterDesc;
        hardwareAdapter->GetDesc1(&adapterDesc);

        // Skip software adapters
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            continue;
        }

        // Try to create the device
        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
        {
            break;
        }
    }

    if (!m_device)
    {
        // Use WARP adapter if no hardware adapter found
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&hardwareAdapter)));
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    // Create SwapChain
    m_pSwapChain = std::make_shared<SwapChain>(m_device.Get(), m_hwnd);

    // Disable Alt+Enter fullscreen toggle
    ThrowIfFailed(dxgiFactory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

    return true;
}

void D3D12Window::handleInput(UINT message, WPARAM wParam, LPARAM lParam)
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

void D3D12Window::onWindowResize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    if (m_pSwapChain)
    {
        // Wait for GPU to complete all operations
        GPUQueue* gpuQueue = m_pSwapChain->getGPUQueue().get();
        if (gpuQueue)
        {
            gpuQueue->flush();
        }

        // Resize swap chain buffers
        m_pSwapChain->notifyWindowResized();
    }

    m_width = width;
    m_height = height;
}

LRESULT CALLBACK D3D12Window::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    D3D12Window* window = nullptr;
    
    if (message == WM_NCCREATE)
    {
        CREATESTRUCT* createStruct = reinterpret_cast<CREATESTRUCT*>(lParam);
        window = reinterpret_cast<D3D12Window*>(createStruct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    else
    {
        window = reinterpret_cast<D3D12Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (window)
    {
        switch (message)
        {
            case WM_DESTROY:
                window->m_isOpen = false;
                return 0;
                
            case WM_SIZE:
                window->onWindowResize(LOWORD(lParam), HIWORD(lParam));
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

} // namespace d3d12

// Factory function implementation
std::unique_ptr<IWindow> createWindow(const WindowConfig& config)
{
    // Try VR first if requested
    if (config.preferVR)
    {
        // Dynamically check for OpenXR support to avoid build dependency
        // OpenXRWindow is in a separate compilation unit
        extern std::unique_ptr<IWindow> tryCreateOpenXRWindow(const WindowConfig& config);
        auto vrWindow = tryCreateOpenXRWindow(config);
        if (vrWindow)
        {
            return vrWindow;
        }
        // Fall through to desktop window if VR failed
    }

    return std::make_unique<d3d12::D3D12Window>(config);
}

} // namespace visLib

#endif // _WIN32
