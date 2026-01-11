// OpenXRWindow.cpp: IWindow implementation for VR headsets

#ifdef _WIN32

#include "OpenXRWindow.h"
#include "OpenXRLoader.h"
#include <dxgi1_6.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace visLib {
namespace openxr {

OpenXRWindow::OpenXRWindow(const WindowConfig& config)
    : m_inputState(std::make_unique<d3d12::D3D12InputState>())
{
    (void)config;  // VR ignores window config (no desktop window)

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
    if (m_session) {
        if (!m_session->pollEvents()) {
            m_isOpen = false;
        }
    }

    // Input state is managed by D3D12InputState
    if (m_inputState) {
        m_inputState->beginFrame();
        m_inputState->endFrame();
    }
}

const InputState& OpenXRWindow::getInputState() const
{
    return *m_inputState;
}

void* OpenXRWindow::getNativeHandle() const
{
    // No native window handle for VR
    return nullptr;
}

} // namespace openxr
} // namespace visLib

#endif // _WIN32
