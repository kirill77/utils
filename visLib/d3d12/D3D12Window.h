#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/common/Win32InputWindow.h"
#include "utils/visLib/d3d12/internal/D3D12SwapChain.h"
#include <memory>

namespace visLib {

// D3D12Window - Win32/D3D12 implementation of IWindow interface
class D3D12Window : public IWindow
{
public:
    D3D12Window(const WindowConfig& config);
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

private:
    std::unique_ptr<Win32InputWindow> m_window;
    bool m_isOpen = false;

    D3D12CreationOverrides m_d3d12Overrides;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_dxgiFactory;
    std::shared_ptr<D3D12SwapChain> m_pSwapChain;
};

} // namespace visLib

#endif // _WIN32
