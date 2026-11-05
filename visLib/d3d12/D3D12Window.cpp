#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Window.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include <atomic>
#include <stdexcept>

// DXGI_DEBUG_DXGI
#pragma comment(lib, "dxguid.lib")

namespace visLib {

namespace {

// EnableDebugLayer() is process-wide, and enabling it once a device exists
// removes that device. So it is enabled at most once, and only before the
// first device this library creates.
std::atomic<bool> s_debugLayerEnabled{ false };
std::atomic<bool> s_deviceCreated{ false };

// D3D12_MESSAGE_SEVERITY and DXGI_INFO_QUEUE_MESSAGE_SEVERITY share values:
// 0 corruption, 1 error, 2 warning, 3 info, 4 message.
void reportValidationMessage(IValidationSink& sink, int severity, const std::string& id, const char* pText)
{
    if (severity > D3D12_MESSAGE_SEVERITY_WARNING) return;
    sink.onMessage(severity == D3D12_MESSAGE_SEVERITY_WARNING ? ValidationSeverity::Warning
                                                              : ValidationSeverity::Error,
                   id.c_str(), pText ? pText : "");
}

// Runs on the thread that raised the message. pContext is the window's sink;
// the window unregisters the callback before it drops the sink.
void __stdcall onD3D12Message(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                              D3D12_MESSAGE_ID id, LPCSTR pDescription, void* pContext)
{
    reportValidationMessage(*static_cast<IValidationSink*>(pContext), severity,
                            "D3D12-" + std::to_string(id), pDescription);
}

// Reports stored messages [read, end) and advances read.
void readD3D12Messages(ID3D12InfoQueue& queue, UINT64& read, UINT64 end, IValidationSink& sink)
{
    std::vector<char> buf;
    for (; read < end; ++read) {
        SIZE_T size = 0;
        if (FAILED(queue.GetMessage(read, nullptr, &size)) || size == 0) continue;
        buf.resize(size);
        auto* pMsg = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (FAILED(queue.GetMessage(read, pMsg, &size))) continue;
        reportValidationMessage(sink, pMsg->Severity, "D3D12-" + std::to_string(pMsg->ID), pMsg->pDescription);
    }
}

} // namespace

D3D12Window::D3D12Window(const WindowConfig& config, const D3D12WindowConfig& d3dConfig)
    : m_d3d12Overrides(d3dConfig.creationOverrides)
    , m_latencyWaitable(d3dConfig.latencyWaitable)
    , m_enableDebugLayer(d3dConfig.enableDebugLayer)
    , m_pValidationSink(d3dConfig.pValidationSink)
{
    // Create Win32 window for input
    Win32WindowConfig winConfig;
    winConfig.title = config.title;
    winConfig.width = config.width;
    winConfig.height = config.height;
    winConfig.resizable = config.resizable;
    winConfig.borderless = config.borderless;
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
    drainValidationMessages();
    if (m_d3dInfoQueue1) {
        m_d3dInfoQueue1->UnregisterMessageCallback(m_d3dCallbackCookie);
        m_d3dInfoQueue1.Reset();
    }
    m_d3dInfoQueue.Reset();
    m_device.Reset();
    m_dxgiFactory.Reset();
    // The DXGI queue holds no device reference, so it also sees the release
    // of the device and factory. Then it is emptied and gets its default cap
    // back, so the process-wide queue does not grow from window to window.
    drainValidationMessages();
    if (m_dxgiInfoQueue) {
        m_dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_DXGI);
        m_dxgiInfoQueue->SetMessageCountLimit(DXGI_DEBUG_DXGI, DXGI_INFO_QUEUE_DEFAULT_MESSAGE_COUNT_LIMIT);
        m_dxgiInfoQueue.Reset();
    }
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

void D3D12Window::resize(uint32_t width, uint32_t height)
{
    m_window->resize(width, height);
}

void D3D12Window::processEvents()
{
    m_window->processMessages();
    drainValidationMessages();
}

void D3D12Window::drainValidationMessages()
{
    if (!m_pValidationSink) return;
    if (m_d3dInfoQueue) {
        readD3D12Messages(*m_d3dInfoQueue.Get(), m_d3dRead,
                          m_d3dInfoQueue->GetNumStoredMessagesAllowedByRetrievalFilter(), *m_pValidationSink);
    }
    if (m_dxgiInfoQueue) {
        std::vector<char> buf;
        const UINT64 count = m_dxgiInfoQueue->GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_DXGI);
        for (; m_dxgiRead < count; ++m_dxgiRead) {
            SIZE_T size = 0;
            if (FAILED(m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_DXGI, m_dxgiRead, nullptr, &size)) || size == 0) continue;
            buf.resize(size);
            auto* pMsg = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(buf.data());
            if (FAILED(m_dxgiInfoQueue->GetMessage(DXGI_DEBUG_DXGI, m_dxgiRead, pMsg, &size))) continue;
            reportValidationMessage(*m_pValidationSink, pMsg->Severity,
                                    "DXGI-" + std::to_string(pMsg->ID), pMsg->pDescription);
        }
    }
}

const InputState& D3D12Window::getInputState() const
{
    return m_window->getInputState();
}

bool D3D12Window::wasFocusLost() const
{
    return m_window->wasFocusLost();
}

void D3D12Window::resetFocusLost()
{
    m_window->resetFocusLost();
}

void* D3D12Window::getNativeHandle() const
{
    return m_window->getHandle();
}

bool D3D12Window::initDirectX()
{
    UINT dxgiFactoryFlags = 0;

    // The debug layer must be on before the device is created. It comes from
    // the Windows "Graphics Tools" optional feature (d3d12SDKLayers.dll).
    bool debugLayerOn = false;
    Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
    if (m_enableDebugLayer && !s_debugLayerEnabled && s_deviceCreated) {
        // Enabling it now would remove the devices that already exist.
        OutputDebugStringA("[visLib] D3D12 debug layer requested after a device was created without it; not enabled.\n");
        if (m_pValidationSink) m_pValidationSink->onLayerStatus(ValidationLayerStatus::Failed, "");
    } else if (m_enableDebugLayer) {
        if (s_debugLayerEnabled || SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            if (!s_debugLayerEnabled) {
                debugController->EnableDebugLayer();
                s_debugLayerEnabled = true;
            }
            debugLayerOn = true;
            // Also shows dxgidebug.dll is there, which the factory debug flag needs.
            Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)))) {
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
                if (m_pValidationSink) {
                    dxgiInfoQueue->SetMessageCountLimit(DXGI_DEBUG_DXGI, UINT64(-1));
                    // The DXGI queue is per process: an earlier window already
                    // reported what is stored.
                    m_dxgiRead = dxgiInfoQueue->GetNumStoredMessagesAllowedByRetrievalFilters(DXGI_DEBUG_DXGI);
                    m_dxgiInfoQueue = dxgiInfoQueue;
                }
            }
        } else if (m_pValidationSink) {
            m_pValidationSink->onLayerStatus(ValidationLayerStatus::Missing, "");
        }
    }

    // Create DXGI factory -- use interposer override if provided
    auto fnCreateFactory = m_d3d12Overrides.pfnCreateDXGIFactory2
        ? m_d3d12Overrides.pfnCreateDXGIFactory2
        : ::CreateDXGIFactory2;
    ThrowIfFailed(fnCreateFactory(dxgiFactoryFlags, IID_PPV_ARGS(&m_dxgiFactory)));

    // Select function for device creation -- use interposer override if provided
    auto fnCreateDevice = m_d3d12Overrides.pfnD3D12CreateDevice
        ? m_d3d12Overrides.pfnD3D12CreateDevice
        : ::D3D12CreateDevice;

    // Create D3D12 device — prefer high-performance (discrete) GPU over integrated
    Microsoft::WRL::ComPtr<IDXGIAdapter1> hardwareAdapter;
    for (UINT adapterIndex = 0;
         DXGI_ERROR_NOT_FOUND != m_dxgiFactory->EnumAdapterByGpuPreference(
             adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&hardwareAdapter));
         ++adapterIndex) {
        DXGI_ADAPTER_DESC1 adapterDesc;
        hardwareAdapter->GetDesc1(&adapterDesc);

        // Skip software adapters
        if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        // Try to create the device
        if (SUCCEEDED(fnCreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)))) {
            break;
        }
    }

    if (!m_device) {
        // Use WARP adapter if no hardware adapter found
        ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&hardwareAdapter)));
        ThrowIfFailed(fnCreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)));
    }

    s_deviceCreated = true;

    // Without a sink the debug layer's own debugger output is all there is.
    if (debugLayerOn && m_pValidationSink) {
        bool ok = false;
        // Preferred: a callback gets each message on the thread that raised it.
        // Messages stored before it was registered are read once. Nothing can
        // raise one in between: this thread has just created the device and
        // has not handed it to anyone yet.
        if (SUCCEEDED(m_device.As(&m_d3dInfoQueue1))) {
            const UINT64 stored = m_d3dInfoQueue1->GetNumStoredMessagesAllowedByRetrievalFilter();
            if (SUCCEEDED(m_d3dInfoQueue1->RegisterMessageCallback(&onD3D12Message, D3D12_MESSAGE_CALLBACK_FLAG_NONE,
                                                                   m_pValidationSink.get(), &m_d3dCallbackCookie))) {
                UINT64 read = 0;
                readD3D12Messages(*m_d3dInfoQueue1.Get(), read, stored, *m_pValidationSink);
                ok = true;
            } else {
                m_d3dInfoQueue1.Reset();
            }
        }
        // Older debug layers: poll every processEvents(). Nothing is cleared,
        // so the queue must not drop messages.
        if (!ok && SUCCEEDED(m_device.As(&m_d3dInfoQueue))) {
            m_d3dInfoQueue->SetMessageCountLimit(UINT64(-1));
            ok = true;
        }
        m_pValidationSink->onLayerStatus(ok ? ValidationLayerStatus::Active : ValidationLayerStatus::Failed, "");
    }

    return true;
}

D3D12SwapChain* D3D12Window::getOrCreateSwapChain()
{
    if (!m_pSwapChain) {
        m_pSwapChain = std::make_shared<D3D12SwapChain>(m_device.Get(), m_window->getHandle(), m_dxgiFactory.Get(),
                                                       m_latencyWaitable);
        ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(m_window->getHandle(), DXGI_MWA_NO_ALT_ENTER));
    }
    return m_pSwapChain.get();
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

// D3D12-specific factory: callers that know they want D3D12 (and may need
// interposer hooks) pass a D3D12WindowConfig directly. Returns the concrete
// type so peer renderer factories can be type-checked at the seam.
std::unique_ptr<D3D12Window> createD3D12Window(const WindowConfig& config,
                                                const D3D12WindowConfig& d3dConfig)
{
    return std::make_unique<D3D12Window>(config, d3dConfig);
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
