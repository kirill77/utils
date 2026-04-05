#ifdef _WIN32

#include "D3D12Common.h"
#include "D3D12Queue.h"
#include "DirectXHelpers.h"

namespace visLib {

D3D12Queue::D3D12Queue(Microsoft::WRL::ComPtr<ID3D12Device> device)
    : m_device(device)
{
    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

    // Create two command allocators for ping-pong double buffering
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[0])));
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[1])));

    // Create command list
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // Close the command list to prepare it for first use
    ThrowIfFailed(m_commandList->Close());

    // Create persistent fence and event for synchronization
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

D3D12Queue::~D3D12Queue()
{
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> D3D12Queue::beginRecording()
{
    // Switch to the other allocator
    m_currentAllocator ^= 1;

    // Wait until the GPU is done with this allocator's previous commands
    if (m_fence->GetCompletedValue() < m_fenceValues[m_currentAllocator])
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValues[m_currentAllocator], m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    // Reset the allocator and command list for recording
    ThrowIfFailed(m_commandAllocators[m_currentAllocator]->Reset());
    ThrowIfFailed(m_commandList->Reset(m_commandAllocators[m_currentAllocator].Get(), nullptr));

    return m_commandList;
}

bool D3D12Queue::execute(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCmdList)
{
    // Close the command list
    ThrowIfFailed(pCmdList->Close());

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { pCmdList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    // Signal the fence so we know when this allocator's work is done
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_nextFenceValue));
    m_fenceValues[m_currentAllocator] = m_nextFenceValue;
    m_nextFenceValue++;

    return true;
}

void D3D12Queue::flush()
{
    uint64_t waitValue = m_nextFenceValue - 1;
    if (waitValue == 0)
        return;

    if (m_fence->GetCompletedValue() < waitValue)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(waitValue, m_fenceEvent));
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

} // namespace visLib

#endif // _WIN32
