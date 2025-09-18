#pragma once

#include "STL_MCU.h"  
#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#include <filesystem>
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
    packed_vector<2> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
    uint16_t label;                     // label of the sample 
};

using sampleID_set = ID_vector<uint16_t>; // Sample ID set type
using sample_set = b_vector<Rf_sample>; // set of samples


struct Tree_node{
    uint32_t packed_data; 
    
    // Bit layout (32 bits total) - optimized for breadth-first tree building:
    // Bits 0-9:    featureID (10 bits) - 0 to 1023 features
    // Bits 10-17:  label (8 bits) - 0 to 255 classes  
    // Bits 18-19:  threshold (2 bits) - 0 to 3
    // Bit 20:      is_leaf (1 bit) - 0 or 1
    // Bits 21-31:  left child index (11 bits) - 0 to 2047 nodes -> max 8kB RAM per tree 
    // Note: right child index = left child index + 1 (breadth-first property)

    // Constructor
    Tree_node() : packed_data(0) {}

    // Getter methods for packed data
    uint16_t getFeatureID() const {
        return packed_data & 0x3FF;  // Bits 0-9 (10 bits)
    }
    
    uint16_t getLabel() const {
        return (packed_data >> 10) & 0xFF;  // Bits 10-17 (8 bits)
    }
    
    uint16_t getThreshold() const {
        return (packed_data >> 18) & 0x03;  // Bits 18-19 (2 bits)
    }
    
    bool getIsLeaf() const {
        return (packed_data >> 20) & 0x01;  // Bit 20
    }
    
    uint16_t getLeftChildIndex() const {
        return (packed_data >> 21) & 0x7FF;  // Bits 21-31 (11 bits)
    }
    
    uint16_t getRightChildIndex() const {
        return getLeftChildIndex() + 1;  // Breadth-first property: right = left + 1
    }
    
    // Setter methods for packed data
    void setFeatureID(uint16_t featureID) {
        packed_data = (packed_data & 0xFFFFFC00) | (featureID & 0x3FF);  // Bits 0-9
    }
    
    void setLabel(uint16_t label) {
        packed_data = (packed_data & 0xFFFC03FF) | ((uint32_t)(label & 0xFF) << 10);  // Bits 10-17
    }
    
    void setThreshold(uint16_t threshold) {
        packed_data = (packed_data & 0xFFF3FFFF) | ((uint32_t)(threshold & 0x03) << 18);  // Bits 18-19
    }
    
    void setIsLeaf(bool isLeaf) {
        packed_data = (packed_data & 0xFFEFFFFF) | ((uint32_t)(isLeaf ? 1 : 0) << 20);  // Bit 20
    }
    
    void setLeftChildIndex(uint16_t index) {
        packed_data = (packed_data & 0x001FFFFF) | ((uint32_t)(index & 0x7FF) << 21);  // Bits 21-31
    }
    
    // Note: setRightChildIndex is not needed since right = left + 1
};

struct NodeToBuild {
    uint16_t nodeIndex;
    uint16_t begin;   // inclusive
    uint16_t end;     // exclusive
    uint16_t depth;
    
    NodeToBuild() : nodeIndex(0), begin(0), end(0), depth(0) {}
    NodeToBuild(uint16_t idx, uint16_t b, uint16_t e, uint16_t d) 
        : nodeIndex(idx), begin(b), end(e), depth(d) {}
};


class Rf_tree {
  public:
    b_vector<Tree_node> nodes;  // Vector-based tree storage
    std::string filename;

    Rf_tree() : filename("") {}
    
    Rf_tree(const std::string& fn) : filename(fn) {}

    // Count total number of nodes in the tree (including leaf nodes)
    uint32_t countNodes() const {
        return nodes.size();
    }

    size_t get_memory_usage() const {
        return nodes.size() * 4;
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
        
        // Write number of nodes
        uint32_t nodeCount = nodes.size();
        file.write(reinterpret_cast<const char*>(&nodeCount), sizeof(nodeCount));
        
        // Save all nodes - just save the packed_data since everything is packed into it
        for (const auto& node : nodes) {
            file.write(reinterpret_cast<const char*>(&node.packed_data), sizeof(node.packed_data));
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
        
        // Read number of nodes
        uint32_t nodeCount;
        file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
        if (nodeCount == 0 || nodeCount > 2047) { // 11-bit limit
            std::cout << "❌ Invalid node count in tree file: " << nodeCount << std::endl;
            file.close();
            return;
        }
        
        // Clear existing nodes and reserve space
        nodes.clear();
        nodes.reserve(nodeCount);
        
        // Load all nodes
        for (uint32_t i = 0; i < nodeCount; i++) {
            Tree_node node;
            file.read(reinterpret_cast<char*>(&node.packed_data), sizeof(node.packed_data));
            
            if (file.fail()) {
                std::cout << "❌ Failed to read node " << i << " from tree file: " << file_path << std::endl;
                nodes.clear();
                file.close();
                return;
            }
            
            nodes.push_back(node);
        }
        
        file.close();
        
        // Update filename for future operations
        filename = file_path;
        
        // std::cout << "✅ Tree loaded successfully: " << file_path 
        //           << " (" << nodeCount << " nodes)" << std::endl;
    }

    uint16_t predictSample(const Rf_sample& sample) const {
        if (nodes.empty()) return 0;
        
        uint16_t currentIndex = 0;  // Start from root
        
        while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
            // Bounds check for feature access
            if (nodes[currentIndex].getFeatureID() >= sample.features.size()) {
                return 0; // Invalid feature access
            }
            
            uint16_t featureValue = sample.features[nodes[currentIndex].getFeatureID()];
            
            if (featureValue <= nodes[currentIndex].getThreshold()) {
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
    uint16_t getTreeDepthRecursive(uint16_t nodeIndex) const {
        if (nodeIndex >= nodes.size()) return 0;
        if (nodes[nodeIndex].getIsLeaf()) return 1;
        
        uint16_t leftIndex = nodes[nodeIndex].getLeftChildIndex();
        uint16_t rightIndex = nodes[nodeIndex].getRightChildIndex();
        
        uint16_t leftDepth = getTreeDepthRecursive(leftIndex);
        uint16_t rightDepth = getTreeDepthRecursive(rightIndex);
        
        return 1 + (leftDepth > rightDepth ? leftDepth : rightDepth);
    }
};

class Rf_data {
public:
    b_vector<Rf_sample> allSamples;    // Simple vector storage for all samples
    std::string filename = "";
 
    Rf_data() {}  
    
    // Constructor with filename
    Rf_data(const std::string& fname) : filename(fname) {}

    // Load data from CSV format (used only once for initial dataset conversion)
    void loadCSVData(std::string csvFilename, uint16_t numFeatures) {
        std::ifstream file(csvFilename);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open CSV file for reading: " << csvFilename << std::endl;
            return;
        }

        std::cout << "📊 Loading CSV: " << csvFilename << " (expecting " << (int)numFeatures << " features per sample)" << std::endl;
        
        uint16_t sampleID = 0;
        uint16_t linesProcessed = 0;
        uint16_t emptyLines = 0;
        uint16_t validSamples = 0;
        uint16_t invalidSamples = 0;
        
        std::string line;
        while (std::getline(file, line) && sampleID < 10000) {
            linesProcessed++;
            
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            
            if (line.empty()) {
                emptyLines++;
                continue;
            }

            Rf_sample s;
            s.features.clear();
            s.features.reserve(numFeatures);

            uint16_t fieldIdx = 0;
            std::stringstream ss(line);
            std::string token;
            
            while (std::getline(ss, token, ',')) {
                // Trim token
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                
                uint16_t v = static_cast<uint16_t>(std::stoi(token));

                if (fieldIdx == 0) {
                    s.label = v;
                } else {
                    s.features.push_back(v);
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
            
            s.features.fit();

            allSamples.push_back(s);
            sampleID++;
            validSamples++;
        }
        
        std::cout << "📋 CSV Processing Results:" << std::endl;
        std::cout << "   Lines processed: " << linesProcessed << std::endl;
        std::cout << "   Empty lines: " << emptyLines << std::endl;
        std::cout << "   Valid samples: " << validSamples << std::endl;
        std::cout << "   Invalid samples: " << invalidSamples << std::endl;
        std::cout << "   Total samples in memory: " << allSamples.size() << std::endl;
        
        file.close();
        std::cout << "✅ CSV data loaded successfully." << std::endl;
    }
};

typedef enum Rf_training_flags : uint16_t{
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_training_flags;

// Helper functions to convert between flag enum and string representation
std::string flagsToString(uint16_t flags) {
    std::vector<std::string> flag_names;
    
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
    uint16_t k_fold; 
    uint16_t min_split; 
    uint16_t max_depth;
    uint16_t num_samples;  // number of samples in the base data
    uint32_t random_seed = 42; // random seed for Rf_random class
    size_t RAM_usage = 0;
    int epochs = 20;    // number of epochs for inner training

    float train_ratio = 0.7f; // ratio of training data to total data, automatically set
    float test_ratio = 0.15f; // ratio of test data to total data, automatically set  
    float valid_ratio = 0.15f; // ratio of validation data to total data, automatically set
    float boostrap_ratio = 0.632f; // ratio of samples taken from train data to create subdata

    b_vector<uint16_t> max_depth_range;      // for training
    b_vector<uint16_t> min_split_range;      // for training 
    b_vector<bool> overwrite{4}; // min_split-> max_depth-> unity_threshold-> training_flag

    Rf_training_flags training_flag;
    std::string data_path;
    
    // model configurations
    float unity_threshold = 0.5f;   // unity_threshold  for classification, effect to precision and recall - default value
    float impurity_threshold = 0.01f; // threshold for impurity, default is 0.01
    
    // Training score method: "oob_score", "valid_score", or "k-fold_score"
    std::string training_score = "oob_score";

    bool use_gini = false; // use Gini impurity for training
    bool use_bootstrap = true; // use bootstrap sampling for training

    float result_score = 0.0f; // result score of the model

public:
    // Constructor
    Rf_config() {};

    Rf_config (std::string init_path = config_path){
        for (size_t i = 0; i < 4; i++) {
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
        
        // Extract k_fold
        pos = content.find("\"k_fold\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                k_fold = static_cast<uint16_t>(std::stoi(value));
            }
        }
        
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
                if (value == "oob_score" || value == "valid_score" || value == "k-fold_score") {
                    training_score = value;
                } else {
                    training_score = "oob_score"; // default
                }
            }
        }

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
                k_fold = static_cast<uint16_t>(std::stoi(value));
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
                
                std::cout << "📊 Split ratios loaded from JSON: train=" << train_ratio 
                          << ", test=" << test_ratio << ", valid=" << valid_ratio << std::endl;
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

        // Initialize overwrite vector: min_split, max_depth, unity_threshold, train_flag
        overwrite.clear();
        for (int i = 0; i < 4; i++) {
            overwrite.push_back(false);
        }

        // Check and extract min_split
        overwrite[0] = isParameterEnabled("min_split");
        if (overwrite[0]) {
            std::string value = extractParameterValue("min_split");
            if (!value.empty()) {
                min_split = static_cast<uint16_t>(std::stoi(value));
                std::cout << "⚙️  min_split override enabled: " << (int)min_split << std::endl;
            }
        }

        // Check and extract max_depth
        overwrite[1] = isParameterEnabled("max_depth");
        if (overwrite[1]) {
            std::string value = extractParameterValue("max_depth");
            if (!value.empty()) {
                max_depth = static_cast<uint16_t>(std::stoi(value));
                std::cout << "⚙️  max_depth override enabled: " << (int)max_depth << std::endl;
            }
        }

        // Check and extract unity_threshold
        overwrite[2] = isParameterEnabled("unity_threshold");
        if (overwrite[2]) {
            std::string value = extractParameterValue("unity_threshold");
            if (!value.empty()) {
                unity_threshold = std::stof(value);
                std::cout << "⚙️  unity_threshold override enabled: " << unity_threshold << std::endl;
            }
        }

        // Check and extract train_flag
        overwrite[3] = isParameterEnabled("train_flag");
        if (overwrite[3]) {
            std::string value = extractParameterValue("train_flag");
            if (!value.empty()) {
                bool isStacked = isParameterStacked("train_flag");
                if (isStacked) {
                    // Stacked mode: combine with automatic flags (will be determined later)
                    uint16_t user_flags = stringToFlags(value);
                    training_flag = static_cast<Rf_training_flags>(user_flags); // Temporarily store user flags
                    std::cout << "⚙️  train_flag stacked mode enabled: " << flagsToString(user_flags) << " (will be combined with auto-detected flags)\n";
                } else {
                    // Overwrite mode: replace automatic flags completely
                    training_flag = static_cast<Rf_training_flags>(stringToFlags(value));
                    std::cout << "⚙️  train_flag overwrite mode enabled: " << flagsToString(training_flag) << std::endl;
                }
            }
        }

        // Extract impurity_threshold if present
        pos = content.find("\"impurity_threshold\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                impurity_threshold = std::stof(value);
            }
        }

        // Check for invalid configuration cases and apply automatic ratio setting
        // This will be done after dataset analysis in init() method
        
        std::cout << "✅ Configuration loaded from " << init_path << std::endl;
        std::cout << "   Number of trees: " << (int)num_trees << std::endl;
        std::cout << "   K-fold: " << (int)k_fold << std::endl;
        std::cout << "   Criterion: " << (use_gini ? "gini" : "entropy") << std::endl;
        std::cout << "   Use bootstrap: " << (use_bootstrap ? "true" : "false") << std::endl;
        std::cout << "   Training score method: " << training_score << std::endl;
        std::cout << "   Data path: " << data_path << std::endl;
        if (json_ratios_found) {
            std::cout << "   JSON split ratios found: train=" << json_train_ratio << ", test=" << json_test_ratio << ", valid=" << json_valid_ratio << " (will be validated)" << std::endl;
        }
        std::cout << "   Random seed: " << random_seed << std::endl;
    }

    void init(std::string data_path){
        // For PC training, use standard file I/O instead of SPIFFS
        std::ifstream file(data_path);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open file: " << data_path << "\n";
            return;
        }

        unordered_map<uint16_t, uint16_t> labelCounts;
        unordered_set<uint16_t> featureValues;

        uint16_t numSamples = 0;
        uint16_t maxFeatures = 0;

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
                    uint16_t featurePos = featureIndex - 1;
                    if (featurePos + 1 > maxFeatures) {
                        maxFeatures = featurePos + 1;
                    }
                }
                featureIndex++;
            }

            if (!malformed) {
                numSamples++;
                if (numSamples >= 10000) break;
            }
        }

        file.close();

        num_features = maxFeatures;
        num_samples = numSamples;
        num_labels = labelCounts.size();

        // Dataset summary
        std::cout << "📊 Dataset Summary:\n";
        std::cout << "  Total samples: " << numSamples << "\n";
        std::cout << "  Total features: " << maxFeatures << "\n";
        std::cout << "  Unique labels: " << labelCounts.size() << "\n";

        // Automatic ratio configuration based on dataset size
        float samples_per_label = (float)numSamples / labelCounts.size();
        if (samples_per_label > 150) {
            if(training_score == "valid_score") {
                // Large dataset: use 0.7, 0.15, 0.15
                train_ratio = 0.7f;
                test_ratio = 0.15f;
                valid_ratio = 0.15f;
                std::cout << "📏 Large dataset (samples/label: " << samples_per_label << " > 50). Using ratios: 0.7/0.15/0.15\n";
            }
        } else {
            // Small dataset: use 0.6, 0.2, 0.2
            train_ratio = 0.6f;
            test_ratio = 0.2f;
            valid_ratio = 0.2f;
            std::cout << "📏 Small dataset (samples/label: " << samples_per_label << " ≤ 150). Using ratios: 0.6/0.2/0.2\n";
        }

        // Validate training_score and split_ratio consistency
        if (json_ratios_found) {
            bool mismatch_detected = false;
            
            // Check for invalid cases
            if (training_score == "valid_score" && json_valid_ratio == 0.0f) {
                std::cout << "⚠️ Invalid configuration: valid_score selected but valid_ratio = 0 in JSON\n";
                if(samples_per_label <= 150){
                    train_ratio = 0.6f;
                    test_ratio = 0.2f;
                    valid_ratio = 0.2f;
                    std::cout << "🔧 Adjusting to small dataset ratios: train=0.6, test=0.2, valid=0.2";
                }else{
                    train_ratio = 0.7f;
                    test_ratio = 0.15f;
                    valid_ratio = 0.15f;
                    std::cout << "🔧 Adjusting to large dataset ratios: train=0.7, test=0.15, valid=0.15";
                }
            }
            else if (training_score != "valid_score" && json_valid_ratio > 0.0f) {
                std::cout << "⚠️ Invalid configuration: " << training_score << " selected but valid_ratio > 0 in JSON\n";
                if(samples_per_label <= 150){
                    train_ratio = 0.75f;
                    test_ratio = 0.25f;
                    valid_ratio = 0.0f;
                    std::cout << "🔧 Adjusting to small dataset ratios: train=0.75, test=0.25, valid=0.0";
                }else{
                    train_ratio = 0.8f;
                    test_ratio = 0.2f;
                    valid_ratio = 0.0f;
                    std::cout << "🔧 Adjusting to large dataset ratios: train=0.8, test=0.2, valid=0.0";
                }
                    
            }
        }

        // Final validation and normalization of ratios
        float total_ratio = train_ratio + test_ratio + valid_ratio;
        if (abs(total_ratio - 1.0f) > 0.001f) {
            std::cout << "⚠️ Split ratios don't sum to 1.0 (sum: " << total_ratio << "). Normalizing...\n";
            train_ratio /= total_ratio;
            test_ratio /= total_ratio;
            valid_ratio /= total_ratio;
        }

        // Analyze label distribution and set appropriate training flags
        if (labelCounts.size() > 0) {
            uint16_t minorityCount = numSamples;
            uint16_t majorityCount = 0;

            for (auto& it : labelCounts) {
                uint16_t count = it.second;
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

            // Set training flags based on class imbalance
            if (!overwrite[3]) {
                // Apply automatic selection only if not overridden
                if (maxImbalanceRatio > 10.0f) {
                    training_flag = Rf_training_flags::RECALL;
                    std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to RECALL.\n";
                } else if (maxImbalanceRatio > 3.0f) {
                    training_flag = Rf_training_flags::F1_SCORE;
                    std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to F1_SCORE.\n";
                } else if (maxImbalanceRatio > 1.5f) {
                    training_flag = Rf_training_flags::PRECISION;
                    std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Setting trainFlag to PRECISION.\n";
                } else {
                    training_flag = Rf_training_flags::ACCURACY;
                    std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to ACCURACY.\n";
                }
            } else {
                // Check if it's stacked mode or overwrite mode
                bool isStacked = false;
                // Re-check the config file for stacked mode (we need this info here)
                std::ifstream check_file("model_json");
                if (check_file.is_open()) {
                    std::string check_content, check_line;
                    while (std::getline(check_file, check_line)) {
                        check_content += check_line;
                    }
                    check_file.close();
                    
                    size_t pos = check_content.find("\"train_flag\"");
                    if (pos != std::string::npos) {
                        size_t status_pos = check_content.find("\"status\":", pos);
                        if (status_pos != std::string::npos && status_pos < check_content.find("}", pos)) {
                            size_t start = check_content.find("\"", status_pos + 9) + 1;
                            size_t end = check_content.find("\"", start);
                            if (start != std::string::npos && end != std::string::npos) {
                                std::string status = check_content.substr(start, end - start);
                                isStacked = (status == "stacked");
                            }
                        }
                    }
                }
                
                if (isStacked) {
                    // Stacked mode: combine user flags with auto-detected flags
                    uint16_t user_flags = static_cast<uint16_t>(training_flag); // User flags from init()
                    uint16_t auto_flags = 0;
                    
                    // Determine automatic flags based on dataset
                    if (maxImbalanceRatio > 10.0f) {
                        auto_flags = RECALL;
                        std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Auto-detected flag: RECALL.\n";
                    } else if (maxImbalanceRatio > 3.0f) {
                        auto_flags = F1_SCORE;
                        std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Auto-detected flag: F1_SCORE.\n";
                    } else if (maxImbalanceRatio > 1.5f) {
                        auto_flags = PRECISION;
                        std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Auto-detected flag: PRECISION.\n";
                    } else {
                        auto_flags = ACCURACY;
                        std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Auto-detected flag: ACCURACY.\n";
                    }
                    
                    // Combine user flags with auto-detected flags
                    uint16_t combined_flags = user_flags | auto_flags;
                    training_flag = static_cast<Rf_training_flags>(combined_flags);
                    std::cout << "🔗 Stacked train_flags: " << flagsToString(combined_flags) 
                              << " (user: " << flagsToString(user_flags) << " + auto: " << flagsToString(auto_flags) << ")\n";
                } else {
                    // Overwrite mode: user flags completely replace automatic detection
                    std::cout << "🔧 Using train_flag overwrite: " << flagsToString(training_flag) << " (dataset ratio: " << maxImbalanceRatio << ")\n";
                }
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
                if (samples_per_label > 150) {
                    train_ratio = 0.85f;  // Redistribute validation ratio to training
                    test_ratio = 0.15f;
                    valid_ratio = 0.0f;
                } else {
                    train_ratio = 0.8f;   // Redistribute validation ratio to training
                    test_ratio = 0.2f;
                    valid_ratio = 0.0f;
                }
                std::cout << "📏 Adjusted ratios after removing validation: train=" << train_ratio 
                         << ", test=" << test_ratio << ", valid=" << valid_ratio << "\n";
            }
        }

        std::cout << "🎯 Final split ratios: train=" << train_ratio << ", test=" << test_ratio 
                  << ", valid=" << valid_ratio << " (method: " << training_score << ")\n";
        
        // Calculate optimal parameters based on dataset size
        int baseline_minsplit_ratio = 100 * (num_samples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint16_t min_minSplit = std::min(2, (int)(num_samples / baseline_minsplit_ratio));
        int dynamic_max_split = std::min(min_minSplit + 6, (int)(log2(num_samples) / 4 + num_features / 25.0f));
        uint16_t max_minSplit = std::min(24, dynamic_max_split); // Cap at 24 to prevent overly simple trees.
        if (max_minSplit <= min_minSplit) max_minSplit = min_minSplit + 4; // Ensure a valid range.


        int base_maxDepth = std::max((int)log2(num_samples * 2.0f), (int)(log2(num_features) * 2.5f));
        uint16_t max_maxDepth = std::max(6, base_maxDepth);
        int dynamic_min_depth = std::max(4, (int)(log2(num_features) + 2));
        uint16_t min_maxDepth = std::min((int)max_maxDepth - 2, dynamic_min_depth); // Ensure a valid range.
        if (min_maxDepth >= max_maxDepth) min_maxDepth = max_maxDepth - 2;
        if (min_maxDepth < 4) min_maxDepth = 4;

        // Set default values only if not overridden
        if (!overwrite[0]) {
            min_split = (min_minSplit + max_minSplit + 1) / 2 ;
        }
        if (!overwrite[1]) {
            max_depth = (min_maxDepth + max_maxDepth) / 2 ;
        }
        std::cout << "min_split range: " << (int)min_minSplit << " - " << (int)max_minSplit << std::endl;
        std::cout << "max_depth range: " << (int)min_maxDepth << " - " << (int)max_maxDepth << std::endl;
        
        // Build ranges based on override status
        min_split_range.clear();
        max_depth_range.clear();
        
        if (overwrite[0]) {
            // min_split override is enabled - use only the override value
            min_split_range.push_back(min_split);
            std::cout << "🔧 min_split override active: using fixed value " << (int)min_split << "\n";
        } else {
            // min_split automatic - build range with step 2
            uint16_t min_split_step = 2;
            if(overwrite[1] || max_minSplit - min_minSplit < 4) {
                min_split_step = 1; // If max_depth is overridden, use smaller step for min_split
            }
            for(uint16_t i = min_minSplit; i <= max_minSplit; i += min_split_step) {
                min_split_range.push_back(i);
            }
        }
        
        if (overwrite[1]) {
            // max_depth override is enabled - use only the override value
            max_depth_range.push_back(max_depth);
            std::cout << "🔧 max_depth override active: using fixed value " << (int)max_depth << "\n";
        } else {
            // max_depth automatic - build range with step 2
            uint16_t max_depth_step = 2;
            if(overwrite[0]) {
                max_depth_step = 1; // If min_split is overridden, use smaller step for max_depth
            }
            for(uint16_t i = min_maxDepth; i <= max_maxDepth; i += max_depth_step) {
                max_depth_range.push_back(i);
            }
        }
        
        // Ensure at least one value in each range (fallback safety)
        if (min_split_range.empty()) {
            min_split_range.push_back(min_split);
        }
        if (max_depth_range.empty()) {
            max_depth_range.push_back(max_depth);
        }

        std::cout << "Setting minSplit to " << (int)min_split
                  << " and maxDepth to " << (int)max_depth << " based on dataset size.\n";
        
        // Debug: Show range sizes
        std::cout << "📊 Training ranges: min_split_range has " << min_split_range.size() 
                  << " values, max_depth_range has " << max_depth_range.size() << " values\n";
        std::cout << "   min_split values: ";
        for(size_t i = 0; i < min_split_range.size(); i++) {
            std::cout << (int)min_split_range[i];
            if (i + 1 < min_split_range.size()) std::cout << ", ";
        }
        std::cout << "\n   max_depth values: ";
        for(size_t i = 0; i < max_depth_range.size(); i++) {
            std::cout << (int)max_depth_range[i];
            if (i + 1 < max_depth_range.size()) std::cout << ", ";
        }
        std::cout << "\n";
        
        // Check for unity_threshold override
        if (!overwrite[2]) {
            // Apply automatic calculation only if not overridden
            unity_threshold  = 1.25f / static_cast<float>(num_labels);
            if(num_features == 2) unity_threshold = 0.6f;
        } else {
            std::cout << "🔧 Using unity_threshold override: " << unity_threshold << std::endl;
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
            config_file << "  \"train_ratio\": " << norm_train_ratio << ",\n";
            config_file << "  \"test_ratio\": " << norm_test_ratio << ",\n";
            config_file << "  \"valid_ratio\": " << norm_valid_ratio << ",\n";
            config_file << "  \"minSplit\": " << (int)min_split << ",\n";
            config_file << "  \"maxDepth\": " << (int)max_depth << ",\n";
            config_file << "  \"useBootstrap\": " << (use_bootstrap ? "true" : "false") << ",\n";
            config_file << "  \"boostrapRatio\": " << boostrap_ratio << ",\n";
            config_file << "  \"useGini\": " << (use_gini ? "true" : "false") << ",\n";
            config_file << "  \"trainingScore\": \"" << training_score << "\",\n";
            config_file << "  \"k_fold\": " << (int)k_fold << ",\n";
            config_file << "  \"unityThreshold\": " << unity_threshold << ",\n";
            config_file << "  \"impurityThreshold\": " << impurity_threshold << ",\n";
            config_file << "  \"trainFlag\": \"" << flagsToString(training_flag) << "\",\n";
            config_file << "  \"resultScore\": " << result_score << ",\n";
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
    uint16_t max_depth;
    uint16_t total_nodes; // Total nodes in the tree for this configuration
    
    node_data() : min_split(3), max_depth(6), total_nodes(0) {}
    node_data(uint16_t split, uint16_t depth, uint16_t nodes) 
        : min_split(split), max_depth(depth), total_nodes(nodes) {}
    node_data(uint16_t min_split, uint16_t max_depth){
        min_split = min_split;
        max_depth = max_depth;
        total_nodes = 0; // Default to 0, will be calculated later
    }
};

class node_predictor {
public:
    std::vector<node_data> training_data;
    
    // Regression coefficients for the prediction formula
    // Formula: nodes = a0 + a1*min_split + a2*max_depth
    float coefficients[3];    
    b_vector<float> peak_nodes;
public:
    bool is_trained;
    uint16_t accuracy; // in percentage
    uint16_t peak_percent;   // number of nodes at depth with maximum number of nodes / total number of nodes in tree
    
    
    // Helper methods for regression analysis
    void compute_coefficients() {
        if (training_data.empty()) {
            std::cerr << "❌ No training data available" << std::endl;
            return;
        }
        
        size_t n = training_data.size();
        
        // Dynamically analyze the data patterns
        // Collect all unique min_split and max_depth values
        std::vector<uint16_t> unique_min_splits;
        std::vector<uint16_t> unique_max_depths;
        
        for (const auto& sample : training_data) {
            // Add unique min_split values
            if (std::find(unique_min_splits.begin(), unique_min_splits.end(), sample.min_split) == unique_min_splits.end()) {
                unique_min_splits.push_back(sample.min_split);
            }
            // Add unique max_depth values
            if (std::find(unique_max_depths.begin(), unique_max_depths.end(), sample.max_depth) == unique_max_depths.end()) {
                unique_max_depths.push_back(sample.max_depth);
            }
        }
        
        std::sort(unique_min_splits.begin(), unique_min_splits.end());
        std::sort(unique_max_depths.begin(), unique_max_depths.end());
        
        // Calculate average nodes for each min_split value
        std::vector<float> avg_nodes_by_split(unique_min_splits.size(), 0.0f);
        std::vector<int> count_by_split(unique_min_splits.size(), 0);
        
        for (const auto& sample : training_data) {
            auto it = std::find(unique_min_splits.begin(), unique_min_splits.end(), sample.min_split);
            if (it != unique_min_splits.end()) {
                size_t idx = std::distance(unique_min_splits.begin(), it);
                avg_nodes_by_split[idx] += sample.total_nodes;
                count_by_split[idx]++;
            }
        }
        
        for (size_t i = 0; i < avg_nodes_by_split.size(); i++) {
            if (count_by_split[i] > 0) {
                avg_nodes_by_split[i] /= count_by_split[i];
            }
        }
        
        // Calculate average nodes for each max_depth value
        std::vector<float> avg_nodes_by_depth(unique_max_depths.size(), 0.0f);
        std::vector<int> count_by_depth(unique_max_depths.size(), 0);
        
        for (const auto& sample : training_data) {
            auto it = std::find(unique_max_depths.begin(), unique_max_depths.end(), sample.max_depth);
            if (it != unique_max_depths.end()) {
                size_t idx = std::distance(unique_max_depths.begin(), it);
                avg_nodes_by_depth[idx] += sample.total_nodes;
                count_by_depth[idx]++;
            }
        }
        
        for (size_t i = 0; i < avg_nodes_by_depth.size(); i++) {
            if (count_by_depth[i] > 0) {
                avg_nodes_by_depth[i] /= count_by_depth[i];
            }
        }
        
        // Calculate overall average as baseline
        float overall_avg = 0.0f;
        for (const auto& sample : training_data) {
            overall_avg += sample.total_nodes;
        }
        overall_avg /= n;
        
        // Calculate min_split effect (how nodes change with min_split)
        float split_effect = 0.0f;
        if (avg_nodes_by_split.size() >= 2) {
            // Calculate slope between first and last min_split values
            float first_avg = avg_nodes_by_split[0];
            float last_avg = avg_nodes_by_split.back();
            float split_range = static_cast<float>(unique_min_splits.back() - unique_min_splits[0]);
            
            if (split_range > 0) {
                split_effect = (last_avg - first_avg) / split_range;
            }
        }
        
        // Calculate max_depth effect (how nodes change with max_depth)
        float depth_effect = 0.0f;
        if (avg_nodes_by_depth.size() >= 2) {
            // Calculate slope between first and last max_depth values
            float first_avg = avg_nodes_by_depth[0];
            float last_avg = avg_nodes_by_depth.back();
            float depth_range = static_cast<float>(unique_max_depths.back() - unique_max_depths[0]);
            
            if (depth_range > 0) {
                depth_effect = (last_avg - first_avg) / depth_range;
            }
        }
        
        // Build the simple linear model: nodes = bias + split_coeff * min_split + depth_coeff * max_depth
        // Calculate bias to center the model around the overall average
        float reference_split = unique_min_splits.empty() ? 3.0f : static_cast<float>(unique_min_splits[0]);
        float reference_depth = unique_max_depths.empty() ? 6.0f : static_cast<float>(unique_max_depths[0]);
        
        coefficients[0] = overall_avg - (split_effect * reference_split) - (depth_effect * reference_depth); // bias
        coefficients[1] = split_effect; // min_split coefficient
        coefficients[2] = depth_effect; // max_depth coefficient
        
        is_trained = true;
    }

    float evaluate_formula(const node_data& data) const {
        if (!is_trained) {
            return 100.0f; // default estimate
        }
        
        float result = coefficients[0]; // bias
        result += coefficients[1] * static_cast<float>(data.min_split);
        result += coefficients[2] * static_cast<float>(data.max_depth);
        
        return std::max(10.0f, result); // ensure reasonable minimum
    }

    
public:
    node_predictor() : is_trained(false), accuracy(0), peak_percent(0) {
        for (int i = 0; i < 3; i++) {
            coefficients[i] = 0.0f;
        }
    }
    
    // Load training data from CSV file
    bool init(const std::string& csv_file_path) {
        std::ifstream file(csv_file_path);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open CSV file: " << csv_file_path << std::endl;
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
            
            // Parse max_depth
            if (!std::getline(ss, token, ',')) continue;
            sample.max_depth = static_cast<uint16_t>(std::stoi(token));
            
            // Parse total_nodes (skip num_samples and num_features)
            if (!std::getline(ss, token, ',')) continue;
            sample.total_nodes = static_cast<uint16_t>(std::stoi(token));
            
            training_data.push_back(sample);
        }
        
        file.close();
        std::cout << "📊 Loaded " << training_data.size() << " training samples from CSV" << std::endl;
        return !training_data.empty();
    }

    
    // Train the predictor using loaded data
    void train() {
        std::cout << "🎯 Training node predictor..." << std::endl;
        compute_coefficients();
        b_vector<int> percent_count{10};
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
                peak_percent = percent_track;
                peak_found = true;
            }
            percent_track++;
        }
        if (!peak_found) { // If no percentage < 10%, use a reasonable default
            peak_percent = 30;
        }
        std::cout << "✅ Successful create node_predictor formula !" << std::endl;
    }
    
    // Predict number of nodes for given parameters
    uint16_t predict(const node_data& data) const {
        float prediction = evaluate_formula(data);
        return static_cast<uint16_t>(std::round(prediction));
    }
    
    // Save trained model to binary file
    bool save_model(const std::string& bin_file_path) const {
        if (!is_trained) {
            std::cerr << "❌ Model not trained yet" << std::endl;
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
        
        // Write number of coefficients
        uint16_t num_coefficients = 3;
        file.write(reinterpret_cast<const char*>(&num_coefficients), sizeof(num_coefficients));
        
        // Write coefficients
        file.write(reinterpret_cast<const char*>(coefficients), sizeof(float) * 3);
        
        file.close();
        std::cout << "💾 Model saved to " << bin_file_path << std::endl;
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
        uint16_t num_coefficients;
        file.read(reinterpret_cast<char*>(&num_coefficients), sizeof(num_coefficients));
        
        if (num_coefficients != 3) {
            std::cerr << "❌ Invalid number of coefficients: " << (int)num_coefficients << std::endl;
            file.close();
            return false;
        }
        
        // Read coefficients
        file.read(reinterpret_cast<char*>(coefficients), sizeof(float) * 3);
        
        file.close();
        std::cout << "📂 Model loaded from " << bin_file_path << std::endl;
        return true;
    }

    // Get prediction accuracy on training data
    float get_accuracy() const {
        if (!is_trained || training_data.empty()) {
            return 0.0f;
        }
        
        float total_error = 0.0f;
        float total_actual = 0.0f;
        
        for (const auto& sample : training_data) {
            uint16_t predicted = predict(sample);
            float error = std::abs(static_cast<float>(predicted) - static_cast<float>(sample.total_nodes));
            total_error += error;
            total_actual += static_cast<float>(sample.total_nodes);
        }
        
        float mape = (total_error / total_actual) * 100.0f; // Mean Absolute Percentage Error

        return std::max(0.0f, 100.0f - mape); // Return accuracy as percentage
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
            // Use high-resolution clock for entropy on PC
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            uint64_t hw = static_cast<uint64_t>(duration.count());
            uint64_t cyc = static_cast<uint64_t>(std::random_device{}());
            base_seed = splitmix64(hw ^ cyc);
        }
        engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
    }

    Rf_random(uint64_t seed, bool use_provided_seed) {
        if (use_provided_seed) {
            base_seed = seed;
        } else if (has_global()) {
            base_seed = global_seed();
        } else {
            // Use high-resolution clock for entropy on PC
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = now.time_since_epoch();
            uint64_t hw = static_cast<uint64_t>(duration.count());
            uint64_t cyc = static_cast<uint64_t>(std::random_device{}());
            base_seed = splitmix64(hw ^ cyc ^ seed);
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
    static inline uint64_t hashBytes(const uint16_t* data, size_t len) {
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < len; ++i) { 
            h ^= data[i]; 
            h *= FNV_PRIME; 
        }
        return h;
    }
    template <class IdVec>
    static uint64_t hashIDVector(const IdVec& ids) {
        uint64_t h = FNV_OFFSET;
        for (size_t i = 0; i < ids.size(); ++i) {
            uint16_t v = ids[i];
            h ^= static_cast<uint64_t>(v & 0xFF);
            h *= FNV_PRIME;
            h ^= static_cast<uint64_t>((v >> 8) & 0xFF);
            h *= FNV_PRIME;
        }
        h ^= static_cast<uint64_t>(ids.size() & 0xFF);
        h *= FNV_PRIME;
        h ^= static_cast<uint64_t>((ids.size() >> 8) & 0xFF);
        h *= FNV_PRIME;
        return h;
    }
};



