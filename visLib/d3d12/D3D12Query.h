#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IQuery.h"
#include "utils/visLib/d3d12/internal/D3D12Common.h"
#include <vector>

namespace visLib {

// D3D12 implementation of IQuery
// Supports timestamps, pipeline stats, or both based on capabilities
// Uses a ring buffer of query slots to support multiple frames in flight
class D3D12Query : public IQuery {
public:
    D3D12Query(ID3D12Device* pDevice, ID3D12CommandQueue* pQueue, 
               QueryCapability capabilities, uint32_t slotCount);
    ~D3D12Query() override;

    // IQuery interface
    QueryCapability getCapabilities() const override { return m_capabilities; }
    uint32_t getReadyCount() const override;
    uint32_t getCapacity() const override { return m_slotCount; }
    bool getTimestampResult(TimestampQueryResult& outResult) override;
    bool getPipelineStatsResult(PipelineStatsQueryResult& outResult) override;

    // D3D12-specific methods (called by D3D12Renderer)
    bool beginInternal(ID3D12GraphicsCommandList* pCmdList, uint64_t frameIndex);
    void endInternal(ID3D12GraphicsCommandList* pCmdList);

private:
    enum class SlotState {
        Free,
        Active,
        Pending,
        Ready
    };

    struct Slot {
        SlotState state = SlotState::Free;
        uint64_t frameIndex = 0;
    };

    void updateSlotStates();
    bool isSlotReady(uint32_t slotIdx) const;

    QueryCapability m_capabilities;
    ID3D12Device* m_pDevice;
    ID3D12CommandQueue* m_pQueue;
    uint32_t m_slotCount;

    // Timestamp resources (created only if Timestamps capability)
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_pTimestampHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pTimestampReadback;
    uint64_t* m_pTimestampBuffer = nullptr;
    uint64_t m_timestampFrequency = 0;

    // Pipeline stats resources (created only if PipelineStats capability)
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_pPipelineStatsHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_pPipelineStatsReadback;
    D3D12_QUERY_DATA_PIPELINE_STATISTICS* m_pPipelineStatsBuffer = nullptr;

    // Ring buffer state (shared across all capabilities)
    std::vector<Slot> m_slots;
    uint32_t m_activeSlot = UINT32_MAX;
    uint32_t m_nextSlot = 0;
    uint32_t m_oldestPendingSlot = 0;
};

} // namespace visLib

#endif // _WIN32
