#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/d3d12/D3D12WindowConfig.h"
#include "utils/visLib/common/Win32InputWindow.h"
#include "utils/visLib/d3d12/internal/D3D12SwapChain.h"
#include <dxgidebug.h>
#include <memory>

namespace visLib {

// D3D12Window - Win32/D3D12 implementation of IWindow interface
class D3D12Window : public IWindow
{
public:
    D3D12Window(const WindowConfig& config, const D3D12WindowConfig& d3dConfig = {});
    ~D3D12Window() override;

    // IWindow interface implementation
    bool isOpen() const override;
    void close() override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;
    void resize(uint32_t width, uint32_t height) override;
    void processEvents() override;
    const InputState& getInputState() const override;
    bool wasFocusLost() const override;
    void resetFocusLost() override;
    void* getNativeHandle() const override;

    // D3D12-specific accessors (used by D3D12Renderer)
    ID3D12Device* getDevice() const { return m_device.Get(); }

    // Lazily creates the swap chain on first call.
    // This allows middleware (e.g. Streamline) to register the device
    // after D3D12Window construction but before any DXGI swap chain calls.
    D3D12SwapChain* getOrCreateSwapChain();

private:
    bool initDirectX();
    static void onWindowResize(uint32_t width, uint32_t height, void* userData);
    // Debug layer with a sink: passes the info-queue messages not read yet to
    // the sink (all DXGI ones; D3D12 ones only without the message callback).
    void drainValidationMessages();

private:
    std::unique_ptr<Win32InputWindow> m_window;
    bool m_isOpen = false;

    D3D12CreationOverrides m_d3d12Overrides;
    bool m_latencyWaitable = false;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgiFactory;
    std::shared_ptr<D3D12SwapChain> m_pSwapChain;

    bool m_enableDebugLayer = false;
    std::shared_ptr<IValidationSink> m_pValidationSink;
    // Null unless the debug layer is on and there is a sink. The D3D12 queues
    // hold a device reference, so they are released before the device.
    // Messages are read by index and never cleared: another thread (e.g. a
    // present thread) can add one between a read and a clear.
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_d3dInfoQueue1;  // message callback
    DWORD m_d3dCallbackCookie = 0;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> m_d3dInfoQueue;    // polled when no callback
    UINT64 m_d3dRead = 0;
    Microsoft::WRL::ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;    // always polled
    UINT64 m_dxgiRead = 0;
};

} // namespace visLib

#endif // _WIN32
