#pragma once

#ifdef _WIN32

#include "utils/visLib/d3d12/internal/D3D12Common.h"

namespace visLib {
namespace d3d12 {

// GPUQueue - Manages D3D12 command queue, allocator, and list
class GPUQueue
{
public:
    GPUQueue(Microsoft::WRL::ComPtr<ID3D12Device> device);
    ~GPUQueue() = default;

    // Command list recording
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> beginRecording();
    bool execute(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCmdList);
    
    // Synchronization
    void flush();
    
    // Accessors
    ID3D12CommandQueue* getQueue() const { return m_commandQueue.Get(); }
    Microsoft::WRL::ComPtr<ID3D12Device> getDevice() const { return m_device; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
};

} // namespace d3d12
} // namespace visLib

#endif // _WIN32
