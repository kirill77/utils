#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dxgi.h>
#include <d3d12.h>
#include <memory>

#include "utils/visLib/include/IWindow.h"

namespace visLib {

class D3D12Window;

// Optional overrides for D3D12/DXGI creation functions.
// When set, these are used instead of the standard Windows APIs, allowing
// an interposer (e.g., Streamline) to proxy device and factory creation.
struct D3D12CreationOverrides {
    using FnCreateDXGIFactory2 = HRESULT(WINAPI*)(UINT Flags, REFIID riid, void** ppFactory);
    using FnD3D12CreateDevice  = HRESULT(WINAPI*)(IUnknown* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid, void** ppDevice);

    FnCreateDXGIFactory2 pfnCreateDXGIFactory2 = nullptr;
    FnD3D12CreateDevice  pfnD3D12CreateDevice  = nullptr;
};

// D3D12-specific window configuration. Passed alongside the platform-agnostic
// WindowConfig to backend-specific factories that need D3D12 interposer hooks.
struct D3D12WindowConfig {
    D3D12CreationOverrides creationOverrides;
};

// D3D12-specific window factory. Returns the concrete D3D12Window type so
// downstream peer factories (e.g. createD3D12Renderer) can be statically
// type-checked against the matching backend.
std::unique_ptr<D3D12Window> createD3D12Window(const WindowConfig& config,
                                                const D3D12WindowConfig& d3dConfig = {});

} // namespace visLib

#endif // _WIN32
