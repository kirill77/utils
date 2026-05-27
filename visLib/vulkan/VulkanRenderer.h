#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IRenderer.h"
#include "utils/visLib/include/Camera.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include "utils/visLib/vulkan/internal/VulkanSwapchain.h"
#include <memory>
#include <vector>

namespace visLib {

class VulkanWindow;

// VulkanRenderer - IRenderer implementation for the Vulkan backend.
// Step 2b scope: swapchain + clear-to-color + present, no geometry/shaders.
// Factory methods for meshes/fonts/text/queries throw; nothing in slVerdict
// currently routes scenes through this path.
class VulkanRenderer : public IRenderer
{
public:
    VulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config);
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // IRenderer factory methods (step 2b: not implemented yet)
    std::shared_ptr<IMesh> createMesh() override;
    std::shared_ptr<IFont> createFont(uint32_t fontSize) override;
    std::shared_ptr<IText> createText(std::shared_ptr<IFont> font) override;

    // Scene management — objects are tracked but ignored during render() in 2b.
    void addObject(std::weak_ptr<IVisObject> object) override;
    void removeObject(std::weak_ptr<IVisObject> object) override;
    void clearObjects() override;

    // Camera
    Camera&       getCamera() override       { return m_camera; }
    const Camera& getCamera() const override { return m_camera; }

    uint64_t getCurrentFrameIndex() const override { return m_frameIndex; }

    box3 render(IQuery* query = nullptr) override;
    void present() override;
    void flush() override;

    const RendererConfig& getConfig() const override { return m_config; }
    void setConfig(const RendererConfig& config) override { m_config = config; }

    std::shared_ptr<IQuery> createQuery(QueryCapability capabilities, uint32_t slotCount = 128) override;

    IWindow* getWindow() const override;

private:
    static constexpr uint32_t kFramesInFlight = 2;

    void initFrameResources();
    void destroyFrameResources();
    void createRenderPass();
    void createFramebuffers();
    void createPipeline();
    void destroyPipelineResources();

    VulkanWindow*    m_pWindow = nullptr;
    RendererConfig   m_config;
    Camera           m_camera;
    uint64_t         m_frameIndex = 0;

    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_queue  = VK_NULL_HANDLE;
    std::unique_ptr<VulkanSwapchain> m_pSwapchain;

    // Render pass + framebuffers — one framebuffer per swapchain image
    VkRenderPass                 m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   m_framebuffers;

    // Triangle pipeline (built from EmbeddedSpirvShaders::TriangleVS/TrianglePS)
    VkPipelineLayout             m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_pipeline       = VK_NULL_HANDLE;

    VkCommandPool                m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore>     m_imageAvailable;
    std::vector<VkSemaphore>     m_renderFinished;
    std::vector<VkFence>         m_inFlightFences;

    uint32_t m_frameSlot         = 0;          // index into per-frame resources
    uint32_t m_currentImageIndex = 0;          // valid between render() and present()

    std::vector<std::weak_ptr<IVisObject>> m_objects;
};

// Peer factory: construct a VulkanRenderer for a concrete VulkanWindow.
std::shared_ptr<IRenderer> createVulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config);

} // namespace visLib

#endif // _WIN32
