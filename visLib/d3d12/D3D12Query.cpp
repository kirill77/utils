#ifdef _WIN32

#include "D3D12Query.h"
#include "utils/visLib/d3d12/internal/DirectXHelpers.h"
#include <cstring>

namespace visLib {

D3D12Query::D3D12Query(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue,
                       QueryCapability capabilities, uint32_t slotCount)
    : m_capabilities(capabilities)
    , m_pDevice(pDevice)
    , m_pQueue(pQueue)
    , m_slotCount(slotCount)
    , m_slots(slotCount)
{
    // Create timestamp resources if requested
    if (hasCapability(capabilities, QueryCapability::Timestamps))
    {
        // Get timestamp frequency
        ThrowIfFailed(pQueue->GetTimestampFrequency(&m_timestampFrequency));

        // Create query heap - need 2 timestamps per slot (begin + end)
        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = slotCount * 2;
        heapDesc.NodeMask = 0;
        ThrowIfFailed(pDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_pTimestampHeap)));

        // Create readback buffer for results (2 uint64 per slot)
        const uint64_t bufferSize = slotCount * 2 * sizeof(uint64_t);
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = bufferSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_pTimestampReadback)));

        // Map and initialize to 0 (sentinel - timestamps are never 0)
        ThrowIfFailed(m_pTimestampReadback->Map(0, nullptr, reinterpret_cast<void**>(&m_pTimestampBuffer)));
        memset(m_pTimestampBuffer, 0, bufferSize);
    }

    // Create pipeline stats resources if requested
    if (hasCapability(capabilities, QueryCapability::PipelineStats))
    {
        // Create query heap - one query per slot
        D3D12_QUERY_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
        heapDesc.Count = slotCount;
        heapDesc.NodeMask = 0;
        ThrowIfFailed(pDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_pPipelineStatsHeap)));

        // Create readback buffer for results
        const uint64_t bufferSize = slotCount * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = bufferSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(pDevice->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_pPipelineStatsReadback)));

        // Map and initialize to 0xFF (sentinel - UINT64_MAX)
        ThrowIfFailed(m_pPipelineStatsReadback->Map(0, nullptr, reinterpret_cast<void**>(&m_pPipelineStatsBuffer)));
        memset(m_pPipelineStatsBuffer, 0xFF, bufferSize);
    }
}

D3D12Query::~D3D12Query()
{
    if (m_pTimestampReadback && m_pTimestampBuffer)
    {
        m_pTimestampReadback->Unmap(0, nullptr);
    }
    if (m_pPipelineStatsReadback && m_pPipelineStatsBuffer)
    {
        m_pPipelineStatsReadback->Unmap(0, nullptr);
    }
}

uint32_t D3D12Query::getReadyCount() const
{
    const_cast<D3D12Query*>(this)->updateSlotStates();
    
    uint32_t count = 0;
    for (const auto& slot : m_slots)
    {
        if (slot.state == SlotState::Ready)
        {
            count++;
        }
    }
    return count;
}

bool D3D12Query::isSlotReady(uint32_t slotIdx) const
{
    // Slot is ready when ALL enabled capabilities have landed
    bool timestampReady = true;
    bool pipelineStatsReady = true;

    if (hasCapability(m_capabilities, QueryCapability::Timestamps))
    {
        // Check end timestamp - if non-zero, GPU has written the result
        uint64_t endTimestamp = m_pTimestampBuffer[slotIdx * 2 + 1];
        timestampReady = (endTimestamp != 0);
    }

    if (hasCapability(m_capabilities, QueryCapability::PipelineStats))
    {
        // Check IAVertices - if not UINT64_MAX, GPU has written the result
        pipelineStatsReady = (m_pPipelineStatsBuffer[slotIdx].IAVertices != UINT64_MAX);
    }

    return timestampReady && pipelineStatsReady;
}

void D3D12Query::updateSlotStates()
{
    for (uint32_t i = 0; i < m_slotCount; i++)
    {
        if (m_slots[i].state == SlotState::Pending)
        {
            if (isSlotReady(i))
            {
                m_slots[i].state = SlotState::Ready;
            }
        }
    }
}

bool D3D12Query::getTimestampResult(TimestampQueryResult& outResult)
{
    if (!hasCapability(m_capabilities, QueryCapability::Timestamps))
    {
        return false;
    }

    updateSlotStates();

    // Find oldest ready slot (FIFO order)
    for (uint32_t i = 0; i < m_slotCount; i++)
    {
        uint32_t slotIdx = (m_oldestPendingSlot + i) % m_slotCount;
        auto& slot = m_slots[slotIdx];
        
        if (slot.state == SlotState::Ready)
        {
            // Read timestamp results
            outResult.frameIndex = slot.frameIndex;
            outResult.beginTimestamp = m_pTimestampBuffer[slotIdx * 2];
            outResult.endTimestamp = m_pTimestampBuffer[slotIdx * 2 + 1];
            outResult.frequency = m_timestampFrequency;

            // Reset timestamp buffer to sentinel for next use
            m_pTimestampBuffer[slotIdx * 2] = 0;
            m_pTimestampBuffer[slotIdx * 2 + 1] = 0;

            // Reset pipeline stats buffer if enabled
            if (hasCapability(m_capabilities, QueryCapability::PipelineStats))
            {
                memset(&m_pPipelineStatsBuffer[slotIdx], 0xFF, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));
            }

            // Recycle slot
            slot.state = SlotState::Free;
            
            // Advance oldest pending pointer if this was it
            if (slotIdx == m_oldestPendingSlot)
            {
                for (uint32_t j = 1; j < m_slotCount; j++)
                {
                    uint32_t nextIdx = (m_oldestPendingSlot + j) % m_slotCount;
                    if (m_slots[nextIdx].state != SlotState::Free)
                    {
                        m_oldestPendingSlot = nextIdx;
                        break;
                    }
                }
            }
            
            return true;
        }
    }

    return false;
}

bool D3D12Query::getPipelineStatsResult(PipelineStatsQueryResult& outResult)
{
    if (!hasCapability(m_capabilities, QueryCapability::PipelineStats))
    {
        return false;
    }

    updateSlotStates();

    // Find oldest ready slot (FIFO order)
    for (uint32_t i = 0; i < m_slotCount; i++)
    {
        uint32_t slotIdx = (m_oldestPendingSlot + i) % m_slotCount;
        auto& slot = m_slots[slotIdx];
        
        if (slot.state == SlotState::Ready)
        {
            // Read pipeline stats results
            const auto& stats = m_pPipelineStatsBuffer[slotIdx];
            outResult.frameIndex = slot.frameIndex;
            outResult.inputAssemblerVertices = stats.IAVertices;
            outResult.inputAssemblerPrimitives = stats.IAPrimitives;
            outResult.vertexShaderInvocations = stats.VSInvocations;
            outResult.geometryShaderInvocations = stats.GSInvocations;
            outResult.geometryShaderPrimitives = stats.GSPrimitives;
            outResult.clipperInvocations = stats.CInvocations;
            outResult.clipperPrimitives = stats.CPrimitives;
            outResult.pixelShaderInvocations = stats.PSInvocations;
            outResult.computeShaderInvocations = stats.CSInvocations;

            // Reset pipeline stats buffer to sentinel for next use
            memset(&m_pPipelineStatsBuffer[slotIdx], 0xFF, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS));

            // Reset timestamp buffer if enabled
            if (hasCapability(m_capabilities, QueryCapability::Timestamps))
            {
                m_pTimestampBuffer[slotIdx * 2] = 0;
                m_pTimestampBuffer[slotIdx * 2 + 1] = 0;
            }

            // Recycle slot
            slot.state = SlotState::Free;
            
            // Advance oldest pending pointer if this was it
            if (slotIdx == m_oldestPendingSlot)
            {
                for (uint32_t j = 1; j < m_slotCount; j++)
                {
                    uint32_t nextIdx = (m_oldestPendingSlot + j) % m_slotCount;
                    if (m_slots[nextIdx].state != SlotState::Free)
                    {
                        m_oldestPendingSlot = nextIdx;
                        break;
                    }
                }
            }
            
            return true;
        }
    }

    return false;
}

bool D3D12Query::beginInternal(ID3D12GraphicsCommandList* pCmdList, uint64_t frameIndex)
{
    // Check if we already have an active measurement
    if (m_activeSlot != UINT32_MAX)
    {
        return false;
    }

    // Find a free slot
    uint32_t freeSlot = UINT32_MAX;
    for (uint32_t i = 0; i < m_slotCount; i++)
    {
        uint32_t slotIdx = (m_nextSlot + i) % m_slotCount;
        if (m_slots[slotIdx].state == SlotState::Free)
        {
            freeSlot = slotIdx;
            break;
        }
    }

    if (freeSlot == UINT32_MAX)
    {
        // No free slots - all are pending/ready
        return false;
    }

    // Record begin timestamp if enabled
    if (hasCapability(m_capabilities, QueryCapability::Timestamps))
    {
        pCmdList->EndQuery(m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, freeSlot * 2);
    }

    // Begin pipeline stats if enabled
    if (hasCapability(m_capabilities, QueryCapability::PipelineStats))
    {
        pCmdList->BeginQuery(m_pPipelineStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, freeSlot);
    }

    // Update state
    m_slots[freeSlot].state = SlotState::Active;
    m_slots[freeSlot].frameIndex = frameIndex;
    m_activeSlot = freeSlot;
    m_nextSlot = (freeSlot + 1) % m_slotCount;

    return true;
}

void D3D12Query::endInternal(ID3D12GraphicsCommandList* pCmdList)
{
    if (m_activeSlot == UINT32_MAX)
    {
        return; // No active measurement
    }

    uint32_t slot = m_activeSlot;

    // Record end timestamp and resolve if enabled
    if (hasCapability(m_capabilities, QueryCapability::Timestamps))
    {
        pCmdList->EndQuery(m_pTimestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);

        // Resolve both timestamps to readback buffer
        pCmdList->ResolveQueryData(
            m_pTimestampHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            slot * 2,           // Start index
            2,                  // Query count (begin + end)
            m_pTimestampReadback.Get(),
            slot * 2 * sizeof(uint64_t)  // Destination offset
        );
    }

    // End pipeline stats and resolve if enabled
    if (hasCapability(m_capabilities, QueryCapability::PipelineStats))
    {
        pCmdList->EndQuery(m_pPipelineStatsHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS, slot);

        pCmdList->ResolveQueryData(
            m_pPipelineStatsHeap.Get(),
            D3D12_QUERY_TYPE_PIPELINE_STATISTICS,
            slot,
            1,
            m_pPipelineStatsReadback.Get(),
            slot * sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)
        );
    }

    m_slots[slot].state = SlotState::Pending;
    m_activeSlot = UINT32_MAX;
}

} // namespace visLib

#endif // _WIN32
