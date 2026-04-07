#ifdef _WIN32

#include "D3D12DeferredDeletion.h"

namespace visLib {

void D3D12DeferredDeletion::deferRelease(Microsoft::WRL::ComPtr<IUnknown> resource, uint64_t fenceValue)
{
    if (resource)
    {
        m_pending.push_back({ std::move(resource), fenceValue });
    }
}

void D3D12DeferredDeletion::cleanup(ID3D12Fence* pFence)
{
    if (!pFence || m_pending.empty())
        return;

    uint64_t completedValue = pFence->GetCompletedValue();

    m_pending.erase(
        std::remove_if(m_pending.begin(), m_pending.end(),
            [completedValue](const DeferredResource& entry) {
                return completedValue >= entry.fenceValue;
            }),
        m_pending.end());
}

void D3D12DeferredDeletion::releaseAll()
{
    m_pending.clear();
}

} // namespace visLib

#endif // _WIN32
