#pragma once

#ifdef _WIN32
#include <cstdint>

/**
 * @class RefreshRateControl
 * @brief Gets or sets the desktop refresh rate on the primary monitor.
 *
 * Uses CDS_FULLSCREEN so the change is temporary and auto-reverts
 * if the process exits or crashes.
 */
class RefreshRateControl {
public:
    RefreshRateControl() = default;
    RefreshRateControl(const RefreshRateControl&) = delete;
    RefreshRateControl& operator=(const RefreshRateControl&) = delete;

    /// @return Current refresh rate in Hz, or 0 on failure.
    uint32_t get();

    /// @brief Set the primary monitor's desktop refresh rate.
    /// @return true if the display mode was changed successfully.
    bool set(uint32_t refreshRateHz);
};

/**
 * @class GSyncControl
 * @brief Gets or sets G-Sync state by writing VRR_MODE into the base
 *        (system-wide) DRS profile.
 *
 * Per-monitor NvAPI_DISP_SetAdaptiveSyncData only updates the NVCPL checkbox
 * state; it does NOT gate the driver's fullscreen-iFlip VRR engagement —
 * which is what Streamline observes via NvAPI_D3D_GetSleepStatus. The DRS
 * setting VRR_MODE does gate that engagement, but the kernel reads it from
 * the resolved base profile only — per-app overrides never reach that gate —
 * so the write has to be system-wide.
 *
 * Does NOT restore original state — the base-profile value persists across
 * the test session (and process death); callers re-write their desired state
 * every run.
 */
class GSyncControl {
public:
    GSyncControl() = default;
    GSyncControl(const GSyncControl&) = delete;
    GSyncControl& operator=(const GSyncControl&) = delete;

    /// @return true if VRR_MODE in the slVerdict.exe profile is non-zero,
    /// false if it is DISABLED, not yet set, or on failure.
    bool get();

    /// @brief Set VRR_MODE in the slVerdict.exe per-app DRS profile.
    /// @param enable true => FULLSCREEN_AND_WINDOWED, false => DISABLED.
    /// @return true on success.
    bool set(bool enable);
};

/**
 * @class DrsKeyControl
 * @brief Writes an arbitrary DWORD driver setting (DRS key) by raw setting id,
 *        either to the base profile or to a per-app profile.
 *
 * Generic escape hatch for settings that have no named accessor here (e.g.
 * ids absent from the public NvApiDriverSettings.h) — the caller supplies the
 * id and value. NOT restored on exit: callers re-write their desired state
 * every run. Prefer setAppKey for settings the UMD resolves per application —
 * it leaves the system-wide base profile untouched, so a crashed run cannot
 * leak the setting into other applications.
 */
class DrsKeyControl {
public:
    DrsKeyControl() = default;
    DrsKeyControl(const DrsKeyControl&) = delete;
    DrsKeyControl& operator=(const DrsKeyControl&) = delete;

    /// @brief Write a DWORD setting to the base DRS profile (system-wide).
    /// @param settingId Raw DRS setting id.
    /// @param value     DWORD value to write.
    /// @return true on success.
    bool setKey(uint32_t settingId, uint32_t value);

    /// @brief Write a DWORD setting to the per-app DRS profile keyed on
    ///        exeName, creating the profile and application entry if missing.
    ///        Only effective for settings resolved per application (UMD-read
    ///        keys); kernel-global settings ignore per-app profiles.
    /// @param exeName   Executable name the profile applies to (e.g. "app.exe").
    /// @param settingId Raw DRS setting id.
    /// @param value     DWORD value to write.
    /// @return true on success.
    bool setAppKey(const char* exeName, uint32_t settingId, uint32_t value);
};

#endif // _WIN32
