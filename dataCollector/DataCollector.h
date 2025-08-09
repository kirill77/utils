#pragma once

#include "biology/organelles/Medium.h"
#include "geometry/vectors/vector.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

class CSVFileWriter;

/**
 * @class DataCollector
 * @brief Collects and saves simulation data to CSV files at specified intervals
 */
class DataCollector
{
public:
    /**
     * @brief Struct representing a data collection point
     */
    struct CollectionPoint {
        float3 position;               // Position to collect data from
        std::string name;              // Name of this collection point (e.g., "Anterior")
        std::vector<std::string> proteins; // Proteins to track at this position
    };
    
    /**
     * @brief Constructor
     * @param medium Reference to the medium to collect data from
     * @param outputFile Filename for the CSV output
     * @param collectionInterval How often to collect data (in simulation seconds)
     */
    DataCollector(Medium& medium, const std::string& outputFile, double collectionInterval = 1.0);
    
    /**
     * @brief Add a collection point
     * @param position Position in simulation space (-1 to 1)
     * @param name Name to identify this collection point
     * @param proteins List of protein names to track at this position
     */
    void addCollectionPoint(const float3& position, const std::string& name, 
                           const std::vector<std::string>& proteins);
    
    /**
     * @brief Update the collector - call this each simulation step
     * @param currentTime Current simulation time
     * @return True if data was collected during this update
     */
    bool update(double currentTime);
    
    /**
     * @brief Get the total number of data points collected so far
     */
    size_t getDataPointCount() const { return m_dataPointCount; }
    
    /**
     * @brief Force data collection at current time
     * @param currentTime Current simulation time
     * @param stepTimeMs Time taken for the current simulation step in milliseconds
     */
    void forceCollection(double currentTime, double stepTimeMs = 0.0);
    
    /**
     * @brief Set collection interval
     * @param interval New interval in simulation seconds
     */
    void setCollectionInterval(double interval) { m_collectionInterval = interval; }

private:
    Medium& m_medium;                           // Reference to simulation medium
    std::string m_outputFile;                   // Path to the output CSV file
    std::shared_ptr<CSVFileWriter> m_csvFile;         // CSV file for output
    std::vector<CollectionPoint> m_collectionPoints;  // Points to collect data from
    double m_lastCollectionTime = 0.0;          // Last time data was collected
    double m_collectionInterval;                // How often to collect data
    size_t m_dataPointCount = 0;                // Total data points collected
    std::unordered_map<std::string, double> m_performanceMetrics; // Performance metrics like step time
    
    /**
     * @brief Collect all data at current time
     * @param currentTime Current simulation time
     * @param stepTimeMs Time taken for the current simulation step in milliseconds
     */
    void collectData(double currentTime, double stepTimeMs);
    
    /**
     * @brief Generate headers for CSV file based on collection points and performance metrics
     */
    std::vector<std::string> generateHeaders() const;
}; 