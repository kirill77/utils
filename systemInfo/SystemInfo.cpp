#include "SystemInfo.h"

#include <Windows.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <locale>
#include <codecvt>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "dxgi.lib")

// ============================================================================
// Helper functions
// ============================================================================

namespace {

std::string wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), 
        static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string result(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()),
        &result[0], sizeNeeded, nullptr, nullptr);
    return result;
}

std::wstring utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, str.c_str(),
        static_cast<int>(str.size()), nullptr, 0);
    std::wstring result(sizeNeeded, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()),
        &result[0], sizeNeeded);
    return result;
}

std::string escapeCSV(const std::string& str) {
    bool needsQuotes = str.find(',') != std::string::npos ||
                       str.find('"') != std::string::npos ||
                       str.find('\n') != std::string::npos;
    if (!needsQuotes) return str;

    std::string result = "\"";
    for (char c : str) {
        if (c == '"') result += "\"\"";
        else result += c;
    }
    result += "\"";
    return result;
}

std::string unescapeCSV(const std::string& str) {
    if (str.empty()) return str;
    if (str.front() != '"') return str;
    if (str.size() < 2) return str;
    
    std::string result;
    for (size_t i = 1; i < str.size() - 1; ++i) {
        if (str[i] == '"' && i + 1 < str.size() - 1 && str[i + 1] == '"') {
            result += '"';
            ++i;
        } else {
            result += str[i];
        }
    }
    return result;
}

std::vector<std::string> parseCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
    }
    fields.push_back(field);
    return fields;
}

std::string trim(const std::string& str) {
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

} // anonymous namespace

// ============================================================================
// GPU Information Collection
// ============================================================================

namespace {

std::vector<GpuInfo> collectGpuInfo() {
    // Use map to deduplicate by VendorId + DeviceId (DXGI can report same GPU multiple times)
    std::unordered_map<uint64_t, GpuInfo> gpuMap;

    ComPtr<IDXGIFactory> pFactory;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), &pFactory);
    if (FAILED(hr) || !pFactory) {
        return {};
    }

    ComPtr<IDXGIAdapter> pAdapter;
    for (UINT i = 0; pFactory->EnumAdapters(i, pAdapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(pAdapter->GetDesc(&desc))) {
            // Skip software adapters
            if (desc.VendorId != 0x1414 || desc.DeviceId != 0x8c) {
                uint64_t key = (static_cast<uint64_t>(desc.VendorId) << 32) | desc.DeviceId;
                if (gpuMap.find(key) != gpuMap.end()) {
                    continue;  // Already have this GPU
                }

                GpuInfo gpu;
                gpu.name = desc.Description;
                gpu.dedicatedVideoMemoryMB = desc.DedicatedVideoMemory / (1024 * 1024);
                gpu.vendorId = desc.VendorId;
                gpu.deviceId = desc.DeviceId;

                // Get driver version using CheckInterfaceSupport
                LARGE_INTEGER driverVersion = {};
                if (SUCCEEDED(pAdapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion))) {
                    WORD parts[4];
                    parts[0] = HIWORD(driverVersion.HighPart);
                    parts[1] = LOWORD(driverVersion.HighPart);
                    parts[2] = HIWORD(driverVersion.LowPart);
                    parts[3] = LOWORD(driverVersion.LowPart);
                    
                    wchar_t versionStr[64];
                    swprintf_s(versionStr, L"%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
                    gpu.driverVersion = versionStr;
                }

                gpuMap[key] = gpu;
            }
        }
    }

    // Convert map to vector
    std::vector<GpuInfo> gpus;
    gpus.reserve(gpuMap.size());
    for (auto& [key, gpu] : gpuMap) {
        gpus.push_back(std::move(gpu));
    }
    return gpus;
}

} // anonymous namespace

// ============================================================================
// CPU Information Collection
// ============================================================================

namespace {

CpuInfo collectCpuInfo() {
    CpuInfo cpu;

    // Get processor count
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cpu.numLogicalProcessors = sysInfo.dwNumberOfProcessors;

    // Get CPU name from registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        wchar_t cpuName[256] = { 0 };
        DWORD bufSize = sizeof(cpuName);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(cpuName), &bufSize) == ERROR_SUCCESS) {
            cpu.name = cpuName;
            // Trim whitespace
            while (!cpu.name.empty() && cpu.name.front() == L' ') {
                cpu.name.erase(cpu.name.begin());
            }
        }
        RegCloseKey(hKey);
    }

    // Get physical core count using GetLogicalProcessorInformation
    DWORD bufferSize = 0;
    GetLogicalProcessorInformation(nullptr, &bufferSize);
    if (bufferSize > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
            bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buffer.data(), &bufferSize)) {
            uint32_t coreCount = 0;
            for (const auto& info : buffer) {
                if (info.Relationship == RelationProcessorCore) {
                    ++coreCount;
                }
            }
            cpu.numCores = coreCount;
        }
    }

    return cpu;
}

} // anonymous namespace

// ============================================================================
// Monitor Information Collection
// ============================================================================

namespace {

struct MonitorEnumContext {
    std::vector<MonitorInfo>* pMonitors;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC /*hdcMonitor*/,
    LPRECT /*lprcMonitor*/, LPARAM dwData) {
    
    MonitorEnumContext* pContext = reinterpret_cast<MonitorEnumContext*>(dwData);
    
    MONITORINFOEXW monitorInfoEx;
    monitorInfoEx.cbSize = sizeof(monitorInfoEx);
    
    if (GetMonitorInfoW(hMonitor, &monitorInfoEx)) {
        MonitorInfo info;
        info.deviceName = monitorInfoEx.szDevice;
        info.bIsPrimary = (monitorInfoEx.dwFlags & MONITORINFOF_PRIMARY) != 0;

        // Get resolution from current display settings
        DEVMODEW devMode;
        devMode.dmSize = sizeof(devMode);
        if (EnumDisplaySettingsW(monitorInfoEx.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
            info.widthPixels = devMode.dmPelsWidth;
            info.heightPixels = devMode.dmPelsHeight;
            info.refreshRateHz = devMode.dmDisplayFrequency;
        }

        // Try to get friendly monitor name from display devices
        DISPLAY_DEVICEW displayDevice;
        displayDevice.cb = sizeof(displayDevice);
        if (EnumDisplayDevicesW(monitorInfoEx.szDevice, 0, &displayDevice, 0)) {
            info.name = displayDevice.DeviceString;
        }

        pContext->pMonitors->push_back(info);
    }

    return TRUE;
}

std::vector<MonitorInfo> collectMonitorInfo() {
    std::vector<MonitorInfo> monitors;
    MonitorEnumContext context;
    context.pMonitors = &monitors;

    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 
        reinterpret_cast<LPARAM>(&context));

    return monitors;
}

} // anonymous namespace

// ============================================================================
// Main Collection Function
// ============================================================================

SystemInfo collectSystemInfo() {
    SystemInfo info;
    info.gpus = collectGpuInfo();
    info.cpu = collectCpuInfo();
    info.monitors = collectMonitorInfo();
    return info;
}

// ============================================================================
// Serialization
// ============================================================================

std::string SystemInfo::toCSV() const {
    std::ostringstream oss;

    // GPU section
    oss << "[GPU]\n";
    oss << "Name,DriverVersion,DedicatedMemoryMB,VendorId,DeviceId\n";
    for (const auto& gpu : gpus) {
        oss << escapeCSV(wstringToUtf8(gpu.name)) << ","
            << escapeCSV(wstringToUtf8(gpu.driverVersion)) << ","
            << gpu.dedicatedVideoMemoryMB << ","
            << gpu.vendorId << ","
            << gpu.deviceId << "\n";
    }

    // CPU section
    oss << "\n[CPU]\n";
    oss << "Name,Cores,LogicalProcessors\n";
    oss << escapeCSV(wstringToUtf8(cpu.name)) << ","
        << cpu.numCores << ","
        << cpu.numLogicalProcessors << "\n";

    // Monitor section
    oss << "\n[Monitor]\n";
    oss << "Name,DeviceName,Width,Height,RefreshHz,IsPrimary\n";
    for (const auto& monitor : monitors) {
        oss << escapeCSV(wstringToUtf8(monitor.name)) << ","
            << escapeCSV(wstringToUtf8(monitor.deviceName)) << ","
            << monitor.widthPixels << ","
            << monitor.heightPixels << ","
            << monitor.refreshRateHz << ","
            << (monitor.bIsPrimary ? 1 : 0) << "\n";
    }

    return oss.str();
}

SystemInfo SystemInfo::fromCSV(const std::string& csvData) {
    SystemInfo info;
    std::istringstream iss(csvData);
    std::string line;
    
    enum class Section { None, GPU, CPU, Monitor };
    Section currentSection = Section::None;
    bool headerSkipped = false;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) {
            headerSkipped = false;
            continue;
        }

        // Check for section markers
        if (line == "[GPU]") {
            currentSection = Section::GPU;
            headerSkipped = false;
            continue;
        } else if (line == "[CPU]") {
            currentSection = Section::CPU;
            headerSkipped = false;
            continue;
        } else if (line == "[Monitor]") {
            currentSection = Section::Monitor;
            headerSkipped = false;
            continue;
        }

        // Skip header row for each section
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }

        auto fields = parseCSVLine(line);

        switch (currentSection) {
        case Section::GPU:
            if (fields.size() >= 5) {
                GpuInfo gpu;
                gpu.name = utf8ToWstring(fields[0]);
                gpu.driverVersion = utf8ToWstring(fields[1]);
                gpu.dedicatedVideoMemoryMB = std::stoull(fields[2]);
                gpu.vendorId = static_cast<uint32_t>(std::stoul(fields[3]));
                gpu.deviceId = static_cast<uint32_t>(std::stoul(fields[4]));
                info.gpus.push_back(gpu);
            }
            break;

        case Section::CPU:
            if (fields.size() >= 3) {
                info.cpu.name = utf8ToWstring(fields[0]);
                info.cpu.numCores = static_cast<uint32_t>(std::stoul(fields[1]));
                info.cpu.numLogicalProcessors = static_cast<uint32_t>(std::stoul(fields[2]));
            }
            break;

        case Section::Monitor:
            if (fields.size() >= 6) {
                MonitorInfo monitor;
                monitor.name = utf8ToWstring(fields[0]);
                monitor.deviceName = utf8ToWstring(fields[1]);
                monitor.widthPixels = static_cast<uint32_t>(std::stoul(fields[2]));
                monitor.heightPixels = static_cast<uint32_t>(std::stoul(fields[3]));
                monitor.refreshRateHz = static_cast<uint32_t>(std::stoul(fields[4]));
                monitor.bIsPrimary = (fields[5] == "1");
                info.monitors.push_back(monitor);
            }
            break;

        default:
            break;
        }
    }

    return info;
}

bool SystemInfo::saveToFile(const std::string& filename) const {
    std::ofstream file(filename, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << toCSV();
    return file.good();
}

SystemInfo SystemInfo::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return SystemInfo();
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return fromCSV(oss.str());
}
