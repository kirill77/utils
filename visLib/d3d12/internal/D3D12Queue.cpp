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

    // Create command allocator
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

    // Create command list
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));

    // Close the command list to prepare it for first use
    ThrowIfFailed(m_commandList->Close());
}

Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> D3D12Queue::beginRecording()
{
    // Reset the command allocator
    ThrowIfFailed(m_commandAllocator->Reset());

    // Reset and open the command list for recording
    ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), nullptr));

    return m_commandList;
}

bool D3D12Queue::execute(Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> pCmdList)
{
    // Close the command list
    ThrowIfFailed(pCmdList->Close());

    // Execute the command list
    ID3D12CommandList* ppCommandLists[] = { pCmdList.Get() };
    m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

    flush();

    return true;
}

void D3D12Queue::flush()
{
    // Create fence for synchronization
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    // Create event handle
    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        return;
    }

    // Signal the fence
    uint64_t fenceValue = 1;
    ThrowIfFailed(m_commandQueue->Signal(fence.Get(), fenceValue));

    // Wait for the fence
    if (fence->GetCompletedValue() < fenceValue)
    {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
    }

    CloseHandle(eventHandle);
}

} // namespace visLib

#endif // _WIN32
