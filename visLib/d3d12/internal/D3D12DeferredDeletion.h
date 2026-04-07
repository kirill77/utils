#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include <vector>

namespace visLib {

// Deferred deletion queue for D3D12 GPU resources.
// Resources are held alive until the GPU has finished using them,
// as determined by a fence value.
class D3D12DeferredDeletion
{
public:
    // Enqueue a resource for deferred release. It will be released only
    // after the GPU completes work up to fenceValue.
    void deferRelease(Microsoft::WRL::ComPtr<IUnknown> resource, uint64_t fenceValue);

    // Release resources whose fence values have been reached by the GPU.
    void cleanup(ID3D12Fence* pFence);

    // Release everything unconditionally (call after flush at shutdown).
    void releaseAll();

private:
    struct DeferredResource
    {
        Microsoft::WRL::ComPtr<IUnknown> resource;
        uint64_t fenceValue;
    };
    std::vector<DeferredResource> m_pending;
};

} // namespace visLib

#endif // _WIN32
