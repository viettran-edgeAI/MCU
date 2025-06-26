// csv_categorizer.cpp with Z-score outlier handling

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include "STL_MCU.h"

using u8 = uint8_t;

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

Rf_categorizer categorizeCSVFeatures(const char* inputFilePath, const char* outputFilePath, int groupsPerFeature){
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
    
    // Write output CSV
    std::ofstream fout(outputFilePath);
    if (!fout) {
        throw std::runtime_error(std::string("Cannot open output file: ") + outputFilePath);
    }
    
    fout << header << "\n";
    for (int i = 0; i < n_samples; ++i) {
        fout << labels[i];
        for (int j = 0; j < n_feats; ++j) {
            fout << "," << static_cast<int>(encoded[i][j]);
        }
        fout << "\n";
    }
    fout.close();
    
    return ctg;
}


int main() {
    try {
        Rf_categorizer test_ctg = categorizeCSVFeatures("full_dataset_truncated.csv", "walker_fall_standard.csv", 4);
        std::cout << "Categorization completed successfully.\n";
        
        // Save categorizer for ESP32 transfer
        test_ctg.saveToCSV("categorizer_esp32.csv");
        std::cout << "Categorizer saved to categorizer_esp32.csv for ESP32 transfer.\n";
        
        // mcu::vector<float> test_sample = mcu::MAKE_FLOAT_LIST(17.99,10.38,122.8,1001,0.1184,0.2776,0.3001,0.1471,0.2419,0.07871,1.095,0.9053,8.589,153.4,0.006399,0.04904,0.05373,0.01587,0.03003,0.006193,25.38,17.33,184.6,2019,0.1622,0.6656,0.7119,0.2654,0.4601,0.1189);
        // mcu::vector<uint8_t> result = test_ctg.categorizeSample(test_sample);
        
        // std::cout << "Categorized sample: ";
        // for (uint8_t cat : result) {
        //     std::cout << static_cast<int>(cat) << " ";
        // }
        std::cout << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}