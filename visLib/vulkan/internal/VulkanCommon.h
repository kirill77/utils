#pragma once

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR

#include <Windows.h>
#include <vulkan/vulkan.h>

#pragma comment(lib, "vulkan-1.lib")

#endif // _WIN32
