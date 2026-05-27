#pragma once

#ifdef _WIN32

#include "utils/visLib/include/IQuery.h"
#include "utils/visLib/vulkan/internal/VulkanCommon.h"
#include <vector>

namespace visLib {

class VulkanWindow;

// VulkanQuery - mirrors D3D12Query: a ring buffer of slots backed by VkQueryPool(s)
// for timestamps and/or pipeline statistics. begin/end record into a slot; the slot
// moves Free -> Active -> Pending -> Ready as the GPU finishes. CPU drains ready
// slots via getTimestampResult / getPipelineStatsResult.
class VulkanQuery : public IQuery
{
public:
    VulkanQuery(VulkanWindow* pWindow, QueryCapability capabilities, uint32_t slotCount);
    ~VulkanQuery() override;

    VulkanQuery(const VulkanQuery&) = delete;
    VulkanQuery& operator=(const VulkanQuery&) = delete;

    // IQuery
    QueryCapability getCapabilities() const override { return m_capabilities; }
    uint32_t        getReadyCount() const override;
    uint32_t        getCapacity() const override     { return m_slotCount; }
    bool            getTimestampResult(TimestampQueryResult& outResult) override;
    bool            getPipelineStatsResult(PipelineStatsQueryResult& outResult) override;

    // Vulkan-specific: called by VulkanRenderer around the scene draw.
    bool beginInternal(VkCommandBuffer cmd, uint64_t frameIndex);
    void endInternal(VkCommandBuffer cmd);

private:
    enum class SlotState { Free, Active, Pending, Ready };

    struct Slot {
        SlotState state      = SlotState::Free;
        uint64_t  frameIndex = 0;
    };

    void updateSlotStates();
    bool isSlotReady(uint32_t slotIdx);

    QueryCapability  m_capabilities;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_slotCount      = 0;
    uint64_t         m_tsFrequency    = 0;   // ticks per second (derived from timestampPeriod)
    bool             m_pipelineStatsAvailable = false;

    VkQueryPool      m_tsPool         = VK_NULL_HANDLE;   // 2 timestamps per slot
    VkQueryPool      m_psPool         = VK_NULL_HANDLE;   // 1 pipeline-stats query per slot

    std::vector<Slot>                          m_slots;
    std::vector<uint64_t>                      m_tsResults;        // 2 per slot
    std::vector<PipelineStatsQueryResult>      m_psResults;        // 1 per slot (already in result struct shape)

    uint32_t m_activeSlot       = UINT32_MAX;
    uint32_t m_nextSlot         = 0;
    uint32_t m_oldestPendingSlot= 0;
};

} // namespace visLib

#endif // _WIN32
