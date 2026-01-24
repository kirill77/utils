// OpenXRFactory.cpp: Factory functions for OpenXR window and renderer
// These are called from the main visLib factory functions when VR is requested

#ifdef _WIN32

#include "OpenXRWindow.h"
#include "OpenXRRenderer.h"
#include "OpenXRLoader.h"
#include "utils/visLib/include/IWindow.h"
#include "utils/visLib/include/IRenderer.h"
#include <memory>

namespace visLib {

// Factory function to try creating an OpenXR VR window
// Returns nullptr if VR is not available (allows fallback to desktop)
std::unique_ptr<IWindow> tryCreateOpenXRWindow(const WindowConfig& config)
{
    // Try to load OpenXR
    if (!openxr::tryInitializeOpenXR()) {
        // OpenXR DLL not available - return nullptr for fallback
        return nullptr;
    }

    // Create OpenXR window
    auto vrWindow = std::make_unique<openxr::OpenXRWindow>(config);

    // Check if VR initialization succeeded
    if (!vrWindow->isVRReady()) {
        // VR not ready (no headset, runtime error, etc.) - return nullptr for fallback
        return nullptr;
    }

    return vrWindow;
}

// Factory function to try creating an OpenXR renderer for a VR window
// Returns nullptr if the window is not an OpenXR window
std::shared_ptr<IRenderer> tryCreateOpenXRRenderer(IWindow* window, const RendererConfig& config)
{
    auto vrWindow = dynamic_cast<openxr::OpenXRWindow*>(window);
    if (!vrWindow) {
        return nullptr;
    }

    if (!vrWindow->isVRReady()) {
        return nullptr;
    }

    return std::make_shared<openxr::OpenXRRenderer>(vrWindow, config);
}

} // namespace visLib

#endif // _WIN32
