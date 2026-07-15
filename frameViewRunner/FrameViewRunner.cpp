#include "FrameViewRunner.h"
#include "utils/processManager/ProcessManager.h"
#include "utils/log/ILog.h"

#include <Windows.h>
#include <optional>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <thread>
#include <chrono>

struct InstalledAppInfo {
    std::wstring displayName;
    std::wstring installLocation;
    std::wstring version;
};

// Queries Windows registry for installed application information.
// Searches both 64-bit and 32-bit (WOW6432Node) registry locations.
class InstalledAppRegistry {
public:
    std::optional<InstalledAppInfo> find(const std::wstring& searchName) const;
};

std::optional<InstalledAppInfo> InstalledAppRegistry::find(const std::wstring& searchName) const {
    if (searchName.empty()) {
        return std::nullopt;
    }

    const wchar_t* uninstallKeys[] = {
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
    };

    for (const auto* keyPath : uninstallKeys) {
        HKEY hUninstallKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hUninstallKey) != ERROR_SUCCESS) {
            continue;
        }

        DWORD subKeyCount = 0;
        RegQueryInfoKeyW(hUninstallKey, nullptr, nullptr, nullptr, &subKeyCount,
                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

        for (DWORD i = 0; i < subKeyCount; ++i) {
            wchar_t subKeyName[256];
            DWORD subKeyNameLen = 256;

            if (RegEnumKeyExW(hUninstallKey, i, subKeyName, &subKeyNameLen,
                             nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
                continue;
            }

            HKEY hAppKey;
            if (RegOpenKeyExW(hUninstallKey, subKeyName, 0, KEY_READ, &hAppKey) != ERROR_SUCCESS) {
                continue;
            }

            wchar_t displayName[512] = {0};
            DWORD displayNameSize = sizeof(displayName);
            if (RegQueryValueExW(hAppKey, L"DisplayName", nullptr, nullptr,
                                reinterpret_cast<LPBYTE>(displayName), &displayNameSize) == ERROR_SUCCESS) {

                std::wstring name(displayName);
                if (name.find(searchName) != std::wstring::npos) {

                    wchar_t installPath[MAX_PATH] = {0};
                    DWORD installPathSize = sizeof(installPath);
                    if (RegQueryValueExW(hAppKey, L"InstallLocation", nullptr, nullptr,
                                        reinterpret_cast<LPBYTE>(installPath), &installPathSize) == ERROR_SUCCESS) {

                        InstalledAppInfo info;
                        info.displayName = name;
                        info.installLocation = installPath;

                        wchar_t version[128] = {0};
                        DWORD versionSize = sizeof(version);
                        if (RegQueryValueExW(hAppKey, L"DisplayVersion", nullptr, nullptr,
                                            reinterpret_cast<LPBYTE>(version), &versionSize) == ERROR_SUCCESS) {
                            info.version = version;
                        }

                        RegCloseKey(hAppKey);
                        RegCloseKey(hUninstallKey);
                        return info;
                    }
                }
            }
            RegCloseKey(hAppKey);
        }
        RegCloseKey(hUninstallKey);
    }

    return std::nullopt;
}

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

    // Directory containing the running slVerdict executable. Used to locate a
    // PresentMon bundled flat alongside it (the no-FrameView-install case).
    std::filesystem::path getExecutableDirectory()
    {
        std::vector<wchar_t> buf(MAX_PATH);
        for (;;) {
            DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (len == 0) {
                return {};  // GetModuleFileNameW failed
            }
            if (len < buf.size()) {
                return std::filesystem::path(std::wstring(buf.data(), len)).parent_path();
            }
            buf.resize(buf.size() * 2);  // truncated; grow and retry
        }
    }

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

std::unique_ptr<FrameViewRunner> FrameViewRunner::create(std::string& outError)
{
    LOG_INFO("FrameViewRunner: Initializing...");

    std::unique_ptr<FrameViewRunner> runner(new FrameViewRunner());

    try {
        runner->killFrameViewProcesses();

        std::filesystem::path frameViewInstallPath = runner->findFrameViewInstallation();
        if (frameViewInstallPath.empty()) {
            outError = "PresentMon not found: no PresentMon_x64.exe bundled alongside "
                       "slVerdict, and no FrameView installation detected";
            LOG_ERROR("FrameViewRunner: %s", outError.c_str());
            return nullptr;
        }
        LOG_INFO("FrameViewRunner: Using PresentMon at: %s", runner->m_presentMonExe.string().c_str());

        // We drive FrameView's capture engine (PresentMon_x64.exe) directly,
        // bypassing the FrameView_x64.exe GUI/orchestrator. PresentMon runs
        // in-place from wherever it was resolved (bundled beside slVerdict, or a
        // FrameView install's bin\ dir), so no copy is needed. CSVs are written
        // to m_outputDirectory in multi-CSV mode (one file per captured
        // process), exactly where findLatestCsvForApp() looks for them.
        std::filesystem::path tempBase = getTempBasePath();
        runner->m_outputDirectory = tempBase / "Results";

        std::error_code ec;
        std::filesystem::create_directories(runner->m_outputDirectory, ec);
        if (ec) {
            outError = "Failed to create output directory: " + ec.message();
            LOG_ERROR("FrameViewRunner: %s", outError.c_str());
            return nullptr;
        }

        // Clean up any leftover CSVs from previous runs so findLatestCsvForApp
        // never returns a stale file.
        int removedCsvs = 0;
        for (const auto& entry : std::filesystem::directory_iterator(runner->m_outputDirectory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                std::filesystem::remove(entry.path(), ec);
                ++removedCsvs;
            }
        }
        if (removedCsvs > 0) {
            LOG_INFO("FrameViewRunner: Removed %d leftover CSV file(s) from results directory", removedCsvs);
        }

        if (!runner->launchPresentMon(outError)) {
            return nullptr;
        }

        LOG_INFO("FrameViewRunner: Successfully initialized");
        return runner;

    } catch (const std::exception& e) {
        outError = std::string("Exception during initialization: ") + e.what();
        LOG_ERROR("FrameViewRunner: %s", outError.c_str());
        return nullptr;
    }
}

FrameViewRunner::~FrameViewRunner()
{
    LOG_INFO("FrameViewRunner: Shutting down...");
    killFrameViewProcesses();
}

std::filesystem::path FrameViewRunner::findFrameViewInstallation()
{
    // Prefer a PresentMon bundled flat alongside slVerdict.exe. This lets us
    // ship slVerdict + PresentMon as a self-contained package and run on
    // machines with no FrameView installed. The bundled exe must be FrameView's
    // fork of PresentMon (see class docs), not the public open-source build.
    std::filesystem::path exeDir = getExecutableDirectory();
    if (!exeDir.empty()) {
        std::filesystem::path bundledExe = exeDir / "PresentMon_x64.exe";
        if (std::filesystem::exists(bundledExe)) {
            LOG_INFO("FrameViewRunner: Using PresentMon bundled alongside slVerdict: %s",
                     bundledExe.string().c_str());
            m_installPath = exeDir;
            m_presentMonExe = bundledExe;
            m_version = "bundled";
            return exeDir;
        }
    }

    // Otherwise, fall back to an installed FrameView. First, try the registry.
    InstalledAppRegistry registry;
    auto frameViewInfo = registry.find(L"FrameView");
    if (frameViewInfo.has_value() && !frameViewInfo->installLocation.empty()) {
        std::filesystem::path installPath(frameViewInfo->installLocation);
        if (std::filesystem::exists(installPath / "FrameView_x64.exe")) {
            LOG_INFO("FrameViewRunner: Found FrameView via registry at: %s",
                     installPath.string().c_str());
            m_installPath = installPath;
            m_presentMonExe = installPath / "bin" / "PresentMon_x64.exe";
            // Convert wide string version to narrow string
            if (!frameViewInfo->version.empty()) {
                const std::wstring& wv = frameViewInfo->version;
                int size = WideCharToMultiByte(CP_UTF8, 0, wv.c_str(), static_cast<int>(wv.size()),
                                               nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    m_version.resize(size);
                    WideCharToMultiByte(CP_UTF8, 0, wv.c_str(), static_cast<int>(wv.size()),
                                        m_version.data(), size, nullptr, nullptr);
                }
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
            m_presentMonExe = commonPath / "bin" / "PresentMon_x64.exe";
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

bool FrameViewRunner::launchPresentMon(std::string& outError)
{
    // Resolved by findFrameViewInstallation(): either bundled flat beside
    // slVerdict.exe, or in a FrameView install's bin\ directory.
    std::filesystem::path presentMonExe = m_presentMonExe;
    if (!std::filesystem::exists(presentMonExe)) {
        outError = "PresentMon_x64.exe not found at: " + presentMonExe.string();
        LOG_ERROR("FrameViewRunner: %s", outError.c_str());
        return false;
    }

    // Resident, unattended capture driven without the FrameView_x64.exe GUI
    // (which crashes in its WndProc on some configurations). Argument notes:
    //   -spawnconsumer        Consume ETW and write the CSV. REQUIRED: the CSV
    //                         path is gated behind this in PresentMon.
    //   -frameview            Emit the FrameView column set (incl. MsPCLatency).
    //   -multi_csv            One CSV per captured process; findLatestCsvForApp()
    //                         matches the per-test renderer by its exe name. Files
    //                         are named FrameView_<proc>_<timestamp>_Log.csv.
    //   -output_file ...\FrameView.csv   Only the *basename* is honored in
    //                         FrameView mode; the directory is ignored (see below).
    //   -session_name FrameView   REQUIRED VERBATIM: PresentMon selects its
    //                         FrameView capture mode from this exact session name;
    //                         any other name fails its shared-memory init.
    //   -stop_existing_session    Clear a stale "FrameView" ETW session first.
    //   -dont_restart_as_admin    Don't self-elevate; inherit slVerdict's token.
    // With no -hotkey/-timed, PresentMon auto-starts recording on launch and
    // records until terminated (the destructor kills it via killFrameViewProcesses).
    // NOTE: PresentMon's ETW consumer needs admin rights, so slVerdict must run
    // elevated; otherwise the process launches but produces no CSV.
    //
    // CWD: in FrameView mode PresentMon ignores the directory of -output_file and
    // writes its per-process CSVs to its current working directory. We therefore
    // launch it with the working directory set to m_outputDirectory so the CSVs
    // land exactly where findLatestCsvForApp() looks. (PresentMon's own DLLs still
    // resolve from the directory of its exe -- FrameView's bin\ or, when bundled,
    // slVerdict's own dir -- which is always on the DLL search path regardless of
    // CWD.) This is why we use CreateProcessW directly rather
    // than ProcessManager::startProcess, which would force CWD to the exe's dir.
    const std::wstring exeW = presentMonExe.wstring();
    const std::wstring csvW = (m_outputDirectory / L"FrameView.csv").wstring();
    const std::wstring cwdW = m_outputDirectory.wstring();
    std::wstring cmd = L"\"" + exeW + L"\""
        L" -spawnconsumer"
        L" -frameview"
        L" -multi_csv"
        L" -output_file \"" + csvW + L"\""
        L" -session_name FrameView"
        L" -stop_existing_session"
        L" -dont_restart_as_admin";

    LOG_INFO("FrameViewRunner: Launching PresentMon: %s", presentMonExe.string().c_str());
    LOG_INFO("FrameViewRunner: PresentMon working directory (CSV output): %s",
             m_outputDirectory.string().c_str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    // CreateProcessW may modify the command-line buffer, so pass a mutable copy.
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        exeW.c_str(),        // lpApplicationName
        cmdBuf.data(),       // lpCommandLine (mutable)
        nullptr,             // lpProcessAttributes
        nullptr,             // lpThreadAttributes
        FALSE,               // bInheritHandles
        CREATE_NO_WINDOW,    // dwCreationFlags
        nullptr,             // lpEnvironment (inherit)
        cwdW.c_str(),        // lpCurrentDirectory -> drives the CSV output dir
        &si,
        &pi);

    if (!ok) {
        DWORD err = GetLastError();
        outError = "CreateProcess for PresentMon failed (error " + std::to_string(err) + ")";
        LOG_ERROR("FrameViewRunner: %s", outError.c_str());
        return false;
    }

    LOG_INFO("FrameViewRunner: Successfully launched PresentMon (PID: %u)", pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::filesystem::path FrameViewRunner::findLatestCsvForApp(const std::string& appName)
{
    LOG_INFO("FrameViewRunner: Searching for CSV for app: %s", appName.c_str());

    // Snapshot the consumed set so we don't hold the lock during directory I/O
    std::set<std::filesystem::path> consumedSnapshot;
    {
        std::lock_guard<std::mutex> lock(m_csvMutex);
        consumedSnapshot = m_consumedCsvs;
    }

    auto searchDir = [&](const std::filesystem::path& dir,
                         std::filesystem::path& outCsv) -> bool {
        if (!std::filesystem::exists(dir)) return false;
        std::filesystem::file_time_type latestTime;
        bool found = false;
        try {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file()) continue;
                const auto& filePath = entry.path();
                if (filePath.extension() != ".csv") continue;
                if (filePath.filename().string().find(appName) == std::string::npos) continue;
                if (consumedSnapshot.count(filePath) > 0) continue;
                auto writeTime = entry.last_write_time();
                if (!found || writeTime > latestTime) {
                    latestTime = writeTime;
                    outCsv = filePath;
                    found = true;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR("FrameViewRunner: Error searching for CSV in %s: %s",
                      dir.string().c_str(), e.what());
        }
        return found;
    };

    std::filesystem::path latestCsv;
    if (searchDir(m_outputDirectory, latestCsv)) {
        LOG_INFO("FrameViewRunner: Found CSV: %s", latestCsv.string().c_str());
        return latestCsv;
    }

    LOG_INFO("FrameViewRunner: No CSV found for app: %s", appName.c_str());
    return {};
}

void FrameViewRunner::notifyCsvConsumed(const std::filesystem::path& path)
{
    if (!path.empty()) {
        std::lock_guard<std::mutex> lock(m_csvMutex);
        m_consumedCsvs.insert(path);
        LOG_INFO("FrameViewRunner: Marked CSV as consumed: %s", path.string().c_str());
    }
}
