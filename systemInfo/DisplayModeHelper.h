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
 * @brief Gets or sets G-Sync state by writing VRR_MODE into a per-app DRS
 *        profile keyed on slVerdict.exe.
 *
 * Per-monitor NvAPI_DISP_SetAdaptiveSyncData only updates the NVCPL checkbox
 * state; it does NOT gate the driver's fullscreen-iFlip VRR engagement —
 * which is what Streamline observes via NvAPI_D3D_GetSleepStatus. The DRS
 * setting VRR_MODE does gate that engagement. Writing it to a per-app
 * profile leaves the user's base profile untouched.
 *
 * Does NOT restore original state — the per-app profile is left in place
 * across the test session.
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
 * @brief Writes an arbitrary DWORD driver setting (DRS key) to the base
 *        profile, by raw setting id.
 *
 * Generic escape hatch for settings that have no named accessor here (e.g.
 * ids absent from the public NvApiDriverSettings.h) — the caller supplies the
 * id and value. Written to the base profile (system-wide) and, like
 * GSyncControl, NOT restored on exit: callers re-write their desired state
 * every run.
 */
class DrsKeyControl {
public:
    DrsKeyControl() = default;
    DrsKeyControl(const DrsKeyControl&) = delete;
    DrsKeyControl& operator=(const DrsKeyControl&) = delete;

    /// @brief Write a DWORD setting to the base DRS profile.
    /// @param settingId Raw DRS setting id.
    /// @param value     DWORD value to write.
    /// @return true on success.
    bool setKey(uint32_t settingId, uint32_t value);
};

#endif // _WIN32
