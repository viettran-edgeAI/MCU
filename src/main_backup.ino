/*
#define minSplit - 3
#define maxDepth - 12
#define max_features - 234
#define max_samples - 50000
#define max_labels - 32
#define max_trees - 234

#define unity_threshold - 1.25f / static_cast<float>(max_features)
#define impurity_threshold - 0.01f 
#define train_ratio - 0.7f

*/

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
    packed_vector<2, SMALL> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
    uint8_t label;                     // label of the sample 
}Rf_sample;

using OOB_set = ChainedUnorderedSet<uint16_t>; // OOB set type
using sampleID_set = ChainedUnorderedSet<uint16_t>; // Sample ID set type
using sample_set = ChainedUnorderedMap<Rf_sample>; // set of samples

typedef struct Tree_node{
    uint8_t featureID;                 
    uint8_t packed_data;               // threshold(2) + label(5) + is_leaf(1)
    pair<Tree_node*, Tree_node*> children; // left and right children for binary split
    
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
        if (!root || !isLoaded) return 255;     // invalid prediction result
        return predictRecursive(root, sample);
    }

  private:
    uint8_t predictRecursive(Tree_node* node, Rf_sample& sample) {
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
    ChainedUnorderedMap<Rf_sample> allSamples;    // all sample and it's ID 
    Rf_data_flags flag;
    static void (*restore_data_callback)(Rf_data_flags&, uint8_t);
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
    sample_set loadData(b_vector<uint16_t>& sampleIDs_bag) {
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
        for (auto id : sampleIDs_bag) sampleIDs_set.insert(id);

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
        b_vector<uint16_t> sampleIDs;
        sampleIDs.reserve(currentSize);
        for (const auto& entry : allSamples) {
            sampleIDs.push_back(entry.first); // Store original IDs
        }
        sampleIDs.sort(); // Sort IDs for consistent access
        uint16_t cursor = 0;
        b_vector<uint16_t> newSampleIDs;
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
void (*Rf_data::restore_data_callback)(Rf_data_flags&, uint8_t) = nullptr;


typedef enum Rf_training_flags : uint8_t{
    EARLY_STOP  = 0x00,        // early stop training if accuracy is not improving
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_training_flags;

// -------------------------------------------------------------------------------- 
class RandomForest{
public:
    Rf_data a;      // base data / baseFile
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    uint16_t maxDepth;
    uint8_t minSplit;
    uint8_t numTree;     
    uint8_t numFeatures;  
    uint8_t numLabels;
    uint16_t numSamples;  // number of samples in the base data

private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<pair<Rf_data, OOB_set>> dataList; // b_vector of pairs: Rf_data and OOB set for each tree
    b_vector<uint16_t> train_backup;   // backup of training set sample IDs 
    b_vector<uint16_t> test_backup;    // backup of testing set sample IDs
    b_vector<uint16_t> validation_backup; // backup of validation set sample IDs
    b_vector<uint8_t> allFeaturesValue;     // value of all features

    float unity_threshold ;          // unity_threshold  for classification, effect to precision and recall
    float impurity_threshold = 0.01f; // threshold for impurity, default is 0.01
    float train_ratio = 0.6f; // ratio of training data to total data, default is 0.6
    float valid_ratio = 0.2f; // ratio of validation data to total data, default is 0.2
    float boostrap_ratio = 0.632f; // ratio of samples taken from train data to create subdata
    float lowest_distribution = 0.01f; // lowest distribution of a label in base dataset

    bool boostrap = true; // use boostrap sampling, default is true
    bool use_Gini = true;
    bool use_validation = true; // use validation data, default is false

public:
    uint8_t trainFlag = EARLY_STOP;    // flags for training, early stop enabled by default
    static RandomForest* instance_ptr;      // Pointer to the single instance

    RandomForest(){};
    RandomForest(String baseFile, int numtree, bool use_Gini = true, bool boostrap = true){
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
        a.flag = Rf_data_flags::BASE_DATA;
        a.filename = baseName + ".bin";
        
        unity_threshold  = 1.25f / static_cast<float>(numLabels);
        if(numFeatures == 2) unity_threshold  = 0.4f;

        this->numTree = numtree;
        this->use_Gini = use_Gini;
        this->boostrap = boostrap;
        
        // OOB.reserve(numTree);
        dataList.reserve(numTree);

        splitData(train_ratio, "train", "test", "valid");

        ClonesData(train_data, numTree);
    }
    
    // Enhanced destructor
    ~RandomForest(){
        // Clear forest safely
        Serial.println("🧹 Cleaning files... ");
        for(auto& tree : root){
            tree.purgeTree();
        }
          
        // Clear data safely
        train_data.purgeData();
        test_data.purgeData();
        if(this->use_validation) validation_data.purgeData();
        a.purgeData();
        
        for(auto& data : dataList){
            data.first.purgeData();
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
            dataList[i].first.loadData(true);
            Serial.printf("building sub_tree: %d\n", i);
            // checkHeapFragmentation();
            Tree_node* rootNode = buildTree(dataList[i].first, minSplit, maxDepth, use_Gini);
            
            // Create SPIFFS filename for this tree
            String treeFilename = "/tree_" + String(i) + ".bin";
            Rf_tree tree(treeFilename);
            tree.root = rootNode;
            tree.isLoaded = true; // Mark as loaded since we just built it
            tree.releaseTree(); // Save tree to SPIFFS
            // Add to root b_vector (tree is now in SPIFFS)
            root.push_back(tree);
            
            // Release sub-data after tree creation
            dataList[i].first.releaseData(true);
            
            Serial.printf("Tree %d saved to SPIFFS: %s\n", i, treeFilename.c_str());
            // Serial.printf("===> RAM left: %d\n", ESP.getFreeHeap());
            // Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        }
        Serial.printf("RAM after forest creation: %d\n", ESP.getFreeHeap());
    }

  private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets
    void splitData(float trainRatio, const char* extension_1, const char* extension_2, const char* extension_3) {
        Serial.println("<-- split data -->");
    
        uint16_t totalSamples = this->a.allSamples.size();
        uint16_t trainSize = static_cast<uint16_t>(totalSamples * trainRatio);
        uint16_t testSize;
        if(this->use_validation){
            testSize = static_cast<uint16_t>((totalSamples - trainSize) * 0.5);
        }else{
            testSize = totalSamples - trainSize; // No validation set, use all remaining for testing
        }
        uint16_t validationSize = totalSamples - trainSize - testSize;
        
        sampleID_set train_sampleIDs;
        sampleID_set test_sampleIDs;
        sampleID_set validation_sampleIDs;
        train_sampleIDs.reserve(trainSize);
        test_sampleIDs.reserve(testSize);
        validation_sampleIDs.reserve(validationSize);

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

        while(test_sampleIDs.size() < testSize) {
            uint16_t i = static_cast<uint16_t>(esp_random() % totalSamples);
            if (train_sampleIDs.find(i) == train_sampleIDs.end()) {
                test_sampleIDs.insert(i);
            }
        }
        test_sampleIDs.fit();
        for(auto sampleID : test_sampleIDs){
          test_backup.push_back(sampleID);
        }
        test_backup.sort();
        if(this->use_validation) {
            // Create validation set from remaining samples    
            while(validation_sampleIDs.size() < validationSize) {
                uint16_t i = static_cast<uint16_t>(esp_random() % totalSamples);
                if (train_sampleIDs.find(i) == train_sampleIDs.end() && test_sampleIDs.find(i) == test_sampleIDs.end()) {
                    validation_sampleIDs.insert(i);
                }
            }
            validation_sampleIDs.fit();
            for(auto sampleID : validation_sampleIDs){
            validation_backup.push_back(sampleID);
            }
            validation_backup.sort();
        }

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
        if(this->use_validation) {
            String validationFilename = "/" + originalName + extension_3 + ".bin";
            validation_data.filename = validationFilename;
            validation_data.isLoaded = true;
            validation_data.flag = VALIDATION_DATA;
        }

        train_data.filename = trainFilename;
        test_data.filename = testFilename;

        train_data.isLoaded = true;
        test_data.isLoaded = true;

        train_data.flag = TRAINING_DATA;
        test_data.flag = TESTING_DATA;

        Serial.printf("Number of samples in train set: %d\n", trainSize);
        Serial.printf("Number of samples in test set: %d\n", test_sampleIDs.size());
        if(this->use_validation) Serial.printf("Number of samples in validation set: %d\n", validation_sampleIDs.size());

        // Copy test samples  
        test_data.allSamples.reserve(testSize);
        for(auto sampleId : test_sampleIDs){
            test_data.allSamples[sampleId] = this->a.allSamples[sampleId];
        }
        checkHeapFragmentation();
        Serial.printf("===> RAM left: %d\n", ESP.getFreeHeap());
        Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        test_sampleIDs.clear(); // Clear sample IDs set to free memory
        test_sampleIDs.fit(); // Fit the set to release unused memory
        test_data.releaseData(); // Write to binary SPIFFS, clear RAM
        // Copy validation samples
        if(this->use_validation){    
            validation_data.allSamples.reserve(validationSize);
            for(auto sampleId : validation_sampleIDs){
                validation_data.allSamples[sampleId] = this->a.allSamples[sampleId];
            }
            validation_data.releaseData(); // Write to binary SPIFFS, clear RAM
        }
        // Clean up source data
        this->a.releaseData();

        b_vector<uint16_t> train_sampleIDs_vec;
        for(auto sampleId : train_sampleIDs){
            train_sampleIDs_vec.push_back(sampleId);
        }
        train_data.allSamples = a.loadData(train_sampleIDs_vec); // Load only training samples
        train_sampleIDs.clear(); // Clear sample IDs set to free memory
        train_sampleIDs.fit(); // Fit the set to release unused memory
        train_data.releaseData(); // Write to binary SPIFFS, clear RAM
    }

    // ---------------------------------------------------------------------------------
    void ClonesData(Rf_data& data, uint8_t numSubData) {
        Serial.println("<- clones data ->");
        if (!data.isLoaded) {
            data.loadData(true);
        }

        // b_vector<pair<unordered_set<uint16_t>,Rf_data>> subDataList;
        dataList.clear();
        dataList.reserve(numSubData);
        uint16_t numSample = data.allSamples.size();  
        uint16_t numSubSample = numSample * 0.632;
        uint16_t oob_size = numSample - numSubSample;

        // Create a b_vector of all sample IDs for efficient random access
        b_vector<uint16_t> allSampleIds;
        allSampleIds.reserve(numSample);
        for (const auto& sample : data.allSamples) {
            allSampleIds.push_back(sample.first);
        }

        for (uint8_t i = 0; i < numSubData; i++) {
            Serial.printf("creating dataset for sub-tree : %d\n", i);
            Rf_data sub_data;
            sampleID_set inBagSamples;
            inBagSamples.reserve(numSubSample);

            OOB_set oob_set;
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
            checkHeapFragmentation();
            // Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());

            sub_data.releaseData(); // Save as binary
            
            // Create OOB set with samples not used in this tree
            for (uint16_t id : allSampleIds) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id);
                }
            }
            dataList.push_back(make_pair(sub_data, oob_set)); // Store pair of subset data and OOB set
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
                this->trainFlag |= Rf_training_flags::RECALL;
                Serial.printf("📉 Imbalanced dataset (ratio: %.2f). Setting trainFlag to RECALL.\n", maxImbalanceRatio);
            } else if (maxImbalanceRatio > 3.0f) {
                this->trainFlag |= Rf_training_flags::F1_SCORE;
                Serial.printf("⚖️ Moderately imbalanced dataset (ratio: %.2f). Setting trainFlag to F1_SCORE.\n", maxImbalanceRatio);
            } else if (maxImbalanceRatio > 1.5f) {
                this->trainFlag |= Rf_training_flags::PRECISION;
                Serial.printf("🟨 Slight imbalance (ratio: %.2f). Setting trainFlag to PRECISION.\n", maxImbalanceRatio);
            } else {
                this->trainFlag |= Rf_training_flags::ACCURACY;
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
        float lowest_distribution = 100.0f;
        for (auto& label : labelCounts) {
            float percent = (float)label.second / numSamples * 100.0f;
            if(percent < lowest_distribution) {
                lowest_distribution = percent;
            }
            Serial.printf("    Label %d: %d samples (%.2f%%)\n", label.first, label.second, percent);
        }
        this->lowest_distribution = lowest_distribution / 100.0f; // Store as fraction
        if(lowest_distribution * numSamples * valid_ratio < 10){
            use_validation = false; // disable validation if too few samples
            Serial.println("⚖️ Setting use_validation to false due to low sample count in validation set.");
            train_ratio = 0.7f; // Adjust train ratio to compensate
        }

        Serial.print("Feature values: ");
        for (uint8_t val : featureValues) {
            Serial.printf("%d ", val);
            this->allFeaturesValue.push_back(val);
        }
        int baseline_minsplit_ratio = 100 * (this->numSamples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint8_t min_minSplit = max(3, this->numSamples / baseline_minsplit_ratio);
        uint8_t max_minSplit = 12;
        int base_maxDepth = min(log2(this->numSamples), log2(this->numFeatures)*1.5f);
        uint8_t max_maxDepth = min(8, base_maxDepth);
        uint8_t min_maxDepth = 3;

        this->minSplit = ( min_minSplit + max_minSplit ) / 2;
        this->maxDepth = ( min_maxDepth + max_maxDepth ) / 2;

        Serial.printf("Setting minSplit to %d and maxDepth to %d based on dataset size.\n", this->minSplit, this->maxDepth);

        Serial.println();
        file.close();
    }

    // Static wrapper to call the member restore_data
    static void static_restore_data(Rf_data_flags& flag, uint8_t treeIndex) {
        if (instance_ptr) {
            instance_ptr->restore_data(flag, treeIndex);
        }
    }
    // restore Rf_data obj when it's loadData() fails
    void restore_data(Rf_data_flags& data_flag, uint8_t treeIndex) {
        Serial.println("trying to restore data...");
        if (Rf_data::restore_data_callback == nullptr) {
            Serial.println("❌ Restore callback not set, cannot restore data.");
            return;
        }
        if(data_flag == Rf_data_flags::TRAINING_DATA || data_flag == Rf_data_flags::TESTING_DATA || data_flag == Rf_data_flags::VALIDATION_DATA) {   
            // Restore train/test set from backup and base data / baseFile (a)
            Rf_data *restore_data;
            b_vector<uint16_t> *restore_backup;
            switch (data_flag) {
                case Rf_data_flags::TRAINING_DATA:
                {
                    if(train_backup.empty()) {
                        Serial.println("❌ No training backup available, cannot restore training data.");
                        return;
                    }
                    restore_data = &train_data;
                    restore_backup = &train_backup;
                }
                break;
                case Rf_data_flags::TESTING_DATA:
                {
                    if(test_backup.empty()) {
                        Serial.println("❌ No testing backup available, cannot restore testing data.");
                        return;
                    }
                    restore_data = &test_data;
                    restore_backup = &test_backup;
                }
                break;
                case Rf_data_flags::VALIDATION_DATA:
                {
                    if(validation_backup.empty()) {
                        Serial.println("❌ No validation backup available, cannot restore validation data.");
                        return;
                    }
                    restore_data = &validation_data;
                    restore_backup = &validation_backup;
                }
                break;
                default:
                    Serial.println("❌ Invalid data flag for restore.");
                    return;
            }
            restore_data->allSamples.clear(); // Clear existing samples
            restore_data->allSamples = a.loadData(*restore_backup); // Load samples from base data using backup IDs
            if(restore_data->allSamples.empty()) {
                Serial.println("❌ Failed to restore data from backup.");
                return;
            }
            restore_data->isLoaded = true; // Mark as loaded
            Serial.printf("Training data restored with %d samples.\n", train_data.allSamples.size());
        }else if(data_flag == Rf_data_flags::SUBSET_DATA){
            // Restore subset data for a specific tree
            // also reconstructs its corresponding oob set
            if (treeIndex >= dataList.size()) {
                Serial.printf("❌ Invalid tree index: %d\n", treeIndex);
                return;
            }
            Rf_data& subsetData = dataList[treeIndex].first; // Get the subset data for this tree
            OOB_set& oob_set = dataList[treeIndex].second;

            subsetData.allSamples.clear(); // Clear existing samples
            oob_set.clear(); // Clear existing OOB set

            if (subsetData.isLoaded) {
                Serial.printf("Subset data for tree %d already loaded, skipping restore.\n", treeIndex);
                return;
            }
            Serial.printf("Restoring subset data for tree %d...\n", treeIndex);
            uint16_t numSubSamples = train_backup.size() * this->boostrap_ratio; // Calculate number of samples for this subset
            sampleID_set inBagSamples;
            inBagSamples.reserve(numSubSamples);

            Serial.printf("Subset data for tree %d restored with %d samples.\n", treeIndex, subsetData.allSamples.size());
            Serial.printf("Restore successful !");

            while(inBagSamples.size() < numSubSamples) {
                uint16_t idx = static_cast<uint16_t>(esp_random() % train_backup.size());
                uint16_t sampleId = train_backup[idx]; // Get sample ID from backup
                inBagSamples.insert(sampleId);
            }
            b_vector<uint16_t> inBagSamplesVec;
            // create OOB set with samples not used in this tree
            for (uint16_t id : train_backup) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id); // Add to OOB set if not in bag
                }
                inBagSamplesVec.push_back(id); // Store all sample IDs for later use
            }
            // restore subset data 
            if(!train_data.isLoaded) {
                // load samples from SPIFFS
                subsetData.allSamples = train_data.loadData(inBagSamplesVec); // Load only in-bag samples
            }else{
                // load samples from RAM
                for (const auto& sampleId : inBagSamples) {
                    if (train_data.allSamples.find(sampleId) != train_data.allSamples.end()) {
                        subsetData.allSamples[sampleId] = train_data.allSamples[sampleId];
                    }
                }
            }
            if(this->boostrap) {
                subsetData.boostrapData(numSubSamples, this->numSamples); // Apply boostrap sampling if enabled
            }
            subsetData.allSamples.fit(); 
            subsetData.isLoaded = true; 
            Serial.printf("Subset data for tree %d restored with %d samples.\n", treeIndex, subsetData.allSamples.size());
        }else{
            return;   // unexpected flag / base data 
        }
    }

    // FIXED: Enhanced forest cleanup
    void clearForest() {
        // Process trees one by one to avoid heap issues
        for (size_t i = 0; i < root.size(); i++) {
            root[i].purgeTree(); 
        }
        root.clear();
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

        // Use mcu::vector instead of VLA for safety and standard compliance.
        vector<uint16_t> baseLabelCounts(this->numLabels, 0);
        for (const auto& entry : data.allSamples) {
            // Bounds check to prevent memory corruption
            if (entry.second.label < this->numLabels) {
                baseLabelCounts[entry.second.label]++;
            }
        }

        float baseImpurity;
        if (use_Gini) {
            baseImpurity = 1.0f;
            for (uint8_t i = 0; i < this->numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * p;
                }
            }
        } else { // Entropy
            baseImpurity = 0.0f;
            for (uint8_t i = 0; i < this->numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        // Iterate through the randomly selected features
        for (const auto& featureID : selectedFeatures) {
            // Use a flat mcu::vector for the contingency table to avoid non-standard 2D VLAs.
            vector<uint16_t> counts(4 * this->numLabels, 0);
            uint32_t value_totals[4] = {0};

            for (const auto& entry : data.allSamples) {
                const Rf_sample& sample = entry.second;
                uint8_t feature_val = sample.features[featureID];
                // Bounds check for both feature value and label
                if (feature_val < 4 && sample.label < this->numLabels) {
                    counts[feature_val * this->numLabels + sample.label]++;
                    value_totals[feature_val]++;
                }
            }

            // Test all possible binary splits (thresholds 0, 1, 2)
            for (uint8_t threshold = 0; threshold <= 2; threshold++) {
                // Use mcu::vector for safety.
                vector<uint16_t> left_counts(this->numLabels, 0);
                vector<uint16_t> right_counts(this->numLabels, 0);
                uint32_t left_total = 0;
                uint32_t right_total = 0;

                // Aggregate counts for left/right sides from the contingency table
                for (uint8_t val = 0; val < 4; val++) {
                    if (val <= threshold) {
                        for (uint8_t label = 0; label < this->numLabels; label++) {
                            left_counts[label] += counts[val * this->numLabels + label];
                        }
                        left_total += value_totals[val];
                    } else {
                        for (uint8_t label = 0; label < this->numLabels; label++) {
                            right_counts[label] += counts[val * this->numLabels + label];
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
                    for (uint8_t i = 0; i < this->numLabels; i++) {
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
                    for (uint8_t i = 0; i < this->numLabels; i++) {
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
    Tree_node* createLeafNode(Rf_data& data) {
        Tree_node* leaf = new Tree_node();
        leaf->setIsLeaf(true);

        // If the node is empty, assign a default label and return. This is a safeguard.
        if (data.allSamples.empty()) {
            leaf->setLabel(0);
            return leaf;
        }

        // FIX: Use a robust two-pass approach to find the majority label.
        // This avoids order-dependent bias that harms multi-class accuracy.

        // Pass 1: Count occurrences of each label.
        uint16_t labelCounts[this->numLabels] = {0};
        for (const auto& entry : data.allSamples) {
            if (entry.second.label < this->numLabels) {
                labelCounts[entry.second.label]++;
            }
        }

        // Pass 2: Find the label with the highest count.
        // This deterministically finds the majority and breaks ties by choosing the lower-indexed label.
        uint16_t maxCount = 0;
        uint8_t majorityLabel = 0; 
        for (uint8_t i = 0; i < this->numLabels; i++) {
            if (labelCounts[i] > maxCount) {
                maxCount = labelCounts[i];
                majorityLabel = i;
            }
        }

        leaf->setLabel(majorityLabel);
        return leaf;
    }

    Tree_node* buildTree(Rf_data &a, uint8_t min_split, uint16_t max_depth, bool use_Gini) {
        Tree_node* node = new Tree_node();

        unordered_set<uint8_t> labels;            // Set of labels  
        for (auto const& [key, val] : a.allSamples) {
            labels.insert(val.label);
        }
        
        // All samples have the same label, mark node as leaf 
        if (labels.size() == 1) {
            node->setIsLeaf(true);
            node->setLabel(*labels.begin());
            return node;
        }

        // Too few samples to split or max depth reached 
        if (a.allSamples.size() < min_split || max_depth == 0) {
            delete node;
            return createLeafNode(a);
        }

        uint8_t num_selected_features = static_cast<uint8_t>(sqrt(numFeatures));
        if (num_selected_features == 0) num_selected_features = 1; // always select at least one feature

        unordered_set<uint16_t> selectedFeatures;
        selectedFeatures.reserve(num_selected_features);
        while (selectedFeatures.size() < num_selected_features) {
            uint16_t idx = static_cast<uint16_t>(esp_random() % numFeatures);
            selectedFeatures.insert(idx);
        }
        
        // OPTIMIZED: Find the best split (feature and threshold) in one go.
        SplitInfo bestSplit = findBestSplit(a, selectedFeatures, use_Gini);

        // Poor split - create leaf. Gain for the true binary split is smaller than the
        // old multi-way gain, so the threshold must be adjusted.
        float gain_threshold = use_Gini ? this->impurity_threshold/2 : this->impurity_threshold;
        if (bestSplit.gain <= gain_threshold) {
            delete node;
            return createLeafNode(a);
        }
    
        // Set node properties from the best split found
        node->featureID = bestSplit.featureID;
        node->setThreshold(bestSplit.threshold);
        
        // Create left and right datasets based on the threshold
        Rf_data leftData, rightData;
        
        for (const auto& sample : a.allSamples) {
            if (sample.second.features[bestSplit.featureID] <= bestSplit.threshold) {
                leftData.allSamples[sample.first] = sample.second;
            } else {
                rightData.allSamples[sample.first] = sample.second;
            }
        }
        
        // Build children recursively
        // LEFT branch
        if (!leftData.allSamples.empty()) {
            node->children.first = buildTree(leftData, min_split, max_depth - 1, use_Gini);
        } else {
            // This case should be rare if gain > 0, but as a fallback, create a leaf from the parent data
            node->children.first = createLeafNode(a);
        }
        // RIGHT branch
        if (!rightData.allSamples.empty()) {
            node->children.second = buildTree(rightData, min_split, max_depth - 1, use_Gini);
        } else {
            // Fallback
            node->children.second = createLeafNode(a);
        }
        
        return node;
    }
     

    // 
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
    pair<float,float> get_training_evaluation_index(){
        Serial.println("Get training evaluation index... ");
    
        uint8_t buffer_chunk = train_backup.size() / 4; // Load data in chunks of 20% of the training set size
        if(buffer_chunk < 10) buffer_chunk = 10; // Ensure at least one sample is processed at a time
        uint16_t start_pos , end_pos;
        sample_set train_samples_buffer;
        b_vector<uint16_t> sampleIDs_bag;

        train_samples_buffer.reserve(buffer_chunk); // Reserve space for the buffer
        sampleIDs_bag.reserve(buffer_chunk); // Reserve space for sample IDs

        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[numLabels] = {0};
        uint16_t oob_fp[numLabels] = {0};
        uint16_t oob_fn[numLabels] = {0};

        uint16_t valid_tp[numLabels] = {0};
        uint16_t valid_fp[numLabels] = {0};
        uint16_t valid_fn[numLabels] = {0};

        uint16_t oob_correct = 0, oob_total = 0,
                 valid_correct = 0, valid_total = 0;

        loadForest(); // Load all trees into RAM
        checkHeapFragmentation();
        
        train_backup.sort(); // Ensure training backup is sorted

        // OOB part:
        for(start_pos = 0; end_pos < train_backup.size(); start_pos += buffer_chunk){
            end_pos = start_pos + buffer_chunk;
            if(end_pos > train_backup.size()) end_pos = train_backup.size(); // Ensure we don't exceed the number of samples

            sampleIDs_bag.clear(); // Clear the bag for the current chunk
            for(uint16_t i = start_pos; i < end_pos; i++){
                sampleIDs_bag.push_back(train_backup[i]); // Fill the bag with sample IDs for the current chunk
            }
            train_samples_buffer.clear(); // Clear the buffer for the current chunk
            train_samples_buffer = train_data.loadData(sampleIDs_bag); // Load the current chunk of training samples
            if(train_samples_buffer.empty()){
                Serial.println("❌ No training samples found in the buffer!");
                //  switch to plan B: clear all and load whole training set into RAM (do once per evaluation)
                Serial.println("Switching to plan B: loading all training data into RAM...");
                releaseForest(); // Release trees from RAM before loading all data
                bool preloaded = train_data.isLoaded;
                if(preloaded) train_data.loadData(true); // Load all training data into RAM
                train_samples_buffer = train_data.allSamples; // Move all samples into the buffer
                if(train_samples_buffer.empty()){
                    Serial.println("❌ No training samples found in RAM!");
                    return make_pair(0.0f, 0.0f);
                }
                //clear previous confusion matrices
                for(uint8_t i = 0; i < numLabels; i++){
                    oob_tp[i] = 0;
                    oob_fp[i] = 0;
                    oob_fn[i] = 0;
                }
                // Reset counters
                oob_correct = 0;
                oob_total = 0;

                end_pos = train_backup.size(); // signal to end of the loop. no need to load more chunks
                if(!preloaded) train_data.releaseData(true); // Release data from RAM
                checkHeapFragmentation();
                loadForest(); // Reload trees into RAM after releasing data
            }
            for(auto sample : train_samples_buffer){                
                uint16_t sampleId = sample.first;  // Get the sample ID
             
                // Find all trees whose OOB set contains this sampleId
                b_vector<uint8_t, SMALL> activeTrees;
                activeTrees.reserve(numTree);
                
                for(uint8_t i = 0; i < numTree; i++){
                    if(dataList[i].second.find(sampleId) != dataList[i].second.end()){
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
                    uint8_t predict = root[treeIdx].predictSample(sample.second);
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
                
                // Check certainty threshold
                float certainty = static_cast<float>(maxVotes) / oobTotalPredict;
                if(certainty < unity_threshold) {
                    continue; // Skip uncertain predictions
                }
                
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
        }

        // Validation part: if validation is enabled, evaluate on the validation set
        if(this->use_validation){   
            // validation evaluation: iterate over all samples in the validation set 
            validation_data.loadData(true); // Load validation data into RAM
            if(validation_data.allSamples.empty()){
                Serial.println("❌ No validation samples found in RAM!");
                return make_pair(0.0f, 0.0f);
            }
            for(auto sample : validation_data.allSamples){
                uint16_t sampleId = sample.first;  // Get the sample ID
                uint8_t actualLabel = sample.second.label;

                // Predict using all trees
                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                for(uint8_t i = 0; i < numTree; i++){
                    uint8_t predict = root[i].predictSample(sample.second);
                    if(predict < numLabels){
                        validPredictClass[predict]++;
                        validTotalPredict++;
                    }
                }

                if(validTotalPredict == 0) continue;

                // Find the most predicted class from all trees
                uint8_t validPredictedLabel = 255;
                uint16_t maxVotes = 0;
                for(const auto& predict : validPredictClass){
                    if(predict.second > maxVotes){
                        maxVotes = predict.second;
                        validPredictedLabel = predict.first;
                    }
                }

                // Check certainty threshold
                float certainty = static_cast<float>(maxVotes) / validTotalPredict;
                if(certainty < unity_threshold) {
                    continue; // Skip uncertain predictions
                }

                // Update confusion matrix
                valid_total++;
                if(validPredictedLabel == actualLabel){
                    valid_correct++;
                    valid_tp[actualLabel]++;
                } else {
                    valid_fn[actualLabel]++;
                    if(validPredictedLabel < numLabels){
                        valid_fp[validPredictedLabel]++;
                    }
                }
                // Serial.printf("Validation sample %d: Predicted %d, Actual %d\n", sampleId, validPredictedLabel, actualLabel);
            }
            validation_data.releaseData(true); // Release validation data from RAM
        }
        
        Serial.printf("Ram before releasing trees: %d\n", ESP.getFreeHeap());
        releaseForest(); // Release trees from RAM after evaluation
        Serial.printf("Ram after releasing trees: %d\n", ESP.getFreeHeap());

        // Calculate the requested metric
float oob_result = 0.0f;
        float valid_result = 0.0f;
        float combined_oob_result = 0.0f;
        float combined_valid_result = 0.0f;
        uint8_t training_flag = static_cast<uint8_t>(this->trainFlag);
        uint8_t numFlags = 0;
        
        if(oob_total == 0){
            Serial.println("❌ No valid OOB predictions found!");
            return make_pair(oob_result, valid_result);
        }
        
        if(training_flag & ACCURACY){
            oob_result = static_cast<float>(oob_correct) / oob_total;
            valid_result = static_cast<float>(valid_correct) / valid_total;
            Serial.printf("OOB Accuracy: %.3f (%d/%d)\n", oob_result, oob_correct, oob_total);
            Serial.printf("Validation Accuracy: %.3f (%d/%d)\n", valid_result, valid_correct, valid_total);
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & PRECISION){
            float oob_totalPrecision = 0.0f, 
                    valid_totalPrecision = 0.0f;
            uint8_t oob_validLabels = 0;
            uint8_t valid_validLabels = 0;
            for(uint8_t label = 0; label < numLabels; label++){
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
            Serial.printf("OOB Precision: %.3f\n", oob_result);
            Serial.printf("Validation Precision: %.3f\n", valid_result);
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & RECALL){
            float oob_totalRecall = 0.0f, valid_totalRecall = 0.0f;
            uint8_t oob_validLabels = 0, valid_validLabels = 0;
            for(uint8_t label = 0; label < numLabels; label++){
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
            Serial.printf("OOB Recall: %.3f\n", oob_result);
            Serial.printf("Validation Recall: %.3f\n", valid_result);
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & F1_SCORE) {
            float oob_totalF1 = 0.0f, 
                    valid_totalF1 = 0.0f;
            uint8_t oob_validLabels = 0, 
                    valid_validLabels = 0;
            for(uint8_t label = 0; label < numLabels; label++){
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
            Serial.printf("Validation F1-Score: %.3f\n", valid_result);
            Serial.printf("OOB F1-Score: %.3f\n", oob_result);
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        return make_pair(combined_oob_result / numFlags, 
                        combined_valid_result / numFlags);
    }


    // Rebuild forest with existing data but new parameters - enhanced for SPIFFS
    void rebuildForest() {
        // Clear existing trees properly
        for (uint8_t i = 0; i < root.size(); i++) {
            if (root[i].root != nullptr) {
                root[i].clearTree(); // Properly clear the tree from memory
            }
        }
        Serial.print("Rebuilding sub_tree: ");
        for(uint8_t i = 0; i < numTree; i++){
            // Load data for this tree
            dataList[i].first.loadData(true);
            Serial.printf("%d, ", i);
            
            // Memory check before building tree
            if (ESP.getFreeHeap() < 3000) {
                Serial.printf("\n⚠️ Low memory (%d bytes) before building tree %d\n", 
                            ESP.getFreeHeap(), i);
                // Force garbage collection attempt
                yield();
                if (ESP.getFreeHeap() < 2000) {
                    Serial.printf("❌ Insufficient memory to build tree %d\n", i);
                    dataList[i].first.releaseData(true);
                    continue; // Skip this tree
                }
            }
            // Build new tree
            Tree_node* rootNode = buildTree(dataList[i].first, minSplit, maxDepth, use_Gini);
            Rf_tree& tree = root[i];
            
            // Clean up any existing root (safety check)
            if(tree.root != nullptr) {
                tree.clearTree(); // Ensure complete cleanup
            }
            tree.root = rootNode;           // Assign the new root node
            tree.isLoaded = true;           // Mark the tree as loaded
            // Verify tree was built successfully
            if (rootNode == nullptr) {
                Serial.printf("❌ Failed to build tree %d\n", i);
                dataList[i].first.releaseData(true);
                continue;
            }
            dataList[i].first.releaseData(true);
            tree.releaseTree(true); 
            yield();
        }
        // Final memory cleanup
        yield();
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

    void training(int epochs, float combine_ratio = 0.5, bool early_stop = true){
        Serial.println("----------- Training started ----------");
        checkHeapFragmentation();
        
        // Core tracking variables (stack-based for embedded)
        float best_oob_score = 0.0f;
        float best_valid_score = 0.0f;
        float current_oob_score = 0.0f;
        float current_valid_score = 0.0f;
        float best_combined_score = 0.0f;
        float current_combined_score = 0.0f;
        
        uint8_t no_improvement_count = 0;
        const uint8_t early_stop_patience = 3;
        const float min_improvement = 0.003f; // Reduced for smaller datasets
        const float difficult_threshold = 0.82f; // Adjusted based on your findings
        
        // Adaptive parameters based on dataset characteristics
        uint8_t baseline_ratio = 100 * (this->numSamples / 500 + 1);
        if (baseline_ratio > 500) baseline_ratio = 500;
        uint8_t min_minSplit = max(3, this->numSamples / baseline_ratio);
        uint8_t max_minSplit = min(12, this->numSamples / 50);
        uint8_t base_depth = min(static_cast<uint8_t>(log2(this->numSamples)), 
                                static_cast<uint8_t>(log2(this->numFeatures) * 1.5f));
        uint8_t max_maxDepth = min(8, (int)base_depth);
        uint8_t min_maxDepth = 3;
        
        // Best state storage
        uint8_t best_minSplit = minSplit;
        uint16_t best_maxDepth = maxDepth;
        
        // Parameter optimization state
        bool adjusting_minSplit = true;
        bool is_difficult_dataset = false;
        bool parameters_optimal = false;
        bool minSplit_reached_optimal = false;
        bool maxDepth_reached_optimal = false;
        
        // Enhanced evaluation system for randomness reduction
        uint8_t evaluation_phase = 0; // 0: normal, 1: first eval, 2: second eval
        float first_eval_score = 0.0f;
        float second_eval_score = 0.0f;
        bool parameter_changed_this_cycle = false;
        uint8_t prev_minSplit = minSplit;
        uint16_t prev_maxDepth = maxDepth;
        
        // Dynamic combine ratio based on dataset analysis
        if(!this->use_validation){
            combine_ratio = 0.0f;
            Serial.println("No validation set - using OOB-only evaluation");
        } else {
            // Adaptive combine ratio based on dataset difficulty and size
            float size_factor = min(1.0f, this->numSamples / 5000.0f);
            float label_balance = this->lowest_distribution * this->numLabels;
            
            // For difficult datasets: favor OOB (more conservative)
            // For easy datasets: balance OOB and validation
            combine_ratio = 0.4f + (0.4f * size_factor) + (0.2f * label_balance);
            if(combine_ratio > 0.7f) combine_ratio = 0.7f;
            
            Serial.printf("Adaptive combine_ratio: %.2f (size_factor: %.2f, balance: %.2f)\n", 
                        combine_ratio, size_factor, label_balance);
        }
        
        Serial.printf("Parameter ranges: minSplit[%d-%d], maxDepth[%d-%d]\n", 
                    min_minSplit, max_minSplit, min_maxDepth, max_maxDepth);
        
        // Get initial evaluation with double-check for stability
        Serial.println("Initial evaluation (double-check for stability)...");
        pair<float,float> eval1 = get_training_evaluation_index();
        rebuildForest(); // Rebuild to account for randomness
        pair<float,float> eval2 = get_training_evaluation_index();
        
        // Use average of two evaluations for more stable baseline
        current_oob_score = (eval1.first + eval2.first) / 2.0f;
        current_valid_score = (eval1.second + eval2.second) / 2.0f;
        current_combined_score = current_valid_score * combine_ratio + current_oob_score * (1.0f - combine_ratio);
        
        float score_variance = abs(eval1.first - eval2.first) + abs(eval1.second - eval2.second);
        Serial.printf("Score variance between builds: %.4f (lower is better)\n", score_variance);
        
        // Determine dataset difficulty using both scores
        if(this->use_validation) {
            is_difficult_dataset = (current_oob_score < difficult_threshold) || 
                                (current_valid_score < difficult_threshold) ||
                                (score_variance > 0.1f); // High variance indicates difficulty
        } else {
            is_difficult_dataset = (current_oob_score < difficult_threshold) || (score_variance > 0.1f);
        }
        
        if(is_difficult_dataset){
            Serial.printf("🔴 Difficult/unstable dataset (combined: %.4f, variance: %.4f)\n", 
                        current_combined_score, score_variance);
            Serial.println("Strategy: Conservative parameter changes, double evaluation");
        } else {
            Serial.printf("🟢 Stable dataset (combined: %.4f, variance: %.4f)\n", 
                        current_combined_score, score_variance);
            Serial.println("Strategy: Standard parameter optimization");
        }
        
        // Initialize best scores
        best_oob_score = current_oob_score;
        best_valid_score = current_valid_score;
        best_combined_score = current_combined_score;
        
        saveBestState();
        Serial.printf("Baseline scores - OOB: %.4f, Validation: %.4f, Combined: %.4f\n", 
                    current_oob_score, current_valid_score, current_combined_score);
        
        for(int epoch = 1; epoch <= epochs; epoch++){
            Serial.printf("\n--- Epoch %d/%d ---\n", epoch, epochs);
            
            bool should_change_parameter = (evaluation_phase == 0) && !parameters_optimal;
            
            // Parameter adjustment phase
            if(should_change_parameter){
                prev_minSplit = minSplit;
                prev_maxDepth = maxDepth;
                
                if(adjusting_minSplit && !minSplit_reached_optimal){
                    Serial.print("Adjusting minSplit: ");
                    
                    if(is_difficult_dataset){
                        if(minSplit < max_minSplit){
                            minSplit++;
                            parameter_changed_this_cycle = true;
                            Serial.printf("increased to %d (reduce overfitting)\n", minSplit);
                        } else {
                            Serial.println("reached maximum");
                            minSplit_reached_optimal = true;
                        }
                    } else {
                        if(minSplit > min_minSplit){
                            minSplit--;
                            parameter_changed_this_cycle = true;
                            Serial.printf("decreased to %d (increase complexity)\n", minSplit);
                        } else {
                            Serial.println("reached minimum");
                            minSplit_reached_optimal = true;
                        }
                    }
                } else if(!maxDepth_reached_optimal){
                    Serial.print("Adjusting maxDepth: ");
                    adjusting_minSplit = false;
                    
                    if(is_difficult_dataset){
                        if(maxDepth > min_maxDepth){
                            maxDepth--;
                            parameter_changed_this_cycle = true;
                            Serial.printf("decreased to %d (reduce overfitting)\n", maxDepth);
                        } else {
                            Serial.println("reached minimum");
                            maxDepth_reached_optimal = true;
                        }
                    } else {
                        if(maxDepth < max_maxDepth){
                            maxDepth++;
                            parameter_changed_this_cycle = true;
                            Serial.printf("increased to %d (increase complexity)\n", maxDepth);
                        } else {
                            Serial.println("reached maximum");
                            maxDepth_reached_optimal = true;
                        }
                    }
                } else {
                    Serial.println("Both parameters reached optimal limits");
                    parameters_optimal = true;
                }
                
                if(parameter_changed_this_cycle){
                    evaluation_phase = 1; // Start double evaluation
                    Serial.println("Parameter changed - starting double evaluation cycle");
                }
            }
            
            // Build and evaluate
            Serial.printf("RAM before rebuild: %d bytes\n", ESP.getFreeHeap());
            rebuildForest();
            Serial.printf("RAM after rebuild: %d bytes\n", ESP.getFreeHeap());
            
            pair<float,float> evaluation_result = get_training_evaluation_index();
            float eval_oob = evaluation_result.first;
            float eval_valid = evaluation_result.second;
            float eval_combined = eval_valid * combine_ratio + eval_oob * (1.0f - combine_ratio);
            
            Serial.printf("Evaluation %d - OOB: %.4f, Validation: %.4f, Combined: %.4f\n", 
                        evaluation_phase + 1, eval_oob, eval_valid, eval_combined);
            
            // Handle evaluation phases
            if(evaluation_phase == 1){
                // First evaluation after parameter change
                first_eval_score = eval_combined;
                evaluation_phase = 2;
                Serial.println("First evaluation complete, performing second evaluation...");
                continue; // Go to next epoch for second evaluation
                
            } else if(evaluation_phase == 2){
                // Second evaluation after parameter change
                second_eval_score = eval_combined;
                evaluation_phase = 0; // Reset for next cycle
                
                // Use average of two evaluations for decision
                float avg_eval_score = (first_eval_score + second_eval_score) / 2.0f;
                float eval_variance = abs(first_eval_score - second_eval_score);
                
                Serial.printf("Double evaluation - Avg: %.4f, Variance: %.4f\n", 
                            avg_eval_score, eval_variance);
                
                // High variance indicates unreliable results - be more conservative
                float effective_improvement = avg_eval_score - best_combined_score;
                if(eval_variance > 0.05f) {
                    effective_improvement -= (eval_variance * 0.5f); // Penalty for high variance
                    Serial.printf("High variance penalty applied: %.4f\n", eval_variance * 0.5f);
                }
                
                current_oob_score = (evaluation_result.first + eval_oob) / 2.0f; // Average of last two
                current_valid_score = (evaluation_result.second + eval_valid) / 2.0f;
                current_combined_score = avg_eval_score;
                
                // Decision making based on averaged results
                if(effective_improvement > min_improvement){
                    // Parameter change was beneficial
                    best_combined_score = current_combined_score;
                    best_oob_score = current_oob_score;
                    best_valid_score = current_valid_score;
                    best_minSplit = minSplit;
                    best_maxDepth = maxDepth;
                    no_improvement_count = 0;
                    
                    saveBestState();
                    Serial.printf("✅ Parameter change beneficial: %.4f improvement\n", effective_improvement);
                    
                } else {
                    // Parameter change was not beneficial - revert
                    Serial.printf("📉 Parameter change not beneficial: %.4f change\n", effective_improvement);
                    minSplit = prev_minSplit;
                    maxDepth = prev_maxDepth;
                    
                    // Mark parameter as reached optimal
                    if(adjusting_minSplit){
                        Serial.println("minSplit reached optimal, switching to maxDepth");
                        minSplit_reached_optimal = true;
                        adjusting_minSplit = false;
                    } else {
                        Serial.println("maxDepth reached optimal, parameters complete");
                        maxDepth_reached_optimal = true;
                        parameters_optimal = true;
                    }
                    
                    // Restore best state
                    restoreBestState();
                    current_combined_score = best_combined_score;
                    current_oob_score = best_oob_score;
                    current_valid_score = best_valid_score;
                    
                    Serial.printf("🔄 Reverted to: minSplit=%d, maxDepth=%d, score=%.4f\n", 
                                minSplit, maxDepth, current_combined_score);
                }
                
                parameter_changed_this_cycle = false;
                
            } else {
                // Normal evaluation (no parameter change)
                current_oob_score = eval_oob;
                current_valid_score = eval_valid;
                current_combined_score = eval_combined;
                
                if(current_combined_score > best_combined_score + min_improvement){
                    best_combined_score = current_combined_score;
                    best_oob_score = current_oob_score;
                    best_valid_score = current_valid_score;
                    best_minSplit = minSplit;
                    best_maxDepth = maxDepth;
                    no_improvement_count = 0;
                    
                    saveBestState();
                    Serial.printf("✅ New best score: %.4f\n", best_combined_score);
                } else {
                    if(parameters_optimal){
                        no_improvement_count++;
                        Serial.printf("⚠️ No improvement (%d/%d) in final optimization\n", 
                                    no_improvement_count, early_stop_patience);
                    }
                }
            }
            
            // Early stopping (only in final optimization phase)
            if(early_stop && parameters_optimal && no_improvement_count >= early_stop_patience){
                Serial.printf("🛑 Early stopping: no improvement for %d epochs\n", early_stop_patience);
                break;
            }
            
            // Progress report
            const char* phase_str = "final optimization";
            if(!parameters_optimal){
                if(evaluation_phase > 0){
                    phase_str = "evaluating change";
                } else if(adjusting_minSplit){
                    phase_str = "optimizing minSplit";
                } else {
                    phase_str = "optimizing maxDepth";
                }
            }
            
            Serial.printf("Progress: epoch %d/%d, best: %.4f, phase: %s\n", 
                        epoch, epochs, best_combined_score, phase_str);
            
            checkHeapFragmentation();
            yield();
        }
        
        // Final restoration if needed
        if(current_combined_score < best_combined_score - min_improvement){
            Serial.println("📥 Final restoration to best state...");
            minSplit = best_minSplit;
            maxDepth = best_maxDepth;
            restoreBestState();
            
            pair<float,float> final_eval = get_training_evaluation_index();
            current_oob_score = final_eval.first;
            current_valid_score = final_eval.second;
            current_combined_score = current_valid_score * combine_ratio + current_oob_score * (1.0f - combine_ratio);
        }
        
        cleanupBestState();
        
        // Training summary
        Serial.println("\n----------- Training completed ----------");
        Serial.printf("Dataset characteristics: %s, variance-adjusted\n", 
                    is_difficult_dataset ? "Difficult/unstable" : "Stable");
        Serial.printf("Final params: minSplit=%d, maxDepth=%d\n", best_minSplit, best_maxDepth);
        Serial.printf("Best scores - OOB: %.4f, Validation: %.4f, Combined: %.4f\n", 
                    best_oob_score, best_valid_score, best_combined_score);
        Serial.printf("Final scores - OOB: %.4f, Validation: %.4f, Combined: %.4f\n", 
                    current_oob_score, current_valid_score, current_combined_score);
        
        if(this->use_validation) {
            float oob_valid_diff = abs(best_oob_score - best_valid_score);
            Serial.printf("OOB-Validation difference: %.4f %s\n", oob_valid_diff,
                        oob_valid_diff > 0.1f ? "(high - may indicate overfitting)" : "(acceptable)");
        }
        
        checkHeapFragmentation();
        Serial.println("Training completed with variance-aware optimization");
    }


    // Add second-best state management functions
    private:

    // Save current forest state as best state (memory-efficient)
    void saveBestState(){
        Serial.print("💾 Saving best state... ");
        
        // Save each tree with best_ prefix
        for(uint8_t i = 0; i < numTree; i++){
            String currentFile = root[i].filename;
            String bestFile = String("/best_tree_") + String(i) + ".bin";
            
            // Copy current tree file to best state file
            if(SPIFFS.exists(currentFile.c_str())){
                if(!cloneTreeFile(currentFile, bestFile)){
                    Serial.printf("❌ Failed to save tree %d\n", i);
                    return;
                }
            }
        }
        Serial.println("✅ Done");
    }

    // Restore forest from best state
    void restoreBestState(){
        Serial.print("📥 Restoring best state... ");
        
        // Clear current forest state
        for(uint8_t i = 0; i < numTree; i++){
            root[i].clearTree();
        }
        
        // Restore from best state files
        for(uint8_t i = 0; i < numTree; i++){
            String bestFile = String("/best_tree_") + String(i) + ".bin";
            String currentFile = String("/tree_") + String(i) + ".bin";
            
            if(SPIFFS.exists(bestFile.c_str())){
                if(!cloneTreeFile(bestFile, currentFile)){
                    Serial.printf("❌ Failed to restore tree %d\n", i);
                    return;
                }
                // Update tree filename
                root[i].filename = currentFile;
                root[i].isLoaded = false;
            }
        }
        Serial.println("✅ Done");
    }

    // Cleanup best state files to free SPIFFS space
    void cleanupBestState(){
        Serial.print("🗑️ Cleaning up best state... ");
        
        for(uint8_t i = 0; i < numTree; i++){
            String bestFile = String("/best_tree_") + String(i) + ".bin";
            if(SPIFFS.exists(bestFile.c_str())){
                SPIFFS.remove(bestFile.c_str());
            }
        }
        Serial.println("✅ Done");
    }

    // Memory-efficient file cloning for tree states
    bool cloneTreeFile(const String& src, const String& dest){
        File srcFile = SPIFFS.open(src.c_str(), FILE_READ);
        if(!srcFile){
            return false;
        }
        
        // Remove destination if exists
        if(SPIFFS.exists(dest.c_str())){
            SPIFFS.remove(dest.c_str());
        }
        
        File destFile = SPIFFS.open(dest.c_str(), FILE_WRITE);
        if(!destFile){
            srcFile.close();
            return false;
        }
        
        // Copy in small chunks to minimize RAM usage
        uint8_t buffer[64]; // Small buffer for embedded systems
        size_t bytesRead;
        
        while((bytesRead = srcFile.read(buffer, sizeof(buffer))) > 0){
            if(destFile.write(buffer, bytesRead) != bytesRead){
                srcFile.close();
                destFile.close();
                SPIFFS.remove(dest.c_str());
                return false;
            }
            yield(); // Prevent watchdog timeout
        }
        
        srcFile.close();
        destFile.close();
        return true;
    }

    void remove_tree(uint8_t treeId) {
        if (treeId < numTree) {
            root[treeId].clearTree(); // Clear the tree from memory
            root[treeId].isLoaded = false; // Mark as unloaded
            dataList[treeId].first.purgeData(); // Release data from RAM
            dataList[treeId].second.clear(); // Clear the OOB set for this tree
            numTree--; // Decrease the number of trees
            Serial.printf("Tree %d removed. Remaining trees: %d\n", treeId, numTree);
        }
    }
    void add_tree(){
    }
  public:

    // New combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
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
        b_vector<pair<uint8_t, float>> precisions, recalls, f1s, accuracies;
        
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
        
        b_vector<b_vector<pair<uint8_t, float>>> result;
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
        b_vector<pair<uint8_t, float>> prec = predict(data)[0];
        float total_prec = 0.0f;
        for (const auto& p : prec) {
            total_prec += p.second;
        }
        return total_prec / prec.size();
    }

    float recall(Rf_data& data) {
        b_vector<pair<uint8_t, float>> rec = predict(data)[1];
        float total_rec = 0.0f;
        for (const auto& r : rec) {
            total_rec += r.second;
        }
        return total_rec / rec.size();
    }

    float f1_score(Rf_data& data) {
        b_vector<pair<uint8_t, float>> f1 = predict(data)[2];
        float total_f1 = 0.0f;
        for (const auto& f : f1) {
            total_f1 += f.second;
        }
        return total_f1 / f1.size();
    }

    float accuracy(Rf_data& data) {
        b_vector<pair<uint8_t, float>> acc = predict(data)[3];
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

    // const char* filename = "/categorical_data.csv";    // easy dataset : use_Gini = false | boostrap = true; 97% - sklearn 95%.
    // const char* filename = "/walker_fall.csv";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "/digit_data.csv"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5)

    RandomForest forest = RandomForest(filename, 20, false, true);
    forest.trainFlag |= Rf_training_flags::ACCURACY;

    forest.MakeForest();
    // forest.Prunning();
    forest.training(15); 

    auto result = forest.predict(forest.test_data);
    Serial.printf("\nlowest RAM: %d\n", lowest_ram);

    // Calculate Precision
    Serial.println("Precision in test set:");
    b_vector<pair<uint8_t, float>> precision = result[0];
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
    b_vector<pair<uint8_t, float>> recall = result[1];
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
    b_vector<pair<uint8_t, float>> f1_scores = result[2];
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
    b_vector<pair<uint8_t, float>> accuracies = result[3];
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
