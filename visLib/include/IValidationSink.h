#pragma once

namespace visLib {

// Outcome of a request to enable the graphics API validation layer.
enum class ValidationLayerStatus {
    Active,   // the layer is on and its messages reach the sink
    Missing,  // the layer is not installed
    TooOld,   // installed, but older than the backend accepts (Vulkan)
    Failed,   // installed, but enabling it or reading its messages failed
};

enum class ValidationSeverity {
    Warning,
    Error,    // includes D3D12 CORRUPTION
};

// Receives validation layer output from a window backend. A backend calls
// onLayerStatus once per window, when it creates the device, and onMessage for
// each warning or error. onMessage may run on any thread, concurrently.
class IValidationSink {
public:
    virtual ~IValidationSink() = default;

    // pVersion is the layer version ("1.4.309"), or "" when unknown.
    virtual void onLayerStatus(ValidationLayerStatus status, const char* pVersion) = 0;

    // pId is a stable message ID: a VUID or other layer message name on Vulkan,
    // "D3D12-<id>" or "DXGI-<id>" on D3D12.
    virtual void onMessage(ValidationSeverity severity, const char* pId, const char* pText) = 0;
};

} // namespace visLib
