#pragma once

#include "STL_MCU.h"  
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <type_traits>
#ifdef _WIN32
#include <direct.h>
#define mkdir _mkdir
#endif

#include <iomanip>
#include <algorithm>

#define result_folder "trained_model/"
#define config_path   "model_config.json"

using namespace mcu;

struct Rf_sample{
    packed_vector<8> features;           // features stored in packed form, runtime bits configurable up to 8
    uint16_t label;                      // label of the sample 
};

struct QuantizationHelper {
    static uint8_t sanitizeBits(int bits) {
        if (bits < 1) return 1;
        if (bits > 8) return 8;
        return static_cast<uint8_t>(bits);
    }

    static uint16_t thresholdFromSlot(uint8_t bits, uint16_t slot) {
        const uint8_t sanitized = sanitizeBits(bits);
        const uint16_t maxValue = static_cast<uint16_t>((1u << sanitized) - 1u);
        return (slot > maxValue) ? maxValue : slot;
    }

    static uint16_t slotCount(uint8_t bits) {
        const uint8_t sanitized = sanitizeBits(bits);
        return static_cast<uint16_t>(1u << sanitized);
    }
};

using sampleID_set = ID_vector<uint32_t>; // Sample ID set type - supports large datasets
using sample_set   = vector<Rf_sample>; // set of samples


struct Tree_node{
    uint32_t layout_1; // packed: feature,label,threshold,is_leaf (fits in 32 bits)
    uint32_t layout_2; // child index (left child). right child = left + 1

    // Layout for layout_1 (32 bits): [is_leaf(1) | threshold(8) | label(8) | feature(15)]
    // Bits: 0..14 feature (15 bits)
    //       15..22 label (8 bits)
    //       23..30 threshold slot (8 bits)
    //       31     is_leaf (1 bit)
    static constexpr uint8_t FEATURE_SHIFT = 0;
    static constexpr uint32_t FEATURE_MASK = 0x7FFFu; // 15 bits

    static constexpr uint8_t LABEL_SHIFT = 15;
    static constexpr uint32_t LABEL_MASK = 0xFFu; // 8 bits

    static constexpr uint8_t THRESHOLD_SHIFT = 23;
    static constexpr uint32_t THRESHOLD_MASK = 0xFFu; // 8 bits

    static constexpr uint8_t IS_LEAF_SHIFT = 31;
    static constexpr uint32_t IS_LEAF_MASK = 0x1u; // 1 bit

    Tree_node() : layout_1(0), layout_2(0) {}

    uint32_t getFeatureID() const {
        return static_cast<uint32_t>((layout_1 >> FEATURE_SHIFT) & FEATURE_MASK);
    }
    
    uint32_t getLabel() const {
        return static_cast<uint32_t>((layout_1 >> LABEL_SHIFT) & LABEL_MASK);
    }
    
    uint16_t getThresholdSlot() const {
        return static_cast<uint16_t>((layout_1 >> THRESHOLD_SHIFT) & THRESHOLD_MASK);
    }
    
    bool getIsLeaf() const {
        return static_cast<bool>((layout_1 >> IS_LEAF_SHIFT) & IS_LEAF_MASK);
    }
    
    uint32_t getLeftChildIndex() const {
        return layout_2;
    }
    
    uint32_t getRightChildIndex() const {
        return getLeftChildIndex() + 1;
    }
    
    void setFeatureID(uint32_t featureID) {
        layout_1 &= ~(static_cast<uint32_t>(FEATURE_MASK) << FEATURE_SHIFT);
        layout_1 |= (static_cast<uint32_t>(featureID) & FEATURE_MASK) << FEATURE_SHIFT;
    }
    
    void setLabel(uint32_t label) {
        layout_1 &= ~(static_cast<uint32_t>(LABEL_MASK) << LABEL_SHIFT);
        layout_1 |= (static_cast<uint32_t>(label) & LABEL_MASK) << LABEL_SHIFT;
    }
    
    void setThresholdSlot(uint16_t slot) {
        layout_1 &= ~(static_cast<uint32_t>(THRESHOLD_MASK) << THRESHOLD_SHIFT);
        layout_1 |= (static_cast<uint32_t>(slot) & THRESHOLD_MASK) << THRESHOLD_SHIFT;
    }
    
    void setIsLeaf(bool isLeaf) {
        layout_1 &= ~(static_cast<uint32_t>(IS_LEAF_MASK) << IS_LEAF_SHIFT);
        layout_1 |= (static_cast<uint32_t>(isLeaf ? 1u : 0u) & IS_LEAF_MASK) << IS_LEAF_SHIFT;
    }
    
    void setLeftChildIndex(uint32_t index) {
        layout_2 = index;
    }
};

struct NodeToBuild {
    uint32_t nodeIndex;
    uint32_t begin;   // inclusive
    uint32_t end;     // exclusive
    uint16_t depth;
    
    NodeToBuild() : nodeIndex(0), begin(0), end(0), depth(0) {}
    NodeToBuild(uint32_t idx, uint32_t b, uint32_t e, uint16_t d) 
        : nodeIndex(idx), begin(b), end(e), depth(d) {}
};


class Rf_tree {
  public:
    vector<Tree_node> nodes;  // Vector-based tree storage
    std::string filename;

    Rf_tree() : filename("") {}
    
    Rf_tree(const std::string& fn) : filename(fn) {}

    // Count total number of nodes in the tree (including leaf nodes)
    uint32_t countNodes() const {
        return nodes.size();
    }

    size_t get_memory_usage() const {
        return nodes.size() * 8;  // 8 bytes per node (two uint32_t)
    }

    // Count leaf nodes in the tree
    uint32_t countLeafNodes() const {
        uint32_t leafCount = 0;
        for (const auto& node : nodes) {
            if (node.getIsLeaf()) {
                leafCount++;
            }
        }
        return leafCount;
    }

    // Get tree depth
    uint16_t getTreeDepth() const {
        if (nodes.empty()) return 0;
        return getTreeDepthRecursive(0);
    }

    // Save tree to disk using standard C++ file I/O
    void saveTree(std::string folder_path = "") {
        if (!filename.length() || nodes.empty()) return;
        std::string full_path = folder_path.length() ? (folder_path + "/" + filename) : filename;
        std::ofstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "❌ Failed to save tree: " << full_path << std::endl;
            return;
        }
        // Write header
        uint32_t magic = 0x54524545;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));

        // Precompute packed values (32-bit for MCU export) and determine highest used bit
        uint32_t maxPacked = 0u;
        vector<uint32_t> packedVals;
        packedVals.reserve(nodes.size());
        for (const auto& node : nodes) {
            uint32_t packed32 = 0u;
            packed32 |= static_cast<uint32_t>(node.getIsLeaf() ? 1u : 0u);
            packed32 |= (static_cast<uint32_t>(node.getThresholdSlot()) & 0xFFu) << 23; // THRESHOLD_SHIFT
            packed32 |= (static_cast<uint32_t>(node.getLabel()) & 0xFFu) << 15; // LABEL_SHIFT
            packed32 |= (static_cast<uint32_t>(node.getFeatureID()) & 0x7FFFu) << 0; // FEATURE_SHIFT
            // Left child index stored separately in layout_2 on PC version if needed, but MCU uses only 32-bit packed layout
            packedVals.push_back(packed32);
            maxPacked |= packed32;
        }

        // compute bits needed
        auto bits_required = [](uint64_t v) -> uint8_t {
            if (v == 0) return 1;
            uint8_t bits = 0;
            while (v) { bits++; v >>= 1; }
            return bits;
        };

        uint8_t bitsPerNode = bits_required(maxPacked);
        if (bitsPerNode == 0) bitsPerNode = 32;
        // For MCU compatibility only allow up to 32 bits per node. Abort if requiring more.
        if (bitsPerNode > 32) {
            std::cout << "⚠️  Node layout requires " << static_cast<int>(bitsPerNode)
                      << " bits which exceeds MCU limit (32). Aborting save for MCU export.\n";
            file.close();
            return;
        }
        uint8_t bytesPerNode = static_cast<uint8_t>((bitsPerNode + 7) / 8);

        // Write bits-per-node then number of nodes
        file.write(reinterpret_cast<const char*>(&bitsPerNode), sizeof(bitsPerNode));
        uint32_t nodeCount = nodes.size();
        file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));

        // Save all nodes: write only bytesPerNode for each packed value
        for (const auto& packed32 : packedVals) {
            file.write(reinterpret_cast<const char*>(&packed32), bytesPerNode);
        }
        
        file.close();
        // Clear from RAM
        purgeTree();
    }

    // load tree from disk into RAM
    void loadTree(const std::string& file_path) {
        std::ifstream file(file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open tree file: " << file_path << std::endl;
            return;
        }
        
        // Read and verify magic number
        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x54524545) { // "TREE" in hex
            std::cout << "❌ Invalid tree file format (bad magic number): " << file_path << std::endl;
            file.close();
            return;
        }

        // Read bits-per-node then number of nodes
        uint8_t bitsPerNode = 0;
        file.read(reinterpret_cast<char*>(&bitsPerNode), sizeof(bitsPerNode));
        if (bitsPerNode == 0) bitsPerNode = 32; // fallback for older files
        if (bitsPerNode > 32) {
            std::cerr << "⚠️  Node bits-per-node in file exceeds MCU limit (" << static_cast<int>(bitsPerNode) << ")\n";
            return;
        }
        uint8_t bytesPerNode = static_cast<uint8_t>((bitsPerNode + 7) / 8);

        uint32_t nodeCount;
        file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
        if (nodeCount == 0 || nodeCount > 4294967295U) { // 32-bit limit for large trees
            std::cout << "❌ Invalid node count in tree file: " << nodeCount << std::endl;
            file.close();
            return;
        }

        // Clear existing nodes and reserve space
        nodes.clear();
        nodes.reserve(nodeCount);

        // Load all nodes (read bytesPerNode into a 32-bit buffer and split into layout_1/layout_2)
        for (uint32_t i = 0; i < nodeCount; i++) {
            Tree_node node;
            uint32_t packed32 = 0u;
            file.read(reinterpret_cast<char*>(&packed32), bytesPerNode);
            if (file.fail()) {
                std::cout << "❌ Failed to read node " << i << " from tree file: " << file_path << std::endl;
                nodes.clear();
                file.close();
                return;
            }

            node.layout_1 = packed32;
            node.layout_2 = 0u;
            nodes.push_back(node);
        }
        
        file.close();
        
        // Update filename for future operations
        filename = file_path;
        
        // std::cout << "✅ Tree loaded successfully: " << file_path 
        //           << " (" << nodeCount << " nodes)" << std::endl;
    }

    uint32_t predictSample(const Rf_sample& sample, uint8_t quant_bits) const {
        if (nodes.empty()) return 0;
        const uint8_t sanitizedBits = QuantizationHelper::sanitizeBits(quant_bits);
        const uint16_t maxThresholdValue = static_cast<uint16_t>((1u << sanitizedBits) - 1u);
        
        uint32_t currentIndex = 0;  // Start from root
        
        while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
            // Bounds check for feature access
            if (nodes[currentIndex].getFeatureID() >= sample.features.size()) {
                return 0; // Invalid feature access
            }
            
            uint16_t featureValue = sample.features[nodes[currentIndex].getFeatureID()];
            const uint16_t slot = nodes[currentIndex].getThresholdSlot();
            const uint16_t thresholdValue = (slot > maxThresholdValue) ? maxThresholdValue : slot;

            if (featureValue <= thresholdValue) {
                // Go to left child
                currentIndex = nodes[currentIndex].getLeftChildIndex();
            } else {
                // Go to right child
                currentIndex = nodes[currentIndex].getRightChildIndex();
            }
            
            // Bounds check for child indices
            if (currentIndex >= nodes.size()) {
                return 0; // Invalid child index
            }
        }
        
        return (currentIndex < nodes.size()) ? nodes[currentIndex].getLabel() : 0;
    }

    void purgeTree() {
        nodes.clear();
        filename = ""; // Clear filename
    }

  private:
    // Recursive helper to get tree depth
    uint16_t getTreeDepthRecursive(uint32_t nodeIndex) const {
        if (nodeIndex >= nodes.size()) return 0;
        if (nodes[nodeIndex].getIsLeaf()) return 1;
        
        uint32_t leftIndex = nodes[nodeIndex].getLeftChildIndex();
        uint32_t rightIndex = nodes[nodeIndex].getRightChildIndex();
        
        uint16_t leftDepth = getTreeDepthRecursive(leftIndex);
        uint16_t rightDepth = getTreeDepthRecursive(rightIndex);
        
        return 1 + (leftDepth > rightDepth ? leftDepth : rightDepth);
    }
};

class Rf_data {
public:
    sample_set allSamples;    // vector storage for all samples
    std::string filename = "";
    uint8_t feature_bits = 2;
 
    Rf_data() {}  
    
    // Constructor with filename
    Rf_data(const std::string& fname) : filename(fname) {}

    void setFeatureBits(uint8_t bits) {
        feature_bits = QuantizationHelper::sanitizeBits(bits);
    }

    uint8_t getFeatureBits() const {
        return feature_bits;
    }

    // Load data from CSV format (used only once for initial dataset conversion)
    void loadCSVData(std::string csvFilename, uint16_t numFeatures) {
        std::ifstream file(csvFilename);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open data file for reading: " << csvFilename << std::endl;
            return;
        }

        uint8_t activeBits = QuantizationHelper::sanitizeBits(feature_bits);
        if (activeBits != feature_bits) {
            std::cout << "⚠️  Adjusting feature bit-width from " << (int)feature_bits
                      << " to sanitized value " << (int)activeBits << std::endl;
            feature_bits = activeBits;
        }
        uint16_t maxFeatureValue = (activeBits >= 8)
                                       ? static_cast<uint16_t>(255)
                                       : static_cast<uint16_t>((1u << activeBits) - 1);
        uint16_t highestObservedValue = 0;
        
        uint32_t sampleID = 0;
        uint32_t linesProcessed = 0;
        uint32_t emptyLines = 0;
        uint32_t validSamples = 0;
        uint32_t invalidSamples = 0;
        
        std::string line;
        while (std::getline(file, line)) {
            linesProcessed++;
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (line.empty()) {
                emptyLines++;
                continue;
            }

            Rf_sample s;
            s.features.set_bits_per_value(activeBits);
            s.features.clear();
            s.features.reserve(numFeatures);

            uint16_t fieldIdx = 0;
            std::stringstream ss(line);
            std::string token;
            bool valueOutOfRange = false;
            while (std::getline(ss, token, ',')) {
                // Trim token
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                
                uint16_t v = static_cast<uint16_t>(std::stoi(token));

                if (fieldIdx == 0) {
                    s.label = v;
                } else {
                    if (v > maxFeatureValue) {
                        valueOutOfRange = true;
                        std::cout << "❌ Line " << linesProcessed << ": Feature value " << v
                                  << " exceeds maximum " << (int)maxFeatureValue
                                  << " for " << (int)activeBits << " bits."
                                  << " Increase quantization bits or re-quantize dataset." << std::endl;
                        break;
                    }
                    s.features.push_back(static_cast<uint8_t>(v));
                    if (v > highestObservedValue) {
                        highestObservedValue = v;
                    }
                }

                fieldIdx++;
            }
            
            // Validate the sample
            if (fieldIdx != numFeatures + 1) {
                std::cout << "❌ Line " << linesProcessed << ": Expected " << (int)(numFeatures + 1) << " fields, got " << (int)fieldIdx << std::endl;
                invalidSamples++;
                continue;
            }
            if (s.features.size() != numFeatures) {
                std::cout << "❌ Line " << linesProcessed << ": Expected " << (int)numFeatures << " features, got " << s.features.size() << std::endl;
                invalidSamples++;
                continue;
            }
            if (valueOutOfRange) {
                std::cout << "❌ Line " << linesProcessed << ": Feature value exceeded quantization range (" << (int)maxFeatureValue << ")" << std::endl;
                invalidSamples++;
                continue;
            }
            
            s.features.fit();

            allSamples.push_back(s);
            sampleID++;
            validSamples++;
        }
        
        // std::cout << "📋 CSV Processing Results:" << std::endl;
        // std::cout << "   Lines processed: " << linesProcessed << std::endl;
        // std::cout << "   Empty lines: " << emptyLines << std::endl;
        // std::cout << "   Valid samples: " << validSamples << std::endl;
        // std::cout << "   Invalid samples: " << invalidSamples << std::endl;
        // std::cout << "   Total samples in memory: " << allSamples.size() << std::endl;
        
        file.close();
    }
};

typedef enum Rf_metric_scores : uint16_t{
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_metric_scores;


std::string criterionToString(bool use_gini) {
    return use_gini ? "gini" : "entropy";
}

// Helper functions to convert between flag enum and string representation
std::string flagsToString(uint16_t flags) {
    vector<std::string> flag_names;
    
    if (flags & ACCURACY) flag_names.push_back("ACCURACY");
    if (flags & PRECISION) flag_names.push_back("PRECISION");
    if (flags & RECALL) flag_names.push_back("RECALL");
    if (flags & F1_SCORE) flag_names.push_back("F1_SCORE");
    
    if (flag_names.empty()) return "NONE";
    
    std::string result = flag_names[0];
    for (size_t i = 1; i < flag_names.size(); i++) {
        result += " | " + flag_names[i];
    }
    return result;
}

uint16_t stringToFlags(const std::string& flag_str) {
    uint16_t flags = 0;
    
    if (flag_str.find("ACCURACY") != std::string::npos) flags |= ACCURACY;
    if (flag_str.find("PRECISION") != std::string::npos) flags |= PRECISION;
    if (flag_str.find("RECALL") != std::string::npos) flags |= RECALL;
    if (flag_str.find("F1_SCORE") != std::string::npos) flags |= F1_SCORE;
    
    // Default to ACCURACY if no valid flags found
    if (flags == 0) flags = ACCURACY;
    
    return flags;
}

struct Rf_config{
    // model parameters
    uint16_t quantization_coefficient = 2;
    uint16_t num_trees = 20;
    uint16_t num_features;  
    uint16_t num_labels;
    uint16_t k_folds; 
    uint16_t min_split; 
    uint16_t min_leaf;
    uint16_t max_depth;
    uint32_t num_samples;  // number of samples in the base data - changed to uint32_t for large datasets
    uint32_t max_samples = 0; // maximum samples allowed in dataset (0 = unlimited, used for ESP32)
    uint32_t random_seed = 42; // random seed for Rf_random class
    size_t RAM_usage = 0;
    int epochs = 20;    // number of epochs for inner training

    float train_ratio = 0.7f; // ratio of training data to total data, automatically set
    float test_ratio = 0.15f; // ratio of test data to total data, automatically set  
    float valid_ratio = 0.15f; // ratio of validation data to total data, automatically set
    float boostrap_ratio = 0.632f; // ratio of samples taken from train data to create subdata

    vector<uint16_t> min_leaf_range;       // for training
    vector<uint16_t> min_split_range;      // for training 
    vector<uint16_t> max_depth_range;      // for training
    vector<bool> overwrite{3}; // min_split, min_leaf, max_depth

    uint16_t max_feature_value = 0;
    uint8_t dataset_quantization_bits = 1;

    // MCU node layout bits (calculated after building trees)
    uint8_t threshold_bits = 0;
    uint8_t feature_bits = 0;
    uint8_t label_bits = 0;
    uint8_t child_bits = 0;

    Rf_metric_scores metric_score;
    std::string data_path;
    
    // model configurations
    float impurity_threshold = 0.01f; // threshold for impurity, default is 0.01
    std::string training_score = "oob_score";
    bool use_gini = false; // use Gini impurity for training
    bool use_bootstrap = true; // use bootstrap sampling for training
    bool enable_retrain = true; // enable retraining the model with new data
    bool extend_base_data = true; // extend the base data with new data when retraining
    bool enable_auto_config = true; // enable automatic configuration based on dataset

    float result_score = 0.0f; // result score of the model

public:
    // Constructor
    Rf_config() {};

    Rf_config (std::string init_path = config_path){
        for (size_t i = 0; i < 3; i++) {
            overwrite[i] = false; // default to not overwriting any parameters
        }

        std::ifstream config_file(init_path);
        
        if (!config_file.is_open()) {
            std::cout << "⚠️  Config file not found: " << init_path << ". Using default values." << std::endl;
            return;
        }
        
        std::string line;
        std::string content;
        
        // Read entire file content
        while (std::getline(config_file, line)) {
            content += line;
        }
        config_file.close();
        
        // Parse JSON values using simple string parsing
        // Extract num_trees
        size_t pos = content.find("\"num_trees\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                // Remove whitespace
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                num_trees = static_cast<uint16_t>(std::stoi(value));
            }
        }

        // Extract quantization_coefficient (bits per feature value)
        pos = content.find("\"quantization_coefficient\"");
        if (pos != std::string::npos) {
            pos = content.find(":", pos);
            if (pos != std::string::npos) {
                pos += 1;
                size_t end = content.find_first_of(",}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                if (!value.empty()) {
                    quantization_coefficient = static_cast<uint16_t>(std::stoi(value));
                }
            }
        }
        quantization_coefficient = QuantizationHelper::sanitizeBits(static_cast<int>(quantization_coefficient));
        
        
        // Extract criterion (gini or entropy)
        pos = content.find("\"criterion\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n\""));
                value.erase(value.find_last_not_of(" \t\r\n\"") + 1);
                use_gini = (value == "gini");
            }
        }
        
        // Extract use_bootstrap
        pos = content.find("\"use_bootstrap\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                use_bootstrap = (value == "true");
            }
        }
        
        // Extract training_score
        pos = content.find("\"training_score\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                // Find the opening quote of the value
                pos = content.find("\"", pos) + 1;
                size_t end = content.find("\"", pos);
                std::string value = content.substr(pos, end - pos);
                if (value == "oob_score" || value == "valid_score" || value == "k_fold_score") {
                    training_score = value;
                } else {
                    training_score = "oob_score"; // default
                }
            }
        }

        // extraact k_folds
        pos = content.find("\"k_folds\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                k_folds = static_cast<uint16_t>(std::stoi(value));
            }
        }

        // Extract random_seed
        pos = content.find("\"random_seed\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                random_seed = static_cast<uint32_t>(std::stoul(value));
            }
        }
        
        // Extract data_path
        pos = content.find("\"data_path\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find("\"", pos + 8); // Find opening quote after "value":
                if (pos != std::string::npos) {
                    pos++; // Move past opening quote
                    size_t end = content.find("\"", pos);
                    if (end != std::string::npos) {
                        data_path = content.substr(pos, end - pos);
                    }
                }
            }
        }

        // Extract and apply split_ratio from JSON
        json_train_ratio = 0.0f;
        json_test_ratio = 0.0f; 
        json_valid_ratio = 0.0f;
        json_ratios_found = false;
        
        pos = content.find("\"split_ratio\"");
        if (pos != std::string::npos) {
            // Find the split_ratio object boundaries
            size_t split_start = content.find("{", pos);
            size_t split_end = content.find("}", split_start);
            
            if (split_start != std::string::npos && split_end != std::string::npos) {
                std::string split_section = content.substr(split_start, split_end - split_start);
                json_ratios_found = true;
                
                // Extract train_ratio
                size_t train_pos = split_section.find("\"train_ratio\"");
                if (train_pos != std::string::npos) {
                    train_pos = split_section.find(":", train_pos) + 1;
                    size_t train_end = split_section.find(",", train_pos);
                    if (train_end == std::string::npos) train_end = split_section.find("}", train_pos);
                    std::string train_value = split_section.substr(train_pos, train_end - train_pos);
                    train_value.erase(0, train_value.find_first_not_of(" \t\r\n"));
                    train_value.erase(train_value.find_last_not_of(" \t\r\n") + 1);
                    json_train_ratio = std::stof(train_value);
                    train_ratio = json_train_ratio; // Apply to actual config
                }
                
                // Extract test_ratio
                size_t test_pos = split_section.find("\"test_ratio\"");
                if (test_pos != std::string::npos) {
                    test_pos = split_section.find(":", test_pos) + 1;
                    size_t test_end = split_section.find(",", test_pos);
                    if (test_end == std::string::npos) test_end = split_section.find("}", test_pos);
                    std::string test_value = split_section.substr(test_pos, test_end - test_pos);
                    test_value.erase(0, test_value.find_first_not_of(" \t\r\n"));
                    test_value.erase(test_value.find_last_not_of(" \t\r\n") + 1);
                    json_test_ratio = std::stof(test_value);
                    test_ratio = json_test_ratio; // Apply to actual config
                }
                
                // Extract valid_ratio
                size_t valid_pos = split_section.find("\"valid_ratio\"");
                if (valid_pos != std::string::npos) {
                    valid_pos = split_section.find(":", valid_pos) + 1;
                    size_t valid_end = split_section.find(",", valid_pos);
                    if (valid_end == std::string::npos) valid_end = split_section.find("}", valid_pos);
                    std::string valid_value = split_section.substr(valid_pos, valid_end - valid_pos);
                    valid_value.erase(0, valid_value.find_first_not_of(" \t\r\n"));
                    valid_value.erase(valid_value.find_last_not_of(" \t\r\n") + 1);
                    json_valid_ratio = std::stof(valid_value);
                    valid_ratio = json_valid_ratio; // Apply to actual config
                }
                
                // std::cout << "📊 Split ratios loaded from JSON: train=" << train_ratio 
                //           << ", test=" << test_ratio << ", valid=" << valid_ratio << std::endl;
            }
        }

        // extract extend_base_data
        pos = content.find("\"extend_base_data\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                extend_base_data = (value == "true");
            }
        }

        // extract enable_retrain
        pos = content.find("\"enable_retrain\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                enable_retrain = (value == "true");
            }
        }

        // extract enable_auto_config
        pos = content.find("\"enable_auto_config\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                enable_auto_config = (value == "true");
            }
        }

        // Helper function to check if parameter is enabled (supports all status types)
        auto isParameterEnabled = [&content](const std::string& param_name) -> bool {
            size_t pos = content.find("\"" + param_name + "\"");
            if (pos != std::string::npos) {
                size_t status_pos = content.find("\"status\":", pos);
                if (status_pos != std::string::npos && status_pos < content.find("}", pos)) {
                    size_t start = content.find("\"", status_pos + 9) + 1;
                    size_t end = content.find("\"", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string status = content.substr(start, end - start);
                        return status == "enabled" || status == "overwrite" || status == "stacked";
                    }
                }
            }
            return false;
        };

        // Helper function to check if parameter has stack mode
        auto isParameterStacked = [&content](const std::string& param_name) -> bool {
            size_t pos = content.find("\"" + param_name + "\"");
            if (pos != std::string::npos) {
                size_t status_pos = content.find("\"status\":", pos);
                if (status_pos != std::string::npos && status_pos < content.find("}", pos)) {
                    size_t start = content.find("\"", status_pos + 9) + 1;
                    size_t end = content.find("\"", start);
                    if (start != std::string::npos && end != std::string::npos) {
                        std::string status = content.substr(start, end - start);
                        return status == "stacked";
                    }
                }
            }
            return false;
        };

        // Helper function to extract parameter value
        auto extractParameterValue = [&content](const std::string& param_name) -> std::string {
            size_t pos = content.find("\"" + param_name + "\"");
            if (pos != std::string::npos) {
                size_t value_pos = content.find("\"value\":", pos);
                if (value_pos != std::string::npos && value_pos < content.find("}", pos)) {
                    size_t start = content.find(":", value_pos) + 1;
                    size_t end = content.find(",", start);
                    if (end == std::string::npos) end = content.find("\n", start);
                    if (end == std::string::npos) end = content.find("}", start);
                    std::string value = content.substr(start, end - start);
                    value.erase(0, value.find_first_not_of(" \t\r\n\""));
                    value.erase(value.find_last_not_of(" \t\r\n\"") + 1);
                    return value;
                }
            }
            return "";
        };

        // Initialize overwrite vector: min_split, min_leaf, max_depth
        overwrite.clear();
        for (int i = 0; i < 3; i++) {
            overwrite.push_back(false);
        }

        // Check and extract min_split
        overwrite[0] = isParameterEnabled("min_split");
        if (overwrite[0]) {
            std::string value = extractParameterValue("min_split");
            if (!value.empty()) {
                min_split = static_cast<uint16_t>(std::stoi(value));
                // std::cout << "⚙️  min_split override enabled: " << (int)min_split << std::endl;
            }
        }

        // Check and extract min_leaf
        overwrite[1] = isParameterEnabled("min_leaf");
        if (overwrite[1]) {
            std::string value = extractParameterValue("min_leaf");
            if (!value.empty()) {
                min_leaf = static_cast<uint16_t>(std::stoi(value));
                // std::cout << "⚙️  min_leaf override enabled: " << (int)min_leaf << std::endl;
            }
        }

        // Check and extract max_depth
        overwrite[2] = isParameterEnabled("max_depth");
        if (overwrite[2]) {
            std::string value = extractParameterValue("max_depth");
            if (!value.empty()) {
                max_depth = static_cast<uint16_t>(std::stoi(value));
                // std::cout << "⚙️  max_depth override enabled: " << (int)max_depth << std::endl;
            }
        }

        // Extract max_samples for dataset size management
        std::string max_samples_value = extractParameterValue("max_samples");
        if (!max_samples_value.empty()) {
            max_samples = static_cast<uint32_t>(std::stoul(max_samples_value));
        }

        // Check for invalid configuration cases and apply automatic ratio setting
        // This will be done after dataset analysis in init() method
        
        std::cout << "✅ Configuration loaded from " << init_path << std::endl;
        std::cout << "   Number of trees: " << (int)num_trees << std::endl;
        // std::cout << "   K-folds: " << (int)k_folds << std::endl;
        std::cout << "   Criterion: " << (use_gini ? "gini" : "entropy") << std::endl;
        std::cout << "   Use bootstrap: " << (use_bootstrap ? "true" : "false") << std::endl;
        std::cout << "   Training score method: " << training_score << std::endl;
        // std::cout << "   Data path: " << data_path << std::endl;
        // if (json_ratios_found) {
        //     std::cout << "   JSON split ratios found: train=" << json_train_ratio << ", test=" << json_test_ratio << ", valid=" << json_valid_ratio << " (will be validated)" << std::endl;
        // }
        // std::cout << "   Quantization bits: " << (int)quantization_coefficient << std::endl;
        std::cout << "   Random seed: " << random_seed << std::endl;
        if (max_samples > 0) {
            std::cout << "   Max samples limit: " << max_samples << std::endl;
        }
    }

    void init(std::string data_path){
        // For PC training, use standard file I/O instead of SPIFFS
        std::ifstream file(data_path);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open file: " << data_path << "\n";
            return;
        }

        unordered_map_s<uint16_t, uint32_t> labelCounts;
        unordered_set_s<uint16_t> featureValues;

        uint32_t numSamples = 0;
        uint16_t maxFeatures = 0;
        uint16_t datasetMaxValue = 0;

        std::string line;
        while (std::getline(file, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty()) continue;

            std::stringstream ss(line);
            std::string token;
            int featureIndex = 0;
            bool malformed = false;

            while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                
                if (token.empty()) {
                    malformed = true;
                    break;
                }

                int numValue = std::stoi(token);
                if (featureIndex == 0) {
                    labelCounts[numValue]++;
                } else {
                    featureValues.insert(numValue);
                    if (numValue > datasetMaxValue) {
                        datasetMaxValue = static_cast<uint16_t>(numValue);
                    }
                    uint16_t featurePos = featureIndex - 1;
                    if (featurePos + 1 > maxFeatures) {
                        maxFeatures = featurePos + 1;
                    }
                }
                featureIndex++;
            }

            if (!malformed) {
                numSamples++;
            }
        }

        file.close();

        num_features = maxFeatures;
        num_samples = numSamples;
        num_labels = labelCounts.size();
        max_feature_value = datasetMaxValue;

        uint8_t datasetBits = 1;
        while (datasetBits < 8 && datasetMaxValue > static_cast<uint16_t>((1u << datasetBits) - 1)) {
            ++datasetBits;
        }
        if (datasetMaxValue > static_cast<uint16_t>((1u << datasetBits) - 1)) {
            datasetBits = 8;
        }
        datasetBits = QuantizationHelper::sanitizeBits(datasetBits);
        dataset_quantization_bits = datasetBits;

        uint8_t configuredBits = QuantizationHelper::sanitizeBits(static_cast<int>(quantization_coefficient));
        if (datasetBits > configuredBits) {
            std::cout << "⚙️  Detected maximum feature value " << datasetMaxValue
                      << ", requiring " << static_cast<int>(datasetBits)
                      << " bits. Adjusting quantization from " << static_cast<int>(configuredBits)
                      << " to " << static_cast<int>(datasetBits) << std::endl;
            configuredBits = datasetBits;
        } else if (datasetBits < configuredBits) {
            std::cout << "ℹ️  Dataset values fit within " << static_cast<int>(datasetBits)
                      << " bits; using configured " << static_cast<int>(configuredBits) << " bits." << std::endl;
        }
        quantization_coefficient = configuredBits;

        // Dataset summary
        std::cout << "📊 Dataset Summary:\n";
        std::cout << "  Total samples: " << numSamples << "\n";
        std::cout << "  Total features: " << maxFeatures << "\n";
        std::cout << "  Unique labels: " << labelCounts.size() << "\n";
        std::cout << "  Active quantization bits: " << static_cast<int>(quantization_coefficient) << "\n";

        // Automatic ratio configuration based on dataset size and training method
        float samples_per_label = (float)numSamples / labelCounts.size();
        
        // Validate training_score and split_ratio consistency
        bool valid_ratios = true;
        if (json_ratios_found) {
            // Check for invalid cases
            if (training_score == "valid_score" && json_valid_ratio == 0.0f) valid_ratios = false;
            else if (training_score != "valid_score" && json_valid_ratio > 0.0f) valid_ratios = false;
        }
        if (!valid_ratios){
            std::cout << "⚠️ Invalid ratios detected. Auto adjusting.." << std::endl;
            if (training_score == "oob_score" || training_score == "k_fold_score") {
                // OOB and K-fold don't need validation data
                if (samples_per_label > 800){
                    train_ratio = 0.9f;
                    test_ratio  = 0.1f;
                    valid_ratio = 0.0f;
                }else if (samples_per_label > 150) {
                    train_ratio = 0.8f;
                    test_ratio = 0.2f;
                    valid_ratio = 0.0f;
                } else {
                    train_ratio = 0.75f;
                    test_ratio = 0.25f;
                    valid_ratio = 0.0f;
                }
            } else if (training_score == "valid_score") {
                // Validation score needs validation data
                test_ratio *= 1.5f;
                train_ratio = 1.0f - test_ratio;
                test_ratio *= 0.5f;
                valid_ratio = 1.0f - train_ratio - test_ratio;
            }

        }

        // Final validation and normalization of ratios
        float total_ratio = train_ratio + test_ratio + valid_ratio;
        if (abs(total_ratio - 1.0f) > 0.001f) {
            train_ratio /= total_ratio;
            test_ratio  /= total_ratio;
            valid_ratio /= total_ratio;
        }

        // Analyze label distribution and set appropriate training flags
        if (labelCounts.size() > 0) {
            uint32_t minorityCount = numSamples;
            uint32_t majorityCount = 0;

            for (auto& it : labelCounts) {
                uint32_t count = it.second;
                if (count > majorityCount) {
                    majorityCount = count;
                }
                if (count < minorityCount) {
                    minorityCount = count;
                }
            }

            float maxImbalanceRatio = 0.0f;
            if (minorityCount > 0) {
                maxImbalanceRatio = (float)majorityCount / minorityCount;
            }

            // Automatically set training flags based on class imbalance
            if (maxImbalanceRatio > 10.0f) {
                metric_score = Rf_metric_scores::RECALL;
                std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting metric_score to RECALL.\n";
            } else if (maxImbalanceRatio > 3.0f) {
                metric_score = Rf_metric_scores::F1_SCORE;
                std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting metric_score to F1_SCORE.\n";
            } else if (maxImbalanceRatio > 1.5f) {
                metric_score = Rf_metric_scores::PRECISION;
                std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Setting metric_score to PRECISION.\n";
            } else {
                metric_score = Rf_metric_scores::ACCURACY;
                std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Setting metric_score to ACCURACY.\n";
            }
        }
 
        std::cout << "  Label distribution:\n";
        float lowestDistribution = 100.0f;
        for (auto& label : labelCounts) {
            float percent = (float)label.second / numSamples * 100.0f;
            if (percent < lowestDistribution) {
                lowestDistribution = percent;
            }
            std::cout << "    Label " << (int)label.first << ": " << label.second << " samples (" << percent << "%)\n";
        }
        
        // Check if validation should be disabled due to low sample count (only if using valid_score)
        if(training_score == "valid_score"){
            float min_validation_samples = lowestDistribution / 100.0f * numSamples * valid_ratio;
            if (min_validation_samples < 10) {
                std::cout << "⚖️ Switching to oob_score due to low sample count in validation set (min class would have " 
                         << min_validation_samples << " samples).\n";
                training_score = "oob_score";
                // Recalculate ratios without validation
                if (samples_per_label > 800){
                    train_ratio = 0.9f;
                    test_ratio  = 0.1f;
                    valid_ratio = 0.0f;
                }else if (samples_per_label > 150) {
                    train_ratio = 0.8f;
                    test_ratio = 0.2f;
                    valid_ratio = 0.0f;
                } else {
                    train_ratio = 0.75f;
                    test_ratio = 0.25f;
                    valid_ratio = 0.0f;
                }
                std::cout << "📏 Adjusted ratios after removing validation: train=" << train_ratio 
                         << ", test=" << test_ratio << ", valid=" << valid_ratio << "\n";
            }
        }

        std::cout << "🎯 Final split ratios: train=" << train_ratio << ", test=" << test_ratio 
                  << ", valid=" << valid_ratio << std::endl;
        
        // Calculate optimal parameters based on dataset size
        int baseline_minsplit_ratio = 100 * (num_samples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint16_t min_minSplit = std::min(2, (int)(num_samples / baseline_minsplit_ratio));
        int dynamic_max_split = std::min(min_minSplit + 4, (int)(log2(num_samples) / 4 + num_features / 25.0f));
        uint16_t max_minSplit = std::min(16, dynamic_max_split); // Cap at 16 to prevent overly simple trees.
        if (max_minSplit <= min_minSplit + 4) max_minSplit = min_minSplit + 4; // Ensure a valid range.

        // Calculate min_leaf range based on dataset density and class balance
        float samples_per_label_leaf = (num_labels > 0)
                                      ? static_cast<float>(num_samples) / static_cast<float>(num_labels)
                                      : static_cast<float>(num_samples);
        float density_factor = std::clamp(samples_per_label_leaf / 600.0f, 0.3f, 3.0f);
        float expected_min_pct_leaf = (num_labels > 0) ? (100.0f / static_cast<float>(num_labels)) : 100.0f;
        float deficit_pct = std::max(0.0f, expected_min_pct_leaf - lowestDistribution);
        float imbalance_factor_leaf = 1.0f - std::min(0.5f, (expected_min_pct_leaf > 0.0f)
                                                        ? (deficit_pct / expected_min_pct_leaf)
                                                        : 0.0f); // 0.5 .. 1.0

        float min_ratio = std::clamp(0.12f + 0.05f * density_factor * imbalance_factor_leaf, 0.1f, 0.35f);
        float max_ratio = std::clamp(min_ratio + (0.12f + 0.04f * density_factor), min_ratio + 0.1f, 0.6f);

        uint16_t min_minLeaf = static_cast<uint16_t>(std::floor(static_cast<float>(min_minSplit) * min_ratio));
        uint16_t max_cap = static_cast<uint16_t>(max_minSplit > 1 ? max_minSplit - 1 : 1);
        min_minLeaf = std::max<uint16_t>(1, std::min<uint16_t>(min_minLeaf, max_cap));

        uint16_t max_minLeaf = static_cast<uint16_t>(std::ceil(static_cast<float>(max_minSplit) * max_ratio));
        max_minLeaf = std::min<uint16_t>(max_minLeaf, max_cap);
        if (max_minLeaf < min_minLeaf) {
            max_minLeaf = min_minLeaf;
        }

        int base_maxDepth = (int)(log2(num_samples) + log2(num_features)) + 1;
        uint16_t max_maxDepth = std::max(8, base_maxDepth);
        uint16_t min_maxDepth = max_maxDepth > 18 ? max_maxDepth - 6 : max_maxDepth > 12 ? max_maxDepth - 4 : max_maxDepth > 8 ? max_maxDepth - 2 : 4;

        // Set default values only if not overridden
        if (!overwrite[0]) {
            min_split = (min_minSplit + max_minSplit + 1) / 2 ;
        }
        if (!overwrite[1]) {
            uint16_t suggested_leaf = static_cast<uint16_t>((min_minLeaf + max_minLeaf + 1) / 2);
            min_leaf = std::clamp<uint16_t>(suggested_leaf, min_minLeaf, max_minLeaf);
        }
        if (!overwrite[2]) {
            max_depth = (min_maxDepth + max_maxDepth) / 2 ;
        }

        // Build ranges based on override status
        min_split_range.clear();
        min_leaf_range.clear();
        max_depth_range.clear();
        
        if (overwrite[0]) {
            // min_split override is enabled - use only the override value
            min_split_range.push_back(min_split);
            std::cout << "🔧 min_split override active: using fixed value " << (int)min_split << "\n";
        } else {
            // min_split automatic - build range with step 2
            uint16_t min_split_step;
            size_t total_object = num_samples * num_features;
            if (total_object < 50000) {
                min_split_step = 1; // Finer steps for small datasets
            } else if (total_object < 1000000) {
                min_split_step = 2;
            } else {
                min_split_step = 3; // Coarser steps for large datasets
            }
            if(overwrite[1] || max_minSplit - min_minSplit < 4) {
                min_split_step = 1; // If min_leaf is overridden, use smaller step for min_split
            }
            for(uint16_t i = min_minSplit; i <= max_minSplit; i += min_split_step) {
                min_split_range.push_back(i);
            }
        }
        
        if (overwrite[1]) {
            // min_leaf override is enabled - use only the override value
            min_leaf_range.push_back(min_leaf);
            std::cout << "🔧 min_leaf override active: using fixed value " << (int)min_leaf << "\n";
        } else {
            // min_leaf automatic - build range with step 1
            uint16_t min_leaf_step = 1;
            if(overwrite[0]) {
                min_leaf_step = 1; // If min_split is overridden, use step of 1
            }
            for(uint16_t i = min_minLeaf; i <= max_minLeaf; i += min_leaf_step) {
                min_leaf_range.push_back(i);
            }
        }
        
        // Ensure at least one value in each range (fallback safety)
        if (min_split_range.empty()) {
            min_split_range.push_back(min_split);
        }
        if (min_leaf_range.empty()) {
            min_leaf_range.push_back(min_leaf);
        }
        
        if (overwrite[2]) {
            // max_depth override is enabled - use only the override value
            max_depth_range.push_back(max_depth);
            std::cout << "🔧 max_depth override active: using fixed value " << (int)max_depth << "\n";
        } else {
            // uint16_t max_depth_step = 2;  // Step of 3 to reduce combinations
            uint16_t max_depth_step;
            size_t total_object = num_samples * num_features;
            if(total_object < 50000) {
                max_depth_step = 1; // Finer steps for small datasets
            } else if (total_object < 1000000) {
                max_depth_step = 2;
            } else {
                max_depth_step = 3; // Coarser steps for large datasets
            }
            for(uint16_t i = min_maxDepth; i <= max_maxDepth; i += max_depth_step) {
                max_depth_range.push_back(i);
            }
            // Always include the max value if not already included
            if (max_depth_range.empty() || max_depth_range.back() < max_maxDepth) {
                max_depth_range.push_back(max_maxDepth);
            }
        }
        
        // Ensure at least one value in max_depth_range
        if (max_depth_range.empty()) {
            max_depth_range.push_back(max_depth);
        }
        
        // Debug: Show range sizes
        std::cout << "📊 Training ranges: \n";
        std::cout << "   min_split values: ";
        for(size_t i = 0; i < min_split_range.size(); i++) {
            std::cout << (int)min_split_range[i];
            if (i + 1 < min_split_range.size()) std::cout << ", ";
        }
        std::cout << "\n   min_leaf values: ";
        for(size_t i = 0; i < min_leaf_range.size(); i++) {
            std::cout << (int)min_leaf_range[i];
            if (i + 1 < min_leaf_range.size()) std::cout << ", ";
        }
        std::cout << "\n   max_depth values: ";
        for(size_t i = 0; i < max_depth_range.size(); i++) {
            std::cout << (int)max_depth_range[i];
            if (i + 1 < max_depth_range.size()) std::cout << ", ";
        }
        std::cout << "\n";

        // generate impurity_threshold
        int K = std::max(2, static_cast<int>(num_labels));
        float expected_min_pct_threshold = 100.0f / static_cast<float>(K);
        float deficit_threshold = std::max(0.0f, expected_min_pct_threshold - lowestDistribution);
        float imbalance_threshold = expected_min_pct_threshold > 0.0f ? std::min(1.0f, deficit_threshold / expected_min_pct_threshold) : 0.0f; // 0..1

        // Sample factor: with more data, we can demand slightly larger impurity gains to split
        // compute using double to avoid mixed-type std::min/std::max template deduction issues, then cast
        double sample_factor_d = std::min(2.0, 0.75 + std::log2(std::max(2.0, static_cast<double>(num_samples))) / 12.0);
        float sample_factor = static_cast<float>(sample_factor_d);
        // Imbalance factor: reduce threshold for imbalanced data to allow splitting on rare classes
        float imbalance_factor_threshold = 1.0f - 0.5f * imbalance_threshold; // 0.5..1.0
        // Feature factor: with many features, weak splits are common; require slightly higher gain
        float feature_factor = 0.9f + 0.1f * std::min(1.0f, static_cast<float>(std::log2(std::max(2, static_cast<int>(num_features)))) / 8.0f);

        if (use_gini) {
            float max_gini = 1.0f - 1.0f / static_cast<float>(K);
            float base = 0.003f * max_gini; // very small base for Gini
            float thr = base * sample_factor * imbalance_factor_threshold * feature_factor;
            impurity_threshold = std::max(0.0005f, std::min(0.02f, thr));
        } else { // entropy
            float max_entropy = std::log2(static_cast<float>(K));
            float base = 0.02f * (max_entropy > 0.0f ? max_entropy : 1.0f); // larger than gini
            float thr = base * sample_factor * imbalance_factor_threshold * feature_factor;
            impurity_threshold = std::max(0.005f, std::min(0.2f, thr));
        }

    }
    
    // save config to transfer to esp32 
    void saveConfig(const std::string& path) const {
        std::ofstream config_file(path);

        std::time_t t = std::time(nullptr);
        std::tm tm_local;
        localtime_r(&t, &tm_local); // Use local time instead of UTC
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", &tm_local);

        // Ensure ratios sum to 1.0 (normalize if needed)
        float total_ratio = train_ratio + test_ratio + valid_ratio;
        float norm_train_ratio = train_ratio / total_ratio;
        float norm_test_ratio = test_ratio / total_ratio;
        float norm_valid_ratio = valid_ratio / total_ratio;

        if(config_file.is_open()) {
            config_file << "{\n";
            // data_path in esp32 SPIFFS : "/base_name."
            config_file << "  \"numTrees\": " << (int)num_trees << ",\n";
            config_file << "  \"randomSeed\": " << random_seed << ",\n";
            config_file << "  \"quantization_coefficient\": " << (int)quantization_coefficient << ",\n";
            config_file << "  \"train_ratio\": " << norm_train_ratio << ",\n";
            config_file << "  \"test_ratio\": " << norm_test_ratio << ",\n";
            config_file << "  \"valid_ratio\": " << norm_valid_ratio << ",\n";
            config_file << "  \"minSplit\": " << (int)min_split << ",\n";
            config_file << "  \"minLeaf\": " << (int)min_leaf << ",\n";
            config_file << "  \"maxDepth\": " << (int)max_depth << ",\n";
            config_file << "  \"useBootstrap\": " << (use_bootstrap ? "true" : "false") << ",\n";
            config_file << "  \"boostrapRatio\": " << boostrap_ratio << ",\n";
            config_file << "  \"criterion\": \"" << criterionToString(use_gini) << "\",\n";
            config_file << "  \"trainingScore\": \"" << training_score << "\",\n";
            config_file << "  \"k_folds\": " << (int)k_folds << ",\n";
            config_file << "  \"impurityThreshold\": " << impurity_threshold << ",\n";
            config_file << "  \"metric_score\": \"" << flagsToString(metric_score) << "\",\n";
            config_file << "  \"resultScore\": " << result_score << ",\n";
            config_file << "  \"threshold_bits\": " << (int)threshold_bits << ",\n";
            config_file << "  \"feature_bits\": " << (int)feature_bits << ",\n";
            config_file << "  \"label_bits\": " << (int)label_bits << ",\n";
            config_file << "  \"child_bits\": " << (int)child_bits << ",\n";
            config_file << "  \"extendBaseData\": " << (extend_base_data ? "true" : "false") << ",\n";
            config_file << "  \"enableRetrain\": " << (enable_retrain ? "true" : "false") << ",\n";
            config_file << "  \"enableAutoConfig\": " << (enable_auto_config ? "true" : "false") << ",\n";
            config_file << "  \"max_samples\": " << max_samples << ",\n";
            config_file << "  \"Estimated RAM (bytes)\": " << RAM_usage <<",\n";
            config_file << "  \"timestamp\": \"" << buf << "\",\n";
            config_file << "  \"author\": \"Viettran - tranvaviet@gmail.com\"\n"; 
            config_file << "}";
            config_file.close();
            // std::cout << "✅ Model configuration saved to " << config_path << "\n";
        }else {
            std::cerr << "❌ Failed to open config file for writing: " << path << "\n";
        }
    }

private:
    // Store JSON split ratios for validation purposes only
    float json_train_ratio = 0.0f;
    float json_test_ratio = 0.0f;
    float json_valid_ratio = 0.0f;
    bool json_ratios_found = false;
};

struct node_data {
    uint16_t min_split;
    uint16_t min_leaf;
    uint16_t max_depth;
    uint16_t total_nodes; // Total nodes in the tree for this configuration
    
    node_data() : min_split(3), min_leaf(1), max_depth(250), total_nodes(0) {}
    node_data(uint16_t split, uint16_t leaf, uint16_t depth, uint16_t nodes) 
        : min_split(split), min_leaf(leaf), max_depth(depth), total_nodes(nodes) {}
    node_data(uint16_t min_split, uint16_t min_leaf, uint16_t max_depth){
        min_split = min_split;
        min_leaf = min_leaf;
        max_depth = max_depth; 
        total_nodes = 0; // Default to 0, will be calculated later
    }
};

class node_predictor {
public:
    vector<node_data> training_data;
    
    // Regression coefficients for the prediction formula
    // Formula: nodes = a0 + a1*min_split + a2*min_leaf + a3*max_depth
    float coefficients[4];    
    vector<float> peak_nodes;
public:
    bool is_trained;
    uint8_t accuracy; // in percentage
    uint8_t peak_percent;   // number of nodes at depth with maximum number of nodes / total number of nodes in tree
    uint32_t trained_sample_count = 0; // Samples observed when predictor was trained
    
    
    // Improved multiple linear regression using least squares method with robustness
    void compute_coefficients() {
        if (training_data.empty()) {
            std::cerr << "❌ No node_predictor training data available" << std::endl;
            return;
        }
        
        size_t n = training_data.size();
        
        // Check for constant features (zero variance)
        bool split_varies = false, leaf_varies = false, depth_varies = false;
        uint16_t first_split = training_data[0].min_split;
        uint16_t first_leaf = training_data[0].min_leaf;
        uint16_t first_depth = training_data[0].max_depth;
        
        for (size_t i = 1; i < n; i++) {
            if (training_data[i].min_split != first_split) split_varies = true;
            if (training_data[i].min_leaf != first_leaf) leaf_varies = true;
            if (training_data[i].max_depth != first_depth) depth_varies = true;
        }
        
        // Build feature set dynamically based on variance
        vector<int> active_features; // 0=intercept, 1=split, 2=leaf, 3=depth
        active_features.push_back(0); // Always include intercept
        if (split_varies) active_features.push_back(1);
        if (leaf_varies) active_features.push_back(2);
        if (depth_varies) active_features.push_back(3);
        
        int num_features = active_features.size();
        
        if (num_features == 1) {
            // Only intercept - use mean
            double mean = 0.0;
            for (const auto& sample : training_data) {
                mean += sample.total_nodes;
            }
            coefficients[0] = static_cast<float>(mean / n);
            coefficients[1] = 0.0f;
            coefficients[2] = 0.0f;
            coefficients[3] = 0.0f;
            is_trained = true;
            return;
        }
        
        // Build design matrix sums dynamically
        vector<vector<double>> XTX(num_features, vector<double>(num_features, 0.0));
        vector<double> XTy(num_features, 0.0);
        
        for (const auto& sample : training_data) {
            vector<double> x(num_features);
            x[0] = 1.0; // intercept
            
            int idx = 1;
            if (split_varies) x[idx++] = static_cast<double>(sample.min_split);
            if (leaf_varies) x[idx++] = static_cast<double>(sample.min_leaf);
            if (depth_varies) x[idx++] = static_cast<double>(sample.max_depth);
            
            double y = static_cast<double>(sample.total_nodes);
            
            // Build X^T * X and X^T * y
            for (int i = 0; i < num_features; i++) {
                XTy[i] += x[i] * y;
                for (int j = 0; j < num_features; j++) {
                    XTX[i][j] += x[i] * x[j];
                }
            }
        }
        
        // Add small regularization to diagonal for numerical stability
        for (int i = 0; i < num_features; i++) {
            XTX[i][i] += 1e-8;
        }
        
        // Solve using Gaussian elimination with partial pivoting
        vector<vector<double>> aug(num_features, vector<double>(num_features + 1));
        for (int i = 0; i < num_features; i++) {
            for (int j = 0; j < num_features; j++) {
                aug[i][j] = XTX[i][j];
            }
            aug[i][num_features] = XTy[i];
        }
        
        // Forward elimination
        for (int k = 0; k < num_features; k++) {
            // Find pivot
            int max_row = k;
            double max_val = std::abs(aug[k][k]);
            for (int i = k + 1; i < num_features; i++) {
                if (std::abs(aug[i][k]) > max_val) {
                    max_val = std::abs(aug[i][k]);
                    max_row = i;
                }
            }
            
            // Swap rows
            if (max_row != k) {
                std::swap(aug[k], aug[max_row]);
            }
            
            // Eliminate
            for (int i = k + 1; i < num_features; i++) {
                if (std::abs(aug[k][k]) > 1e-10) {
                    double factor = aug[i][k] / aug[k][k];
                    for (int j = k; j <= num_features; j++) {
                        aug[i][j] -= factor * aug[k][j];
                    }
                }
            }
        }
        
        // Back substitution
        vector<double> solution(num_features, 0.0);
        for (int i = num_features - 1; i >= 0; i--) {
            double sum = aug[i][num_features];
            for (int j = i + 1; j < num_features; j++) {
                sum -= aug[i][j] * solution[j];
            }
            if (std::abs(aug[i][i]) > 1e-10) {
                solution[i] = sum / aug[i][i];
            }
        }
        
        // Map solution back to coefficients
        coefficients[0] = static_cast<float>(solution[0]); // intercept
        coefficients[1] = 0.0f;
        coefficients[2] = 0.0f;
        coefficients[3] = 0.0f;
        
        int sol_idx = 1;
        if (split_varies) coefficients[1] = static_cast<float>(solution[sol_idx++]);
        if (leaf_varies) coefficients[2] = static_cast<float>(solution[sol_idx++]);
        if (depth_varies) coefficients[3] = static_cast<float>(solution[sol_idx++]);
        
        // Adjust intercept for constant features
        if (!split_varies) coefficients[0] -= coefficients[1] * first_split;
        if (!leaf_varies) coefficients[0] -= coefficients[2] * first_leaf;
        if (!depth_varies) coefficients[0] -= coefficients[3] * first_depth;
        
        is_trained = true;
    }

    float evaluate_formula(const node_data& data) const {
        if (!is_trained) {
            return manual_estimate(data);
        }
        
        float result = coefficients[0]; // bias
        result += coefficients[1] * static_cast<float>(data.min_split);
        result += coefficients[2] * static_cast<float>(data.min_leaf);
        result += coefficients[3] * static_cast<float>(data.max_depth);
        
        return std::max(10.0f, result); // ensure reasonable minimum
    }
    
    // Fallback manual estimation if model fails
    float manual_estimate(const node_data& data) const {
        if (data.min_split == 0) {
            return 100.0f;
        }
        // Simple heuristic based on tree constraints
        float safe_leaf = std::max(1.0f, static_cast<float>(data.min_leaf));
        float leaf_adjustment = 60.0f / safe_leaf;
        float depth_factor = std::min(250.0f, static_cast<float>(data.max_depth)) / 50.0f;
        float estimate = 120.0f - data.min_split * 10.0f + leaf_adjustment + depth_factor * 15.0f;
        return std::max(10.0f, estimate);
    }    
public:
    node_predictor() : is_trained(false), accuracy(0), peak_percent(0), trained_sample_count(0) {
        for (int i = 0; i < 4; i++) {
            coefficients[i] = 0.0f;
        }
    }
    
    // Load training data from CSV file
    bool init(const std::string& csv_file_path) {
        std::ifstream file(csv_file_path);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open node log file: " << csv_file_path << std::endl;
            return false;
        }
        
        training_data.clear();
        std::string line;
        
        // Skip header line
        if (!std::getline(file, line)) {
            std::cerr << "❌ Empty CSV file" << std::endl;
            return false;
        }
        
        // Read data lines
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string token;
            node_data sample;
            
            // Parse min_split
            if (!std::getline(ss, token, ',')) continue;
            sample.min_split = static_cast<uint16_t>(std::stoi(token));
            
            // Parse min_leaf
            if (!std::getline(ss, token, ',')) continue;
            sample.min_leaf = static_cast<uint16_t>(std::stoi(token));
            
            // Parse max_depth
            if (!std::getline(ss, token, ',')) continue;
            sample.max_depth = static_cast<uint16_t>(std::stoi(token));
            
            // Parse total_nodes
            if (!std::getline(ss, token, ',')) continue;
            sample.total_nodes = static_cast<uint16_t>(std::stoi(token));
            
            training_data.push_back(sample);
        }
        
        file.close();
        return !training_data.empty();
    }

    
    // Train the predictor using loaded data
    void train() {
        compute_coefficients();
        vector<int> percent_count{10};
        for(auto peak : peak_nodes) {
            if(peak > 25) percent_count[0]++;
            if(peak > 26) percent_count[1]++;
            if(peak > 27) percent_count[2]++;
            if(peak > 28) percent_count[3]++;
            if(peak > 29) percent_count[4]++;
            if(peak > 30) percent_count[5]++;   
            if(peak > 31) percent_count[6]++;
            if(peak > 32) percent_count[7]++;
            if(peak > 33) percent_count[8]++;
            if(peak > 34) percent_count[9]++;
        }
        bool peak_found = false;
        uint16_t percent_track = 25;  // start from 25% 
        size_t total_peak_nodes = peak_nodes.size(); // Fix: use actual number of trees
        for(auto count : percent_count) {
            float percent = total_peak_nodes > 0 ? (float)count/(float)total_peak_nodes * 100.0f : 0.0f; // Fix: calculate actual percentage
            if(percent < 10.0f && !peak_found) { // Fix: compare with 10.0 instead of 10
                peak_percent = static_cast<uint8_t>(std::min<uint16_t>(percent_track, 100));
                peak_found = true;
            }
            percent_track++;
        }
        if (!peak_found) { // If no percentage < 10%, use a reasonable default
            peak_percent = 30;
        }
        // std::cout << "✅ Successful create node_predictor formula !" << std::endl;
    }
    
    // Predict number of nodes for given parameters
    uint16_t predict(const node_data& data) const {
        float prediction = evaluate_formula(data);
        return static_cast<uint16_t>(std::round(prediction));
    }
    
    // Save trained model to binary file
    bool save_model(const std::string& bin_file_path) const {
        if (!is_trained) {
            std::cerr << "❌ Node predictor not trained yet" << std::endl;
            return false;
        }
        
        std::ofstream file(bin_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to create binary file: " << bin_file_path << std::endl;
            return false;
        }
        
        // Write magic number for validation
        uint32_t magic = 0x4E4F4445; // "NODE" in hex
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        
        // Write training status
        file.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
        
        // Write accuracy and peak_percent
        file.write(reinterpret_cast<const char*>(&accuracy), sizeof(accuracy));
        file.write(reinterpret_cast<const char*>(&peak_percent), sizeof(peak_percent));
        
        // Write number of coefficients (now 4 instead of 3)
        uint8_t num_coefficients = 4;
        file.write(reinterpret_cast<const char*>(&num_coefficients), sizeof(num_coefficients));
        
        // Write coefficients
        file.write(reinterpret_cast<const char*>(coefficients), sizeof(float) * 4);

        // Write dataset sample count for MCU-side drift detection
        file.write(reinterpret_cast<const char*>(&trained_sample_count), sizeof(trained_sample_count));
        
        file.close();
        return true;
    }
    
    // Load trained model from binary file
    bool load_model(const std::string& bin_file_path) {
        std::ifstream file(bin_file_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open binary file: " << bin_file_path << std::endl;
            return false;
        }
        
        // Read and verify magic number
        uint32_t magic;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        if (magic != 0x4E4F4445) {
            std::cerr << "❌ Invalid binary file format" << std::endl;
            file.close();
            return false;
        }
        
        // Read training status
        file.read(reinterpret_cast<char*>(&is_trained), sizeof(is_trained));
        
        // Read accuracy and peak_percent
        file.read(reinterpret_cast<char*>(&accuracy), sizeof(accuracy));
        file.read(reinterpret_cast<char*>(&peak_percent), sizeof(peak_percent));
        
        // Read number of coefficients
        uint8_t num_coefficients;
        file.read(reinterpret_cast<char*>(&num_coefficients), sizeof(num_coefficients));
        
        // Support both old (3) and new (4) coefficient formats for backward compatibility
        if (num_coefficients == 3) {
            std::cout << "⚠️  Loading old format with 3 coefficients, max_depth coefficient will be 0" << std::endl;
            file.read(reinterpret_cast<char*>(coefficients), sizeof(float) * 3);
            coefficients[3] = 0.0f; // Default max_depth coefficient
        } else if (num_coefficients == 4) {
            file.read(reinterpret_cast<char*>(coefficients), sizeof(float) * 4);
        } else {
            std::cerr << "❌ Invalid number of coefficients: " << (int)num_coefficients << std::endl;
            file.close();
            return false;
        }

        // Optional sample count metadata (MCU-aware builds append this)
        trained_sample_count = 0;
        uint32_t stored_samples = 0;
        file.read(reinterpret_cast<char*>(&stored_samples), sizeof(stored_samples));
        if (file.gcount() == static_cast<std::streamsize>(sizeof(stored_samples))) {
            trained_sample_count = stored_samples;
        } else {
            file.clear();
        }
        
        file.close();
        std::cout << "📂 Model loaded from " << bin_file_path << std::endl;
        return true;
    }

    // Get prediction accuracy on training data using R-squared metric
    float get_accuracy() const {
        if (!is_trained || training_data.empty()) {
            return 0.0f;
        }
        
        // Calculate mean of actual values
        double mean_actual = 0.0;
        for (const auto& sample : training_data) {
            mean_actual += static_cast<double>(sample.total_nodes);
        }
        mean_actual /= training_data.size();
        
        // Calculate total sum of squares (TSS) and residual sum of squares (RSS)
        double tss = 0.0;
        double rss = 0.0;
        
        for (const auto& sample : training_data) {
            double actual = static_cast<double>(sample.total_nodes);
            double predicted = static_cast<double>(predict(sample));
            
            tss += (actual - mean_actual) * (actual - mean_actual);
            rss += (actual - predicted) * (actual - predicted);
        }
        
        // Calculate R-squared (coefficient of determination)
        // R² = 1 - (RSS/TSS)
        // R² ranges from -∞ to 1, where 1 is perfect prediction
        double r_squared = (tss > 0.0) ? (1.0 - (rss / tss)) : 0.0;
        
        // Convert to percentage (0-100%) and clamp to valid range
        float accuracy_percent = static_cast<float>(r_squared * 100.0);
        return std::max(0.0f, std::min(100.0f, accuracy_percent));
    }
};

/*
------------------------------------------------------------------------------------------------------------------
-------------------------------------------------- RF_RANDOM -----------------------------------------------------
------------------------------------------------------------------------------------------------------------------
*/
class Rf_random {
private:
    struct PCG32 {
        uint64_t state = 0x853c49e6748fea9bULL;
        uint64_t inc   = 0xda3e39cb94b95bdbULL;

        inline void seed(uint64_t initstate, uint64_t initseq) {
            state = 0U;
            inc = (initseq << 1u) | 1u;
            next();
            state += initstate;
            next();
        }

        inline uint32_t next() {
            uint64_t oldstate = state;
            state = oldstate * 6364136223846793005ULL + inc;
            uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
            uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
            return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
        }

        inline uint32_t bounded(uint32_t bound) {
            if (bound == 0) return 0;
            uint32_t threshold = -bound % bound;
            while (true) {
                uint32_t r = next();
                if (r >= threshold) return r % bound;
            }
        }
    };

    static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
    static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
    static constexpr uint64_t SMIX_C1 = 0x9e3779b97f4a7c15ULL;
    static constexpr uint64_t SMIX_C2 = 0xbf58476d1ce4e5b9ULL;
    static constexpr uint64_t SMIX_C3 = 0x94d049bb133111ebULL;

    // Avoid ODR by using function-local statics for global seed state
    static uint64_t &global_seed() { static uint64_t v = 0ULL; return v; }
    static bool &has_global() { static bool v = false; return v; }

    uint64_t base_seed = 0;
    PCG32 engine;

    inline uint64_t splitmix64(uint64_t x) const {
        x += SMIX_C1;
        x = (x ^ (x >> 30)) * SMIX_C2;
        x = (x ^ (x >> 27)) * SMIX_C3;
        return x ^ (x >> 31);
    }

public:
    Rf_random() {
        if (has_global()) {
            base_seed = global_seed();
        } else {
            // Use clock-derived entropy only (avoid std::random_device for MCU parity)
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            uint64_t hw = static_cast<uint64_t>(duration.count());
            auto steady_now = std::chrono::steady_clock::now().time_since_epoch();
            uint64_t mono = static_cast<uint64_t>(steady_now.count());
            uint64_t addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
            base_seed = splitmix64(hw ^ (mono << 1) ^ (addr >> 3));
        }
        engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
    }

    Rf_random(uint64_t seed, bool use_provided_seed) {
        if (use_provided_seed) {
            base_seed = seed;
        } else if (has_global()) {
            base_seed = global_seed();
        } else {
            // Use clock-derived entropy only (avoid std::random_device for MCU parity)
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            uint64_t hw = static_cast<uint64_t>(duration.count());
            auto steady_now = std::chrono::steady_clock::now().time_since_epoch();
            uint64_t mono = static_cast<uint64_t>(steady_now.count());
            uint64_t addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
            base_seed = splitmix64(hw ^ (mono << 1) ^ (addr >> 3) ^ seed);
        }
        engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
    }

    // Global seed control
    static void setGlobalSeed(uint64_t seed) { global_seed() = seed; has_global() = true; }
    static void clearGlobalSeed() { has_global() = false; }
    static bool hasGlobalSeed() { return has_global(); }

    // Basic API
    inline uint32_t next() { return engine.next(); }
    inline uint32_t bounded(uint32_t bound) { return engine.bounded(bound); }
    inline float nextFloat() { return static_cast<float>(next()) / static_cast<float>(UINT32_MAX); }
    inline double nextDouble() { return static_cast<double>(next()) / static_cast<double>(UINT32_MAX); }

    void seed(uint64_t new_seed) {
        base_seed = new_seed;
        engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
    }
    inline uint64_t getBaseSeed() const { return base_seed; }

    // Deterministic substreams
    Rf_random deriveRNG(uint64_t stream, uint64_t nonce = 0) const {
        uint64_t s = splitmix64(base_seed ^ (stream * SMIX_C1 + nonce));
        uint64_t inc = splitmix64(base_seed + (stream << 1) + 0x632be59bd9b4e019ULL);
        Rf_random r;
        r.base_seed = s;
        r.engine.seed(s, inc);
        return r;
    }

    // Hash helpers (FNV-1a)
    static inline uint64_t hashString(const char* data) {
        uint64_t h = FNV_OFFSET;
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(data); *p; ++p) { 
            h ^= *p; 
            h *= FNV_PRIME; 
        }
        return h;
    }
    static inline uint64_t hashBytes(const uint8_t* data, size_t len) {
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < len; ++i) {
            h ^= static_cast<uint64_t>(data[i]);
            h *= FNV_PRIME;
        }
        return h;
    }

    template <class IdVec>
    static uint64_t hashIDVector(const IdVec& ids) {
        uint64_t h = FNV_OFFSET;
        for (const auto& v : ids) {
            using ValueT = std::decay_t<decltype(v)>;
            ValueT value = v;
            for (size_t byte = 0; byte < sizeof(ValueT); ++byte) {
                h ^= (static_cast<uint64_t>(value) >> (byte * 8)) & 0xFFULL;
                h *= FNV_PRIME;
            }
        }
        size_t sz = ids.size();
        for (size_t byte = 0; byte < sizeof(sz); ++byte) {
            h ^= (static_cast<uint64_t>(sz) >> (byte * 8)) & 0xFFULL;
            h *= FNV_PRIME;
        }
        return h;
    }
};



