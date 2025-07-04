#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include "STL_MCU.h"

#include <utility>
#include <cstring>
#include <cerrno>
#include <filesystem>

using u8 = uint8_t;

// Function declarations
int truncate_csv(const char *in_path, int n_cols);

// Quantization coefficient for feature values. 1->8 bits per feature value.
static constexpr uint8_t quantization_coefficient = 2; // Coefficient for quantization (bits per feature value)
static const int MAX_NUM_FEATURES = 234; // Maximum number of features supported
static const int MAX_LABELS = 31; // Maximum number of unique labels supporte (5 bits per label - fixed)
// Helper functions to calculate derived values from quantization coefficient
static constexpr uint16_t getGroupsPerFeature() {
    return quantization_coefficient >= 8 ? 256 : (1 << quantization_coefficient); // 2^quantization_coefficient
}

static constexpr uint8_t getMaxFeatureValue() {
    return quantization_coefficient >= 8 ? 255 : ((1 << quantization_coefficient) - 1); // 2^quantization_coefficient - 1
}

static constexpr uint8_t getFeaturesPerByte() {
    return 8 / quantization_coefficient; // How many features fit in one byte
}

static constexpr uint8_t getFeatureMask() {
    return (1 << quantization_coefficient) - 1; // Bit mask for extracting feature values
}

// Split a line on commas (naïve; assumes no embedded commas/quotes)
static mcu::vector<std::string> split(const std::string &line) {
    mcu::vector<std::string> out;
    std::istringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        out.push_back(cell);
    }
    return out;
}

// Structure to store feature statistics for Z-score calculation
struct FeatureStats {
    float mean = 0.0f;
    float stdDev = 0.0f;
    float min = std::numeric_limits<float>::infinity();
    float max = -std::numeric_limits<float>::infinity();
    bool isDiscrete = false;
};

// cấu trúc chuẩn hóa 1 sample bất kỳ, được sinh ra trong quá trình : categorizeCSVFeatures()
class Rf_categorizer{
    uint16_t numFeatures = 0; // số feature trong file csv
    uint8_t groupsPerFeature = 0; // số nhóm cho mỗi feature
    mcu::vector<mcu::pair<float,float>> featureRange; // min, max của mỗi feature
    mcu::vector<mcu::vector<float>> discreteValues; // store actual values for discrete features
    mcu::vector<bool> isDiscrete; // flag for each feature (discrete or continuous)
    mcu::vector<mcu::vector<float>> quantileBinEdges; // For continuous features mcu::vector<mcu::vector<float>> quantileBinEdges; // For continuous features

public:
    // default constructor
    Rf_categorizer() = default;
    
    Rf_categorizer(uint16_t numFeatures, uint8_t gpf){
        this->numFeatures = numFeatures;
        groupsPerFeature = gpf;
        featureRange.reserve(numFeatures);
        isDiscrete.reserve(numFeatures);
        discreteValues.reserve(numFeatures);
        
        quantileBinEdges.reserve(numFeatures);
        for(int i = 0; i < numFeatures; ++i){
            featureRange.push_back({std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()});
            isDiscrete.push_back(false);
            discreteValues.push_back(mcu::vector<float>());
            quantileBinEdges.push_back(mcu::vector<float>());
        }
    }
    
    // Update feature range for continuous features
    void updateFeatureRange(uint16_t featureIdx, float value) {
        if(featureIdx < numFeatures) {
            featureRange[featureIdx].first = std::min(featureRange[featureIdx].first, value);
            featureRange[featureIdx].second = std::max(featureRange[featureIdx].second, value);
        }
    }
    // Set quantile bin edges for a feature
    void setQuantileBinEdges(uint16_t featureIdx, const mcu::vector<float>& edges) {
        if(featureIdx < numFeatures) {
            quantileBinEdges[featureIdx] = edges;
        }
    }
    
    // Set discrete values for a categorical feature
    void setDiscreteFeature(uint16_t featureIdx, const mcu::vector<float>& values) {
        if(featureIdx < numFeatures) {
            isDiscrete[featureIdx] = true;
            discreteValues[featureIdx] = values;
        }
    }
    
    // Categorize a single feature value
    uint8_t categorizeFeature(uint16_t featureIdx, float value) const {
        if(featureIdx >= numFeatures) return 0;
        
        if(isDiscrete[featureIdx]) {
            // For discrete features, find the matching value index
            const auto& values = discreteValues[featureIdx];
            for(uint8_t i = 0; i < values.size(); ++i) {
                if(values[i] == value) return i;
            }
            return 0; // Default if not found
        } else {
            // // For continuous features, use equal-width bins
            // const auto& range = featureRange[featureIdx];
            // float min = range.first;
            // float max = range.second;
            // float width = (max - min) / groupsPerFeature;
            
            // if(width <= 0) return 0;
            
            // int bin = static_cast<int>(std::floor((value - min) / width));
            // if(bin < 0) bin = 0;
            // else if(bin >= groupsPerFeature) bin = groupsPerFeature - 1;
            
            // return static_cast<uint8_t>(bin);
            // Quantile binning
            const auto& edges = quantileBinEdges[featureIdx];
            for(uint8_t bin = 0; bin < edges.size(); ++bin) {
                if(value < edges[bin]) {
                    return bin;
                }
            }
            return edges.size(); // Last bin
        }
    }
    
    // Categorize an entire sample (return vector of categorized features)
    mcu::vector<uint8_t> categorizeSample(const mcu::vector<float>& sample) const {
        mcu::vector<uint8_t> result;
        result.reserve(numFeatures);
        
        for(uint16_t i = 0; i < numFeatures && i < sample.size(); ++i) {
            result.push_back(categorizeFeature(i, sample[i]));
        }
        
        return result;
    }
    
    // Save categorizer to CSV format for ESP32 transfer
    void saveToCSV(const char* filename) const {
        std::ofstream fout(filename);
        if (!fout) {
            throw std::runtime_error(std::string("Cannot open categorizer file: ") + filename);
        }
        
        // Header: numFeatures,groupsPerFeature
        fout << numFeatures << "," << (int)groupsPerFeature << "\n";
        
        // For each feature: isDiscrete,numValues/numEdges,values/edges...
        for(uint16_t i = 0; i < numFeatures; ++i) {
            fout << (isDiscrete[i] ? 1 : 0);
            
            if(isDiscrete[i]) {
                fout << "," << discreteValues[i].size();
                for(const auto& val : discreteValues[i]) {
                    fout << "," << val;
                }
            } else {
                fout << "," << quantileBinEdges[i].size();
                for(const auto& edge : quantileBinEdges[i]) {
                    fout << "," << edge;
                }
            }
            fout << "\n";
        }
        fout.close();
    }
    
    // Accessors
    uint16_t getNumFeatures() const { return numFeatures; }
    uint8_t getGroupsPerFeature() const { return groupsPerFeature; }
};

// Helper: compute quantile bin edges for a feature
mcu::vector<float> computeQuantileBinEdges(mcu::vector<float> values, int numBins) {
    mcu::vector<float> edges;
    if (values.size() < 2 || numBins < 1) return edges;
    std::sort(values.begin(), values.end());
    for (int b = 1; b < numBins; ++b) {
        float q = b * (values.size() / (float)numBins);
        int idx = static_cast<int>(q);
        if (idx >= values.size()) idx = values.size() - 1;
        edges.push_back(values[idx]);
    }
    return edges;
}

// Replace both instances of std::set usage with this helper function
// Helper function to collect unique values using mcu::vector
mcu::vector<float> collectUniqueValues(const mcu::vector<mcu::vector<float>>& data, int featureIdx, int numSamples) {
    mcu::vector<float> unique;
    
    for (int i = 0; i < numSamples; ++i) {
        float value = data[i][featureIdx];
        
        // Check if value already exists in unique vector
        bool found = false;
        for (size_t j = 0; j < unique.size(); ++j) {
            if (unique[j] == value) {
                found = true;
                break;
            }
        }
        
        // Add value if not found
        if (!found) {
            unique.push_back(value);
        }
    }
    
    return unique;
}
// Apply Z-score outlier detection and clipping
float clipOutlier(float value, float mean, float stdDev, float minVal, float maxVal) {
    // Only apply if we have enough standard deviation to avoid division by near-zero
    if (stdDev > 1e-6f) {
        float zScore = (value - mean) / stdDev;
        const float THRESHOLD = 3.0f; // Z-score threshold for outliers
        
        if (zScore > THRESHOLD) {
            return mean + THRESHOLD * stdDev; // Upper bound
        } else if (zScore < -THRESHOLD) {
            return mean - THRESHOLD * stdDev; // Lower bound
        }
    }
    return value; // No clipping needed
}

// Forward declaration
uint8_t getNormalizedLabel(const std::string& originalLabel, const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMapping);

Rf_categorizer categorizeCSVFeatures(const char* inputFilePath, const char* outputFilePath, int groupsPerFeature, const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMapping){
    if (groupsPerFeature < 1) {
        throw std::runtime_error("groupsPerFeature must be >= 1");
    }

    std::ifstream fin(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open input file: ") + inputFilePath);
    }

    // Read header
    std::string header;
    std::getline(fin, header);
    auto cols = split(header);
    int n_cols = (int)cols.size();
    if (n_cols < 2) {
        fin.close();
        throw std::runtime_error("Input CSV needs at least one label + one feature");
    }
    
    int n_feats = n_cols - 1;
    
    // First pass: collect data and calculate statistics for Z-score
    mcu::vector<FeatureStats> featureStats(n_feats);
    mcu::vector<std::string> labels;
    mcu::vector<mcu::vector<float>> data;
    
    std::string line;
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        auto cells = split(line);
        if ((int)cells.size() != n_cols) {
            // Silently skip rows with incorrect column count
            continue; 
        }
        
        labels.push_back(cells[0]);
        mcu::vector<float> feats;
        feats.reserve(n_feats);
        
        for (int j = 1; j < n_cols; ++j) {
            try {
                float val = std::stof(cells[j]);
                feats.push_back(val);
                
                int idx = j - 1;
                featureStats[idx].min = std::min(featureStats[idx].min, val);
                featureStats[idx].max = std::max(featureStats[idx].max, val);
                featureStats[idx].mean += val;
            } catch (const std::exception& e) {
                // Handle conversion error with a default value
                feats.push_back(0.0f);
            }
        }
        
        data.push_back(std::move(feats));
    }
    
    fin.close();
    
    int n_samples = data.size();
    if (n_samples == 0) {
        throw std::runtime_error("No data rows found in file");
    }
    
    // Calculate mean and standard deviation
    for (int j = 0; j < n_feats; ++j) {
        if (n_samples > 0) featureStats[j].mean /= n_samples;
    }
    
    for (int i = 0; i < n_samples; ++i) {
        for (int j = 0; j < n_feats; ++j) {
            float diff = data[i][j] - featureStats[j].mean;
            featureStats[j].stdDev += diff * diff;
        }
    }
    
    for (int j = 0; j < n_feats; ++j) {
        if (n_samples > 0) featureStats[j].stdDev = std::sqrt(featureStats[j].stdDev / n_samples);
    }
    
    // Initial check for discrete features to avoid clipping them
    for (int j = 0; j < n_feats; ++j) {
        mcu::vector<float> distinct = collectUniqueValues(data, j, n_samples);
        if (distinct.size() <= groupsPerFeature) {
            featureStats[j].isDiscrete = true;
        }
    }

    // Apply Z-score outlier clipping to continuous features
    for (int i = 0; i < n_samples; ++i) {
        for (int j = 0; j < n_feats; ++j) {
            if (!featureStats[j].isDiscrete) {
                data[i][j] = clipOutlier(
                    data[i][j], 
                    featureStats[j].mean, 
                    featureStats[j].stdDev,
                    featureStats[j].min,
                    featureStats[j].max
                );
            }
        }
    }

    // Final setup of the categorizer based on cleaned data
    Rf_categorizer ctg(n_feats, groupsPerFeature);
    for (int j = 0; j < n_feats; ++j) {
        mcu::vector<float> distinct_after_clip = collectUniqueValues(data, j, n_samples);
        
        if (distinct_after_clip.size() <= groupsPerFeature) {
            // Treat as a discrete feature
            ctg.setDiscreteFeature(j, distinct_after_clip);
        } else {
            // Treat as a continuous feature and create quantile bins
            mcu::vector<float> values;
            values.reserve(n_samples);
            for (int i = 0; i < n_samples; ++i) {
                values.push_back(data[i][j]);
            }
            mcu::vector<float> edges = computeQuantileBinEdges(values, groupsPerFeature);
            ctg.setQuantileBinEdges(j, edges);
        }
    }
    
    // Encode into uint8_t categories
    mcu::vector<mcu::vector<u8>> encoded(n_samples, mcu::vector<u8>(n_feats));
    for (int i = 0; i < n_samples; ++i) {
        encoded[i] = ctg.categorizeSample(data[i]);
    }
    
    // Write output CSV (no header, normalized labels)
    std::ofstream fout(outputFilePath);
    if (!fout) {
        throw std::runtime_error(std::string("Cannot open output file: ") + outputFilePath);
    }
    
    for (int i = 0; i < n_samples; ++i) {
        // Use normalized label from mapping
        uint8_t normalizedLabel = getNormalizedLabel(labels[i], labelMapping);
        fout << static_cast<int>(normalizedLabel);
        for (int j = 0; j < n_feats; ++j) {
            fout << "," << static_cast<int>(encoded[i][j]);
        }
        fout << "\n";
    }
    fout.close();
    
    return ctg;
}

// Dataset scanner to check features and collect labels
struct DatasetInfo {
    int numFeatures;
    int numSamples;
    mcu::vector<mcu::pair<std::string, uint8_t>> labelMapping; // original -> normalized
    bool needsTruncation;
    
    DatasetInfo() : numFeatures(0), numSamples(0), needsTruncation(false) {}
};

// Scan dataset to get info and create label mapping
DatasetInfo scanDataset(const char* inputFilePath) {
    DatasetInfo info;
    std::ifstream fin(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open input file for scanning: ") + inputFilePath);
    }

    // Read header to get number of columns
    std::string header;
    std::getline(fin, header);
    auto cols = split(header);
    int n_cols = (int)cols.size();
    if (n_cols < 2) {
        fin.close();
        throw std::runtime_error("Input CSV needs at least one label + one feature");
    }
    
    info.numFeatures = n_cols - 1; // Exclude label column
    info.needsTruncation = (info.numFeatures > MAX_NUM_FEATURES);
    
    // Collect unique labels
    mcu::vector<std::string> uniqueLabels;
    std::string line;
    int lineCount = 0;
    
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        auto cells = split(line);
        if ((int)cells.size() != n_cols) continue; // Skip malformed rows
        
        lineCount++;
        std::string label = cells[0].c_str();
        
        // Check if label already exists in uniqueLabels
        bool found = false;
        for (size_t i = 0; i < uniqueLabels.size(); ++i) {
            if (uniqueLabels[i] == label) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            uniqueLabels.push_back(label);
        }
    }
    
    fin.close();
    info.numSamples = lineCount;
    uniqueLabels.sort(); // Sort labels for consistent mapping
    
    // Create label mapping: original label -> normalized index (0, 1, 2, ...)
    info.labelMapping.reserve(uniqueLabels.size());
    for (size_t i = 0; i < uniqueLabels.size(); ++i) {
        info.labelMapping.push_back({uniqueLabels[i], static_cast<uint8_t>(i)});
    }
    
    std::cout << "Dataset scan results:\n";
    std::cout << "  📊 Samples: " << info.numSamples << "\n";
    std::cout << "  🔢 Features: " << info.numFeatures << "\n";
    std::cout << "  🏷️  Labels: " << uniqueLabels.size() << " unique\n";
    std::cout << "  📝 Label mapping:\n";
    for (const auto& mapping : info.labelMapping) {
        std::cout << "     \"" << mapping.first << "\" -> " << static_cast<int>(mapping.second) << "\n";
    }
    
    if (info.needsTruncation) {
        std::cout << "  ⚠️  Feature count (" << info.numFeatures << ") exceeds MAX_NUM_FEATURES (" 
                  << MAX_NUM_FEATURES << "). Truncation needed.\n";
    }
    
    return info;
}

// Get normalized label from mapping
uint8_t getNormalizedLabel(const std::string& originalLabel, const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMapping) {
    for (const auto& mapping : labelMapping) {
        if (mapping.first == originalLabel) {
            return mapping.second;
        }
    }
    return 0; // Default if not found (shouldn't happen after scanning)
}

// CSV truncation function to limit number of features
int truncate_csv(const char *in_path, int n_cols) {
    char out_path[256];
    const char *ext = strrchr(in_path, '.');
    size_t base_len = ext ? (ext - in_path) : strlen(in_path);
    const char suffix[] = "_truncated.csv";

    if (base_len + sizeof(suffix) >= sizeof(out_path))
        return -ENAMETOOLONG;

    /* build output filename: <base>_truncated.csv */
    memcpy(out_path, in_path, base_len);
    memcpy(out_path + base_len, suffix, sizeof(suffix));

    FILE *in  = fopen(in_path, "r");
    if (!in)  return -errno;
    FILE *out = fopen(out_path, "w");
    if (!out) { fclose(in); return -errno; }

    int c, col = 0;
    while ((c = fgetc(in)) != EOF) {
        if (c == ',') {
            if (++col < n_cols)
                fputc(',', out);
        }
        else if (c == '\n') {
            fputc('\n', out);
            col = 0;
        }
        else {
            if (col < n_cols)
                fputc(c, out);
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

// Dataset parameter synthesis for ESP32 transfer
void generateDatasetParamsCSV(std::string path, const DatasetInfo& datasetInfo, const char* outputFile = "dataset_params.csv") {
    std::ofstream fout(outputFile);
    if (!fout) {
        throw std::runtime_error(std::string("Cannot create dataset params file: ") + outputFile);
    }
    
    // Calculate samples per label from the validation results
    mcu::vector<uint32_t> samplesPerLabel(datasetInfo.labelMapping.size(), 0);
    
    // Read the generated CSV to count actual samples per label
    std::ifstream csvFile(path);
    if (csvFile) {
        std::string line;
        while (std::getline(csvFile, line)) {
            if (line.empty()) continue;
            auto cells = split(line);
            if (cells.size() < 1) continue;
            
            try {
                int labelValue = std::stoi(cells[0]);
                if (labelValue >= 0 && labelValue < samplesPerLabel.size()) {
                    samplesPerLabel[labelValue]++;
                }
            } catch (...) {
                // Skip invalid labels
            }
        }
        csvFile.close();
    }
    
    // Calculate actual number of features after possible truncation
    uint16_t actualFeatures = std::min(datasetInfo.numFeatures, MAX_NUM_FEATURES);
    
    // Write CSV header
    fout << "parameter,value\n";
    
    // Write core parameters
    fout << "quantization_coefficient," << static_cast<int>(quantization_coefficient) << "\n";
    fout << "max_feature_value," << static_cast<int>(getMaxFeatureValue()) << "\n";
    fout << "features_per_byte," << static_cast<int>(getFeaturesPerByte()) << "\n";
    fout << "num_features," << actualFeatures << "\n";
    fout << "num_samples," << datasetInfo.numSamples << "\n";
    fout << "num_labels," << datasetInfo.labelMapping.size() << "\n";
    
    // Write samples per label
    for (size_t i = 0; i < samplesPerLabel.size(); ++i) {
        fout << "samples_label_" << i << "," << samplesPerLabel[i] << "\n";
    }
    
    // Write label mappings
    for (const auto& mapping : datasetInfo.labelMapping) {
        fout << "label_mapping_" << static_cast<int>(mapping.second) << "," << mapping.first << "\n";
    }
    
    // Calculate and write compression metrics
    uint16_t packedFeatureBytes = (actualFeatures + getFeaturesPerByte() - 1) / getFeaturesPerByte();
    fout << "packed_bytes_per_sample," << packedFeatureBytes << "\n";
    fout << "compression_ratio," << std::fixed << std::setprecision(2) 
         << (float)actualFeatures / packedFeatureBytes << "\n";
    
    fout.close();
    
    std::cout << "✅ Dataset parameters saved to: " << outputFile << "\n";
    std::cout << "   📊 Parameters summary:\n";
    std::cout << "     Quantization: " << static_cast<int>(quantization_coefficient) << " bits per feature\n";
    std::cout << "     Features: " << actualFeatures << "\n";
    std::cout << "     Samples: " << datasetInfo.numSamples << "\n";
    std::cout << "     Labels: " << datasetInfo.labelMapping.size() << "\n";
    std::cout << "     Compression: " << (float)actualFeatures / packedFeatureBytes << ":1\n";
}

// Add binary conversion structures and functions before main()
// ESP32-compatible sample structure for binary conversion
struct ESP32_Sample {
    mcu::vector<uint8_t> features;
    uint8_t label;
    
    ESP32_Sample() : label(0) {}
    
    bool validate() const {
        for (uint8_t feature : features) {
            if (feature > getMaxFeatureValue()) {
                return false;
            }
        }
        return true;
    }
};

// Load CSV data for binary conversion
mcu::vector<ESP32_Sample> loadCSVForBinary(const std::string& csvFilename, uint8_t expectedFeatures) {
    std::cout << "🔄 Loading CSV data for binary conversion: " << csvFilename << std::endl;
    
    std::ifstream file(csvFilename);
    if (!file) {
        throw std::runtime_error("Cannot open CSV file: " + csvFilename);
    }
    
    mcu::vector<ESP32_Sample> samples;
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
        
        auto fields = split(line);
        
        // Validate field count (label + features)
        if (fields.size() != expectedFeatures + 1) {
            errorCount++;
            continue;
        }
        
        ESP32_Sample sample;
        sample.features.reserve(expectedFeatures);
        
        try {
            // Parse label
            int labelValue = std::stoi(fields[0]);
            if (labelValue < 0 || labelValue > 255) {
                errorCount++;
                continue;
            }
            sample.label = static_cast<uint8_t>(labelValue);
            
            // Parse features
            bool parseError = false;
            for (size_t i = 1; i < fields.size(); ++i) {
                int featureValue = std::stoi(fields[i]);
                
                if (featureValue < 0 || featureValue > getMaxFeatureValue()) {
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
            errorCount++;
            continue;
        }
    }
    
    file.close();
    
    std::cout << "✅ CSV loading completed:" << std::endl;
    std::cout << "   📊 Valid samples loaded: " << validSamples << std::endl;
    std::cout << "   📋 Lines processed: " << lineCount << std::endl;
    std::cout << "   ❌ Errors encountered: " << errorCount << std::endl;
    
    return samples;
}

// Convert samples to ESP32-compatible binary format
void saveBinaryDataset(const mcu::vector<ESP32_Sample>& samples, 
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
    uint16_t packedFeatureBytes = (numFeatures + getFeaturesPerByte() - 1) / getFeaturesPerByte();
    
    std::cout << "🗜️  Packing configuration:" << std::endl;
    std::cout << "   Features per byte: " << static_cast<int>(getFeaturesPerByte()) << std::endl;
    std::cout << "   Packed bytes per sample: " << packedFeatureBytes << std::endl;
    
    // Write samples (exactly like ESP32 Rf_data)
    for (size_t i = 0; i < samples.size(); ++i) {
        const ESP32_Sample& sample = samples[i];
        
        // Write sample ID
        uint16_t sampleID = static_cast<uint16_t>(i);
        file.write(reinterpret_cast<const char*>(&sampleID), sizeof(uint16_t));
        
        // Write label
        file.write(reinterpret_cast<const char*>(&sample.label), sizeof(uint8_t));
        
        // Pack and write features
        mcu::vector<uint8_t> packedBuffer(packedFeatureBytes, 0);
        
        // Pack features into bytes
        for (size_t f = 0; f < sample.features.size(); f++) {
            uint16_t byteIndex = f / getFeaturesPerByte();
            uint8_t bitOffset = (f % getFeaturesPerByte()) * quantization_coefficient;
            uint8_t feature_value = sample.features[f] & getFeatureMask();
            
            if (byteIndex < packedBuffer.size()) {
                packedBuffer[byteIndex] |= (feature_value << bitOffset);
            }
        }
        
        file.write(reinterpret_cast<const char*>(packedBuffer.data()), packedFeatureBytes);
    }
    
    file.close();
    
    // Verify file size
    std::ifstream check(binaryFilename, std::ios::binary | std::ios::ate);
    if (check) {
        size_t fileSize = check.tellg();
        check.close();
        
        size_t expectedSize = 6 + samples.size() * (3 + packedFeatureBytes);
        
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

// Integrated CSV to binary conversion function
void convertCSVToBinary(const std::string& inputCSV, const std::string& outputBinary, uint16_t numFeatures) {
    std::cout << "\n=== CSV to Binary Conversion ===\n";
    std::cout << "🔧 Configuration:" << std::endl;
    std::cout << "   Input CSV: " << inputCSV << std::endl;
    std::cout << "   Output binary: " << outputBinary << std::endl;
    std::cout << "   Features per sample: " << numFeatures << std::endl;
    std::cout << "   Quantization: " << static_cast<int>(quantization_coefficient) << " bits per feature" << std::endl;
    std::cout << "   Valid range: 0-" << static_cast<int>(getMaxFeatureValue()) << std::endl;
    
    // Load CSV data
    auto samples = loadCSVForBinary(inputCSV, static_cast<uint8_t>(numFeatures));
    
    if (samples.empty()) {
        throw std::runtime_error("No valid samples found in CSV file");
    }
    
    // Convert to binary format
    saveBinaryDataset(samples, outputBinary, numFeatures);
    
    std::cout << "✅ CSV to binary conversion completed successfully!" << std::endl;
}

int main() {
    try {
        const char* inputFile = "digit_data.csv";
        const char* workingFile = inputFile; // File to work with (may be truncated)

        // Generate output file names based on inputFile
        std::string inputName(inputFile);
        std::string baseName = inputName;
        size_t dot = baseName.find_last_of('.') ;
        if (dot != std::string::npos) baseName = baseName.substr(0, dot);
        std::string categorizerFile = baseName + "_ctg.csv";
        std::string dataParamsFile = baseName + "_dp.csv";
        std::string normalizedFile = baseName + "_nml.csv";
        std::string truncatedFile = baseName + "_truncated.csv";
        std::string binaryFile = baseName + "_nml.bin";  // Add binary output file

        // Step 1: Scan dataset to get info and create label mapping
        std::cout << "=== Dataset Analysis ===\n";
        DatasetInfo datasetInfo = scanDataset(inputFile);

        // Step 2: Handle feature truncation if needed
        if (datasetInfo.needsTruncation) {
            std::cout << "\n=== Feature Truncation ===\n";
            std::cout << "Truncating from " << datasetInfo.numFeatures << " to " << MAX_NUM_FEATURES << " features...\n";

            int result = truncate_csv(inputFile, MAX_NUM_FEATURES + 1); // +1 for label column
            if (result != 0) {
                throw std::runtime_error("Failed to truncate CSV file");
            }

            // Update working file to the truncated version
            workingFile = truncatedFile.c_str();
            std::cout << "✅ Truncated dataset saved as: " << workingFile << "\n";
        }

        // Step 3: Categorize features with the (possibly truncated) dataset
        std::cout << "\n=== Feature Categorization ===\n";
        Rf_categorizer test_ctg = categorizeCSVFeatures(workingFile, normalizedFile.c_str(), getGroupsPerFeature(), datasetInfo.labelMapping);
        std::cout << "Categorization completed successfully.\n";

        // Save categorizer for ESP32 transfer
        test_ctg.saveToCSV(categorizerFile.c_str());
        std::cout << "Categorizer saved to " << categorizerFile << " for ESP32 transfer.\n";

        // Step 4: CSV dataset generation completed
        std::cout << "\n=== CSV Dataset Generation Complete ===\n";
        std::cout << "✅ Normalized CSV dataset saved: " << normalizedFile << "\n";
        std::cout << "   📊 Features per sample: " << test_ctg.getNumFeatures() << "\n";
        std::cout << "   🔢 Feature values: 0-" << static_cast<int>(getMaxFeatureValue()) << " (" << static_cast<int>(quantization_coefficient) << "-bit quantization)\n";
        std::cout << "   � Ready for binary conversion using csv_to_binary tool\n";

        // Step 5: Generate dataset parameters CSV for ESP32 transfer
        std::cout << "\n=== Dataset Parameters Generation ===\n";
        generateDatasetParamsCSV(normalizedFile, datasetInfo, dataParamsFile.c_str());

        // Step 6: Convert CSV to binary format
        convertCSVToBinary(normalizedFile, binaryFile, test_ctg.getNumFeatures());

        std::cout << "\n=== Processing Complete ===\n";
        std::cout << "✅ Dataset processing completed successfully:\n";
        std::cout << "   📊 Normalized CSV: " << normalizedFile << "\n";
        std::cout << "   💾 Binary dataset: " << binaryFile << "\n";
        std::cout << "   📊 Features per sample: " << test_ctg.getNumFeatures() << " (" << static_cast<int>(quantization_coefficient)
                  << "-bit values: 0-" << static_cast<int>(getMaxFeatureValue()) << ")\n";
        std::cout << "   🏷️  Labels: " << datasetInfo.labelMapping.size() << " classes (normalized 0-"
                  << (datasetInfo.labelMapping.size() - 1) << ")\n";
        std::cout << "   📋 Categorizer: " << categorizerFile << "\n";
        std::cout << "   ⚙️  Parameters: " << dataParamsFile << "\n";
        std::cout << "\n🚀 Ready for ESP32 transfer!\n";

        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}