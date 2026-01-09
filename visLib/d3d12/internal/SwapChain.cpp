#ifdef _WIN32

#include "D3D12Common.h"
#include "SwapChain.h"
#include "DirectXHelpers.h"
#include <dxgi1_6.h>
#include <stdexcept>

namespace visLib {
namespace d3d12 {

SwapChain::SwapChain(ID3D12Device* pDevice, HWND hWnd)
    : m_pDevice(pDevice), m_hWnd(hWnd)
{
    // Get descriptor sizes
    m_rtvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_dsvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    m_srvDescriptorSize = m_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create descriptor heaps
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = m_backBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    HRESULT hr = m_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_pRTVHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create RTV descriptor heap");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = m_pDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_pDSVHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create DSV descriptor heap");

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = m_backBufferCount + 1; // Back buffers + depth buffer
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = m_pDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_pSRVHeap));
    if (FAILED(hr)) throw std::runtime_error("Failed to create SRV descriptor heap");

    // Get window client area dimensions
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    // Create DXGI factory
    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) throw std::runtime_error("Failed to create DXGI factory");

    // Create GPUQueue (which creates and manages the command queue)
    m_pGPUQueue = std::make_shared<GPUQueue>(m_pDevice);

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = width;
    swapChainDesc.Height = height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = m_backBufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = 0;

    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(m_pGPUQueue->getQueue(), m_hWnd, &swapChainDesc, nullptr, nullptr, &swapChain1);
    if (FAILED(hr)) throw std::runtime_error("Failed to create swap chain");

    hr = swapChain1.As(&m_pSwapChain);
    if (FAILED(hr)) throw std::runtime_error("Failed to query IDXGISwapChain4");

    // Create back buffer resources
    createBackBufferResources();
    createDepthBuffer();
}

IDXGISwapChain4* SwapChain::getSwapChain()
{
    return m_pSwapChain.Get();
}

ID3D12CommandQueue* SwapChain::getCommandQueue()
{
    return m_pGPUQueue->getQueue();
}

void SwapChain::notifyWindowResized()
{
    // Release existing resources
    releaseBackBufferResources();

    // Get new window dimensions
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    // Resize swap chain buffers
    HRESULT hr = m_pSwapChain->ResizeBuffers(m_backBufferCount, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) throw std::runtime_error("Failed to resize swap chain buffers");

    // Recreate resources with new size
    createBackBufferResources();
    createDepthBuffer();
}

ID3D12Resource* SwapChain::getBBColor()
{
    UINT currentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    return m_pBackBuffers[currentBackBufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::getBBColorCPUHandle()
{
    UINT currentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += currentBackBufferIndex * m_rtvDescriptorSize;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE SwapChain::getBBColorGPUHandle()
{
    UINT currentBackBufferIndex = m_pSwapChain->GetCurrentBackBufferIndex();
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_pSRVHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += currentBackBufferIndex * m_srvDescriptorSize;
    return handle;
}

ID3D12Resource* SwapChain::getBBDepth()
{
    return m_pDepthBuffer.Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE SwapChain::getBBDepthCPUHandle()
{
    return m_pDSVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE SwapChain::getBBDepthGPUHandle()
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_pSRVHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += m_backBufferCount * m_srvDescriptorSize; // Depth SRV is after color SRVs
    return handle;
}

void SwapChain::createBackBufferResources()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pRTVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_pSRVHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT i = 0; i < m_backBufferCount; ++i)
    {
        // Get back buffer resource
        HRESULT hr = m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pBackBuffers[i]));
        if (FAILED(hr)) throw std::runtime_error("Failed to get swap chain buffer");

        // Create RTV
        m_pDevice->CreateRenderTargetView(m_pBackBuffers[i].Get(), nullptr, rtvHandle);

        // Create SRV for shader access
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_pDevice->CreateShaderResourceView(m_pBackBuffers[i].Get(), &srvDesc, srvHandle);

        // Move to next descriptors
        rtvHandle.ptr += m_rtvDescriptorSize;
        srvHandle.ptr += m_srvDescriptorSize;
    }
}

void SwapChain::createDepthBuffer()
{
    // Get window dimensions
    RECT clientRect;
    GetClientRect(m_hWnd, &clientRect);
    UINT width = clientRect.right - clientRect.left;
    UINT height = clientRect.bottom - clientRect.top;

    // Create depth buffer resource
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = m_pDevice->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_pDepthBuffer)
    );
    if (FAILED(hr)) throw std::runtime_error("Failed to create depth buffer");

    // Create DSV
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    m_pDevice->CreateDepthStencilView(m_pDepthBuffer.Get(), &dsvDesc, m_pDSVHeap->GetCPUDescriptorHandleForHeapStart());

    // Create SRV for shader access to depth buffer
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_pSRVHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += m_backBufferCount * m_srvDescriptorSize; // Place after color buffer SRVs
    m_pDevice->CreateShaderResourceView(m_pDepthBuffer.Get(), &srvDesc, srvHandle);
}

void SwapChain::releaseBackBufferResources()
{
    for (UINT i = 0; i < m_backBufferCount; ++i)
    {
        m_pBackBuffers[i].Reset();
    }
    m_pDepthBuffer.Reset();
}

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
