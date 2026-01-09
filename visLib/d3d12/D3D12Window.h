#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IWindow.h"
#include "D3D12InputState.h"
#include "utils/visLib/d3d12/internal/SwapChain.h"
#include <memory>

namespace visLib {
namespace d3d12 {

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
    void processEvents() override;
    const InputState& getInputState() const override;
    void* getNativeHandle() const override;

    // D3D12-specific accessors (used by D3D12Renderer)
    ID3D12Device* getDevice() const { return m_device.Get(); }
    SwapChain* getSwapChain() const { return m_pSwapChain.get(); }
    
    // Window resize handler
    void onWindowResize(uint32_t width, uint32_t height);
    
    // Input handler for Win32 messages
    void handleInput(UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool createWindowAndDevice(const WindowConfig& config);
    bool initDirectX();
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_isOpen = false;
    
    std::unique_ptr<D3D12InputState> m_inputState;
    
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    std::shared_ptr<SwapChain> m_pSwapChain;
};

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
