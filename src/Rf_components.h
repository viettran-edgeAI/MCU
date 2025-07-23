#include "STL_MCU.h"  
#include "Rf_file_manager.h"
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"


// Forward declaration for callback
class RandomForest;

struct Rf_sample{
    mcu::packed_vector<2, mcu::SMALL> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
    uint8_t label;                     // label of the sample 
};

using OOB_set = mcu::ChainedUnorderedSet<uint16_t>; // OOB set type
using sampleID_set = mcu::ChainedUnorderedSet<uint16_t>; // Sample ID set type
using sample_set = mcu::ChainedUnorderedMap<uint16_t, Rf_sample>; // set of samples

struct Tree_node{
    uint8_t featureID;                 
    uint8_t packed_data;               // threshold(2) + label(5) + is_leaf(1)
    mcu::pair<Tree_node*, Tree_node*> children; // left and right children for binary split
    
    // Getter methods for packed data
    uint8_t getThreshold() const { return packed_data & 0x03; }                    // bits 0-1
    uint8_t getLabel() const { return (packed_data >> 2) & 0x1F; }                 // bits 2-6
    bool getIsLeaf() const { return (packed_data >> 7) & 0x01; }                   // bit 7
    
    // Setter methods for packed data
    void setThreshold(uint8_t threshold) { 
        packed_data = (packed_data & 0xFC) | (threshold & 0x03); 
    }
    void setLabel(uint8_t label) { 
        packed_data = (packed_data & 0x83) | ((label & 0x1F) << 2); 
    }
    void setIsLeaf(bool is_leaf) { 
        packed_data = (packed_data & 0x7F) | (is_leaf ? 0x80 : 0x00); 
    }
    
    Tree_node() : featureID(0), packed_data(0), children({nullptr, nullptr}) {}
};

class Rf_tree {
  public:
    Tree_node* root = nullptr;
    String filename;
    bool isLoaded = false;
    bool isPurged = false; // Flag to indicate if the tree has been purged

    Rf_tree() : filename(""), isLoaded(true) {}
    
    Rf_tree(const String& fn) : filename(fn), isLoaded(false) {}

    ~Rf_tree() {
        if (!isPurged) { // Only clean up if not already purged
            if (isLoaded && filename.length()) {
                releaseTree();
            }
            clearTree();
        }
    }

    void clearTree() {
        if (root) {
            clearTreeRecursive(root);
            root = nullptr;
        }
        isLoaded = false;
    }

    // Optimized tree loading with memory monitoring
    void loadTree(bool reuse = true) {
        if (isLoaded || !filename.length()) return;
        
        // Check available RAM before loading
        if (ESP.getFreeHeap() < 4000) {
            Serial.printf("⚠️ Low memory (%d bytes), skipping tree load: %s\n", 
                         ESP.getFreeHeap(), filename.c_str());
            return;
        }
        
        File file = SPIFFS.open(filename.c_str(), FILE_READ);
        if (!file) {
            Serial.printf("❌ Failed to load tree: %s\n", filename.c_str());
            return;
        }

        // Read and verify header
        uint32_t magic = 0;
        if (file.readBytes((char*)&magic, 4) != 4 || magic != 0x54524545) {
            Serial.printf("❌ Invalid tree header: %s\n", filename.c_str());
            file.close();
            return;
        }

        // Load tree structure
        root = loadNodeFromFile(file);
        file.close();
        if(!reuse) {
            // Remove the file after loading if not reusing
            if (SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                // Serial.printf("✅ Tree file removed after loading: %s\n", filename.c_str());
            }
        }
        
        if (root) {
            isLoaded = true;
        }
    }

    // Optimized tree saving with compact format
    void releaseTree(bool reuse = true) {
        if (!isLoaded || !filename.length() || !root) return;
        
        File file = SPIFFS.open(filename.c_str(), FILE_WRITE);

        if (!file) {
            Serial.printf("❌ Failed to save tree: %s\n", filename.c_str());
            return;
        }

        // Write header
        uint32_t magic = 0x54524545;
        file.write((uint8_t*)&magic, 4);

        // Save tree structure
        saveNodeToFile(file, root);
        file.close();

        // Clear from RAM
        clearTree();
    }

    // Direct prediction without loading entire tree (streaming)
    uint8_t predictSample(const Rf_sample& sample) {
        if (!root || !isLoaded) return 255;     // invalid prediction result
        return predictRecursive(root, sample);
    }

  private:
    uint8_t predictRecursive(Tree_node* node,const Rf_sample& sample) {
        if (node->getIsLeaf()) {
            return node->getLabel();
        }
        
        uint8_t sampleValue = sample.features[node->featureID];
        uint8_t threshold = node->getThreshold();
        
        // Binary split: left child (<=threshold), right child (>threshold)
        if (sampleValue <= threshold) {
            if (node->children.first) {
                return predictRecursive(node->children.first, sample);
            }
        } else {
            if (node->children.second) {
                return predictRecursive(node->children.second, sample);
            }
        }
        
        // Fallback to node's label if child is null
        return node->getLabel();
    }

    void clearTreeRecursive(Tree_node* node) {
        if (!node) return;
        
        if (node->children.first) {
            clearTreeRecursive(node->children.first);
        }
        if (node->children.second) {
            clearTreeRecursive(node->children.second);
        }
        delete node;
    }

    Tree_node* loadNodeFromFile(File& file) {
        uint8_t featureID, packed_data;
        
        if (file.readBytes((char*)&featureID, 1) != 1 ||
            file.readBytes((char*)&packed_data, 1) != 1) {
            return nullptr;
        }
        
        Tree_node* node = new Tree_node();
        node->featureID = featureID;
        node->packed_data = packed_data;

        // Load children with memory check
        if (!node->getIsLeaf() && ESP.getFreeHeap() > 2000) {
            // Load left child
            node->children.first = loadNodeFromFile(file);
            if (!node->children.first) {
                delete node;
                return nullptr;
            }
            
            // Load right child
            node->children.second = loadNodeFromFile(file);
            if (!node->children.second) {
                clearTreeRecursive(node->children.first);
                delete node;
                return nullptr;
            }
        } else if (!node->getIsLeaf()) {
            // Skip children if low memory - convert to leaf
            skipNodeInFile(file); // Skip left child
            skipNodeInFile(file); // Skip right child
            node->setIsLeaf(true);
        }

        return node;
    }

    bool saveNodeToFile(File& file, Tree_node* node) {
        if (file.write(node->featureID) != 1 ||
            file.write(node->packed_data) != 1) {
            return false;
        }

        // Save children if not a leaf
        if (!node->getIsLeaf()) {
            if (!saveNodeToFile(file, node->children.first) ||
                !saveNodeToFile(file, node->children.second)) {
                return false;
            }
        }
        
        return true;
    }
    
    void skipNodeInFile(File& file) {
        uint8_t featureID, packed_data;
        if (file.readBytes((char*)&featureID, 1) == 1 &&
            file.readBytes((char*)&packed_data, 1) == 1) {
            
            Tree_node temp;
            temp.packed_data = packed_data;
            
            if (!temp.getIsLeaf()) {
                skipNodeInFile(file); // Skip left child
                skipNodeInFile(file); // Skip right child
            }
        }
    }
    
  public:
    void purgeTree() {
        if (root) {
            clearTreeRecursive(root);
            root = nullptr;
        }
        isLoaded = false;
        isPurged = true; // Set purged flag
        // remove tree file from SPIFFS
        if(filename){
            if (filename.length() && SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                Serial.printf("🗑️ Deleted file %s\n", filename.c_str());
            } 
        }
        filename = ""; // Clear filename
    }
};

// flag for Rf_data : base data, training data, subset data (for each tree in forest), etc.
typedef enum Rf_data_flags{
    BASE_DATA = 0,          // base data, used for initial training
    TRAINING_DATA,          // training data, used for training the forest
    SUBSET_DATA,            // subset data, used for each tree in the forest
    TESTING_DATA,          // testing data, used for evaluating the model
    VALIDATION_DATA      // validation data, used for model validation
}Rf_data_flags;

class Rf_data {
  public:
    mcu::ChainedUnorderedMap<uint16_t, Rf_sample> allSamples;    // all sample and it's ID 
    Rf_data_flags flag;
    static void (*restore_data_callback)(Rf_data_flags&, uint8_t);
    String filename;      
    bool   isLoaded = false;

    Rf_data() : filename(""){}
    Rf_data(const String& fn) : filename(fn), isLoaded(false) {}

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
            
            if (sampleID >= 10000) {
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
    void releaseData(bool reuse = false) {
        if(!isLoaded || !filename.length()) return;
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
    // load from binary format : sampleID (2 bytes) | label (1 byte) | features (packed : 4 values per byte)
    void loadData(bool reuse = false) {
        if(isLoaded || !filename.length()) return;
        bool restore_yet = false;

        //extract tree_index from filename : tree_0_data.bin -> 0
        uint8_t treeIndex = 0;
        if(flag == SUBSET_DATA) {
            int tree_index = filename.indexOf('_');
            if(tree_index > 0) {
                String index_str = filename.substring(tree_index + 1, filename.indexOf('_', tree_index + 1));
                if(index_str.length() > 0) {
                    treeIndex = static_cast<uint8_t>(index_str.toInt());
                }
            }
        }
        
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
        if(!reuse) {
            if (SPIFFS.exists(filename.c_str())) {
                Serial.printf("Removing binary file: %s\n", filename.c_str());
                SPIFFS.remove(filename.c_str());
            }
        }
    }

    // overload : load a chunk of data instead of all data
    sample_set loadData(mcu::b_vector<uint16_t>& sampleIDs_bag) {
        sample_set chunkSamples;
        if(isLoaded || !filename.length()) return chunkSamples;
        sampleIDs_bag.sort();

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
            loadData(true);
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
        if (!filename.length()) {
            Serial.println("❌ addSample: No filename specified.");
            return false;
        }

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
        if (filename.length()) {
            if (SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                Serial.printf("🗑️ Deleted file %s\n", filename.c_str());
            }
            filename = "";
        }
    }
};

typedef enum Rf_training_flags : uint8_t{
    EARLY_STOP  = 0x00,        // early stop training if accuracy is not improving
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_training_flags;

