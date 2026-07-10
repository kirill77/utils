#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct FrameViewMetrics {
    double avgFrameMs = 0.0;                  // mean(MsBetweenDisplayChange)
    double pacing50 = 0.0;                   // pacing error %: mean of the worst 50% of per-window
                                             // (16-frame, step 1) mean |residual from line fit| / window mean * 100
    double avgPcLatencyMs = 0.0;             // mean(MsPCLatency)
    size_t analyzedFrames = 0;               // number of frames analyzed (after skipping warmup + outlier reject)
    size_t droppedOutliers = 0;              // intervals discarded as FrameView garbage (> ceiling * median)
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
