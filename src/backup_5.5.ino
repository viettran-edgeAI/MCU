#include "STL_MCU.h"  
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"


using namespace mcu;
bool cloneCSVFile(const String& src, const String& dest);
void checkHeapFragmentation();

static int lowest_ram = 9999999; // lowest RAM usage during forest creation and training
static int min_largest_block; // minimum largest block size during forest creation and training
static int lowest_rom;


typedef struct Rf_sample{
    packed_vector<2, SMALL> features;           // set containing the values ​​of the features corresponding to that sample 
    uint8_t label;                     // label of the sample 
}Rf_sample;

typedef struct Tree_node{
    uint8_t featureID;                 
    uint8_t branchValue;                
    uint8_t label;                
    b_vector<Tree_node*, SMALL> children;        
    bool is_leaf = false;
} Tree_node;

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
    uint8_t predictSample(Rf_sample& sample) {
        if (!isLoaded) {
            return streamPredict(sample);  // Use streaming prediction
        }
        
        if (!root) return 255;
        return predictRecursive(root, sample);
    }

  private:
    // Streaming prediction - read tree nodes on-demand from SPIFFS
    uint8_t streamPredict(Rf_sample& sample) {
        if (!filename.length()) return 255;
        
        File file = SPIFFS.open(filename.c_str(), FILE_READ);
        if (!file) return 255;

        // Skip header
        uint32_t magic;
        file.readBytes((char*)&magic, 4);

        uint8_t result = streamPredictNode(file, sample);
        file.close();
        return result;
    }

    uint8_t streamPredictNode(File& file, Rf_sample& sample) {
        // Read node data (5 bytes total)
        uint8_t flags, featureID, branchValue, label, numChildren;
        
        if (file.readBytes((char*)&flags, 1) != 1 ||
            file.readBytes((char*)&featureID, 1) != 1 ||
            file.readBytes((char*)&branchValue, 1) != 1 ||
            file.readBytes((char*)&label, 1) != 1 ||
            file.readBytes((char*)&numChildren, 1) != 1) {
            return 255;
        }

        bool is_leaf = (flags & 0x01) != 0;
        
        if (is_leaf) {
            // Skip remaining children in file
            for (uint8_t i = 0; i < numChildren; i++) {
                skipNodeInFile(file);
            }
            return label;
        }

        // Find matching child
        uint8_t sampleValue = sample.features[featureID];
        
        for (uint8_t i = 0; i < numChildren; i++) {
            // Peek at child's branch value
            long pos = file.position();
            uint8_t childFlags, childFeatureID, childBranchValue;
            
            if (file.readBytes((char*)&childFlags, 1) == 1 &&
                file.readBytes((char*)&childFeatureID, 1) == 1 &&
                file.readBytes((char*)&childBranchValue, 1) == 1) {
                
                if (childBranchValue == sampleValue) {
                    // Reset to start of this child and recurse
                    file.seek(pos);
                    return streamPredictNode(file, sample);
                }
            }
            
            // Skip this child
            file.seek(pos);
            skipNodeInFile(file);
        }
        
        return label; // Default to current node's label
    }

    void skipNodeInFile(File& file) {
        uint8_t numChildren;
        file.seek(file.position() + 4); // Skip flags, featureID, branchValue, label
        if (file.readBytes((char*)&numChildren, 1) == 1) {
            for (uint8_t i = 0; i < numChildren; i++) {
                skipNodeInFile(file);
            }
        }
    }

    uint8_t predictRecursive(Tree_node* node, Rf_sample& sample) {
        if (node->is_leaf) {
            return node->label;
        }
        
        uint8_t sampleValue = sample.features[node->featureID];
        
        for (Tree_node* child : node->children) {
            if (child->branchValue == sampleValue) {
                return predictRecursive(child, sample);
            }
        }
        
        return node->label;
    }

    void clearTreeRecursive(Tree_node* node) {
        if (!node) return;
        
        for (Tree_node* child : node->children) {
            clearTreeRecursive(child);
        }
        delete node;
    }

    Tree_node* loadNodeFromFile(File& file) {
        uint8_t flags, numChildren;
        
        if (file.readBytes((char*)&flags, 1) != 1) return nullptr;
        
        Tree_node* node = new Tree_node();
        
        if (file.readBytes((char*)&node->featureID, 1) != 1 ||
            file.readBytes((char*)&node->branchValue, 1) != 1 ||
            file.readBytes((char*)&node->label, 1) != 1 ||
            file.readBytes((char*)&numChildren, 1) != 1) {
            delete node;
            return nullptr;
        }

        node->is_leaf = (flags & 0x01) != 0;

        // Load children with memory check
        if (numChildren > 0 && ESP.getFreeHeap() > 2000) {
            node->children.reserve(numChildren);
            for (uint8_t i = 0; i < numChildren; i++) {
                Tree_node* child = loadNodeFromFile(file);
                if (!child) {
                    delete node;
                    return nullptr;
                }
                node->children.push_back(child);
            }
            node->children.fit();
        } else if (numChildren > 0) {
            // Skip children if low memory
            for (uint8_t i = 0; i < numChildren; i++) {
                skipNodeInFile(file);
            }
            node->is_leaf = true; // Force leaf behavior
        }

        return node;
    }

    bool saveNodeToFile(File& file, Tree_node* node) {
        uint8_t flags = node->is_leaf ? 0x01 : 0x00;
        uint8_t numChildren = static_cast<uint8_t>(node->children.size());
        
        if (file.write(flags) != 1 ||
            file.write(node->featureID) != 1 ||
            file.write(node->branchValue) != 1 ||
            file.write(node->label) != 1 ||
            file.write(numChildren) != 1) {
            return false;
        }

        for (Tree_node* child : node->children) {
            if (!saveNodeToFile(file, child)) return false;
        }
        
        return true;
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
        // Serial.println("✅ Tree purged from memory and SPIFFS.");
    }
};

// flag for Rf_data : base data, training data, subset data (for each tree in forest), etc.
typedef enum rf_flag{
    BASE_DATA = 0,          // base data, used for initial training
    TRAINING_DATA,          // training data, used for training the forest
    SUBSET_DATA,            // subset data, used for each tree in the forest
    TESTING_DATA           // testing data, used for evaluating the model
}rf_flag;

class Rf_data {
  public:
    ChainedUnorderedMap<Rf_sample> allSamples;    // all sample and it's ID 
    rf_flag flag;
    static void (*restore_data_callback)(rf_flag&, uint8_t);
    String filename;      
    bool   isLoaded = false;

    Rf_data() : filename(""){}
    Rf_data(const String& fn) : filename(fn), isLoaded(false) {}

    // Load data from CSV format (used only once for initial dataset loading)
    void loadCSVData(String csvFilename, uint8_t numFeatures) {
        if(isLoaded) return;
        
        File file = SPIFFS.open(csvFilename.c_str(), FILE_READ);
        if (!file) {
            Serial.println("❌ Failed to open CSV file for reading.");
            return;
        }

        uint16_t sampleID = 0;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

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
            s.features.fit();

            allSamples[sampleID] = s;
            if (sampleID++ >= 10000) break;
        }
        allSamples.fit();
        file.close();
        isLoaded = true;
        SPIFFS.remove(csvFilename);
        Serial.println("✅ CSV data loaded from file.");
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
    vector<pair<uint16_t,Rf_sample>> loadData(vector<uint16_t> sampleIDs_bag) {
        vector<pair<uint16_t,Rf_sample>> chunkSamples;
        if(isLoaded || !filename.length()) return chunkSamples;;
        sampleIDs_bag.sort();
        
        File file = SPIFFS.open(filename.c_str(), FILE_READ);
        if (!file) {
            Serial.println("❌ Failed to open binary file for reading.");
            return chunkSamples; // Return empty vector on error
        }

        // Read binary header
        uint32_t numSamples;
        uint16_t numFeatures;
            
        if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
        file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
            Serial.println("❌ Failed to read binary header.");
            return chunkSamples; // Return empty vector on error
        }

            // Calculate packed bytes needed for features (4 values per byte)
        uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
            
        // ✅ Read samples with original IDs
        uint16_t cursor = 0;
        chunkSamples.reserve(sampleIDs_bag.size()); // Reserve space for expected number of samples
        for(uint16_t row = 0; row < numSamples; row++) {
            if(row != sampleIDs_bag[cursor]) {
                // If current row ID does not match requested sample ID, skip it
                uint16_t originalId;
                file.read((uint8_t*)&originalId, sizeof(originalId));
                file.seek(file.position() + sizeof(uint8_t) + packedFeatureBytes); // Skip label and features
                continue;
            }else{
                uint16_t originalId;  // ✅ Read original sample ID
                Rf_sample s;
                cursor++; // Move to next requested sample ID
                
                // ✅ Read original ID first
                if(file.read((uint8_t*)&originalId, sizeof(originalId)) != sizeof(originalId)) {
                    Serial.printf("❌ Failed to read sample ID for sample %u\n", row);
                    chunkSamples.clear(); // Clear vector on error
                    chunkSamples.fit();
                    return chunkSamples; // Return empty vector on error
                }
                    
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    Serial.printf("❌ Failed to read label for sample %u\n", row);
                    chunkSamples.clear(); // Clear vector on error
                    chunkSamples.fit();
                    return chunkSamples; // Return empty vector on error
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                    
                // Read packed bytes
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    Serial.printf("❌ Failed to read packed features for sample %u\n", row);
                    chunkSamples.clear(); // Clear vector on error
                    chunkSamples.fit();
                    return chunkSamples;
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
                chunkSamples.push_back(make_pair(originalId, s));  
                if(chunkSamples.size() >= sampleIDs_bag.size()) {
                    break; // Stop if we have loaded all requested samples
                }
            }
        }
        if(file){
            file.close();
        }
        return chunkSamples; // Return the loaded chunk
    }
    // repeat a number of samples to reach a certain number of samples: boostrap sampling
    void boostrapData(uint16_t numSamples, uint16_t maxSamples){
        bool preloaded = isLoaded;
        if(!isLoaded){
            loadData(true);
        }
        uint16_t currentSize = allSamples.size();
        vector<uint16_t> sampleIDs;
        sampleIDs.reserve(currentSize);
        for (const auto& entry : allSamples) {
            sampleIDs.push_back(entry.first); // Store original IDs
        }
        sampleIDs.sort(); // Sort IDs for consistent access
        uint16_t cursor = 0;
        vector<uint16_t> newSampleIDs;
        for(uint16_t i = 0; i<maxSamples; i++){
            if(sampleIDs[cursor] != i){
                newSampleIDs.push_back(i); // Add missing IDs
            }else{
                cursor++;
            }
        }
        if(currentSize >= numSamples) {
            Serial.printf("Data already has %d samples, no need to boostrap.\n", currentSize);
            return;
        }
        Serial.printf("Boostraping data from %d to %d samples...\n", currentSize, numSamples);
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
            uint16_t newID = newSampleIDs.back();
            allSamples[newID] = sample; // Use new ID
            newSampleIDs.pop_back(); // Remove used ID

        }
        if(!preloaded) {
            releaseData(true); // Save to SPIFFS if not preloaded
        }
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
void (*Rf_data::restore_data_callback)(rf_flag&, uint8_t) = nullptr;


typedef enum trainingFlags{
    ACCURACY = 0x01,          // calculate accuracy of the model
    PRECISSION = 0x02,          // calculate precision of the model
    RECALL = 0x04,             // calculate recall of the model
    F1_SCORE = 0x08,           // calculate F1 score of the model
}trainingFlags;

// -------------------------------------------------------------------------------- 
class RandomForest{
public:
    Rf_data a;      // base data
    Rf_data train_data;
    Rf_data test_data;
    String baseFile;            // the base csv file 
    uint16_t maxDepth;
    uint8_t minSplit;
    uint8_t numTree;     
    uint8_t numFeatures;  
    uint8_t numLabels;
    uint16_t numSamples;  // number of samples in the base data
    vector<Rf_tree, SMALL> root;                     // vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<uint8_t> allFeaturesValue;     // value of all features
    vector<Rf_data> dataList;
    vector<ChainedUnorderedSet<uint16_t>> OOB;          // OOB set for each tree
    vector<uint16_t> train_backup;   // backup of training set sample IDs 
    float unity_threshold ;          // unity_threshold  for classification, effect to precision and recall
    trainingFlags trainFlag = trainingFlags::ACCURACY; // flags for training, default is accuracy
    // Pointer to the single instance
    static RandomForest* instance_ptr;
    bool useGini = true;
    bool boostrap = false;

    RandomForest(){};
    RandomForest(String baseFile, int numtree, int max_depth, int min_split){
        this->baseFile = baseFile;
        int dot_index = baseFile.lastIndexOf('.');
        String baseName = baseFile.substring(0,dot_index);
        String extension = baseFile.substring(dot_index);
        String backup_file = baseName + "_2" + extension;
        cloneCSVFile(baseFile, backup_file);
        first_scan(backup_file, false);
        
        instance_ptr = this; // Set the static instance pointer
        Rf_data::restore_data_callback = &RandomForest::static_restore_data;

        // Load CSV data once and convert to binary format
        a.loadCSVData(backup_file, numFeatures);
        a.flag = rf_flag::BASE_DATA;
        
        unity_threshold  = 1.25f / static_cast<float>(numFeatures);
        if(numFeatures == 2) unity_threshold  = 0.4f;
        maxDepth = max_depth;
        minSplit = min_split;
        numTree = numtree;
        
        OOB.reserve(numTree);
        dataList.reserve(numTree);

        splitData(0.7, "train", "test");

        ClonesData(train_data, numTree);
    }
    
      // Enhanced destructor
      ~RandomForest(){
          // Clear forest safely
          for(auto& tree : root){
            tree.purgeTree();
          }
          
          // Clear data safely
          train_data.purgeData();
          test_data.purgeData();
          // a.purgeData();
        
          OOB.clear();
          for(auto& data : dataList){
            data.purgeData();
          }
          dataList.clear();
          allFeaturesValue.clear();

      }


    void MakeForest(){
        // Clear any existing forest first
        clearForest();
        
        Serial.println("START MAKING FOREST...");
        
        root.reserve(numTree);
        
        for(uint8_t i = 0; i < numTree; i++){
            dataList[i].loadData(true);
            Serial.printf("building sub_tree: %d\n", i);
            // checkHeapFragmentation();
            Tree_node* rootNode = buildTree(dataList[i], minSplit, maxDepth, useGini);
            
            // Create SPIFFS filename for this tree
            String treeFilename = "/tree_" + String(i) + ".bin";
            Rf_tree tree(treeFilename);
            tree.root = rootNode;
            tree.isLoaded = true; // Mark as loaded since we just built it
            tree.releaseTree(); // Save tree to SPIFFS
            // Add to root vector (tree is now in SPIFFS)
            root.push_back(tree);
            
            // Release sub-data after tree creation
            dataList[i].releaseData(true);
            
            Serial.printf("Tree %d saved to SPIFFS: %s\n", i, treeFilename.c_str());
            // Serial.printf("===> RAM left: %d\n", ESP.getFreeHeap());
            // Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        }
        Serial.printf("RAM after forest creation: %d\n", ESP.getFreeHeap());
    }

  private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets
    void splitData(float trainRatio, const char* extension_1, const char* extension_2) {
        Serial.println("<-- split data -->");
    
        uint16_t totalSamples = this->a.allSamples.size();
        uint16_t trainSize = static_cast<uint16_t>(totalSamples * trainRatio);
        
        ChainedUnorderedSet<uint16_t> train_sampleIDs;
        ChainedUnorderedSet<uint16_t> test_sampleIDs;
        train_sampleIDs.reserve(trainSize);
        test_sampleIDs.reserve(totalSamples - trainSize);

        train_backup.clear(); // Clear previous backup
        train_backup.reserve(totalSamples - trainSize);

        while (train_sampleIDs.size() < trainSize) {
            uint16_t sampleId = static_cast<uint16_t>(esp_random() % totalSamples);
            train_sampleIDs.insert(sampleId);
        }
        for(auto sampleID : train_sampleIDs){
          train_backup.push_back(sampleID);
        }
        train_sampleIDs.fit();
        train_backup.sort();

        for(uint16_t i = 0; i < totalSamples; ++i) {
            if (train_sampleIDs.find(i) == train_sampleIDs.end()) {
                test_sampleIDs.insert(i);
            }
        }
        test_sampleIDs.fit();

        // Extract base name from filename
        String originalName = String(this->a.filename);
        if (originalName.startsWith("/")) {
            originalName = originalName.substring(1);
        }
        // Remove extension (.bin)
        int dotIndex = originalName.lastIndexOf('.');
        if (dotIndex > 0) {
            originalName = originalName.substring(0, dotIndex);
        }

        // Create binary filenames
        String trainFilename = "/" + originalName + extension_1 + ".bin";
        String testFilename  = "/" + originalName + extension_2 + ".bin";

        train_data.filename = trainFilename;
        test_data.filename = testFilename;

        train_data.isLoaded = true;
        test_data.isLoaded = true;

        train_data.flag = TRAINING_DATA;
        test_data.flag = TESTING_DATA;
        train_data.allSamples.reserve(trainSize);
        test_data.allSamples.reserve(totalSamples - trainSize);

        Serial.printf("Number of samples in train set: %d\n", trainSize);
        Serial.printf("Number of samples in test set: %d\n", test_sampleIDs.size());

        // Serial.println("Train sample IDs:");
        // Copy train samples
        for(auto sampleId : train_sampleIDs){
            train_data.allSamples[sampleId] = this->a.allSamples[sampleId];
            // Serial.printf("%d ", sampleId);
        }
        // checkHeapFragmentation();
        Serial.printf("===> RAM left: %d\n", ESP.getFreeHeap());
        Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        // Serial.println("\nTrain sample IDs:");
        // for(auto sampleId : train_sampleIDs){
        //     Serial.printf("%d ", sampleId);
        // }
        Serial.println();
        train_data.releaseData(); // Write to binary SPIFFS, clear RAM

        // Copy test samples  
        for(auto sampleId : test_sampleIDs){
            test_data.allSamples[sampleId] = this->a.allSamples[sampleId];
        }
        test_data.releaseData(); // Write to binary SPIFFS, clear RAM

        // Clean up source data
        this->a.purgeData();      // remove original dataset
    }

    // ---------------------------------------------------------------------------------
    void ClonesData(Rf_data& data, uint8_t numSubData) {
        Serial.println("<- clones data ->");
        if (!data.isLoaded) {
            data.loadData(true);
        }

        // vector<pair<unordered_set<uint16_t>,Rf_data>> subDataList;
        dataList.clear();
        dataList.reserve(numSubData);
        uint16_t numSample = data.allSamples.size();  
        uint16_t numSubSample = numSample * 0.632;
        uint16_t oob_size = numSample - numSubSample;

        // Create a vector of all sample IDs for efficient random access
        vector<uint16_t> allSampleIds;
        allSampleIds.reserve(numSample);
        for (const auto& sample : data.allSamples) {
            allSampleIds.push_back(sample.first);
        }

        for (uint8_t i = 0; i < numSubData; i++) {
            Serial.printf("creating dataset for sub-tree : %d\n", i);
            Rf_data sub_data;
            ChainedUnorderedSet<uint16_t> inBagSamples;
            inBagSamples.reserve(numSubSample);

            ChainedUnorderedSet<uint16_t> oob_set;
            oob_set.reserve(oob_size);

            // Initialize subset data
            sub_data.allSamples.clear();
            sub_data.allSamples.reserve(numSubSample);
            // Set flags for data types
            sub_data.flag = SUBSET_DATA;

            String sub_data_name = "/tree_" + String(i) + "_data.bin";
            sub_data.filename = sub_data_name;
            sub_data.isLoaded = true;
            
            
            // Bootstrap sampling WITH replacement
            while(sub_data.allSamples.size() < numSubSample){
                uint16_t idx = static_cast<uint16_t>(esp_random() % numSample);
                // Get sample ID from random index(alway present in allSampleIds)
                uint16_t sampleId = allSampleIds[idx];     
                
                inBagSamples.insert(sampleId);
                sub_data.allSamples[sampleId] = data.allSamples[sampleId];
            }
            sub_data.allSamples.fit();
            if(this->boostrap) sub_data.boostrapData(numSample, this->numSamples);     // boostrap sampling 
            // checkHeapFragmentation();
            // Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());

            sub_data.releaseData(); // Save as binary
            dataList.push_back(sub_data); // Store subset data for this tree
            
            // Create OOB set with samples not used in this tree
            for (uint16_t id : allSampleIds) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id);
                }
            }
            OOB.push_back(oob_set); // Store OOB set for this tree
        }
        data.releaseData(true);
    }
    // ------------------------------------------------------------------------------

    void first_scan(String filename, bool header = false) {
      File file = SPIFFS.open(filename, "r");
      if (!file) {
          Serial.println("❌ Failed to open file.");
          return;
      }

      unordered_map<uint8_t, uint16_t> labelCounts;
      unordered_set<uint8_t> featureValues;

      uint16_t numSamples = 0;
      uint16_t maxFeatures = 0;

      // Skip header
      if (header) {
          file.readStringUntil('\n');
      }

      while (file.available()) {
          String line = file.readStringUntil('\n');
          line.trim();
          if (line.length() == 0) continue;

          int lastIndex = 0;
          int featureIndex = 0;
          bool malformed = false;

          while (true) {
              int commaIndex = line.indexOf(',', lastIndex);
              String token = (commaIndex != -1) ?
                  line.substring(lastIndex, commaIndex) :
                  line.substring(lastIndex);

              token.trim();
              if (token.length() == 0) {
                  malformed = true;
                  break;
              }

              int numValue = token.toInt();
              if (featureIndex == 0) {
                  labelCounts[numValue]++;
              } else {
                  featureValues.insert(numValue);
                  uint16_t featurePos = featureIndex - 1;
                  if (featurePos + 1 > maxFeatures) {
                      maxFeatures = featurePos + 1;
                  }
              }

              if (commaIndex == -1) break;
              lastIndex = commaIndex + 1;
              featureIndex++;
          }

          if (!malformed) {
              numSamples++;
              if (numSamples >= 10000) break;
          }
      }

      this->numFeatures = maxFeatures;
      this->numSamples = numSamples;

      // Analyze label distribution
      if (labelCounts.size() > 0) {
          float expectedCountPerLabel = (float)numSamples / labelCounts.size();
          float maxImbalanceRatio = 0.0f;
          uint8_t minorityLabel = 0;
          uint8_t majorityLabel = 0;
          uint16_t minorityCount = numSamples;
          uint16_t majorityCount = 0;

          for (auto& it : labelCounts) {
              uint8_t label = it.first;
              uint16_t count = it.second;
              if (count > majorityCount) {
                  majorityCount = count;
                  majorityLabel = label;
              }
              if (count < minorityCount) {
                  minorityCount = count;
                  minorityLabel = label;
              }
          }

          if (minorityCount > 0) {
              maxImbalanceRatio = (float)majorityCount / minorityCount;
          }

          if (maxImbalanceRatio > 10.0f) {
              trainFlag = trainingFlags::RECALL;
              Serial.printf("📉 Imbalanced dataset (ratio: %.2f). Setting trainFlag to RECALL.\n", maxImbalanceRatio);
          } else if (maxImbalanceRatio > 3.0f) {
              trainFlag = trainingFlags::F1_SCORE;
              Serial.printf("⚖️ Moderately imbalanced dataset (ratio: %.2f). Setting trainFlag to F1_SCORE.\n", maxImbalanceRatio);
          } else if (maxImbalanceRatio > 1.5f) {
              trainFlag = trainingFlags::PRECISSION;
              Serial.printf("🟨 Slight imbalance (ratio: %.2f). Setting trainFlag to PRECISION.\n", maxImbalanceRatio);
          } else {
              trainFlag = trainingFlags::ACCURACY;
              Serial.printf("✅ Balanced dataset (ratio: %.2f). Setting trainFlag to ACCURACY.\n", maxImbalanceRatio);
          }
      }

      // Summary
      Serial.printf("📊 Dataset Summary:\n");
      Serial.printf("  Total samples: %d\n", numSamples);
      Serial.printf("  Total features: %d\n", maxFeatures);
      Serial.printf("  Unique labels: %d\n", labelCounts.size());
      this->numLabels = labelCounts.size();

      Serial.println("  Label distribution:");
      for (auto& label : labelCounts) {
          float percent = (float)label.second / numSamples * 100.0f;
          Serial.printf("    Label %d: %d samples (%.2f%%)\n", label.first, label.second, percent);
      }

      Serial.print("Feature values: ");
      for (uint8_t val : featureValues) {
          Serial.printf("%d ", val);
          this->allFeaturesValue.push_back(val);
      }
      Serial.println();
      file.close();
    }

    // Static wrapper to call the member restore_data
    static void static_restore_data(rf_flag& flag, uint8_t treeIndex) {
        if (instance_ptr) {
            instance_ptr->restore_data(flag, treeIndex);
        }
    }
    // restore Rf_data obj when it's loadData() fails
    // restore Rf_data obj when it's loadData() fails
    void restore_data(rf_flag& data_flag, uint8_t treeIndex) {
        Serial.println("trying to restore data...");
        if (Rf_data::restore_data_callback == nullptr) {
            Serial.println("❌ Restore callback not set, cannot restore data.");
            return;
        }
        if(data_flag == rf_flag::TRAINING_DATA || data_flag == rf_flag::TESTING_DATA){   
            // Restore training set from backup
            if (train_data.isLoaded) {
                Serial.println("Training data already loaded, skipping restore.");
                return;
            }
            if (train_backup.empty()) {
                Serial.println("No training backup available, cannot restore.");
                return;
            }
            Serial.println("Restoring training data from backup...");
            train_data.allSamples.clear();
            train_data.allSamples.reserve(train_backup.size());

            // open baseFile (csv file)
            File baseFileHandle = SPIFFS.open(baseFile.c_str(), FILE_READ);
            if (!baseFileHandle) {
                Serial.printf("❌ Failed to open base file: %s\n", baseFile.c_str());
                return;
            }
            vector<uint16_t> sampleID_bag;
            if(data_flag == rf_flag::TRAINING_DATA) {
                sampleID_bag = train_backup; // Use train_backup for training data
            }else{
                uint16_t test_cursor = 0;
                for(uint16_t i = 0; i < numSamples; i++){
                    if(train_backup[test_cursor] == i){
                        test_cursor++;
                    }else{
                        sampleID_bag.push_back(i); // Use all samples not in train_backup for testing data
                    }
                }
            }
            // All sampleIDs are sorted from smallest to largest, just browse baeFile once
            uint16_t current_row = 0;   // 
            for(uint16_t sampleId : sampleID_bag) {
                // Skip rows until we reach the sampleId
                while (current_row < sampleId) {
                    String line = baseFileHandle.readStringUntil('\n');
                    line.trim();
                    current_row++;
                }
                // Now read the line corresponding to sampleId
                String line = baseFileHandle.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;

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
                s.features.fit();

                train_data.allSamples[sampleId] = s;
            }
            train_data.allSamples.fit();
            baseFileHandle.close();
            train_data.isLoaded = true;
            Serial.printf("Training data restored with %d samples.\n", train_data.allSamples.size());
        }else if(data_flag == rf_flag::SUBSET_DATA){
            // Restore subset data for a specific tree
            // also reconstructs its corresponding oob set
            if (treeIndex >= dataList.size()) {
                Serial.printf("❌ Invalid tree index: %d\n", treeIndex);
                return;
            }
            Rf_data& subsetData = dataList[treeIndex];
            if (subsetData.isLoaded) {
                Serial.printf("Subset data for tree %d already loaded, skipping restore.\n", treeIndex);
                return;
            }
            ChainedUnorderedSet<uint16_t>& oob_set = OOB[treeIndex];

            Serial.printf("Restoring subset data for tree %d...\n", treeIndex);
            uint16_t numSubSamples;
            if(!this->boostrap) {
                numSubSamples = train_backup.size() * 0.632;
            }else{
                numSubSamples = train_backup.size();     // with boostrap sampling
            }
            if(!train_data.isLoaded){
                train_data.loadData(true); // Load train_data if not already loaded
            }
            subsetData.allSamples.clear();
            subsetData.allSamples.reserve(numSubSamples);

            ChainedUnorderedSet<uint16_t> subsetSampleIDs;
            subsetSampleIDs.reserve(numSubSamples);

            // Clone samples from train_data based on train_data
            while (subsetData.allSamples.size() < numSubSamples) {
                uint16_t idx = static_cast<uint16_t>(esp_random() % train_backup.size());
                uint16_t sampleId = train_backup[idx];
                subsetSampleIDs.insert(sampleId);
            }
            vector<uint16_t> sampleIDs_bag;
            for(auto id : subsetSampleIDs){
                sampleIDs_bag.push_back(id);
            }
            vector<pair<uint16_t, Rf_sample>> subsetSamples = train_data.loadData(sampleIDs_bag);
            for (const auto& entry : subsetSamples) {
                uint16_t originalId = entry.first; // Original sample ID
                const Rf_sample& s = entry.second;
                
                // Add to subset data
                subsetData.allSamples[originalId] = s;
            }
            subsetData.allSamples.fit();
            subsetData.isLoaded = true;
            
            // restore oob set for this tree
            oob_set.clear();
            for (uint16_t id : train_backup) {
                if (subsetSampleIDs.find(id) == subsetSampleIDs.end()) {
                    oob_set.insert(id); // Add to OOB set if not in subset
                }
            }
            oob_set.fit(); // Fit the OOB set

            Serial.printf("Subset data for tree %d restored with %d samples.\n", treeIndex, subsetData.allSamples.size());
            Serial.printf("Restore successful !");
        }else{
            return;   // unexpected flag / base data 
        }
    }

    // FIXED: Enhanced forest cleanup
    void clearForest() {
        // Process trees one by one to avoid heap issues
        for (size_t i = 0; i < root.size(); i++) {
            root[i].purgeTree(); // Release each tree safely
        }
        root.clear();
    }

    // Function to calculate entropy of a set sample 
    float entropy(ChainedUnorderedMap<Rf_sample>& allSamples) {
        unordered_set<uint8_t> labels; 
        for (const auto& sample : allSamples) {
            labels.insert(sample.second.label);
        }
        float result = 0;
        for (const auto& label : labels) {
            float p = 0;
            for (const auto& sample : allSamples) {
                if (sample.second.label == label) {
                    p += 1;
                }
            }
            p /= allSamples.size();
            result -= p * log2(p);
        }
        return result;
    }

    // calculate Information Gain of a feature 
    float infoGain(ChainedUnorderedMap<Rf_sample>& allSamples, uint8_t featureID) {
        float result = 0; 
        float baseEntropy = entropy(allSamples);     

        for (const auto& value : this->allFeaturesValue){
            ChainedUnorderedMap<Rf_sample> subset;          // create subset containing ids of samples with the same value 
            for (const auto& sample : allSamples) {
                if (sample.second.features[featureID] == value) {
                    subset[sample.first] = sample.second;         // add id to subset 
                }
            }
            // subset.fit();
            float p = (float)subset.size() / allSamples.size();
            result += p * entropy(subset);
        }
        return baseEntropy - result;
    }

    // Function to calculate Gini impurity of a set of samples
    float gini(ChainedUnorderedMap<Rf_sample>& allSamples) {
        if (allSamples.size() == 0) return 0.0f;
        
        // Count frequency of each label
        uint8_t labelCounts[this->numLabels] = {0};
        for (const auto& sample : allSamples) {
            if (sample.second.label < this->numLabels) {
                labelCounts[sample.second.label]++;
            }
        }
        
        float giniImpurity = 1.0f;
        uint32_t totalSamples = allSamples.size();
        
        // Calculate Gini: 1 - Σ(pi^2) where pi is probability of class i
        for (uint8_t i = 0; i < this->numLabels; i++) {
            if (labelCounts[i] > 0) {
                float probability = static_cast<float>(labelCounts[i]) / totalSamples;
                giniImpurity -= probability * probability;
            }
        }
        
        return giniImpurity;
    }

    // Calculate Gini gain (reduction in Gini impurity) for a feature
    float giniGain(ChainedUnorderedMap<Rf_sample>& allSamples, uint8_t featureID) {
        float baseGini = gini(allSamples);
        float weightedGini = 0.0f;
        uint32_t totalSamples = allSamples.size();
        
        if (totalSamples == 0) return 0.0f;
        
        // Calculate weighted Gini for each feature value
        for (const auto& value : this->allFeaturesValue) {
            ChainedUnorderedMap<Rf_sample> subset;
            subset.reserve(totalSamples / this->allFeaturesValue.size() + 1); // Estimate size
            
            // Create subset for this feature value
            for (const auto& sample : allSamples) {
                if (sample.second.features[featureID] == value) {
                    subset[sample.first] = sample.second;
                }
            }
            
            if (subset.size() > 0) {
                float weight = static_cast<float>(subset.size()) / totalSamples;
                weightedGini += weight * gini(subset);
            }
        }
        
        return baseGini - weightedGini; // Gini gain
    }

    Tree_node* createLeafNode(Rf_data& data) {
        Tree_node* leaf = new Tree_node();
        leaf->is_leaf = true;

        // Efficient majority label calculation
        uint8_t labelCounts[this->numLabels] = {0};
        uint8_t maxCount = 0;
        uint8_t majorityLabel = 0;

        for (const auto& sample : data.allSamples) {
            if (sample.second.label < this->numLabels) {
                labelCounts[sample.second.label]++;
                if (labelCounts[sample.second.label] > maxCount) {
                    maxCount = labelCounts[sample.second.label];
                    majorityLabel = sample.second.label;
                }
            }
        }
        leaf->label = majorityLabel;
        return leaf;
    }


    Tree_node* buildTree(Rf_data &a, uint8_t min_split, uint16_t max_depth, bool useGini) {
        Tree_node* node = new Tree_node();

        unordered_set<uint8_t> labels;            // Set of labels  
        for (auto sample : a.allSamples) {
            labels.insert(sample.second.label);
        }
        
        // All samples have the same label, mark node as leaf 
        if (labels.size() == 1) {
            node->is_leaf = true;
            node->label = *labels.begin();
            return node;
        }

        // Too few samples to split || no features left || max depth reached 
        if (a.allSamples.size() < min_split || max_depth == 0) {
            delete node;
            return createLeafNode(a);
        }

        float maxGain = -1.0f;
        uint8_t num_selected_features = static_cast<uint8_t>(sqrt(numFeatures));
        if (num_selected_features == 0) num_selected_features = 1; // always select at least one feature

        unordered_set<uint16_t> selectedFeatures(num_selected_features);
        while (selectedFeatures.size() < num_selected_features) {
            uint16_t idx = static_cast<uint16_t>(esp_random() % numFeatures);
            selectedFeatures.insert(idx);
        }
        
        // Select best feature using either Gini or Information Gain
        for (const auto& featureID : selectedFeatures) {
            float gain;
            if (useGini) {
                gain = giniGain(a.allSamples, featureID);
            } else {
                gain = infoGain(a.allSamples, featureID);
            }
            
            if (gain > maxGain) {
                maxGain = gain;
                node->featureID = featureID; 
            }
        }
        
        // Poor split - create leaf (threshold may need adjustment for Gini)
        float threshold = useGini ? 0.05f : 0.1f; // Gini gains are typically smaller
        if (maxGain <= threshold) {
            delete node;
            return createLeafNode(a);
        }
    
        uint8_t valid_child = 0; // count valid children created
    
        // GROUPING AND RECURSION 
        for(uint8_t value : allFeaturesValue){                            
            Rf_data sub_data = Rf_data();                     // Sub Data with samples and features for next recursion 
            for (const auto& sample : a.allSamples) {
                if (sample.second.features[node->featureID] == value){
                    sub_data.allSamples[sample.first] = sample.second;     // assign samples 
                }
            }
            if (sub_data.allSamples.empty()) {                // no samples left to split 
                int mostLab, max = -999;
                unordered_map<uint8_t, int> labelCount;
                labelCount.reserve(sub_data.allSamples.size());
                for (const auto& sample : a.allSamples) {
                    labelCount[sample.second.label]++;
                    if (labelCount[sample.second.label] > max) {
                        max = labelCount[sample.second.label];
                        mostLab = sample.second.label;
                    }
                }
                Tree_node* childNode = new Tree_node();
                childNode->label = mostLab;             // assign most common label
                childNode->is_leaf = true;              // mark as leaf
                childNode->branchValue = value;         // assign branch value to child node 
                node->children.push_back(childNode);
                node->children.fit();
                valid_child++;                          // increase valid child count
            }else{
                Tree_node* childNode = buildTree(sub_data,min_split,max_depth-1, useGini); 
                // max_depth--;                                 // increase tree depth by 1 
                childNode->branchValue = value;                 // assign branch value to child node
                node->children.push_back(childNode);
                node->children.fit();
                valid_child++;                                  // increase valid child count
            }
            // Convert to leaf if no valid children
            if (valid_child == 0) {
                delete node;
                return createLeafNode(a);
            }
        }
        return node;
    }

    uint8_t one_tree_predict(Rf_tree& tree, Rf_sample& sample) {
        return tree.predictSample(sample);
    }
    
    // Enhanced prediction with SPIFFS loading
    // fix the predClassSample function to handle multi-class better
    uint8_t predClassSample(Rf_sample& s){
        int16_t totalPredict = 0;
        unordered_map<uint8_t, uint8_t> predictClass;
        
        // Use streaming prediction 
        for(auto& tree : root){
            // comment this line to predict from SPIFFS (if tree is not loaded)
            // if(!tree->isLoaded) tree->loadTree(); // Load tree if not already loaded

            uint8_t predict = tree.predictSample(s); // Uses streaming if not loaded
            if(predict < numLabels){
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
        if(certainty < unity_threshold) {
            return 255;
        }
        
        return mostPredict;
    }

    // hellper function: evaluate the entire forest, using OOB_score : iterate over all samples in 
    // the train set and evaluate by trees whose OOB set contains its ID
    // Optimized training evaluation : Activate only the necessary plants
    float get_training_evaluation_index(trainingFlags flag){
        Serial.println("Get training evaluation index (fixed)... ");
        uint8_t chunk_ratio = 3; 
        
        uint16_t samples_buffer_chunk = numSamples / (chunk_ratio + 1); 
        uint8_t tree_buffer_chunk = numTree / chunk_ratio; 
        if(samples_buffer_chunk == 0) samples_buffer_chunk = 10; // Ensure at least one sample per chunk
        if(tree_buffer_chunk == 0) tree_buffer_chunk = 3; // Ensure at least one tree per chunk

        uint16_t samples_start_pos = 0; 
        uint16_t samples_end_pos = samples_start_pos + samples_buffer_chunk;
        vector<pair<uint16_t,Rf_sample>> train_samples_buffer;
        vector<uint16_t> sampleIDs_bag;
        for(uint16_t i= samples_start_pos; i< samples_end_pos; i++){
            sampleIDs_bag.push_back(i); // Fill the bag with sample IDs
        }
        train_samples_buffer = train_data.loadData(sampleIDs_bag); // Load training data in chunks
        // get samples from the SPIFFS file of train_data
        if(train_samples_buffer.empty()){
            Serial.println("❌ No training samples found in the buffer!");
            Serial.println("Switching to plan B: loading all samples from RAM...");
            bool train_data_preloaded = train_data.isLoaded; // Check if training data is already loaded
            if(!train_data_preloaded) train_data.loadData(true); // Load all training data into RAM
            for(auto p : train_data.allSamples){
                train_samples_buffer.push_back(make_pair(p.first, p.second)); // Fill the buffer with all samples
            }
            samples_end_pos = train_samples_buffer.size(); // Set samples_end_pos to the size of all samples
            if(!train_data_preloaded) train_data.releaseData(true); // Release data from RAM
        }

        loadForest(); // Load all trees into RAM
        checkHeapFragmentation();
        vector<Rf_tree*> root_buffer; // Buffer to hold trees in chunks
        root_buffer.reserve(tree_buffer_chunk); // Reserve space for tree chunks
        uint8_t tree_start_pos ; // Start position for tree chunking
        uint8_t tree_end_pos ;

        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[numLabels] = {0};
        uint16_t oob_fp[numLabels] = {0};
        uint16_t oob_fn[numLabels] = {0};
        uint16_t oob_correct = 0, oob_total = 0;
        
        // for(auto sample : train_data.allSamples){
        for(tree_start_pos = 0; tree_end_pos < numTree; tree_start_pos += tree_buffer_chunk){  
            // charging trees in chunks
            root_buffer.clear();
            tree_end_pos = tree_start_pos + tree_buffer_chunk; // Update end position for the next chunk
            if(tree_end_pos > numTree) tree_end_pos = numTree; // Ensure we don't exceed the number of trees
            for(uint8_t i = tree_start_pos; i < tree_end_pos && i < numTree; i++){
                root_buffer.push_back(&root[i]); 
            }    
            // activate trees in the buffer
            for(auto* tree : root_buffer){
                tree->loadTree();
            }    
            while(samples_end_pos < numSamples){
                auto sample = train_samples_buffer.back();
                train_samples_buffer.pop_back();
                if(train_samples_buffer.empty() && samples_end_pos < numSamples){
                    samples_start_pos += samples_buffer_chunk;
                    samples_end_pos = samples_start_pos + samples_buffer_chunk;
                    sampleIDs_bag.clear(); // Clear the bag for the next chunk
                    for(uint16_t i = samples_start_pos; i < samples_end_pos; i++){
                        sampleIDs_bag.push_back(i); // Fill the bag with next chunk of sample IDs
                    }
                    train_samples_buffer = train_data.loadData(sampleIDs_bag); // Load next chunk
                }
                uint16_t sampleId = sample.first;  // Get the sample ID
                // Serial.printf("Evaluating sample ID: %d\n", sampleId);
                
                // Find all trees whose OOB set contains this sampleId
                vector<uint8_t, SMALL> activeTrees;
                activeTrees.reserve(numTree);
                
                for(uint8_t i = 0; i < tree_buffer_chunk; i++){
                    if(OOB[i].find(sampleId) != OOB[i].end()){
                        activeTrees.push_back(i);
                    }
                }
                if(activeTrees.empty()){
                    continue; // No OOB trees for this sample
                }
                
                // Predict using only the OOB trees for this sample
                uint8_t actualLabel = sample.second.label;
                unordered_map<uint8_t, uint8_t> oobPredictClass;
                uint16_t oobTotalPredict = 0;
                
                for(uint8_t treeIdx : activeTrees){
                    uint8_t predict = one_tree_predict(root[treeIdx], sample.second);
                    if(predict < numLabels){
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
                
                // // Check certainty threshold
                // float certainty = static_cast<float>(maxVotes) / oobTotalPredict;
                // if(certainty < unity_threshold / chunk_ratio) {
                //     continue; // Skip uncertain predictions
                // }
                
                // Update confusion matrix
                oob_total++;
                if(oobPredictedLabel == actualLabel){
                    oob_correct++;
                    oob_tp[actualLabel]++;
                } else {
                    oob_fn[actualLabel]++;
                    if(oobPredictedLabel < numLabels){
                        oob_fp[oobPredictedLabel]++;
                    }
                }

            }
            for(auto* tree : root_buffer){
                tree->releaseTree();
            }
        }

        Serial.printf("Ram before releasing trees: %d\n", ESP.getFreeHeap());
        releaseForest(); // Release trees from RAM after evaluation
        Serial.printf("Ram after releasing trees: %d\n", ESP.getFreeHeap());
        // Calculate the requested metric
        float result = 0.0f;
        
        if(oob_total == 0){
            Serial.println("❌ No valid OOB predictions found!");
            return 0.0f;
        }
        
        switch(flag){
            case trainingFlags::ACCURACY:
                result = static_cast<float>(oob_correct) / oob_total;
                Serial.printf("OOB Accuracy: %.3f (%d/%d)\n", result, oob_correct, oob_total);
                break;
                
            case trainingFlags::PRECISSION:
                {
                    float totalPrecision = 0.0f;
                    uint8_t validLabels = 0;
                    for(uint8_t label = 0; label < numLabels; label++){
                        uint16_t tp = oob_tp[label];
                        uint16_t fp = oob_fp[label];
                        if(tp + fp > 0){
                            totalPrecision += static_cast<float>(tp) / (tp + fp);
                            validLabels++;
                        }
                    }
                    result = validLabels > 0 ? totalPrecision / validLabels : 0.0f;
                    Serial.printf("OOB Precision: %.3f\n", result);
                }
                break;
                
            case trainingFlags::RECALL:
                {
                    float totalRecall = 0.0f;
                    uint8_t validLabels = 0;
                    for(uint8_t label = 0; label < numLabels; label++){
                        uint16_t tp = oob_tp[label];
                        uint16_t fn = oob_fn[label];
                        if(tp + fn > 0){
                            totalRecall += static_cast<float>(tp) / (tp + fn);
                            validLabels++;
                        }
                    }
                    result = validLabels > 0 ? totalRecall / validLabels : 0.0f;
                    Serial.printf("OOB Recall: %.3f\n", result);
                }
                break;
                
            case trainingFlags::F1_SCORE:
                {
                    float totalF1 = 0.0f;
                    uint8_t validLabels = 0;
                    for(uint8_t label = 0; label < numLabels; label++){
                        uint16_t tp = oob_tp[label];
                        uint16_t fp = oob_fp[label];
                        uint16_t fn = oob_fn[label];
                        
                        if(tp + fp > 0 && tp + fn > 0){
                            float precision = static_cast<float>(tp) / (tp + fp);
                            float recall = static_cast<float>(tp) / (tp + fn);
                            if(precision + recall > 0){
                                float f1 = 2.0f * precision * recall / (precision + recall);
                                totalF1 += f1;
                                validLabels++;
                            }
                        }
                    }
                    result = validLabels > 0 ? totalF1 / validLabels : 0.0f;
                    Serial.printf("OOB F1-Score: %.3f\n", result);
                }
                break;
        }
        
        // // Clean up: release train_data if we loaded it
        // if(!train_data_preloaded) {
        //     train_data.releaseData(true);
        // }
        
        return result;
    }


    // Rebuild forest with existing data but new parameters - enhanced for SPIFFS
    void rebuildForest() {
        // Clear trees but preserve dataList structure
        for (size_t i = 0; i < root.size(); i++) {

        }
        Serial.print("Rebuilding sub_tree: ");
        for(uint8_t i = 0; i < numTree; i++){
            dataList[i].loadData(true);
            Serial.printf("%d, ", i);
            // checkHeapFragmentation();
            Tree_node* rootNode = buildTree(dataList[i], minSplit, maxDepth, useGini);
            
            Rf_tree& tree = root[i];
            if(tree.root != nullptr) {
                delete tree.root; // Clean up old root node
            }
            tree.root = rootNode; // Assign the new root node
            tree.isLoaded = true; // Mark the tree as loaded
            tree.releaseTree(); // Release the tree from RAM to SPIFFS  

            dataList[i].releaseData(true);
        }
        Serial.printf("\n===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());;
        // Serial.printf("RAM after forest rebuild: %d\n", ESP.getFreeHeap());
    }

    void loadForest(){
        for (auto& tree : root) {
            if (!tree.isLoaded) {
                tree.loadTree();
            }
        }
    }

    // releaseForest: Release all trees from RAM into SPIFFS
    void releaseForest(){
        for(auto& tree : root) {
            if (tree.isLoaded) {
                tree.releaseTree(); // Release the tree from RAM
            }
        }
    }
  public:
    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------
    // train model : using OOB (Out of Bag) samples to evaluate the model
    void training(int epochs, bool early_stop = false) {
        Serial.println("----------- Training started ----------");
        checkHeapFragmentation();
        
        float best_evaluation_index = 0.0f;
        float current_evaluation_index = 0.0f;
        uint8_t no_improvement_count = 0;
        const uint8_t early_stop_patience = 3; // Stop after 3 epochs without improvement
        const float min_improvement = 0.001f;  // Minimum improvement threshold
        
        Serial.printf("Training using %s metric\n", 
            (trainFlag == trainingFlags::ACCURACY) ? "ACCURACY" :
            (trainFlag == trainingFlags::PRECISSION) ? "PRECISION" :
            (trainFlag == trainingFlags::RECALL) ? "RECALL" : "F1_SCORE");
        
        for(int epoch = 0; epoch < epochs; epoch++) {
            Serial.printf("\n========== EPOCH %d/%d ==========\n", epoch + 1, epochs);
            
            // Get current evaluation index
            current_evaluation_index = get_training_evaluation_index(trainFlag);
            
            Serial.printf("Current %s: %.4f\n", 
                (trainFlag == trainingFlags::ACCURACY) ? "accuracy" :
                (trainFlag == trainingFlags::PRECISSION) ? "precision" :
                (trainFlag == trainingFlags::RECALL) ? "recall" : "F1-score",
                current_evaluation_index);
            
            // Check for improvement
            if(current_evaluation_index > best_evaluation_index + min_improvement) {
                best_evaluation_index = current_evaluation_index;
                no_improvement_count = 0;
                Serial.println("✅ Improvement detected! Model saved.");
            } else {
                no_improvement_count++;
                Serial.printf("⚠️  No improvement for %d epoch(s)\n", no_improvement_count);
            }
            
            // Early stopping check
            if(early_stop && no_improvement_count >= early_stop_patience) {
                Serial.printf("🛑 Early stopping triggered after %d epochs without improvement\n", no_improvement_count);
                Serial.printf("Best %s achieved: %.4f\n", 
                    (trainFlag == trainingFlags::ACCURACY) ? "accuracy" :
                    (trainFlag == trainingFlags::PRECISSION) ? "precision" :
                    (trainFlag == trainingFlags::RECALL) ? "recall" : "F1-score",
                    best_evaluation_index);
                break;
            }
            
            // If not the last epoch and we need improvement, rebuild forest
            if(epoch < epochs - 1) {
                // Simple forest improvement strategy: adjust parameters slightly
                if(current_evaluation_index < 0.7f && maxDepth > 3) {
                    maxDepth -= 1; // Reduce overfitting
                    Serial.printf("📉 Low performance detected. Reducing max depth to %d\n", maxDepth);
                    rebuildForest();
                } else if(current_evaluation_index > 0.9f && maxDepth < 15) {
                    maxDepth += 1; // Allow more complexity
                    Serial.printf("📈 High performance detected. Increasing max depth to %d\n", maxDepth);
                    rebuildForest();
                } else if(no_improvement_count >= 2) {
                    // Randomize forest by rebuilding with same parameters
                    Serial.println("🔄 Rebuilding forest with random variation...");
                    rebuildForest();
                }
                
                // Memory management between epochs
                // checkHeapFragmentation();
                Serial.printf("RAM after epoch %d: %d bytes\n", epoch + 1, ESP.getFreeHeap());
            }
            
            delay(100); // Small delay to prevent WDT reset
        }
        
        // Final evaluation
        Serial.println("\n============ TRAINING COMPLETE ============");
        Serial.printf("Final %s: %.4f\n", 
            (trainFlag == trainingFlags::ACCURACY) ? "accuracy" :
            (trainFlag == trainingFlags::PRECISSION) ? "precision" :
            (trainFlag == trainingFlags::RECALL) ? "recall" : "F1-score",
            current_evaluation_index);
        
        Serial.printf("Best %s achieved: %.4f\n", 
            (trainFlag == trainingFlags::ACCURACY) ? "accuracy" :
            (trainFlag == trainingFlags::PRECISSION) ? "precision" :
            (trainFlag == trainingFlags::RECALL) ? "recall" : "F1-score",
            best_evaluation_index);
        
        Serial.printf("Total epochs: %d\n", epochs);
        Serial.printf("Trees in forest: %d\n", numTree);
        Serial.printf("Final max depth: %d\n", maxDepth);
        Serial.printf("Min split: %d\n", minSplit);
        Serial.printf("useGini: %s, boostrap: %s\n", 
                        useGini ? "true" : "false", 
                        boostrap ? "true" : "false");
        Serial.printf("\nlowest RAM: %d\n", lowest_ram);
        
        // Ensure all trees are released to SPIFFS after training
        releaseForest();
        
        Serial.printf("Final RAM: %d bytes\n", ESP.getFreeHeap());
        Serial.println("🎓 Training completed successfully!");
    }


    // New combined prediction metrics function
    vector<vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        bool pre_load_data = true;
        if(!data.isLoaded){
            data.loadData(true);
            pre_load_data = false;
        }
        loadForest();
      
        // Counters for each label
        unordered_map<uint8_t, uint32_t> tp, fp, fn, totalPred, correctPred;
        
        // Initialize counters for all actual labels
        for (uint8_t label=0; label < numLabels; label++) {
            tp[label] = 0;
            fp[label] = 0; 
            fn[label] = 0;
            totalPred[label] = 0;
            correctPred[label] = 0;
        }
        
        // Single pass over samples
        for (auto kv : data.allSamples) {
            uint8_t actual = kv.second.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(kv.second));
            
            totalPred[actual]++;
            
            if (pred == actual) {
                tp[actual]++;
                correctPred[actual]++;
            } else {
                if (pred < numLabels && pred >=0) {
                    fp[pred]++;
                }
                fn[actual]++;
            }
        }
        
        // Build metric vectors using ONLY actual labels
        vector<pair<uint8_t, float>> precisions, recalls, f1s, accuracies;
        
        for (uint8_t label = 0; label < numLabels; label++) {
            uint32_t tpv = tp[label], fpv = fp[label], fnv = fn[label];
            
            float prec = (tpv + fpv == 0) ? 0.0f : float(tpv) / (tpv + fpv);
            float rec  = (tpv + fnv == 0) ? 0.0f : float(tpv) / (tpv + fnv);
            float f1   = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
            float acc  = (totalPred[label] == 0) ? 0.0f : float(correctPred[label]) / totalPred[label];
            
            precisions.push_back(make_pair(label, prec));
            recalls.push_back(make_pair(label, rec));
            f1s.push_back(make_pair(label, f1));
            accuracies.push_back(make_pair(label, acc));
            
            Serial.printf("Label %d: TP=%d, FP=%d, FN=%d, Prec=%.3f, Rec=%.3f, F1=%.3f\n", 
                        label, tpv, fpv, fnv, prec, rec, f1);
        }
        
        vector<vector<pair<uint8_t, float>>> result;
        result.push_back(precisions);  // 0: precisions
        result.push_back(recalls);     // 1: recalls
        result.push_back(f1s);         // 2: F1 scores
        result.push_back(accuracies);  // 3: accuracies

        if(!pre_load_data) data.releaseData();
        releaseForest();
        return result;
    }


    // overload: predict for new sample - enhanced with SPIFFS loading
    uint8_t predict(packed_vector<2, SMALL>& features) {
        Rf_sample sample;
        sample.features = features;
        return predClassSample(sample);
    }

    float precision(Rf_data& data) {
        vector<pair<uint8_t, float>> prec = predict(data)[0];
        float total_prec = 0.0f;
        for (const auto& p : prec) {
            total_prec += p.second;
        }
        return total_prec / prec.size();
    }

    float recall(Rf_data& data) {
        vector<pair<uint8_t, float>> rec = predict(data)[1];
        float total_rec = 0.0f;
        for (const auto& r : rec) {
            total_rec += r.second;
        }
        return total_rec / rec.size();
    }

    float f1_score(Rf_data& data) {
        vector<pair<uint8_t, float>> f1 = predict(data)[2];
        float total_f1 = 0.0f;
        for (const auto& f : f1) {
            total_f1 += f.second;
        }
        return total_f1 / f1.size();
    }

    float accuracy(Rf_data& data) {
        vector<pair<uint8_t, float>> acc = predict(data)[3];
        float total_acc = 0.0f;
        for (const auto& a : acc) {
            total_acc += a.second;
        }
        return total_acc / acc.size();
    } 
    void visual_result(Rf_data& testSet) {
        loadForest(); // Ensure all trees are loaded before prediction
        testSet.loadData(true); // Load test set data if not already loaded
        // std::cout << "SampleID, Predicted, Actual" << std::endl;
        Serial.println("SampleID, Predicted, Actual");
        for (const auto& kv : testSet.allSamples) {
            uint16_t sampleId = kv.first;
            const Rf_sample& sample = kv.second;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            // std::cout << sampleId << "  " << (int)pred << " - " << (int)sample.label << std::endl;
            Serial.printf("%d, %d, %d\n", sampleId, pred, sample.label);
        }
        testSet.releaseData(true); // Release test set data after use
        releaseForest(); // Release all trees after prediction
    }
};
RandomForest* RandomForest::instance_ptr = nullptr;

void setup() {
    Serial.begin(115200);
    while (!Serial);
    delay(2000);
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }
    manageSPIFFSFiles();
    delay(1000);
    checkHeapFragmentation();
    Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());;

    // const char* filename = "/categorical_data.csv";    // easy dataset : useGini = false | boostrap = true; 97% - sklearn 95%.
    // const char* filename = "/walker_fall.csv";    // medium dataset : useGini = false | boostrap = true; 89% - sklearn : 85%
    const char* filename = "/digit_data.csv"; // hard dataset : useGini = true | boostrap = true; 78% - sklearn : 90% 

    RandomForest forest = RandomForest(filename,21, 4, 15);
    forest.useGini = false;
    forest.boostrap = true;

    forest.MakeForest();
    // forest.Prunning();
    forest.training(6); 

    auto result = forest.predict(forest.test_data);

    // Serial.printf("Accuracy in test set: %.3f\n", result[3]);
    // printout useGini,boostrap
    Serial.printf("useGini: %s, boostrap: %s\n", 
        forest.useGini ? "true" : "false", 
        forest.boostrap ? "true" : "false");
    Serial.printf("\nlowest RAM: %d\n", lowest_ram);

    // Calculate Precision
    Serial.println("Precision in test set:");
    vector<pair<uint8_t, float>> precision = result[0];
    for (const auto& p : precision) {
      Serial.printf("Label: %d - %.3f\n", p.first, p.second);
    }
    float avgPrecision = 0.0f;
    for (const auto& p : precision) {
      avgPrecision += p.second;
    }
    avgPrecision /= precision.size();
    Serial.printf("Avg: %.3f\n", avgPrecision);

    // Calculate Recall
    Serial.println("Recall in test set:");
    vector<pair<uint8_t, float>> recall = result[1];
    for (const auto& r : recall) {
      Serial.printf("Label: %d - %.3f\n", r.first, r.second);
    }
    float avgRecall = 0.0f;
    for (const auto& r : recall) {
      avgRecall += r.second;
    }
    avgRecall /= recall.size();
    Serial.printf("Avg: %.3f\n", avgRecall);

    // Calculate F1 Score
    Serial.println("F1 Score in test set:");
    vector<pair<uint8_t, float>> f1_scores = result[2];
    for (const auto& f1 : f1_scores) {
      Serial.printf("Label: %d - %.3f\n", f1.first, f1.second);
    }
    float avgF1 = 0.0f;
    for (const auto& f1 : f1_scores) {
      avgF1 += f1.second;
    }
    avgF1 /= f1_scores.size();
    Serial.printf("Avg: %.3f\n", avgF1);

    // Calculate Overall Accuracy
    Serial.println("Overall Accuracy in test set:");
    vector<pair<uint8_t, float>> accuracies = result[3];
    for (const auto& acc : accuracies) {
      Serial.printf("Label: %d - %.3f\n", acc.first, acc.second);
    }
    float avgAccuracy = 0.0f;
    for (const auto& acc : accuracies) {
      avgAccuracy += acc.second;
    }
    avgAccuracy /= accuracies.size();
    Serial.printf("Avg: %.3f\n", avgAccuracy);

    Serial.printf("\n📊 FINAL SUMMARY:\n");
    Serial.printf("Dataset: %s\n", filename);
    Serial.printf("Trees: %d, Max Depth: %d, Min Split: %d\n", forest.numTree, forest.maxDepth, forest.minSplit);
    Serial.printf("Labels in dataset: %d\n", forest.numLabels);
    Serial.printf("Average Precision: %.3f\n", avgPrecision);
    Serial.printf("Average Recall: %.3f\n", avgRecall);
    Serial.printf("Average F1-Score: %.3f\n", avgF1);
    Serial.printf("Average Accuracy: %.3f\n", avgAccuracy);


    // forest.visual_result(forest.test_data); // Optional visualization
}


void loop() {
    manageSPIFFSFiles();
}
void manageSPIFFSFiles() {
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS Mount Failed!");
    return;
  }

  while (true) {
    Serial.println("\n====== 📂 Files in SPIFFS ======");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    String fileList[50];  // List of full paths
    int fileCount = 0;

    Serial.printf("📦 SPIFFS Free Space: %d / %d bytes available\n", SPIFFS.totalBytes() - SPIFFS.usedBytes(), SPIFFS.totalBytes());

    while (file && fileCount < 50) {
      String path = String(file.name());  // Full path, e.g. "/animal_data.csv"
      size_t fileSize = file.size();
      if (!path.startsWith("/")) {
        path = "/" + path;
      }
      fileList[fileCount] = path;


      Serial.printf("%2d: %-20s (%d bytes)\n", fileCount + 1, path.c_str(), fileSize);

      file.close();
      file = root.openNextFile();
      fileCount++;
    }

    if (fileCount == 0) {
      Serial.println("⚠️ No files found.");
    }

    Serial.println("\nType a file number to delete it, or type END to finish:");

    String input = "";
    while (input.length() == 0) {
      if (Serial.available()) {
        input = Serial.readStringUntil('\n');
        input.trim();
      }
      delay(10);
    }

    if (input.equalsIgnoreCase("END")) {
      Serial.println("🔚 Exiting file manager.");
      break;
    }

    int index = input.toInt();
    if (index >= 1 && index <= fileCount) {
      String fileToDelete = fileList[index - 1];
      if (!fileToDelete.startsWith("/")) {
        fileToDelete = "/" + fileToDelete;
      }

      Serial.printf("🗑️  Are you sure you want to delete '%s'?\n", fileToDelete.c_str());
      Serial.println("Type OK to confirm or NO to cancel:");

      String confirm = "";
      while (confirm.length() == 0) {
        if (Serial.available()) {
          confirm = Serial.readStringUntil('\n');
          confirm.trim();
        }
        delay(10);
      }

      if (confirm.equalsIgnoreCase("OK")) {
        if (!SPIFFS.exists(fileToDelete)) {
          Serial.println("⚠️ File does not exist!");
          continue;
        }

        if (SPIFFS.remove(fileToDelete)) {
          Serial.printf("✅ Deleted: %s\n", fileToDelete.c_str());
        } else {
          Serial.printf("❌ Failed to delete: %s\n", fileToDelete.c_str());
        }
      } else {
        Serial.println("❎ Deletion canceled.");
      }
    } else {
      Serial.println("⚠️ Invalid file number.");
    }
  }
}

bool cloneCSVFile(const String& src, const String& dest) {
    File sourceFile = SPIFFS.open(src, FILE_READ);
    if (!sourceFile) {
        Serial.print("❌ Failed to open source file: ");
        Serial.println(src);
        return false;
    }

    if(SPIFFS.exists(dest.c_str())){
      SPIFFS.remove(dest.c_str());
    }

    File destFile = SPIFFS.open(dest, FILE_WRITE);
    if (!destFile) {
        Serial.print("❌ Failed to create destination file: ");
        Serial.println(dest);
        sourceFile.close();
        return false;
    }

    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        destFile.print(line);
        destFile.print('\n');  // manually add \n to preserve exact format
    }

    sourceFile.close();
    destFile.close();

    Serial.print("✅ File cloned exactly from ");
    Serial.print(src);
    Serial.print(" ➝ ");
    Serial.println(dest);
    return true;
}
void checkHeapFragmentation() {
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    Serial.print("--> RAM LEFT (heap): ");
    if(freeHeap < lowest_ram){
        lowest_ram = freeHeap;  // Update lowest RAM if current is lower
    }
    if(freeHeap < 10000){
        Serial.printf("⚠️ LOW RAM: %d \n", freeHeap);
    }
    Serial.println(freeHeap);
    Serial.print("Largest Free Block: ");
    Serial.println(largestBlock);
    Serial.print("Fragmentation: ");
    Serial.print(100 - (largestBlock * 100 / freeHeap));
    Serial.println("%");
}
