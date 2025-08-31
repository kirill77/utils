#include "ProcessManager.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <stdexcept>
#include <iostream>
#include <vector>
#include "utils/log/ILog.h"

// Helper function to convert std::string to std::wstring (static to avoid linker collision)
static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

// Helper function to convert std::wstring to std::string (static to avoid linker collision)
static std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size_needed, NULL, NULL);
    return str;
}

ProcessManager::ProcessInfo ProcessManager::findProcessWithImage(const std::string& sName) {
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;

    // Take a snapshot of all processes in the system
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateToolhelp32Snapshot failed");
    }

    // Set the size of the structure before using it
    pe32.dwSize = sizeof(PROCESSENTRY32);

    // Retrieve information about the first process and exit if unsuccessful
    if (!Process32First(hProcessSnap, &pe32)) {
        CloseHandle(hProcessSnap);
        throw std::runtime_error("Process32First failed");
    }

    // Convert search name to wide string for comparison
    std::wstring wsSearchName = StringToWString(sName);

    // Walk through the process list
    do {
        std::wstring wsProcessName(pe32.szExeFile);
        
        // Compare process names (case-insensitive)
        if (_wcsicmp(wsProcessName.c_str(), wsSearchName.c_str()) == 0) {
            uint32_t processId = pe32.th32ProcessID;
            std::string processName = WStringToString(wsProcessName);
            CloseHandle(hProcessSnap);
            return ProcessInfo(processId, processName);
        }
    } while (Process32Next(hProcessSnap, &pe32));

    CloseHandle(hProcessSnap);
    return ProcessInfo(); // Process not found (invalid ProcessInfo)
}

bool ProcessManager::killProcess(const ProcessInfo& processInfo) {
    if (!processInfo.isValid()) {
        LOG_ERROR("Invalid process info provided");
        return false;
    }

    // Open the process with termination rights
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processInfo.id);
    if (hProcess == NULL) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to open process %u (%s). Error: %lu", processInfo.id, processInfo.imageName.c_str(), error);
        return false;
    }

    // Terminate the process
    BOOL result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);

    if (!result) {
        DWORD error = GetLastError();
        LOG_ERROR("Failed to terminate process %u (%s). Error: %lu", processInfo.id, processInfo.imageName.c_str(), error);
        return false;
    }

    LOG_INFO("Successfully terminated process %u (%s)", processInfo.id, processInfo.imageName.c_str());
    return true;
}

ProcessManager::ProcessInfo ProcessManager::startProcess(const std::string& sFullPath, const std::string& sArguments) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    // Initialize structures
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    // Convert strings to wide strings
    std::wstring wsFullPath = StringToWString(sFullPath);
    std::wstring wsArguments = StringToWString(sArguments);

    // Extract working directory from the executable path
    std::string workingDir = sFullPath;
    size_t lastSlash = workingDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        workingDir = workingDir.substr(0, lastSlash);
    } else {
        workingDir.clear(); // Use current directory if no path separator found
    }
    std::wstring wsWorkingDir = workingDir.empty() ? std::wstring() : StringToWString(workingDir);

    // Combine executable path and arguments
    std::wstring wsCommandLine;
    if (!wsArguments.empty()) {
        wsCommandLine = L"\"" + wsFullPath + L"\" " + wsArguments;
    } else {
        wsCommandLine = L"\"" + wsFullPath + L"\"";
    }

    // Create a modifiable copy of the command line
    std::vector<wchar_t> commandLine(wsCommandLine.begin(), wsCommandLine.end());
    commandLine.push_back(L'\0');

    // Start the child process
    BOOL result = CreateProcessW(
        wsFullPath.c_str(),        // Application name
        commandLine.data(),        // Command line
        NULL,                      // Process handle not inheritable
        NULL,                      // Thread handle not inheritable
        FALSE,                     // Set handle inheritance to FALSE
        0,                         // No creation flags
        NULL,                      // Use parent's environment block
        wsWorkingDir.empty() ? NULL : wsWorkingDir.c_str(), // Working directory
        &si,                       // Pointer to STARTUPINFO structure
        &pi                        // Pointer to PROCESS_INFORMATION structure
    );

    if (!result) {
        DWORD error = GetLastError();
        std::string errorMsg = "CreateProcess failed for: " + sFullPath + 
                              " with arguments: " + sArguments + 
                              ". Error code: " + std::to_string(error);
        throw std::runtime_error(errorMsg);
    }

    // Extract the executable name from the full path
    std::string imageName = sFullPath;
    size_t imageNameSlash = imageName.find_last_of("/\\");
    if (imageNameSlash != std::string::npos) {
        imageName = imageName.substr(imageNameSlash + 1);
    }

    ProcessInfo processInfo(pi.dwProcessId, imageName);
    
    LOG_INFO("Successfully started process: %s", sFullPath.c_str());
    LOG_INFO("Process ID: %u", processInfo.id);
    LOG_INFO("Image Name: %s", processInfo.imageName.c_str());
    LOG_INFO("Working Directory: %s", workingDir.empty() ? "(current directory)" : workingDir.c_str());
    LOG_INFO("Thread ID: %lu", pi.dwThreadId);

    // Close process and thread handles as we don't need to wait for the process
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return processInfo;
}

ProcessManager::ProcessInfo ProcessManager::startProcessElevated(const std::string& sFullPath, const std::string& sArguments) {
    LOG_INFO("Starting process with elevation: %s", sFullPath.c_str());
    
    // Convert strings to wide strings
    std::wstring wsFullPath = StringToWString(sFullPath);
    std::wstring wsArguments = StringToWString(sArguments);
    
    // Extract working directory from the executable path
    std::string workingDir = sFullPath;
    size_t lastSlash = workingDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        workingDir = workingDir.substr(0, lastSlash);
    } else {
        workingDir.clear(); // Use current directory if no path separator found
    }
    std::wstring wsWorkingDir = workingDir.empty() ? std::wstring() : StringToWString(workingDir);
    
    // Setup ShellExecuteEx structure
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;  // Keep process handle open so we can get process ID
    sei.lpVerb = L"runas";                // Request elevation
    sei.lpFile = wsFullPath.c_str();
    sei.lpParameters = wsArguments.empty() ? nullptr : wsArguments.c_str();
    sei.lpDirectory = wsWorkingDir.empty() ? nullptr : wsWorkingDir.c_str();
    sei.nShow = SW_SHOW;
    
    // Launch the process with elevation
    BOOL result = ShellExecuteExW(&sei);
    
    if (!result) {
        DWORD error = GetLastError();
        std::string errorMsg = "ShellExecuteEx failed for: " + sFullPath + 
                              " with arguments: " + sArguments + 
                              ". Error code: " + std::to_string(error);
        throw std::runtime_error(errorMsg);
    }
    
    // Get the process ID from the handle
    DWORD processId = 0;
    if (sei.hProcess != nullptr) {
        processId = GetProcessId(sei.hProcess);
        CloseHandle(sei.hProcess);  // Close handle as we don't need it anymore
    }
    
    if (processId == 0) {
        throw std::runtime_error("Failed to get process ID for elevated process: " + sFullPath);
    }
    
    // Extract the executable name from the full path
    std::string imageName = sFullPath;
    size_t imageNameSlash = imageName.find_last_of("/\\");
    if (imageNameSlash != std::string::npos) {
        imageName = imageName.substr(imageNameSlash + 1);
    }

    ProcessInfo processInfo(processId, imageName);
    
    LOG_INFO("Successfully started elevated process: %s", sFullPath.c_str());
    LOG_INFO("Process ID: %u", processInfo.id);
    LOG_INFO("Image Name: %s", processInfo.imageName.c_str());
    LOG_INFO("Working Directory: %s", workingDir.empty() ? "(current directory)" : workingDir.c_str());
    
    return processInfo;
}

bool ProcessManager::isProcessRunning(const ProcessInfo& processInfo) {
    if (!processInfo.isValid()) {
        return false;
    }

    // Try to open the process with minimal access rights
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processInfo.id);
    if (hProcess == NULL) {
        // Process doesn't exist or we don't have access to it
        return false;
    }

    // Check if the process has exited
    DWORD exitCode;
    BOOL result = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);

    if (!result) {
        // Failed to get exit code, assume process is not running
        return false;
    }

    // If exit code is STILL_ACTIVE, the process is still running
    return (exitCode == STILL_ACTIVE);
}
