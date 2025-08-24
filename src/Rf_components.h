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

    using sampleID_set = mcu::ID_vector<uint16_t>;      // set of unique sample IDs
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
        void releaseTree(String base_name,  bool re_use = false) {
            if(!re_use){
                if (index == 255 || nodes.empty()) return;

                char filename[32];  // filename = "/"+ base_name + "tree_" + index + ".bin"
                snprintf(filename, sizeof(filename), "/%stree_%d.bin", base_name.c_str(), index);
                
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
    -------------------------------------------------- RF_DATA ------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    class Rf_data {
    public:
        mcu::b_vector<Rf_sample, mcu::index_size_flag::MEDIUM, 50> allSamples;  // vector of all samples, at least 50 samples
        String filename = "";
        bool   isLoaded = false;

        Rf_data() : isLoaded(false) {}
        Rf_data(const String& fname) : filename(fname), isLoaded(false) {}

    private:
        // Load data from CSV format (used only once for initial dataset conversion)
        void loadCSVData(String csvFilename, uint8_t numFeatures) {
            if(isLoaded) return;
            
            File file = SPIFFS.open(csvFilename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open CSV file for reading.");
                return;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    Serial.println("❌ CSV file is empty or missing header.");
                    file.close();
                    return;
                }
                int commaCount = 0;
                for (char c : line) {
                    if (c == ',') commaCount++;
                }
                numFeatures = commaCount;
            }

            Serial.printf("📊 Loading CSV: %s (expecting %d features per sample)\n", csvFilename.c_str(), numFeatures);
            
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

                // Add sample to vector - index becomes implicit sample ID
                allSamples.push_back(s);
                validSamples++;
                
                if (allSamples.size() >= 50000) {
                    Serial.println("⚠️  Reached sample limit (50000)");
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

    public:
        void convertCSVtoBinary(String csvFilename, uint8_t numFeatures = 0) {
            loadCSVData(csvFilename, numFeatures);
            releaseData(false); // Save to binary and clear memory
        }

        // Save data with sequential sample IDs (vector indices)
        // save to binary format : label (1 byte) | features (packed : 4 values per byte)
        void releaseData(bool reuse = true) {
            if(!isLoaded) return;
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
                    numFeatures = allSamples[0].features.size();
                }
                
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

                // Calculate packed bytes needed for features (4 values per byte)
                uint16_t packedFeatureBytes = (numFeatures + 3) / 4;

                // Write samples WITHOUT sample IDs (using vector indices)
                for (uint32_t i = 0; i < allSamples.size(); i++) {
                    const Rf_sample& s = allSamples[i];
                    
                    // Write label only (no sample ID needed)
                    file.write(&s.label, sizeof(s.label));
                    
                    // Pack and write features
                    uint8_t packedBuffer[packedFeatureBytes];
                    // Initialize buffer to 0
                    for(uint16_t j = 0; j < packedFeatureBytes; j++) {
                        packedBuffer[j] = 0;
                    }
                    
                    // Pack 4 feature values into each byte
                    for (size_t j = 0; j < s.features.size(); ++j) {
                        uint16_t byteIndex = j / 4;
                        uint8_t bitOffset = (j % 4) * 2;
                        uint8_t feature_value = s.features[j] & 0x03;
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

        // Load data using sequential indices (no sample IDs stored)
        void loadData(bool re_use = true) {
            if(isLoaded || filename.length() < 1) return;
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open binary file for reading.");
                if(SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }
                return;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                Serial.println("❌ Failed to read binary header.");
                file.close();
                return;
            }

            // Calculate packed bytes needed for features (4 values per byte)
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
            
            // Reserve space in vector
            allSamples.clear();
            allSamples.reserve(numSamples);
            
            // Read samples sequentially (no sample IDs stored)
            for(uint32_t i = 0; i < numSamples; i++) {
                Rf_sample s;
                
                // Read label (no sample ID to read)
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    Serial.printf("❌ Failed to read label for sample %u\n", i);
                    file.close();
                    return;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                // Read packed bytes
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    Serial.printf("❌ Failed to read packed features for sample %u\n", i);
                    file.close();
                    return;
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
                
                // Add to vector - index becomes implicit sample ID
                allSamples.push_back(s);
            }
            allSamples.fit();
            isLoaded = true;
            file.close();
            if(!re_use) {
                SPIFFS.remove(filename.c_str()); // Remove file after loading in single mode
            }
        }

        // overload : load a chunk of samples by indices from binary file or memory
        sample_set loadData(const mcu::ID_vector<uint16_t> &sampleIndices_bag) {
            sample_set chunkSamples;
            bool release = false;
            
            // If data is loaded in memory, extract directly from vector
            if(isLoaded) {
                for(uint16_t idx : sampleIndices_bag) {
                    if(idx < allSamples.size()) {
                        chunkSamples[idx] = allSamples[idx];
                    }
                }
                return chunkSamples;
            }
            
            // Data not in memory, need to load from file
            if(SPIFFS.exists(filename.c_str())) {
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
                chunkSamples.reserve(sampleIndices_bag.size());

                // Use a set for fast lookup
                sampleID_set sampleIndices_set;
                for (const auto& idx : sampleIndices_bag) sampleIndices_set.insert(idx);

                // Read samples sequentially 
                for(uint32_t i = 0; i < numSamples; i++) {
                    // Check if this index is requested
                    if(sampleIndices_set.find(i) == sampleIndices_set.end()) {
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
                    chunkSamples[i] = s;  // Use vector index as sample ID

                    if(chunkSamples.size() >= sampleIndices_bag.size()) break;
                }
                file.close();
            }
            return chunkSamples;
        }
        
        // add new sample to the end of the dataset
        bool add_new_sample(const Rf_sample& sample) {
            // If data is loaded, add to in-memory vector
            if (isLoaded) {
                allSamples.push_back(sample);
                allSamples.fit();
                releaseData(false); // Save to SPIFFS
                return true;
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

            // Append the new sample data (no sample ID needed)
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

        // Get total number of samples
        uint32_t size() const {
            return allSamples.size();
        }


        // FIXED: Safe data purging
        void purgeData() {
            // Clear in-memory structures first
            allSamples.clear();
            allSamples.fit();
            isLoaded = false;

            // Then remove the SPIFFS file if one was specified
            if (filename.length() > 0 && SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                Serial.printf("🗑️ Deleted file %s\n", filename.c_str());
            }
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
    -------------------------------------------------- RF_BASE -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // flags indicating the status of member files
    typedef enum Rf_base_flags : uint8_t{
        EXIST_BASE_DATA         = 0x01,           // base data file exists
        EXIST_DATA_PARAMS       = 0x02,          // data_params file exists
        EXIST_CATEGORIZER       = 0x04,         // categorizer file exists
        ABLE_TO_INFERENCE       = 0x08,        // able to run inference (all tree files exist)
        ABLE_TO_TRAINING        = 0x10        // able to re_train
    } Rf_base_flags;

    // Base file management class for Random Forest project status
    class Rf_base {
        constexpr static size_t MAX_INFER_LOGFILE_SIZE = 2048; // Max log file size in bytes - about 16000 inferences (1 bits per inference)
    private:
        uint8_t flags; // flags indicating the status of member files
        mcu::b_vector<bool> buffer; //buffer for logging inference results (with internal 16 slots on stack - fast access)
        String base_name;
    public:
        Rf_base() : flags(static_cast<Rf_base_flags>(0)), base_name("") {}
        Rf_base(const char* bn) : flags(static_cast<Rf_base_flags>(0)), base_name(bn) {
            init(bn);
        }

        void init(const char* base_name){
            if (!base_name || strlen(base_name) == 0) {
                Serial.println("❌ Base name is empty.");
                return;
            } else {
                this->base_name = String(base_name);
                
                // BaseFile will now always have the structure "/base_name_nml.bin"
                String baseFile_bin = "/" + this->base_name + "_nml.bin";
                
                // Check if binary base file exists
                if(SPIFFS.exists(baseFile_bin.c_str())) {
                    // setup flags to indicate base file exists
                    flags = static_cast<Rf_base_flags>(EXIST_BASE_DATA);
                    
                    // generate filenames for data_params and categorizer based on base_name
                    String dataParamsFile = "/" + this->base_name + "_dp.csv"; // data_params file
                    String categorizerFile = "/" + this->base_name + "_ctg.csv"; // categorizer file

                    //  check categorizer file
                    if(SPIFFS.exists(categorizerFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_CATEGORIZER);
                    } else {
                        Serial.printf("❌ No categorizer file found : %s\n", categorizerFile.c_str());
                        Serial.println("-> Model still able to re_train or run inference, but cannot re_train with new data later.");
                    }
                    // check data_params file
                    if(SPIFFS.exists(dataParamsFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_DATA_PARAMS);
                        // If data_params exists, able to training
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                    } else {
                        Serial.printf("❌ No data_parameters file found: %s\n", dataParamsFile.c_str());
                        Serial.println("Re_training and inference are not available..\n");
                    }

                    // Check for tree files to set ABLE_TO_INFERENCE flag
                    // We need to check if tree files exist (tree_0.bin, tree_1.bin, etc.)
                    bool all_trees_exist = true;
                    uint8_t tree_count = 0;
                    
                    // Check for tree files starting from tree_0.bin
                    for(uint8_t i = 0; i < 50; i++) { // Max 50 trees check
                        String treeFile = "/" + base_name + "tree_" + String(i) + ".bin";
                        if (SPIFFS.exists(treeFile.c_str())) {
                            tree_count++;
                        } else {
                            break; // Stop when we find a missing tree file
                        }
                    }
                    
                    if(tree_count > 0 && flags & EXIST_CATEGORIZER) {
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                        Serial.printf("✅ Found %d tree files, inference enabled\n", tree_count);
                    } 
                    
                } else {
                    Serial.printf("❌ Base file does not exist: %s\n", baseFile_bin.c_str());
                    // setup flags to indicate no base file
                    flags = static_cast<Rf_base_flags>(0);
                    return;
                }
            }
        }

        // Get base name
        String get_baseName() const {
            return base_name;
        }

        void set_baseName(const char* bn) {
            String old_base_name = base_name;
            if (bn && strlen(bn) > 0) {
                base_name = String(bn);
                // find and rename all existing related files

                // base file
                String old_baseFile = "/" + old_base_name + "_nml.bin";
                String new_baseFile = "/" + base_name + "_nml.bin";
                cloneFile(old_baseFile, new_baseFile);
                SPIFFS.remove(old_baseFile.c_str());

                // data_params file
                String old_dpFile = "/" + old_base_name + "_dp.csv";
                String new_dpFile = "/" + base_name + "_dp.csv";
                cloneFile(old_dpFile, new_dpFile);
                SPIFFS.remove(old_dpFile.c_str());

                // categorizer file
                String old_ctgFile = "/" + old_base_name + "_ctg.csv";
                String new_ctgFile = "/" + base_name + "_ctg.csv";
                cloneFile(old_ctgFile, new_ctgFile);
                SPIFFS.remove(old_ctgFile.c_str());

                // inference log file
                String old_logFile = "/" + old_base_name + "_infer_log.bin";
                String new_logFile = "/" + base_name + "_infer_log.bin";
                cloneFile(old_logFile, new_logFile);
                SPIFFS.remove(old_logFile.c_str());

                // tree files
                for(uint8_t i = 0; i < 50; i++) { // Max 50 trees check
                    String old_treeFile = "/" + old_base_name + "tree_" + String(i) + ".bin";
                    String new_treeFile = "/" + base_name + "tree_" + String(i) + ".bin";
                    if (SPIFFS.exists(old_treeFile.c_str())) {
                        cloneFile(old_treeFile, new_treeFile);
                        SPIFFS.remove(old_treeFile.c_str());
                    }else{
                        break; // Stop when we find a missing tree file
                    }
                }
                // Re-initialize flags based on new base name
                init(base_name.c_str());  
            }
        }

        // Get binary base file path (/base_name_nml.bin)
        String get_baseFile() const {
            return "/" + base_name + "_nml.bin";
        }

        // generate data_params filename based on base_name
        String get_dpFile() const {
            return "/" + base_name + "_dp.csv";
        }

        // generate categorizer filename based on base_name
        String get_ctgFile() const {
            return "/" + base_name + "_ctg.csv";
        }

        // generate inference log filename based on base_name
        String get_inferenceLogFile() const {
            return "/" + base_name + "_infer_log.bin";
        }

        // Check if base file is in CSV format (always false now as we only use binary)
        bool baseFile_is_csv() const {
            return false; // Always binary format now
        }

        bool baseFile_exists() const {
            return (flags & EXIST_BASE_DATA) != 0;
        }

        bool dataParams_exists() const {
            return (flags & EXIST_DATA_PARAMS) != 0;
        }
        
        bool categorizer_exists() const {
            return (flags & EXIST_CATEGORIZER) != 0;
        }

        // Fast check for able to training
        bool able_to_training() const {
            return (flags & ABLE_TO_TRAINING) != 0;
        }

        // fast check for able to inference 
        bool able_to_inference(uint8_t num_trees) const {
            return (flags & ABLE_TO_INFERENCE) != 0;
        }

    private:
        // Helper method to trim log file by removing oldest bytes
        void trim_log_file(const String& logFile, size_t bytes_to_remove) {
            if(!SPIFFS.exists(logFile.c_str())) {
                return;
            }
            
            File originalFile = SPIFFS.open(logFile.c_str(), FILE_READ);
            if(!originalFile) {
                Serial.printf("❌ Failed to open log file for trimming: %s\n", logFile.c_str());
                return;
            }
            
            size_t original_size = originalFile.size();
            if(original_size <= bytes_to_remove) {
                // If file is smaller than or equal to bytes to remove, just delete it
                originalFile.close();
                SPIFFS.remove(logFile.c_str());
                Serial.printf("🗑️ Removed entire log file (%zu bytes <= %zu bytes to remove)\n", 
                            original_size, bytes_to_remove);
                return;
            }
            
            // Create temporary file for trimmed data
            String tempFile = logFile + ".tmp";
            File trimmedFile = SPIFFS.open(tempFile.c_str(), FILE_WRITE);
            if(!trimmedFile) {
                Serial.printf("❌ Failed to create temporary file: %s\n", tempFile.c_str());
                originalFile.close();
                return;
            }
            
            // Skip the first bytes_to_remove bytes (oldest data)
            originalFile.seek(bytes_to_remove);
            
            // Copy remaining data to temporary file
            uint8_t buffer_chunk[64]; // Use small buffer for ESP32 memory constraints
            size_t bytes_copied = 0;
            while(originalFile.available()) {
                size_t bytes_to_read = min(sizeof(buffer_chunk), (size_t)originalFile.available());
                size_t bytes_read = originalFile.read(buffer_chunk, bytes_to_read);
                if(bytes_read > 0) {
                    trimmedFile.write(buffer_chunk, bytes_read);
                    bytes_copied += bytes_read;
                } else {
                    break;
                }
            }
            
            originalFile.close();
            trimmedFile.close();
            
            // Replace original file with trimmed file
            SPIFFS.remove(logFile.c_str());
            if(SPIFFS.rename(tempFile.c_str(), logFile.c_str())) {
                Serial.printf("✂️ Trimmed log file: removed %zu bytes, kept %zu bytes\n", 
                            bytes_to_remove, bytes_copied);
            } else {
                Serial.printf("❌ Failed to rename trimmed file from %s to %s\n", 
                            tempFile.c_str(), logFile.c_str());
                SPIFFS.remove(tempFile.c_str()); // Clean up temp file
            }
        }

    public:
        void log_inference(bool result){
            if(buffer.size() < 16){     // just log when buffer is full(16 results), avoid file write too often
                buffer.push_back(result);       
            } else {
                // Buffer full, write to file and clear buffer
                String logFile = get_inferenceLogFile();
                
                // Check if log file needs size management
                bool need_trim = false;
                if(SPIFFS.exists(logFile.c_str())) {
                    File checkFile = SPIFFS.open(logFile.c_str(), FILE_READ);
                    if(checkFile) {
                        size_t current_size = checkFile.size();
                        checkFile.close();
                        
                        if(current_size >= MAX_INFER_LOGFILE_SIZE) {
                            need_trim = true;
                            Serial.printf("📏 Log file size (%zu bytes) exceeds limit (%zu bytes), trimming oldest 128 results\n", 
                                        current_size, MAX_INFER_LOGFILE_SIZE);
                        }
                    }
                }
                
                // If file needs trimming, remove oldest 128 inference results (16 bytes)
                if(need_trim) {
                    trim_log_file(logFile, 16); // Remove 16 bytes (128 bits = 128 inference results)
                }
                
                File file = SPIFFS.open(logFile.c_str(), FILE_APPEND);
                if (!file) {
                    // If file doesn't exist, create it
                    file = SPIFFS.open(logFile.c_str(), FILE_WRITE);
                    if (!file) {
                        Serial.printf("❌ Failed to create inference log file: %s\n", logFile.c_str());
                        return;
                    }
                }

                // Pack 16 boolean values into 2 bytes (8 bits per byte)
                uint8_t packed_bytes[2] = {0, 0};
                for(uint8_t i = 0; i < 16; i++) {
                    if(buffer[i]) {
                        uint8_t byte_index = i / 8;      // Which byte (0 or 1)
                        uint8_t bit_index = i % 8;       // Which bit in that byte
                        packed_bytes[byte_index] |= (1 << bit_index);
                    }
                }
                
                // Write packed data to file
                file.write(packed_bytes, 2);
                file.close();
                
                // Clear buffer and add current result
                buffer.clear();
                buffer.push_back(result);
                
                Serial.printf("📝 Logged 16 inference results to %s\n", logFile.c_str());
            }
        }

        // Get inference statistics: total inferences and correct inferences
        mcu::pair<size_t, size_t> get_inference_stats() const {
            size_t total_inferences = 0;
            size_t correct_inferences = 0;
            
            String logFile = get_inferenceLogFile();
            if(!SPIFFS.exists(logFile.c_str())) {
                return mcu::make_pair(total_inferences, correct_inferences);
            }
            
            File file = SPIFFS.open(logFile.c_str(), FILE_READ);
            if(!file) {
                return mcu::make_pair(total_inferences, correct_inferences);
            }
            
            uint8_t packed_bytes[2];
            while(file.available() >= 2) {
                if(file.read(packed_bytes, 2) == 2) {
                    // Unpack and count
                    for(uint8_t i = 0; i < 16; i++) {
                        uint8_t byte_index = i / 8;
                        uint8_t bit_index = i % 8;
                        bool result = (packed_bytes[byte_index] & (1 << bit_index)) != 0;
                        
                        total_inferences++;
                        if(result) {
                            correct_inferences++;
                        }
                    }
                }
            }
            
            // Add current buffer results
            for(uint8_t i = 0; i < buffer.size(); i++) {
                total_inferences++;
                if(buffer[i]) {
                    correct_inferences++;
                }
            }
            file.close();
            return mcu::make_pair(total_inferences, correct_inferences);
        }
    };

} // namespace mcu

