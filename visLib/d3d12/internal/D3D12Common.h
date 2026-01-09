// D3D12Common.h: Common includes for D3D12 backend
// Only included when building on Windows with D3D12 support

#ifndef VISLIB_D3D12_COMMON_H
#define VISLIB_D3D12_COMMON_H

#ifdef _WIN32

// Define feature level for required D3D12 features
#define D3D12_FEATURE_LEVEL D3D12_FEATURE_LEVEL_12_0

// Prevent Windows.h min/max macro conflicts
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

// Windows and DirectX headers
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <DirectXColors.h>

// STL headers
#include <memory>
#include <vector>
#include <string>
#include <array>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <exception>
#include <filesystem>

// C++20 headers
#if _MSVC_LANG >= 202002L
#include <numbers>
#include <concepts>
#endif

// Link necessary libraries
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#endif // _WIN32

#endif // VISLIB_D3D12_COMMON_H
