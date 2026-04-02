#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct FrameViewMetrics {
    double avgIntervalMs = 0.0;              // mean(MsBetweenDisplayChange)
    double stddevMs = 0.0;                   // standard deviation of intervals
    double coefficientOfVariation = 0.0;     // stddev / mean (0 = perfectly smooth)
    double jitterMs = 0.0;                   // avg |interval[i] - interval[i-1]|
    size_t frameCount = 0;                   // number of frames analyzed (after skip)
    size_t skippedFrames = 0;                // number of warmup frames skipped
};

class FrameViewAnalyzer {
public:
    // Analyze a FrameView CSV file, skipping the first skipFrames rows.
    // Computes metrics from the MsBetweenDisplayChange column.
    // Returns false if the file can't be opened, the column is missing,
    // or there are not enough rows after skipping.
    static bool analyze(const std::filesystem::path& csvPath,
                        size_t skipFrames,
                        FrameViewMetrics& outMetrics,
                        std::string& outError);
};
