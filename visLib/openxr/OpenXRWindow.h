// OpenXRWindow.h: IWindow implementation for VR headsets
// Creates a desktop companion window for input and renders to VR headset via OpenXR

#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/common/Win32InputWindow.h"
#include "OpenXRSession.h"
#include <memory>

namespace visLib {
namespace openxr {

// OpenXRWindow: IWindow implementation for VR
// Creates a companion desktop window for keyboard/mouse input
// Renders to VR headset via OpenXR
class OpenXRWindow : public IWindow
{
public:
    OpenXRWindow(const WindowConfig& config);
    ~OpenXRWindow() override;

    // IWindow interface
    bool isOpen() const override;
    void close() override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;
    void processEvents() override;
    const InputState& getInputState() const override;
    void* getNativeHandle() const override;

    // Check if VR initialization succeeded
    bool isVRReady() const { return m_vrReady; }

    // Get the OpenXR session for rendering
    OpenXRSession* getSession() { return m_session.get(); }

    // Get the D3D12 device (created internally for VR)
    ID3D12Device* getDevice() const { return m_device.Get(); }
    ID3D12CommandQueue* getCommandQueue() const { return m_commandQueue.Get(); }

    // Error handling
    const std::string& getLastError() const { return m_lastError; }

private:
    bool initializeD3D12();
    bool initializeOpenXR();

    bool m_isOpen = false;
    bool m_vrReady = false;
    std::string m_lastError;

    // Companion desktop window for input
    std::unique_ptr<Win32InputWindow> m_companionWindow;

    // D3D12 resources (owned by this window for VR)
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;

    // OpenXR session
    std::unique_ptr<OpenXRSession> m_session;
};

} // namespace openxr
} // namespace visLib

#endif // _WIN32
