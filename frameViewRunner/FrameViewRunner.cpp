#include "FrameViewRunner.h"
#include "utils/processManager/ProcessManager.h"
#include "utils/systemInfo/SystemInfo.h"
#include "utils/log/ILog.h"

#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace
{
    // Common FrameView installation paths to check as fallback
    const std::vector<std::filesystem::path> kCommonFrameViewPaths = {
        "C:/Program Files/NVIDIA Corporation/FrameView",
        "C:/Program Files (x86)/NVIDIA Corporation/FrameView"
    };

    // Processes to kill for clean FrameView state
    const std::vector<std::string> kFrameViewProcesses = {
        "FrameView_x64.exe",
        "PresentMon_x64.exe",
        "EnableVROverlay_x64.exe"
    };

    std::filesystem::path getTempBasePath()
    {
        // Use TEMP environment variable
        char* tempDir = nullptr;
        size_t len = 0;
        if (_dupenv_s(&tempDir, &len, "TEMP") == 0 && tempDir != nullptr) {
            std::filesystem::path result = std::filesystem::path(tempDir) / "FrameViewRunner";
            free(tempDir);
            return result;
        }
        // Fallback
        return std::filesystem::path("C:/Temp/FrameViewRunner");
    }
}

FrameViewRunner::FrameViewRunner()
{
    LOG_INFO("FrameViewRunner: Initializing...");

    try {
        // Step 1: Kill any existing FrameView processes
        killFrameViewProcesses();

        // Step 2: Find FrameView installation
        std::filesystem::path frameViewInstallPath = findFrameViewInstallation();
        if (frameViewInstallPath.empty()) {
            m_error = "FrameView installation not found";
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return;
        }
        LOG_INFO("FrameViewRunner: Found FrameView at: %s", frameViewInstallPath.string().c_str());

        // Step 3: Set up paths
        std::filesystem::path tempBase = getTempBasePath();
        m_frameViewCopyPath = tempBase / "FrameView";
        m_outputDirectory = tempBase / "Results";

        // Step 4: Copy FrameView to isolated location
        if (!prepareFrameViewCopy(frameViewInstallPath)) {
            return; // Error already set
        }

        // Step 5: Modify Settings.ini
        if (!modifyIniFile()) {
            return; // Error already set
        }

        // Step 6: Launch FrameView
        if (!launchFrameView()) {
            return; // Error already set
        }

        m_valid = true;
        LOG_INFO("FrameViewRunner: Successfully initialized");

    } catch (const std::exception& e) {
        m_error = std::string("Exception during initialization: ") + e.what();
        LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
    }
}

FrameViewRunner::~FrameViewRunner()
{
    LOG_INFO("FrameViewRunner: Shutting down...");
    killFrameViewProcesses();
}

std::filesystem::path FrameViewRunner::findFrameViewInstallation()
{
    // First, try the registry via InstalledAppRegistry
    InstalledAppRegistry registry;
    auto frameViewInfo = registry.find(L"FrameView");
    if (frameViewInfo.has_value() && !frameViewInfo->installLocation.empty()) {
        std::filesystem::path installPath(frameViewInfo->installLocation);
        if (std::filesystem::exists(installPath / "FrameView_x64.exe")) {
            LOG_INFO("FrameViewRunner: Found FrameView via registry at: %s", 
                     installPath.string().c_str());
            m_installPath = installPath;
            // Convert wide string version to narrow string
            if (!frameViewInfo->version.empty()) {
                m_version = std::string(frameViewInfo->version.begin(), frameViewInfo->version.end());
            }
            return installPath;
        }
    }

    // Fallback: check common installation paths
    for (const auto& commonPath : kCommonFrameViewPaths) {
        if (std::filesystem::exists(commonPath / "FrameView_x64.exe")) {
            LOG_INFO("FrameViewRunner: Found FrameView at common path: %s", 
                     commonPath.string().c_str());
            m_installPath = commonPath;
            // Version unknown when found via fallback path
            return commonPath;
        }
    }

    return {};
}

void FrameViewRunner::killFrameViewProcesses()
{
    ProcessManager processManager;

    LOG_INFO("FrameViewRunner: Checking for running FrameView processes...");

    for (const std::string& processName : kFrameViewProcesses) {
        int killedCount = 0;

        // Keep killing until no more instances found
        while (true) {
            ProcessManager::ProcessInfo processInfo = processManager.findProcessWithImage(processName);
            if (processInfo.isValid()) {
                LOG_INFO("FrameViewRunner: Found running process: %s (ID: %u) - terminating...",
                    processInfo.imageName.c_str(), processInfo.id);
                if (processManager.killProcess(processInfo)) {
                    killedCount++;
                    LOG_INFO("FrameViewRunner: Successfully terminated %s (instance %d)", 
                             processName.c_str(), killedCount);
                } else {
                    LOG_WARN("FrameViewRunner: Failed to terminate %s (ID: %u)", 
                             processName.c_str(), processInfo.id);
                    break; // Avoid infinite loop
                }
            } else {
                break; // No more instances
            }
        }

        if (killedCount > 0) {
            LOG_INFO("FrameViewRunner: Total %s instances terminated: %d", 
                     processName.c_str(), killedCount);
        }
    }

    LOG_INFO("FrameViewRunner: Process cleanup completed");
}

bool FrameViewRunner::prepareFrameViewCopy(const std::filesystem::path& sourceDir)
{
    LOG_INFO("FrameViewRunner: Preparing FrameView copy...");

    try {
        // Remove existing copy if present
        if (std::filesystem::exists(m_frameViewCopyPath)) {
            std::uintmax_t removedCount = std::filesystem::remove_all(m_frameViewCopyPath);
            LOG_INFO("FrameViewRunner: Removed existing copy (%llu items)", removedCount);
        }

        // Create fresh directories
        std::filesystem::create_directories(m_frameViewCopyPath);
        std::filesystem::create_directories(m_outputDirectory);

        LOG_INFO("FrameViewRunner: Copying FrameView from: %s", sourceDir.string().c_str());
        LOG_INFO("FrameViewRunner: Copying FrameView to: %s", m_frameViewCopyPath.string().c_str());

        // Copy all contents
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
            const auto& sourcePath = entry.path();
            auto relativePath = std::filesystem::relative(sourcePath, sourceDir);
            auto destPath = m_frameViewCopyPath / relativePath;

            if (entry.is_directory()) {
                std::filesystem::create_directories(destPath);
            } else if (entry.is_regular_file()) {
                std::filesystem::create_directories(destPath.parent_path());
                std::filesystem::copy_file(sourcePath, destPath, 
                                           std::filesystem::copy_options::overwrite_existing);
            }
        }

        LOG_INFO("FrameViewRunner: Successfully copied FrameView");
        return true;

    } catch (const std::filesystem::filesystem_error& e) {
        m_error = std::string("Failed to copy FrameView: ") + e.what();
        LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
        return false;
    }
}

bool FrameViewRunner::modifyIniFile()
{
    LOG_INFO("FrameViewRunner: Modifying Settings.ini...");

    try {
        std::filesystem::path settingsIniPath = m_frameViewCopyPath / "Settings.ini";

        if (!std::filesystem::exists(settingsIniPath)) {
            m_error = "Settings.ini not found at: " + settingsIniPath.string();
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return false;
        }

        // Read existing content
        std::ifstream inputFile(settingsIniPath);
        if (!inputFile.is_open()) {
            m_error = "Failed to open Settings.ini for reading";
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return false;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(inputFile, line)) {
            // Skip lines we want to replace
            if (line.find("CaptureOnLaunchDurationInSeconds") != std::string::npos) {
                LOG_INFO("FrameViewRunner: Removing existing CaptureOnLaunchDurationInSeconds");
                continue;
            }
            if (line.find("BenchmarkDirectory") != std::string::npos) {
                LOG_INFO("FrameViewRunner: Removing existing BenchmarkDirectory");
                continue;
            }
            lines.push_back(line);
        }
        inputFile.close();

        // Add our settings
        lines.push_back("CaptureOnLaunchDurationInSeconds=1");
        
        // Convert output directory to string (use forward slashes for consistency)
        std::string outputDirStr = m_outputDirectory.string();
        std::replace(outputDirStr.begin(), outputDirStr.end(), '\\', '/');
        lines.push_back("BenchmarkDirectory=" + outputDirStr);

        LOG_INFO("FrameViewRunner: Setting BenchmarkDirectory=%s", outputDirStr.c_str());

        // Write modified content
        std::ofstream outputFile(settingsIniPath);
        if (!outputFile.is_open()) {
            m_error = "Failed to open Settings.ini for writing";
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return false;
        }

        for (const std::string& modifiedLine : lines) {
            outputFile << modifiedLine << std::endl;
        }
        outputFile.close();

        LOG_INFO("FrameViewRunner: Successfully modified Settings.ini");
        return true;

    } catch (const std::exception& e) {
        m_error = std::string("Failed to modify Settings.ini: ") + e.what();
        LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
        return false;
    }
}

bool FrameViewRunner::launchFrameView()
{
    ProcessManager processManager;
    std::filesystem::path frameViewExePath = m_frameViewCopyPath / "FrameView_x64.exe";

    LOG_INFO("FrameViewRunner: Launching FrameView from: %s", frameViewExePath.string().c_str());

    try {
        ProcessManager::ProcessInfo processInfo = processManager.startProcess(
            frameViewExePath.string(), "");
        
        if (processInfo.isValid()) {
            LOG_INFO("FrameViewRunner: Successfully launched FrameView (PID: %u)", processInfo.id);
            return true;
        } else {
            m_error = "Failed to launch FrameView - invalid process info";
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return false;
        }

    } catch (const std::exception& e) {
        std::string errorMsg = e.what();

        // Check if elevation is required (error code 740)
        if (errorMsg.find("Error code: 740") != std::string::npos) {
            LOG_WARN("FrameViewRunner: FrameView requires elevation, attempting elevated launch...");

            try {
                ProcessManager::ProcessInfo processInfo = processManager.startProcessElevated(
                    frameViewExePath.string(), "");
                
                if (processInfo.isValid()) {
                    LOG_INFO("FrameViewRunner: Successfully launched FrameView with elevation (PID: %u)", 
                             processInfo.id);
                    return true;
                } else {
                    m_error = "Failed to launch FrameView with elevation - invalid process info";
                    LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
                    return false;
                }

            } catch (const std::exception& elevatedError) {
                m_error = std::string("Failed to launch FrameView with elevation: ") + elevatedError.what();
                LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
                return false;
            }
        } else {
            m_error = std::string("Failed to launch FrameView: ") + e.what();
            LOG_ERROR("FrameViewRunner: %s", m_error.c_str());
            return false;
        }
    }
}

std::filesystem::path FrameViewRunner::findLatestCsvForApp(const std::string& appName)
{
    LOG_INFO("FrameViewRunner: Searching for CSV for app: %s", appName.c_str());

    if (!std::filesystem::exists(m_outputDirectory)) {
        LOG_WARN("FrameViewRunner: Output directory does not exist: %s", 
                 m_outputDirectory.string().c_str());
        return {};
    }

    std::filesystem::path latestCsv;
    std::filesystem::file_time_type latestTime;
    bool foundAny = false;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(m_outputDirectory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto& filePath = entry.path();
            
            // Must be a CSV file
            if (filePath.extension() != ".csv") {
                continue;
            }

            // Must contain the app name
            std::string filename = filePath.filename().string();
            if (filename.find(appName) == std::string::npos) {
                continue;
            }

            // Must not be already consumed
            if (m_consumedCsvs.count(filePath) > 0) {
                continue;
            }

            // Check if this is the latest
            auto writeTime = entry.last_write_time();
            if (!foundAny || writeTime > latestTime) {
                latestTime = writeTime;
                latestCsv = filePath;
                foundAny = true;
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR("FrameViewRunner: Error searching for CSV: %s", e.what());
        return {};
    }

    if (foundAny) {
        LOG_INFO("FrameViewRunner: Found CSV: %s", latestCsv.string().c_str());
    } else {
        LOG_INFO("FrameViewRunner: No CSV found for app: %s", appName.c_str());
    }

    return latestCsv;
}

void FrameViewRunner::notifyCsvConsumed(const std::filesystem::path& path)
{
    if (!path.empty()) {
        m_consumedCsvs.insert(path);
        LOG_INFO("FrameViewRunner: Marked CSV as consumed: %s", path.string().c_str());
    }
}
