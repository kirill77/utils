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

    // Read all rows, skip warmup frames, collect intervals and latencies.
    // Each interval keeps its original frame index: invalid samples (missing,
    // unparseable, non-positive, or garbage) leave a gap rather than shifting
    // their neighbors together, so the per-window line fits below see true
    // frame positions.
    struct Sample { size_t idx; double val; };
    std::vector<Sample> intervals;
    std::vector<double> latencies;
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
                if (val > 0.0) {
                    intervals.push_back({idx, val});
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

    constexpr size_t WINDOW = 16;    // frames per fit window
    constexpr size_t MIN_VALID = 12; // valid samples required for a window to count

    if (intervals.size() < WINDOW) {
        outError = "Not enough data rows after skipping " + std::to_string(skipFrames) + " frames";
        return false;
    }

    // Reject FrameView's occasional single-frame garbage interval (a known FV
    // bug: one absurd value, orders of magnitude beyond physical). A
    // median-relative ceiling separates such samples from real frame hitches —
    // which sit within a few tens of x of the median even in a bad run — without
    // clipping genuine stutters. Filtering here (before any statistic) protects
    // avgFrameMs and pacing50 alike: a single garbage sample would otherwise
    // poison both the line fits and the window-mean denominators. The
    // garbage-vs-real gap is ~13 orders of magnitude, so the exact ceiling is
    // not sensitive; 100x is comfortably above any real frame yet nukes the
    // artifact. Dropped samples leave a gap (indices are preserved), same as
    // any other invalid frame.
    {
        std::vector<double> sorted;
        sorted.reserve(intervals.size());
        for (const Sample& s : intervals) sorted.push_back(s.val);
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double ceiling = 100.0 * median;
        const size_t before = intervals.size();
        std::erase_if(intervals, [ceiling](const Sample& s) { return s.val > ceiling; });
        outMetrics.droppedOutliers = before - intervals.size();
        if (intervals.size() < WINDOW) {
            outError = "Not enough valid intervals after dropping " +
                       std::to_string(outMetrics.droppedOutliers) + " FrameView outlier(s)";
            return false;
        }
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
            const double windowMean = sy / nd; // > 0: all samples are positive
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

    return true;
}
