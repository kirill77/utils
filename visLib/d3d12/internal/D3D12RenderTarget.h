#pragma once

#ifdef _WIN32

#include "D3D12Common.h"
#include <wrl/client.h>

namespace visLib {

// Render target info for D3D12 rendering operations
// Can be populated from D3D12SwapChain (desktop) or OpenXRSession (VR)
struct D3D12RenderTarget
{
    uint32_t width = 0;
    uint32_t height = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};

    // Resource for barrier management
    // nullptr = caller manages barriers (render target already in correct state)
    Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
};

} // namespace visLib

#endif // _WIN32
