#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include "D3D12Renderer.h"
#include "D3D12Query.h"
#include "utils/visLib/include/IVisObject.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include "utils/visLib/d3d12/internal/CD3DX12.h"
#include "utils/visLib/d3d12/internal/D3D12ShaderHelper.h"
#include <DirectXMath.h>

namespace visLib {

D3D12Renderer::D3D12Renderer(D3D12Window* pWindow, const RendererConfig& config)
    : m_pWindow(pWindow)
    , m_config(config)
{
    // Camera uses defaults from Camera constructor
    // All camera modifications happen in shared application code
    initializeRenderResources();
}

D3D12Renderer::~D3D12Renderer()
{
    // Wait for GPU before cleanup
    if (m_pWindow && m_pWindow->getSwapChain())
    {
        auto pQueue = m_pWindow->getSwapChain()->getQueue();
        if (pQueue)
        {
            pQueue->flush();
        }
    }
}

std::shared_ptr<IMesh> D3D12Renderer::createMesh()
{
    return std::make_shared<D3D12Mesh>(m_pWindow->getDevice());
}

std::shared_ptr<IFont> D3D12Renderer::createFont(uint32_t fontSize)
{
    auto pQueue = m_pWindow->getSwapChain()->getQueue();
    // Desktop swap chain uses UNORM format
    return std::make_shared<D3D12Font>(fontSize, pQueue.get(), DXGI_FORMAT_R8G8B8A8_UNORM);
}

std::shared_ptr<IText> D3D12Renderer::createText(std::shared_ptr<IFont> font)
{
    auto d3d12Font = std::dynamic_pointer_cast<D3D12Font>(font);
    if (!d3d12Font)
    {
        throw std::runtime_error("Font must be created by the same renderer");
    }
    auto text = std::make_shared<D3D12Text>(d3d12Font);
    m_textObjects.push_back(text);
    return text;
}

void D3D12Renderer::addObject(std::weak_ptr<IVisObject> object)
{
    m_objects.push_back(object);
}

void D3D12Renderer::removeObject(std::weak_ptr<IVisObject> object)
{
    auto objPtr = object.lock();
    if (!objPtr) return;

    m_objects.erase(
        std::remove_if(m_objects.begin(), m_objects.end(),
            [&objPtr](const std::weak_ptr<IVisObject>& wp) {
                auto p = wp.lock();
                return !p || p == objPtr;
            }),
        m_objects.end());
}

void D3D12Renderer::clearObjects()
{
    m_objects.clear();
}

void D3D12Renderer::setConfig(const RendererConfig& config)
{
    m_config = config;
    // TODO: Recreate pipeline state if wireframe mode changed
}

void D3D12Renderer::initializeRenderResources()
{
    auto pDevice = m_pWindow->getDevice();

    // Create shared root signature
    CD3DX12_DESCRIPTOR_RANGE ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0);  // b0-b1
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);  // t0-t3

    CD3DX12_ROOT_PARAMETER rootParameters[3];
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[2].InitAsConstants(16, 2, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    // Static sampler
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MipLODBias = 0;
    sampler.MaxAnisotropy = 0;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MinLOD = 0.0f;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.RegisterSpace = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(_countof(rootParameters), rootParameters, 1, &sampler,
                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    m_pRootSignature = CreateRootSignature(pDevice, rootSignatureDesc);

    // Load shaders
    D3D12ShaderHelper& shaderHelper = D3D12ShaderHelper::getInstance();

#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    Microsoft::WRL::ComPtr<ID3DBlob> vertexShader;
    Microsoft::WRL::ComPtr<ID3DBlob> pixelShader;

    // Convert pixel shader name to wide string
    std::wstring pixelShaderName(m_config.pixelShader.begin(), m_config.pixelShader.end());

    // Try precompiled shaders
    std::wstring shaderPath = L"Shaders/";
    vertexShader = shaderHelper.loadCompiledShader(shaderPath + L"VertexShader.cso");
    pixelShader = shaderHelper.loadCompiledShader(shaderPath + pixelShaderName + L".cso");

    // Fallback: compile at runtime
    if (!vertexShader || !pixelShader)
    {
        shaderPath = L"utils/visLib/d3d12/Shaders/";
        if (!vertexShader)
            vertexShader = shaderHelper.loadShader(shaderPath + L"VertexShader.hlsl", "main", "vs_5_0", compileFlags);
        if (!pixelShader)
            pixelShader = shaderHelper.loadShader(shaderPath + pixelShaderName + L".hlsl", "main", "ps_5_0", compileFlags);
    }

    // Vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pRootSignature.Get();
    psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
    psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();

    // Rasterizer state
    psoDesc.RasterizerState.FillMode = m_config.wireframeMode ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = FALSE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Blend state
    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;

    // Depth stencil state
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    depthStencilDesc.StencilEnable = FALSE;
    psoDesc.DepthStencilState = depthStencilDesc;

    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NodeMask = 0;
    psoDesc.CachedPSO.pCachedBlob = nullptr;
    psoDesc.CachedPSO.CachedBlobSizeInBytes = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ThrowIfFailed(pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineState)));

    // Create transform constant buffer (b0)
    const UINT transformBufferSize = (sizeof(TransformBuffer) + 255) & ~255;
    m_pTransformBuffer = CreateBuffer(pDevice, transformBufferSize,
                                      D3D12_RESOURCE_FLAG_NONE,
                                      D3D12_RESOURCE_STATE_GENERIC_READ);

    // Create pixel params constant buffer (b1) - available for any shader that needs it
    const UINT pixelParamsBufferSize = (sizeof(PixelParamsBuffer) + 255) & ~255;
    m_pPixelParamsBuffer = CreateBuffer(pDevice, pixelParamsBufferSize,
                                        D3D12_RESOURCE_FLAG_NONE,
                                        D3D12_RESOURCE_STATE_GENERIC_READ);

    // Initialize pixel params with iteration count
    PixelParamsBuffer pixelParams = {};
    pixelParams.IterationCount = m_config.pixelShaderIterations;
    
    uint8_t* pMappedPixelParams = nullptr;
    CD3DX12_RANGE readRangePixel(0, 0);
    ThrowIfFailed(m_pPixelParamsBuffer->Map(0, &readRangePixel, reinterpret_cast<void**>(&pMappedPixelParams)));
    memcpy(pMappedPixelParams, &pixelParams, sizeof(PixelParamsBuffer));
    m_pPixelParamsBuffer->Unmap(0, nullptr);

    // Create CBV heap (2 descriptors: transform buffer b0, pixel params b1)
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
    cbvHeapDesc.NumDescriptors = 2;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(pDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_pCBVHeap)));

    UINT cbvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE cbvHandle(m_pCBVHeap->GetCPUDescriptorHandleForHeapStart());

    // Create CBV for transform buffer (b0)
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_pTransformBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = transformBufferSize;
    pDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);

    // Create CBV for pixel params buffer (b1)
    cbvHandle.Offset(1, cbvDescriptorSize);
    cbvDesc.BufferLocation = m_pPixelParamsBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = pixelParamsBufferSize;
    pDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);

    // Map transform buffer
    CD3DX12_RANGE readRange(0, 0);
    ThrowIfFailed(m_pTransformBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pMappedTransformBuffer)));
}

std::shared_ptr<IQuery> D3D12Renderer::createQuery(QueryCapability capabilities, uint32_t slotCount)
{
    auto pSwapChain = m_pWindow->getSwapChain();
    auto pQueue = pSwapChain->getQueue();
    return std::make_shared<D3D12Query>(m_pWindow->getDevice(), pQueue->getQueue(), capabilities, slotCount);
}

box3 D3D12Renderer::render(IQuery* query)
{
    auto pSwapChain = m_pWindow->getSwapChain();
    auto pQueue = pSwapChain->getQueue();
    auto pCmdList = pQueue->beginRecording();

    // Begin query if provided
    D3D12Query* pD3D12Query = nullptr;
    if (query)
    {
        pD3D12Query = dynamic_cast<D3D12Query*>(query);
        if (pD3D12Query)
        {
            pD3D12Query->beginInternal(pCmdList.Get(), m_frameIndex);
        }
    }

    // Get dimensions
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    ThrowIfFailed(pSwapChain->getSwapChain()->GetDesc1(&swapChainDesc));
    UINT width = swapChainDesc.Width;
    UINT height = swapChainDesc.Height;

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = width;
    scissorRect.bottom = height;

    pCmdList->RSSetViewports(1, &viewport);
    pCmdList->RSSetScissorRects(1, &scissorRect);

    // Get render targets
    ID3D12Resource* backBuffer = pSwapChain->getBBColor();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pSwapChain->getBBColorCPUHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pSwapChain->getBBDepthCPUHandle();

    // Transition to render target
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pCmdList->ResourceBarrier(1, &barrier);

    // Set render targets
    pCmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear
    const float* clearColor = &m_config.clearColor.x;
    pCmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    pCmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Update transform buffer
    if (m_pMappedTransformBuffer)
    {
        m_camera.setAspectRatio(static_cast<float>(width) / static_cast<float>(height));
        
        // Convert visLib::float4x4 to DirectX::XMMATRIX
        auto viewMat = m_camera.getViewMatrix();
        auto projMat = m_camera.getProjectionMatrix();
        
        m_transformData.View = DirectX::XMMATRIX(
            viewMat.row0.x, viewMat.row0.y, viewMat.row0.z, viewMat.row0.w,
            viewMat.row1.x, viewMat.row1.y, viewMat.row1.z, viewMat.row1.w,
            viewMat.row2.x, viewMat.row2.y, viewMat.row2.z, viewMat.row2.w,
            viewMat.row3.x, viewMat.row3.y, viewMat.row3.z, viewMat.row3.w
        );
        
        m_transformData.Projection = DirectX::XMMATRIX(
            projMat.row0.x, projMat.row0.y, projMat.row0.z, projMat.row0.w,
            projMat.row1.x, projMat.row1.y, projMat.row1.z, projMat.row1.w,
            projMat.row2.x, projMat.row2.y, projMat.row2.z, projMat.row2.w,
            projMat.row3.x, projMat.row3.y, projMat.row3.z, projMat.row3.w
        );
        
        memcpy(m_pMappedTransformBuffer, &m_transformData, sizeof(TransformBuffer));
    }

    // Set pipeline state
    ID3D12DescriptorHeap* heaps[] = { m_pCBVHeap.Get() };
    pCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    pCmdList->SetGraphicsRootSignature(m_pRootSignature.Get());
    pCmdList->SetPipelineState(m_pPipelineState.Get());
    pCmdList->SetGraphicsRootDescriptorTable(0, m_pCBVHeap->GetGPUDescriptorHandleForHeapStart());

    // Render objects
    box3 sceneBoundingBox;
    sceneBoundingBox.m_mins = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    sceneBoundingBox.m_maxs = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool hasValidBounds = false;

    for (auto it = m_objects.begin(); it != m_objects.end(); )
    {
        auto pObject = it->lock();
        if (!pObject)
        {
            it = m_objects.erase(it);
            continue;
        }

        auto node = pObject->updateMeshNode();
        if (!node.isEmpty())
        {
            renderMeshNode(node, affine3::identity(), pCmdList.Get(), sceneBoundingBox, hasValidBounds);
        }

        ++it;
    }

    // Render text objects on top
    for (auto it = m_textObjects.begin(); it != m_textObjects.end(); )
    {
        auto pText = it->lock();
        if (!pText)
        {
            it = m_textObjects.erase(it);
            continue;
        }
        pText->render(pSwapChain, m_pRootSignature.Get(), pCmdList.Get());
        ++it;
    }

    // Transition back to present
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    pCmdList->ResourceBarrier(1, &barrier);

    // End query (before execute)
    if (pD3D12Query)
    {
        pD3D12Query->endInternal(pCmdList.Get());
    }

    // Execute
    pQueue->execute(pCmdList);

    // Return scene bounds
    if (!hasValidBounds)
    {
        sceneBoundingBox.m_mins = float3(0.0f, 0.0f, 0.0f);
        sceneBoundingBox.m_maxs = float3(0.0f, 0.0f, 0.0f);
    }

    return sceneBoundingBox;
}

void D3D12Renderer::renderMeshNode(const MeshNode& node, const affine3& parentTransform,
                                    ID3D12GraphicsCommandList* pCmdList, box3& sceneBoundingBox, bool& hasValidBounds)
{
    affine3 worldTransform = node.getTransform() * parentTransform;

    const auto& meshes = node.getMeshes();
    for (const auto& pMesh : meshes)
    {
        if (!pMesh) continue;

        // Cast to D3D12Mesh
        auto pD3D12Mesh = std::dynamic_pointer_cast<D3D12Mesh>(pMesh);
        if (!pD3D12Mesh || pD3D12Mesh->isEmpty()) continue;

        // Set world matrix
        const auto& m = worldTransform.m_linear;
        const auto& t = worldTransform.m_translation;
        DirectX::XMMATRIX worldMatrix(
            m.m00, m.m01, m.m02, 0.0f,
            m.m10, m.m11, m.m12, 0.0f,
            m.m20, m.m21, m.m22, 0.0f,
            t.x, t.y, t.z, 1.0f
        );
        pCmdList->SetGraphicsRoot32BitConstants(2, 16, &worldMatrix, 0);

        pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW vbv = pD3D12Mesh->getVertexBufferView();
        pCmdList->IASetVertexBuffers(0, 1, &vbv);

        D3D12_INDEX_BUFFER_VIEW ibv = pD3D12Mesh->getIndexBufferView();
        pCmdList->IASetIndexBuffer(&ibv);

        pCmdList->DrawIndexedInstanced(pD3D12Mesh->getIndexCount(), 1, 0, 0, 0);

        // Accumulate bounds
        const box3& localBounds = pD3D12Mesh->getBoundingBox();
        if (!localBounds.isempty())
        {
            box3 worldBounds = localBounds * worldTransform;
            if (hasValidBounds)
            {
                sceneBoundingBox = sceneBoundingBox | worldBounds;
            }
            else
            {
                sceneBoundingBox = worldBounds;
                hasValidBounds = true;
            }
        }
    }

    // Render children
    for (const auto& child : node.getChildren())
    {
        renderMeshNode(child, worldTransform, pCmdList, sceneBoundingBox, hasValidBounds);
    }
}

void D3D12Renderer::present()
{
    auto pSwapChain = m_pWindow->getSwapChain();
    ThrowIfFailed(pSwapChain->getSwapChain()->Present(1, 0));
    m_frameIndex++;
}

void D3D12Renderer::waitForGPU()
{
    auto pSwapChain = m_pWindow->getSwapChain();
    if (pSwapChain)
    {
        auto pQueue = pSwapChain->getQueue();
        if (pQueue)
        {
            pQueue->flush();
        }
    }
}

// Factory function implementation
std::shared_ptr<IRenderer> createRenderer(IWindow* window, const RendererConfig& config)
{
    // Try D3D12 desktop window first
    auto pD3D12Window = dynamic_cast<D3D12Window*>(window);
    if (pD3D12Window)
    {
        return std::make_shared<D3D12Renderer>(pD3D12Window, config);
    }

    // Try OpenXR VR window
    extern std::shared_ptr<IRenderer> tryCreateOpenXRRenderer(IWindow* window, const RendererConfig& config);
    auto vrRenderer = tryCreateOpenXRRenderer(window, config);
    if (vrRenderer)
    {
        return vrRenderer;
    }

    throw std::runtime_error("Unsupported window type for renderer creation");
}

} // namespace visLib

#endif // _WIN32
