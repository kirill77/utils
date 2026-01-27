#pragma once

#include <cstdint>
#include <memory>

namespace visLib {

// ============================================================================
// Query Capabilities - what data to capture
// ============================================================================

enum class QueryCapability : uint32_t {
    None = 0,
    Timestamps = 1 << 0,      // GPU timing measurement
    PipelineStats = 1 << 1,   // Pipeline statistics (vertices, invocations, etc.)
    All = Timestamps | PipelineStats
};

inline QueryCapability operator|(QueryCapability a, QueryCapability b) {
    return static_cast<QueryCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline QueryCapability operator&(QueryCapability a, QueryCapability b) {
    return static_cast<QueryCapability>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool hasCapability(QueryCapability caps, QueryCapability flag) {
    return (caps & flag) == flag;
}

// ============================================================================
// Query Result Types
// ============================================================================

// Result from timestamp measurement
struct TimestampQueryResult {
    uint64_t frameIndex = 0;        // Frame this measurement belongs to
    uint64_t beginTimestamp = 0;    // GPU timestamp at start of render
    uint64_t endTimestamp = 0;      // GPU timestamp at end of render
    uint64_t frequency = 0;         // GPU timestamp frequency (ticks/second)
    
    // Convenience: compute elapsed time in milliseconds
    double getElapsedMs() const {
        if (frequency == 0) return 0.0;
        return static_cast<double>(endTimestamp - beginTimestamp) * 1000.0 / 
               static_cast<double>(frequency);
    }
};

// Result from pipeline statistics measurement
struct PipelineStatsQueryResult {
    uint64_t frameIndex = 0;                 // Frame this measurement belongs to
    uint64_t inputAssemblerVertices = 0;     // Vertices read by input assembler
    uint64_t inputAssemblerPrimitives = 0;   // Primitives read by input assembler
    uint64_t vertexShaderInvocations = 0;    // Vertex shader invocations
    uint64_t geometryShaderInvocations = 0;  // Geometry shader invocations
    uint64_t geometryShaderPrimitives = 0;   // Primitives output by geometry shader
    uint64_t clipperInvocations = 0;         // Primitives sent to clipper
    uint64_t clipperPrimitives = 0;          // Primitives that passed clipping
    uint64_t pixelShaderInvocations = 0;     // Pixel shader invocations
    uint64_t computeShaderInvocations = 0;   // Compute shader invocations
};

// ============================================================================
// IQuery - Unified GPU query interface
// ============================================================================

class IQuery {
public:
    virtual ~IQuery() = default;
    
    // Returns the capabilities this query was created with
    virtual QueryCapability getCapabilities() const = 0;
    
    // Returns the number of results ready to be retrieved
    virtual uint32_t getReadyCount() const = 0;
    
    // Returns the total slot capacity of this query object
    virtual uint32_t getCapacity() const = 0;
    
    // Retrieve the oldest ready timestamp result.
    // Returns false if Timestamps capability not enabled or no results ready.
    // The retrieved slot is recycled for future measurements.
    virtual bool getTimestampResult(TimestampQueryResult& outResult) = 0;
    
    // Retrieve the oldest ready pipeline stats result.
    // Returns false if PipelineStats capability not enabled or no results ready.
    // The retrieved slot is recycled for future measurements.
    virtual bool getPipelineStatsResult(PipelineStatsQueryResult& outResult) = 0;
};

} // namespace visLib
