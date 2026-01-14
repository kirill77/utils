// OpenXRRenderer.cpp: VR stereo renderer implementation

#ifdef _WIN32

#include "OpenXRRenderer.h"
#include "utils/visLib/include/IVisObject.h"
#include "utils/visLib/d3d12/D3D12Mesh.h"
#include "utils/visLib/d3d12/D3D12Font.h"
#include "utils/visLib/d3d12/D3D12Text.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include "utils/visLib/d3d12/internal/CD3DX12.h"
#include "utils/visLib/d3d12/internal/D3D12ShaderHelper.h"
#include "utils/log/ILog.h"
#include <cmath>

namespace visLib {
namespace openxr {

OpenXRRenderer::OpenXRRenderer(OpenXRWindow* pWindow, const RendererConfig& config)
    : m_pWindow(pWindow)
    , m_session(pWindow->getSession())
    , m_config(config)
{
    // Set default camera (VR will override this with head tracking)
    m_camera.setPosition(float3(0.0f, 0.0f, 0.0f));
    m_camera.setDirection(float3(0.0f, 0.0f, 1.0f));
    m_camera.setUp(float3(0.0f, 1.0f, 0.0f));
    m_camera.setFOV(90.0f);  // VR typically uses wide FOV

    initializeRenderResources();
}

OpenXRRenderer::~OpenXRRenderer()
{
    waitForGPU();

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
    }
}

void OpenXRRenderer::initializeRenderResources()
{
    auto pDevice = m_pWindow->getDevice();
    if (!pDevice) return;

    // Create command allocator and list
    HRESULT hr = pDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocator));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create command allocator: 0x%08X", hr);
        return;
    }

    hr = pDevice->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_pCommandList));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create command list: 0x%08X", hr);
        return;
    }
    m_pCommandList->Close();

    // Create fence for GPU synchronization
    hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create fence: 0x%08X", hr);
        return;
    }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // Create root signature (same as D3D12Renderer)
    CD3DX12_DESCRIPTOR_RANGE ranges[2];
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 2, 0);
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

    CD3DX12_ROOT_PARAMETER rootParameters[3];
    rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
    rootParameters[2].InitAsConstants(16, 2, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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

    std::wstring shaderPath = L"Shaders/";
    vertexShader = shaderHelper.loadCompiledShader(shaderPath + L"VertexShader.cso");
    pixelShader = shaderHelper.loadCompiledShader(shaderPath + L"PixelShader.cso");

    if (!vertexShader || !pixelShader) {
        shaderPath = L"utils/visLib/d3d12/Shaders/";
        if (!vertexShader)
            vertexShader = shaderHelper.loadShader(shaderPath + L"VertexShader.hlsl", "main", "vs_5_0", compileFlags);
        if (!pixelShader)
            pixelShader = shaderHelper.loadShader(shaderPath + L"PixelShader.hlsl", "main", "ps_5_0", compileFlags);
    }

    // Vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    // Get swapchain format
    DXGI_FORMAT rtvFormat = m_session ? m_session->getSwapchainFormat() : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

    // Pipeline state
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pRootSignature.Get();
    psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
    psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
    psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
    psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();

    psoDesc.RasterizerState.FillMode = m_config.wireframeMode ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState = blendDesc;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState = depthStencilDesc;

    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = rtvFormat;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    hr = pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pPipelineState));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create pipeline state: 0x%08X", hr);
        return;
    }

    // Create transform constant buffer
    const UINT constantBufferSize = (sizeof(TransformBuffer) + 255) & ~255;
    m_pTransformBuffer = CreateBuffer(pDevice, constantBufferSize,
                                              D3D12_RESOURCE_FLAG_NONE,
                                              D3D12_RESOURCE_STATE_GENERIC_READ);

    // Create CBV heap
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = pDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_pCBVHeap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create CBV heap: 0x%08X", hr);
        return;
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = m_pTransformBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = constantBufferSize;
    pDevice->CreateConstantBufferView(&cbvDesc, m_pCBVHeap->GetCPUDescriptorHandleForHeapStart());

    CD3DX12_RANGE readRange(0, 0);
    hr = m_pTransformBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_pMappedTransformBuffer));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to map transform buffer: 0x%08X", hr);
        return;
    }

    // Create RTV heap for swapchain images
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 8;  // Enough for multiple swapchain images per eye
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pRTVHeap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create RTV heap: 0x%08X", hr);
        return;
    }

    // Create DSV heap
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hr = pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_pDSVHeap));
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create DSV heap: 0x%08X", hr);
        return;
    }

    // Create depth buffer (sized for one eye - we reuse it)
    if (m_session) {
        uint32_t width = m_session->getRenderWidth();
        uint32_t height = m_session->getRenderHeight();

        D3D12_RESOURCE_DESC depthDesc = {};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = width;
        depthDesc.Height = height;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        hr = pDevice->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
            IID_PPV_ARGS(&m_pDepthBuffer));
        if (FAILED(hr)) {
            LOG_ERROR("Failed to create depth buffer: 0x%08X", hr);
            return;
        }

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        pDevice->CreateDepthStencilView(m_pDepthBuffer.Get(), &dsvDesc,
                                        m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Create D3D12Queue for font texture uploads
    m_pGPUQueue = std::make_shared<D3D12Queue>(pDevice);
}

std::shared_ptr<IMesh> OpenXRRenderer::createMesh()
{
    return std::make_shared<D3D12Mesh>(m_pWindow->getDevice());
}

std::shared_ptr<IFont> OpenXRRenderer::createFont(uint32_t fontSize)
{
    if (!m_pGPUQueue)
    {
        return nullptr;
    }
    DXGI_FORMAT rtvFormat = m_session ? m_session->getSwapchainFormat() : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    return std::make_shared<D3D12Font>(fontSize, m_pGPUQueue.get(), rtvFormat);
}

std::shared_ptr<IText> OpenXRRenderer::createText(std::shared_ptr<IFont> font)
{
    auto d3d12Font = std::dynamic_pointer_cast<D3D12Font>(font);
    if (!d3d12Font)
    {
        return nullptr;
    }
    auto text = std::make_shared<D3D12Text>(d3d12Font);
    m_textObjects.push_back(text);
    return text;
}

void OpenXRRenderer::addObject(std::weak_ptr<IVisObject> object)
{
    m_objects.push_back(object);
}

void OpenXRRenderer::removeObject(std::weak_ptr<IVisObject> object)
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

void OpenXRRenderer::clearObjects()
{
    m_objects.clear();
}

void OpenXRRenderer::setConfig(const RendererConfig& config)
{
    m_config = config;
}

DirectX::XMMATRIX OpenXRRenderer::xrPoseToViewMatrix(const XrPosef& pose)
{
    // Convert XR quaternion to rotation matrix
    DirectX::XMVECTOR quat = DirectX::XMVectorSet(
        pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w);
    DirectX::XMMATRIX rotation = DirectX::XMMatrixRotationQuaternion(quat);

    // Translation
    DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(
        pose.position.x, pose.position.y, pose.position.z);

    // View matrix is inverse of pose
    DirectX::XMMATRIX transform = rotation * translation;
    return DirectX::XMMatrixInverse(nullptr, transform);
}

DirectX::XMMATRIX OpenXRRenderer::xrFovToProjectionMatrix(const XrFovf& fov, float nearZ, float farZ)
{
    // Asymmetric projection matrix from XR FOV angles
    float tanLeft = tanf(fov.angleLeft);
    float tanRight = tanf(fov.angleRight);
    float tanUp = tanf(fov.angleUp);
    float tanDown = tanf(fov.angleDown);

    float width = tanRight - tanLeft;
    float height = tanUp - tanDown;

    DirectX::XMMATRIX proj;
    proj.r[0] = DirectX::XMVectorSet(2.0f / width, 0.0f, 0.0f, 0.0f);
    proj.r[1] = DirectX::XMVectorSet(0.0f, 2.0f / height, 0.0f, 0.0f);
    proj.r[2] = DirectX::XMVectorSet(
        (tanRight + tanLeft) / width,
        (tanUp + tanDown) / height,
        -farZ / (farZ - nearZ),
        -1.0f);
    proj.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, -farZ * nearZ / (farZ - nearZ), 0.0f);

    return proj;
}

box3 OpenXRRenderer::render()
{
    if (!m_session || !m_session->isSessionRunning()) {
        return box3();
    }

    // Begin XR frame
    if (!m_session->beginFrame()) {
        m_frameStarted = false;
        return box3();
    }
    m_frameStarted = true;

    box3 sceneBoundingBox;
    sceneBoundingBox.m_mins = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    sceneBoundingBox.m_maxs = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    m_lastStats = {};

    auto pDevice = m_pWindow->getDevice();

    // Render each eye
    for (int eye = 0; eye < 2; ++eye) {
        uint32_t imageIndex = 0;
        if (!m_session->acquireSwapchainImage(eye, &imageIndex)) {
            continue;
        }

        ID3D12Resource* renderTarget = m_session->getSwapchainImage(eye, imageIndex);
        if (!renderTarget) {
            m_session->releaseSwapchainImage(eye);
            continue;
        }

        // Get view/projection for this eye
        const XrView& view = m_session->getView(eye);
        DirectX::XMMATRIX viewMatrix = xrPoseToViewMatrix(view.pose);
        DirectX::XMMATRIX projMatrix = xrFovToProjectionMatrix(view.fov, 0.01f, 1000.0f);

        // Create RTV for this swapchain image
        UINT rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), eye, rtvDescriptorSize);

        // Must specify format explicitly - swapchain may use typeless format
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = m_session->getSwapchainFormat();
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        rtvDesc.Texture2D.PlaneSlice = 0;
        pDevice->CreateRenderTargetView(renderTarget, &rtvDesc, rtvHandle);

        renderEye(eye, viewMatrix, projMatrix, renderTarget, sceneBoundingBox);

        m_session->releaseSwapchainImage(eye);
    }

    // Update camera position from head tracking (for any code that queries camera)
    if (m_session) {
        const XrView& leftEye = m_session->getView(0);
        m_camera.setPosition(float3(
            leftEye.pose.position.x,
            leftEye.pose.position.y,
            leftEye.pose.position.z));
    }

    return sceneBoundingBox;
}

void OpenXRRenderer::renderEye(int eye, const DirectX::XMMATRIX& viewMatrix,
                                const DirectX::XMMATRIX& projMatrix,
                                ID3D12Resource* renderTarget, box3& sceneBoundingBox)
{
    auto pDevice = m_pWindow->getDevice();
    auto pCommandQueue = m_pWindow->getCommandQueue();

    // Reset command allocator and list
    m_pCommandAllocator->Reset();
    m_pCommandList->Reset(m_pCommandAllocator.Get(), m_pPipelineState.Get());

    uint32_t width = m_session->getRenderWidth();
    uint32_t height = m_session->getRenderHeight();

    // Viewport and scissor
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

    m_pCommandList->RSSetViewports(1, &viewport);
    m_pCommandList->RSSetScissorRects(1, &scissor);

    // Get descriptor handles
    UINT rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_pRTVHeap->GetCPUDescriptorHandleForHeapStart(), eye, rtvDescriptorSize);
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();

    // Transition render target to render target state
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_pCommandList->ResourceBarrier(1, &barrier);

    m_pCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Clear
    const float* clearColor = &m_config.clearColor.x;
    m_pCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Update transform buffer
    if (m_pMappedTransformBuffer) {
        m_transformData.View = viewMatrix;
        m_transformData.Projection = projMatrix;
        memcpy(m_pMappedTransformBuffer, &m_transformData, sizeof(TransformBuffer));
    }

    // Set pipeline state
    ID3D12DescriptorHeap* heaps[] = { m_pCBVHeap.Get() };
    m_pCommandList->SetDescriptorHeaps(_countof(heaps), heaps);
    m_pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());
    m_pCommandList->SetPipelineState(m_pPipelineState.Get());
    m_pCommandList->SetGraphicsRootDescriptorTable(0, m_pCBVHeap->GetGPUDescriptorHandleForHeapStart());

    // Render objects
    bool hasValidBounds = false;
    for (auto it = m_objects.begin(); it != m_objects.end(); ) {
        auto pObject = it->lock();
        if (!pObject) {
            it = m_objects.erase(it);
            continue;
        }

        auto node = pObject->updateMeshNode();
        if (!node.isEmpty()) {
            renderMeshNode(node, affine3::identity(), m_pCommandList.Get(), sceneBoundingBox, hasValidBounds);
            m_lastStats.objectsRendered++;
        }
        ++it;
    }

    // Render text objects (after 3D, while still in RENDER_TARGET state)
    D3D12RenderTarget textTarget;
    textTarget.width = m_session->getRenderWidth();
    textTarget.height = m_session->getRenderHeight();
    textTarget.rtvHandle = rtvHandle;
    textTarget.dsvHandle = dsvHandle;
    textTarget.pResource = nullptr;  // Skip barriers - already in RENDER_TARGET state

    for (auto it = m_textObjects.begin(); it != m_textObjects.end(); )
    {
        auto pText = it->lock();
        if (!pText)
        {
            it = m_textObjects.erase(it);
            continue;
        }
        pText->render(textTarget, m_pRootSignature.Get(), m_pCommandList.Get());
        ++it;
    }

    // Transition back
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    m_pCommandList->ResourceBarrier(1, &barrier);

    m_pCommandList->Close();

    // Execute
    ID3D12CommandList* cmdLists[] = { m_pCommandList.Get() };
    pCommandQueue->ExecuteCommandLists(1, cmdLists);

    // Wait for GPU (simple sync for now)
    m_fenceValue++;
    pCommandQueue->Signal(m_pFence.Get(), m_fenceValue);
    if (m_pFence->GetCompletedValue() < m_fenceValue) {
        m_pFence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void OpenXRRenderer::renderMeshNode(const MeshNode& node, const affine3& parentTransform,
                                     ID3D12GraphicsCommandList* pCmdList, box3& sceneBoundingBox, bool& hasValidBounds)
{
    affine3 worldTransform = node.getTransform() * parentTransform;

    const auto& meshes = node.getMeshes();
    for (const auto& pMesh : meshes) {
        if (!pMesh) continue;

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

        m_lastStats.drawCalls++;
        m_lastStats.trianglesRendered += pD3D12Mesh->getTriangleCount();

        // Accumulate bounds
        const box3& localBounds = pD3D12Mesh->getBoundingBox();
        if (!localBounds.isempty()) {
            box3 worldBounds = localBounds * worldTransform;
            if (hasValidBounds) {
                sceneBoundingBox = sceneBoundingBox | worldBounds;
            } else {
                sceneBoundingBox = worldBounds;
                hasValidBounds = true;
            }
        }
    }

    // Render children
    for (const auto& child : node.getChildren()) {
        renderMeshNode(child, worldTransform, pCmdList, sceneBoundingBox, hasValidBounds);
    }
}

void OpenXRRenderer::present()
{
    if (m_frameStarted && m_session) {
        m_session->endFrame();
        m_frameStarted = false;
    }
}

void OpenXRRenderer::waitForGPU()
{
    if (!m_pFence || !m_pWindow->getCommandQueue()) return;

    m_fenceValue++;
    m_pWindow->getCommandQueue()->Signal(m_pFence.Get(), m_fenceValue);

    if (m_pFence->GetCompletedValue() < m_fenceValue) {
        m_pFence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

} // namespace openxr
} // namespace visLib

#endif // _WIN32
