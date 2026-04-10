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
 * @brief Gets or sets G-Sync (adaptive sync) state on the primary monitor via NVAPI.
 *
 * Caches the NVAPI displayId after the first successful call.
 * Does NOT restore original state — by design the system stays in
 * whatever state the last test left it, to minimise display switching.
 */
class GSyncControl {
public:
    GSyncControl() = default;
    GSyncControl(const GSyncControl&) = delete;
    GSyncControl& operator=(const GSyncControl&) = delete;

    /// @return true if G-Sync is currently enabled, false if disabled or on failure.
    bool get();

    /// @brief Enable or disable G-Sync on the primary monitor.
    /// @return true if the state was set (or was already in the desired state).
    bool set(bool enable);

private:
    bool ensureDisplayId();

    uint32_t m_displayId = 0;
};

#endif // _WIN32
