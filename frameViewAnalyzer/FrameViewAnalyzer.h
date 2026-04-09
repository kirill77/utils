#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct FrameViewMetrics {
    double avgIntervalMs = 0.0;              // mean(MsBetweenDisplayChange)
    double stddevMs = 0.0;                   // standard deviation of intervals
    double jitterPct = 0.0;                  // avg(|interval[i] - interval[i-1]| / interval[i-1] * 100)
    size_t analyzedFrames = 0;               // number of frames analyzed (after skipping warmup)
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
