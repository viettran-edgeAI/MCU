/**
 * CSV to Binary Dataset Converter for ESP32
 * 
 * This program converts normalized CSV datasets (output from processing_data.cpp)
 * to ESP32-compatible binary format that exactly matches Rf_data structure.
 * 
 * Usage: ./csv_to_bin <input.csv> <output.bin> <num_features>
 * 
 * Input CSV format (no header):
 * label,feature1,feature2,...,featureN
 * 
 * Output binary format (ESP32 compatible):
 * - Header: 4-byte sample count + 2-byte feature count
 * - Each sample: 2-byte ID + 1-byte label + packed features (4 per byte, 2 bits each)
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>

// ESP32 Configuration Constants
static constexpr uint8_t QUANTIZATION_COEFFICIENT = 2;
static constexpr uint8_t MAX_FEATURE_VALUE = 3; // 2^2 - 1
static constexpr uint8_t FEATURES_PER_BYTE = 4; // 8 / 2

// ESP32-compatible sample structure
struct ESP32_Sample {
    std::vector<uint8_t> features;
    uint8_t label;
    
    ESP32_Sample() : label(0) {}
    
    bool validate() const {
        for (uint8_t feature : features) {
            if (feature > MAX_FEATURE_VALUE) {
                return false;
            }
        }
        return true;
    }
};

// Split CSV line into fields
std::vector<std::string> splitCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream ss(line);
    std::string field;
    
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    
    return fields;
}

// Load CSV data and validate
std::vector<ESP32_Sample> loadCSVData(const std::string& csvFilename, uint8_t expectedFeatures) {
    std::cout << "🔄 Loading CSV data from: " << csvFilename << std::endl;
    
    std::ifstream file(csvFilename);
    if (!file) {
        throw std::runtime_error("Cannot open CSV file: " + csvFilename);
    }
    
    std::vector<ESP32_Sample> samples;
    std::string line;
    int lineCount = 0;
    int errorCount = 0;
    int validSamples = 0;
    
    while (std::getline(file, line)) {
        lineCount++;
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        if (line.empty()) continue;
        
        auto fields = splitCSVLine(line);
        
        // Validate field count (label + features)
        if (fields.size() != expectedFeatures + 1) {
            std::cout << "❌ Line " << lineCount << ": expected " << (expectedFeatures + 1) 
                      << " fields, got " << fields.size() << std::endl;
            errorCount++;
            continue;
        }
        
        ESP32_Sample sample;
        sample.features.reserve(expectedFeatures);
        
        try {
            // Parse label
            int labelValue = std::stoi(fields[0]);
            if (labelValue < 0 || labelValue > 255) {
                std::cout << "❌ Line " << lineCount << ": invalid label " << labelValue << std::endl;
                errorCount++;
                continue;
            }
            sample.label = static_cast<uint8_t>(labelValue);
            
            // Parse features
            bool parseError = false;
            for (size_t i = 1; i < fields.size(); ++i) {
                int featureValue = std::stoi(fields[i]);
                
                if (featureValue < 0 || featureValue > MAX_FEATURE_VALUE) {
                    std::cout << "❌ Line " << lineCount << ", feature " << (i-1) 
                              << ": value " << featureValue << " outside valid range [0," 
                              << (int)MAX_FEATURE_VALUE << "]" << std::endl;
                    parseError = true;
                    break;
                }
                
                sample.features.push_back(static_cast<uint8_t>(featureValue));
            }
            
            if (parseError) {
                errorCount++;
                continue;
            }
            
            // Final validation
            if (!sample.validate()) {
                std::cout << "❌ Line " << lineCount << ": sample validation failed" << std::endl;
                errorCount++;
                continue;
            }
            
            samples.push_back(sample);
            validSamples++;
            
            // ESP32 sample limit check
            if (validSamples >= 10000) {
                std::cout << "⚠️  Reached ESP32 sample limit (10000), stopping." << std::endl;
                break;
            }
            
        } catch (const std::exception& e) {
            std::cout << "❌ Line " << lineCount << " parse error: " << e.what() << std::endl;
            errorCount++;
            continue;
        }
    }
    
    file.close();
    
    std::cout << "✅ CSV loading completed:" << std::endl;
    std::cout << "   📊 Valid samples loaded: " << validSamples << std::endl;
    std::cout << "   🔢 Features per sample: " << (int)expectedFeatures << std::endl;
    std::cout << "   📋 Lines processed: " << lineCount << std::endl;
    std::cout << "   ❌ Errors encountered: " << errorCount << std::endl;
    
    if (errorCount > 0) {
        std::cout << "   ⚠️  " << errorCount << " problematic samples were skipped" << std::endl;
    }
    
    return samples;
}

// Convert samples to ESP32-compatible binary format
void saveBinaryDataset(const std::vector<ESP32_Sample>& samples, 
                      const std::string& binaryFilename, 
                      uint16_t numFeatures) {
    std::cout << "🔄 Converting to ESP32 binary format: " << binaryFilename << std::endl;
    
    std::ofstream file(binaryFilename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create binary file: " + binaryFilename);
    }
    
    // Write ESP32-compatible header
    uint32_t numSamples = static_cast<uint32_t>(samples.size());
    uint16_t numFeatures_header = numFeatures;
    
    std::cout << "📊 Binary header:" << std::endl;
    std::cout << "   Samples: " << numSamples << " (4 bytes, little-endian)" << std::endl;
    std::cout << "   Features: " << numFeatures_header << " (2 bytes, little-endian)" << std::endl;
    
    // Write header (exactly like ESP32 Rf_data)
    file.write(reinterpret_cast<const char*>(&numSamples), sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&numFeatures_header), sizeof(uint16_t));
    
    // Calculate packed bytes needed for features
    uint16_t packedFeatureBytes = (numFeatures + FEATURES_PER_BYTE - 1) / FEATURES_PER_BYTE;
    
    std::cout << "🗜️  Packing configuration:" << std::endl;
    std::cout << "   Features per byte: " << (int)FEATURES_PER_BYTE << std::endl;
    std::cout << "   Packed bytes per sample: " << packedFeatureBytes << std::endl;
    std::cout << "   Total bytes per sample: " << (2 + 1 + packedFeatureBytes) << " (ID + label + features)" << std::endl;
    
    // Write samples (exactly like ESP32 Rf_data)
    for (size_t i = 0; i < samples.size(); ++i) {
        const ESP32_Sample& sample = samples[i];
        
        // Write sample ID (exactly like ESP32)
        uint16_t sampleID = static_cast<uint16_t>(i);
        file.write(reinterpret_cast<const char*>(&sampleID), sizeof(uint16_t));
        
        // Write label (exactly like ESP32)
        file.write(reinterpret_cast<const char*>(&sample.label), sizeof(uint8_t));
        
        // Pack and write features (exactly like ESP32)
        std::vector<uint8_t> packedBuffer(packedFeatureBytes, 0);
        
        // Pack 4 feature values into each byte (2 bits each)
        for (size_t f = 0; f < sample.features.size(); ++f) {
            uint16_t byteIndex = f / FEATURES_PER_BYTE;
            uint8_t bitOffset = (f % FEATURES_PER_BYTE) * QUANTIZATION_COEFFICIENT;
            uint8_t feature_value = sample.features[f] & ((1 << QUANTIZATION_COEFFICIENT) - 1);
            
            if (byteIndex < packedBuffer.size()) {
                packedBuffer[byteIndex] |= (feature_value << bitOffset);
            }
        }
        
        file.write(reinterpret_cast<const char*>(packedBuffer.data()), packedFeatureBytes);
        
        // Debug first few samples
        if (i < 3) {
            std::cout << "📝 Sample " << i << " (ID=" << sampleID << "):" << std::endl;
            std::cout << "   Label: " << (int)sample.label << std::endl;
            std::cout << "   Features: ";
            for (size_t f = 0; f < std::min(sample.features.size(), (size_t)16); ++f) {
                std::cout << (int)sample.features[f];
                if (f < std::min(sample.features.size(), (size_t)16) - 1) std::cout << ",";
            }
            if (sample.features.size() > 16) std::cout << "...";
            std::cout << std::endl;
            
            std::cout << "   Packed bytes: ";
            for (size_t b = 0; b < std::min(packedBuffer.size(), (size_t)8); ++b) {
                std::cout << "0x" << std::hex << (int)packedBuffer[b] << std::dec;
                if (b < std::min(packedBuffer.size(), (size_t)8) - 1) std::cout << " ";
            }
            if (packedBuffer.size() > 8) std::cout << "...";
            std::cout << std::endl;
        }
    }
    
    file.close();
    
    // Verify file size
    std::ifstream check(binaryFilename, std::ios::binary | std::ios::ate);
    if (check) {
        size_t fileSize = check.tellg();
        check.close();
        
        size_t expectedSize = 6 + samples.size() * (3 + packedFeatureBytes); // header + samples
        
        std::cout << "✅ Binary conversion completed:" << std::endl;
        std::cout << "   📁 File: " << binaryFilename << std::endl;
        std::cout << "   📊 Samples written: " << samples.size() << std::endl;
        std::cout << "   💾 File size: " << fileSize << " bytes" << std::endl;
        std::cout << "   🎯 Expected size: " << expectedSize << " bytes" << std::endl;
        
        if (fileSize == expectedSize) {
            std::cout << "   ✅ File size matches ESP32 expectation" << std::endl;
        } else {
            std::cout << "   ❌ File size mismatch!" << std::endl;
        }
    }
}

// Verify binary format by reading back
bool verifyBinaryFormat(const std::string& binaryFilename) {
    std::cout << "\n🔍 Verifying ESP32 binary compatibility..." << std::endl;
    
    std::ifstream file(binaryFilename, std::ios::binary);
    if (!file) {
        std::cout << "❌ Cannot open binary file for verification" << std::endl;
        return false;
    }
    
    // Read and verify header
    uint32_t numSamples;
    uint16_t numFeatures;
    file.read(reinterpret_cast<char*>(&numSamples), sizeof(uint32_t));
    file.read(reinterpret_cast<char*>(&numFeatures), sizeof(uint16_t));
    
    std::cout << "📊 Header verification:" << std::endl;
    std::cout << "   Samples: " << numSamples << std::endl;
    std::cout << "   Features: " << numFeatures << std::endl;
    
    if (numSamples == 0 || numFeatures == 0) {
        std::cout << "❌ Invalid header values" << std::endl;
        file.close();
        return false;
    }
    
    uint16_t packedFeatureBytes = (numFeatures + FEATURES_PER_BYTE - 1) / FEATURES_PER_BYTE;
    bool allValid = true;
    
    // Verify first few samples
    uint32_t samplesToCheck = std::min(numSamples, 5u);
    for (uint32_t i = 0; i < samplesToCheck; ++i) {
        uint16_t sampleId;
        uint8_t label;
        file.read(reinterpret_cast<char*>(&sampleId), sizeof(uint16_t));
        file.read(reinterpret_cast<char*>(&label), sizeof(uint8_t));
        
        std::vector<uint8_t> packedFeatures(packedFeatureBytes);
        file.read(reinterpret_cast<char*>(packedFeatures.data()), packedFeatureBytes);
        
        // Unpack and verify features
        for (uint16_t f = 0; f < numFeatures; ++f) {
            uint16_t byteIndex = f / FEATURES_PER_BYTE;
            uint8_t bitOffset = (f % FEATURES_PER_BYTE) * QUANTIZATION_COEFFICIENT;
            uint8_t feature_value = (packedFeatures[byteIndex] >> bitOffset) & ((1 << QUANTIZATION_COEFFICIENT) - 1);
            
            if (feature_value > MAX_FEATURE_VALUE) {
                std::cout << "❌ Sample " << i << ", feature " << f 
                          << ": invalid value " << (int)feature_value << std::endl;
                allValid = false;
            }
        }
        
        if (i < 3) {
            std::cout << "✅ Sample " << i << " verified (ID=" << sampleId << ", label=" << (int)label << ")" << std::endl;
        }
    }
    
    file.close();
    
    if (allValid) {
        std::cout << "✅ Binary format is ESP32-compatible!" << std::endl;
    } else {
        std::cout << "❌ Binary format has compatibility issues!" << std::endl;
    }
    
    return allValid;
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <input.csv> <output.bin> <num_features>" << std::endl;
    std::cout << std::endl;
    std::cout << "Convert normalized CSV dataset to ESP32-compatible binary format." << std::endl;
    std::cout << std::endl;
    std::cout << "Arguments:" << std::endl;
    std::cout << "  input.csv     : Input CSV file (no header, format: label,feature1,feature2,...)" << std::endl;
    std::cout << "  output.bin    : Output binary file (ESP32 Rf_data compatible)" << std::endl;
    std::cout << "  num_features  : Number of features per sample" << std::endl;
    std::cout << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Quantization: " << (int)QUANTIZATION_COEFFICIENT << " bits per feature" << std::endl;
    std::cout << "  Valid range : 0-" << (int)MAX_FEATURE_VALUE << std::endl;
    std::cout << "  Packing     : " << (int)FEATURES_PER_BYTE << " features per byte" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << programName << " walker_fall_standard.csv walker_fall_standard.bin 234" << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        std::cout << "=== CSV to ESP32 Binary Dataset Converter ===" << std::endl;
        std::cout << std::endl;
        
        // Parse command line arguments
        if (argc != 4) {
            printUsage(argv[0]);
            return 1;
        }
        
        std::string inputCSV = argv[1];
        std::string outputBinary = argv[2];
        int numFeatures = std::stoi(argv[3]);
        
        if (numFeatures <= 0 || numFeatures > 10000) {
            std::cout << "❌ Invalid number of features: " << numFeatures << std::endl;
            return 1;
        }
        
        std::cout << "🔧 Configuration:" << std::endl;
        std::cout << "   Input CSV: " << inputCSV << std::endl;
        std::cout << "   Output binary: " << outputBinary << std::endl;
        std::cout << "   Features per sample: " << numFeatures << std::endl;
        std::cout << "   Quantization: " << (int)QUANTIZATION_COEFFICIENT << " bits per feature" << std::endl;
        std::cout << "   Valid range: 0-" << (int)MAX_FEATURE_VALUE << std::endl;
        std::cout << std::endl;
        
        // Step 1: Load and validate CSV data
        auto samples = loadCSVData(inputCSV, static_cast<uint8_t>(numFeatures));
        
        if (samples.empty()) {
            std::cout << "❌ No valid samples found in CSV file" << std::endl;
            return 1;
        }
        
        // Step 2: Convert to binary format
        saveBinaryDataset(samples, outputBinary, static_cast<uint16_t>(numFeatures));
        
        // Step 3: Verify the result
        bool isValid = verifyBinaryFormat(outputBinary);
        
        // Summary
        std::cout << "\n=== Conversion Summary ===" << std::endl;
        std::cout << "✅ Conversion completed" << std::endl;
        std::cout << "📊 Results:" << std::endl;
        std::cout << "   Samples converted: " << samples.size() << std::endl;
        std::cout << "   ESP32 compatibility: " << (isValid ? "✅ PASS" : "❌ FAIL") << std::endl;
        std::cout << "   Binary format: ESP32 Rf_data compatible" << std::endl;
        std::cout << "   Ready for ESP32 transfer: " << (isValid ? "Yes" : "No") << std::endl;
        
        return isValid ? 0 : 1;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
}
