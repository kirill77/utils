// OpenXRSession.h: Manages OpenXR instance, session, and swapchains
// Encapsulates all OpenXR state needed for VR rendering

#pragma once

#ifdef _WIN32

#include "OpenXRLoader.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <memory>

namespace visLib {
namespace openxr {

// Per-eye swapchain data
struct EyeSwapchain
{
    XrSwapchain handle = XR_NULL_HANDLE;
    int64_t format = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<XrSwapchainImageD3D12KHR> images;
};

// View configuration for stereo rendering
struct StereoView
{
    XrViewConfigurationView configView;
    XrView view;  // Updated each frame with pose/fov
};

// OpenXRSession: Manages XR instance, session, space, and swapchains
class OpenXRSession
{
public:
    OpenXRSession() = default;
    ~OpenXRSession();

    // Non-copyable
    OpenXRSession(const OpenXRSession&) = delete;
    OpenXRSession& operator=(const OpenXRSession&) = delete;

    // Initialize OpenXR with a D3D12 device
    // Returns false if initialization fails (VR not available)
    bool initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue);

    // Shutdown and release all OpenXR resources
    void shutdown();

    // Check if session is active and ready for rendering
    bool isSessionRunning() const { return m_sessionRunning; }

    // Poll and handle OpenXR events
    // Returns false if session should end
    bool pollEvents();

    // Begin a new frame - call before rendering
    // Returns false if frame should be skipped (not focused, etc.)
    bool beginFrame();

    // End the frame - call after rendering both eyes
    void endFrame();

    // Get the swapchain image index to render to for the given eye
    // eye: 0 = left, 1 = right
    bool acquireSwapchainImage(int eye, uint32_t* imageIndex);
    void releaseSwapchainImage(int eye);

    // Accessors
    uint32_t getRenderWidth() const { return m_swapchains[0].width; }
    uint32_t getRenderHeight() const { return m_swapchains[0].height; }

    ID3D12Resource* getSwapchainImage(int eye, uint32_t imageIndex) const;
    DXGI_FORMAT getSwapchainFormat() const;

    // Get view/projection matrices for the current frame
    // Must be called after beginFrame()
    const XrView& getView(int eye) const { return m_views[eye].view; }

    // Get the reference space for head tracking
    XrSpace getReferenceSpace() const { return m_referenceSpace; }

    // Get predicted display time for the current frame
    XrTime getPredictedDisplayTime() const { return m_frameState.predictedDisplayTime; }

    // Error handling
    const std::string& getLastError() const { return m_lastError; }

private:
    bool createInstance();
    bool getSystem();
    bool createSession(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    bool createSwapchains();
    bool createReferenceSpace();
    void resolveInstanceFunctions();

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_referenceSpace = XR_NULL_HANDLE;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;

    bool m_sessionRunning = false;
    bool m_frameActive = false;

    XrFrameState m_frameState = {};

    std::array<EyeSwapchain, 2> m_swapchains;  // Left and right eye
    std::array<StereoView, 2> m_views;

    std::string m_lastError;

    // Cached device reference for resource access
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
};

} // namespace openxr
} // namespace visLib

#endif // _WIN32
