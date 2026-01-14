#pragma once

#ifdef _WIN32

#include "D3D12Common.h"

namespace visLib {

// D3D12Queue - Manages D3D12 command queue, allocator, and list
class D3D12Queue
{
public:
    D3D12Queue(Microsoft::WRL::ComPtr<ID3D12Device> device);
    ~D3D12Queue() = default;

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

} // namespace visLib

#endif // _WIN32
