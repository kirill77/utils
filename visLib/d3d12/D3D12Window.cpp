#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Window.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include <stdexcept>

namespace visLib {

D3D12Window::D3D12Window(const WindowConfig& config)
{
    // Create Win32 window for input
    Win32WindowConfig winConfig;
    winConfig.title = config.title;
    winConfig.width = config.width;
    winConfig.height = config.height;
    winConfig.resizable = config.resizable;
    winConfig.fullDesktop = config.fullDesktop;
    winConfig.exclusiveFullscreen = config.exclusiveFullscreen;

    m_window = std::make_unique<Win32InputWindow>(winConfig);
    if (!m_window->isValid()) {
        throw std::runtime_error("Failed to create window");
    }

    // Set resize callback for swap chain handling
    m_window->setResizeCallback(D3D12Window::onWindowResize, this);

    // Initialize DirectX
    if (!initDirectX()) {
        throw std::runtime_error("Failed to initialize D3D12");
    }

    m_isOpen = true;
}

D3D12Window::~D3D12Window()
{
    // Release DirectX resources before window is destroyed
    m_pSwapChain.reset();
    m_device.Reset();
}

bool D3D12Window::isOpen() const
{
    return m_isOpen && !m_window->isCloseRequested();
}

void D3D12Window::close()
{
    m_isOpen = false;
}

uint32_t D3D12Window::getWidth() const
{
    return m_window->getWidth();
}

uint32_t D3D12Window::getHeight() const
{
    return m_window->getHeight();
}

void D3D12Window::processEvents()
{
    m_window->processMessages();
}

const InputState& D3D12Window::getInputState() const
{
    return m_window->getInputState();
}

void* D3D12Window::getNativeHandle() const
{
    return m_window->getHandle();
}

bool D3D12Window::initDirectX()
{
    UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
    // Enable debug layer
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
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
         ++adapterIndex) {
        DXGI_ADAPTER_DESC1 adapterDesc;
        hardwareAdapter->GetDesc1(&adapterDesc);

        // Skip software adapters
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Try to create the device
        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
            break;
        }
    }

    if (!m_device) {
        // Use WARP adapter if no hardware adapter found
        ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&hardwareAdapter)));
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

#ifdef _DEBUG
    // Break into debugger on D3D12 validation errors
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(m_device.As(&infoQueue))) {
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
    }
#endif

    // Create D3D12SwapChain
    m_pSwapChain = std::make_shared<D3D12SwapChain>(m_device.Get(), m_window->getHandle());

    // Disable Alt+Enter fullscreen toggle
    ThrowIfFailed(dxgiFactory->MakeWindowAssociation(m_window->getHandle(), DXGI_MWA_NO_ALT_ENTER));

    return true;
}

void D3D12Window::onWindowResize(uint32_t width, uint32_t height, void* userData)
{
    auto* self = static_cast<D3D12Window*>(userData);
    if (!self || width == 0 || height == 0) {
        return;
    }

    if (self->m_pSwapChain) {
        // Wait for GPU to complete all operations
        D3D12Queue* pQueue = self->m_pSwapChain->getQueue().get();
        if (pQueue) {
            pQueue->flush();
        }

        // Resize swap chain buffers
        self->m_pSwapChain->notifyWindowResized();
    }
}

// Factory function implementation
std::unique_ptr<IWindow> createWindow(const WindowConfig& config)
{
    // Try VR first if requested
    if (config.preferVR) {
        // Dynamically check for OpenXR support to avoid build dependency
        // OpenXRWindow is in a separate compilation unit
        extern std::unique_ptr<IWindow> tryCreateOpenXRWindow(const WindowConfig& config);
        auto vrWindow = tryCreateOpenXRWindow(config);
        if (vrWindow) {
            return vrWindow;
        }
        // Fall through to desktop window if VR failed
    }

    return std::make_unique<D3D12Window>(config);
}

} // namespace visLib

#endif // _WIN32
