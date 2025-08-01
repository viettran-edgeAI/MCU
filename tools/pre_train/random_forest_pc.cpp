#include "STL_MCU.h"  
#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#ifdef _WIN32
#include <direct.h>
#define mkdir _mkdir
#endif

using namespace mcu;

struct Rf_sample{
    packed_vector<2, SMALL> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
    uint8_t label;                     // label of the sample 
};

using OOB_set = ChainedUnorderedSet<uint16_t>; // OOB set type
using sampleID_set = ChainedUnorderedSet<uint16_t>; // Sample ID set type
using sample_set = ChainedUnorderedMap<uint16_t, Rf_sample>; // set of samples

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
    
    uint8_t getLabel() const {
        return (packed_data >> 10) & 0xFF;  // Bits 10-17 (8 bits)
    }
    
    uint8_t getThreshold() const {
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
    
    void setLabel(uint8_t label) {
        packed_data = (packed_data & 0xFFFC03FF) | ((uint32_t)(label & 0xFF) << 10);  // Bits 10-17
    }
    
    void setThreshold(uint8_t threshold) {
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
        
        std::cout << "✅ Tree loaded successfully: " << file_path 
                  << " (" << nodeCount << " nodes)" << std::endl;
    }

    uint8_t predictSample(const Rf_sample& sample) const {
        if (nodes.empty()) return 0;
        
        uint16_t currentIndex = 0;  // Start from root
        
        while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
            // Bounds check for feature access
            if (nodes[currentIndex].getFeatureID() >= sample.features.size()) {
                return 0; // Invalid feature access
            }
            
            uint8_t featureValue = sample.features[nodes[currentIndex].getFeatureID()];
            
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
    ChainedUnorderedMap<uint16_t, Rf_sample> allSamples;    // all sample and it's ID 

    Rf_data(){}

    // Load data from CSV format (used only once for initial dataset conversion)
    void loadCSVData(std::string csvFilename, uint8_t numFeatures) {
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

            uint8_t fieldIdx = 0;
            std::stringstream ss(line);
            std::string token;
            
            while (std::getline(ss, token, ',')) {
                // Trim token
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                
                uint8_t v = static_cast<uint8_t>(std::stoi(token));

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

            allSamples[sampleID] = s;
            sampleID++;
            validSamples++;
        }
        
        std::cout << "📋 CSV Processing Results:" << std::endl;
        std::cout << "   Lines processed: " << linesProcessed << std::endl;
        std::cout << "   Empty lines: " << emptyLines << std::endl;
        std::cout << "   Valid samples: " << validSamples << std::endl;
        std::cout << "   Invalid samples: " << invalidSamples << std::endl;
        std::cout << "   Total samples in memory: " << allSamples.size() << std::endl;
        
        allSamples.fit();
        file.close();
        std::cout << "✅ CSV data loaded successfully." << std::endl;
    }
};

typedef enum Rf_training_flags : uint8_t{
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_training_flags;

// Helper functions to convert between flag enum and string representation
std::string flagsToString(uint8_t flags) {
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

uint8_t stringToFlags(const std::string& flag_str) {
    uint8_t flags = 0;
    
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
    uint8_t quantization_coefficient = 2;
    uint8_t num_trees = 20;
    uint8_t num_features;  
    uint8_t num_labels;
    uint8_t k_fold; 
    uint8_t min_split; 
    uint16_t max_depth;
    uint16_t num_samples;  // number of samples in the base data
    size_t RAM_usage = 0;
    int epochs = 20;    // number of epochs for inner training

    float train_ratio = 0.75; // ratio of training data to total data, default is 0.6
    float valid_ratio = 0.2f; // ratio of validation data to total data, default is 0.2
    float boostrap_ratio = 0.632f; // ratio of samples taken from train data to create subdata

    b_vector<uint8_t> max_depth_range;      // for training
    b_vector<uint8_t> min_split_range;      // for training 
    b_vector<bool, SMALL> overwrite{5}; // min_slit-> max_depth-> unity_threshold-> combine_ratio-> training_flag

    Rf_training_flags training_flag;
    std::string data_path;
    std::string config_path = "trained_model/model_config.json"; // path to model configuration file
    
    // model configurations
    float unity_threshold = 0.5f;   // unity_threshold  for classification, effect to precision and recall - default value
    float impurity_threshold = 0.01f; // threshold for impurity, default is 0.01
    float combine_ratio = 0.5f;      // auto-calculated ratio for combining validation with primary evaluation (OOB/CV)

    bool use_gini = false; // use Gini impurity for training
    bool use_validation = false; // use validation set for training
    bool use_bootstrap = true; // use bootstrap sampling for training
    bool cross_validation = false; // use cross validation for evaluation

    // Constructor
    Rf_config() {};

    void init(std::string init_path = "model_config.json"){
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
                num_trees = static_cast<uint8_t>(std::stoi(value));
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
                k_fold = static_cast<uint8_t>(std::stoi(value));
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
        
        // Extract use_validation
        pos = content.find("\"use_validation\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                use_validation = (value == "true");
            }
        }
        
        // Extract cross_validation
        pos = content.find("\"cross_validation\"");
        if (pos != std::string::npos) {
            pos = content.find("\"value\":", pos);
            if (pos != std::string::npos) {
                pos = content.find(":", pos) + 1;
                size_t end = content.find(",", pos);
                if (end == std::string::npos) end = content.find("}", pos);
                std::string value = content.substr(pos, end - pos);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);
                cross_validation = (value == "true");
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
                k_fold = static_cast<uint8_t>(std::stoi(value));
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

        // Initialize overwrite vector: min_split, max_depth, unity_threshold, combine_ratio, train_flag
        overwrite.clear();
        for (int i = 0; i < 5; i++) {
            overwrite.push_back(false);
        }

        // Check and extract min_split
        overwrite[0] = isParameterEnabled("min_split");
        if (overwrite[0]) {
            std::string value = extractParameterValue("min_split");
            if (!value.empty()) {
                min_split = static_cast<uint8_t>(std::stoi(value));
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

        // Check and extract combine_ratio
        overwrite[3] = isParameterEnabled("combine_ratio");
        if (overwrite[3]) {
            std::string value = extractParameterValue("combine_ratio");
            if (!value.empty()) {
                combine_ratio = std::stof(value);
                std::cout << "⚙️  combine_ratio override enabled: " << combine_ratio << std::endl;
            }
        }

        // Check and extract train_flag
        overwrite[4] = isParameterEnabled("train_flag");
        if (overwrite[4]) {
            std::string value = extractParameterValue("train_flag");
            if (!value.empty()) {
                bool isStacked = isParameterStacked("train_flag");
                if (isStacked) {
                    // Stacked mode: combine with automatic flags (will be determined later)
                    uint8_t user_flags = stringToFlags(value);
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

        if(use_validation){
            train_ratio = 0.6f; // default training ratio
            valid_ratio = 0.2f; // default validation ratio
        }else{
            valid_ratio = 0.0f; // no validation ratio
        }

        if(use_validation && cross_validation){
            use_validation = false; // disable validation if cross-validation is used
            std::cout << "⚠️  Cross-validation is enabled, validation will be disabled." << std::endl;
        }
        
        std::cout << "✅ Configuration loaded from " << init_path << std::endl;
        std::cout << "   Number of trees: " << (int)num_trees << std::endl;
        std::cout << "   K-fold: " << (int)k_fold << std::endl;
        std::cout << "   Criterion: " << (use_gini ? "gini" : "entropy") << std::endl;
        std::cout << "   Use bootstrap: " << (use_bootstrap ? "true" : "false") << std::endl;
        std::cout << "   Use validation: " << (use_validation ? "true" : "false") << std::endl;
        std::cout << "   Cross validation: " << (cross_validation ? "true" : "false") << std::endl;
        std::cout << "   Data path: " << data_path << std::endl;
    }


    void saveConfig(size_t ram_usage, std::string config_path = "") const {
        // Save model configuration with forest statistics
        if (config_path.empty()) {
            config_path = "trained_model/esp32_config.json";
        }
        std::ofstream config_file(config_path);
        if(config_file.is_open()) {
            config_file << "{\n";
            config_file << "  \"numTrees\": " << (int)num_trees << ",\n";
            config_file << "  \"numFeatures\": " << (int)num_features << ",\n";
            config_file << "  \"numLabels\": " << (int)num_labels << ",\n";
            config_file << "  \"minSplit\": " << (int)min_split << ",\n";
            config_file << "  \"maxDepth\": " << (int)max_depth << ",\n";
            config_file << "  \"useGini\": " << (use_gini ? "true" : "false") << ",\n";
            config_file << "  \"useValidation\": " << (use_validation ? "true" : "false") << ",\n";
            config_file << "  \"crossValidation\": " << (cross_validation ? "true" : "false") << ",\n";
            config_file << "  \"k_fold\": " << (int)k_fold << ",\n";
            config_file << "  \"unityThreshold\": " << unity_threshold << ",\n";
            config_file << "  \"impurityThreshold\": " << impurity_threshold << ",\n";
            config_file << "  \"combineRatio\": " << combine_ratio << ",\n";
            config_file << "  \"trainRatio\": " << train_ratio << ",\n";
            config_file << "  \"validRatio\": " << valid_ratio << ",\n";
            config_file << "  \"useBootstrap\": " << (use_bootstrap ? "true" : "false") << ",\n";
            config_file << "  \"trainFlag\": \"" << flagsToString(training_flag) << "\",\n";
            config_file << "  \"Estimated RAM (bytes)\": " << ram_usage <<"\n";
            config_file << "}";
            config_file.close();
            std::cout << "✅ Model configuration saved to " << config_path << "\n";
        }else {
            std::cerr << "❌ Failed to open config file for writing: " << config_path << "\n";
        }
    }

    void saveConfigCSV(size_t ram_usage, std::string csv_path = "") const {
        // Save model configuration as CSV
        if (csv_path.empty()) {
            csv_path = "trained_model/esp32_config.csv";
        }
        
        std::ofstream csv_file(csv_path);
        if(csv_file.is_open()) {
            // Header
            csv_file << "Parameter,Value\n";
            
            // Data rows
            csv_file << "numTrees," << (int)num_trees << "\n";
            csv_file << "numFeatures," << (int)num_features << "\n";
            csv_file << "numLabels," << (int)num_labels << "\n";
            csv_file << "minSplit," << (int)min_split << "\n";
            csv_file << "maxDepth," << (int)max_depth << "\n";
            csv_file << "useGini," << (use_gini ? "true" : "false") << "\n";
            csv_file << "useValidation," << (use_validation ? "true" : "false") << "\n";
            csv_file << "crossValidation," << (cross_validation ? "true" : "false") << "\n";
            csv_file << "unityThreshold," << unity_threshold << "\n";
            csv_file << "impurityThreshold," << impurity_threshold << "\n";
            csv_file << "combineRatio," << combine_ratio << "\n";
            csv_file << "trainRatio," << train_ratio << "\n";
            csv_file << "validRatio," << valid_ratio << "\n";
            csv_file << "useBootstrap," << (use_bootstrap ? "true" : "false") << "\n";
            csv_file << "trainFlag," << flagsToString(training_flag) << "\n";
            csv_file << "RAM_usage," << ram_usage << "\n";
            
            csv_file.close();
            std::cout << "✅ Model configuration saved to " << csv_path << " (CSV format)\n";
        } else {
            std::cerr << "❌ Failed to open CSV config file for writing: " << csv_path << "\n";
        }
    }
};


class RandomForest{
public:
    Rf_data a;      // base data / baseFile
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    Rf_config config;

private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<pair<Rf_data, OOB_set>> dataList; // b_vector of pairs: Rf_data and OOB set for each tree

public:
    // RandomForest(){};
    RandomForest(){
        config.init(); // Load configuration from default path
        first_scan(config.data_path);
        a.loadCSVData(config.data_path, config.num_features);

        config.unity_threshold  = 1.25f / static_cast<float>(config.num_labels);
        if(config.num_features == 2) config.unity_threshold = 0.4f;
        
        // Check for unity_threshold override
        if (!config.overwrite[2]) {
            // Apply automatic calculation only if not overridden
            config.unity_threshold  = 1.25f / static_cast<float>(config.num_labels);
            if(config.num_features == 2) config.unity_threshold = 0.4f;
        } else {
            std::cout << "🔧 Using unity_threshold override: " << config.unity_threshold << std::endl;
        }

        if(config.use_validation){
            config.valid_ratio = 0.2f; // default validation ratio
            config.train_ratio = 0.6f; // default training ratio
        }
        
        // OOB.reserve(numTree);
        dataList.reserve(config.num_trees);

        splitData(config.train_ratio);

        ClonesData();
    }
    
    // Enhanced destructor
    ~RandomForest(){
        // Clear forest safely
        std::cout << "🧹 Cleaning files... \n";
        for(auto& tree : root){
            tree.purgeTree();
        }
          
        // Clear data safely
        dataList.clear();
    }



    void MakeForest(){
        root.clear();
        root.reserve(config.num_trees);
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            // For PC training, no SPIFFS filename needed
            Rf_tree tree("");
            build_tree(tree, dataList[i].first, config.min_split, config.max_depth, config.use_gini);
            root.push_back(tree);
        }
    }

    // Get forest statistics
    void printForestStatistics() {
        std::cout << "\n🌳 FOREST STATISTICS:\n";
        std::cout << "----------------------------------------\n";
        
        uint32_t totalNodes = 0;
        uint32_t totalLeafNodes = 0;
        uint16_t maxDepth = 0;
        uint16_t minDepth = UINT16_MAX;
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxDepth) maxDepth = depth;
            if(depth < minDepth) minDepth = depth;
            
            // std::cout << "Tree " << (int)i << ": " 
            //           << nodeCount << " nodes (" 
            //           << leafCount << " leaves), depth " 
            //           << depth << "\n";
        }
        
        std::cout << "----------------------------------------\n";
        std::cout << "Total trees: " << (int)config.num_trees << "\n";
        std::cout << "Total nodes: " << totalNodes << "\n";
        std::cout << "Total leaf nodes: " << totalLeafNodes << "\n";
        std::cout << "Average nodes per tree: " << (float)totalNodes / config.num_trees << "\n";
        std::cout << "Average leaf nodes per tree: " << (float)totalLeafNodes / config.num_trees << "\n";
        std::cout << "Depth range: " << minDepth << " - " << maxDepth << "\n";
        std::cout << "Average depth: " << (float)(maxDepth + minDepth) / 2.0f << "\n";
        std::cout << "----------------------------------------\n";
    }
    private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets (or validation if enabled)
    void splitData(float trainRatio) {
        uint16_t totalSamples = this->a.allSamples.size();
        uint16_t trainSize = static_cast<uint16_t>(totalSamples * trainRatio);
        uint16_t testSize;
        if(this->config.use_validation){
            testSize = static_cast<uint16_t>((totalSamples - trainSize) * 0.5);
        }else{
            testSize = totalSamples - trainSize; // No validation set, use all remaining for testing
        }
        uint16_t validationSize = totalSamples - trainSize - testSize;
        
        // Create vectors to hold all sample IDs for shuffling
        b_vector<uint16_t> allSampleIDs;
        allSampleIDs.reserve(totalSamples);
        for(const auto& sample : this->a.allSamples) {
            allSampleIDs.push_back(sample.first);
        }
        
        // Shuffle the sample IDs for random split using PC random
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, UINT16_MAX);
        for(uint16_t i = totalSamples - 1; i > 0; i--) {
            uint16_t j = dis(gen) % (i + 1);
            uint16_t temp = allSampleIDs[i];
            allSampleIDs[i] = allSampleIDs[j];
            allSampleIDs[j] = temp;
        }
        
        // Clear and reserve space for each dataset
        train_data.allSamples.clear();
        test_data.allSamples.clear();
        if(config.use_validation) validation_data.allSamples.clear();
        
        train_data.allSamples.reserve(trainSize);
        test_data.allSamples.reserve(testSize);
        if(config.use_validation) validation_data.allSamples.reserve(validationSize);

        // Split samples based on shuffled indices
        for(uint16_t i = 0; i < totalSamples; i++) {
            uint16_t sampleID = allSampleIDs[i];
            const Rf_sample& sample = this->a.allSamples[sampleID];
            
            if(i < trainSize) {
                train_data.allSamples[sampleID] = sample;
            } else if(i < trainSize + testSize) {
                test_data.allSamples[sampleID] = sample;
            } else if(config.use_validation && i < trainSize + testSize + validationSize) {
                validation_data.allSamples[sampleID] = sample;
            }
        }
        
        // Fit the containers to optimize memory usage
        train_data.allSamples.fit();
        test_data.allSamples.fit();
        if(config.use_validation) validation_data.allSamples.fit();
    }

    // ---------------------------------------------------------------------------------
    // create dataset for each tree from train set
    void ClonesData() {
        // Clear previous data
        dataList.clear();
        dataList.reserve(config.num_trees);
        uint16_t numSample = train_data.allSamples.size();  
        
        // The target size for a bootstrap sample is the same as the original training set size.
        uint16_t bootstrap_sample_size = config.use_bootstrap ? numSample : static_cast<uint16_t>(numSample * 0.632f);

        // Create a b_vector of all sample IDs for efficient random access
        b_vector<uint16_t> allSampleIds;
        allSampleIds.reserve(numSample);
        for (const auto& sample : train_data.allSamples) {
            allSampleIds.push_back(sample.first);
        }

        // Create a single random number generator for the whole process for efficiency.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, numSample - 1);

        for (uint8_t i = 0; i < config.num_trees; i++) {
            Rf_data sub_data;
            sampleID_set inBagSamples;
            
            // Reserve space for efficiency.
            sub_data.allSamples.reserve(bootstrap_sample_size);
            inBagSamples.reserve(bootstrap_sample_size);

            // Perform standard bootstrap sampling: sample N times WITH replacement.
            // Note: We are adding samples to a map, so we need to use new unique IDs for duplicates.
            for(uint16_t j = 0; j < bootstrap_sample_size; j++){
                uint16_t original_idx = dis(gen);
                uint16_t original_sampleId = allSampleIds[original_idx];     
                
                // The original sample ID is tracked for the OOB set.
                inBagSamples.insert(original_sampleId);
                
                // We add the sample with a new, unique ID (j) to allow for duplicates in the data.
                sub_data.allSamples[j] = train_data.allSamples[original_sampleId];
            }
            sub_data.allSamples.fit();
            
            // Create OOB set with samples not used in this tree's bootstrap sample.
            OOB_set oob_set;
            oob_set.reserve(numSample); // Reserve worst-case
            for (uint16_t id : allSampleIds) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id);
                }
            }
            dataList.push_back(make_pair(sub_data, oob_set)); // Store pair of subset data and OOB set
        }
    }

    // ------------------------------------------------------------------------------
    // Quickly scan through the original dataset to retrieve and set the necessary parameters
    void first_scan(std::string data_path, bool header = false) {
        // For PC training, use standard file I/O instead of SPIFFS
        std::ifstream file(data_path);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open file: " << data_path << "\n";
            return;
        }

        unordered_map<uint8_t, uint16_t> labelCounts;
        unordered_set<uint8_t> featureValues;

        uint16_t numSamples = 0;
        uint16_t maxFeatures = 0;

        // Skip header if specified
        if (header) {
            std::string headerLine;
            std::getline(file, headerLine);
        }

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

        config.num_features = maxFeatures;
        config.num_samples = numSamples;
        config.num_labels = labelCounts.size();

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
            if (!config.overwrite[4]) {
                // Apply automatic selection only if not overridden
                if (maxImbalanceRatio > 10.0f) {
                    config.training_flag = Rf_training_flags::RECALL;
                    std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to RECALL.\n";
                } else if (maxImbalanceRatio > 3.0f) {
                    config.training_flag = Rf_training_flags::F1_SCORE;
                    std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to F1_SCORE.\n";
                } else if (maxImbalanceRatio > 1.5f) {
                    config.training_flag = Rf_training_flags::PRECISION;
                    std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Setting trainFlag to PRECISION.\n";
                } else {
                    config.training_flag = Rf_training_flags::ACCURACY;
                    std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to ACCURACY.\n";
                }
            } else {
                // Check if it's stacked mode or overwrite mode
                bool isStacked = false;
                // Re-check the config file for stacked mode (we need this info here)
                std::ifstream check_file("model_config.json");
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
                    uint8_t user_flags = static_cast<uint8_t>(config.training_flag); // User flags from init()
                    uint8_t auto_flags = 0;
                    
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
                    uint8_t combined_flags = user_flags | auto_flags;
                    config.training_flag = static_cast<Rf_training_flags>(combined_flags);
                    std::cout << "🔗 Stacked train_flags: " << flagsToString(combined_flags) 
                              << " (user: " << flagsToString(user_flags) << " + auto: " << flagsToString(auto_flags) << ")\n";
                } else {
                    // Overwrite mode: user flags completely replace automatic detection
                    std::cout << "🔧 Using train_flag overwrite: " << flagsToString(config.training_flag) << " (dataset ratio: " << maxImbalanceRatio << ")\n";
                }
            }
        }

        // Dataset summary
        std::cout << "📊 Dataset Summary:\n";
        std::cout << "  Total samples: " << numSamples << "\n";
        std::cout << "  Total features: " << maxFeatures << "\n";
        std::cout << "  Unique labels: " << labelCounts.size() << "\n";

        std::cout << "  Label distribution:\n";
        float lowestDistribution = 100.0f;
        for (auto& label : labelCounts) {
            float percent = (float)label.second / numSamples * 100.0f;
            if (percent < lowestDistribution) {
                lowestDistribution = percent;
            }
            std::cout << "    Label " << (int)label.first << ": " << label.second << " samples (" << percent << "%)\n";
        }
        
        // config.lowest_distribution = lowestDistribution / 100.0f; // Store as fraction
        
        // Check if validation should be disabled due to low sample count
        if (lowestDistribution * numSamples * config.valid_ratio < 10) {
            config.use_validation = false;
            std::cout << "⚖️ Setting use_validation to false due to low sample count in validation set.\n";
            config.train_ratio = 0.75f; // Adjust train ratio to compensate
        }

        std::cout << "Feature values: ";
        for (uint8_t val : featureValues) {
            std::cout << (int)val << " ";
        }
        std::cout << "\n";
        
        // Calculate optimal parameters based on dataset size
        int baseline_minsplit_ratio = 100 * (config.num_samples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint8_t min_minSplit = std::min(2, (int)(config.num_samples / baseline_minsplit_ratio));
        int dynamic_max_split = std::min(min_minSplit + 6, (int)(log2(config.num_samples) / 4 + config.num_features / 25.0f));
        uint8_t max_minSplit = std::min(24, dynamic_max_split); // Cap at 24 to prevent overly simple trees.
        if (max_minSplit <= min_minSplit) max_minSplit = min_minSplit + 4; // Ensure a valid range.


        int base_maxDepth = std::max((int)log2(config.num_samples * 2.0f), (int)(log2(config.num_features) * 2.5f));
        uint8_t max_maxDepth = std::max(6, base_maxDepth);
        int dynamic_min_depth = std::max(4, (int)(log2(config.num_features) + 2));
        uint8_t min_maxDepth = std::min((int)max_maxDepth - 2, dynamic_min_depth); // Ensure a valid range.
        if (min_maxDepth >= max_maxDepth) min_maxDepth = max_maxDepth - 2;
        if (min_maxDepth < 4) min_maxDepth = 4;

        // Set default values only if not overridden
        if (!config.overwrite[0]) {
            config.min_split = (min_minSplit + max_minSplit) / 2;
        }
        if (!config.overwrite[1]) {
            config.max_depth = (min_maxDepth + max_maxDepth) / 2;
        }

        std::cout << "min minSplit: " << (int)min_minSplit << ", max minSplit: " << (int)max_minSplit << "\n";
        std::cout << "min maxDepth: " << (int)min_maxDepth << ", max maxDepth: " << (int)max_maxDepth << "\n";
        
        // Build ranges based on override status
        config.min_split_range.clear();
        config.max_depth_range.clear();
        
        if (config.overwrite[0]) {
            // min_split override is enabled - use only the override value
            config.min_split_range.push_back(config.min_split);
            std::cout << "🔧 min_split override active: using fixed value " << (int)config.min_split << "\n";
        } else {
            // min_split automatic - build range with step 2
            uint8_t min_split_step = 2;
            if(config.overwrite[1]) {
                min_split_step = 1; // If max_depth is overridden, use smaller step for min_split
            }
            for(uint8_t i = min_minSplit; i <= max_minSplit; i += min_split_step) {
                config.min_split_range.push_back(i);
            }
        }
        
        if (config.overwrite[1]) {
            // max_depth override is enabled - use only the override value
            config.max_depth_range.push_back(config.max_depth);
            std::cout << "🔧 max_depth override active: using fixed value " << (int)config.max_depth << "\n";
        } else {
            // max_depth automatic - build range with step 2
            uint8_t max_depth_step = 2;
            if(config.overwrite[0]) {
                max_depth_step = 1; // If min_split is overridden, use smaller step for max_depth
            }
            for(uint8_t i = min_maxDepth; i <= max_maxDepth; i += max_depth_step) {
                config.max_depth_range.push_back(i);
            }
        }
        
        // Ensure at least one value in each range (fallback safety)
        if (config.min_split_range.empty()) {
            config.min_split_range.push_back(config.min_split);
        }
        if (config.max_depth_range.empty()) {
            config.max_depth_range.push_back(config.max_depth);
        }

        std::cout << "Setting minSplit to " << (int)config.min_split
                  << " and maxDepth to " << (int)config.max_depth << " based on dataset size.\n";
        
        // Debug: Show range sizes
        std::cout << "📊 Training ranges: min_split_range has " << config.min_split_range.size() 
                  << " values, max_depth_range has " << config.max_depth_range.size() << " values\n";
        std::cout << "   min_split values: ";
        for(size_t i = 0; i < config.min_split_range.size(); i++) {
            std::cout << (int)config.min_split_range[i];
            if(i < config.min_split_range.size() - 1) std::cout << ", ";
        }
        std::cout << "\n   max_depth values: ";
        for(size_t i = 0; i < config.max_depth_range.size(); i++) {
            std::cout << (int)config.max_depth_range[i];
            if(i < config.max_depth_range.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
        
        // Calculate optimal combine_ratio based on dataset characteristics
        float validation_reliability = 1.0f;
        if(config.use_validation) {
            // Lower combine ratio when validation set is small or data is limited
            float validation_samples = config.num_samples * config.valid_ratio;
            validation_reliability = std::min(1.0f, validation_samples / 100.0f); // Normalize by 100 samples
        }

        float dataset_factor = std::min(1.0f, (float)config.num_samples / 1000.0f);
        float feature_factor = std::min(1.0f, (float)config.num_features / 50.0f);
        float balance_factor = std::min(1.0f, lowestDistribution / 20.0f); // 20% as good balance threshold
        
        // Calculate adaptive combine_ratio
        if (!config.overwrite[3]) {
            // Apply automatic calculation only if not overridden
            config.combine_ratio = 0.3f + 0.4f * validation_reliability * dataset_factor * feature_factor * balance_factor;
            config.combine_ratio = std::max(0.2f, std::min(0.8f, config.combine_ratio)); // Clamp between 0.2 and 0.8
            
            std::cout << "Auto-calculated combine_ratio: " << config.combine_ratio 
                      << " (validation_weight=" << config.combine_ratio 
                      << ", primary_weight=" << (1.0f - config.combine_ratio) << ")\n";
        } else {
            std::cout << "🔧 Using combine_ratio override: " << config.combine_ratio << std::endl;
        }
        
        file.close();
    }


    typedef struct SplitInfo {
        float gain = -1.0f;
        uint16_t featureID = 0;
        uint8_t threshold = 0;
    } SplitInfo;

    // OPTIMIZED: Finds the best feature and threshold to split on in a more efficient manner.
    SplitInfo findBestSplit(Rf_data& data, const unordered_set<uint16_t>& selectedFeatures, bool use_Gini) {
        SplitInfo bestSplit;
        uint32_t totalSamples = data.allSamples.size();
        if (totalSamples < 2) return bestSplit; // Cannot split less than 2 samples

        // Use vector instead of VLA for safety and standard compliance.
        vector<uint16_t> baseLabelCounts(config.num_labels, 0);
        for (const auto& entry : data.allSamples) {
            // Bounds check to prevent memory corruption
            if (entry.second.label < config.num_labels) {
                baseLabelCounts[entry.second.label]++;
            }
        }

        float baseImpurity;
        if (use_Gini) {
            baseImpurity = 1.0f;
            for (uint8_t i = 0; i < config.num_labels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * p;
                }
            }
        } else { // Entropy
            baseImpurity = 0.0f;
            for (uint8_t i = 0; i < config.num_labels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        // Iterate through the randomly selected features
        for (const auto& featureID : selectedFeatures) {
            // Use a flat vector for the contingency table to avoid non-standard 2D VLAs.
            vector<uint16_t> counts(4 * config.num_labels, 0);
            uint32_t value_totals[4] = {0};

            for (const auto& entry : data.allSamples) {
                const Rf_sample& sample = entry.second;
                uint8_t feature_val = sample.features[featureID];
                // Bounds check for both feature value and label
                if (feature_val < 4 && sample.label < config.num_labels) {
                    counts[feature_val * config.num_labels + sample.label]++;
                    value_totals[feature_val]++;
                }
            }

            // Test all possible binary splits (thresholds 0, 1, 2)
            for (uint8_t threshold = 0; threshold <= 2; threshold++) {
                // Use vector for safety.
                vector<uint16_t> left_counts(config.num_labels, 0);
                vector<uint16_t> right_counts(config.num_labels, 0);
                uint32_t left_total = 0;
                uint32_t right_total = 0;

                // Aggregate counts for left/right sides from the contingency table
                for (uint8_t val = 0; val < 4; val++) {
                    if (val <= threshold) {
                        for (uint8_t label = 0; label < config.num_labels; label++) {
                            left_counts[label] += counts[val * config.num_labels + label];
                        }
                        left_total += value_totals[val];
                    } else {
                        for (uint8_t label = 0; label < config.num_labels; label++) {
                            right_counts[label] += counts[val * config.num_labels + label];
                        }
                        right_total += value_totals[val];
                    }
                }

                if (left_total == 0 || right_total == 0) continue;

                // Calculate impurity for left and right splits
                float leftImpurity, rightImpurity;
                if (use_Gini) {
                    leftImpurity = 1.0f;
                    rightImpurity = 1.0f;
                    for (uint8_t i = 0; i < config.num_labels; i++) {
                        if (left_counts[i] > 0) {
                            float p = static_cast<float>(left_counts[i]) / left_total;
                            leftImpurity -= p * p;
                        }
                        if (right_counts[i] > 0) {
                            float p = static_cast<float>(right_counts[i]) / right_total;
                            rightImpurity -= p * p;
                        }
                    }
                } else { // Entropy
                    leftImpurity = 0.0f;
                    rightImpurity = 0.0f;
                    for (uint8_t i = 0; i < config.num_labels; i++) {
                        if (left_counts[i] > 0) {
                            float p = static_cast<float>(left_counts[i]) / left_total;
                            leftImpurity -= p * log2f(p);
                        }
                        if (right_counts[i] > 0) {
                            float p = static_cast<float>(right_counts[i]) / right_total;
                            rightImpurity -= p * log2f(p);
                        }
                    }
                }

                float weightedImpurity = (static_cast<float>(left_total) / totalSamples * leftImpurity) +
                                         (static_cast<float>(right_total) / totalSamples * rightImpurity);
                
                float gain = baseImpurity - weightedImpurity;

                if (gain > bestSplit.gain) {
                    bestSplit.gain = gain;
                    bestSplit.featureID = featureID;
                    bestSplit.threshold = threshold;
                }
            }
        }
        return bestSplit;
    }
    
    // Breadth-first tree building for optimal node layout
    void build_tree(Rf_tree& tree, Rf_data& data, uint8_t min_split, uint16_t max_depth, bool use_Gini) {
        tree.nodes.clear();
        if (data.allSamples.empty()) return;
        
        // Structure to hold node building information
        struct NodeToBuild {
            uint16_t nodeIndex;
            Rf_data nodeData;
            uint16_t depth;
            
            NodeToBuild() : nodeIndex(0), depth(0) {}  // Default constructor
            NodeToBuild(uint16_t idx, Rf_data data, uint16_t d) 
                : nodeIndex(idx), nodeData(std::move(data)), depth(d) {}
        };
        
        // Queue for breadth-first processing
        b_vector<NodeToBuild> queue;
        queue.reserve(2048); // Max possible nodes (11-bit index = 2047 max)
        
        // Create root node
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);
        queue.push_back(NodeToBuild(0, data, 0));
        
        // Process nodes breadth-first
        while (!queue.empty()) {
            NodeToBuild current = queue.front();
            queue.erase(0); // Remove first element
            
            // Check stopping conditions for this node
            unordered_set<uint8_t> labels;
            for (const auto& [key, val] : current.nodeData.allSamples) {
                labels.insert(val.label);
            }
            
            bool shouldBeLeaf = false;
            uint8_t leafLabel = 0;
            
            // Determine if this should be a leaf
            if (labels.size() == 1) {
                shouldBeLeaf = true;
                leafLabel = *labels.begin();
            } else if (current.nodeData.allSamples.size() < min_split || current.depth >= max_depth) {
                shouldBeLeaf = true;
                // Find majority label
                uint16_t labelCounts[config.num_labels] = {0};
                for (const auto& entry : current.nodeData.allSamples) {
                    if (entry.second.label < config.num_labels) {
                        labelCounts[entry.second.label]++;
                    }
                }
                uint16_t maxCount = 0;
                for (uint8_t i = 0; i < config.num_labels; i++) {
                    if (labelCounts[i] > maxCount) {
                        maxCount = labelCounts[i];
                        leafLabel = i;
                    }
                }
            }
            
            if (shouldBeLeaf) {
                // Configure as leaf node
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue; // Skip to next node
            }
            
            // Find best split for internal node
            uint8_t num_selected_features = static_cast<uint8_t>(sqrt(config.num_features));
            if (num_selected_features == 0) num_selected_features = 1;

            unordered_set<uint16_t> selectedFeatures;
            selectedFeatures.reserve(num_selected_features);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint16_t> feature_dis(0, config.num_features - 1);
            while (selectedFeatures.size() < num_selected_features) {
                uint16_t idx = feature_dis(gen);
                selectedFeatures.insert(idx);
            }
            
            SplitInfo bestSplit = findBestSplit(current.nodeData, selectedFeatures, use_Gini);
            float gain_threshold = use_Gini ? config.impurity_threshold/2 : config.impurity_threshold;
            
            if (bestSplit.gain <= gain_threshold) {
                // Make it a leaf with majority label
                uint16_t labelCounts[config.num_labels] = {0};
                for (const auto& entry : current.nodeData.allSamples) {
                    if (entry.second.label < config.num_labels) {
                        labelCounts[entry.second.label]++;
                    }
                }
                uint16_t maxCount = 0;
                uint8_t leafLabel = 0;
                for (uint8_t i = 0; i < config.num_labels; i++) {
                    if (labelCounts[i] > maxCount) {
                        maxCount = labelCounts[i];
                        leafLabel = i;
                    }
                }
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            // Configure as internal node
            tree.nodes[current.nodeIndex].setFeatureID(bestSplit.featureID);
            tree.nodes[current.nodeIndex].setThreshold(bestSplit.threshold);
            tree.nodes[current.nodeIndex].setIsLeaf(false);
            
            // Split data for children
            Rf_data leftData, rightData;
            for (const auto& sample : current.nodeData.allSamples) {
                if (sample.second.features[bestSplit.featureID] <= bestSplit.threshold) {
                    leftData.allSamples[sample.first] = sample.second;
                } else {
                    rightData.allSamples[sample.first] = sample.second;
                }
            }
            
            // Create child nodes (breadth-first: left child, then right child)
            uint16_t leftChildIndex = tree.nodes.size();
            uint16_t rightChildIndex = leftChildIndex + 1;
            
            // Set left child index in parent (right child index is automatically left + 1)
            tree.nodes[current.nodeIndex].setLeftChildIndex(leftChildIndex);
            
            // Add left child node
            Tree_node leftChild;
            tree.nodes.push_back(leftChild);
            
            // Add right child node  
            Tree_node rightChild;
            tree.nodes.push_back(rightChild);
            
            // Queue children for processing (maintain breadth-first order)
            if (!leftData.allSamples.empty()) {
                queue.push_back(NodeToBuild(leftChildIndex, std::move(leftData), current.depth + 1));
            } else {
                // Empty left child becomes a leaf with majority label
                tree.nodes[leftChildIndex].setIsLeaf(true);
                tree.nodes[leftChildIndex].setLabel(leafLabel);
                tree.nodes[leftChildIndex].setFeatureID(0);
            }
            
            if (!rightData.allSamples.empty()) {
                queue.push_back(NodeToBuild(rightChildIndex, std::move(rightData), current.depth + 1));
            } else {
                // Empty right child becomes a leaf with majority label  
                tree.nodes[rightChildIndex].setIsLeaf(true);
                tree.nodes[rightChildIndex].setLabel(leafLabel);
                tree.nodes[rightChildIndex].setFeatureID(0);
            }
        }
    }

    // 
    uint8_t predClassSample(Rf_sample& s){
        int16_t totalPredict = 0;
        unordered_map<uint8_t, uint8_t> predictClass;
        
        // Use streaming prediction 
        for(auto& tree : root){

            uint8_t predict = tree.predictSample(s); // Uses streaming if not loaded
            if(predict < config.num_labels){
                predictClass[predict]++;
                totalPredict++;
            }
        }
        
        if(predictClass.size() == 0 || totalPredict == 0) {
            return 255;
        }
        
        // Find most predicted class
        int16_t max = -1;
        uint8_t mostPredict = 255;
        
        for(const auto& predict : predictClass){
            if(predict.second > max){
                max = predict.second;
                mostPredict = predict.first;
            }
        }
        
        // Check certainty threshold
        float certainty = static_cast<float>(max) / totalPredict;
        if(certainty < config.unity_threshold) {
            return 255;
        }
        
        return mostPredict;
    }

    // Enhanced OOB evaluation with better statistics and reliability checks
    pair<float,float> get_training_evaluation_index(Rf_data& validation_data){
        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[config.num_labels] = {0};
        uint16_t oob_fp[config.num_labels] = {0};
        uint16_t oob_fn[config.num_labels] = {0};

        uint16_t valid_tp[config.num_labels] = {0};
        uint16_t valid_fp[config.num_labels] = {0};
        uint16_t valid_fn[config.num_labels] = {0};

        uint16_t oob_correct = 0, oob_total = 0, oob_skipped = 0;
        uint16_t valid_correct = 0, valid_total = 0;
        
        // Track OOB coverage statistics
        uint16_t oob_votes_histogram[21] = {0}; // 0-20+ votes
        uint16_t min_votes_required = std::max(1, (int)(config.num_trees * 0.15f)); // At least 15% of trees

        // OOB evaluation with enhanced reliability
        for(const auto& sample : train_data.allSamples){                
            uint16_t sampleId = sample.first;
        
            // Find all trees whose OOB set contains this sampleId
            b_vector<uint8_t, SMALL> activeTrees;
            activeTrees.reserve(config.num_trees);
            
            for(uint8_t i = 0; i < config.num_trees; i++){
                if(dataList[i].second.find(sampleId) != dataList[i].second.end()){
                    activeTrees.push_back(i);
                }
            }
            
            // Enhanced reliability check: require minimum vote count
            if(activeTrees.size() < min_votes_required){
                oob_skipped++;
                continue; // Skip samples with too few OOB trees
            }
            
            // Track vote distribution for analysis
            uint16_t vote_bucket = std::min(20, (int)activeTrees.size());
            oob_votes_histogram[vote_bucket]++;
            
            // Predict using only the OOB trees for this sample
            uint8_t actualLabel = sample.second.label;
            unordered_map<uint8_t, uint8_t> oobPredictClass;
            uint16_t oobTotalPredict = 0;
            
            for(const uint8_t& treeIdx : activeTrees){
                uint8_t predict = root[treeIdx].predictSample(sample.second);
                if(predict < config.num_labels){
                    oobPredictClass[predict]++;
                    oobTotalPredict++;
                }
            }
            
            if(oobTotalPredict == 0) continue;
            
            // Find the most predicted class from OOB trees
            uint8_t oobPredictedLabel = 255;
            uint16_t maxVotes = 0;
            for(const auto& predict : oobPredictClass){
                if(predict.second > maxVotes){
                    maxVotes = predict.second;
                    oobPredictedLabel = predict.first;
                }
            }
            
            // Adaptive certainty threshold based on available votes
            float base_threshold = config.unity_threshold;
            float adaptive_threshold = base_threshold * (1.0f - 0.3f * (float)activeTrees.size() / config.num_trees);
            adaptive_threshold = std::max(0.3f, adaptive_threshold); // Minimum threshold
            
            float certainty = static_cast<float>(maxVotes) / oobTotalPredict;
            if(certainty < adaptive_threshold) {
                oob_skipped++;
                continue; // Skip uncertain predictions
            }
            
            // Update confusion matrix
            oob_total++;
            if(oobPredictedLabel == actualLabel){
                oob_correct++;
                oob_tp[actualLabel]++;
            } else {
                oob_fn[actualLabel]++;
                if(oobPredictedLabel < config.num_labels){
                    oob_fp[oobPredictedLabel]++;
                }
            }
        }

        // Validation evaluation (unchanged)
        if(this->config.use_validation){   
            for(const auto& sample : validation_data.allSamples){
                uint16_t sampleId = sample.first;
                uint8_t actualLabel = sample.second.label;

                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                for(uint8_t i = 0; i < config.num_trees; i++){
                    uint8_t predict = root[i].predictSample(sample.second);
                    if(predict < config.num_labels){
                        validPredictClass[predict]++;
                        validTotalPredict++;
                    }
                }

                if(validTotalPredict == 0) continue;

                uint8_t validPredictedLabel = 255;
                uint16_t maxVotes = 0;
                for(const auto& predict : validPredictClass){
                    if(predict.second > maxVotes){
                        maxVotes = predict.second;
                        validPredictedLabel = predict.first;
                    }
                }

                float certainty = static_cast<float>(maxVotes) / validTotalPredict;
                if(certainty < config.unity_threshold) {
                    continue;
                }

                valid_total++;
                if(validPredictedLabel == actualLabel){
                    valid_correct++;
                    valid_tp[actualLabel]++;
                } else {
                    valid_fn[actualLabel]++;
                    if(validPredictedLabel < config.num_labels){
                        valid_fp[validPredictedLabel]++;
                    }
                }
            }
        }

        // Calculate the requested metric with the same logic as before
        float oob_result = 0.0f;
        float valid_result = 0.0f;
        float combined_oob_result = 0.0f;
        float combined_valid_result = 0.0f;
        uint8_t training_flag = static_cast<uint8_t>(config.training_flag);
        uint8_t numFlags = 0;
        
        if(oob_total == 0){
            return make_pair(0.0f, 0.0f);
        }
        
        if(training_flag & ACCURACY){
            oob_result = static_cast<float>(oob_correct) / oob_total;
            valid_result = (valid_total > 0) ? static_cast<float>(valid_correct) / valid_total : 0.0f;
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & PRECISION){
            float oob_totalPrecision = 0.0f, valid_totalPrecision = 0.0f;
            uint8_t oob_validLabels = 0, valid_validLabels = 0;
            for(uint8_t label = 0; label < config.num_labels; label++){
                uint16_t otp = oob_tp[label];
                uint16_t ofp = oob_fp[label];
                uint16_t vtp = valid_tp[label];
                uint16_t vfp = valid_fp[label];
                if(otp + ofp > 0){
                    oob_totalPrecision += static_cast<float>(otp) / (otp + ofp);
                    oob_validLabels++;
                }
                if(vtp + vfp > 0){
                    valid_totalPrecision += static_cast<float>(vtp) / (vtp + vfp);
                    valid_validLabels++;
                }
            }
            oob_result = oob_validLabels > 0 ? oob_totalPrecision / oob_validLabels : 0.0f;
            valid_result = valid_validLabels > 0 ? valid_totalPrecision / valid_validLabels : 0.0f;
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & RECALL){
            float oob_totalRecall = 0.0f, valid_totalRecall = 0.0f;
            uint8_t oob_validLabels = 0, valid_validLabels = 0;
            for(uint8_t label = 0; label < config.num_labels; label++){
                uint16_t otp = oob_tp[label];
                uint16_t ofn = oob_fn[label];
                uint16_t vtp = valid_tp[label];
                uint16_t vfn = valid_fn[label];
                
                if(otp + ofn > 0){
                    oob_totalRecall += static_cast<float>(otp) / (otp + ofn);
                    oob_validLabels++;
                }
                if(vtp + vfn > 0){
                    valid_totalRecall += static_cast<float>(vtp) / (vtp + vfn);
                    valid_validLabels++;
                }
            }
            valid_result = valid_validLabels > 0 ? valid_totalRecall / valid_validLabels : 0.0f;
            oob_result = oob_validLabels > 0 ? oob_totalRecall / oob_validLabels : 0.0f;
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & F1_SCORE) {
            float oob_totalF1 = 0.0f, valid_totalF1 = 0.0f;
            uint8_t oob_validLabels = 0, valid_validLabels = 0;
            for(uint8_t label = 0; label < config.num_labels; label++){
                uint16_t otp = oob_tp[label];
                uint16_t ofp = oob_fp[label];
                uint16_t ofn = oob_fn[label];

                uint16_t vtp = valid_tp[label];
                uint16_t vfp = valid_fp[label];
                uint16_t vfn = valid_fn[label];

                if(otp + ofp > 0 && otp + ofn > 0){
                    float precision = static_cast<float>(otp) / (otp + ofp);
                    float recall = static_cast<float>(otp) / (otp + ofn);
                    if(precision + recall > 0){
                        float f1 = 2.0f * precision * recall / (precision + recall);
                        oob_totalF1 += f1;
                        oob_validLabels++;
                    }
                }
                if(vtp + vfp > 0 && vtp + vfn > 0){
                    float precision = static_cast<float>(vtp) / (vtp + vfp);
                    float recall = static_cast<float>(vtp) / (vtp + vfn);
                    if(precision + recall > 0){
                        float f1 = 2.0f * precision * recall / (precision + recall);
                        valid_totalF1 += f1;
                        valid_validLabels++;
                    }
                }
            }
            oob_result = oob_validLabels > 0 ? oob_totalF1 / oob_validLabels : 0.0f;
            valid_result = valid_validLabels > 0 ? valid_totalF1 / valid_validLabels : 0.0f;
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        return make_pair(
            (numFlags > 0) ? combined_oob_result / numFlags : 0.0f, 
            (numFlags > 0) ? combined_valid_result / numFlags : 0.0f
        );
    }

    // K-fold cross validation evaluation
    float get_cross_validation_score() {
        uint8_t k_folds = config.k_fold;
        if (k_folds < 2) k_folds = 4; // Default to 4-fold if not properly set
        
        // Create vector of all training sample IDs for k-fold split
        b_vector<uint16_t> allTrainIDs;
        allTrainIDs.reserve(train_data.allSamples.size());
        for (const auto& sample : train_data.allSamples) {
            allTrainIDs.push_back(sample.first);
        }
        
        // Shuffle the sample IDs for random k-fold split
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, UINT16_MAX);
        for (uint16_t i = allTrainIDs.size() - 1; i > 0; i--) {
            uint16_t j = dis(gen) % (i + 1);
            uint16_t temp = allTrainIDs[i];
            allTrainIDs[i] = allTrainIDs[j];
            allTrainIDs[j] = temp;
        }
        
        uint16_t fold_size = allTrainIDs.size() / k_folds;
        float total_cv_score = 0.0f;
        uint8_t valid_folds = 0;
        
        // Perform k-fold cross validation
        for (uint8_t fold = 0; fold < k_folds; fold++) {
            // Create train and test sets for this fold
            Rf_data cv_train_data, cv_test_data;
            
            uint16_t test_start = fold * fold_size;
            uint16_t test_end = (fold == k_folds - 1) ? allTrainIDs.size() : (fold + 1) * fold_size;
            
            // Split data into train and test for this fold
            for (uint16_t i = 0; i < allTrainIDs.size(); i++) {
                uint16_t sampleID = allTrainIDs[i];
                const Rf_sample& sample = train_data.allSamples[sampleID];
                
                if (i >= test_start && i < test_end) {
                    cv_test_data.allSamples[sampleID] = sample;
                } else {
                    cv_train_data.allSamples[sampleID] = sample;
                }
            }
            
            if (cv_train_data.allSamples.empty() || cv_test_data.allSamples.empty()) {
                continue; // Skip this fold if empty
            }
            
            // Temporarily store original dataList and rebuild for this fold
            auto original_dataList = dataList;
            dataList.clear();
            
            // Create bootstrap samples from cv_train_data
            uint16_t numSample = cv_train_data.allSamples.size();
            uint16_t bootstrap_sample_size = config.use_bootstrap ? numSample : static_cast<uint16_t>(numSample * 0.632f);
            
            b_vector<uint16_t> cvTrainIDs;
            cvTrainIDs.reserve(numSample);
            for (const auto& sample : cv_train_data.allSamples) {
                cvTrainIDs.push_back(sample.first);
            }
            
            std::uniform_int_distribution<uint16_t> cv_dis(0, numSample - 1);
            for (uint8_t tree_idx = 0; tree_idx < config.num_trees; tree_idx++) {
                Rf_data sub_data;
                sub_data.allSamples.reserve(bootstrap_sample_size);
                
                for (uint16_t j = 0; j < bootstrap_sample_size; j++) {
                    uint16_t original_idx = cv_dis(gen);
                    uint16_t original_sampleId = cvTrainIDs[original_idx];
                    sub_data.allSamples[j] = cv_train_data.allSamples[original_sampleId];
                }
                sub_data.allSamples.fit();
                
                // Create empty OOB set for cross validation (not used)
                OOB_set empty_oob;
                dataList.push_back(make_pair(sub_data, empty_oob));
            }
            
            // Build forest for this fold
            rebuildForest();
            
            // Evaluate on cv_test_data
            float fold_score = predict(cv_test_data, static_cast<Rf_training_flags>(config.training_flag));
            total_cv_score += fold_score;
            valid_folds++;
            
            // Restore original dataList
            dataList = original_dataList;
        }
        
        return (valid_folds > 0) ? (total_cv_score / valid_folds) : 0.0f;
    }
        
    void rebuildForest() {
        // Clear existing trees properly
        for (uint8_t i = 0; i < root.size(); i++) {
            root[i].purgeTree(); // Clear the vector-based tree
        }
        for(uint8_t i = 0; i < config.num_trees; i++){
            // Build new tree using vector approach
            Rf_tree& tree = root[i];
            tree.purgeTree(); // Ensure complete cleanup
            build_tree(tree, dataList[i].first, config.min_split, config.max_depth, config.use_gini);
            
            // Verify tree was built successfully
            if (tree.nodes.empty()) {
                std::cout << "❌ Failed to build tree " << (int)i << "\n";
                continue;
            }
        }
        
        // Print forest statistics after rebuilding
        // printForestStatistics();
    }
    
  public:
    // -----------------------------------------------------------------------------------
    // Grid Search Training with Multiple Runs
    // -----------------------------------------------------------------------------------
    // Enhanced training with adaptive evaluation strategy
    void fit(){
        std::cout << "\n🚀 Training Random Forest...\n";

        uint8_t best_min_split = config.min_split;
        uint16_t best_max_depth = config.max_depth;
        float best_score = -1.0f;

        // Determine evaluation mode and number of runs
        bool use_cv = config.cross_validation;
        const int num_runs = use_cv ? 1 : 3; // 1 run for CV, 3 runs for OOB/validation
        
        if (use_cv) {
            std::cout << "📊 Using " << (int)config.k_fold << "-fold cross validation for evaluation\n";
        } else {
            std::cout << "📊 Using OOB ";
            if (config.use_validation) {
                std::cout << "and validation data for evaluation\n";
            } else {
                std::cout << "for evaluation\n";
            }
        }

        // Calculate total iterations for progress bar
        uint32_t total_iterations = config.min_split_range.size() * config.max_depth_range.size() * num_runs;
        uint32_t current_iteration = 0;

        // Grid search over min_split and max_depth ranges
        for (uint8_t current_min_split : config.min_split_range) {
            for (uint16_t current_max_depth : config.max_depth_range) {
                config.min_split = current_min_split;
                config.max_depth = current_max_depth;

                float total_run_score = 0.0f;

                for (int i = 0; i < num_runs; ++i) {
                    float combined_score = 0.0f;
                    
                    if (use_cv) {
                        // Cross validation mode
                        combined_score = get_cross_validation_score();
                    } else {
                        // OOB and validation mode
                        ClonesData();
                        rebuildForest();

                        pair<float, float> scores = get_training_evaluation_index(validation_data);
                        float oob_score = scores.first;
                        float validation_score = scores.second;

                        // Combine OOB and validation scores using the adaptive ratio
                        combined_score = (1.0f - config.combine_ratio) * oob_score + config.combine_ratio * validation_score;
                    }
                    
                    total_run_score += combined_score;
                    
                    // Update progress bar
                    current_iteration++;
                    float progress = (float)current_iteration / total_iterations;
                    int bar_width = 50;
                    int pos = bar_width * progress;
                    
                    std::cout << "\r[";
                    for (int j = 0; j < bar_width; ++j) {
                        if (j < pos) std::cout << "█";
                        else if (j == pos) std::cout << "▓";
                        else std::cout << "░";
                    }
                    std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% ";
                    std::cout << "(" << current_iteration << "/" << total_iterations << ") ";
                    std::cout << "Score: " << std::setprecision(3) << combined_score;
                    std::cout.flush();
                }

                float avg_score = total_run_score / num_runs;

                if (avg_score > best_score) {
                    best_score = avg_score;
                    best_min_split = config.min_split;
                    best_max_depth = config.max_depth;
                }
            }
        }

        std::cout << "\n✅ Training Complete! Best: min_split=" << (int)best_min_split 
                  << ", max_depth=" << (int)best_max_depth << ", score=" << best_score << "\n";

        // Set the best parameters and rebuild the final forest
        config.min_split = best_min_split;
        config.max_depth = best_max_depth;

        std::cout << "🔨 Building final forest...\n";
        ClonesData(); // Use fresh bootstrap samples for the final model
        rebuildForest();
    }

public:
    // Save the trained forest to files
    void saveForest(const std::string& folder_path = "trained_model") {
        std::cout << "💾 Saving trained forest to " << folder_path << "...\n";
        
        // Create directory if it doesn't exist
        #ifdef _WIN32
            _mkdir(folder_path.c_str());
        #else
            mkdir(folder_path.c_str(), 0755);
        #endif
        size_t ram_usage ;
        // Calculate forest statistics BEFORE saving (which purges the trees)
        uint32_t totalNodes = 0;
        uint32_t totalLeafNodes = 0;
        uint16_t maxTreeDepth = 0;
        uint16_t minTreeDepth = UINT16_MAX;
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxTreeDepth) maxTreeDepth = depth;
            if(depth < minTreeDepth) minTreeDepth = depth;
        }
        ram_usage = totalNodes * sizeof(Tree_node);
        
        // Save individual tree files
        for(uint8_t i = 0; i < config.num_trees; i++){
            std::string filename = "tree_" + std::to_string(i) + ".bin";
            root[i].filename = filename;
            root[i].saveTree(folder_path);
        }
        
        // Save config in both JSON and CSV formats
        config.saveConfig(ram_usage);
        config.saveConfigCSV(ram_usage);
    }
    
    // Load the best trained forest from files (trees only, ignores config file)
    void loadForest(const std::string& folder_path = "trained_model") {
        std::cout << "📂 Loading trained forest from " << folder_path << "...\n";
        
        uint8_t loaded_trees = 0;
        
        // Load individual tree files
        for(uint8_t i = 0; i < config.num_trees; i++){
            std::string tree_filename = folder_path + "/tree_" + std::to_string(i) + ".bin";
            
            // Check if file exists
            std::ifstream check_file(tree_filename);
            if (!check_file.is_open()) {
                std::cout << "⚠️  Tree file not found: " << tree_filename << "\n";
                continue;
            }
            check_file.close();
            
            // Load the tree
            root[i].filename = "tree_" + std::to_string(i) + ".bin";
            root[i].loadTree(tree_filename);
            
            // Verify tree was loaded successfully
            if (!root[i].nodes.empty()) {
                loaded_trees++;
            } else {
                std::cout << "❌ Failed to load tree " << (int)i << " from " << tree_filename << "\n";
            }
        }
        
        if (loaded_trees == config.num_trees) {
            std::cout << "✅ Forest loaded successfully! (" << (int)loaded_trees << "/" << (int)config.num_trees << " trees)\n";
        } else if (loaded_trees > 0) {
            std::cout << "⚠️  Partial forest loaded: " << (int)loaded_trees << "/" << (int)config.num_trees << " trees\n";
        } else {
            std::cout << "❌ Failed to load any trees from " << folder_path << "\n";
        }
    }
    
    // combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        // Counters for each label
        unordered_map<uint8_t, uint32_t> tp, fp, fn, totalPred, correctPred;
        
        // Initialize counters for all actual labels
        for (uint8_t label=0; label < config.num_labels; label++) {
            tp[label] = 0;
            fp[label] = 0; 
            fn[label] = 0;
            totalPred[label] = 0;
            correctPred[label] = 0;
        }
        
        // Single pass over samples
        for (const auto& kv : data.allSamples) {
            uint8_t actual = kv.second.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(kv.second));
            
            totalPred[actual]++;
            
            if (pred == actual) {
                tp[actual]++;
                correctPred[actual]++;
            } else {
                if (pred < config.num_labels && pred >=0) {
                    fp[pred]++;
                }
                fn[actual]++;
            }
        }
        
        // Build metric vectors using ONLY actual labels
        b_vector<pair<uint8_t, float>> precisions, recalls, f1s, accuracies;
        
        for (uint8_t label = 0; label < config.num_labels; label++) {
            uint32_t tpv = tp[label], fpv = fp[label], fnv = fn[label];
            
            float prec = (tpv + fpv == 0) ? 0.0f : float(tpv) / (tpv + fpv);
            float rec  = (tpv + fnv == 0) ? 0.0f : float(tpv) / (tpv + fnv);
            float f1   = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
            float acc  = (totalPred[label] == 0) ? 0.0f : float(correctPred[label]) / totalPred[label];
            
            precisions.push_back(make_pair(label, prec));
            recalls.push_back(make_pair(label, rec));
            f1s.push_back(make_pair(label, f1));
            accuracies.push_back(make_pair(label, acc));
        }
        
        b_vector<b_vector<pair<uint8_t, float>>> result;
        result.push_back(precisions);  // 0: precisions
        result.push_back(recalls);     // 1: recalls
        result.push_back(f1s);         // 2: F1 scores
        result.push_back(accuracies);  // 3: accuracies

        return result;
    }

    // get prediction score based on training flags
    float predict(Rf_data& data, Rf_training_flags flags) {
        auto metrics = predict(data);

        float combined_score = 0.0f;
        uint8_t num_flags = 0;

        // Helper: average a vector of (label, value) pairs
        auto avg_metric = [](const b_vector<pair<uint8_t, float>>& vec) -> float {
            float sum = 0.0f;
            for (const auto& p : vec) sum += p.second;
            return vec.size() ? sum / vec.size() : 0.0f;
        };

        if (flags & ACCURACY) {
            combined_score += avg_metric(metrics[3]);
            num_flags++;
        }
        if (flags & PRECISION) {
            combined_score += avg_metric(metrics[0]);
            num_flags++;
        }
        if (flags & RECALL) {
            combined_score += avg_metric(metrics[1]);
            num_flags++;
        }
        if (flags & F1_SCORE) {
            combined_score += avg_metric(metrics[2]);
            num_flags++;
        }

        return (num_flags > 0) ? (combined_score / num_flags) : 0.0f;
    }

    // overload: predict for new sample - enhanced with SPIFFS loading
    uint8_t predict(packed_vector<2, SMALL>& features) {
        Rf_sample sample;
        sample.features = features;
        return predClassSample(sample);
    }
};



int main() {
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Random Forest PC Training\n";
    // std::string data_path = "/home/viettran/Arduino/libraries/STL_MCU/tools/data_processing/data/result/digit_data_nml.csv"; 
    RandomForest forest = RandomForest();
    // Build initial forest
    forest.MakeForest();
    
    // Train the forest to find optimal parameters (combine_ratio auto-calculated in first_scan)
    forest.fit();

    //print forest statistics
    forest.printForestStatistics();
    
    
    std::cout << "Training complete! Model saved to 'trained_model' directory.\n";
    auto result = forest.predict(forest.test_data);

    // Calculate Precision
    std::cout << "Precision in test set:\n";
    b_vector<pair<uint8_t, float>> precision = result[0];
    for (const auto& p : precision) {
    //   Serial.printf("Label: %d - %.3f\n", p.first, p.second);
        std::cout << "Label: " << (int)p.first << " - " << p.second << "\n";
    }
    float avgPrecision = 0.0f;
    for (const auto& p : precision) {
      avgPrecision += p.second;
    }
    avgPrecision /= precision.size();
    std::cout << "Avg: " << avgPrecision << "\n";

    // Calculate Recall
    std::cout << "Recall in test set:\n";
    b_vector<pair<uint8_t, float>> recall = result[1];
    for (const auto& r : recall) {
    std::cout << "Label: " << (int)r.first << " - " << r.second << "\n";
    }
    float avgRecall = 0.0f;
    for (const auto& r : recall) {
      avgRecall += r.second;
    }
    avgRecall /= recall.size();
    std::cout << "Avg: " << avgRecall << "\n";

    // Calculate F1 Score
    std::cout << "F1 Score in test set:\n";
    b_vector<pair<uint8_t, float>> f1_scores = result[2];
    for (const auto& f1 : f1_scores) {
        std::cout << "Label: " << (int)f1.first << " - " << f1.second << "\n";
    }
    float avgF1 = 0.0f;
    for (const auto& f1 : f1_scores) {
      avgF1 += f1.second;
    }
    avgF1 /= f1_scores.size();
    std::cout << "Avg: " << avgF1 << "\n";

    // Calculate Overall Accuracy
    std::cout << "Overall Accuracy in test set:\n";
    b_vector<pair<uint8_t, float>> accuracies = result[3];
    for (const auto& acc : accuracies) {
      std::cout << "Label: " << (int)acc.first << " - " << acc.second << "\n";
    }
    float avgAccuracy = 0.0f;
    for (const auto& acc : accuracies) {
      avgAccuracy += acc.second;
    }
    avgAccuracy /= accuracies.size();
    std::cout << "Avg: " << avgAccuracy << "\n";

    float result_score = forest.predict(forest.test_data, static_cast<Rf_training_flags>(forest.config.training_flag));
    std::cout << "result score: " << result_score << "\n";


    // Save the trained model
    forest.saveForest();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Total training time: " << elapsed.count() << " seconds\n ";
    return 0;
}