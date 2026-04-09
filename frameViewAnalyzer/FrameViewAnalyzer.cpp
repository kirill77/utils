#include "utils/frameViewAnalyzer/FrameViewAnalyzer.h"
#include "utils/csvFile/CSVFileReader.h"

#include <cmath>
#include <vector>

static const char* const COLUMN_NAME = "MsBetweenDisplayChange";

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

    // Find the MsBetweenDisplayChange column
    const auto& headers = reader.getHeaders();
    size_t colIndex = headers.size(); // invalid sentinel
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == COLUMN_NAME) {
            colIndex = i;
            break;
        }
    }
    if (colIndex >= headers.size()) {
        outError = std::string("Column '") + COLUMN_NAME + "' not found in " + csvPath.string();
        return false;
    }

    // Read all rows, skip warmup frames, collect intervals
    std::vector<double> intervals;
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
    }

    if (intervals.size() < 2) {
        outError = "Not enough data rows after skipping " + std::to_string(skipFrames) + " frames";
        return false;
    }

    outMetrics.analyzedFrames = intervals.size();

    // Mean
    double sum = 0.0;
    for (double v : intervals) sum += v;
    double mean = sum / static_cast<double>(intervals.size());
    outMetrics.avgIntervalMs = mean;

    // Standard deviation
    double sumSqDiff = 0.0;
    for (double v : intervals) {
        double diff = v - mean;
        sumSqDiff += diff * diff;
    }
    outMetrics.stddevMs = std::sqrt(sumSqDiff / static_cast<double>(intervals.size()));

    // Jitter: average relative difference between consecutive intervals (%)
    double jitterSum = 0.0;
    for (size_t i = 1; i < intervals.size(); ++i) {
        jitterSum += std::abs(intervals[i] - intervals[i - 1]) / intervals[i - 1] * 100.0;
    }
    outMetrics.jitterPct = jitterSum / static_cast<double>(intervals.size() - 1);

    return true;
}
