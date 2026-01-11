// OpenXRSession.cpp: OpenXR session management implementation

#ifdef _WIN32

#include "OpenXRSession.h"
#include <sstream>
#include <algorithm>

namespace visLib {
namespace openxr {

OpenXRSession::~OpenXRSession()
{
    shutdown();
}

bool OpenXRSession::initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
    m_device = device;

    if (!createInstance()) return false;
    if (!getSystem()) return false;
    if (!createSession(device, commandQueue)) return false;
    if (!createSwapchains()) return false;
    if (!createReferenceSpace()) return false;

    return true;
}

void OpenXRSession::shutdown()
{
    auto* loader = getOpenXRLoader();
    if (!loader) return;

    // Destroy swapchains
    for (auto& swapchain : m_swapchains) {
        if (swapchain.handle != XR_NULL_HANDLE && loader->xrDestroySwapchain) {
            loader->xrDestroySwapchain(swapchain.handle);
            swapchain.handle = XR_NULL_HANDLE;
        }
    }

    // Destroy reference space
    if (m_referenceSpace != XR_NULL_HANDLE && loader->xrDestroySpace) {
        loader->xrDestroySpace(m_referenceSpace);
        m_referenceSpace = XR_NULL_HANDLE;
    }

    // End and destroy session
    if (m_session != XR_NULL_HANDLE) {
        if (m_sessionRunning && loader->xrEndSession) {
            loader->xrEndSession(m_session);
        }
        if (loader->xrDestroySession) {
            loader->xrDestroySession(m_session);
        }
        m_session = XR_NULL_HANDLE;
    }

    // Destroy instance
    if (m_instance != XR_NULL_HANDLE && loader->xrDestroyInstance) {
        loader->xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    m_sessionRunning = false;
    m_device.Reset();
}

bool OpenXRSession::createInstance()
{
    auto* loader = getOpenXRLoader();
    if (!loader || !loader->isLoaded()) {
        m_lastError = "OpenXR loader not available";
        return false;
    }

    // Check for D3D12 extension support
    uint32_t extensionCount = 0;
    XrResult result = loader->xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    if (XR_FAILED(result)) {
        m_lastError = "Failed to enumerate OpenXR extensions";
        return false;
    }

    std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    result = loader->xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
    if (XR_FAILED(result)) {
        m_lastError = "Failed to get OpenXR extensions";
        return false;
    }

    // Check for D3D12 support
    bool hasD3D12 = false;
    for (const auto& ext : extensions) {
        if (strcmp(ext.extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
            hasD3D12 = true;
            break;
        }
    }

    if (!hasD3D12) {
        m_lastError = "OpenXR runtime does not support D3D12";
        return false;
    }

    // Create instance
    const char* enabledExtensions[] = {
        XR_KHR_D3D12_ENABLE_EXTENSION_NAME
    };

    XrInstanceCreateInfo createInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strncpy_s(createInfo.applicationInfo.applicationName, "Worm Simulation", XR_MAX_APPLICATION_NAME_SIZE);
    strncpy_s(createInfo.applicationInfo.engineName, "visLib", XR_MAX_ENGINE_NAME_SIZE);
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    result = loader->xrCreateInstance(&createInfo, &m_instance);
    if (XR_FAILED(result)) {
        m_lastError = "Failed to create OpenXR instance";
        return false;
    }

    // Resolve instance-level functions
    resolveInstanceFunctions();

    return true;
}

void OpenXRSession::resolveInstanceFunctions()
{
    auto* loader = getOpenXRLoader();
    if (!loader || m_instance == XR_NULL_HANDLE) return;

    loader->getInstanceProcAddr(m_instance, "xrDestroyInstance", &loader->xrDestroyInstance);
    loader->getInstanceProcAddr(m_instance, "xrGetInstanceProperties", &loader->xrGetInstanceProperties);
    loader->getInstanceProcAddr(m_instance, "xrGetSystem", &loader->xrGetSystem);
    loader->getInstanceProcAddr(m_instance, "xrGetSystemProperties", &loader->xrGetSystemProperties);
    loader->getInstanceProcAddr(m_instance, "xrEnumerateViewConfigurations", &loader->xrEnumerateViewConfigurations);
    loader->getInstanceProcAddr(m_instance, "xrEnumerateViewConfigurationViews", &loader->xrEnumerateViewConfigurationViews);
    loader->getInstanceProcAddr(m_instance, "xrCreateSession", &loader->xrCreateSession);
    loader->getInstanceProcAddr(m_instance, "xrDestroySession", &loader->xrDestroySession);
    loader->getInstanceProcAddr(m_instance, "xrBeginSession", &loader->xrBeginSession);
    loader->getInstanceProcAddr(m_instance, "xrEndSession", &loader->xrEndSession);
    loader->getInstanceProcAddr(m_instance, "xrRequestExitSession", &loader->xrRequestExitSession);
    loader->getInstanceProcAddr(m_instance, "xrWaitFrame", &loader->xrWaitFrame);
    loader->getInstanceProcAddr(m_instance, "xrBeginFrame", &loader->xrBeginFrame);
    loader->getInstanceProcAddr(m_instance, "xrEndFrame", &loader->xrEndFrame);
    loader->getInstanceProcAddr(m_instance, "xrLocateViews", &loader->xrLocateViews);
    loader->getInstanceProcAddr(m_instance, "xrCreateSwapchain", &loader->xrCreateSwapchain);
    loader->getInstanceProcAddr(m_instance, "xrDestroySwapchain", &loader->xrDestroySwapchain);
    loader->getInstanceProcAddr(m_instance, "xrEnumerateSwapchainImages", &loader->xrEnumerateSwapchainImages);
    loader->getInstanceProcAddr(m_instance, "xrEnumerateSwapchainFormats", &loader->xrEnumerateSwapchainFormats);
    loader->getInstanceProcAddr(m_instance, "xrAcquireSwapchainImage", &loader->xrAcquireSwapchainImage);
    loader->getInstanceProcAddr(m_instance, "xrWaitSwapchainImage", &loader->xrWaitSwapchainImage);
    loader->getInstanceProcAddr(m_instance, "xrReleaseSwapchainImage", &loader->xrReleaseSwapchainImage);
    loader->getInstanceProcAddr(m_instance, "xrCreateReferenceSpace", &loader->xrCreateReferenceSpace);
    loader->getInstanceProcAddr(m_instance, "xrDestroySpace", &loader->xrDestroySpace);
    loader->getInstanceProcAddr(m_instance, "xrPollEvent", &loader->xrPollEvent);
    loader->getInstanceProcAddr(m_instance, "xrResultToString", &loader->xrResultToString);
    loader->getInstanceProcAddr(m_instance, "xrGetD3D12GraphicsRequirementsKHR", &loader->xrGetD3D12GraphicsRequirementsKHR);
}

bool OpenXRSession::getSystem()
{
    auto* loader = getOpenXRLoader();
    if (!loader->xrGetSystem) {
        m_lastError = "xrGetSystem not available";
        return false;
    }

    XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = loader->xrGetSystem(m_instance, &systemInfo, &m_systemId);
    if (XR_FAILED(result)) {
        m_lastError = "No VR headset found. Is your headset connected?";
        return false;
    }

    // Get view configuration views (resolution per eye)
    uint32_t viewCount = 0;
    result = loader->xrEnumerateViewConfigurationViews(
        m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        0, &viewCount, nullptr);

    if (XR_FAILED(result) || viewCount != 2) {
        m_lastError = "Failed to get stereo view configuration";
        return false;
    }

    for (auto& view : m_views) {
        view.configView = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
        view.view = {XR_TYPE_VIEW};
    }

    std::vector<XrViewConfigurationView> configViews(2, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    result = loader->xrEnumerateViewConfigurationViews(
        m_instance, m_systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        2, &viewCount, configViews.data());

    if (XR_FAILED(result)) {
        m_lastError = "Failed to enumerate view configuration views";
        return false;
    }

    m_views[0].configView = configViews[0];
    m_views[1].configView = configViews[1];

    return true;
}

bool OpenXRSession::createSession(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
    auto* loader = getOpenXRLoader();

    // Check D3D12 requirements
    if (!loader->xrGetD3D12GraphicsRequirementsKHR) {
        m_lastError = "D3D12 graphics requirements function not available";
        return false;
    }

    XrGraphicsRequirementsD3D12KHR graphicsReqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    XrResult result = loader->xrGetD3D12GraphicsRequirementsKHR(m_instance, m_systemId, &graphicsReqs);
    if (XR_FAILED(result)) {
        m_lastError = "Failed to get D3D12 graphics requirements";
        return false;
    }

    // Create session with D3D12 binding
    XrGraphicsBindingD3D12KHR graphicsBinding = {XR_TYPE_GRAPHICS_BINDING_D3D12_KHR};
    graphicsBinding.device = device;
    graphicsBinding.queue = commandQueue;

    XrSessionCreateInfo sessionInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = m_systemId;

    result = loader->xrCreateSession(m_instance, &sessionInfo, &m_session);
    if (XR_FAILED(result)) {
        m_lastError = "Failed to create OpenXR session";
        return false;
    }

    return true;
}

bool OpenXRSession::createSwapchains()
{
    auto* loader = getOpenXRLoader();

    // Get supported swapchain formats
    uint32_t formatCount = 0;
    loader->xrEnumerateSwapchainFormats(m_session, 0, &formatCount, nullptr);

    std::vector<int64_t> formats(formatCount);
    loader->xrEnumerateSwapchainFormats(m_session, formatCount, &formatCount, formats.data());

    // Prefer RGBA8 SRGB
    int64_t selectedFormat = formats[0];
    for (int64_t format : formats) {
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            selectedFormat = format;
            break;
        }
        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            selectedFormat = format;
        }
    }

    // Create swapchain for each eye
    for (int eye = 0; eye < 2; ++eye) {
        auto& configView = m_views[eye].configView;
        auto& swapchain = m_swapchains[eye];

        XrSwapchainCreateInfo swapchainInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        swapchainInfo.format = selectedFormat;
        swapchainInfo.sampleCount = 1;
        swapchainInfo.width = configView.recommendedImageRectWidth;
        swapchainInfo.height = configView.recommendedImageRectHeight;
        swapchainInfo.faceCount = 1;
        swapchainInfo.arraySize = 1;
        swapchainInfo.mipCount = 1;

        XrResult result = loader->xrCreateSwapchain(m_session, &swapchainInfo, &swapchain.handle);
        if (XR_FAILED(result)) {
            m_lastError = "Failed to create swapchain for eye";
            return false;
        }

        swapchain.format = selectedFormat;
        swapchain.width = swapchainInfo.width;
        swapchain.height = swapchainInfo.height;

        // Get swapchain images
        uint32_t imageCount = 0;
        loader->xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr);

        swapchain.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        loader->xrEnumerateSwapchainImages(
            swapchain.handle, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.images.data()));
    }

    return true;
}

bool OpenXRSession::createReferenceSpace()
{
    auto* loader = getOpenXRLoader();

    XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;  // Standing/room scale
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;  // Identity

    XrResult result = loader->xrCreateReferenceSpace(m_session, &spaceInfo, &m_referenceSpace);
    if (XR_FAILED(result)) {
        m_lastError = "Failed to create reference space";
        return false;
    }

    return true;
}

bool OpenXRSession::pollEvents()
{
    auto* loader = getOpenXRLoader();
    if (!loader->xrPollEvent) return true;

    XrEventDataBuffer eventData = {XR_TYPE_EVENT_DATA_BUFFER};

    while (loader->xrPollEvent(m_instance, &eventData) == XR_SUCCESS) {
        switch (eventData.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto* stateEvent = reinterpret_cast<XrEventDataSessionStateChanged*>(&eventData);
                m_sessionState = stateEvent->state;

                switch (m_sessionState) {
                    case XR_SESSION_STATE_READY: {
                        XrSessionBeginInfo beginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        loader->xrBeginSession(m_session, &beginInfo);
                        m_sessionRunning = true;
                        break;
                    }
                    case XR_SESSION_STATE_STOPPING:
                        loader->xrEndSession(m_session);
                        m_sessionRunning = false;
                        break;
                    case XR_SESSION_STATE_EXITING:
                    case XR_SESSION_STATE_LOSS_PENDING:
                        return false;  // Session should end
                    default:
                        break;
                }
                break;
            }
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                return false;
            default:
                break;
        }

        eventData = {XR_TYPE_EVENT_DATA_BUFFER};
    }

    return true;
}

bool OpenXRSession::beginFrame()
{
    auto* loader = getOpenXRLoader();
    if (!m_sessionRunning) return false;

    m_frameState = {XR_TYPE_FRAME_STATE};
    XrResult result = loader->xrWaitFrame(m_session, nullptr, &m_frameState);
    if (XR_FAILED(result)) return false;

    result = loader->xrBeginFrame(m_session, nullptr);
    if (XR_FAILED(result)) return false;

    m_frameActive = true;

    // Skip rendering if not visible
    if (!m_frameState.shouldRender) {
        return false;
    }

    // Locate views (get eye positions/orientations for this frame)
    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    XrViewLocateInfo viewLocateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    viewLocateInfo.displayTime = m_frameState.predictedDisplayTime;
    viewLocateInfo.space = m_referenceSpace;

    std::array<XrView, 2> views = {{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    uint32_t viewCount = 2;
    result = loader->xrLocateViews(m_session, &viewLocateInfo, &viewState, 2, &viewCount, views.data());
    if (XR_FAILED(result)) return false;

    m_views[0].view = views[0];
    m_views[1].view = views[1];

    return true;
}

void OpenXRSession::endFrame()
{
    auto* loader = getOpenXRLoader();
    if (!m_frameActive) return;

    // Build projection layer for both eyes
    std::array<XrCompositionLayerProjectionView, 2> projViews;
    for (int eye = 0; eye < 2; ++eye) {
        projViews[eye] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projViews[eye].pose = m_views[eye].view.pose;
        projViews[eye].fov = m_views[eye].view.fov;
        projViews[eye].subImage.swapchain = m_swapchains[eye].handle;
        projViews[eye].subImage.imageRect.offset = {0, 0};
        projViews[eye].subImage.imageRect.extent = {
            static_cast<int32_t>(m_swapchains[eye].width),
            static_cast<int32_t>(m_swapchains[eye].height)
        };
        projViews[eye].subImage.imageArrayIndex = 0;
    }

    XrCompositionLayerProjection projLayer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projLayer.space = m_referenceSpace;
    projLayer.viewCount = 2;
    projLayer.views = projViews.data();

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<XrCompositionLayerBaseHeader*>(&projLayer)
    };

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = m_frameState.shouldRender ? 1 : 0;
    endInfo.layers = m_frameState.shouldRender ? layers : nullptr;

    loader->xrEndFrame(m_session, &endInfo);
    m_frameActive = false;
}

bool OpenXRSession::acquireSwapchainImage(int eye, uint32_t* imageIndex)
{
    auto* loader = getOpenXRLoader();
    if (eye < 0 || eye > 1) return false;

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = loader->xrAcquireSwapchainImage(m_swapchains[eye].handle, &acquireInfo, imageIndex);
    if (XR_FAILED(result)) return false;

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = loader->xrWaitSwapchainImage(m_swapchains[eye].handle, &waitInfo);
    return XR_SUCCEEDED(result);
}

void OpenXRSession::releaseSwapchainImage(int eye)
{
    auto* loader = getOpenXRLoader();
    if (eye < 0 || eye > 1) return;

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    loader->xrReleaseSwapchainImage(m_swapchains[eye].handle, &releaseInfo);
}

ID3D12Resource* OpenXRSession::getSwapchainImage(int eye, uint32_t imageIndex) const
{
    if (eye < 0 || eye > 1) return nullptr;
    if (imageIndex >= m_swapchains[eye].images.size()) return nullptr;
    return m_swapchains[eye].images[imageIndex].texture;
}

DXGI_FORMAT OpenXRSession::getSwapchainFormat() const
{
    return static_cast<DXGI_FORMAT>(m_swapchains[0].format);
}

} // namespace openxr
} // namespace visLib

#endif // _WIN32
