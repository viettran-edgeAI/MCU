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
#include <cstdint>
#include <cctype>

using u8 = uint8_t;

static uint8_t quantization_coefficient = 2; // Coefficient for quantization (bits per feature value)

static const int MAX_LABELS = 256; // Maximum number of unique labels supporte 
static const int MAX_FEATURES = 1023;

// Helper functions to calculate derived values from quantization coefficient
static uint16_t getGroupsPerFeature() {
    if (quantization_coefficient >= 8) {
        return 256u;
    }
    return static_cast<uint16_t>(1u << quantization_coefficient); // 2^quantization_coefficient
}

static uint8_t getMaxFeatureValue() {
    if (quantization_coefficient >= 8) {
        return 255u;
    }
    return static_cast<uint8_t>((1u << quantization_coefficient) - 1u); // 2^quantization_coefficient - 1
}

static uint16_t getFeatureMask() {
    if (quantization_coefficient >= 8) {
        return 0xFFu;
    }
    return static_cast<uint16_t>((1u << quantization_coefficient) - 1u); // Bit mask for extracting feature values
}

static uint8_t getFeaturesPerByte() {
    return quantization_coefficient == 0 ? 0 : static_cast<uint8_t>(8 / quantization_coefficient); // How many whole features fit in one byte
}

static uint16_t getPackedFeatureBytes(uint16_t featureCount) {
    uint32_t totalBits = static_cast<uint32_t>(featureCount) * quantization_coefficient;
    return static_cast<uint16_t>((totalBits + 7u) / 8u);
}

static int num_features = 1023;
static int label_column_index = 0; // Column index containing the label (0 = first column)

struct QuantizationConfig {
    std::string inputPath;
    std::string modelName;
    std::string headerMode = "auto"; // auto|yes|no
    int maxFeatures = MAX_FEATURES;
    int quantBits = quantization_coefficient;
    int labelColumn = 0;
    bool runVisualization = true;
    bool removeOutliers = true;
    int32_t maxSamples = -1; // -1 = current size (default), 0 = unlimited, >0 = specific limit
};

static std::string trimWhitespace(const std::string& in) {
    size_t start = in.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = in.find_last_not_of(" \t\r\n");
    return in.substr(start, end - start + 1);
}

static std::string toLowerCopy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

// Tiny JSON extractor supporting both flat and nested {"value": ...} format
static bool extractValue(const std::string& content, const std::string& key, std::string& out) {
    const std::string pattern = "\"" + key + "\"";
    size_t keyPos = content.find(pattern);
    if (keyPos == std::string::npos) return false;

    size_t colon = content.find(':', keyPos + pattern.size());
    if (colon == std::string::npos) return false;

    size_t valueStart = content.find_first_not_of(" \t\r\n", colon + 1);
    if (valueStart == std::string::npos) return false;

    // Check if value is a nested object
    if (content[valueStart] == '{') {
        // Find the nested "value" field
        size_t objEnd = content.find('}', valueStart);
        if (objEnd == std::string::npos) return false;
        std::string objContent = content.substr(valueStart, objEnd - valueStart + 1);
        
        size_t valueKeyPos = objContent.find("\"value\"");
        if (valueKeyPos == std::string::npos) return false;
        
        size_t valueColon = objContent.find(':', valueKeyPos + 7);
        if (valueColon == std::string::npos) return false;
        
        size_t nestedStart = objContent.find_first_not_of(" \t\r\n", valueColon + 1);
        if (nestedStart == std::string::npos) return false;
        
        if (objContent[nestedStart] == '"') {
            size_t end = objContent.find('"', nestedStart + 1);
            if (end == std::string::npos) return false;
            out = objContent.substr(nestedStart + 1, end - nestedStart - 1);
        } else {
            size_t end = objContent.find_first_of(",}\n\r", nestedStart);
            out = objContent.substr(nestedStart, end - nestedStart);
            out = trimWhitespace(out);
        }
        return true;
    }
    
    // Flat format (backward compatibility)
    if (content[valueStart] == '"') {
        size_t end = content.find('"', valueStart + 1);
        if (end == std::string::npos) return false;
        out = content.substr(valueStart + 1, end - valueStart - 1);
    } else {
        size_t end = content.find_first_of(",}\n\r", valueStart);
        out = content.substr(valueStart, end - valueStart);
        out = trimWhitespace(out);
    }
    return true;
}

static QuantizationConfig loadQuantizationConfig(const std::string& configPath) {
    QuantizationConfig cfg;
    std::ifstream fin(configPath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open config file: ") + configPath);
    }

    std::ostringstream ss;
    ss << fin.rdbuf();
    std::string content = ss.str();

    std::string raw;
    if (extractValue(content, "input_path", raw)) {
        cfg.inputPath = trimWhitespace(raw);
    }
    if (extractValue(content, "model_name", raw)) {
        cfg.modelName = trimWhitespace(raw);
    }
    if (extractValue(content, "header_mode", raw)) {
        cfg.headerMode = toLowerCopy(trimWhitespace(raw));
    }
    if (extractValue(content, "max_features", raw)) {
        cfg.maxFeatures = std::stoi(raw);
    }
    if (extractValue(content, "quantization_bits", raw)) {
        cfg.quantBits = std::stoi(raw);
    }
    if (extractValue(content, "label_column", raw)) {
        cfg.labelColumn = std::stoi(raw);
    }
    if (extractValue(content, "run_visualization", raw)) {
        std::string lowered = toLowerCopy(raw);
        cfg.runVisualization = (lowered == "true" || lowered == "1" || lowered == "yes");
    }
    if (extractValue(content, "remove_outliers", raw)) {
        std::string lowered = toLowerCopy(raw);
        cfg.removeOutliers = (lowered == "true" || lowered == "1" || lowered == "yes");
    }
    if (extractValue(content, "max_samples", raw)) {
        cfg.maxSamples = std::stoi(raw);
    }

    // Validation and defaults fallback
    if (cfg.inputPath.empty()) {
        throw std::runtime_error("Config missing required field: input_path");
    }
    if (cfg.maxFeatures < 1 || cfg.maxFeatures > MAX_FEATURES) {
        cfg.maxFeatures = MAX_FEATURES;
    }
    if (cfg.quantBits < 1 || cfg.quantBits > 8) {
        cfg.quantBits = quantization_coefficient;
    }
    if (cfg.labelColumn < 0) {
        cfg.labelColumn = 0;
    }

    return cfg;
}

// Split a line on commas (naïve; assumes no embedded commas/quotes)
static mcu::vector<std::string> split(const std::string &line) {
    mcu::vector<std::string> out;
    std::istringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        // Trim leading/trailing whitespace from cell
        size_t first = cell.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) {
            out.push_back("");
            continue;
        }
        size_t last = cell.find_last_not_of(" \t\r\n");
        out.push_back(cell.substr(first, (last - first + 1)));
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

// Structure for building QTZ3 format with pattern sharing and optimization
class Rf_quantizer{
    uint16_t numFeatures = 0;
    uint16_t groupsPerFeature = 0;
    
    // Feature type definitions
    enum FeatureType { FT_DF = 0, FT_DC = 1, FT_CS = 2, FT_CU = 3 };
    
    // Structures for building the quantizer
    struct SharedPattern {
        mcu::vector<uint16_t> scaledEdges;
        uint16_t patternId;
        
        SharedPattern() : patternId(0) {} // Default constructor

        SharedPattern(const mcu::vector<uint16_t>& edges, uint16_t id)
            : scaledEdges(edges), patternId(id) {}
        
        std::string getKey() const {
            std::string key;
            for (size_t i = 0; i < scaledEdges.size(); ++i) {
                if (i > 0) key += ":";
                key += std::to_string(scaledEdges[i]);
            }
            return key;
        }
    };
    
    struct FeatureInfo {
        FeatureType type;
        mcu::vector<uint8_t> discreteValues;  // For FT_DC
        mcu::vector<uint16_t> uniqueEdges;    // For FT_CU 
        int64_t baselineScaled;               // Baseline offset (scaled)
        uint64_t scaleFactor;                 // Per-feature scaling factor
        uint16_t patternId;                   // For FT_CS
        
        FeatureInfo() : type(FT_DF), baselineScaled(0), scaleFactor(1), patternId(0) {}
    };
    
    mcu::vector<FeatureInfo> features;
    mcu::vector<SharedPattern> sharedPatterns;
    mcu::unordered_map_s<std::string, uint16_t> patternMap; // key -> patternId
    mcu::vector<mcu::pair<std::string, uint8_t>> labelMapping;
    bool removeOutliers = true; // Whether outlier filtering was applied during quantization
    mcu::vector<float> featureMeans;
    mcu::vector<float> featureStdDevs;

    static int64_t scaleFloatToInt64(double value, uint64_t scale) {
        long double scaled = static_cast<long double>(value) * static_cast<long double>(scale);
        if (scaled >= static_cast<long double>(std::numeric_limits<int64_t>::max())) {
            return std::numeric_limits<int64_t>::max();
        }
        if (scaled <= static_cast<long double>(std::numeric_limits<int64_t>::min())) {
            return std::numeric_limits<int64_t>::min();
        }
        if (scaled >= 0.0L) {
            scaled += 0.5L;
        } else {
            scaled -= 0.5L;
        }
        return static_cast<int64_t>(scaled);
    }

public:
    // Default constructor
    Rf_quantizer() = default;
    
    // Constructor for building quantizer
    Rf_quantizer(uint16_t featureCount, uint16_t gpf)
        : numFeatures(featureCount), groupsPerFeature(gpf) {
        features.resize(numFeatures);
    }
    
    // Constructor with label mapping
    Rf_quantizer(uint16_t featureCount, uint16_t gpf, const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMap, bool enableOutlierRemoval = true)
        : numFeatures(featureCount), groupsPerFeature(gpf), labelMapping(labelMap), removeOutliers(enableOutlierRemoval) {
        features.resize(numFeatures);
    }

    // Set outlier statistics for Z-score clipping
    void setOutlierStatistics(const mcu::vector<float>& means, const mcu::vector<float>& stdDevs) {
        featureMeans = means;
        featureStdDevs = stdDevs;
    }
    
    // Set feature as discrete full range (0..groupsPerFeature-1)
    void setDiscreteFullFeature(uint16_t featureIdx) {
        if (featureIdx < numFeatures) {
            FeatureInfo& info = features[featureIdx];
            info.type = FT_DF;
            info.baselineScaled = 0;
            info.scaleFactor = 1;
            info.discreteValues.clear();
            info.uniqueEdges.clear();
        }
    }
    
    // Set feature as discrete with custom values
    void setDiscreteCustomFeature(uint16_t featureIdx, const mcu::vector<float>& values) {
        if (featureIdx < numFeatures) {
            FeatureInfo& info = features[featureIdx];
            info.type = FT_DC;
            info.baselineScaled = 0;
            info.scaleFactor = 1;
            info.uniqueEdges.clear();
            info.discreteValues.clear();
            info.discreteValues.reserve(values.size());
            for (float val : values) {
                info.discreteValues.push_back(static_cast<uint8_t>(val));
            }
        }
    }
    
    // Set feature as continuous with quantile edges
    void setContinuousFeature(uint16_t featureIdx, const mcu::vector<float>& edges, float minValue, float maxValue) {
        if (featureIdx >= numFeatures) return;

        FeatureInfo& info = features[featureIdx];
        info.discreteValues.clear();

        const double baselineValue = static_cast<double>(minValue);
        double range = static_cast<double>(maxValue) - baselineValue;
        if (!std::isfinite(range) || range < 0.0) {
            range = 0.0;
        }

        long double rawScale = 1.0L;
        if (range > 0.0) {
            rawScale = static_cast<long double>(std::numeric_limits<uint16_t>::max()) / static_cast<long double>(range);
        }
        if (rawScale < 1.0L) {
            rawScale = 1.0L;
        }
        if (rawScale > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
            rawScale = static_cast<long double>(std::numeric_limits<uint64_t>::max());
        }
        uint64_t scaleValue = static_cast<uint64_t>(rawScale);
        if (scaleValue == 0) {
            scaleValue = 1;
        }

        info.scaleFactor = scaleValue;
        info.baselineScaled = scaleFloatToInt64(baselineValue, scaleValue);

        // Create scaled edge representation relative to baseline
        mcu::vector<uint16_t> scaledEdges;
        scaledEdges.reserve(edges.size());
        for (float edge : edges) {
            double diff = static_cast<double>(edge) - baselineValue;
            if (diff < 0.0) {
                diff = 0.0;
            }
            long double scaledEdge = static_cast<long double>(diff) * static_cast<long double>(scaleValue);
            if (scaledEdge >= static_cast<long double>(std::numeric_limits<uint16_t>::max())) {
                scaledEdge = static_cast<long double>(std::numeric_limits<uint16_t>::max());
            }
            if (scaledEdge < 0.0L) {
                scaledEdge = 0.0L;
            }
            scaledEdges.push_back(static_cast<uint16_t>(scaledEdge + 0.5L));
        }

        // Create pattern key
        std::string key;
        for (size_t i = 0; i < scaledEdges.size(); ++i) {
            if (i > 0) key += ":";
            key += std::to_string(scaledEdges[i]);
        }

        // Check if pattern already exists
        auto it = patternMap.find(key);
        if (it != patternMap.end()) {
            info.type = FT_CS;
            info.patternId = it->second;
            info.uniqueEdges.clear();
        } else {
            if (sharedPatterns.size() < 60) {
                uint16_t patternId = static_cast<uint16_t>(sharedPatterns.size());
                sharedPatterns.push_back(SharedPattern(scaledEdges, patternId));
                patternMap[key] = patternId;

                info.type = FT_CS;
                info.patternId = patternId;
                info.uniqueEdges.clear();
            } else {
                info.type = FT_CU;
                info.patternId = 0;
                info.uniqueEdges = scaledEdges;
            }
        }
    }
    
    // Set label mapping
    void setLabelMapping(const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMap) {
        labelMapping = labelMap;
    }
    
    // Quantize a single feature value
    uint8_t quantizeFeature(uint16_t featureIdx, float value) const {
        if (featureIdx >= numFeatures) return 0;
        
        const FeatureInfo& info = features[featureIdx];
        switch (info.type) {
            case FT_DF:
                // Full discrete range: assume value is already quantized 0..groupsPerFeature-1
                {
                    int intValue = static_cast<int>(value);
                    if (intValue < 0) {
                        intValue = 0;
                    } else if (intValue >= static_cast<int>(groupsPerFeature)) {
                        intValue = static_cast<int>(groupsPerFeature - 1);
                    }
                    return static_cast<uint8_t>(intValue);
                }
                
            case FT_DC:
                // Discrete custom values
                for (uint8_t i = 0; i < info.discreteValues.size(); ++i) {
                    if (static_cast<uint8_t>(value) == info.discreteValues[i]) {
                        return i;
                    }
                }
                return 0;
                
            case FT_CS:
                // Continuous shared pattern
                if (info.patternId < sharedPatterns.size()) {
                    int64_t scaledValue = scaleFloatToInt64(static_cast<double>(value), info.scaleFactor);
                    int64_t adjusted = scaledValue - info.baselineScaled;
                    if (adjusted <= 0) {
                        return 0;
                    }
                    const auto& pattern = sharedPatterns[info.patternId];
                    const uint32_t limited = static_cast<uint32_t>(std::min<int64_t>(adjusted, std::numeric_limits<uint32_t>::max()));
                    const uint8_t limit = static_cast<uint8_t>(pattern.scaledEdges.size());
                    for (uint8_t bin = 0; bin < limit; ++bin) {
                        if (limited < pattern.scaledEdges[bin]) {
                            return bin;
                        }
                    }
                    return limit;
                }
                return 0;
                
            case FT_CU:
                // Continuous unique edges
                {
                    int64_t scaledValue = scaleFloatToInt64(static_cast<double>(value), info.scaleFactor);
                    int64_t adjusted = scaledValue - info.baselineScaled;
                    if (adjusted <= 0) {
                        return 0;
                    }
                    const uint32_t limited = static_cast<uint32_t>(std::min<int64_t>(adjusted, std::numeric_limits<uint32_t>::max()));
                    const uint8_t edgeCount = static_cast<uint8_t>(info.uniqueEdges.size());
                    for (uint8_t bin = 0; bin < edgeCount; ++bin) {
                        if (limited < info.uniqueEdges[bin]) {
                            return bin;
                        }
                    }
                    return edgeCount;
                }
        }
        return 0;
    }
    
    // Quantize an entire sample
    mcu::vector<uint8_t> quantizeSample(const mcu::vector<float>& sample) const {
        mcu::vector<uint8_t> result;
        result.reserve(numFeatures);
        
        for (uint16_t i = 0; i < numFeatures && i < sample.size(); ++i) {
            result.push_back(quantizeFeature(i, sample[i]));
        }
        
        return result;
    }
    
    // Save quantizer to binary format for ESP32 transfer
    void saveQuantizer(const char* filename) const {
        std::ofstream fout(filename, std::ios::binary);
        if (!fout) {
            throw std::runtime_error(std::string("Cannot open quantizer binary file: ") + filename);
        }

        // Write header: magic number "QTZ3" + version
        const char magic[4] = {'Q', 'T', 'Z', '3'};
        fout.write(magic, 4);

        // Write basic parameters
        fout.write(reinterpret_cast<const char*>(&numFeatures), sizeof(uint16_t));
        fout.write(reinterpret_cast<const char*>(&groupsPerFeature), sizeof(uint16_t));
        
        uint8_t numLabelsU8 = static_cast<uint8_t>(labelMapping.size());
        fout.write(reinterpret_cast<const char*>(&numLabelsU8), sizeof(uint8_t));
        
        uint16_t numSharedPatternsU16 = static_cast<uint16_t>(sharedPatterns.size());
        fout.write(reinterpret_cast<const char*>(&numSharedPatternsU16), sizeof(uint16_t));
        
        // Write outlier filtering flag
        uint8_t outlierFlag = removeOutliers ? 1 : 0;
        fout.write(reinterpret_cast<const char*>(&outlierFlag), sizeof(uint8_t));

        // Write outlier statistics if enabled
        if (removeOutliers) {
            for (uint16_t i = 0; i < numFeatures; ++i) {
                float mean = (i < featureMeans.size()) ? featureMeans[i] : 0.0f;
                float stdDev = (i < featureStdDevs.size()) ? featureStdDevs[i] : 0.0f;
                fout.write(reinterpret_cast<const char*>(&mean), sizeof(float));
                fout.write(reinterpret_cast<const char*>(&stdDev), sizeof(float));
            }
        }

        // Write label mappings
        for (const auto& mapping : labelMapping) {
            fout.write(reinterpret_cast<const char*>(&mapping.second), sizeof(uint8_t));
            uint8_t labelLen = static_cast<uint8_t>(mapping.first.length());
            fout.write(reinterpret_cast<const char*>(&labelLen), sizeof(uint8_t));
            fout.write(mapping.first.c_str(), labelLen);
        }

        // Write shared patterns
        for (const auto& pattern : sharedPatterns) {
            uint16_t patternId = pattern.patternId;
            fout.write(reinterpret_cast<const char*>(&patternId), sizeof(uint16_t));
            
            uint16_t edgeCount = static_cast<uint16_t>(pattern.scaledEdges.size());
            fout.write(reinterpret_cast<const char*>(&edgeCount), sizeof(uint16_t));
            
            for (uint16_t edge : pattern.scaledEdges) {
                fout.write(reinterpret_cast<const char*>(&edge), sizeof(uint16_t));
            }
        }

        // Write feature definitions
        for (uint16_t i = 0; i < numFeatures; ++i) {
            const FeatureInfo& info = features[i];
            
            uint8_t typeU8 = static_cast<uint8_t>(info.type);
            fout.write(reinterpret_cast<const char*>(&typeU8), sizeof(uint8_t));
            
            fout.write(reinterpret_cast<const char*>(&info.baselineScaled), sizeof(int64_t));
            fout.write(reinterpret_cast<const char*>(&info.scaleFactor), sizeof(uint64_t));
            
            switch (info.type) {
                case FT_DF:
                    // No additional data
                    break;
                    
                case FT_DC:
                    {
                        uint8_t count = static_cast<uint8_t>(info.discreteValues.size());
                        fout.write(reinterpret_cast<const char*>(&count), sizeof(uint8_t));
                        for (uint8_t val : info.discreteValues) {
                            fout.write(reinterpret_cast<const char*>(&val), sizeof(uint8_t));
                        }
                    }
                    break;
                    
                case FT_CS:
                    fout.write(reinterpret_cast<const char*>(&info.patternId), sizeof(uint16_t));
                    break;
                    
                case FT_CU:
                    {
                        uint8_t edgeCount = static_cast<uint8_t>(info.uniqueEdges.size());
                        fout.write(reinterpret_cast<const char*>(&edgeCount), sizeof(uint8_t));
                        for (uint16_t edge : info.uniqueEdges) {
                            fout.write(reinterpret_cast<const char*>(&edge), sizeof(uint16_t));
                        }
                    }
                    break;
            }
        }

        fout.close();
    }
    
    // Accessors
    uint16_t getNumFeatures() const { return numFeatures; }
    uint16_t getGroupsPerFeature() const { return groupsPerFeature; }
    const mcu::vector<mcu::pair<std::string, uint8_t>>& getLabelMapping() const { return labelMapping; }
    
    // Estimate memory usage for QTZ3 format
    size_t estimateQTZ3MemoryUsage() const {
        size_t usage = 0;
        
        // Basic header data
        usage += sizeof(numFeatures) + sizeof(groupsPerFeature);
        
        // Feature references (packed into uint16_t each)
        usage += numFeatures * sizeof(uint16_t);

        // Baseline offsets per feature
        usage += numFeatures * sizeof(int64_t);

        // Scale factors per feature
        usage += numFeatures * sizeof(uint64_t);

        // Outlier statistics
        if (removeOutliers) {
            usage += numFeatures * sizeof(float) * 2;
        }
        
        // Shared patterns
        for (const auto& pattern : sharedPatterns) {
            usage += pattern.scaledEdges.size() * sizeof(uint16_t);
        }
        
        // Unique edges (estimated)
        size_t uniqueEdgeCount = 0;
        for (const auto& info : features) {
            if (info.type == FT_CU) {
                uniqueEdgeCount += info.uniqueEdges.size();
            }
        }
        usage += uniqueEdgeCount * sizeof(uint16_t);
        
        // Discrete custom values (estimated)
        size_t discreteValueCount = 0;
        for (const auto& info : features) {
            if (info.type == FT_DC) {
                discreteValueCount += info.discreteValues.size();
            }
        }
        usage += discreteValueCount * sizeof(uint8_t);
        
        // Label mappings (optional)
        for (const auto& mapping : labelMapping) {
            usage += mapping.first.length() + 1; // +1 for null terminator
        }
        
        return usage;
    }
};

// Helper: compute quantile bin edges for a feature
mcu::vector<float> computeQuantileBinEdges(mcu::vector<float> values, int numBins) {
    mcu::vector<float> edges;
    if (values.empty() || numBins < 2) return edges;

    std::sort(values.begin(), values.end());
    
    // Generate numBins - 1 edges
    for (int b = 1; b < numBins; ++b) {
        float q_idx = b * ((float)(values.size() - 1) / (float)numBins);
        int idx = static_cast<int>(q_idx);
        
        // Basic linear interpolation
        float fraction = q_idx - idx;
        float edge_val;
        if (static_cast<size_t>(idx + 1) < values.size()) {
            edge_val = values[idx] + fraction * (values[idx+1] - values[idx]);
        } else {
            edge_val = values.back();
        }
        edges.push_back(edge_val);
    }

    // Post-check for constant edges due to low variance
    bool all_same = true;
    for(size_t i = 1; i < edges.size(); ++i) {
        if (std::abs(edges[i] - edges[0]) > 1e-6f) {
            all_same = false;
            break;
        }
    }

    if (all_same && !edges.empty()) {
        float min_val = values.front();
        float max_val = values.back();
        float range = max_val - min_val;

        // If there's some range, create simple linear bins
        if (range > 1e-6f) {
            for (int b = 1; b < numBins; ++b) {
                edges[b-1] = min_val + b * (range / numBins);
            }
        }
        // If no range, all edges will be the same, which is acceptable.
        // The categorization logic will place all values in the last bin.
    }
    
    return edges;
}

// Replace both instances of std::set usage with this helper function
// Helper function to collect unique values using mcu::vector
mcu::vector<float> collectUniqueValues(const mcu::vector<mcu::vector<float>>& data,
                                      int featureIdx,
                                      int numSamples,
                                      size_t maxValues = 0) {
    mcu::vector<float> unique;
    if (maxValues > 0) {
        unique.reserve(maxValues + 1);
    }
    
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
            if (maxValues > 0 && unique.size() > maxValues) {
                return unique;
            }
        }
    }
    
    return unique;
}
// Apply Z-score outlier detection and clipping
float clipOutlier(float value, float mean, float stdDev, float minVal, float maxVal) {
    // Suppress unused parameter warnings
    (void)minVal;
    (void)maxVal;
    
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

// Helper function to check if a string is likely numeric (including scientific notation)
bool isLikelyNumeric(const std::string& str) {
    if (str.empty()) return false;
    
    // Trim whitespace
    std::string trimmed = str;
    size_t start = trimmed.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return false;
    size_t end = trimmed.find_last_not_of(" \t\r\n");
    trimmed = trimmed.substr(start, end - start + 1);
    
    if (trimmed.empty()) return false;
    
    try {
        // Try to parse as float - this handles integers, decimals, and scientific notation
        std::stof(trimmed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Function to automatically detect if CSV has header by analyzing first two rows
bool detectCSVHeader(const char* inputFilePath) {
    std::ifstream fin(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open input file for header detection: ") + inputFilePath);
    }
    
    std::string firstLine, secondLine;
    if (!std::getline(fin, firstLine) || !std::getline(fin, secondLine)) {
        fin.close();
        // If we can't read two lines, assume no header
        return false;
    }
    fin.close();
    
    if (firstLine.empty() || secondLine.empty()) {
        return false;
    }
    
    auto firstCols = split(firstLine);
    auto secondCols = split(secondLine);
    
    if (firstCols.size() != secondCols.size() || firstCols.size() < 2) {
        return false;
    }
    
    // Check if first row looks like headers (non-numeric) and second row looks like data (numeric)
    int firstRowNumeric = 0;
    int secondRowNumeric = 0;
    int totalCols = firstCols.size();
    
    // Skip the first column (label) for numeric analysis, focus on feature columns
    for (int i = 1; i < totalCols; ++i) {
        if (isLikelyNumeric(firstCols[i])) {
            firstRowNumeric++;
        }
        if (isLikelyNumeric(secondCols[i])) {
            secondRowNumeric++;
        }
    }
    
    int featureCols = totalCols - 1; // Exclude label column
    
    // Heuristic: if first row has significantly fewer numeric values than second row,
    // and second row is mostly numeric, then first row is likely a header
    float firstRowNumericRatio = static_cast<float>(firstRowNumeric) / featureCols;
    float secondRowNumericRatio = static_cast<float>(secondRowNumeric) / featureCols;
    
    // Header detection criteria:
    // 1. Second row should be mostly numeric (>= 80% of feature columns)
    // 2. First row should be significantly less numeric than second row
    bool hasHeader = (secondRowNumericRatio >= 0.8f) && 
                     (firstRowNumericRatio < 0.5f) && 
                     (secondRowNumericRatio - firstRowNumericRatio >= 0.3f);
    
    return hasHeader;
}

// Forward declaration
uint8_t getNormalizedLabel(const std::string& originalLabel, const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMapping);

Rf_quantizer quantizeCSVFeatures(const char* inputFilePath,
                                const char* outputFilePath,
                                int groupsPerFeature,
                                const mcu::vector<mcu::pair<std::string, uint8_t>>& labelMapping,
                                bool skipHeader = false,
                                bool enableOutlierClipping = true){
    if (groupsPerFeature < 1) {
        throw std::runtime_error("groupsPerFeature must be >= 1");
    }

    std::ifstream fin(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open input file: ") + inputFilePath);
    }

    // Read first line to determine structure
    std::string firstLine;
    std::getline(fin, firstLine);
    auto cols = split(firstLine);
    int n_cols = (int)cols.size();
    if (n_cols < 2) {
        fin.close();
        throw std::runtime_error("Input CSV needs at least one label + one feature");
    }
    
    // Validate label_column_index
    if (label_column_index < 0 || label_column_index >= n_cols) {
        fin.close();
        throw std::runtime_error("Label column index " + std::to_string(label_column_index) + 
                               " is out of range (0-" + std::to_string(n_cols-1) + ")");
    }
    
    int n_feats = n_cols - 1;
    
    // First pass: collect data and calculate statistics for Z-score
    mcu::vector<FeatureStats> featureStats(n_feats);
    mcu::vector<std::string> labels;
    mcu::vector<mcu::vector<float>> data;
    
    // If skipHeader=false, process the first line as data
    // If skipHeader=true, skip the first line (treat it as header)
    bool processFirstLine = !skipHeader;
    
    if (processFirstLine) {
        // Process the first line as data
        if ((int)cols.size() == n_cols) {
            labels.push_back(cols[label_column_index]);
            mcu::vector<float> feats;
            feats.reserve(n_feats);
            
            for (int j = 0; j < n_cols; ++j) {
                if (j == label_column_index) continue; // Skip label column
                try {
                    float val = std::stof(cols[j]);
                    feats.push_back(val);
                    
                    int idx = (j < label_column_index) ? j : (j - 1);
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
    }
    
    std::string line;
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        auto cells = split(line);
        if ((int)cells.size() != n_cols) {
            // Silently skip rows with incorrect column count
            continue; 
        }
        
        labels.push_back(cells[label_column_index]);
        mcu::vector<float> feats;
        feats.reserve(n_feats);
        
        for (int j = 0; j < n_cols; ++j) {
            if (j == label_column_index) continue; // Skip label column
            try {
                float val = std::stof(cells[j]);
                feats.push_back(val);
                
                int idx = (j < label_column_index) ? j : (j - 1);
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
        mcu::vector<float> distinct = collectUniqueValues(data, j, n_samples, static_cast<size_t>(groupsPerFeature));
        if (distinct.size() <= static_cast<size_t>(groupsPerFeature)) {
            featureStats[j].isDiscrete = true;
        }
    }

    // Apply Z-score outlier clipping to continuous features (optional)
    if (enableOutlierClipping) {
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
    }

    // Update feature min/max values after clipping
    for (int j = 0; j < n_feats; ++j) {
        float updatedMin = std::numeric_limits<float>::infinity();
        float updatedMax = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < n_samples; ++i) {
            float v = data[i][j];
            if (v < updatedMin) {
                updatedMin = v;
            }
            if (v > updatedMax) {
                updatedMax = v;
            }
        }
        featureStats[j].min = updatedMin;
        featureStats[j].max = updatedMax;
    }

    // Setup quantizer with QTZ3 format and outlier removal flag
    Rf_quantizer ctg(n_feats, groupsPerFeature, labelMapping, enableOutlierClipping);
    
    // Pass outlier statistics to quantizer
    if (enableOutlierClipping) {
        mcu::vector<float> means(n_feats);
        mcu::vector<float> stdDevs(n_feats);
        for (int j = 0; j < n_feats; ++j) {
            means[j] = featureStats[j].mean;
            stdDevs[j] = featureStats[j].stdDev;
        }
        ctg.setOutlierStatistics(means, stdDevs);
    }

    for (int j = 0; j < n_feats; ++j) {
    mcu::vector<float> distinct_after_clip = collectUniqueValues(data, j, n_samples, static_cast<size_t>(groupsPerFeature));
        
        if (distinct_after_clip.size() <= static_cast<size_t>(groupsPerFeature)) {
            // Check if it's a full discrete range (0..groupsPerFeature-1)
            bool isFullRange = (distinct_after_clip.size() == static_cast<size_t>(groupsPerFeature));
            if (isFullRange) {
                std::sort(distinct_after_clip.begin(), distinct_after_clip.end());
                for (size_t k = 0; k < distinct_after_clip.size(); ++k) {
                    if (static_cast<int>(distinct_after_clip[k]) != static_cast<int>(k)) {
                        isFullRange = false;
                        break;
                    }
                }
            }
            
            if (isFullRange) {
                ctg.setDiscreteFullFeature(j);
            } else {
                ctg.setDiscreteCustomFeature(j, distinct_after_clip);
            }
        } else {
            // Treat as continuous feature with quantile bins
            mcu::vector<float> values;
            values.reserve(n_samples);
            for (int i = 0; i < n_samples; ++i) {
                values.push_back(data[i][j]);
            }
            mcu::vector<float> edges = computeQuantileBinEdges(values, static_cast<int>(groupsPerFeature));
            ctg.setContinuousFeature(j, edges, featureStats[j].min, featureStats[j].max);
        }
    }
    
    // Encode into uint8_t categories
    mcu::vector<mcu::vector<u8>> encoded(n_samples, mcu::vector<u8>(n_feats));
    for (int i = 0; i < n_samples; ++i) {
        encoded[i] = ctg.quantizeSample(data[i]);
    }
    
    // Write output CSV (quantized data without headers)
    std::ofstream fout(outputFilePath);
    if (!fout) {
        throw std::runtime_error(std::string("Cannot open output file: ") + outputFilePath);
    }
    
    // Write quantized data (no headers needed for quantized output)
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
    bool needsHorizontalTruncation;
    
    DatasetInfo() : numFeatures(0), numSamples(0), 
                   needsHorizontalTruncation(false) {}
};

// Scan dataset to get info and create label mapping
DatasetInfo scanDataset(const char* inputFilePath) {
    DatasetInfo info;
    std::ifstream fin(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot open input file for scanning: ") + inputFilePath);
    }

    // Detect if CSV has header
    fin.close(); // Close and reopen for header detection
    bool hasHeader = detectCSVHeader(inputFilePath);
    
    fin.open(inputFilePath);
    if (!fin) {
        throw std::runtime_error(std::string("Cannot reopen input file for scanning: ") + inputFilePath);
    }

    // Read first line to get number of columns
    std::string firstLine;
    std::getline(fin, firstLine);
    auto cols = split(firstLine);
    int n_cols = (int)cols.size();
    if (n_cols < 2) {
        fin.close();
        throw std::runtime_error("Input CSV needs at least one label + one feature");
    }
    
    // Validate label_column_index
    if (label_column_index < 0 || label_column_index >= n_cols) {
        fin.close();
        throw std::runtime_error("Label column index " + std::to_string(label_column_index) + 
                               " is out of range (0-" + std::to_string(n_cols-1) + ")");
    }
    
    info.numFeatures = n_cols - 1; // Exclude label column
    info.needsHorizontalTruncation = (info.numFeatures > num_features);
    
    // Collect unique labels
    mcu::vector<std::string> uniqueLabels;
    std::string line;
    int lineCount = 0;
    
    // If first line is not a header, process it as data
    if (!hasHeader) {
        auto cells = split(firstLine);
        if ((int)cells.size() == n_cols) {
            lineCount++;
            std::string label = cells[label_column_index].c_str();
            uniqueLabels.push_back(label);
        }
    }
    
    while (std::getline(fin, line)) {
        if (line.empty()) continue;
        auto cells = split(line);
        if ((int)cells.size() != n_cols) continue; // Skip malformed rows
        
        lineCount++;
        std::string label = cells[label_column_index].c_str();
        
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
    
    // Silent operation - only report if there are issues
    if (info.needsHorizontalTruncation) {
        std::cout << "⚠️  Feature count (" << info.numFeatures << ") exceeds num_features (" 
                  << num_features << "). Truncating to " << num_features << " features.\n";
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

// Dataset parameter synthesis for ESP32 transfer
void generateDatasetParamsCSV(std::string path, const DatasetInfo& datasetInfo, const char* outputFile = "dataset_params.csv") {
    std::ofstream fout(outputFile);
    if (!fout) {
        throw std::runtime_error(std::string("Cannot create dataset params file: ") + outputFile);
    }
    
    // Calculate samples per label from the validation results
    mcu::vector<uint32_t> samplesPerLabel(datasetInfo.labelMapping.size(), 0);
    uint32_t actualTotalSamples = 0;
    
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
                if (labelValue >= 0 && static_cast<size_t>(labelValue) < samplesPerLabel.size()) {
                    samplesPerLabel[labelValue]++;
                    actualTotalSamples++;
                }
            } catch (...) {
                // Skip invalid labels
            }
        }
        csvFile.close();
    }
    
    // Calculate actual number of features after possible truncation
    uint16_t actualFeatures = std::min(datasetInfo.numFeatures, num_features);
    
    // Write CSV header
    fout << "parameter,value\n";
    
    // Write core parameters
    fout << "quantization_coefficient," << static_cast<int>(quantization_coefficient) << "\n";
    fout << "num_features," << actualFeatures << "\n";
    fout << "num_samples," << actualTotalSamples << "\n";
    fout << "num_labels," << datasetInfo.labelMapping.size() << "\n";
    
    // Write samples per label
    for (size_t i = 0; i < samplesPerLabel.size(); ++i) {
        fout << "samples_label_" << i << "," << samplesPerLabel[i] << "\n";
    }
    
    fout.close();
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
mcu::vector<ESP32_Sample> loadCSVForBinary(const std::string& csvFilename, uint16_t expectedFeatures) {
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
        if (fields.size() != static_cast<size_t>(expectedFeatures + 1)) {
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
            
        } catch (const std::exception& e) {
            errorCount++;
            continue;
        }
    }
    
    file.close();
    
    if (errorCount > 0) {
        std::cout << "⚠️  Warning: " << errorCount << " invalid rows skipped during loading\n";
    }
    
    return samples;
}

// Convert samples to ESP32-compatible binary format
void saveBinaryDataset(const mcu::vector<ESP32_Sample>& samples, 
                      const std::string& binaryFilename, 
                      uint16_t numFeatures) {
    std::ofstream file(binaryFilename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot create binary file: " + binaryFilename);
    }
    
    // Write ESP32-compatible header
    uint32_t numSamples = static_cast<uint32_t>(samples.size());
    uint16_t numFeatures_header = numFeatures;
    
    // Write header (exactly like ESP32 Rf_data)
    file.write(reinterpret_cast<const char*>(&numSamples), sizeof(uint32_t));
    file.write(reinterpret_cast<const char*>(&numFeatures_header), sizeof(uint16_t));
    
    // Calculate packed bytes needed for features
    uint16_t packedFeatureBytes = getPackedFeatureBytes(numFeatures);
    
    // Write samples (exactly like ESP32 Rf_data)
    for (size_t i = 0; i < samples.size(); ++i) {
        const ESP32_Sample& sample = samples[i];
        
        // Write label
        file.write(reinterpret_cast<const char*>(&sample.label), sizeof(uint8_t));
        
        // Pack and write features
        mcu::vector<uint8_t> packedBuffer(packedFeatureBytes, 0);
        
        uint16_t mask = getFeatureMask();
        size_t bitPosition = 0;
        
        // Pack features into bytes
        for (size_t f = 0; f < sample.features.size(); ++f) {
            uint16_t featureValue = static_cast<uint16_t>(sample.features[f]) & mask;
            size_t byteIndex = bitPosition / 8u;
            uint8_t bitOffset = static_cast<uint8_t>(bitPosition % 8u);
            uint16_t shifted = static_cast<uint16_t>(featureValue << bitOffset);
            
            if (byteIndex < packedBuffer.size()) {
                packedBuffer[byteIndex] |= static_cast<uint8_t>(shifted & 0xFFu);
            }
            
            if (bitOffset + quantization_coefficient > 8 && (byteIndex + 1) < packedBuffer.size()) {
                packedBuffer[byteIndex + 1] |= static_cast<uint8_t>(shifted >> 8);
            }
            
            bitPosition += quantization_coefficient;
        }
        
        file.write(reinterpret_cast<const char*>(packedBuffer.data()), packedFeatureBytes);
    }
    
    file.close();
    
    // Verify file size
    std::ifstream check(binaryFilename, std::ios::binary | std::ios::ate);
    if (check) {
        size_t fileSize = check.tellg();
        check.close();
        
        size_t expectedSize = 6 + samples.size() * (1 + packedFeatureBytes);
        
        if (fileSize != expectedSize) {
            std::cout << "❌ Binary file size mismatch: " << fileSize << " bytes (expected " << expectedSize << " bytes)\n";
        }
    }
}

// Integrated CSV to binary conversion function
void convertCSVToBinary(const std::string& inputCSV, const std::string& outputBinary, uint16_t numFeatures, int32_t maxSamples) {
    // Load CSV data
    auto samples = loadCSVForBinary(inputCSV, numFeatures);
    
    if (samples.empty()) {
        throw std::runtime_error("No valid samples found in CSV file");
    }

    // Enforce FIFO-style cap for ESP32 binary data (keep the newest samples)
    // maxSamples: -1 = keep current size (no change), 0 = unlimited (no cap), >0 = specific limit
    uint32_t effectiveLimit = 0;
    if (maxSamples == -1) {
        // Keep current dataset size (no capping)
        effectiveLimit = static_cast<uint32_t>(samples.size());
    } else if (maxSamples > 0) {
        effectiveLimit = static_cast<uint32_t>(maxSamples);
    }
    // maxSamples == 0 means unlimited, no capping applied
    
    if (maxSamples != 0 && samples.size() > effectiveLimit) {
        size_t startIndex = samples.size() - static_cast<size_t>(effectiveLimit);
        mcu::vector<ESP32_Sample> limited;
        limited.reserve(static_cast<size_t>(effectiveLimit));
        for (size_t i = startIndex; i < samples.size(); ++i) {
            limited.push_back(samples[i]);
        }
        samples = std::move(limited);
    }
    
    // Convert to binary format
    saveBinaryDataset(samples, outputBinary, numFeatures);
}

int main(int argc, char* argv[]) {
    try {
        std::string configPath = "quantization_config.json";
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-c" || arg == "--config") {
                if (i + 1 < argc) {
                    configPath = argv[++i];
                } else {
                    std::cerr << "Error: -c/--config requires a file path\n";
                    return 1;
                }
            } else if (arg == "-h" || arg == "--help") {
                std::cout << "Usage: " << argv[0] << " [-c quantization_config.json]\n";
                std::cout << "Configuration is provided via quantization_config.json with fields:\n";
                std::cout << "  input_path (string, required)\n";
                std::cout << "  model_name (string, optional)\n";
                std::cout << "  header_mode (auto|yes|no, default auto)\n";
                std::cout << "  label_column (int, default 0)\n";
                std::cout << "  max_features (1-1023, default 1023)\n";
                std::cout << "  quantization_bits (1-8, default 2)\n";
                std::cout << "  remove_outliers (bool, default true)\n";
                std::cout << "  max_samples (int, -1=current size (default), 0=unlimited, >0=limit, applies to binary FIFO)\n";
                std::cout << "  run_visualization (bool, handled by wrapper script)\n";
                return 0;
            } else {
                std::cerr << "Unknown argument: " << arg << " (use -h for help)\n";
                return 1;
            }
        }

        QuantizationConfig config = loadQuantizationConfig(configPath);

        // Apply configuration to globals used elsewhere
        quantization_coefficient = static_cast<uint8_t>(config.quantBits);
        num_features = config.maxFeatures;
        label_column_index = config.labelColumn;

        bool skipHeader = false;
        bool headerSpecified = false;
        std::string headerLower = toLowerCopy(config.headerMode);
        if (headerLower == "yes") {
            skipHeader = true;
            headerSpecified = true;
        } else if (headerLower == "no") {
            skipHeader = false;
            headerSpecified = true;
        }

        const std::string inputFile = config.inputPath;
        const std::string modelName = config.modelName;

        // Generate output file names based on inputFile or modelName
        std::string baseName;
        std::string inputDir = ".";
        
        if (!modelName.empty()) {
            // Use the provided model name
            baseName = modelName;
            
            // Extract directory from input file path for output location
            std::string inputName(inputFile);
            size_t slash = inputName.find_last_of("/\\");
            if (slash != std::string::npos) {
                inputDir = inputName.substr(0, slash);
            }
        } else {
            // Extract base name from input file
            std::string inputName(inputFile);
            size_t slash = inputName.find_last_of("/\\");
            if (slash != std::string::npos) {
                inputDir = inputName.substr(0, slash);
                inputName = inputName.substr(slash + 1);
            }
            baseName = inputName;
            size_t dot = baseName.find_last_of('.');
            if (dot != std::string::npos) {
                baseName = baseName.substr(0, dot);
            }
        }
        
        // All result files in the same directory as input
        std::string resultDir = inputDir + "/result";
        if (!std::filesystem::exists(resultDir)) {
            std::filesystem::create_directories(resultDir);
        }
        std::string quantizerFile = resultDir + "/" + baseName + "_qtz.bin";
        std::string dataParamsFile = resultDir + "/" + baseName + "_dp.csv";
        std::string normalizedFile = resultDir + "/" + baseName + "_nml.csv";
        std::string binaryFile = resultDir + "/" + baseName + "_nml.bin";

        // Step 1: Scan dataset to get info and create label mapping
        DatasetInfo datasetInfo = scanDataset(inputFile.c_str());

        // Auto-detect header if not explicitly specified by user
        if (!headerSpecified) {
            bool hasHeader = detectCSVHeader(inputFile.c_str());
            skipHeader = hasHeader; // Skip header if detected
            (void)hasHeader; // Header detection is silent for clean CLI output
        }

        // Step 2: Quantize features with the dataset
        Rf_quantizer test_ctg = quantizeCSVFeatures(inputFile.c_str(),
                               normalizedFile.c_str(),
                               getGroupsPerFeature(),
                               datasetInfo.labelMapping,
                               skipHeader,
                               config.removeOutliers);

        // Save quantizer for ESP32 transfer
        test_ctg.saveQuantizer(quantizerFile.c_str());

        // Step 3: Generate dataset parameters CSV for ESP32 transfer
        generateDatasetParamsCSV(normalizedFile, datasetInfo, dataParamsFile.c_str());

        // Step 4: Convert CSV to binary format
        convertCSVToBinary(normalizedFile, binaryFile, test_ctg.getNumFeatures(), config.maxSamples);

        // Calculate file size compression ratio
        size_t inputFileSize = 0;
        size_t outputFileSize = 0;
        std::ifstream inputFileCheck(inputFile, std::ios::binary | std::ios::ate);
        if (inputFileCheck) {
            inputFileSize = inputFileCheck.tellg();
            inputFileCheck.close();
        }
        std::ifstream outputFileCheck(binaryFile, std::ios::binary | std::ios::ate);
        if (outputFileCheck) {
            outputFileSize = outputFileCheck.tellg();
            outputFileCheck.close();
        }
        
        std::cout << "\n=== Processing Complete ===\n";
        std::cout << "✅ Dataset quantized and compressed:\n";
        std::cout << "   📊 Samples: " << datasetInfo.numSamples << " | Features: " << test_ctg.getNumFeatures() 
                  << " | Labels: " << datasetInfo.labelMapping.size() << "\n";
        std::cout << "   🗜️  Quantization: " << static_cast<int>(quantization_coefficient) << std::endl;
        
        if (inputFileSize > 0 && outputFileSize > 0) {
            float compressionRatio = static_cast<float>(inputFileSize) / static_cast<float>(outputFileSize);
            float compressionPercent = (1.0f - static_cast<float>(outputFileSize) / static_cast<float>(inputFileSize)) * 100.0f;
            std::cout << "   📉 Compression: " << std::fixed << std::setprecision(2) 
                      << compressionRatio << "x (" << compressionPercent << "% size reduction)\n";
            std::cout << "      Input: " << inputFileSize << " bytes → Output: " << outputFileSize << " bytes\n";
        }
        (void)config.runVisualization; // Wrapper script controls visualization
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}