#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include "D3D12Queue.h"

namespace visLib {

// D3D12SwapChain - Manages DXGI swap chain and related resources
class D3D12SwapChain
{
public:
    D3D12SwapChain(ID3D12Device* pDevice, HWND hWnd, IDXGIFactory6* pFactory, bool latencyWaitable);
    ~D3D12SwapChain();
    // Owns m_frameLatencyWaitable: no copies, or the handle is closed twice
    D3D12SwapChain(const D3D12SwapChain&) = delete;
    D3D12SwapChain& operator=(const D3D12SwapChain&) = delete;

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

    // Waits on the frame latency waitable object (1 s timeout). Returns at
    // once when the swap chain was created without the waitable flag.
    // Returns false if the wait timed out.
    bool waitForFrameLatency();

    // D3D12Queue access
    std::shared_ptr<D3D12Queue> getQueue() const { return m_pQueue; }

    // Descriptor heap access
    ID3D12DescriptorHeap* getSRVHeap() const { return m_pSRVHeap.Get(); }
    UINT getSRVDescriptorSize() const { return m_srvDescriptorSize; }

    // Tearing support query (true if DXGI_PRESENT_ALLOW_TEARING is usable)
    bool isTearingSupported() const { return m_tearingSupported; }

private:
    void createBackBufferResources();
    void createDepthBuffer();
    void releaseBackBufferResources();

private:
    ID3D12Device* m_pDevice = nullptr;
    HWND m_hWnd = nullptr;

    // 3 is the more typical request from PC games than 2.
    static constexpr UINT m_backBufferCount = 3;

    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_pSwapChain;
    std::shared_ptr<D3D12Queue> m_pQueue;

    // Descriptor heaps
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pRTVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pDSVHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pSRVHeap;

    UINT m_rtvDescriptorSize = 0;
    UINT m_dsvDescriptorSize = 0;
    UINT m_srvDescriptorSize = 0;

    // Tearing support (needed for VSync-off with flip-model swap chains)
    bool m_tearingSupported = false;

    // Flags used at creation; ResizeBuffers must pass the same ones
    UINT m_swapChainFlags = 0;

    // From GetFrameLatencyWaitableObject; null without the waitable flag
    HANDLE m_frameLatencyWaitable = nullptr;

    // Back buffer resources
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pBackBuffers[m_backBufferCount];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pDepthBuffer;
};

} // namespace visLib

#endif // _WIN32
