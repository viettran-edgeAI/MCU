#include "processing_data.h"

#ifdef ARDUINO
#include <SPIFFS.h>
#include <Arduino.h>
#else
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <cmath>
#endif

namespace mcu {
    // Constructor
    Categorizer::Categorizer(uint16_t numFeatures, uint8_t gpf) {
        this->numFeatures = numFeatures;
        groupsPerFeature = gpf;
        featureRange.reserve(numFeatures);
        isDiscrete.reserve(numFeatures);
        discreteValues.reserve(numFeatures);
        quantileBinEdges.reserve(numFeatures);
        
        for(int i = 0; i < numFeatures; ++i){
            featureRange.push_back({99999.0f, -99999.0f}); // Initialize with extreme values
            isDiscrete.push_back(false);
            discreteValues.push_back(mcu::vector<float>());
            quantileBinEdges.push_back(mcu::vector<float>());
        }
    }

#ifdef ARDUINO
    // Arduino-compatible string splitting
    mcu::vector<String> Categorizer::splitString(const String &line) const {
        mcu::vector<String> out;
        int start = 0;
        for (int i = 0; i <= line.length(); i++) {
            if (i == line.length() || line[i] == ',') {
                out.push_back(line.substring(start, i));
                start = i + 1;
            }
        }
        return out;
    }

    // Arduino-compatible sorting
    void Categorizer::sortFloatArray(mcu::vector<float>& arr) const {
        // Simple bubble sort for embedded systems
        for (size_t i = 0; i < arr.size() - 1; i++) {
            for (size_t j = 0; j < arr.size() - i - 1; j++) {
                if (arr[j] > arr[j + 1]) {
                    float temp = arr[j];
                    arr[j] = arr[j + 1];
                    arr[j + 1] = temp;
                }
            }
        }
    }
#else
    // Helper: Split string by comma
    mcu::vector<std::string> Categorizer::split(const std::string &line) const {
        mcu::vector<std::string> out;
        std::istringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, ',')) {
            out.push_back(cell);
        }
        return out;
    }

    void Categorizer::sortFloatArray(mcu::vector<float>& arr) const {
        std::sort(arr.begin(), arr.end());
    }
#endif

    // Helper: Collect unique values
    mcu::vector<float> Categorizer::collectUniqueValues(
        const mcu::vector<mcu::vector<float>>& data, int featureIdx, int numSamples) const {
        mcu::vector<float> unique;
        
        for (int i = 0; i < numSamples; ++i) {
            float value = data[i][featureIdx];
            
            bool found = false;
            for (size_t j = 0; j < unique.size(); ++j) {
                if (abs(unique[j] - value) < 1e-6f) { // Epsilon comparison for floats
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                unique.push_back(value);
            }
        }
        
        return unique;
    }

    // Helper: Compute quantile bin edges
    mcu::vector<float> Categorizer::computeQuantileBinEdges(mcu::vector<float> values, int numBins) const {
        mcu::vector<float> edges;
        if (values.size() < 2 || numBins < 1) return edges;
        
        sortFloatArray(values);
        for (int b = 1; b < numBins; ++b) {
            float q = b * (values.size() / (float)numBins);
            int idx = static_cast<int>(q);
            if (idx >= values.size()) idx = values.size() - 1;
            edges.push_back(values[idx]);
        }
        return edges;
    }

    // Helper: Clip outliers using Z-score
    float Categorizer::clipOutlier(float value, float mean, float stdDev, 
                                    float minVal, float maxVal) const {
        if (stdDev > 1e-6f) {
            float zScore = (value - mean) / stdDev;
            const float THRESHOLD = 3.0f;
            
            if (zScore > THRESHOLD) {
                return mean + THRESHOLD * stdDev;
            } else if (zScore < -THRESHOLD) {
                return mean - THRESHOLD * stdDev;
            }
        }
        return value;
    }

    // Update feature range
    void Categorizer::updateFeatureRange(uint16_t featureIdx, float value) {
        if(featureIdx < numFeatures) {
#ifdef ARDUINO
            featureRange[featureIdx].first = min(featureRange[featureIdx].first, value);
            featureRange[featureIdx].second = max(featureRange[featureIdx].second, value);
#else
            featureRange[featureIdx].first = std::min(featureRange[featureIdx].first, value);
            featureRange[featureIdx].second = std::max(featureRange[featureIdx].second, value);
#endif
        }
    }

    // Set quantile bin edges
    void Categorizer::setQuantileBinEdges(uint16_t featureIdx, const mcu::vector<float>& edges) {
        if(featureIdx < numFeatures) {
            quantileBinEdges[featureIdx] = edges;
        }
    }

    // Set discrete feature values
    void Categorizer::setDiscreteFeature(uint16_t featureIdx, const mcu::vector<float>& values) {
        if(featureIdx < numFeatures) {
            isDiscrete[featureIdx] = true;
            discreteValues[featureIdx] = values;
        }
    }

    // Categorize single feature
    uint8_t Categorizer::categorizeFeature(uint16_t featureIdx, float value) const {
        if(featureIdx >= numFeatures) return 0;
        
        if(isDiscrete[featureIdx]) {
            const auto& values = discreteValues[featureIdx];
            for(uint8_t i = 0; i < values.size(); ++i) {
                if(values[i] == value) return i;
            }
            return 0;
        } else {
            const auto& edges = quantileBinEdges[featureIdx];
            for(uint8_t bin = 0; bin < edges.size(); ++bin) {
                if(value < edges[bin]) {
                    return bin;
                }
            }
            return edges.size();
        }
    }

    // Categorize entire sample
    mcu::vector<uint8_t> Categorizer::categorizeSample(const mcu::vector<float>& sample) const {
        mcu::vector<uint8_t> result;
        result.reserve(numFeatures);
        
        for(uint16_t i = 0; i < numFeatures && i < sample.size(); ++i) {
            result.push_back(categorizeFeature(i, sample[i]));
        }
        
        return result;
    }

    #ifdef ARDUINO
    // Save to binary file (ESP32 SPIFFS)
    bool Categorizer::saveToBinary(const char* filename) const {
        if (!SPIFFS.begin(true)) {
            Serial.println("Failed to mount SPIFFS");
            return false;
        }
        
        File file = SPIFFS.open(filename, FILE_WRITE);
        if (!file) {
            Serial.println("Failed to open file for writing");
            return false;
        }
        
        // Write header
        file.write((uint8_t*)&MAGIC_NUMBER, sizeof(MAGIC_NUMBER));
        file.write(&VERSION, sizeof(VERSION));
        file.write((uint8_t*)&numFeatures, sizeof(numFeatures));
        file.write(&groupsPerFeature, sizeof(groupsPerFeature));
        
        // Write feature ranges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            float minVal = featureRange[i].first;
            float maxVal = featureRange[i].second;
            file.write((uint8_t*)&minVal, sizeof(minVal));
            file.write((uint8_t*)&maxVal, sizeof(maxVal));
        }
        
        // Write discrete flags
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint8_t flag = isDiscrete[i] ? 1 : 0;
            file.write(&flag, sizeof(flag));
        }
        
        // Write discrete values
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count = discreteValues[i].size();
            file.write((uint8_t*)&count, sizeof(count));
            for (uint16_t j = 0; j < count; ++j) {
                float val = discreteValues[i][j];
                file.write((uint8_t*)&val, sizeof(val));
            }
        }
        
        // Write quantile bin edges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count = quantileBinEdges[i].size();
            file.write((uint8_t*)&count, sizeof(count));
            for (uint16_t j = 0; j < count; ++j) {
                float edge = quantileBinEdges[i][j];
                file.write((uint8_t*)&edge, sizeof(edge));
            }
        }
        
        file.close();
        Serial.printf("Categorizer saved to %s (%d bytes)\n", filename, file.size());
        return true;
    }

    // Load from binary file (ESP32 SPIFFS)
    bool Categorizer::loadFromBinary(const char* filename) {
        if (!SPIFFS.begin(true)) {
            Serial.println("Failed to mount SPIFFS");
            return false;
        }
        
        File file = SPIFFS.open(filename, FILE_READ);
        if (!file) {
            Serial.println("Failed to open file for reading");
            return false;
        }
        
        // Read and verify header
        uint32_t magic;
        uint8_t version;
        file.read((uint8_t*)&magic, sizeof(magic));
        file.read(&version, sizeof(version));
        
        if (magic != MAGIC_NUMBER || version != VERSION) {
            Serial.println("Invalid file format or version");
            file.close();
            return false;
        }
        
        // Read basic info
        file.read((uint8_t*)&numFeatures, sizeof(numFeatures));
        file.read(&groupsPerFeature, sizeof(groupsPerFeature));
        
        // Clear and resize vectors
        featureRange.clear();
        isDiscrete.clear();
        discreteValues.clear();
        quantileBinEdges.clear();
        
        featureRange.reserve(numFeatures);
        isDiscrete.reserve(numFeatures);
        discreteValues.reserve(numFeatures);
        quantileBinEdges.reserve(numFeatures);
        
        // Read feature ranges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            float minVal, maxVal;
            file.read((uint8_t*)&minVal, sizeof(minVal));
            file.read((uint8_t*)&maxVal, sizeof(maxVal));
            featureRange.push_back({minVal, maxVal});
        }
        
        // Read discrete flags
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint8_t flag;
            file.read(&flag, sizeof(flag));
            isDiscrete.push_back(flag == 1);
        }
        
        // Read discrete values
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count;
            file.read((uint8_t*)&count, sizeof(count));
            mcu::vector<float> values;
            values.reserve(count);
            for (uint16_t j = 0; j < count; ++j) {
                float val;
                file.read((uint8_t*)&val, sizeof(val));
                values.push_back(val);
            }
            discreteValues.push_back(values);
        }
        
        // Read quantile bin edges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count;
            file.read((uint8_t*)&count, sizeof(count));
            mcu::vector<float> edges;
            edges.reserve(count);
            for (uint16_t j = 0; j < count; ++j) {
                float edge;
                file.read((uint8_t*)&edge, sizeof(edge));
                edges.push_back(edge);
            }
            quantileBinEdges.push_back(edges);
        }
        
        file.close();
        Serial.printf("Categorizer loaded from %s\n", filename);
        return true;
    }

    // Process CSV from SPIFFS
    bool Categorizer::processCSVFromSPIFFS(const char* inputPath, const char* outputPath, int groupsPerFeature) {
        if (!SPIFFS.begin(true)) {
            Serial.println("Failed to mount SPIFFS");
            return false;
        }
        
        File inputFile = SPIFFS.open(inputPath, FILE_READ);
        if (!inputFile) {
            Serial.printf("Cannot open input file: %s\n", inputPath);
            return false;
        }
        
        // Read header
        String header = inputFile.readStringUntil('\n');
        header.trim();
        
        // Count columns
        int n_cols = 1;
        for (int i = 0; i < header.length(); i++) {
            if (header[i] == ',') n_cols++;
        }
        
        if (n_cols < 2) {
            Serial.println("CSV needs at least label + one feature");
            inputFile.close();
            return false;
        }
        
        int n_feats = n_cols - 1;
        this->numFeatures = n_feats;
        this->groupsPerFeature = groupsPerFeature;
        
        // Initialize vectors with Arduino-compatible limits
        featureRange.clear();
        isDiscrete.clear();
        discreteValues.clear();
        quantileBinEdges.clear();
        
        for(int i = 0; i < numFeatures; ++i){
            featureRange.push_back({3.4028235e+38f, -3.4028235e+38f}); // FLT_MAX equivalents
            isDiscrete.push_back(false);
            discreteValues.push_back(mcu::vector<float>());
            quantileBinEdges.push_back(mcu::vector<float>());
        }
        
        // First pass: collect all data
        mcu::vector<String> labels;
        mcu::vector<mcu::vector<float>> data;
        mcu::vector<mcu::vector<float>> featureStats(n_feats);
        
        while (inputFile.available()) {
            String line = inputFile.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            
            // Parse CSV line using Arduino-compatible method
            mcu::vector<String> cells = splitString(line);
            
            if (cells.size() != n_cols) continue;
            
            labels.push_back(cells[0]);
            mcu::vector<float> feats;
            feats.reserve(n_feats);
            
            for (int j = 1; j < n_cols; ++j) {
                float val = cells[j].toFloat();
                feats.push_back(val);
                featureStats[j-1].push_back(val);
            }
            data.push_back(feats);
        }
        inputFile.close();
        
        int n_samples = data.size();
        if (n_samples == 0) {
            Serial.println("No data rows found in file");
            return false;
        }
        
        // Process features similar to desktop version
        for (int j = 0; j < n_feats; ++j) {
            // Calculate mean and std dev
            float sum = 0;
            for (float val : featureStats[j]) {
                sum += val;
            }
            float mean = sum / featureStats[j].size();
            
            float variance = 0;
            for (float val : featureStats[j]) {
                variance += (val - mean) * (val - mean);
            }
            float stdDev = sqrt(variance / featureStats[j].size());
            
            // Find min/max
            float minVal = featureStats[j][0];
            float maxVal = featureStats[j][0];
            for (float val : featureStats[j]) {
                minVal = min(minVal, val);
                maxVal = max(maxVal, val);
            }
            
            // Apply outlier clipping and update ranges
            for (int i = 0; i < n_samples; ++i) {
                data[i][j] = clipOutlier(data[i][j], mean, stdDev, minVal, maxVal);
                updateFeatureRange(j, data[i][j]);
            }
            
            // Determine if discrete or continuous
            mcu::vector<float> unique = collectUniqueValues(data, j, n_samples);
            if (unique.size() <= groupsPerFeature) {
                setDiscreteFeature(j, unique);
            } else {
                mcu::vector<float> values;
                values.reserve(n_samples);
                for (int i = 0; i < n_samples; ++i) {
                    values.push_back(data[i][j]);
                }
                mcu::vector<float> edges = computeQuantileBinEdges(values, groupsPerFeature);
                setQuantileBinEdges(j, edges);
            }
        }
        
        // Write output file
        File outputFile = SPIFFS.open(outputPath, FILE_WRITE);
        if (!outputFile) {
            Serial.printf("Cannot open output file: %s\n", outputPath);
            return false;
        }
        
        outputFile.println(header);
        for (int i = 0; i < n_samples; ++i) {
            outputFile.print(labels[i]);
            mcu::vector<uint8_t> categorized = categorizeSample(data[i]);
            for (uint8_t cat : categorized) {
                outputFile.print(",");
                outputFile.print((int)cat);
            }
            outputFile.println();
        }
        outputFile.close();
        
        Serial.println("CSV processing completed");
        return true;
    }
    #else
    // Desktop versions (complete implementation)
    bool Categorizer::saveToBinary(const char* filename) const {
        std::ofstream file(filename, std::ios::binary);
        if (!file) return false;
        
        // Write header
        file.write(reinterpret_cast<const char*>(&MAGIC_NUMBER), sizeof(MAGIC_NUMBER));
        file.write(reinterpret_cast<const char*>(&VERSION), sizeof(VERSION));
        file.write(reinterpret_cast<const char*>(&numFeatures), sizeof(numFeatures));
        file.write(reinterpret_cast<const char*>(&groupsPerFeature), sizeof(groupsPerFeature));
        
        // Write feature ranges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            float minVal = featureRange[i].first;
            float maxVal = featureRange[i].second;
            file.write(reinterpret_cast<const char*>(&minVal), sizeof(minVal));
            file.write(reinterpret_cast<const char*>(&maxVal), sizeof(maxVal));
        }
        
        // Write discrete flags
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint8_t flag = isDiscrete[i] ? 1 : 0;
            file.write(reinterpret_cast<const char*>(&flag), sizeof(flag));
        }
        
        // Write discrete values
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count = discreteValues[i].size();
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (uint16_t j = 0; j < count; ++j) {
                float val = discreteValues[i][j];
                file.write(reinterpret_cast<const char*>(&val), sizeof(val));
            }
        }
        
        // Write quantile bin edges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count = quantileBinEdges[i].size();
            file.write(reinterpret_cast<const char*>(&count), sizeof(count));
            for (uint16_t j = 0; j < count; ++j) {
                float edge = quantileBinEdges[i][j];
                file.write(reinterpret_cast<const char*>(&edge), sizeof(edge));
            }
        }
        
        file.close();
        std::cout << "Categorizer saved to " << filename << std::endl;
        return true;
    }

    bool Categorizer::loadFromBinary(const char* filename) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        
        // Read and verify header
        uint32_t magic;
        uint8_t version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        
        if (magic != MAGIC_NUMBER || version != VERSION) {
            std::cout << "Invalid file format or version" << std::endl;
            file.close();
            return false;
        }
        
        // Read basic info
        file.read(reinterpret_cast<char*>(&numFeatures), sizeof(numFeatures));
        file.read(reinterpret_cast<char*>(&groupsPerFeature), sizeof(groupsPerFeature));
        
        // Clear and resize vectors
        featureRange.clear();
        isDiscrete.clear();
        discreteValues.clear();
        quantileBinEdges.clear();
        
        featureRange.reserve(numFeatures);
        isDiscrete.reserve(numFeatures);
        discreteValues.reserve(numFeatures);
        quantileBinEdges.reserve(numFeatures);
        
        // Read feature ranges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            float minVal, maxVal;
            file.read(reinterpret_cast<char*>(&minVal), sizeof(minVal));
            file.read(reinterpret_cast<char*>(&maxVal), sizeof(maxVal));
            featureRange.push_back({minVal, maxVal});
        }
        
        // Read discrete flags
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint8_t flag;
            file.read(reinterpret_cast<char*>(&flag), sizeof(flag));
            isDiscrete.push_back(flag == 1);
        }
        
        // Read discrete values
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            mcu::vector<float> values;
            values.reserve(count);
            for (uint16_t j = 0; j < count; ++j) {
                float val;
                file.read(reinterpret_cast<char*>(&val), sizeof(val));
                values.push_back(val);
            }
            discreteValues.push_back(values);
        }
        
        // Read quantile bin edges
        for (uint16_t i = 0; i < numFeatures; ++i) {
            uint16_t count;
            file.read(reinterpret_cast<char*>(&count), sizeof(count));
            mcu::vector<float> edges;
            edges.reserve(count);
            for (uint16_t j = 0; j < count; ++j) {
                float edge;
                file.read(reinterpret_cast<char*>(&edge), sizeof(edge));
                edges.push_back(edge);
            }
            quantileBinEdges.push_back(edges);
        }
        
        file.close();
        std::cout << "Categorizer loaded from " << filename << std::endl;
        return true;
    }

    // Desktop CSV processing function
    bool Categorizer::processCSVFile(const char* inputPath, const char* outputPath, int groupsPerFeature) {
        std::ifstream inputFile(inputPath);
        if (!inputFile) {
            std::cout << "Cannot open input file: " << inputPath << std::endl;
            return false;
        }
        
        // Read header
        std::string header;
        std::getline(inputFile, header);
        auto cols = split(header);
        int n_cols = (int)cols.size();
        
        if (n_cols < 2) {
            std::cout << "CSV needs at least label + one feature" << std::endl;
            inputFile.close();
            return false;
        }
        
        int n_feats = n_cols - 1;
        this->numFeatures = n_feats;
        this->groupsPerFeature = groupsPerFeature;
        
        // Initialize vectors
        featureRange.clear();
        isDiscrete.clear();
        discreteValues.clear();
        quantileBinEdges.clear();
        
        for(int i = 0; i < numFeatures; ++i){
            featureRange.push_back({std::numeric_limits<float>::max(), 
                                std::numeric_limits<float>::lowest()});
            isDiscrete.push_back(false);
            discreteValues.push_back(mcu::vector<float>());
            quantileBinEdges.push_back(mcu::vector<float>());
        }
        
        // First pass: collect data and calculate statistics
        mcu::vector<mcu::vector<float>> featureStats(n_feats);
        mcu::vector<std::string> labels;
        mcu::vector<mcu::vector<float>> data;
        
        std::string line;
        while (std::getline(inputFile, line)) {
            if (line.empty()) continue;
            auto cells = split(line);
            if ((int)cells.size() != n_cols) continue;
            
            labels.push_back(cells[0]);
            mcu::vector<float> feats;
            feats.reserve(n_feats);
            
            for (int j = 1; j < n_cols; ++j) {
                float val = std::stof(cells[j]);
                feats.push_back(val);
                featureStats[j-1].push_back(val);
            }
            data.push_back(std::move(feats));
        }
        inputFile.close();
        
        int n_samples = data.size();
        if (n_samples == 0) {
            std::cout << "No data rows found in file" << std::endl;
            return false;
        }
        
        // Calculate means and std devs for outlier detection
        for (int j = 0; j < n_feats; ++j) {
            float sum = 0;
            for (float val : featureStats[j]) {
                sum += val;
            }
            float mean = sum / featureStats[j].size();
            
            float variance = 0;
            for (float val : featureStats[j]) {
                variance += (val - mean) * (val - mean);
            }
            float stdDev = std::sqrt(variance / featureStats[j].size());
            
            // Apply outlier clipping to data
            for (int i = 0; i < n_samples; ++i) {
                data[i][j] = clipOutlier(data[i][j], mean, stdDev, 
                                       *std::min_element(featureStats[j].begin(), featureStats[j].end()),
                                       *std::max_element(featureStats[j].begin(), featureStats[j].end()));
                updateFeatureRange(j, data[i][j]);
            }
            
            // Check if feature is discrete
            mcu::vector<float> unique = collectUniqueValues(data, j, n_samples);
            if (unique.size() <= groupsPerFeature) {
                setDiscreteFeature(j, unique);
            } else {
                // Compute quantile bin edges
                mcu::vector<float> values;
                values.reserve(n_samples);
                for (int i = 0; i < n_samples; ++i) {
                    values.push_back(data[i][j]);
                }
                mcu::vector<float> edges = computeQuantileBinEdges(values, groupsPerFeature);
                setQuantileBinEdges(j, edges);
            }
        }
        
        // Write output CSV
        std::ofstream outputFile(outputPath);
        if (!outputFile) {
            std::cout << "Cannot open output file: " << outputPath << std::endl;
            return false;
        }
        
        outputFile << header << "\n";
        for (int i = 0; i < n_samples; ++i) {
            outputFile << labels[i];
            mcu::vector<uint8_t> categorized = categorizeSample(data[i]);
            for (uint8_t cat : categorized) {
                outputFile << "," << static_cast<int>(cat);
            }
            outputFile << "\n";
        }
        outputFile.close();
        
        std::cout << "CSV processing completed" << std::endl;
        return true;
    }
    #endif

    // Clear all data
    void Categorizer::clear() {
        numFeatures = 0;
        groupsPerFeature = 0;
        featureRange.clear();
        discreteValues.clear();
        isDiscrete.clear();
        quantileBinEdges.clear();
    }

    // Get memory usage
    size_t Categorizer::getMemoryUsage() const {
        size_t total = sizeof(*this);
        
        // Add vector memory usage
        total += featureRange.size() * sizeof(mcu::pair<float,float>);
        total += isDiscrete.size() * sizeof(bool);
        
        for (const auto& vec : discreteValues) {
            total += vec.size() * sizeof(float);
        }
        
        for (const auto& vec : quantileBinEdges) {
            total += vec.size() * sizeof(float);
        }
        
        return total;
    }

    // Print debug info
    void Categorizer::printInfo() const {
        #ifdef ARDUINO
        Serial.printf("Categorizer Info:\n");
        Serial.printf("  Features: %d\n", numFeatures);
        Serial.printf("  Groups per feature: %d\n", groupsPerFeature);
        Serial.printf("  Memory usage: %d bytes\n", getMemoryUsage());
        #else
        std::cout << "Categorizer Info:\n";
        std::cout << "  Features: " << numFeatures << "\n";
        std::cout << "  Groups per feature: " << (int)groupsPerFeature << "\n";
        std::cout << "  Memory usage: " << getMemoryUsage() << " bytes\n";
        #endif
    }
} // namespace mcu