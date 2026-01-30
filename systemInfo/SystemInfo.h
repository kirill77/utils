#pragma once

#include <string>
#include <vector>
#include <cstdint>

/**
 * @struct GpuInfo
 * @brief Information about a GPU installed in the system
 */
struct GpuInfo {
    std::wstring name;                  ///< GPU name (e.g., "NVIDIA GeForce RTX 4090")
    std::wstring driverVersion;         ///< Driver version string (e.g., "31.0.15.5050")
    size_t dedicatedVideoMemoryMB = 0;  ///< Dedicated video memory in megabytes
    uint32_t vendorId = 0;              ///< PCI vendor ID
    uint32_t deviceId = 0;              ///< PCI device ID
};

/**
 * @struct CpuInfo
 * @brief Information about the CPU installed in the system
 */
struct CpuInfo {
    std::wstring name;                      ///< CPU name (e.g., "Intel Core i9-13900K")
    uint32_t numCores = 0;                  ///< Number of physical cores
    uint32_t numLogicalProcessors = 0;      ///< Number of logical processors (threads)
};

/**
 * @struct MonitorInfo
 * @brief Information about a monitor connected to the system
 */
struct MonitorInfo {
    std::wstring name;              ///< Monitor name/model
    std::wstring deviceName;        ///< Device name (e.g., "\\\\.\\DISPLAY1")
    uint32_t widthPixels = 0;       ///< Horizontal resolution in pixels
    uint32_t heightPixels = 0;      ///< Vertical resolution in pixels
    uint32_t refreshRateHz = 0;     ///< Refresh rate in Hz
    bool bIsPrimary = false;        ///< True if this is the primary monitor
};

/**
 * @struct SystemInfo
 * @brief Aggregated information about the system hardware
 */
struct SystemInfo {
    std::vector<GpuInfo> gpus;          ///< List of GPUs
    CpuInfo cpu;                        ///< CPU information
    std::vector<MonitorInfo> monitors;  ///< List of monitors

    /**
     * @brief Serialize the system info to a CSV-formatted string
     * @return CSV string representation of the system info
     */
    std::string toCSV() const;

    /**
     * @brief Deserialize system info from a CSV-formatted string
     * @param csvData The CSV string to parse
     * @return Parsed SystemInfo structure
     */
    static SystemInfo fromCSV(const std::string& csvData);

    /**
     * @brief Save system info to a CSV file
     * @param filename Path to the file to write
     * @return true if successful, false otherwise
     */
    bool saveToFile(const std::string& filename) const;

    /**
     * @brief Load system info from a CSV file
     * @param filename Path to the file to read
     * @return Parsed SystemInfo structure (empty if file cannot be read)
     */
    static SystemInfo loadFromFile(const std::string& filename);
};

/**
 * @brief Collect information about the current system's hardware
 * @return SystemInfo structure populated with GPU, CPU, and monitor information
 */
SystemInfo collectSystemInfo();
