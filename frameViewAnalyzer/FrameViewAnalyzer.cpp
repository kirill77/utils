#include "utils/frameViewAnalyzer/FrameViewAnalyzer.h"
#include "utils/csvFile/CSVFileReader.h"

#include <algorithm>
#include <cmath>
#include <vector>

static const char* const COLUMN_NAME = "MsBetweenDisplayChange";
static const char* const LATENCY_COLUMN_NAME = "MsPCLatency";

bool FrameViewAnalyzer::analyze(const std::filesystem::path& csvPath,
                                size_t skipFrames,
                                FrameViewMetrics& outMetrics,
                                std::string& outError)
{
    outMetrics = FrameViewMetrics{};

    CSVFileReader reader(csvPath.string());
    if (!reader.isValid()) {
        outError = "Failed to open FrameView CSV: " + csvPath.string();
        return false;
    }

    // Find the MsBetweenDisplayChange and MsPCLatency columns
    const auto& headers = reader.getHeaders();
    size_t colIndex = headers.size(); // invalid sentinel
    size_t latencyColIndex = headers.size();
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == COLUMN_NAME) colIndex = i;
        else if (headers[i] == LATENCY_COLUMN_NAME) latencyColIndex = i;
    }
    if (colIndex >= headers.size()) {
        outError = std::string("Column '") + COLUMN_NAME + "' not found in " + csvPath.string();
        return false;
    }

    // Read all rows, skip warmup frames, collect intervals and latencies
    std::vector<double> intervals;
    std::vector<double> latencies;
    std::vector<std::string> row;
    size_t rowIndex = 0;

    while (reader.readRow(row)) {
        if (rowIndex < skipFrames) {
            ++rowIndex;
            continue;
        }
        ++rowIndex;

        if (colIndex < row.size() && !row[colIndex].empty()) {
            try {
                double val = std::stod(row[colIndex]);
                if (val > 0.0) {
                    intervals.push_back(val);
                }
            } catch (...) {
                // Skip unparseable values
            }
        }

        if (latencyColIndex < row.size() && !row[latencyColIndex].empty()) {
            try {
                double val = std::stod(row[latencyColIndex]);
                if (val > 0.0) {
                    latencies.push_back(val);
                }
            } catch (...) {}
        }
    }

    if (intervals.size() < 2) {
        outError = "Not enough data rows after skipping " + std::to_string(skipFrames) + " frames";
        return false;
    }

    // Reject FrameView's occasional single-frame garbage interval (a known FV
    // bug: one absurd value, orders of magnitude beyond physical). A
    // median-relative ceiling separates such samples from real frame hitches —
    // which sit within a few tens of x of the median even in a bad run — without
    // clipping genuine stutters. Filtering here (before any statistic) protects
    // avgFrameMs, stddevMs AND jitterPct alike: jitterPct now divides by the
    // window mean, so a single garbage sample would otherwise poison the
    // denominator too. The garbage-vs-real gap is ~13 orders of magnitude, so
    // the exact ceiling is not sensitive; 100x is comfortably above any real
    // frame yet nukes the artifact.
    {
        std::vector<double> sorted = intervals;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double ceiling = 100.0 * median;
        const size_t before = intervals.size();
        std::erase_if(intervals, [ceiling](double v) { return v > ceiling; });
        outMetrics.droppedOutliers = before - intervals.size();
        if (intervals.size() < 2) {
            outError = "Not enough valid intervals after dropping " +
                       std::to_string(outMetrics.droppedOutliers) + " FrameView outlier(s)";
            return false;
        }
    }

    outMetrics.analyzedFrames = intervals.size();

    // Mean
    double sum = 0.0;
    for (double v : intervals) sum += v;
    double mean = sum / static_cast<double>(intervals.size());
    outMetrics.avgFrameMs = mean;

    // Standard deviation
    double sumSqDiff = 0.0;
    for (double v : intervals) {
        double diff = v - mean;
        sumSqDiff += diff * diff;
    }
    outMetrics.stddevMs = std::sqrt(sumSqDiff / static_cast<double>(intervals.size()));

    // Jitter: mean absolute difference between consecutive intervals, expressed
    // as a percentage of the mean frame time (relative pacing variation).
    // Normalizing by the window mean — not by the previous interval — keeps the
    // estimator stable: dividing each term by interval[i-1] blows up whenever a
    // neighbor is sub-millisecond (the small mode of the bimodal FG present
    // stream), which made the old metric swing run-to-run while stddev held
    // steady. mean(interval) is bounded away from zero for any real capture, so
    // this is a well-conditioned, run-to-run comparable measure.
    double absDiffSum = 0.0;
    for (size_t i = 1; i < intervals.size(); ++i) {
        absDiffSum += std::abs(intervals[i] - intervals[i - 1]);
    }
    const double meanAbsDiff = absDiffSum / static_cast<double>(intervals.size() - 1);
    outMetrics.jitterPct = (mean > 0.0) ? (meanAbsDiff / mean * 100.0) : 0.0;

    // PC latency (optional column)
    if (!latencies.empty()) {
        double latSum = 0.0;
        for (double v : latencies) latSum += v;
        outMetrics.avgPcLatencyMs = latSum / static_cast<double>(latencies.size());
    }

    return true;
}
