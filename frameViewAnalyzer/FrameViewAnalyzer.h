#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

struct FrameViewMetrics {
    double avgFrameMs = 0.0;                  // mean(MsBetweenDisplayChange)
    double pacing50 = 0.0;                   // pacing error %: mean of the worst 50% of per-window
                                             // (16-frame, step 1) mean |residual from line fit| / window mean * 100
    double avgPcLatencyMs = 0.0;             // mean(MsPCLatency)
    double avgTimeInQueueMs = 0.0;           // mean(MsUntilDisplayed - MsRenderPresentLatency): present-to-flip
                                             // minus present-to-render-complete = RSync's time-in-queue. Valid
                                             // only when hasTimeInQueue is true (can be legitimately ~0).
    bool hasTimeInQueue = false;             // true when both source columns were present and computed a mean
    size_t analyzedFrames = 0;               // number of frames analyzed (after skipping warmup + outlier reject)
    size_t droppedOutliers = 0;              // intervals discarded as FrameView garbage (> absolute ceiling)
    size_t keptRows = 0;                     // rows actually analyzed from the tail: min(keepLastRows, rows in file).
                                             // < keepLastRows means the capture was shorter than the requested window.
};

class FrameViewAnalyzer {
public:
    // Analyze the LAST keepLastRows rows of a FrameView CSV file (the
    // measurement window sits at the end of a capture; everything before it
    // is warmup). Computes metrics from the MsBetweenDisplayChange column.
    // Returns false if the file can't be opened, the column is missing,
    // or the kept tail has too few rows.
    static bool analyze(const std::filesystem::path& csvPath,
                        size_t keepLastRows,
                        FrameViewMetrics& outMetrics,
                        std::string& outError);
};
