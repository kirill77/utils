#include "pch.h"
#include "DataCollector.h"

DataCollector::DataCollector(Medium& medium, const std::string& outputFile, double collectionInterval)
    : m_medium(medium)
    , m_outputFile(outputFile)
    , m_collectionInterval(collectionInterval)
{
    // Initialize performance metrics
    m_performanceMetrics["StepTime"] = 0.0;
    
    // Defer CSV file creation until we have collection points and know the headers
}

void DataCollector::addCollectionPoint(const float3& position, const std::string& name, 
                                     const std::vector<std::string>& proteins)
{
    // Add new collection point
    CollectionPoint point;
    point.position = position;
    point.name = name;
    point.proteins = proteins;
    
    m_collectionPoints.push_back(point);
    
    // Create or recreate the CSV file with updated headers
    if (!m_csvFile) {
        // First time creation
        m_csvFile = std::make_unique<CSVFile>(m_outputFile, generateHeaders());
        m_csvFile->setPrecision(6);
    } else {
        // Recreate with new headers
        std::string filename = m_outputFile;
        m_csvFile = std::make_unique<CSVFile>(filename, generateHeaders());
        m_csvFile->setPrecision(6);
        // Note: This will overwrite previous data
    }
}

bool DataCollector::update(double currentTime)
{
    // Check if it's time to collect data
    if (currentTime >= m_lastCollectionTime + m_collectionInterval) {
        collectData(currentTime, 0.0); // No step time info for regular updates
        return true;
    }
    return false;
}

void DataCollector::forceCollection(double currentTime, double stepTimeMs)
{
    collectData(currentTime, stepTimeMs);
}

void DataCollector::collectData(double currentTime, double stepTimeMs)
{
    // Make sure we have a CSV file and collection points
    if (!m_csvFile || m_collectionPoints.empty()) {
        return;
    }
    
    // Store the step time
    m_performanceMetrics["StepTime"] = stepTimeMs;
    
    // Prepare row of data
    std::vector<double> dataRow;
    dataRow.push_back(currentTime); // First column is time
    
    // For each collection point, get protein concentrations
    for (const auto& point : m_collectionPoints) {
        // For each protein at this point
        for (const auto& protein : point.proteins) {
            double concentration = m_medium.getProteinNumber(protein, point.position);
            dataRow.push_back(concentration);
        }
    }
    
    // Add ATP data for each point
    for (const auto& point : m_collectionPoints) {
        double atp = m_medium.getAvailableATP(point.position);
        dataRow.push_back(atp);
    }
    
    // Add performance metrics
    for (const auto& metric : m_performanceMetrics) {
        dataRow.push_back(metric.second);
    }
    
    // Add the row to the CSV file
    m_csvFile->addRow(dataRow);
    
    // Update tracking variables
    m_lastCollectionTime = currentTime;
    m_dataPointCount++;
    
    // Flush every 10 data points to ensure data is written
    if (m_dataPointCount % 10 == 0) {
        m_csvFile->flush();
    }
}

std::vector<std::string> DataCollector::generateHeaders() const
{
    std::vector<std::string> headers;
    
    // First column is time
    headers.push_back("Time(s)");
    
    // For each collection point, add protein headers
    for (const auto& point : m_collectionPoints) {
        for (const auto& protein : point.proteins) {
            headers.push_back(protein + "_" + point.name);
        }
    }
    
    // Add ATP headers for each point
    for (const auto& point : m_collectionPoints) {
        headers.push_back("ATP_" + point.name);
    }
    
    // Add performance metric headers
    for (const auto& metric : m_performanceMetrics) {
        headers.push_back(metric.first + "(ms)");
    }
    
    return headers;
} 