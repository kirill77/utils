#pragma once

#include <string>

class ProcessManager
{
public:
    // Struct to hold process information
    struct ProcessInfo {
        uint32_t id;
        std::string imageName;
        
        ProcessInfo() : id(0) {}
        ProcessInfo(uint32_t processId, const std::string& name) : id(processId), imageName(name) {}
        
        // Helper to check if process info is valid
        bool isValid() const { return id != 0; }
    };

    // returns ProcessInfo of the found process (or invalid ProcessInfo if not found)
    ProcessInfo findProcessWithImage(const std::string& sName);

    bool killProcess(const ProcessInfo& processInfo);

    // returns ProcessInfo of the started process
    ProcessInfo startProcess(const std::string& sFullPath, const std::string& sArguments);
    
    // returns ProcessInfo of the started process with elevation (UAC prompt)
    ProcessInfo startProcessElevated(const std::string& sFullPath, const std::string& sArguments);
    
    // checks if a process is still running
    bool isProcessRunning(const ProcessInfo& processInfo);

private:
};

