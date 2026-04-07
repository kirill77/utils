#pragma once

#ifdef _WIN32
#include <cstdint>
#include <windows.h>

/**
 * @class DisplayModeGuard
 * @brief RAII guard that temporarily changes the desktop refresh rate.
 *
 * Uses CDS_FULLSCREEN so the change is temporary and auto-reverts
 * if the process exits or crashes without an explicit restore.
 */
class DisplayModeGuard {
public:
    ~DisplayModeGuard() { restore(); }

    DisplayModeGuard() = default;
    DisplayModeGuard(const DisplayModeGuard&) = delete;
    DisplayModeGuard& operator=(const DisplayModeGuard&) = delete;

    /**
     * @brief Change the primary monitor's desktop refresh rate.
     * @param refreshRateHz Target refresh rate in Hz.
     * @return true if the display mode was changed successfully.
     */
    bool apply(uint32_t refreshRateHz)
    {
        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(devMode);
        // Get current settings to preserve resolution
        if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &devMode)) {
            return false;
        }
        // Skip if already at the requested frequency
        if (devMode.dmDisplayFrequency == refreshRateHz) {
            return true;
        }
        devMode.dmDisplayFrequency = refreshRateHz;
        devMode.dmFields = DM_DISPLAYFREQUENCY;
        LONG result = ChangeDisplaySettingsW(&devMode, CDS_FULLSCREEN);
        m_applied = (result == DISP_CHANGE_SUCCESSFUL);
        return m_applied;
    }

    /**
     * @brief Restore the original display settings. Safe to call multiple times.
     */
    void restore()
    {
        if (m_applied) {
            ChangeDisplaySettingsW(nullptr, 0);
            m_applied = false;
        }
    }

    bool isApplied() const { return m_applied; }

private:
    bool m_applied = false;
};

#endif // _WIN32
