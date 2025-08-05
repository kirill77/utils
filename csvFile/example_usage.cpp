// Example usage of CSVFileReader and CSVFileWriter classes
// This is a demonstration file, not part of the build

#include "CSVFileReader.h"
#include "CSVFileWriter.h"
#include <iostream>
#include <vector>
#include <string>

void demonstrateCSVReader() {
    std::cout << "CSVFileReader Example Usage:\n";
    std::cout << "=============================\n\n";
    
    // Example 1: Reading headers and basic information
    CSVFileReader reader("../../exampleInput/bfm250730_NewSL.csv");
    
    if (!reader.isValid()) {
        std::cout << "Error: Could not open CSV file\n";
        return;
    }
    
    // Display file information
    std::cout << "File: " << reader.getFilename() << "\n";
    std::cout << "Column count: " << reader.getColumnCount() << "\n";
    
    // Display headers
    const auto& headers = reader.getHeaders();
    std::cout << "Headers: ";
    for (size_t i = 0; i < headers.size(); ++i) {
        std::cout << headers[i];
        if (i < headers.size() - 1) std::cout << ", ";
    }
    std::cout << "\n\n";
    
    // Example 2: Reading a few rows as strings
    std::cout << "First 5 data rows:\n";
    std::vector<std::string> row;
    for (int i = 0; i < 5 && reader.readRow(row); ++i) {
        std::cout << "Row " << reader.getCurrentRowNumber() << ": ";
        for (size_t j = 0; j < row.size() && j < 5; ++j) { // Show first 5 columns
            std::cout << row[j];
            if (j < row.size() - 1 && j < 4) std::cout << ", ";
        }
        if (row.size() > 5) std::cout << "...";
        std::cout << "\n";
    }
    
    // Example 3: Reset and read as numbers (if applicable)
    std::cout << "\nResetting to beginning...\n";
    reader.reset();
    
    std::vector<double> numericRow;
    if (reader.readRowAsNumbers(numericRow)) {
        std::cout << "First row as numbers: ";
        for (size_t i = 0; i < numericRow.size() && i < 5; ++i) {
            std::cout << numericRow[i];
            if (i < numericRow.size() - 1 && i < 4) std::cout << ", ";
        }
        if (numericRow.size() > 5) std::cout << "...";
        std::cout << "\n";
    } else {
        std::cout << "First row cannot be converted to numbers\n";
    }
    
    std::cout << "\nCSVFileReader demonstration complete!\n";
}

void demonstrateCSVReaderWriter() {
    std::cout << "\n\nCSVFileReader + CSVFileWriter Example:\n";
    std::cout << "======================================\n\n";
    
    // Create a small test CSV file
    std::vector<std::string> headers = {"Name", "Age", "Score", "City"};
    CSVFileWriter writer("test_output.csv", headers);
    
    if (writer.isValid()) {
        writer.addMixedRow("Alice", 25, 95.5, "New York");
        writer.addMixedRow("Bob", 30, 87.2, "San Francisco");
        writer.addMixedRow("Charlie", 22, 92.8, "Chicago");
        writer.flush();
        std::cout << "Created test CSV file: test_output.csv\n";
    }
    
    // Now read it back
    CSVFileReader reader("test_output.csv");
    if (reader.isValid()) {
        std::cout << "Reading back the test file:\n";
        std::cout << "Headers: ";
        const auto& headers = reader.getHeaders();
        for (size_t i = 0; i < headers.size(); ++i) {
            std::cout << headers[i];
            if (i < headers.size() - 1) std::cout << " | ";
        }
        std::cout << "\n";
        
        std::vector<std::string> row;
        while (reader.readRow(row)) {
            for (size_t i = 0; i < row.size(); ++i) {
                std::cout << row[i];
                if (i < row.size() - 1) std::cout << " | ";
            }
            std::cout << "\n";
        }
    }
}

// Uncomment the main function below to run this example
/*
int main() {
    demonstrateCSVReader();
    demonstrateCSVReaderWriter();
    return 0;
}
*/