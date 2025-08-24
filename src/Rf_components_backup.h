#include "STL_MCU.h"  
#include "Rf_file_manager.h"
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"

class RandomForest;
// Forward declaration for callback 
namespace mcu {
    /*
    ------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ RF_COMPONENTS ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct Rf_sample{
        mcu::packed_vector<2, mcu::SMALL> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
        uint8_t label;                     // label of the sample 
    };

    using OOB_set = mcu::ChainedUnorderedSet<uint16_t>; // OOB set type
    using sampleID_set = mcu::ChainedUnorderedSet<uint16_t>; // Sample ID set type
    using sample_set = mcu::ChainedUnorderedMap<uint16_t, Rf_sample>; // set of samples

    struct Tree_node{
        uint32_t packed_data; 
        
        // Bit layout optimize for breadth-first tree building:
        // Bits 0-9:    featureID (10 bits) - 0 to 1023 features
        // Bits 10-17:  label (8 bits) - 0 to 255 classes  
        // Bits 18-19:  threshold (2 bits) - 0 to 3
        // Bit 20:      is_leaf (1 bit) - 0 or 1
        // Bits 21-31:  left child index (11 bits) - 0 to 2047 nodes -> max 8kB RAM per tree 
        // Note: right child index = left child index + 1 

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

    struct NodeToBuild {
        uint16_t nodeIndex;
        b_vector<uint16_t> sampleIDs;  
        uint16_t depth;
        
        NodeToBuild() : nodeIndex(0), depth(0) {}
        NodeToBuild(uint16_t idx, b_vector<uint16_t>&& ids, uint16_t d) 
            : nodeIndex(idx), sampleIDs(ids), depth(d) {}
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_TREE -----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    class Rf_tree {
    public:
        mcu::b_vector<Tree_node> nodes;  // Vector-based tree storage
        uint8_t index;
        bool isLoaded;

        Rf_tree() : index(255), isLoaded(false) {}
        
        Rf_tree(uint8_t idx) : index(idx), isLoaded(false) {}

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

        // Save tree to SPIFFS for ESP32
        void releaseTree(bool re_use = false) {
            if(!re_use){
                if (index == 255 || nodes.empty()) return;

                char filename[16];
                snprintf(filename, sizeof(filename), "/tree_%d.bin", index);
                
                // Remove existing file if it exists
                if (SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                }
                
                File file = SPIFFS.open(filename, FILE_WRITE);
                if (!file) {
                    Serial.printf("❌ Failed to save tree: %s\n", filename);
                    return;
                }
                
                // Write header - magic number for validation
                uint32_t magic = 0x54524545; // "TREE" in hex
                file.write((uint8_t*)&magic, sizeof(magic));
                
                // Write number of nodes
                uint32_t nodeCount = nodes.size();
                file.write((uint8_t*)&nodeCount, sizeof(nodeCount));
                
                // Save all nodes - just save the packed_data since everything is packed into it
                for (const auto& node : nodes) {
                    file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data));
                }    
                file.close();
            }

            // uint16_t countLeaf = countLeafNodes();
            // Serial.printf("✅ Tree saved: %s (%d nodes, %d leaves, %d bytes)\n", 
            //             filename, nodeCount, countLeaf, nodeCount * 4);

            nodes.clear(); // Clear nodes to free memory
            nodes.fit(); // Release excess memory
            isLoaded = false; // Mark as unloaded
        }

        // Load tree from SPIFFS into RAM for ESP32
        void loadTree(bool re_use = false) {
            if (isLoaded) return;
            
            if (index == 255) {
                Serial.println("❌ No valid index specified for tree loading");
                return;
            }
            
            char path_to_use[16];
            snprintf(path_to_use, sizeof(path_to_use), "/tree_%d.bin", index);
            
            File file = SPIFFS.open(path_to_use, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open tree file: %s\n", path_to_use);
                return;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x54524545) {
                Serial.printf("❌ Invalid tree file format: %s\n", path_to_use);
                file.close();
                return;
            }
            
            // Read number of nodes
            uint32_t nodeCount;
            if (file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                Serial.printf("❌ Failed to read node count: %s\n", path_to_use);
                file.close();
                return;
            }
            
            if (nodeCount == 0 || nodeCount > 2047) { // 11-bit limit for child indices
                Serial.printf("❌ Invalid node count: %d\n", nodeCount);
                file.close();
                return;
            }
            
            // Clear existing nodes and reserve space
            nodes.clear();
            nodes.reserve(nodeCount);
            
            // Load all nodes
            for (uint32_t i = 0; i < nodeCount; i++) {
                Tree_node node;
                if (file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                    Serial.printf("❌ Failed to read node %d from: %s\n", i, path_to_use);
                    nodes.clear();
                    file.close();
                    return;
                }
                nodes.push_back(node);
            }
            
            file.close();
            
            // Update state
            isLoaded = true;
            
            // Serial.printf("✅ Tree loaded: %s (%d nodes, %d bytes)\n", 
            //             path_to_use, nodeCount, get_memory_usage());
            if (!re_use) {
                SPIFFS.remove(path_to_use); // Remove file after loading in single mode
            }
        }

        uint8_t predictSample(const Rf_sample& sample) const {
            if (nodes.empty() || !isLoaded) return 0;
            
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

        void clearTree(bool freeMemory = false) {
            nodes.clear();
            nodes.fit();
            if(freeMemory) nodes.fit(); // Release excess memory
            isLoaded = false;
        }

        void purgeTree(bool rmf = true) {
            nodes.clear();
            nodes.fit(); // Release excess memory
            if(rmf && index != 255) {
                char filename[16];
                snprintf(filename, sizeof(filename), "/tree_%d.bin", index);
                if (SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                    // Serial.printf("✅ Tree file removed: %s\n", filename);
                } 
            }
            index = 255;
            isLoaded = false;
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

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_CONFIG ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // Configuration class for Random Forest parameters
    class Rf_config {
    public:
        // Core training parameters
        uint8_t num_trees;
        uint8_t min_split;
        uint8_t max_depth;
        bool use_boostrap;
        bool use_gini;
        bool use_validation;
        bool crossValidation;
        uint8_t k_fold;
        float boostrap_ratio; // Ratio of bootstrap samples to original size
        float unity_threshold;
        float impurity_threshold;
        float combine_ratio;
        float train_ratio;
        float valid_ratio;
        uint8_t train_flag;
        float result_score;
        uint32_t estimatedRAM;

        uint16_t num_samples;
        uint16_t num_features;
        uint8_t num_labels;

        mcu::b_vector<uint8_t, mcu::SMALL> min_split_range;
        mcu::b_vector<uint8_t, mcu::SMALL> max_depth_range;  
        
        bool isLoaded;

        Rf_config() : isLoaded(false) {
            // Set default values
            num_trees = 20;
            min_split = 2;
            max_depth = 13;
            use_boostrap = true;
            boostrap_ratio = 0.632f; // Default bootstrap ratio
            use_gini = false;
            use_validation = false;
            crossValidation = false;
            k_fold = 4;
            unity_threshold = 0.125;
            impurity_threshold = 0.1;
            combine_ratio = 0.386;
            train_ratio = 0.75;
            valid_ratio = 0.0;
            train_flag = 0x01; // ACCURACY
            result_score = 0.0;
            estimatedRAM = 0;
        }

        // Load configuration from JSON file in SPIFFS
        void loadConfig(bool re_use = true) {
            if (isLoaded) return;

            File file = SPIFFS.open(rf_config_file, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open config file: %s\n", rf_config_file);
                Serial.println("Switching to default configuration.");
                return;
            }

            String jsonString = file.readString();
            file.close();

            // Parse JSON manually (simple parsing for known structure)
            parseJSONConfig(jsonString);
            isLoaded = true;
            
            Serial.printf("✅ Config loaded: %s\n", rf_config_file);
            Serial.printf("   Trees: %d, max_depth: %d, min_split: %d\n", num_trees, max_depth, min_split);
            Serial.printf("   Estimated RAM: %d bytes\n", estimatedRAM);

            if(!re_use) {
                SPIFFS.remove(rf_config_file); // Remove file after loading in single mode
            }
        }

        // Save configuration to JSON file in SPIFFS  
        void releaseConfig(bool re_use = true) {
            // Read existing file to preserve timestamp and author
            if(!re_use){                
                String existingTimestamp = "";
                String existingAuthor = "Viettran";
                
                if (SPIFFS.exists(rf_config_file)) {
                    File readFile = SPIFFS.open(rf_config_file, FILE_READ);
                    if (readFile) {
                        String jsonContent = readFile.readString();
                        readFile.close();
                        existingTimestamp = extractStringValue(jsonContent, "timestamp");
                        existingAuthor = extractStringValue(jsonContent, "author");
                    }
                    SPIFFS.remove(rf_config_file);
                }

                File file = SPIFFS.open(rf_config_file, FILE_WRITE);
                if (!file) {
                    Serial.printf("❌ Failed to create config file: %s\n", rf_config_file);
                    return;
                }

                // Write JSON format preserving timestamp and author
                file.println("{");
                file.printf("  \"num_trees\": %d,\n", num_trees);
                file.printf("  \"min_split\": %d,\n", min_split);
                file.printf("  \"max_depth\": %d,\n", max_depth);
                file.printf("  \"use_boostrap\": %s,\n", use_boostrap ? "true" : "false");
                file.printf("  \"boostrap_ratio\": %.3f,\n", boostrap_ratio);
                file.printf("  \"use_gini\": %s,\n", use_gini ? "true" : "false");
                file.printf("  \"use_validation\": %s,\n", use_validation ? "true" : "false");
                file.printf("  \"crossValidation\": %s,\n", crossValidation ? "true" : "false");
                file.printf("  \"k_fold\": %d,\n", k_fold);
                file.printf("  \"unity_threshold\": %.3f,\n", unity_threshold);
                file.printf("  \"impurity_threshold\": %.1f,\n", impurity_threshold);
                file.printf("  \"combine_ratio\": %.3f,\n", combine_ratio);
                file.printf("  \"train_ratio\": %.2f,\n", train_ratio);
                file.printf("  \"valid_ratio\": %.1f,\n", valid_ratio);
                file.printf("  \"train_flag\": \"%s\",\n", getFlagString(train_flag).c_str());
                file.printf("  \"result_score\": %.1f,\n", result_score);
                file.printf("  \"Estimated RAM (bytes)\": %d,\n", estimatedRAM);
                
                // Preserve existing timestamp and author
                if (existingTimestamp.length() > 0) {
                    file.printf("  \"timestamp\": \"%s\",\n", existingTimestamp.c_str());
                }
                if (existingAuthor.length() > 0) {
                    file.printf("  \"author\": \"%s\"\n", existingAuthor.c_str());
                } else {
                    // Remove trailing comma if no author
                    file.seek(file.position() - 2); // Go back to remove ",\n"
                    file.println("");
                }
                
                file.println("}");
                file.close();
            }
            
            // Clear from RAM  
            purgeConfig();
            
            Serial.printf("✅ Config saved to: %s\n", rf_config_file);
        }

        // Update timestamp in the JSON file
        void update_timestamp() {
            if (!SPIFFS.exists(rf_config_file)) return;
            
            // Read existing file
            File readFile = SPIFFS.open(rf_config_file, FILE_READ);
            if (!readFile) return;
            
            String jsonContent = readFile.readString();
            readFile.close();
            
            // Get current timestamp
            String currentTime = String(millis()); // Simple timestamp, can be improved
            
            // Find and replace timestamp
            int timestampStart = jsonContent.indexOf("\"timestamp\":");
            if (timestampStart != -1) {
                int valueStart = jsonContent.indexOf("\"", timestampStart + 12);
                int valueEnd = jsonContent.indexOf("\"", valueStart + 1);
                if (valueStart != -1 && valueEnd != -1) {
                    String newContent = jsonContent.substring(0, valueStart + 1) + 
                                    currentTime + 
                                    jsonContent.substring(valueEnd);
                    
                    // Write updated content
                    SPIFFS.remove(rf_config_file);
                    File writeFile = SPIFFS.open(rf_config_file, FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(newContent);
                        writeFile.close();
                    }
                }
            }
        }

        void purgeConfig() {
            isLoaded = false;
        }

    private:
        // Simple JSON parser for configuration
        void parseJSONConfig(const String& jsonStr) {
            // Use the actual keys from esp32_config.json
            num_trees = extractIntValue(jsonStr, "numTrees");              // ✅ Fixed
            min_split = extractIntValue(jsonStr, "minSplit");              // ✅ Fixed  
            max_depth = extractIntValue(jsonStr, "maxDepth");              // ✅ Fixed
            use_boostrap = extractBoolValue(jsonStr, "useBootstrap");      // ✅ Fixed
            boostrap_ratio = extractFloatValue(jsonStr, "boostrapRatio");  // ✅ Fixed
            use_gini = extractBoolValue(jsonStr, "useGini");               // ✅ Fixed
            use_validation = extractBoolValue(jsonStr, "useValidation");   // ✅ Fixed
            crossValidation = extractBoolValue(jsonStr, "crossValidation"); // ✅ Already correct
            k_fold = extractIntValue(jsonStr, "k_fold");                   // ✅ Already correct
            unity_threshold = extractFloatValue(jsonStr, "unityThreshold"); // ✅ Fixed
            impurity_threshold = extractFloatValue(jsonStr, "impurityThreshold"); // ✅ Fixed
            combine_ratio = extractFloatValue(jsonStr, "combineRatio");     // ✅ Fixed
            train_ratio = extractFloatValue(jsonStr, "trainRatio");         // ✅ Fixed
            valid_ratio = extractFloatValue(jsonStr, "validRatio");         // ✅ Fixed
            train_flag = parseFlagValue(extractStringValue(jsonStr, "trainFlag")); // ✅ Fixed
            result_score = extractFloatValue(jsonStr, "resultScore");       // ✅ Fixed
            estimatedRAM = extractIntValue(jsonStr, "Estimated RAM (bytes)"); // ✅ Already correct
        }

        // Convert flag string to uint8_t
        uint8_t parseFlagValue(const String& flagStr) {
            if (flagStr == "ACCURACY") return 0x01;
            if (flagStr == "PRECISION") return 0x02;
            if (flagStr == "RECALL") return 0x04;
            if (flagStr == "F1_SCORE") return 0x08;
            if (flagStr == "EARLY_STOP") return 0x00;
            return 0x01; // Default to ACCURACY
        }

        // Convert uint8_t flag to string
        String getFlagString(uint8_t flag) {
            switch(flag) {
                case 0x01: return "ACCURACY";
                case 0x02: return "PRECISION";
                case 0x04: return "RECALL";
                case 0x08: return "F1_SCORE";
                case 0x00: return "EARLY_STOP";
                default: return "ACCURACY";
            }
        }

        uint32_t extractIntValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return 0;
            
            int colonIndex = json.indexOf(":", keyIndex);
            if (colonIndex == -1) return 0;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.toInt();
        }

        float extractFloatValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return 0.0;
            
            int colonIndex = json.indexOf(":", keyIndex);
            if (colonIndex == -1) return 0.0;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.toFloat();
        }

        bool extractBoolValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return false;
            
            int colonIndex = json.indexOf(":", keyIndex);
            if (colonIndex == -1) return false;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.indexOf("true") != -1;
        }

        String extractStringValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return "";
            
            int colonIndex = json.indexOf(":", keyIndex);
            if (colonIndex == -1) return "";
            
            int firstQuoteIndex = json.indexOf("\"", colonIndex);
            if (firstQuoteIndex == -1) return "";
            
            int secondQuoteIndex = json.indexOf("\"", firstQuoteIndex + 1);
            if (secondQuoteIndex == -1) return "";
            
            return json.substring(firstQuoteIndex + 1, secondQuoteIndex);
        }
    };

        /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_DATA ------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // flag for Rf_data : base data, training data, subset data (for each tree in forest), etc.
    typedef enum Rf_data_flags{
        BASE_DATA = 0,          // base data, used for initial training
        TRAIN_DATA,          // training data, used for training the forest
        SUB_DATA,            // subset data, used for each tree in the forest
        TEST_DATA,          // testing data, used for evaluating the model
        VALID_DATA      // validation data, used for model validation
    }Rf_data_flags;

    class Rf_data {
    public:
        mcu::ChainedUnorderedMap<uint16_t, Rf_sample> allSamples;    // all sample and it's ID 
        Rf_data_flags flag;
        static void (*restore_data_callback)(Rf_data_flags&, uint8_t);
        uint8_t index;      
        bool   isLoaded = false;

        Rf_data() : index(255){}
        Rf_data(uint8_t idx) : index(idx), isLoaded(false) {}

        // Generate filename based on index and flag
        String generateDataFilename() const {
            if (index == 255 && flag == SUB_DATA) return "";
            
            switch(flag) {
                case BASE_DATA:
                    return "/base_data.bin";
                case TRAIN_DATA:
                    return "/train_data.bin";
                case SUB_DATA:
                    return "/tree_" + String(index) + "_data.bin";
                case TEST_DATA:
                    return "/test_data.bin";
                case VALID_DATA:
                    return "/valid_data.bin";
                default:
                    return "/data_" + String(index) + ".bin";
            }
        }
        // Load data from CSV format (used only once for initial dataset conversion)
        void loadCSVData(String csvFilename, uint8_t numFeatures) {
            if(isLoaded) return;
            
            File file = SPIFFS.open(csvFilename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open CSV file for reading.");
                return;
            }

            Serial.printf("📊 Loading CSV: %s (expecting %d features per sample)\n", csvFilename.c_str(), numFeatures);
            
            uint16_t sampleID = 0;
            uint16_t linesProcessed = 0;
            uint16_t emptyLines = 0;
            uint16_t validSamples = 0;
            uint16_t invalidSamples = 0;
            
            while (file.available()) {
                String line = file.readStringUntil('\n');
                line.trim();
                linesProcessed++;
                
                if (line.length() == 0) {
                    emptyLines++;
                    continue;
                }

                Rf_sample s;
                s.features.clear();
                s.features.reserve(numFeatures);

                uint8_t fieldIdx = 0;
                int start = 0;
                while (start < line.length()) {
                    int comma = line.indexOf(',', start);
                    if (comma < 0) comma = line.length();

                    String tok = line.substring(start, comma);
                    uint8_t v = (uint8_t)tok.toInt();

                    if (fieldIdx == 0) {
                        s.label = v;
                    } else {
                        s.features.push_back(v);
                    }

                    fieldIdx++;
                    start = comma + 1;
                }
                
                // Validate the sample
                if (fieldIdx != numFeatures + 1) {
                    Serial.printf("❌ Line %d: Expected %d fields, got %d\n", linesProcessed, numFeatures + 1, fieldIdx);
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    Serial.printf("❌ Line %d: Expected %d features, got %d\n", linesProcessed, numFeatures, s.features.size());
                    invalidSamples++;
                    continue;
                }
                
                s.features.fit();

                allSamples[sampleID] = s;
                sampleID++;
                validSamples++;
                
                if (sampleID >= 50000) {
                    Serial.println("⚠️  Reached sample limit (10000)");
                    break;
                }
            }
            
            Serial.printf("📋 CSV Processing Results:\n");
            Serial.printf("   Lines processed: %d\n", linesProcessed);
            Serial.printf("   Empty lines: %d\n", emptyLines);
            Serial.printf("   Valid samples: %d\n", validSamples);
            Serial.printf("   Invalid samples: %d\n", invalidSamples);
            Serial.printf("   Total samples in memory: %d\n", allSamples.size());
            
            allSamples.fit();
            file.close();
            isLoaded = true;
            SPIFFS.remove(csvFilename);
            Serial.println("✅ CSV data loaded and file removed.");
        }

        // Save data with original sample IDs preserved
        // save to binary format : sampleID (2 bytes) | label (1 byte) | features (packed : 4 values per byte)
        void releaseData(bool reuse = true) {
            if(!isLoaded) return;
            if(flag == SUB_DATA && index == 255) {
                Serial.println("❌ Cannot release subset data without a valid index.");
                return;
            }
            String filename = generateDataFilename();
            // Serial.printf("📂 Saving data to: %s\n", filename.c_str());
            
            if(!reuse){
                // Remove any existing file
                if (SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }

                File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
                if (!file) {
                    Serial.println("❌ Failed to open binary file for writing.");
                    return;
                }

                // Write binary header
                uint32_t numSamples = allSamples.size();
                uint16_t numFeatures = 0;
                
                // Determine number of features from first sample
                if(numSamples > 0) {
                    numFeatures = allSamples.begin()->second.features.size();
                }
                
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

                // Calculate packed bytes needed for features (4 values per byte)
                uint16_t packedFeatureBytes = (numFeatures + 3) / 4;

                // Write samples WITH original IDs
                for (auto entry = allSamples.begin(); entry != allSamples.end(); ++entry) {
                    uint16_t originalId = entry->first;  // ✅ Get original sample ID
                    const Rf_sample& s = entry->second;
                    
                    // ✅ Write original ID first
                    file.write((uint8_t*)&originalId, sizeof(originalId));
                    
                    // Write label
                    file.write(&s.label, sizeof(s.label));
                    
                    // Pack and write features
                    uint8_t packedBuffer[packedFeatureBytes];
                    // Initialize buffer to 0
                    for(uint16_t i = 0; i < packedFeatureBytes; i++) {
                        packedBuffer[i] = 0;
                    }
                    
                    // Pack 4 feature values into each byte
                    for (size_t i = 0; i < s.features.size(); ++i) {
                        uint16_t byteIndex = i / 4;
                        uint8_t bitOffset = (i % 4) * 2;
                        uint8_t feature_value = s.features[i] & 0x03;
                        packedBuffer[byteIndex] |= (feature_value << bitOffset);
                    }
                    
                    file.write(packedBuffer, packedFeatureBytes);
                }

                file.close();
            }
            
            // Clear memory
            allSamples.clear();
            allSamples.fit();
            isLoaded = false;
        }

        // Load data with original sample IDs preserved
        void loadData(bool re_use = true, String path = "") {
            if(isLoaded) return;
            if(flag == SUB_DATA && index == 255) {
                Serial.println("❌ Cannot load subset data without a valid index.");
                return;
            }
            bool restore_yet = false;

            String filename;
            if(flag == BASE_DATA && path.length() > 0) filename = path;       // path is specified for BASE_DATA
            else filename = generateDataFilename();

            uint8_t treeIndex = index;
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open binary file for reading.");
                if(SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }
                if(restore_data_callback){
                    restore_yet = true;
                    restore_data_callback(flag, treeIndex);
                }
            }

            if(!restore_yet) {    
                // Read binary header
                uint32_t numSamples;
                uint16_t numFeatures;
                
                if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
                file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                    Serial.println("❌ Failed to read binary header.");
                    if(restore_data_callback){
                        restore_yet = true;
                        restore_data_callback(flag, treeIndex);
                    }
                }

                if(!restore_yet) {
                    // Calculate packed bytes needed for features (4 values per byte)
                    uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
                    
                    // ✅ Read samples with original IDs
                    for(uint16_t i = 0; i < numSamples; i++) {
                        uint16_t originalId;  // ✅ Read original sample ID
                        Rf_sample s;
                        
                        // ✅ Read original ID first
                        if(file.read((uint8_t*)&originalId, sizeof(originalId)) != sizeof(originalId)) {
                            Serial.printf("❌ Failed to read sample ID for sample %u\n", i);
                            if(restore_data_callback){
                                restore_data_callback(flag, treeIndex);
                            }
                            break;
                        }
                        
                        // Read label
                        if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                            Serial.printf("❌ Failed to read label for sample %u\n", i);
                            if(restore_data_callback){
                                restore_data_callback(flag, treeIndex);
                            }
                            break;
                        }
                        
                        // Read packed features
                        s.features.clear();
                        s.features.reserve(numFeatures);
                        
                        // Read packed bytes
                        uint8_t packedBuffer[packedFeatureBytes];
                        if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                            Serial.printf("❌ Failed to read packed features for sample %u\n", i);
                            if(restore_data_callback){
                                restore_data_callback(flag, treeIndex);
                            }
                            break;
                        }
                        
                        // Unpack features from bytes
                        for(uint16_t j = 0; j < numFeatures; j++) {
                            uint16_t byteIndex = j / 4;
                            uint8_t bitOffset = (j % 4) * 2;
                            uint8_t mask = 0x03 << bitOffset;
                            uint8_t feature = (packedBuffer[byteIndex] & mask) >> bitOffset;
                            s.features.push_back(feature);
                        }
                        s.features.fit();
                        
                        // ✅ Use original ID instead of sequential counter
                        allSamples[originalId] = s;
                    }
                }
                allSamples.fit();
                isLoaded = true;
            }
            if(file){
                file.close();
            }
            if(!re_use) {
                SPIFFS.remove(filename.c_str()); // Remove file after loading in single mode
            }
        }

        // overload : load a chunk of samples by IDs from binary file
        sample_set loadData(mcu::b_vector<uint16_t>& sampleIDs_bag) {
            sample_set chunkSamples;
            if(isLoaded || (index == 255 && flag == SUB_DATA)) return chunkSamples;
            sampleIDs_bag.sort();

            String filename = generateDataFilename();
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open binary file for reading.");
                return chunkSamples;
            }

            uint32_t numSamples;
            uint16_t numFeatures;
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                Serial.println("❌ Failed to read binary header.");
                file.close();
                return chunkSamples;
            }

            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
            chunkSamples.reserve(sampleIDs_bag.size());

            // Use a set for fast lookup
            sampleID_set sampleIDs_set;
            for (const auto& id : sampleIDs_bag) sampleIDs_set.insert(id);

            for(uint32_t i = 0; i < numSamples; i++) {
                uint16_t originalId;
                if(file.read((uint8_t*)&originalId, sizeof(originalId)) != sizeof(originalId)) break;

                if(sampleIDs_set.find(originalId) == sampleIDs_set.end()) {
                    // Not needed, skip label and features
                    file.seek(file.position() + sizeof(uint8_t) + packedFeatureBytes);
                    continue;
                }

                Rf_sample s;
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) break;

                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) break;

                for(uint16_t j = 0; j < numFeatures; j++) {
                    uint16_t byteIndex = j / 4;
                    uint8_t bitOffset = (j % 4) * 2;
                    uint8_t mask = 0x03 << bitOffset;
                    uint8_t feature = (packedBuffer[byteIndex] & mask) >> bitOffset;
                    s.features.push_back(feature);
                }
                s.features.fit();
                chunkSamples[originalId] = s;

                if(chunkSamples.size() >= sampleIDs_bag.size()) break;
            }
            file.close();
            return chunkSamples;
        }
            
        // repeat a number of samples to reach a certain number of samples: boostrap sampling
        void boostrapData(uint16_t numSamples, uint16_t maxSamples){
            bool preloaded = isLoaded;
            if(!isLoaded){
                loadData();
            }
            uint16_t currentSize = allSamples.size();
            mcu::b_vector<uint16_t> sampleIDs;
            sampleIDs.reserve(currentSize);
            for (const auto& entry : allSamples) {
                sampleIDs.push_back(entry.first); // Store original IDs
            }
            sampleIDs.sort(); // Sort IDs for consistent access
            uint16_t cursor = 0;
            mcu::b_vector<uint16_t> newSampleIDs;
            for(uint16_t i = 0; i<maxSamples; i++){
                if(cursor < sampleIDs.size() && sampleIDs[cursor] == i){
                    cursor++;
                } else {
                    newSampleIDs.push_back(i); // Add missing IDs
                }
            }
            if(currentSize >= numSamples) {
                Serial.printf("Data already has %d samples, no need to boostrap.\n", currentSize);
                return;
            }
            // Serial.printf("Boostraping data from %d to %d samples...\n", currentSize, numSamples);
            allSamples.reserve(numSamples);
            while(allSamples.size() < numSamples) {
                // Randomly select a sample ID from existing samples
                uint16_t pos = esp_random()% currentSize;
                uint16_t sampleID = sampleIDs[pos]; // Get the original ID
                auto it = allSamples.find(sampleID);
                if (it == allSamples.end()) {
                    continue; // Skip if not found
                }
                Rf_sample sample = it->second; // Get the sample
                // Add the sample again with a new ID
                if(!newSampleIDs.empty()) {
                    uint16_t newID = newSampleIDs.back();
                    allSamples[newID] = sample; // Use new ID
                    newSampleIDs.pop_back(); // Remove used ID
                }
            }
            if(!preloaded) {
                releaseData(true); // Save to SPIFFS if not preloaded
            }
        }
        
        // add new sample to the end of the dataset
        bool add_new_sample(const Rf_sample& sample, uint16_t sampleID) {
            if (flag != BASE_DATA) {
                Serial.println("❌ only BASE_DATA can be modified with new samples.");
                return false;
            }

            String filename = generateDataFilename();

            // If data is loaded, add to in-memory map as well
            if (isLoaded) {
                allSamples[sampleID] = sample;
            }

            File file;
            uint16_t numFeatures = sample.features.size();
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;

            if (SPIFFS.exists(filename.c_str())) {
                // File exists, open to read header and then append
                file = SPIFFS.open(filename.c_str(), "r+");
                if (!file) {
                    Serial.printf("❌ Failed to open existing file for update: %s\n", filename.c_str());
                    return false;
                }

                // Read current sample count and update it
                uint32_t currentSamples;
                file.read((uint8_t*)&currentSamples, sizeof(currentSamples));
                currentSamples++;
                
                // Rewind and write the new sample count
                file.seek(0);
                file.write((uint8_t*)&currentSamples, sizeof(currentSamples));
                file.close();

                // Re-open in append mode to add the new sample
                file = SPIFFS.open(filename.c_str(), "a");

            } else {
                // File doesn't exist, create it and write the header
                file = SPIFFS.open(filename.c_str(), "w");
                if (!file) {
                    Serial.printf("❌ Failed to create new file: %s\n", filename.c_str());
                    return false;
                }
                uint32_t numSamples = 1;
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));
            }

            if (!file) {
                Serial.printf("❌ File operation failed for: %s\n", filename.c_str());
                return false;
            }

            // Append the new sample data
            file.write((uint8_t*)&sampleID, sizeof(sampleID));
            file.write(&sample.label, sizeof(sample.label));

            // Pack and write features
            uint8_t packedBuffer[packedFeatureBytes];
            memset(packedBuffer, 0, packedFeatureBytes);

            for (size_t i = 0; i < numFeatures; ++i) {
                uint16_t byteIndex = i / 4;
                uint8_t bitOffset = (i % 4) * 2;
                uint8_t feature_value = sample.features[i] & 0x03;
                packedBuffer[byteIndex] |= (feature_value << bitOffset);
            }
            file.write(packedBuffer, packedFeatureBytes);
            file.close();
            return true;
        }


        // FIXED: Safe data purging
        void purgeData() {
            // Clear in-memory structures first
            allSamples.clear();
            allSamples.fit();
            isLoaded = false;

            // Then remove the SPIFFS file if one was specified
            if (index != 255 || flag != SUB_DATA) {
                String filename = generateDataFilename();
                if (SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                    Serial.printf("🗑️ Deleted file %s\n", filename.c_str());
                }
                index = 255;
            }
        }
    };
    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_BASE -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // flags indicating the status of member files
    typedef enum Rf_base_flags : uint8_t{
        BASE_FILE       = 0x01,            // base data file exists
        DATA_PARAMS     = 0x02,          // data_params file exists
        CATEGORIZER     = 0x04,         // categorizer file exists
        BASE_DATA_CSV   = 0x08        // base data file in CSV format
    } Rf_base_flags;

    // Base file management class for Random Forest project status
    class Rf_base {
    private:
        uint8_t flags; // flags indicating the status of member files
        String baseFile;
    public:
        Rf_base() : flags(static_cast<Rf_base_flags>(0)), baseFile("") {}
        Rf_base(const char* baseFile) : flags(static_cast<Rf_base_flags>(0)), baseFile(baseFile) {
            init(baseFile);
        }

        void init(const char* baseFile){
            if (!baseFile || strlen(baseFile) == 0) {
                Serial.println("❌ Base file name is empty.");
                return;
            }else{
                this->baseFile = String(baseFile);
                if(SPIFFS.exists(baseFile)) {
                    // setup flags to indicate base file exists
                    flags = static_cast<Rf_base_flags>(BASE_FILE);
                    // generate filenames for data_params and categorizer based on baseFile
                    String input = String(baseFile);
                    int pos = input.lastIndexOf("_nml");
                    if(pos == -1){
                        Serial.println("❌ Invalid base file name format, expected '_nml' suffix.");
                        return;
                    }
                    if (input.endsWith(".csv")) {
                        flags |= static_cast<Rf_base_flags>(BASE_DATA_CSV);
                    }
                    String prefix = input.substring(0, pos);
                    String dataParamsFile = prefix + "_dp.csv"; // data_params file
                    String categorizerFile = prefix + "_ctg.csv"; // categorizer file

                    //  check categorizer file
                    if(SPIFFS.exists(categorizerFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(CATEGORIZER);
                    } else {
                        Serial.printf("❌ No categorizer file found : %s\n", categorizerFile.c_str());
                        Serial.println("-> Model still able to re_train or run inference, but cannot re_train with new data later.");
                    }
                    // check data_params file
                    if(SPIFFS.exists(dataParamsFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(DATA_PARAMS);
                    } else {
                        Serial.printf("❌ No data_parameters file found: %s\n", dataParamsFile.c_str());
                        Serial.println("Re_training and inference are not available..\n");
                    }
                } else {
                    Serial.printf("❌ Base file does not exist: %s\n", baseFile);
                    // setup flags to indicate no base file
                    flags = static_cast<Rf_base_flags>(0);
                    return;
                }
            }

        }

        // generate data_params and categorizer filenames based on baseFile
        String get_dpFile(){
            int pos = baseFile.lastIndexOf("_nml");
            if(pos == -1){
                Serial.println("❌ Invalid base file name format, expected '_nml' suffix.");
                return "";  
            }else{
                String prefix = baseFile.substring(0, pos);
                return prefix + "_dp.csv"; // data_params file
            }
        }

        String get_ctgFile(){
            int pos = baseFile.lastIndexOf("_nml");
            if(pos == -1){
                Serial.println("❌ Invalid base file name format, expected '_nml' suffix.");
                return "";  
            }else{
                String prefix = baseFile.substring(0, pos);
                return prefix + "_ctg.csv"; // categorizer file
            }
        }

        bool baseFile_is_csv() const {
            return (flags & BASE_DATA_CSV) != 0;
        }

        bool baseFile_exists() const {
            return (flags & BASE_FILE) != 0;
        }

        bool dataParams_exists() const {
            return (flags & DATA_PARAMS) != 0;
        }
        bool categorizer_exists() const {
            return (flags & CATEGORIZER) != 0;
        }

        // check for tree files exists : tree_0.bin, tree_1.bin..
        bool able_to_inference(uint8_t num_trees){
            for(uint8_t i = 0; i < num_trees; i++) {
                String treeFile = "/tree_" + String(i) + ".bin";
                if (!SPIFFS.exists(treeFile)) {
                    return false; // If any tree file is missing, inference is not possible
                }
            }
            return true; // All tree files exist
        }

        bool able_to_training(){
            if(baseFile_exists()){
                if(baseFile_is_csv()) return true;
                if(!dataParams_exists()) return false;
                else return true; // if binary baseFile, data_params is required
            }
            return false;
        }
    };
    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------- RF_NODE_PREDCITOR ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    struct node_data {
        uint8_t min_split;
        uint16_t max_depth;
        uint16_t total_nodes;
        
        node_data() : min_split(0), max_depth(0), total_nodes(0) {}
        node_data(uint8_t min_split, uint16_t max_depth) 
            : min_split(min_split), max_depth(max_depth), total_nodes(0) {}
        node_data(uint8_t min_split, uint16_t max_depth, uint16_t total_nodes) 
            : min_split(min_split), max_depth(max_depth), total_nodes(total_nodes) {}
    };

    class Rf_node_predictor {
    public:
        float coefficients[3];  // bias, min_split_coeff, max_depth_coeff
        bool is_trained;
        b_vector<node_data> buffer;
    private:
        
        float evaluate_formula(const node_data& data) const {
            if (!is_trained) {
                return manual_estimate(data); // Use manual estimate if not trained
            }
            
            float result = coefficients[0]; // bias
            result += coefficients[1] * static_cast<float>(data.min_split);
            result += coefficients[2] * static_cast<float>(data.max_depth);
            
            return result > 10.0f ? result : 10.0f; // ensure reasonable minimum
        }

        // if failed to load predictor, manual estimate will be used
        float manual_estimate(const node_data& data) const {
            if (data.min_split == 0 || data.max_depth == 0) {
                return 100.0f; // default estimate
            }
            // Simple heuristic: more nodes = better accuracy
            float estimate = 100.0f - data.min_split * 12 + data.max_depth * 3;
            return estimate < 10.0f ? 10.0f : estimate; // ensure reasonable minimum
        }

    public:
        uint8_t accuracy;      // in percentage
        uint8_t peak_percent;  // number of nodes at depth with maximum number of nodes / total number of nodes in tree
        
        Rf_node_predictor() : is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
            loadPredictor();
        }

        
        // Load trained model from SPIFFS (updated format without version)
        bool loadPredictor() {
            if(is_trained) return true;
            if (!SPIFFS.exists(node_predictor_file)) {
                Serial.printf("❌ No predictor file found: %s !\n", node_predictor_file);
                Serial.println("Switching to use default predictor.");
                return false;
            }
            
            File file = SPIFFS.open(node_predictor_file, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open predictor file: %s\n", node_predictor_file);
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                Serial.printf("❌ Invalid predictor file format: %s\n", node_predictor_file);
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                Serial.println("❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                Serial.println("❌ Failed to read accuracy");
                file.close();
                return false;
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                Serial.println("❌ Failed to read peak_percent");
                file.close();
                return false;
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients) || num_coefficients != 3) {
                Serial.printf("❌ Invalid coefficient count: %d (expected 3)\n", num_coefficients);
                file.close();
                return false;
            }
            
            // Read coefficients
            if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                Serial.println("❌ Failed to read coefficients");
                file.close();
                return false;
            }
            
            file.close();
            
            // Only set is_trained to true if the file was actually trained
            if (file_is_trained) {
                is_trained = true;
                
                // Fix PC version's peak_percent bug where it often saves 0%
                if (peak_percent == 0) {
                    peak_percent = 30; // Use reasonable default for binary trees
                    Serial.printf("⚠️  Fixed peak_percent from 0%% to 30%% (PC version bug)\n");
                }
                
                Serial.printf("✅ Node_predictor loaded: %s (accuracy: %d%%, peak: %d%%)\n", 
                            node_predictor_file, accuracy, peak_percent);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
            } else {
                Serial.printf("⚠️  predictor file exists but is not trained: %s\n", node_predictor_file);
                is_trained = false;
            }
            
            return file_is_trained;
        }
        
        // Save trained predictor to SPIFFS
        bool savePredictor() {
            // Remove existing file
            if (SPIFFS.exists(node_predictor_file)) {
                SPIFFS.remove(node_predictor_file);
            }
            
            File file = SPIFFS.open(node_predictor_file, FILE_WRITE);
            if (!file) {
                Serial.printf("❌ Failed to create node_predictor file: %s\n", node_predictor_file);
                return false;
            }
            
            // Write magic number
            uint32_t magic = 0x4E4F4445; // "NODE" in hex
            file.write((uint8_t*)&magic, sizeof(magic));
            
            // Write training status
            file.write((uint8_t*)&is_trained, sizeof(is_trained));
            
            // Write accuracy and peak_percent
            file.write((uint8_t*)&accuracy, sizeof(accuracy));
            file.write((uint8_t*)&peak_percent, sizeof(peak_percent));
            
            // Write number of coefficients
            uint8_t num_coefficients = 3;
            file.write((uint8_t*)&num_coefficients, sizeof(num_coefficients));
            
            // Write coefficients
            file.write((uint8_t*)coefficients, sizeof(float) * 3);
            
            file.close();
            
            Serial.printf("✅ Node_predictor saved: %s (%d bytes, accuracy: %d%%, peak: %d%%)\n", 
                        node_predictor_file, 
                        sizeof(magic) + sizeof(is_trained) + sizeof(accuracy) + sizeof(peak_percent) + 
                        sizeof(num_coefficients) + sizeof(float) * 3, accuracy, peak_percent);
            return true;
        }
        
        // Predict number of nodes for given parameters
        uint16_t estimate(const node_data& data) {
            if(!is_trained) {
                if(!loadPredictor()){
                    return manual_estimate(data); // Use manual estimate if predictor is disabled 
                }
            }
            
            float prediction = evaluate_formula(data);
            return static_cast<uint16_t>(prediction + 0.5f); // Round to nearest integer
        }
        uint16_t estimate(uint8_t min_split, uint16_t max_depth) {
            node_data data(min_split, max_depth);
            return estimate(data);
        }
        
        // Retrain the predictor using data from rf_tree_log.csv (synchronized with PC version)
        bool re_train(bool save_after_retrain = true) {
            if(!can_retrain()) {
                Serial.println("❌ No training data available for retraining.");
                return false;
            }
            if(buffer.size() > 0){
                add_new_samples(buffer);
            }
            buffer.clear();
            buffer.fit();
            
            File file = SPIFFS.open(node_predictor_log, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open training log: %s\n", node_predictor_log);
                return false;
            }
            
            Serial.println("🔄 Retraining node predictor from CSV data...");
            

            mcu::b_vector<node_data> training_data;
            training_data.reserve(50); // Reserve space for training samples
            
            String line;
            bool first_line = true;
            
            // Read CSV data
            while (file.available()) {
                line = file.readStringUntil('\n');
                line.trim();
                
                if (line.length() == 0 || first_line) {
                    first_line = false;
                    continue; // Skip header line
                }
                
                // Parse CSV line: min_split,max_depth,total_nodes
                node_data sample;
                int comma1 = line.indexOf(',');
                int comma2 = line.indexOf(',', comma1 + 1);
                
                if (comma1 != -1 && comma2 != -1) {
                    String min_split_str = line.substring(0, comma1);
                    String max_depth_str = line.substring(comma1 + 1, comma2);
                    String total_nodes_str = line.substring(comma2 + 1);
                    
                    sample.min_split = min_split_str.toInt();
                    sample.max_depth = max_depth_str.toInt();
                    sample.total_nodes = total_nodes_str.toInt();
                    
                    // skip invalid samples
                    if (sample.min_split > 0 && sample.max_depth > 0 && sample.total_nodes > 0) {
                        training_data.push_back(sample);
                    }
                }
            }
            file.close();
            
            if (training_data.size() < 3) {
                Serial.printf("❌ Insufficient training data: %d samples (need at least 3)\n", training_data.size());
                return false;
            }
            
            // Use PC version's trend analysis approach instead of complex regression
            // Collect all unique min_split and max_depth values
            mcu::b_vector<uint8_t> unique_min_splits;
            mcu::b_vector<uint16_t> unique_max_depths;
            
            for (const auto& sample : training_data) {
                // Add unique min_split values
                bool found_split = false;
                for (const auto& existing_split : unique_min_splits) {
                    if (existing_split == sample.min_split) {
                        found_split = true;
                        break;
                    }
                }
                if (!found_split) {
                    unique_min_splits.push_back(sample.min_split);
                }
                
                // Add unique max_depth values
                bool found_depth = false;
                for (const auto& existing_depth : unique_max_depths) {
                    if (existing_depth == sample.max_depth) {
                        found_depth = true;
                        break;
                    }
                }
                if (!found_depth) {
                    unique_max_depths.push_back(sample.max_depth);
                }
            }
            
            Serial.printf("   Found %d unique min_splits, %d unique max_depths\n", 
                        unique_min_splits.size(), unique_max_depths.size());
            
            // Sort vectors for easier processing
            // Simple bubble sort for small vectors
            for (size_t i = 0; i < unique_min_splits.size(); i++) {
                for (size_t j = i + 1; j < unique_min_splits.size(); j++) {
                    if (unique_min_splits[i] > unique_min_splits[j]) {
                        uint8_t temp = unique_min_splits[i];
                        unique_min_splits[i] = unique_min_splits[j];
                        unique_min_splits[j] = temp;
                    }
                }
            }
            
            for (size_t i = 0; i < unique_max_depths.size(); i++) {
                for (size_t j = i + 1; j < unique_max_depths.size(); j++) {
                    if (unique_max_depths[i] > unique_max_depths[j]) {
                        uint16_t temp = unique_max_depths[i];
                        unique_max_depths[i] = unique_max_depths[j];
                        unique_max_depths[j] = temp;
                    }
                }
            }
            
            // Calculate effects using a simpler approach without large intermediate vectors
            // Calculate min_split effect directly
            float split_effect = 0.0f;
            if (unique_min_splits.size() >= 2) {
                // Calculate average nodes for first and last min_split values
                float first_split_avg = 0.0f;
                float last_split_avg = 0.0f;
                int first_split_count = 0;
                int last_split_count = 0;
                
                uint8_t first_split = unique_min_splits[0];
                uint8_t last_split = unique_min_splits[unique_min_splits.size() - 1];
                
                // Calculate averages directly from training data
                for (const auto& sample : training_data) {
                    if (sample.min_split == first_split) {
                        first_split_avg += sample.total_nodes;
                        first_split_count++;
                    } else if (sample.min_split == last_split) {
                        last_split_avg += sample.total_nodes;
                        last_split_count++;
                    }
                }
                
                if (first_split_count > 0 && last_split_count > 0) {
                    first_split_avg /= first_split_count;
                    last_split_avg /= last_split_count;
                    
                    float split_range = static_cast<float>(last_split - first_split);
                    if (split_range > 0.01f) {
                        split_effect = (last_split_avg - first_split_avg) / split_range;
                    }
                }
            }
            
            // Calculate max_depth effect directly
            float depth_effect = 0.0f;
            if (unique_max_depths.size() >= 2) {
                // Calculate average nodes for first and last max_depth values
                float first_depth_avg = 0.0f;
                float last_depth_avg = 0.0f;
                int first_depth_count = 0;
                int last_depth_count = 0;
                
                uint16_t first_depth = unique_max_depths[0];
                uint16_t last_depth = unique_max_depths[unique_max_depths.size() - 1];
                
                // Calculate averages directly from training data
                for (const auto& sample : training_data) {
                    if (sample.max_depth == first_depth) {
                        first_depth_avg += sample.total_nodes;
                        first_depth_count++;
                    } else if (sample.max_depth == last_depth) {
                        last_depth_avg += sample.total_nodes;
                        last_depth_count++;
                    }
                }
                
                if (first_depth_count > 0 && last_depth_count > 0) {
                    first_depth_avg /= first_depth_count;
                    last_depth_avg /= last_depth_count;
                    
                    float depth_range = static_cast<float>(last_depth - first_depth);
                    if (depth_range > 0.01f) {
                        depth_effect = (last_depth_avg - first_depth_avg) / depth_range;
                    }
                }
            }
            
            // Calculate overall average as baseline
            float overall_avg = 0.0f;
            for (const auto& sample : training_data) {
                overall_avg += sample.total_nodes;
            }
            overall_avg /= training_data.size();
            
            // Build the simple linear predictor: nodes = bias + split_coeff * min_split + depth_coeff * max_depth
            // Calculate bias to center the predictor around the overall average
            float reference_split = unique_min_splits.empty() ? 3.0f : static_cast<float>(unique_min_splits[0]);
            float reference_depth = unique_max_depths.empty() ? 6.0f : static_cast<float>(unique_max_depths[0]);
            
            coefficients[0] = overall_avg - (split_effect * reference_split) - (depth_effect * reference_depth); // bias
            coefficients[1] = split_effect; // min_split coefficient
            coefficients[2] = depth_effect; // max_depth coefficient
            
            // Calculate accuracy using PC version's approach exactly
            float total_error = 0.0f;
            float total_actual = 0.0f;
            
            for (const auto& sample : training_data) {
                node_data data(sample.min_split, sample.max_depth);
                float predicted = evaluate_formula(data);
                float actual = static_cast<float>(sample.total_nodes);
                float error = fabs(predicted - actual);
                total_error += error;
                total_actual += actual;
            }
            
            float mae = total_error / training_data.size(); // Mean Absolute Error
            float mape = (total_error / total_actual) * 100.0f; // Mean Absolute Percentage Error
            
            // PC version returns (100.0f - mape) which gives 0-100, then multiplies by 100 in training code
            // Let's follow the same logic but fix the overflow issue
            float get_accuracy_result = fmax(0.0f, 100.0f - mape);
            accuracy = static_cast<uint8_t>(fmin(255.0f, get_accuracy_result * 100.0f / 100.0f)); // Simplify to match intent
            
            // Actually, let's just match the logical intent rather than the bug
            accuracy = static_cast<uint8_t>(fmin(100.0f, fmax(0.0f, 100.0f - mape)));
            
            peak_percent = 30; // A reasonable default for binary tree structures
            
            is_trained = true;
            
            Serial.printf("✅ Retraining complete! Accuracy: %d%%, Peak: %d%%\n", accuracy, peak_percent);
            Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                        coefficients[0], coefficients[1], coefficients[2]);
            Serial.printf("   MAE: %.2f, MAPE: %.2f%%\n", mae, mape);
            Serial.printf("   Split effect: %.2f, Depth effect: %.2f\n", split_effect, depth_effect);

            if(save_after_retrain) savePredictor(); // Save the new predictor
            return true;
        }
        /// @brief  add new samples to the beginning of the node_predictor_log file. 
        // If the file has more than 50 samples (rows, not including header), 
        // remove the samples at the end of the file (limit the file to the 50 latest samples)
        void add_new_samples(b_vector<node_data>& new_samples) {
            // Read all existing lines
            mcu::b_vector<String> lines;
            File file = SPIFFS.open(node_predictor_log, FILE_READ);
            if (file) {
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0) lines.push_back(line);
                }
                file.close();
            }
            // Ensure header is present
            String header = "min_split,max_depth,total_nodes";
            if (lines.empty() || lines[0] != header) {
                lines.insert(0, header);
            }
            // Remove header for easier manipulation
            mcu::b_vector<String> data_lines;
            for (size_t i = 1; i < lines.size(); ++i) {
                data_lines.push_back(lines[i]);
            }
            // Prepend new samples
            for (int i = new_samples.size() - 1; i >= 0; --i) {
                const node_data& nd = new_samples[i];
                String row = String(nd.min_split) + "," + String(nd.max_depth) + "," + String(nd.total_nodes);
                data_lines.insert(0, row);
            }
            // Limit to 50 rows
            while (data_lines.size() > 50) {
                data_lines.pop_back();
            }
            // Write back to file
            SPIFFS.remove(node_predictor_log);
            file = SPIFFS.open(node_predictor_log, FILE_WRITE);
            if (file) {
                file.println(header);
                for (const auto& row : data_lines) {
                    file.println(row);
                }
                file.close();
            }
        }

        // Check if training log is available and size of the file is greater than 0
        bool can_retrain() const {
            if (!SPIFFS.exists(node_predictor_log)) return false;
            File file = SPIFFS.open(node_predictor_log, FILE_READ);
            bool result = file && file.size() > 0;
            if (file) file.close();
            return result;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    --------------------------------------------- RF_MEMORY_LOGGER ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    class Rf_memory_logger {
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        uint32_t starting_time;
        uint8_t fragmentation;
        uint32_t lowest_ram;
        uint32_t lowest_rom; 
        uint32_t freeDisk;
        float log_time;

        Rf_memory_logger() : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
            lowest_ram = UINT32_MAX;
            lowest_rom = UINT32_MAX;
            // clear SPIFFS log file if it exists
            if(SPIFFS.exists(memory_log_file)){
                SPIFFS.remove(memory_log_file); 
            }
            // write header to log file
            File logFile = SPIFFS.open(memory_log_file, FILE_WRITE);
            if (logFile) {
                logFile.println("Time(s),FreeHeap,Fragmentation,FreeDisk");
                logFile.close();
            } 
        }
        
        void init() {
            starting_time = millis();
            log(false, true); // Initial log without printing
        }

        void log(bool print = true, bool log = true){
            freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            freeDisk = SPIFFS.totalBytes() - SPIFFS.usedBytes();

            if(freeHeap < lowest_ram) lowest_ram = freeHeap;
            if(freeDisk < lowest_rom) lowest_rom = freeDisk;

            largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            fragmentation = 100 - (largestBlock * 100 / freeHeap);
            if(print){                
                Serial.print("--> RAM LEFT (heap): ");
                Serial.println(freeHeap);
                Serial.print("Largest Free Block: ");
                Serial.println(largestBlock);
                Serial.printf("Fragmentation: %d", fragmentation);
                Serial.println("%");
            }

            // Log to file with timestamp
            if(log) {        
                log_time = (millis() - starting_time)/1000.0f; 
                File logFile = SPIFFS.open(memory_log_file, FILE_APPEND);
                if (logFile) {
                    logFile.printf("%.2f, %u, %d%%, %u\n",
                                   log_time, freeHeap, fragmentation, freeDisk);
                    logFile.close();
                }
            }
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ----------------------------------------------- RF_CATEGORIZER ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // Optional compile-time flag to disable label mapping (saves memory)
    #ifndef DISABLE_LABEL_MAPPING
    #define SUPPORT_LABEL_MAPPING 1
    #else
    #define SUPPORT_LABEL_MAPPING 0
    #endif

    // Feature type definitions for CTG v2 format
    enum FeatureType { FT_DF = 0, FT_DC = 1, FT_CS = 2, FT_CU = 3 };
    
    // Packed feature reference structure (2 bytes per feature)
    struct FeatureRef {
        uint16_t packed; // bits 15..14: type, bits 13..8: aux, bits 7..0: offset
        
        FeatureRef() : packed(0) {}
        FeatureRef(FeatureType type, uint8_t aux, uint8_t offset) {
            packed = (static_cast<uint16_t>(type) << 14) | 
                    ((static_cast<uint16_t>(aux) & 0x3F) << 8) | 
                    static_cast<uint16_t>(offset);
        }
        
        FeatureType getType() const { return static_cast<FeatureType>(packed >> 14); }
        uint8_t getAux() const { return (packed >> 8) & 0x3F; }
        uint8_t getOffset() const { return packed & 0xFF; }
    };

    class Rf_categorizer {
    private:
        uint16_t numFeatures = 0;
        uint8_t groupsPerFeature = 0;
        uint8_t numLabels = 0;
        uint32_t scaleFactor = 50000;
        String filename = "";
        bool isLoaded = false;

        // Compact storage arrays
        mcu::vector<FeatureRef> featureRefs;              // One per feature
        mcu::vector<uint16_t> sharedPatterns;             // Concatenated pattern edges
        mcu::vector<uint16_t> allUniqueEdges;             // Concatenated unique edges
        mcu::vector<uint8_t> allDiscreteValues;           // Concatenated discrete values
        
        #if SUPPORT_LABEL_MAPPING
        mcu::b_vector<String, mcu::SMALL, 8> labelMapping; // Optional label reverse mapping
        #endif
        
        // Helper function to split CSV line
        mcu::b_vector<String, mcu::SMALL> split(const String& line, char delimiter = ',') {
            mcu::b_vector<String, mcu::SMALL> result;
            int start = 0;
            int end = line.indexOf(delimiter);
            
            while (end != -1) {
                result.push_back(line.substring(start, end));
                start = end + 1;
                end = line.indexOf(delimiter, start);
            }
            result.push_back(line.substring(start));
            
            return result;
        }

        // Optimized feature categorization
        uint8_t categorizeFeature(uint16_t featureIdx, float value) const {
            if (!isLoaded || featureIdx >= numFeatures) {
                return 0;
            }
            
            const FeatureRef& ref = featureRefs[featureIdx];
            uint32_t scaledValue = static_cast<uint32_t>(value * scaleFactor + 0.5f);
            
            switch (ref.getType()) {
                case FT_DF:
                    // Full discrete range: clamp to 0..groupsPerFeature-1
                    return static_cast<uint8_t>(std::min(static_cast<int>(value), static_cast<int>(groupsPerFeature - 1)));
                    
                case FT_DC: {
                    // Discrete custom values: linear search
                    uint8_t count = ref.getAux();
                    uint8_t offset = ref.getOffset();
                    uint8_t targetValue = static_cast<uint8_t>(value);
                    
                    for (uint8_t i = 0; i < count; ++i) {
                        if (allDiscreteValues[offset + i] == targetValue) {
                            return i;
                        }
                    }
                    return 0; // Default if not found
                }
                
                case FT_CS: {
                    // Continuous shared pattern
                    uint8_t patternId = ref.getAux();
                    uint16_t baseOffset = patternId * (groupsPerFeature - 1);
                    
                    for (uint8_t bin = 0; bin < (groupsPerFeature - 1); ++bin) {
                        if (scaledValue < sharedPatterns[baseOffset + bin]) {
                            return bin;
                        }
                    }
                    return groupsPerFeature - 1; // Last bin
                }
                
                case FT_CU: {
                    // Continuous unique edges
                    uint8_t edgeCount = ref.getAux();
                    uint8_t offset = ref.getOffset();
                    uint16_t baseOffset = offset * (groupsPerFeature - 1);
                    
                    for (uint8_t bin = 0; bin < edgeCount; ++bin) {
                        if (scaledValue < allUniqueEdges[baseOffset + bin]) {
                            return bin;
                        }
                    }
                    return edgeCount; // Last bin
                }
            }
            
            return 0; // Fallback
        }
         
    public:
        Rf_categorizer() = default;
        
        Rf_categorizer(const String& csvFilename) : filename(csvFilename), isLoaded(false) {}

        void init(const String& csvFilename) {
            filename = csvFilename;
            isLoaded = false;
        }
        
        // Load categorizer data from CTG v2 format
        bool loadCategorizer(bool re_use = true) {
            if (!SPIFFS.exists(filename)) {
                Serial.println("❌ CTG2 file not found: " + filename);
                return false;
            }
            
            File file = SPIFFS.open(filename, "r");
            if (!file) {
                Serial.println("❌ Failed to open CTG2 file: " + filename);
                return false;
            }
            
            Serial.println("📂 Loading CTG2 from: " + filename);
            
            try {
                // Read header: CTG2,numFeatures,groupsPerFeature,numLabels,numSharedPatterns,scaleFactor
                if (!file.available()) {
                    Serial.println("❌ Empty CTG2 file");
                    file.close();
                    return false;
                }
                
                String headerLine = file.readStringUntil('\n');
                headerLine.trim();
                auto headerParts = split(headerLine, ',');
                
                if (headerParts.size() != 6 || headerParts[0] != "CTG2") {
                    Serial.println("❌ Invalid CTG2 header format");
                    file.close();
                    return false;
                }
                
                numFeatures = headerParts[1].toInt();
                groupsPerFeature = headerParts[2].toInt();
                numLabels = headerParts[3].toInt();
                uint16_t numSharedPatterns = headerParts[4].toInt();
                scaleFactor = headerParts[5].toInt();
                
                Serial.println("📊 Features: " + String(numFeatures) + ", Groups: " + String(groupsPerFeature) + 
                             ", Labels: " + String(numLabels) + ", Patterns: " + String(numSharedPatterns) + 
                             ", Scale: " + String(scaleFactor));
                
                // Clear existing data
                featureRefs.clear();
                sharedPatterns.clear();
                allUniqueEdges.clear();
                allDiscreteValues.clear();
                #if SUPPORT_LABEL_MAPPING
                labelMapping.clear();
                #endif
                
                // Reserve memory
                featureRefs.reserve(numFeatures);
                sharedPatterns.reserve(numSharedPatterns * (groupsPerFeature - 1));
                
                #if SUPPORT_LABEL_MAPPING
                labelMapping.reserve(numLabels);
                // Initialize label mapping with empty strings
                for (uint8_t i = 0; i < numLabels; i++) {
                    labelMapping.push_back("");
                }
                #endif
                
                // Read label mappings: L,normalizedId,originalLabel
                #if SUPPORT_LABEL_MAPPING
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.startsWith("L,")) {
                        auto parts = split(line, ',');
                        if (parts.size() >= 3) {
                            uint8_t id = parts[1].toInt();
                            String originalLabel = parts[2];
                            if (id < numLabels) {
                                labelMapping[id] = originalLabel;
                            }
                        }
                    } else {
                        // Rewind to read this line again as it's not a label
                        file.seek(file.position() - line.length() - 1);
                        break;
                    }
                }
                #else
                // Skip label lines
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (!line.startsWith("L,")) {
                        file.seek(file.position() - line.length() - 1);
                        break;
                    }
                }
                #endif
                
                // Read shared patterns: P,patternId,edgeCount,e1,e2,...
                for (uint16_t i = 0; i < numSharedPatterns; i++) {
                    if (!file.available()) {
                        Serial.println("❌ Unexpected end of file reading patterns");
                        file.close();
                        return false;
                    }
                    
                    String patternLine = file.readStringUntil('\n');
                    patternLine.trim();
                    auto parts = split(patternLine, ',');
                    
                    if (parts.size() < 3 || parts[0] != "P") {
                        Serial.println("❌ Invalid pattern line format");
                        file.close();
                        return false;
                    }
                    
                    uint16_t patternId = parts[1].toInt();
                    uint16_t edgeCount = parts[2].toInt();
                    
                    if (parts.size() != (3 + edgeCount)) {
                        Serial.println("❌ Pattern edge count mismatch");
                        file.close();
                        return false;
                    }
                    
                    // Store edges in shared pattern array
                    for (uint16_t j = 0; j < edgeCount; j++) {
                        sharedPatterns.push_back(parts[3 + j].toInt());
                    }
                }
                
                // Read feature definitions
                for (uint16_t i = 0; i < numFeatures; i++) {
                    if (!file.available()) {
                        Serial.println("❌ Unexpected end of file reading features");
                        file.close();
                        return false;
                    }
                    
                    String featureLine = file.readStringUntil('\n');
                    featureLine.trim();
                    auto parts = split(featureLine, ',');
                    
                    if (parts.size() < 1) {
                        Serial.println("❌ Invalid feature line");
                        file.close();
                        return false;
                    }
                    
                    if (parts[0] == "DF") {
                        // Discrete full range
                        featureRefs.push_back(FeatureRef(FT_DF, 0, 0));
                    } 
                    else if (parts[0] == "DC") {
                        // Discrete custom values
                        if (parts.size() < 2) {
                            Serial.println("❌ Invalid DC line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t count = parts[1].toInt();
                        if (parts.size() != (2 + count)) {
                            Serial.println("❌ DC value count mismatch");
                            file.close();
                            return false;
                        }
                        
                        uint8_t offset = allDiscreteValues.size();
                        for (uint8_t j = 0; j < count; j++) {
                            allDiscreteValues.push_back(parts[2 + j].toInt());
                        }
                        
                        featureRefs.push_back(FeatureRef(FT_DC, count, offset));
                    }
                    else if (parts[0] == "CS") {
                        // Continuous shared pattern
                        if (parts.size() != 2) {
                            Serial.println("❌ Invalid CS line format");
                            file.close();
                            return false;
                        }
                        
                        uint16_t patternId = parts[1].toInt();
                        featureRefs.push_back(FeatureRef(FT_CS, patternId, 0));
                    }
                    else if (parts[0] == "CU") {
                        // Continuous unique edges
                        if (parts.size() < 2) {
                            Serial.println("❌ Invalid CU line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t edgeCount = parts[1].toInt();
                        if (parts.size() != (2 + edgeCount)) {
                            Serial.println("❌ CU edge count mismatch");
                            file.close();
                            return false;
                        }
                        
                        uint8_t offset = allUniqueEdges.size() / (groupsPerFeature - 1);
                        for (uint8_t j = 0; j < edgeCount; j++) {
                            allUniqueEdges.push_back(parts[2 + j].toInt());
                        }
                        
                        featureRefs.push_back(FeatureRef(FT_CU, edgeCount, offset));
                    }
                    else {
                        Serial.println("❌ Unknown feature type: " + parts[0]);
                        file.close();
                        return false;
                    }
                }
                
                file.close();
                isLoaded = true;
                
                Serial.println("✅ CTG2 loaded successfully!");
                Serial.println("   Memory usage: " + String(memoryUsage()) + " bytes");
                
                // Clean up file if not reusing
                if (!re_use) {
                    SPIFFS.remove(filename);
                }
                
                return true;
                
            } catch (...) {
                Serial.println("❌ Error parsing CTG2 file");
                file.close();
                return false;
            }
        }
        
        
        // Release loaded data from memory
        void releaseCategorizer(bool re_use = true) {
            if (!isLoaded) {
                Serial.println("🧹 Categorizer already released");
                return;
            }
            
            // Clear all data structures
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            #if SUPPORT_LABEL_MAPPING
            labelMapping.clear();
            #endif
            
            isLoaded = false;
            Serial.println("🧹 Categorizer data released from memory");
        }
        // Categorize an entire sample
        mcu::packed_vector<2, SMALL> categorizeSample(const mcu::b_vector<float>& sample) const {
            if(sample.size() != numFeatures) {
                Serial.println("❌ Sample size mismatch. Expected " + String(numFeatures) + 
                             " features, got " + String(sample.size()));
                return mcu::packed_vector<2, SMALL>();
            }
            mcu::packed_vector<2, SMALL> result;
            
            if (!isLoaded) {
                Serial.println("❌ Categorizer not loaded");
                return result;
            }
            
            if (sample.size() != numFeatures) {
                Serial.println("❌ Input sample size mismatch. Expected " + String(numFeatures) + 
                             " features, got " + String(sample.size()));
                return result;
            }
            
            result.reserve(numFeatures);
            
            for (uint16_t i = 0; i < numFeatures; ++i) {
                result.push_back(categorizeFeature(i, sample[i]));
            }
            
            return result;
        }
        
        // Debug methods
        void printInfo() const {
            Serial.println("=== Rf_categorizer Categorizer Info ===");
            Serial.println("File: " + filename);
            Serial.println("Loaded: " + String(isLoaded ? "Yes" : "No"));
            Serial.println("Features: " + String(numFeatures));
            Serial.println("Groups per feature: " + String(groupsPerFeature));
            Serial.println("Labels: " + String(numLabels));
            Serial.println("Scale factor: " + String(scaleFactor));
            Serial.println("Memory usage: " + String(memoryUsage()) + " bytes");
            
            #if SUPPORT_LABEL_MAPPING
            if (isLoaded && labelMapping.size() > 0) {
                Serial.println("Label mappings:");
                for (uint8_t i = 0; i < labelMapping.size(); i++) {
                    if (labelMapping[i].length() > 0) {
                        Serial.printf("  %d -> %s\n", i, labelMapping[i].c_str());
                    } else {
                        Serial.printf("  %d: (empty)\n", i);
                    }
                }
            }
            #endif
            
            Serial.println("=================================");
        }
        
        size_t memoryUsage() const {
            size_t usage = 0;
            
            // Basic members
            usage += sizeof(numFeatures) + sizeof(groupsPerFeature) + sizeof(numLabels) + 
                    sizeof(scaleFactor) + sizeof(isLoaded);
            usage += filename.length();
            
            // Core data structures
            usage += featureRefs.size() * sizeof(FeatureRef);
            usage += sharedPatterns.size() * sizeof(uint16_t);
            usage += allUniqueEdges.size() * sizeof(uint16_t);
            usage += allDiscreteValues.size() * sizeof(uint8_t);
            
            #if SUPPORT_LABEL_MAPPING
            // Label mappings
            for (const auto& label : labelMapping) {
                usage += label.length() + sizeof(String);
            }
            #endif
            
            return usage;
        }

        String inline getOriginalLabel(uint8_t normalizedLabel) const {
            if (normalizedLabel < labelMapping.size()) {
                return labelMapping[normalizedLabel];
            }
            return String(normalizedLabel);
        }
    };
} // namespace mcu

