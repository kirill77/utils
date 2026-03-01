#pragma once

#include <string>
#include <vector>

class ProcessManager
{
public:
    struct ProcessInfo {
        uint32_t id;
        std::string imageName;
        
        ProcessInfo() : id(0) {}
        ProcessInfo(uint32_t processId, const std::string& name) : id(processId), imageName(name) {}
        
        bool isValid() const { return id != 0; }
    };

    struct WindowInfo {
        void* handle;       // Platform window handle (HWND on Windows)
        std::string title;

        WindowInfo() : handle(nullptr) {}
        bool isValid() const { return handle != nullptr; }
    };

    // returns ProcessInfo of the found process (or invalid ProcessInfo if not found)
    ProcessInfo findProcessWithImage(const std::string& sName);

    // returns all processes matching the given image name
    std::vector<ProcessInfo> findAllProcessesWithImage(const std::string& sName);

    bool killProcess(const ProcessInfo& processInfo);

    // returns ProcessInfo of the started process
    ProcessInfo startProcess(const std::string& sFullPath, const std::string& sArguments);
    
    // returns ProcessInfo of the started process with elevation (UAC prompt)
    ProcessInfo startProcessElevated(const std::string& sFullPath, const std::string& sArguments);
    
    // checks if a process is still running
    bool isProcessRunning(const ProcessInfo& processInfo);

    // finds the main visible window belonging to a process
    WindowInfo findMainWindow(uint32_t processId);

    // finds all visible titled windows belonging to processes with the given image name
    std::vector<WindowInfo> findAllWindows(const std::string& imageName);

    // brings a window to the foreground and gives it input focus
    static bool bringWindowToForeground(const WindowInfo& windowInfo);

private:
};

