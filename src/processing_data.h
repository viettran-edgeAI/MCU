#ifndef PROCESSING_DATA_H
#define PROCESSING_DATA_H

#ifdef ARDUINO
    #include <Arduino.h>
    #include <SPIFFS.h>
    #include <FS.h>
#else
    #include <fstream>
    #include <iostream>
    #include <sstream>
    #include <algorithm>
    #include <cmath>
    #include <limits>
#endif

#include "STL_MCU.h"

namespace mcu {

    using u8 = uint8_t;

    // Structure to store feature statistics for Z-score calculation
    struct FeatureStats {
        float mean = 0.0f;
        float stdDev = 0.0f;
        float min = 3.4028235e+38f;  // FLT_MAX equivalent
        float max = -3.4028235e+38f; // -FLT_MAX equivalent
        bool isDiscrete = false;
    };

    class Categorizer {
    private:
        static constexpr uint32_t MAGIC_NUMBER = 0x43415447; // "CATG"
        static constexpr uint8_t VERSION = 1;
        
        uint16_t numFeatures = 0;
        uint8_t groupsPerFeature = 0;
        mcu::vector<mcu::pair<float,float>> featureRange;
        mcu::vector<mcu::vector<float>> discreteValues;
        mcu::vector<bool> isDiscrete;
        mcu::vector<mcu::vector<float>> quantileBinEdges;
        
        // Helper methods
#ifdef ARDUINO
        mcu::vector<String> splitString(const String &line) const;
#else
        mcu::vector<std::string> split(const std::string &line) const;
#endif
        mcu::vector<float> collectUniqueValues(const mcu::vector<mcu::vector<float>>& data, 
                                            int featureIdx, int numSamples) const;
        mcu::vector<float> computeQuantileBinEdges(mcu::vector<float> values, int numBins) const;
        float clipOutlier(float value, float mean, float stdDev, float minVal, float maxVal) const;
        void sortFloatArray(mcu::vector<float>& arr) const;

    public:
        // Constructors
        Categorizer() = default;
        Categorizer(uint16_t numFeatures, uint8_t gpf);
        
        // Core functionality
        void updateFeatureRange(uint16_t featureIdx, float value);
        void setQuantileBinEdges(uint16_t featureIdx, const mcu::vector<float>& edges);
        void setDiscreteFeature(uint16_t featureIdx, const mcu::vector<float>& values);
        uint8_t categorizeFeature(uint16_t featureIdx, float value) const;
        mcu::vector<uint8_t> categorizeSample(const mcu::vector<float>& sample) const;
        
        // File operations
        bool saveToBinary(const char* filename) const;
        bool loadFromBinary(const char* filename);
        
        #ifdef ARDUINO
        bool processCSVFromSPIFFS(const char* inputPath, const char* outputPath, int groupsPerFeature);
        #else
        bool processCSVFile(const char* inputPath, const char* outputPath, int groupsPerFeature);
        #endif
        
        // Utility
        void clear();
        size_t getMemoryUsage() const;
        void printInfo() const;
        
        // Accessors
        uint16_t getNumFeatures() const { return numFeatures; }
        uint8_t getGroupsPerFeature() const { return groupsPerFeature; }
    };

    // Global helper function for creating categorizer from CSV
    #ifdef ARDUINO
    Categorizer createCategorizerFromSPIFFS(const char* inputFilePath, 
                                                const char* outputFilePath, 
                                                int groupsPerFeature);
    #else
    Categorizer createCategorizerFromFile(const char* inputFilePath, 
                                            const char* outputFilePath, 
                                            int groupsPerFeature);
    #endif

} // namespace mcu

#endif // PROCESSING_DATA_H