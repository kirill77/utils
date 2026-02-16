// OpenXRRenderer.h: IRenderer implementation for VR stereo rendering
// Renders the scene twice (once per eye) to OpenXR swapchains

#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IRenderer.h"
#include "utils/visLib/include/MeshNode.h"
#include "utils/visLib/d3d12/internal/D3D12Queue.h"
#include "utils/visLib/d3d12/D3D12Text.h"
#include "OpenXRWindow.h"
#include "OpenXRSession.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <vector>
#include <memory>

namespace visLib {

// Forward declarations
class IMesh;
class IFont;
class IText;
class IVisObject;

namespace openxr {

// OpenXRRenderer: Stereo VR renderer using OpenXR and D3D12
class OpenXRRenderer : public IRenderer
{
public:
    OpenXRRenderer(OpenXRWindow* pWindow, const RendererConfig& config);
    ~OpenXRRenderer() override;

    // IRenderer factory methods
    std::shared_ptr<IMesh> createMesh() override;
    std::shared_ptr<IFont> createFont(uint32_t fontSize) override;
    std::shared_ptr<IText> createText(std::shared_ptr<IFont> font) override;

    // Scene management
    void addObject(std::weak_ptr<IVisObject> object) override;
    void removeObject(std::weak_ptr<IVisObject> object) override;
    void clearObjects() override;

    // Camera (note: in VR, camera is driven by head tracking)
    Camera& getCamera() override { return *m_pCamera; }
    const Camera& getCamera() const override { return *m_pCamera; }
    std::shared_ptr<Camera> getCameraPtr() override { return m_pCamera; }

    // Frame tracking
    uint64_t getCurrentFrameIndex() const override { return m_frameIndex; }

    // Rendering
    box3 render(IQuery* query = nullptr) override;
    void present() override;
    void waitForGPU() override;

    // Configuration
    const RendererConfig& getConfig() const override { return m_config; }
    void setConfig(const RendererConfig& config) override;

    // Query factory method
    std::shared_ptr<IQuery> createQuery(QueryCapability capabilities, uint32_t slotCount = 128) override;

    // Window access
    IWindow* getWindow() const override { return m_pWindow; }

private:
    void initializeRenderResources();
    void renderEye(int eye, const DirectX::XMMATRIX& viewMatrix, const DirectX::XMMATRIX& projMatrix,
                   ID3D12Resource* renderTarget, box3& sceneBoundingBox);
    void renderMeshNode(const MeshNode& node, const affine3& parentTransform,
                        ID3D12GraphicsCommandList* pCmdList, box3& sceneBoundingBox, bool& hasValidBounds);

    // Convert OpenXR pose/fov to DirectX matrices
    DirectX::XMMATRIX xrPoseToViewMatrix(const XrPosef& pose);
    DirectX::XMMATRIX xrFovToProjectionMatrix(const XrFovf& fov, float nearZ, float farZ);

private:
    struct TransformBuffer {
        DirectX::XMMATRIX View;
        DirectX::XMMATRIX Projection;
    };

    OpenXRWindow* m_pWindow;
    OpenXRSession* m_session;
    RendererConfig m_config;
    std::shared_ptr<Camera> m_pCamera;
    uint64_t m_frameIndex = 0;

    std::vector<std::weak_ptr<IVisObject>> m_objects;

    // D3D12 resources
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pTransformBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pCBVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pRTVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pDSVHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pDepthBuffer;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_pCommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_pCommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_pFence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;

    TransformBuffer m_transformData;
    uint8_t* m_pMappedTransformBuffer = nullptr;

    bool m_frameStarted = false;

    // Text rendering support
    std::shared_ptr<D3D12Queue> m_pGPUQueue;
    std::vector<std::weak_ptr<D3D12Text>> m_textObjects;
};

} // namespace openxr
} // namespace visLib

#endif // _WIN32
