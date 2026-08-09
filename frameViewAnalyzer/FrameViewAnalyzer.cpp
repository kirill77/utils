#include "utils/frameViewAnalyzer/FrameViewAnalyzer.h"
#include "utils/csvFile/CSVFileReader.h"

#include <algorithm>
#include <cmath>
#include <vector>

static const char* const COLUMN_NAME = "MsBetweenDisplayChange";
static const char* const LATENCY_COLUMN_NAME = "MsPCLatency";
// Time-in-queue = present-to-flip minus present-to-render-complete. RSync targets
// ~vblank/2 here; a PCL win that drives TiQ below that target is margin theft, not
// a real improvement — so we surface TiQ alongside latency for the judge.
static const char* const UNTIL_DISPLAYED_COLUMN_NAME = "MsUntilDisplayed";
static const char* const RENDER_PRESENT_COLUMN_NAME = "MsRenderPresentLatency";

// Absolute per-sample sanity ceiling for every column read here. FrameView
// occasionally emits single-frame garbage orders of magnitude beyond physical
// (a known FV bug); one such sample destroys the plain means and the window
// fits, so any value above this is dropped at ingestion. Real values stay far
// below (even a 1 fps hitch is 1000 ms) while the garbage sits ~13 orders of
// magnitude out, so the exact ceiling is not sensitive. Deliberately NOT
// median-relative: with flip metering off, MFG presents flip in bursts, making
// the interval stream legitimately bimodal (sub-ms burst gaps + one real
// ~frame-time gap per render frame); the burst mode holds the majority, so a
// median-relative ceiling landed below the real intervals and rejected them all.
static constexpr double GARBAGE_CEILING_MS = 5000.0;

bool FrameViewAnalyzer::analyze(const std::filesystem::path& csvPath,
                                size_t keepLastRows,
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
    size_t untilDisplayedColIndex = headers.size();
    size_t renderPresentColIndex = headers.size();
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == COLUMN_NAME) colIndex = i;
        else if (headers[i] == LATENCY_COLUMN_NAME) latencyColIndex = i;
        else if (headers[i] == UNTIL_DISPLAYED_COLUMN_NAME) untilDisplayedColIndex = i;
        else if (headers[i] == RENDER_PRESENT_COLUMN_NAME) renderPresentColIndex = i;
    }
    if (colIndex >= headers.size()) {
        outError = std::string("Column '") + COLUMN_NAME + "' not found in " + csvPath.string();
        return false;
    }

    // The tail anchor needs the file's total row count first: pass 1 counts
    // data rows, pass 2 (below) skips everything before the kept tail. Two
    // cheap streaming passes beat buffering every parsed row.
    size_t totalRows = 0;
    {
        std::vector<std::string> countRow;
        while (reader.readRow(countRow)) ++totalRows;
    }
    if (!reader.reset()) {
        outError = "Failed to rewind FrameView CSV: " + csvPath.string();
        return false;
    }
    const size_t skipFrames = (totalRows > keepLastRows) ? totalRows - keepLastRows : 0;
    outMetrics.keptRows = totalRows - skipFrames;

    // Read all rows, skip everything before the measurement tail, collect
    // intervals and latencies. Each interval keeps its original frame index:
    // invalid samples (missing, unparseable, negative, or garbage) leave
    // a gap rather than shifting their neighbors together, so the per-window
    // line fits below see true frame positions.
    struct Sample { size_t idx; double val; };
    std::vector<Sample> intervals;
    std::vector<double> latencies;
    std::vector<double> timesInQueue;
    std::vector<std::string> row;
    size_t rowIndex = 0;
    size_t frameIdx = 0;

    while (reader.readRow(row)) {
        if (rowIndex < skipFrames) {
            ++rowIndex;
            continue;
        }
        ++rowIndex;
        const size_t idx = frameIdx++;

        if (colIndex < row.size() && !row[colIndex].empty()) {
            try {
                double val = std::stod(row[colIndex]);
                if (val >= 0.0 && val <= GARBAGE_CEILING_MS) {
                    intervals.push_back({idx, val});
                } else if (val > GARBAGE_CEILING_MS) {
                    ++outMetrics.droppedOutliers;
                }
            } catch (...) {
                // Skip unparseable values
            }
        }

        if (latencyColIndex < row.size() && !row[latencyColIndex].empty()) {
            try {
                double val = std::stod(row[latencyColIndex]);
                if (val > 0.0 && val <= GARBAGE_CEILING_MS) {
                    latencies.push_back(val);
                }
            } catch (...) {}
        }

        // Time-in-queue is the per-frame difference of two present-path columns.
        // Only count a frame where both parse to positive values, so a missing
        // half never skews the mean, and reject the row if either exceeds the
        // garbage ceiling (see GARBAGE_CEILING_MS above).
        if (untilDisplayedColIndex < row.size() && renderPresentColIndex < row.size() &&
            !row[untilDisplayedColIndex].empty() && !row[renderPresentColIndex].empty()) {
            try {
                double untilDisplayed = std::stod(row[untilDisplayedColIndex]);
                double renderPresent = std::stod(row[renderPresentColIndex]);
                if (untilDisplayed > 0.0 && renderPresent > 0.0 &&
                    untilDisplayed <= GARBAGE_CEILING_MS && renderPresent <= GARBAGE_CEILING_MS) {
                    timesInQueue.push_back(untilDisplayed - renderPresent);
                }
            } catch (...) {}
        }
    }

    constexpr size_t WINDOW = 16;    // frames per fit window
    constexpr size_t MIN_VALID = 12; // valid samples required for a window to count

    if (intervals.size() < WINDOW) {
        outError = "Not enough data rows in measurement tail (kept " +
                   std::to_string(outMetrics.keptRows) + " of " + std::to_string(totalRows) +
                   " rows, " + std::to_string(intervals.size()) + " valid intervals)";
        return false;
    }

    outMetrics.analyzedFrames = intervals.size();

    // Mean
    double sum = 0.0;
    for (const Sample& s : intervals) sum += s.val;
    outMetrics.avgFrameMs = sum / static_cast<double>(intervals.size());

    // pacing_50: slide a WINDOW-frame window one frame at a time (a hitch is
    // therefore seen by up to WINDOW windows — intentional weighting). In each
    // window, fit a line to the valid samples (so slow drift — load ramps, GPU
    // clock changes — is forgiven) and score the window as the mean |vertical
    // distance| from the line, as a percent of the window's mean interval.
    // Windows with fewer than MIN_VALID valid samples are skipped: a fit on a
    // handful of points is near-perfect by construction and would report a
    // bogus low score. The final metric is the mean of the worst 50% of window
    // scores, so short bad episodes are not drowned out by a long clean run.
    std::vector<double> scores;
    {
        const size_t firstIdx = intervals.front().idx;
        const size_t lastIdx = intervals.back().idx;
        size_t lo = 0, hi = 0;
        for (size_t start = firstIdx; start + WINDOW - 1 <= lastIdx; ++start) {
            while (lo < intervals.size() && intervals[lo].idx < start) ++lo;
            while (hi < intervals.size() && intervals[hi].idx < start + WINDOW) ++hi;
            const size_t n = hi - lo;
            if (n < MIN_VALID) continue;

            // Least-squares fit y = a + b*x, x centered on the window start
            // to keep the normal equations well-conditioned.
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (size_t k = lo; k < hi; ++k) {
                const double x = static_cast<double>(intervals[k].idx - start);
                const double y = intervals[k].val;
                sx += x; sy += y; sxx += x * x; sxy += x * y;
            }
            const double nd = static_cast<double>(n);
            const double denom = nd * sxx - sx * sx;
            const double b = (denom != 0.0) ? (nd * sxy - sx * sy) / denom : 0.0;
            const double a = (sy - b * sx) / nd;

            double absSum = 0.0;
            for (size_t k = lo; k < hi; ++k) {
                const double x = static_cast<double>(intervals[k].idx - start);
                absSum += std::abs(intervals[k].val - (a + b * x));
            }
            const double windowMean = sy / nd;
            if (windowMean <= 0.0) continue; // all-zero window: nothing to normalize by
            scores.push_back(absSum / nd / windowMean * 100.0);
        }
    }
    if (scores.empty()) {
        outError = "No window had " + std::to_string(MIN_VALID) + " valid frames; cannot compute pacing_50";
        return false;
    }
    std::sort(scores.begin(), scores.end(), std::greater<double>());
    const size_t keep = (scores.size() + 1) / 2; // worst half, rounded up
    double worstSum = 0.0;
    for (size_t i = 0; i < keep; ++i) worstSum += scores[i];
    outMetrics.pacing50 = worstSum / static_cast<double>(keep);

    // PC latency (optional column)
    if (!latencies.empty()) {
        double latSum = 0.0;
        for (double v : latencies) latSum += v;
        outMetrics.avgPcLatencyMs = latSum / static_cast<double>(latencies.size());
    }

    // Time-in-queue (optional columns)
    if (!timesInQueue.empty()) {
        double tiqSum = 0.0;
        for (double v : timesInQueue) tiqSum += v;
        outMetrics.avgTimeInQueueMs = tiqSum / static_cast<double>(timesInQueue.size());
        outMetrics.hasTimeInQueue = true;
    }

    return true;
}
