#include "DisplayModeHelper.h"

#ifdef _WIN32

#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <dxgi.h>
#include <nvapi.h>
#include "utils/log/ILog.h"

// ---------------------------------------------------------------------------
// RefreshRateControl
// ---------------------------------------------------------------------------

uint32_t RefreshRateControl::get()
{
    DEVMODEW devMode = {};
    devMode.dmSize = sizeof(devMode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &devMode))
        return 0;
    return devMode.dmDisplayFrequency;
}

bool RefreshRateControl::set(uint32_t refreshRateHz)
{
    DEVMODEW devMode = {};
    devMode.dmSize = sizeof(devMode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &devMode)) {
        return false;
    }
    if (devMode.dmDisplayFrequency == refreshRateHz) {
        return true;
    }
    devMode.dmDisplayFrequency = refreshRateHz;
    devMode.dmFields = DM_DISPLAYFREQUENCY;
    return ChangeDisplaySettingsW(&devMode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
}

// ---------------------------------------------------------------------------
// GSyncControl
// ---------------------------------------------------------------------------

bool GSyncControl::get()
{
    if (!ensureDisplayId())
        return false;

    NV_GET_ADAPTIVE_SYNC_DATA getData = {};
    getData.version = NV_GET_ADAPTIVE_SYNC_DATA_VER;
    if (NvAPI_DISP_GetAdaptiveSyncData(m_displayId, &getData) != NVAPI_OK)
        return false;

    return getData.bDisableAdaptiveSync == 0;
}

bool GSyncControl::set(bool enable)
{
    const char* desired = enable ? "enabled" : "disabled";
    LOG_INFO("GSync: requesting %s (displayId=0x%x)", desired, m_displayId);

    if (!ensureDisplayId()) {
        LOG_WARN("GSync: ensureDisplayId failed");
        return false;
    }

    // Read current state so we preserve maxFrameInterval and other fields
    NV_GET_ADAPTIVE_SYNC_DATA getData = {};
    getData.version = NV_GET_ADAPTIVE_SYNC_DATA_VER;
    NvAPI_Status getStatus = NvAPI_DISP_GetAdaptiveSyncData(m_displayId, &getData);
    if (getStatus != NVAPI_OK) {
        LOG_WARN("GSync: GetAdaptiveSyncData failed (status=%d)", (int)getStatus);
        return false;
    }

    NvU32 wantDisabled = enable ? 0 : 1;
    LOG_INFO("GSync: current bDisableAdaptiveSync=%u, want=%u, maxFrameInterval=%u",
             getData.bDisableAdaptiveSync, wantDisabled, getData.maxFrameInterval);

    if (getData.bDisableAdaptiveSync == wantDisabled) {
        LOG_INFO("GSync: already %s, nothing to do", desired);
        return true;
    }

    // Write back with only the disable flag changed
    NV_SET_ADAPTIVE_SYNC_DATA setData = {};
    setData.version = NV_SET_ADAPTIVE_SYNC_DATA_VER;
    setData.maxFrameInterval = getData.maxFrameInterval;
    setData.bDisableAdaptiveSync = wantDisabled;
    setData.bDisableFrameSplitting = getData.bDisableFrameSplitting;

    NvAPI_Status setStatus = NvAPI_DISP_SetAdaptiveSyncData(m_displayId, &setData);
    if (setStatus != NVAPI_OK) {
        LOG_WARN("GSync: SetAdaptiveSyncData failed (status=%d)", (int)setStatus);
        return false;
    }

    LOG_INFO("GSync: successfully set to %s", desired);
    return true;
}

bool GSyncControl::ensureDisplayId()
{
    if (m_displayId != 0) return true;

    if (NvAPI_Initialize() != NVAPI_OK) return false;

    // Get GDI device name for the primary monitor
    HMONITOR hPrimary = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW monInfo = {};
    monInfo.cbSize = sizeof(monInfo);
    if (!GetMonitorInfoW(hPrimary, &monInfo)) {
        NvAPI_Unload();
        return false;
    }
    std::wstring primaryDev = monInfo.szDevice;

    // Enumerate NVAPI GPUs -> displays, match by GDI name
    NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvU32 gpuCount = 0;
    if (NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount) != NVAPI_OK || gpuCount == 0) {
        NvAPI_Unload();
        return false;
    }

    for (NvU32 g = 0; g < gpuCount && m_displayId == 0; ++g) {
        NvU32 displayCount = 0;
        if (NvAPI_GPU_GetAllDisplayIds(gpuHandles[g], nullptr, &displayCount) != NVAPI_OK || displayCount == 0)
            continue;

        std::vector<NV_GPU_DISPLAYIDS> displayIds(displayCount);
        for (auto& d : displayIds) d.version = NV_GPU_DISPLAYIDS_VER;
        if (NvAPI_GPU_GetAllDisplayIds(gpuHandles[g], displayIds.data(), &displayCount) != NVAPI_OK)
            continue;

        for (NvU32 i = 0; i < displayCount && m_displayId == 0; ++i) {
            if (!displayIds[i].isActive) continue;

            // Map NVAPI logical displays to GDI names
            NvDisplayHandle hDisplay = nullptr;
            for (NvU32 e = 0; NvAPI_EnumNvidiaDisplayHandle(e, &hDisplay) == NVAPI_OK; ++e) {
                NvAPI_ShortString name = {};
                if (NvAPI_GetAssociatedNvidiaDisplayName(hDisplay, name) != NVAPI_OK)
                    continue;
                std::wstring nvapiDev(name, name + std::strlen(name));
                if (primaryDev == nvapiDev) {
                    m_displayId = displayIds[i].displayId;
                    break;
                }
            }
        }
    }

    if (m_displayId == 0) {
        NvAPI_Unload();
        return false;
    }
    return true;
}

#endif // _WIN32
