#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include "D3D12Queue.h"

namespace visLib {

// D3D12SwapChain - Manages DXGI swap chain and related resources
class D3D12SwapChain
{
public:
    D3D12SwapChain(ID3D12Device* pDevice, HWND hWnd);
    ~D3D12SwapChain() = default;

    // Swap chain access
    IDXGISwapChain4* getSwapChain();
    ID3D12CommandQueue* getCommandQueue();

    // Resource access
    ID3D12Resource* getBBColor();
    D3D12_CPU_DESCRIPTOR_HANDLE getBBColorCPUHandle();
    D3D12_GPU_DESCRIPTOR_HANDLE getBBColorGPUHandle();

    ID3D12Resource* getBBDepth();
    D3D12_CPU_DESCRIPTOR_HANDLE getBBDepthCPUHandle();
    D3D12_GPU_DESCRIPTOR_HANDLE getBBDepthGPUHandle();

    // Window resize handling
    void notifyWindowResized();

    // D3D12Queue access
    std::shared_ptr<D3D12Queue> getQueue() const { return m_pQueue; }

    // Descriptor heap access
    ID3D12DescriptorHeap* getSRVHeap() const { return m_pSRVHeap.Get(); }
    UINT getSRVDescriptorSize() const { return m_srvDescriptorSize; }

private:
    void createBackBufferResources();
    void createDepthBuffer();
    void releaseBackBufferResources();

private:
    ID3D12Device* m_pDevice = nullptr;
    HWND m_hWnd = nullptr;

    static constexpr UINT m_backBufferCount = 2;

    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_pSwapChain;
    std::shared_ptr<D3D12Queue> m_pQueue;

    // Descriptor heaps
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pRTVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pDSVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pSRVHeap;

    UINT m_rtvDescriptorSize = 0;
    UINT m_dsvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;

    // Back buffer resources
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pBackBuffers[m_backBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pDepthBuffer;
};

} // namespace visLib

#endif // _WIN32
