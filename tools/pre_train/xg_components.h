#pragma once

#include "STL_MCU.h"
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <limits>

#define xg_config_path "xg_config.json"
#define xg_result_folder "trained_model/"

using namespace mcu;

// Forward declaration of Rf_sample (defined in pc_components.h)
struct Rf_sample;

// XGBoost-specific node structure
// Optimized for memory efficiency with 64-bit layout
struct XG_node {
    // Layout: 64 bits total
    // Split node: [is_leaf(1) | feature_id(15) | threshold(16) | left_child_idx(32)]
    // Leaf node:  [is_leaf(1) | unused(15) | weight(48 bits as scaled int)]
    
    uint64_t data;
    
    // Bit positions for split nodes
    static constexpr uint8_t IS_LEAF_SHIFT = 63;
    static constexpr uint8_t FEATURE_SHIFT = 48;
    static constexpr uint8_t THRESHOLD_SHIFT = 32;
    static constexpr uint8_t LEFT_CHILD_SHIFT = 0;
    
    // Masks
    static constexpr uint64_t IS_LEAF_MASK = 0x1ULL;
    static constexpr uint64_t FEATURE_MASK = 0x7FFFULL;  // 15 bits
    static constexpr uint64_t THRESHOLD_MASK = 0xFFFFULL; // 16 bits
    static constexpr uint64_t LEFT_CHILD_MASK = 0xFFFFFFFFULL; // 32 bits
    static constexpr uint64_t WEIGHT_MASK = 0xFFFFFFFFFFFFULL; // 48 bits
    
    // Weight scaling: map float weights to 48-bit signed integers
    // Range: approximately [-140737.0, 140737.0] with precision ~0.00000001
    static constexpr int64_t WEIGHT_SCALE = 1000000000LL; // 10^9 for precision
    static constexpr float WEIGHT_RANGE = 140737.0f;
    
    XG_node() : data(0) {}
    
    // Getters
    bool isLeaf() const {
        return (data >> IS_LEAF_SHIFT) & IS_LEAF_MASK;
    }
    
    uint16_t getFeatureID() const {
        return static_cast<uint16_t>((data >> FEATURE_SHIFT) & FEATURE_MASK);
    }
    
    uint16_t getThreshold() const {
        return static_cast<uint16_t>((data >> THRESHOLD_SHIFT) & THRESHOLD_MASK);
    }
    
    uint32_t getLeftChildIndex() const {
        return static_cast<uint32_t>((data >> LEFT_CHILD_SHIFT) & LEFT_CHILD_MASK);
    }
    
    uint32_t getRightChildIndex() const {
        return getLeftChildIndex() + 1;
    }
    
    float getWeight() const {
        // Extract 48-bit signed weight from lower bits
        int64_t scaled = static_cast<int64_t>(data & WEIGHT_MASK);
        // Sign extend from 48 bits to 64 bits
        if (scaled & 0x800000000000LL) {
            scaled |= 0xFFFF000000000000LL;
        }
        return static_cast<float>(scaled) / WEIGHT_SCALE;
    }
    
    // Setters
    void setIsLeaf(bool is_leaf) {
        data &= ~(IS_LEAF_MASK << IS_LEAF_SHIFT);
        data |= (static_cast<uint64_t>(is_leaf ? 1 : 0) << IS_LEAF_SHIFT);
    }
    
    void setFeatureID(uint16_t feature_id) {
        data &= ~(FEATURE_MASK << FEATURE_SHIFT);
        data |= (static_cast<uint64_t>(feature_id & FEATURE_MASK) << FEATURE_SHIFT);
    }
    
    void setThreshold(uint16_t threshold) {
        data &= ~(THRESHOLD_MASK << THRESHOLD_SHIFT);
        data |= (static_cast<uint64_t>(threshold & THRESHOLD_MASK) << THRESHOLD_SHIFT);
    }
    
    void setLeftChildIndex(uint32_t index) {
        data &= ~(LEFT_CHILD_MASK << LEFT_CHILD_SHIFT);
        data |= (static_cast<uint64_t>(index & LEFT_CHILD_MASK) << LEFT_CHILD_SHIFT);
    }
    
    void setWeight(float weight) {
        // Clamp weight to valid range
        if (weight < -WEIGHT_RANGE) weight = -WEIGHT_RANGE;
        if (weight > WEIGHT_RANGE) weight = WEIGHT_RANGE;
        
        // Scale to 48-bit signed integer
        int64_t scaled = static_cast<int64_t>(weight * WEIGHT_SCALE);
        
        // Clear lower 48 bits and set weight
        data &= 0xFFFF000000000000ULL;
        data |= static_cast<uint64_t>(scaled & WEIGHT_MASK);
    }
    
    // Helper to create split node
    static XG_node makeSplitNode(uint16_t feature_id, uint16_t threshold, uint32_t left_child_idx) {
        XG_node node;
        node.setIsLeaf(false);
        node.setFeatureID(feature_id);
        node.setThreshold(threshold);
        node.setLeftChildIndex(left_child_idx);
        return node;
    }
    
    // Helper to create leaf node
    static XG_node makeLeafNode(float weight) {
        XG_node node;
        node.setIsLeaf(true);
        node.setWeight(weight);
        return node;
    }
};

// XGBoost tree structure
class XG_tree {
public:
    vector<XG_node> nodes;
    std::string filename;
    
    XG_tree() : filename("") {}
    XG_tree(const std::string& fn) : filename(fn) {}
    
    uint32_t countNodes() const {
        return static_cast<uint32_t>(nodes.size());
    }
    
    uint32_t countLeafNodes() const {
        uint32_t count = 0;
        for (const auto& node : nodes) {
            if (node.isLeaf()) count++;
        }
        return count;
    }
    
    uint16_t getTreeDepth() const {
        if (nodes.empty()) return 0;
        return getTreeDepthRecursive(0);
    }
    
    float predictSample(const Rf_sample& sample, uint8_t quant_bits) const {
        if (nodes.empty()) return 0.0f;
        
        uint32_t currentIndex = 0;
        while (currentIndex < nodes.size() && !nodes[currentIndex].isLeaf()) {
            uint16_t feature_id = nodes[currentIndex].getFeatureID();
            if (feature_id >= sample.features.size()) {
                return 0.0f; // Invalid feature access
            }
            
            uint16_t feature_value = sample.features[feature_id];
            uint16_t threshold = nodes[currentIndex].getThreshold();
            
            if (feature_value <= threshold) {
                currentIndex = nodes[currentIndex].getLeftChildIndex();
            } else {
                currentIndex = nodes[currentIndex].getRightChildIndex();
            }
            
            if (currentIndex >= nodes.size()) {
                return 0.0f; // Invalid index
            }
        }
        
        return (currentIndex < nodes.size()) ? nodes[currentIndex].getWeight() : 0.0f;
    }
    
    void clear() {
        nodes.clear();
        filename.clear();
    }
    
    size_t memoryUsage() const {
        return nodes.size() * sizeof(XG_node);
    }
    
private:
    uint16_t getTreeDepthRecursive(uint32_t nodeIndex) const {
        if (nodeIndex >= nodes.size()) return 0;
        if (nodes[nodeIndex].isLeaf()) return 1;
        
        uint32_t leftIndex = nodes[nodeIndex].getLeftChildIndex();
        uint32_t rightIndex = nodes[nodeIndex].getRightChildIndex();
        
        uint16_t leftDepth = getTreeDepthRecursive(leftIndex);
        uint16_t rightDepth = getTreeDepthRecursive(rightIndex);
        
        return 1 + std::max(leftDepth, rightDepth);
    }
};

// XGBoost configuration
class XG_config {
public:
    // Dataset parameters
    std::string data_path;
    uint16_t num_features = 0;
    uint16_t num_labels = 2;
    uint32_t num_samples = 0;
    uint8_t quantization_coefficient = 2;
    
    // XGBoost parameters
    uint16_t num_boost_rounds = 100;
    float learning_rate = 0.3f;
    float lambda = 1.0f;        // L2 regularization
    float alpha = 0.0f;         // L1 regularization
    float gamma = 0.0f;         // Minimum loss reduction
    
    // Tree parameters
    uint16_t max_depth = 6;
    uint16_t min_child_weight = 1;  // Minimum sum of instance weight (hessian) in a child
    float subsample = 1.0f;         // Subsample ratio of training instances
    float colsample_bytree = 1.0f;  // Subsample ratio of features
    
    // Training parameters
    float train_ratio = 0.8f;
    float test_ratio = 0.2f;
    uint32_t random_seed = 42;
    std::string objective = "multi:softprob"; // binary:logistic, multi:softprob, reg:squarederror
    std::string eval_metric = "mlogloss";     // logloss, mlogloss, rmse, mae
    
    // Early stopping
    bool early_stopping = false;
    uint16_t early_stopping_rounds = 10;
    float early_stopping_threshold = 0.001f;
    
    XG_config() {}
    
    XG_config(const std::string& config_file) {
        loadConfig(config_file);
    }
    
    void init(const std::string& data_file) {
        // Scan dataset to get num_features, num_labels, num_samples
        std::ifstream file(data_file);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to open dataset: " << data_file << std::endl;
            return;
        }
        
        std::set<uint16_t> unique_labels;
        num_samples = 0;
        std::string line;
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::stringstream ss(line);
            std::string token;
            uint16_t field_idx = 0;
            uint16_t label = 0;
            
            while (std::getline(ss, token, ',')) {
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                
                if (field_idx == 0) {
                    label = static_cast<uint16_t>(std::stoi(token));
                    unique_labels.insert(label);
                }
                field_idx++;
            }
            
            if (field_idx > 0) {
                if (num_features == 0) {
                    num_features = field_idx - 1; // First field is label
                }
                num_samples++;
            }
        }
        
        num_labels = static_cast<uint16_t>(unique_labels.size());
        file.close();
    }
    
    bool loadConfig(const std::string& config_file) {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            std::cout << "⚠️  XGBoost config file not found: " << config_file << ". Using defaults." << std::endl;
            return false;
        }
        
        std::string content;
        std::string line;
        while (std::getline(file, line)) {
            content += line;
        }
        file.close();
        
        // Simple JSON parser for key parameters
        auto extractValue = [&content](const std::string& key) -> std::string {
            size_t pos = content.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            
            pos = content.find(":", pos);
            if (pos == std::string::npos) return "";
            
            pos = content.find_first_not_of(" \t\n\r:", pos + 1);
            if (pos == std::string::npos) return "";
            
            size_t end = content.find_first_of(",}\n", pos);
            std::string value = content.substr(pos, end - pos);
            
            // Remove quotes if present
            value.erase(0, value.find_first_not_of(" \t\n\r\""));
            value.erase(value.find_last_not_of(" \t\n\r\",") + 1);
            
            return value;
        };
        
        // Load parameters
        std::string val;
        
        if (!(val = extractValue("data_path")).empty()) data_path = val;
        if (!(val = extractValue("num_boost_rounds")).empty()) num_boost_rounds = std::stoi(val);
        if (!(val = extractValue("learning_rate")).empty()) learning_rate = std::stof(val);
        if (!(val = extractValue("lambda")).empty()) lambda = std::stof(val);
        if (!(val = extractValue("alpha")).empty()) alpha = std::stof(val);
        if (!(val = extractValue("gamma")).empty()) gamma = std::stof(val);
        if (!(val = extractValue("max_depth")).empty()) max_depth = std::stoi(val);
        if (!(val = extractValue("min_child_weight")).empty()) min_child_weight = std::stoi(val);
        if (!(val = extractValue("subsample")).empty()) subsample = std::stof(val);
        if (!(val = extractValue("colsample_bytree")).empty()) colsample_bytree = std::stof(val);
        if (!(val = extractValue("train_ratio")).empty()) train_ratio = std::stof(val);
        if (!(val = extractValue("test_ratio")).empty()) test_ratio = std::stof(val);
        if (!(val = extractValue("random_seed")).empty()) random_seed = std::stoi(val);
        if (!(val = extractValue("quantization_coefficient")).empty()) quantization_coefficient = std::stoi(val);
        if (!(val = extractValue("objective")).empty()) objective = val;
        if (!(val = extractValue("eval_metric")).empty()) eval_metric = val;
        if (!(val = extractValue("early_stopping")).empty()) early_stopping = (val == "true");
        if (!(val = extractValue("early_stopping_rounds")).empty()) early_stopping_rounds = std::stoi(val);
        
        std::cout << "✅ XGBoost configuration loaded from " << config_file << std::endl;
        std::cout << "   Boost rounds: " << num_boost_rounds << std::endl;
        std::cout << "   Learning rate: " << learning_rate << std::endl;
        std::cout << "   Max depth: " << max_depth << std::endl;
        std::cout << "   Lambda (L2): " << lambda << std::endl;
        std::cout << "   Objective: " << objective << std::endl;
        
        return true;
    }
    
    void saveConfig(const std::string& output_path) const {
        std::ofstream file(output_path);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to save config: " << output_path << std::endl;
            return;
        }
        
        file << "{\n";
        file << "  \"data_path\": \"" << data_path << "\",\n";
        file << "  \"num_features\": " << num_features << ",\n";
        file << "  \"num_labels\": " << num_labels << ",\n";
        file << "  \"num_samples\": " << num_samples << ",\n";
        file << "  \"quantization_coefficient\": " << (int)quantization_coefficient << ",\n";
        file << "  \"num_boost_rounds\": " << num_boost_rounds << ",\n";
        file << "  \"learning_rate\": " << learning_rate << ",\n";
        file << "  \"lambda\": " << lambda << ",\n";
        file << "  \"alpha\": " << alpha << ",\n";
        file << "  \"gamma\": " << gamma << ",\n";
        file << "  \"max_depth\": " << max_depth << ",\n";
        file << "  \"min_child_weight\": " << min_child_weight << ",\n";
        file << "  \"subsample\": " << subsample << ",\n";
        file << "  \"colsample_bytree\": " << colsample_bytree << ",\n";
        file << "  \"train_ratio\": " << train_ratio << ",\n";
        file << "  \"test_ratio\": " << test_ratio << ",\n";
        file << "  \"random_seed\": " << random_seed << ",\n";
        file << "  \"objective\": \"" << objective << "\",\n";
        file << "  \"eval_metric\": \"" << eval_metric << "\",\n";
        file << "  \"early_stopping\": " << (early_stopping ? "true" : "false") << ",\n";
        file << "  \"early_stopping_rounds\": " << early_stopping_rounds << "\n";
        file << "}\n";
        
        file.close();
        std::cout << "✅ Config saved to " << output_path << std::endl;
    }
    
    void printSummary() const {
        std::cout << "\n📊 XGBoost Configuration Summary:\n";
        std::cout << "----------------------------------------\n";
        std::cout << "Dataset:\n";
        std::cout << "  Samples: " << num_samples << "\n";
        std::cout << "  Features: " << num_features << "\n";
        std::cout << "  Labels: " << num_labels << "\n";
        std::cout << "  Quantization: " << (int)quantization_coefficient << " bits\n";
        std::cout << "\nModel Parameters:\n";
        std::cout << "  Boost rounds: " << num_boost_rounds << "\n";
        std::cout << "  Learning rate: " << learning_rate << "\n";
        std::cout << "  Max depth: " << max_depth << "\n";
        std::cout << "  Lambda (L2): " << lambda << "\n";
        std::cout << "  Gamma: " << gamma << "\n";
        std::cout << "  Subsample: " << subsample << "\n";
        std::cout << "  Feature subsample: " << colsample_bytree << "\n";
        std::cout << "\nTraining:\n";
        std::cout << "  Train ratio: " << train_ratio << "\n";
        std::cout << "  Test ratio: " << test_ratio << "\n";
        std::cout << "  Objective: " << objective << "\n";
        std::cout << "  Eval metric: " << eval_metric << "\n";
        std::cout << "----------------------------------------\n";
    }
};

// Helper structure for node building queue
struct XG_NodeToBuild {
    uint32_t nodeIndex;
    vector<uint32_t> indices;
    uint16_t depth;
    
    XG_NodeToBuild() : nodeIndex(0), depth(0) {}
    XG_NodeToBuild(uint32_t idx, vector<uint32_t> inds, uint16_t d)
        : nodeIndex(idx), indices(std::move(inds)), depth(d) {}
};
