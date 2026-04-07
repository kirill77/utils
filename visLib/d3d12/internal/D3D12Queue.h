#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include "D3D12DeferredDeletion.h"

namespace visLib {

// D3D12Queue - Manages D3D12 command queue, allocator, and list
class D3D12Queue
{
public:
    D3D12Queue(Microsoft::WRL::ComPtr<ID3D12Device> device);
    ~D3D12Queue();

    // Command list recording
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> beginRecording();
    bool execute(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCmdList);

    // Synchronization
    void flush();

    // Deferred GPU resource deletion — enqueue a resource to be released
    // after the GPU finishes the currently recording command list.
    void deferRelease(Microsoft::WRL::ComPtr<IUnknown> resource);

    // Accessors
    ID3D12CommandQueue* getQueue() const { return m_commandQueue.Get(); }
    Microsoft::WRL::ComPtr<ID3D12Device> getDevice() const { return m_device; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[2];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValues[2] = {};
    uint64_t m_nextFenceValue = 1;
    int m_currentAllocator = 0;
    D3D12DeferredDeletion m_deferredDeletion;
};

} // namespace visLib

#endif // _WIN32
