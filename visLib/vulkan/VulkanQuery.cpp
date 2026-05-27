#ifdef _WIN32

#include "VulkanQuery.h"
#include "VulkanWindow.h"
#include <stdexcept>
#include <cstring>

namespace visLib {

namespace {

// Match the pipeline stat fields requested by D3D12 path so backends are comparable.
constexpr VkQueryPipelineStatisticFlags kPipelineStatFlags =
      VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT
    | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT
    | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT
    | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT
    | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT
    | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT
    | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT
    | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT
    | VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

// Number of uint64_t values vkCmdCopyQueryPoolResults would write per stats query —
// must match the bit count of kPipelineStatFlags above.
constexpr uint32_t kPipelineStatFieldCount = 9;

} // namespace

VulkanQuery::VulkanQuery(VulkanWindow* pWindow, QueryCapability capabilities, uint32_t slotCount)
    : m_capabilities(capabilities)
    , m_device(pWindow->getDevice())
    , m_slotCount(slotCount)
    , m_slots(slotCount)
{
    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(pWindow->getPhysicalDevice(), &props);

    if (hasCapability(capabilities, QueryCapability::Timestamps)) {
        if (props.limits.timestampPeriod <= 0.0f) {
            throw std::runtime_error("Device reports no timestamp support");
        }
        // VK timestampPeriod is ns/tick; convert to ticks/second to match D3D12's frequency.
        m_tsFrequency = static_cast<uint64_t>(1.0e9 / props.limits.timestampPeriod);

        VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        ci.queryCount = slotCount * 2;
        if (vkCreateQueryPool(m_device, &ci, nullptr, &m_tsPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateQueryPool (timestamps) failed");
        }
        // Initial reset happens via vkCmdResetQueryPool in beginInternal.
        m_tsResults.assign(slotCount * 2, 0);
    }

    if (hasCapability(capabilities, QueryCapability::PipelineStats)) {
        VkPhysicalDeviceFeatures feats = {};
        vkGetPhysicalDeviceFeatures(pWindow->getPhysicalDevice(), &feats);
        m_pipelineStatsAvailable = (feats.pipelineStatisticsQuery == VK_TRUE);

        if (m_pipelineStatsAvailable) {
            VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            ci.queryType          = VK_QUERY_TYPE_PIPELINE_STATISTICS;
            ci.queryCount         = slotCount;
            ci.pipelineStatistics = kPipelineStatFlags;
            if (vkCreateQueryPool(m_device, &ci, nullptr, &m_psPool) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateQueryPool (pipeline stats) failed");
            }
        }
        m_psResults.assign(slotCount, PipelineStatsQueryResult{});
    }
}

VulkanQuery::~VulkanQuery()
{
    if (m_tsPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_device, m_tsPool, nullptr);
    if (m_psPool != VK_NULL_HANDLE) vkDestroyQueryPool(m_device, m_psPool, nullptr);
}

bool VulkanQuery::isSlotReady(uint32_t slotIdx)
{
    // Poll each pool with vkGetQueryPoolResults (no wait). When a slot's results
    // are available, copy them into our shadow buffers and return true.
    bool tsReady = true;
    bool psReady = true;

    if (m_tsPool != VK_NULL_HANDLE) {
        uint64_t two[2] = { 0, 0 };
        VkResult r = vkGetQueryPoolResults(m_device, m_tsPool, slotIdx * 2, 2,
                                            sizeof(two), two, sizeof(uint64_t),
                                            VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS) {
            m_tsResults[slotIdx * 2 + 0] = two[0];
            m_tsResults[slotIdx * 2 + 1] = two[1];
        } else {
            tsReady = false;
        }
    }

    if (m_psPool != VK_NULL_HANDLE && m_pipelineStatsAvailable) {
        uint64_t stats[kPipelineStatFieldCount] = {};
        VkResult r = vkGetQueryPoolResults(m_device, m_psPool, slotIdx, 1,
                                            sizeof(stats), stats, sizeof(stats),
                                            VK_QUERY_RESULT_64_BIT);
        if (r == VK_SUCCESS) {
            auto& dst = m_psResults[slotIdx];
            dst.inputAssemblerVertices    = stats[0];
            dst.inputAssemblerPrimitives  = stats[1];
            dst.vertexShaderInvocations   = stats[2];
            dst.geometryShaderInvocations = stats[3];
            dst.geometryShaderPrimitives  = stats[4];
            dst.clipperInvocations        = stats[5];
            dst.clipperPrimitives         = stats[6];
            dst.pixelShaderInvocations    = stats[7];
            dst.computeShaderInvocations  = stats[8];
        } else {
            psReady = false;
        }
    }

    return tsReady && psReady;
}

void VulkanQuery::updateSlotStates()
{
    for (uint32_t i = 0; i < m_slotCount; ++i) {
        if (m_slots[i].state == SlotState::Pending) {
            if (isSlotReady(i)) {
                m_slots[i].state = SlotState::Ready;
            }
        }
    }
}

uint32_t VulkanQuery::getReadyCount() const
{
    const_cast<VulkanQuery*>(this)->updateSlotStates();
    uint32_t count = 0;
    for (const auto& s : m_slots) if (s.state == SlotState::Ready) ++count;
    return count;
}

bool VulkanQuery::getTimestampResult(TimestampQueryResult& outResult)
{
    if (!hasCapability(m_capabilities, QueryCapability::Timestamps)) return false;
    updateSlotStates();

    for (uint32_t i = 0; i < m_slotCount; ++i) {
        uint32_t slotIdx = (m_oldestPendingSlot + i) % m_slotCount;
        auto& slot = m_slots[slotIdx];
        if (slot.state != SlotState::Ready) continue;

        outResult.frameIndex     = slot.frameIndex;
        outResult.beginTimestamp = m_tsResults[slotIdx * 2 + 0];
        outResult.endTimestamp   = m_tsResults[slotIdx * 2 + 1];
        outResult.frequency      = m_tsFrequency;

        // Recycle slot. The underlying pool ranges are reset on the GPU in
        // beginInternal via vkCmdResetQueryPool the next time this slot is used.
        slot.state = SlotState::Free;

        if (slotIdx == m_oldestPendingSlot) {
            for (uint32_t j = 1; j < m_slotCount; ++j) {
                uint32_t nextIdx = (m_oldestPendingSlot + j) % m_slotCount;
                if (m_slots[nextIdx].state != SlotState::Free) {
                    m_oldestPendingSlot = nextIdx;
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

bool VulkanQuery::getPipelineStatsResult(PipelineStatsQueryResult& outResult)
{
    if (!hasCapability(m_capabilities, QueryCapability::PipelineStats)) return false;
    if (!m_pipelineStatsAvailable) return false;
    updateSlotStates();

    for (uint32_t i = 0; i < m_slotCount; ++i) {
        uint32_t slotIdx = (m_oldestPendingSlot + i) % m_slotCount;
        auto& slot = m_slots[slotIdx];
        if (slot.state != SlotState::Ready) continue;

        outResult = m_psResults[slotIdx];
        outResult.frameIndex = slot.frameIndex;

        if (m_tsPool != VK_NULL_HANDLE) vkResetQueryPool(m_device, m_tsPool, slotIdx * 2, 2);
        if (m_psPool != VK_NULL_HANDLE) vkResetQueryPool(m_device, m_psPool, slotIdx, 1);
        slot.state = SlotState::Free;

        if (slotIdx == m_oldestPendingSlot) {
            for (uint32_t j = 1; j < m_slotCount; ++j) {
                uint32_t nextIdx = (m_oldestPendingSlot + j) % m_slotCount;
                if (m_slots[nextIdx].state != SlotState::Free) {
                    m_oldestPendingSlot = nextIdx;
                    break;
                }
            }
        }
        return true;
    }
    return false;
}

bool VulkanQuery::beginInternal(VkCommandBuffer cmd, uint64_t frameIndex)
{
    if (m_activeSlot != UINT32_MAX) return false;

    uint32_t freeSlot = UINT32_MAX;
    for (uint32_t i = 0; i < m_slotCount; ++i) {
        uint32_t idx = (m_nextSlot + i) % m_slotCount;
        if (m_slots[idx].state == SlotState::Free) { freeSlot = idx; break; }
    }
    if (freeSlot == UINT32_MAX) return false;

    // Reset this slot's queries on the GPU before reuse (no host-feature dep).
    if (m_tsPool != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(cmd, m_tsPool, freeSlot * 2, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_tsPool, freeSlot * 2);
    }
    if (m_psPool != VK_NULL_HANDLE && m_pipelineStatsAvailable) {
        vkCmdResetQueryPool(cmd, m_psPool, freeSlot, 1);
        vkCmdBeginQuery(cmd, m_psPool, freeSlot, 0);
    }

    m_slots[freeSlot].state      = SlotState::Active;
    m_slots[freeSlot].frameIndex = frameIndex;
    m_activeSlot                 = freeSlot;
    m_nextSlot                   = (freeSlot + 1) % m_slotCount;
    return true;
}

void VulkanQuery::endInternal(VkCommandBuffer cmd)
{
    if (m_activeSlot == UINT32_MAX) return;
    uint32_t slot = m_activeSlot;

    if (m_psPool != VK_NULL_HANDLE && m_pipelineStatsAvailable) {
        vkCmdEndQuery(cmd, m_psPool, slot);
    }
    if (m_tsPool != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_tsPool, slot * 2 + 1);
    }

    m_slots[slot].state = SlotState::Pending;
    m_activeSlot        = UINT32_MAX;
}

} // namespace visLib

#endif // _WIN32
