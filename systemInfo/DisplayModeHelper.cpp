#include "DisplayModeHelper.h"

#ifdef _WIN32

#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include <dxgi.h>
#include <nvapi.h>
#include <NvApiDriverSettings.h>
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
// GSyncControl — VRR_MODE in a per-app DRS profile keyed on slVerdict.exe.
// ---------------------------------------------------------------------------

// VRR_MODE is read kernel-side from g_GlobalData.pClientArbitration->m_vrrMode,
// which points into the resolved base-profile settings array. Per-app DRS
// overrides do not surface through that global pointer, so the fullscreen
// iFlip VRR engagement gate is governed by the base profile only.

bool GSyncControl::get()
{
    if (NvAPI_Initialize() != NVAPI_OK) return false;

    NvDRSSessionHandle hSession = nullptr;
    if (NvAPI_DRS_CreateSession(&hSession) != NVAPI_OK) return false;

    bool isOn = false;
    if (NvAPI_DRS_LoadSettings(hSession) == NVAPI_OK) {
        NvDRSProfileHandle hBase = nullptr;
        if (NvAPI_DRS_GetBaseProfile(hSession, &hBase) == NVAPI_OK) {
            NVDRS_SETTING setting{};
            setting.version = NVDRS_SETTING_VER;
            if (NvAPI_DRS_GetSetting(hSession, hBase, VRR_MODE_ID, &setting) == NVAPI_OK) {
                isOn = (setting.u32CurrentValue != VRR_MODE_DISABLED);
            }
        }
    }
    NvAPI_DRS_DestroySession(hSession);
    return isOn;
}

namespace {

// Write one DWORD DRS setting to the base profile (system-wide). Shared by
// GSyncControl and DrsKeyControl; `tag` prefixes the log lines.
bool drsWriteBaseProfileDword(const char* tag, NvU32 settingId, NvU32 value)
{
    if (NvAPI_Initialize() != NVAPI_OK) {
        LOG_WARN("%s: NvAPI_Initialize failed", tag);
        return false;
    }

    NvDRSSessionHandle hSession = nullptr;
    if (NvAPI_DRS_CreateSession(&hSession) != NVAPI_OK) {
        LOG_WARN("%s: NvAPI_DRS_CreateSession failed", tag);
        return false;
    }

    bool ok = false;
    do {
        if (NvAPI_DRS_LoadSettings(hSession) != NVAPI_OK) {
            LOG_WARN("%s: NvAPI_DRS_LoadSettings failed", tag);
            break;
        }

        NvDRSProfileHandle hBase = nullptr;
        if (NvAPI_DRS_GetBaseProfile(hSession, &hBase) != NVAPI_OK) {
            LOG_WARN("%s: NvAPI_DRS_GetBaseProfile failed", tag);
            break;
        }

        NVDRS_SETTING setting{};
        setting.version         = NVDRS_SETTING_VER;
        setting.settingId       = settingId;
        setting.settingType     = NVDRS_DWORD_TYPE;
        setting.u32CurrentValue = value;

        NvAPI_Status st = NvAPI_DRS_SetSetting(hSession, hBase, &setting);
        if (st != NVAPI_OK) {
            LOG_WARN("%s: NvAPI_DRS_SetSetting(0x%08x) failed (status=%d)", tag, settingId, (int)st);
            break;
        }

        st = NvAPI_DRS_SaveSettings(hSession);
        if (st != NVAPI_OK) {
            LOG_WARN("%s: NvAPI_DRS_SaveSettings failed (status=%d)", tag, (int)st);
            break;
        }

        ok = true;
    } while (false);

    NvAPI_DRS_DestroySession(hSession);
    return ok;
}

} // namespace

bool GSyncControl::set(bool enable)
{
    const NvU32 desired = enable ? VRR_MODE_FULLSCREEN_AND_WINDOWED : VRR_MODE_DISABLED;
    const char* desiredStr = enable ? "FULLSCREEN_AND_WINDOWED" : "DISABLED";

    LOG_INFO("GSync: writing VRR_MODE=%s to base DRS profile (system-wide)", desiredStr);
    if (!drsWriteBaseProfileDword("GSync", VRR_MODE_ID, desired)) return false;
    LOG_INFO("GSync: VRR_MODE=%s persisted to base profile", desiredStr);
    return true;
}

// ---------------------------------------------------------------------------
// DrsKeyControl — arbitrary DWORD DRS setting in the base profile.
// ---------------------------------------------------------------------------

bool DrsKeyControl::setKey(uint32_t settingId, uint32_t value)
{
    LOG_INFO("DrsKey: writing setting 0x%08x=0x%x to base DRS profile (system-wide)", settingId, value);
    if (!drsWriteBaseProfileDword("DrsKey", static_cast<NvU32>(settingId), static_cast<NvU32>(value))) return false;
    LOG_INFO("DrsKey: setting 0x%08x=0x%x persisted to base profile", settingId, value);
    return true;
}

#endif // _WIN32
