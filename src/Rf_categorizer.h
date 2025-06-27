#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <vector>
#include <utility>

#include "STL_MCU.h"
#include "Rf_file_manager.h"

using u8 = uint8_t;

class Rf_categorizer {
private:
    uint16_t numFeatures = 0;
    uint8_t groupsPerFeature = 0;
    String filename = "";
    
    // Runtime data (loaded into RAM when needed)
    mcu::b_vector<bool> isDiscrete;
    mcu::b_vector<mcu::b_vector<float>> discreteValues;
    mcu::b_vector<mcu::b_vector<float>> quantileBinEdges;
    
    bool isLoaded = false;
    
    // Helper function to parse CSV line
    mcu::b_vector<String> split(const String& line, char delimiter = ',');
    bool convertToBin(const String& csvPath);
    
public:
    // Constructors
    Rf_categorizer();
    Rf_categorizer(const String& binFilename);
    
    // Serial transfer methods
    bool receiveFromPySerial(Stream& serial, unsigned long timeout = 30000);
    bool receiveFromSerialMonitor(bool print_file = false);
    
    // Data input methods
    /**
     * @brief Interactive categorizer data input using CSV format
     * 
     * This method uses the reception_data() function to allow interactive input
     * of categorizer data through the Serial interface. The user can enter
     * categorizer configuration data in CSV format and it will be automatically
     * converted to binary format for efficient storage and loading.
     * 
     * Expected CSV format:
     * Line 1: numFeatures,groupsPerFeature
     * Line 2+: isDiscrete,dataCount,value1,value2,value3,...
     * 
     * @param exact_columns Number of expected columns for validation (default: 234)
     * @param print_file Whether to print the resulting file contents (default: false)
     * @return true if data was successfully received and processed, false otherwise
     * 
     * @note The binary file will be saved with "_categorizer.bin" suffix
     */
    
    // Memory management
    bool loadCtg();
    void releaseCtg();
    
    // Categorization methods
    uint8_t categorizeFeature(uint16_t featureIdx, float value) const;
    mcu::b_vector<uint8_t> categorizeSample(const mcu::b_vector<float>& sample) const;
    
    // Utility methods
    bool isValid() const;
    uint16_t getNumFeatures() const { return numFeatures; }
    uint8_t getGroupsPerFeature() const { return groupsPerFeature; }
    String getFilename() const { return filename; }
    bool getIsLoaded() const { return isLoaded; }
    
    // Debug methods
    void printInfo() const;
    size_t getMemoryUsage() const;
};

