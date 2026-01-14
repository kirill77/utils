#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "utils/visLib/include/IRenderer.h"
#include "utils/visLib/include/MeshNode.h"
#include "D3D12Window.h"
#include "D3D12Mesh.h"
#include "D3D12Font.h"
#include "D3D12Text.h"
#include "utils/visLib/d3d12/internal/D3D12Queue.h"
#include "utils/visLib/d3d12/internal/D3D12SwapChain.h"
#include <DirectXMath.h>

namespace visLib {

// D3D12Renderer - D3D12 implementation of IRenderer interface
class D3D12Renderer : public IRenderer
{
public:
    D3D12Renderer(D3D12Window* pWindow, const RendererConfig& config);
    ~D3D12Renderer() override;

    // IRenderer factory methods
    std::shared_ptr<IMesh> createMesh() override;
    std::shared_ptr<IFont> createFont(uint32_t fontSize) override;
    std::shared_ptr<IText> createText(std::shared_ptr<IFont> font) override;

    // Scene management
    void addObject(std::weak_ptr<IVisObject> object) override;
    void removeObject(std::weak_ptr<IVisObject> object) override;
    void clearObjects() override;

    // Camera
    Camera& getCamera() override { return m_camera; }
    const Camera& getCamera() const override { return m_camera; }

    // Rendering
    box3 render() override;
    void present() override;
    void waitForGPU() override;

    // Configuration
    const RendererConfig& getConfig() const override { return m_config; }
    void setConfig(const RendererConfig& config) override;

    // Statistics
    RenderStats getLastFrameStats() const override { return m_lastStats; }

    // Window access
    IWindow* getWindow() const override { return m_pWindow; }

private:
    void initializeRenderResources();
    void renderMeshNode(const MeshNode& node, const affine3& parentTransform,
                        ID3D12GraphicsCommandList* pCmdList, box3& sceneBoundingBox, bool& hasValidBounds);

private:
    struct TransformBuffer {
        DirectX::XMMATRIX View;
        DirectX::XMMATRIX Projection;
    };

    D3D12Window* m_pWindow;
    RendererConfig m_config;
    Camera m_camera;
    RenderStats m_lastStats;

    std::vector<std::weak_ptr<IVisObject>> m_objects;
    std::vector<std::weak_ptr<D3D12Text>> m_textObjects;

    // DirectX resources
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_pRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pPipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pTransformBuffer;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pCBVHeap;
    
    TransformBuffer m_transformData;
    uint8_t* m_pMappedTransformBuffer = nullptr;
};

} // namespace visLib

#endif // _WIN32
