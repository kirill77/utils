#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IRenderer.h"
#include "utils/visLib/include/Camera.h"
#include "utils/visLib/include/MeshNode.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include "utils/visLib/vulkan/internal/VulkanSwapchain.h"
#include <memory>
#include <vector>

namespace visLib {

class VulkanWindow;
class VulkanFont;
class VulkanQuery;
class VulkanText;

// VulkanRenderer - full IRenderer implementation for the Vulkan backend.
// Owns the swapchain, render pass, per-frame command buffers + sync, descriptor
// pool, mesh pipeline (MeshVS + QRCodePS), and text pipeline (TextVS + TextPS).
// Mesh objects are uploaded as VkBuffers (host-visible) and drawn with a push
// constant for the per-object world matrix and pixel shader iteration count.
class VulkanRenderer : public IRenderer
{
public:
    VulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config);
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    // IRenderer factory methods
    std::shared_ptr<IMesh>  createMesh() override;
    std::shared_ptr<IFont>  createFont(uint32_t fontSize) override;
    std::shared_ptr<IText>  createText(std::shared_ptr<IFont> font) override;

    // Scene management
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
    // Stores the config; if vsyncInterval changed, rebuilds the swapchain so
    // the new present mode takes effect (D3D12 applies vsync per-Present, but
    // Vulkan bakes the present mode into the swapchain at creation).
    void setConfig(const RendererConfig& config) override;

    std::shared_ptr<IQuery> createQuery(QueryCapability capabilities, uint32_t slotCount = 128) override;

    IWindow* getWindow() const override;

    // Depth-buffer accessors. The renderer owns a per-frame depth image
    // attached to its render pass; SL Frame Generation needs to tag it as
    // sl::kBufferTypeDepth via slSetTagForFrame, which requires the full
    // VkImage + VkImageView + format + extent + usage set (sl::Resource
    // on Vulkan demands view + the createInfo-derived metadata).
    VkImage           getDepthImage()  const { return m_depthImage; }
    VkImageView       getDepthView()   const { return m_depthView; }
    VkDeviceMemory    getDepthMemory() const { return m_depthMemory; }
    VkFormat          getDepthFormat() const { return m_depthFormat; }
    VkExtent2D        getDepthExtent() const { return m_depthExtent; }
    VkImageUsageFlags getDepthUsage()  const { return m_depthUsage; }

private:
    static constexpr uint32_t kFramesInFlight = 2;

    void initFrameResources();
    void destroyFrameResources();
    void recreateSwapchain();
    void createRenderPass();
    void createDepthResources();
    void destroyDepthResources();
    void createFramebuffers();
    void createDescriptorResources();
    void createUniformBuffers();
    void createMeshPipeline();
    void createTextPipeline();
    void destroyPipelineResources();

    void renderMeshNode(VkCommandBuffer cmd, const MeshNode& node,
                        const affine3& parentTransform,
                        box3& outBounds, bool& hasBounds);
    void updateTransformCB();

    VulkanWindow*    m_pWindow = nullptr;
    RendererConfig   m_config;
    Camera           m_camera;
    uint64_t         m_frameIndex = 0;

    VkDevice         m_device = VK_NULL_HANDLE;
    VkQueue          m_queue  = VK_NULL_HANDLE;
    std::unique_ptr<VulkanSwapchain> m_pSwapchain;

    // Render pass + per-image framebuffers
    VkRenderPass                 m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   m_framebuffers;

    // Single depth image shared across frames-in-flight. Lives between
    // createRenderPass() (which declares the depth attachment) and
    // createFramebuffers() (which attaches the depth view). DLSS-G needs
    // the full VkImage / VkImageView / format / usage metadata when the
    // resource is tagged via slSetTagForFrame on the Vulkan path.
    VkImage           m_depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory    m_depthMemory = VK_NULL_HANDLE;
    VkImageView       m_depthView   = VK_NULL_HANDLE;
    VkFormat          m_depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D        m_depthExtent = { 0, 0 };
    VkImageUsageFlags m_depthUsage  = 0;

    // Shared descriptor pool large enough for mesh + text descriptor sets.
    VkDescriptorPool             m_descriptorPool = VK_NULL_HANDLE;

    // Mesh pipeline (MeshVS + QRCodePS, drives the scene loop).
    VkDescriptorSetLayout        m_meshDSL          = VK_NULL_HANDLE;
    VkDescriptorSet              m_meshDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout             m_meshPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_meshPipeline       = VK_NULL_HANDLE;

    // Text pipeline (TextVS + TextPS).
    VkDescriptorSetLayout        m_textDSL          = VK_NULL_HANDLE;
    VkPipelineLayout             m_textPipelineLayout = VK_NULL_HANDLE;
    VkPipeline                   m_textPipeline       = VK_NULL_HANDLE;

    // Uniform buffers for the mesh pipeline (TransformCB at binding 0, PixelParams at binding 1).
    VkBuffer       m_transformBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_transformMemory = VK_NULL_HANDLE;
    void*          m_transformMapped = nullptr;

    VkBuffer       m_pixelParamsBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_pixelParamsMemory = VK_NULL_HANDLE;
    void*          m_pixelParamsMapped = nullptr;

    // Per-frame command buffers + sync primitives.
    VkCommandPool                m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore>     m_imageAvailable;
    std::vector<VkSemaphore>     m_renderFinished;
    std::vector<VkFence>         m_inFlightFences;
    uint32_t m_frameSlot         = 0;
    uint32_t m_currentImageIndex = 0;

    std::vector<std::weak_ptr<IVisObject>> m_objects;
    std::vector<std::weak_ptr<VulkanText>> m_textObjects;
};

// Peer factory: construct a VulkanRenderer for a concrete VulkanWindow.
std::shared_ptr<IRenderer> createVulkanRenderer(VulkanWindow* pWindow, const RendererConfig& config);

} // namespace visLib

#endif // _WIN32
