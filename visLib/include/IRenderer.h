#pragma once

#include "Types.h"
#include "Camera.h"
#include "IQuery.h"
#include <memory>
#include <cstdint>
#include <string>

namespace visLib {

// Forward declarations
class IMesh;
class IFont;
class IText;
class IWindow;
class IVisObject;

// Renderer configuration
struct RendererConfig {
    bool enableDebugLayer = false;      // Enable graphics API debug validation
    bool wireframeMode = true;          // Render in wireframe (default for scientific vis)
    float4 clearColor = { 0.0f, 0.2f, 0.4f, 1.0f };  // Background color
    
    // Pixel shader name (matches filename without extension)
    // e.g., "PixelShader", "HeavyPixelShader"
    std::string pixelShader = "PixelShader";
    uint32_t pixelShaderIterations = 100;  // Iteration count (used by HeavyPixelShader)
};

// Abstract renderer interface
// Main facade for the visualization library
// Derives from enable_shared_from_this to allow safe shared_ptr creation from within methods
class IRenderer : public std::enable_shared_from_this<IRenderer> {
public:
    virtual ~IRenderer() = default;

    // ===== Factory Methods =====
    // Create GPU resources managed by this renderer

    // Create an empty mesh (upload geometry later with setGeometry)
    virtual std::shared_ptr<IMesh> createMesh() = 0;

    // Create a font for text rendering
    virtual std::shared_ptr<IFont> createFont(uint32_t fontSize) = 0;

    // Create a text block using the specified font
    virtual std::shared_ptr<IText> createText(std::shared_ptr<IFont> font) = 0;

    // ===== Scene Management =====

    // Add an object to the scene (weak reference - object must be kept alive externally)
    virtual void addObject(std::weak_ptr<IVisObject> object) = 0;

    // Remove an object from the scene
    virtual void removeObject(std::weak_ptr<IVisObject> object) = 0;

    // Remove all objects from the scene
    virtual void clearObjects() = 0;

    // ===== Camera =====

    // Get the scene camera (mutable reference for direct manipulation)
    virtual Camera& getCamera() = 0;
    virtual const Camera& getCamera() const = 0;
    
    // Get shared pointer to camera (for shared ownership)
    virtual std::shared_ptr<Camera> getCameraPtr() = 0;

    // ===== Frame Tracking =====

    // Get the current frame index (increments after each present())
    virtual uint64_t getCurrentFrameIndex() const = 0;

    // ===== Rendering =====

    // Render the scene, optionally measuring with provided query.
    // Query will have its internal begin/end called automatically.
    // Returns the bounding box of all rendered objects.
    virtual box3 render(IQuery* query = nullptr) = 0;

    // Present the rendered frame to the window
    virtual void present() = 0;

    // Wait for GPU to finish all pending work
    virtual void waitForGPU() = 0;

    // ===== Configuration =====

    // Get/set renderer configuration
    virtual const RendererConfig& getConfig() const = 0;
    virtual void setConfig(const RendererConfig& config) = 0;

    // ===== Query Factory Methods =====

    // Create a query for GPU measurements
    // capabilities: what data to capture (Timestamps, PipelineStats, or both)
    // slotCount: number of concurrent measurements supported (ring buffer size)
    virtual std::shared_ptr<IQuery> createQuery(QueryCapability capabilities, uint32_t slotCount = 128) = 0;

    // ===== Window Access =====

    // Get the window this renderer is attached to
    virtual IWindow* getWindow() const = 0;
};

// Factory function declaration
// Implementation provided by the backend (visLib_d3d12, visLib_vulkan, etc.)
// window: The window to render to (must remain valid for renderer lifetime)
// config: Renderer configuration options
// Returns shared_ptr to enable shared ownership and enable_shared_from_this
std::shared_ptr<IRenderer> createRenderer(IWindow* window, const RendererConfig& config = {});

} // namespace visLib
