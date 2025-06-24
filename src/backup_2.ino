#include "STL_MCU.h"  
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"


using namespace mcu;

// flag for Rf_data : base data, training data, subset data (for each tree in forest), etc.
typedef enum rf_flag{
    BASE_DATA = 0,          // base data, used for initial training
    TRAINING_DATA,          // training data, used for training the forest
    SUBSET_DATA,            // subset data, used for each tree in the forest
    TESTING_DATA           // testing data, used for evaluating the model
}rf_flag;

typedef struct Rf_sample{
    packed_vector<2, SMALL> features;           // set containing the values ​​of the features corresponding to that sample 
    uint8_t label;                     // label of the sample 
}Rf_sample;

class Tree_node {
  public:
    uint8_t featureID;                 
    uint8_t branchValue;                
    uint8_t label;                
    b_vector<Tree_node*, SMALL> children;        
    bool is_leaf = false;
    
    // SPIFFS management
    String filename;
    bool isLoaded = false;
    bool isModified = false;

    Tree_node() : filename(""), isLoaded(true) {} // In-memory by default during construction
    
    Tree_node(const String& fn) : filename(fn), isLoaded(false) {}

    ~Tree_node() {
        if (isLoaded && isModified && filename.length()) {
            releaseTree(); // Auto-save if modified
        }
        clearChildren();
    }

    void clearChildren() {
        for (Tree_node* child : children) {
            delete child;
        }
        children.clear();
    }

    // Load tree structure from SPIFFS using custom binary format
    void loadTree() {
        if (isLoaded || !filename.length()) return;
        
        File file = SPIFFS.open(filename.c_str(), FILE_READ);
        if (!file) {
            Serial.printf("❌ Failed to load tree: %s\n", filename.c_str());
            return;
        }

        // Read and verify header (magic number: 0x54524545 = "TREE" in hex)
        uint32_t magic = 0;
        if (file.readBytes((char*)&magic, 4) != 4 || magic != 0x54524545) {
            Serial.printf("❌ Invalid tree file header: %s\n", filename.c_str());
            file.close();
            return;
        }

        // Load root node
        if (!loadNodeFromFile(file, this)) {
            Serial.printf("❌ Failed to load tree data: %s\n", filename.c_str());
            file.close();
            return;
        }

        file.close();
        isLoaded = true;
        isModified = false;
        Serial.printf("✅ Tree loaded: %s\n", filename.c_str());
    }

    // Release tree structure to SPIFFS using custom binary format
    void releaseTree() {
        if (!isLoaded || !filename.length()) return;
        
  

        File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
        if (!file) {
            Serial.printf("❌ Failed to save tree: %s\n", filename.c_str());
            return;
        }

        // Write header (magic number: 0x54524545 = "TREE" in hex)
        uint32_t magic = 0x54524545;
        file.write((uint8_t*)&magic, 4);

        // Save tree recursively
        if (!saveNodeToFile(file, this)) {
            Serial.printf("❌ Failed to save tree data: %s\n", filename.c_str());
            file.close();
            return;
        }

        file.close();

        // Clear from RAM
        clearChildren();
        isLoaded = false;
        isModified = false;
        Serial.printf("✅ Tree saved: %s\n", filename.c_str());
    }

    // Ensure tree is loaded for prediction
    void ensureLoaded() {
        if (!isLoaded) {
            loadTree();
        }
    }

  private:
    // Binary format optimized for embedded: [flags(1)] [featureID(1)] [branchValue(1)] [label(1)] [num_children(1)]
    bool saveNodeToFile(File& file, Tree_node* node) {
        // Pack node data into 5 bytes
        uint8_t flags = node->is_leaf ? 0x01 : 0x00;
        uint8_t numChildren = (uint8_t)(node->children.size() & 0xFF);
        
        // Write node data
        if (file.write(flags) != 1) return false;
        if (file.write(node->featureID) != 1) return false;
        if (file.write(node->branchValue) != 1) return false;
        if (file.write(node->label) != 1) return false;
        if (file.write(numChildren) != 1) return false;

        // Write children recursively
        for (Tree_node* child : node->children) {
            if (!saveNodeToFile(file, child)) return false;
        }
        
        return true;
    }

    bool loadNodeFromFile(File& file, Tree_node* node) {
        // Read node data (5 bytes)
        uint8_t flags, numChildren;
        
        if (file.readBytes((char*)&flags, 1) != 1) return false;
        if (file.readBytes((char*)&node->featureID, 1) != 1) return false;
        if (file.readBytes((char*)&node->branchValue, 1) != 1) return false;
        if (file.readBytes((char*)&node->label, 1) != 1) return false;
        if (file.readBytes((char*)&numChildren, 1) != 1) return false;

        node->is_leaf = (flags & 0x01) != 0;

        // Load children
        if (numChildren > 0) {
            node->children.reserve(numChildren);
            for (uint8_t i = 0; i < numChildren; i++) {
                Tree_node* child = new Tree_node();
                if (!loadNodeFromFile(file, child)) {
                    delete child;
                    return false;
                }
                node->children.push_back(child);
            }
        }

        return true;
    }
};


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



    // Load data from binary format (main method used during runtime)
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
                    tree_index = static_cast<uint8_t>(index_str.toInt());
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

            Serial.printf("Loading %u samples with %u features from binary file\n", numSamples, numFeatures);

            if(!restore_yet) {
                // Calculate packed bytes needed for features (4 values per byte)
                uint16_t packedFeatureBytes = (numFeatures + 3) / 4; // Round up division
                
                // Read samples
                for(uint32_t sampleID = 0; sampleID < numSamples; sampleID++) {
                    Rf_sample s;
                    
                    // Read label (still 1 byte)
                    if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                        Serial.printf("❌ Failed to read label for sample %u\n", sampleID);
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
                        Serial.printf("❌ Failed to read packed features for sample %u\n", sampleID);
                        if(restore_data_callback){
                            restore_data_callback(flag, treeIndex);
                        }
                        break;
                    }
                    
                    // Unpack features from bytes
                    for(uint16_t i = 0; i < numFeatures; i++) {
                        uint16_t byteIndex = i / 4;
                        uint8_t bitOffset = (i % 4) * 2; // 2 bits per value
                        uint8_t mask = 0x03 << bitOffset; // 0b00000011 shifted
                        uint8_t feature = (packedBuffer[byteIndex] & mask) >> bitOffset;
                        s.features.push_back(feature);
                    }
                    s.features.fit();
                    
                    allSamples[sampleID] = s;
                }
            }
            allSamples.fit();
            isLoaded = true;
            Serial.println("✅ Binary data loaded from file.");
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

    // Save data to binary format and clear RAM
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
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4; // Round up division

            // Write samples
            for (auto entry = allSamples.begin(); entry != allSamples.end(); ++entry) {
                const Rf_sample& s = entry->second;
                
                // Write label (still 1 byte)
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
                    uint8_t bitOffset = (i % 4) * 2; // 2 bits per value
                    uint8_t feature_value = s.features[i] & 0x03; // Ensure only 2 bits
                    packedBuffer[byteIndex] |= (feature_value << bitOffset);
                }
                
                // Write packed bytes
                file.write(packedBuffer, packedFeatureBytes);
            }

            file.close();
        }
        
        // Clear memory
        allSamples.clear();
        allSamples.fit();
        isLoaded = false;
        Serial.println("✅ Data written to binary file and memory cleared.");
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

// ---------------------------------------------------------------------------------
vector<pair<unordered_set<uint16_t>,Rf_data>>  ClonesData(Rf_data& data, uint8_t numSubData) {
    Serial.println("<- clones data ->");
    if (!data.isLoaded) {
        data.loadData(true);
    }

    vector<pair<unordered_set<uint16_t>,Rf_data>> subDataList;
    uint16_t numSample = data.allSamples.size();  
    uint16_t numSubSample = numSample * 0.632;
    uint16_t oob_size = numSample - numSubSample;
    subDataList.reserve(numSubData);

    // Create a vector of all sample IDs for efficient random access
    vector<uint16_t> allSampleIds;
    allSampleIds.reserve(numSample);
    for (const auto& sample : data.allSamples) {
        allSampleIds.push_back(sample.first);
    }

    for (uint8_t i = 0; i < numSubData; i++) {
        Serial.printf("creating dataset for sub-tree : %d\n", i);
        Rf_data subData;
        subData.flag = SUBSET_DATA;
        
        // Track which samples are selected for this tree
        ChainedUnorderedSet<uint16_t> inBagSamples;
        inBagSamples.reserve(numSubSample);
        
        // Bootstrap sampling WITH replacement
        while(subData.allSamples.size() < numSubSample){
            uint16_t idx = static_cast<uint16_t>(esp_random() % numSample);
            uint16_t sampleId = allSampleIds[idx];
            
                if(data.allSamples.find(sampleId) != data.allSamples.end() &&
                   inBagSamples.find(sampleId) == inBagSamples.end()) {
                    // Add sample to subset data
                    inBagSamples.insert(sampleId);
                    sub_data.allSamples[sampleId] = data.allSamples[sampleId];
                }
        }
        subData.allSamples.fit();
        
        // Create OOB set with samples not used in this tree
        unordered_set<uint16_t> OOB;
        if(oob_size > 234){
          OOB.set_fullness(1.0f);
        }
        oob_size = oob_size % 255;
        OOB.reserve(oob_size);
        for (uint16_t id : allSampleIds) {
            if (inBagSamples.find(id) == inBagSamples.end()) {
                OOB.insert(id);
            }
        }
        // OOB.fit();

        // Create binary filename for sub-tree
        String newName = "/tree_" + String(i) + "_data.bin";
        subData.filename = newName;
        subData.isLoaded = true;
        checkHeapFragmentation();
        Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        subData.releaseData(); // Save as binary

        subDataList.push_back(make_pair(OOB, subData));
    }

    data.releaseData(true);
    return subDataList;
}


// ------------------------------------------------------------------------------

uint8_t predictClassSample(Tree_node* rootNode,  Rf_sample& s) {
    while (!rootNode->is_leaf) {
        uint8_t featureIndex = rootNode->featureID;
        float sampleValue = s.features[featureIndex];
        
        bool foundChild = false;
        for (const auto& childNode : rootNode->children) {    // find branch 
            if (childNode->branchValue == sampleValue) {    // match branch value 
                rootNode = childNode;
                foundChild = true;
                break;
            }
        }
        if (!foundChild) {
            return rootNode->label;
        }
    }
    return rootNode->label;
}

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
    vector<Tree_node*, SMALL> root;                     // vector storing root nodes of trees (now manages SPIFFS filenames)
    uint16_t maxDepth;
    uint8_t minSplit;
    uint8_t numTree;     
    uint8_t numFeatures;  
    uint8_t numLabels;
    uint16_t numSamples;  // number of samples in the base data
    vector<uint8_t> allFeaturesValue;     // value of all features
    vector<Rf_data> dataList;
    vector<unordered_set<uint16_t>> OOB;          // OOB set for each tree
    vector<uint16_t> train_backup;   // backup of training set sample IDs 
    float threshold;          // threshold for classification, effect to precision and recall
    trainingFlags trainFlag = trainingFlags::ACCURACY; // flags for training, default is accuracy

    // Pointer to the single instance
    static RandomForest* instance_ptr;

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
        
        threshold = 1.25f / static_cast<float>(numFeatures);
        if(numFeatures == 2) threshold = 0.4f;
        maxDepth = max_depth;
        minSplit = min_split;
        numTree = numtree;
        
        OOB.reserve(numTree);
        dataList.reserve(numTree);

        splitData(0.7, "train", "test");

        vector<pair<unordered_set<uint16_t>, Rf_data>> all_trees = ClonesData(train_data, numTree);
        for(auto& p : all_trees){
            OOB.push_back(p.first);
            dataList.push_back(p.second);
        }
        
    }
    
    // Enhanced destructor
    ~RandomForest(){
        // Clear forest safely
        clearForest();
        
        // Clear data safely
        train_data.purgeData();
        test_data.purgeData();
        // a.purgeData();
        
        // Clean vectors
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
            checkHeapFragmentation();
            Tree_node* rootNode = buildTree(dataList[i], minSplit, maxDepth);
            
            // Create SPIFFS filename for this tree
            String treeFilename = "/tree_" + String(i) + ".bin";
            rootNode->filename = treeFilename;
            
            // Save tree to SPIFFS and release from RAM
            rootNode->releaseTree();
            
            // Add to root vector (tree is now in SPIFFS)
            root.push_back(rootNode);
            
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
        train_sampleIDs.fit();

        for(uint16_t i = 0; i < totalSamples; ++i) {
            if (train_sampleIDs.find(i) == train_sampleIDs.end()) {
                test_sampleIDs.insert(i);
                train_backup.push_back(i);
            }
        }
        test_sampleIDs.fit();
        train_backup.sort();

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

        // Copy train samples
        for(auto sampleId : train_sampleIDs){
            train_data.allSamples[sampleId] = this->a.allSamples[sampleId];
        }
        // checkHeapFragmentation();
        Serial.printf("===> RAM left: %d\n", ESP.getFreeHeap());
        Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
        train_data.releaseData(); // Write to binary SPIFFS, clear RAM

        // Copy test samples  
        for(auto sampleId : test_sampleIDs){
            test_data.allSamples[sampleId] = this->a.allSamples[sampleId];
        }
        test_data.releaseData(); // Write to binary SPIFFS, clear RAM

        // Clean up source data
        this->a.purgeData();      // remove original dataset
    }

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
            if(data_flag == rf_flag::TESTING_DATA) {
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


            Serial.printf("Restoring subset data for tree %d...\n", treeIndex);
            uint16_t numSubSamples = train_backup.size() * 0.632; // Bootstrap sample size
            bool preload = train_data.isLoaded; // Check if train_data is already loaded
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
                
                // Check if sampleId exists in train_data
                if (train_data.allSamples.find(sampleId) != train_data.allSamples.end() && 
                    subsetSampleIDs.find(sampleId) == subsetSampleIDs.end()) {
                    // Add to subset if not already included
                    subsetSampleIDs.insert(sampleId);
                    subsetData.allSamples[sampleId] = train_data.allSamples[sampleId];
                }
            }
            subsetData.allSamples.fit();
            
            subsetData.isLoaded = true;

            // Save to SPIFFS
            subsetData.releaseData(); // Write to binary SPIFFS, clear RAM

            if(!preload){
                train_data.releaseData(true);
            }
            Serial.printf("Restore successful !");
        }else{      // base data
            return;
        }
    }

    // Add tree cleanup function - enhanced for SPIFFS management
    // FIXED: Safe tree deletion
    void deleteTree(Tree_node* node) {
        if (!node) return;
        
        // Clear children first to avoid recursion issues
        if (node->isLoaded) {
            node->clearChildren();
        }
        
        // Delete SPIFFS file if it exists
        if (node->filename.length() > 0) {
            if (SPIFFS.exists(node->filename.c_str())) {
                SPIFFS.remove(node->filename.c_str());
                Serial.printf("🗑️ Deleted tree file: %s\n", node->filename.c_str());
            }
        }
        
        delete node;
    }

    // FIXED: Enhanced forest cleanup
    void clearForest() {
        // Process trees one by one to avoid heap issues
        for (size_t i = 0; i < root.size(); i++) {
            if (root[i]) {
                deleteTree(root[i]);
                root[i] = nullptr;
            }
        }
        root.clear();
        root.fit();
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


    Tree_node* buildTree(Rf_data &a, uint8_t min_split, uint16_t max_depth) {
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
            int mostLab, max = -999;
            unordered_map<uint8_t, uint8_t> labelCount;
            for (const auto& sample : a.allSamples) {
                labelCount[sample.second.label]++;
            }
            for (const auto& label : labelCount) {
                if (label.second > max) {
                    max = label.second;
                    mostLab = label.first;          // assign most common label 
                }
            }
            node->label = mostLab;
            node->is_leaf = true;
            return node;
        }

        float maxIG = -1.0f;
        uint8_t num_selected_features = static_cast<uint8_t>(sqrt(numFeatures));
        if (num_selected_features == 0) num_selected_features = 1; // always select at least one feature

        unordered_set<uint16_t> selectedFeatures(num_selected_features);
        while (selectedFeatures.size() < num_selected_features) {
            uint16_t idx = static_cast<uint16_t>(esp_random() % numFeatures);
            selectedFeatures.insert(idx);
        }
        
        for (const auto& featureID : selectedFeatures) {          // iterate through selected features
            float IG = infoGain(a.allSamples, featureID);    // multiply by feature weight 
            if (IG > maxIG) {
                maxIG = IG;
                node->featureID = featureID; 
            }
        }
        // a.allFeatures.erase(node->featureID);
    
        // GROUPING AND RECURSION 
        for(uint8_t value : allFeaturesValue){                            
            Rf_data subData = Rf_data();                     // Sub Data with samples and features for next recursion 
            for (const auto& sample : a.allSamples) {
                if (sample.second.features[node->featureID] == value){
                    subData.allSamples[sample.first] = sample.second;     // assign samples 
                }
            }
            if (subData.allSamples.empty()) {                // no samples left to split 
                int mostLab, max = -999;
                unordered_map<uint8_t, int> labelCount;
                labelCount.reserve(subData.allSamples.size());
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
            }
            else {
                Tree_node* childNode = buildTree(subData,min_split,max_depth-1); 
                // max_depth--;                                 // increase tree depth by 1 
                childNode->branchValue = value;                 // assign branch value to child node
                node->children.push_back(childNode);
                node->children.fit();
            }
        }
        return node;
    }


    // Enhanced prediction with SPIFFS loading
    // fix the predClassSample function to handle multi-class better
    uint8_t predClassSample(Rf_sample& s){
        int16_t totalPredict = 0;
        unordered_map<uint8_t, uint8_t> predictClass;
        
        for(auto& node : root){
            if(!node->isLoaded){
              // Serial.println("Tree node not loaded yet! pls upload node into RAM before.. ");
              // return 255;
              node->loadTree();
            }
            uint8_t predict = predictClassSample(node, s);
            // Validate prediction is within expected label range
            if(predict < numLabels){  // Use numLabels instead of hardcoded check
                predictClass[predict]++;
                totalPredict++;
            }
        }
        
        if(predictClass.size() == 0 || totalPredict == 0) {
            return 255;  // No valid predictions
        }
        
        // Find the most predicted class
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
        if(certainty < threshold) {
            return 255;  // Uncertain prediction
        }
        
        return mostPredict;
    }

    // Enhanced OOB accuracy with SPIFFS management
    vector<float, SMALL> OOB_accuracy() {
        vector<float, SMALL> result(4, 0.0f); // {accuracy, precision, recall, f1_score}
        if (root.empty() || OOB.empty()) {
            Serial.println("No tree in forest. Ignoring..");
            return result;
        }

        // FIXED: Ensure train_data is loaded
        bool pre_load_data = train_data.isLoaded;
        if (!train_data.isLoaded) {
            train_data.loadData(true);
        }

        // FIXED: Pre-allocate arrays outside the loop to avoid memory issues
        uint32_t* tp = new uint32_t[numLabels]();
        uint32_t* fp = new uint32_t[numLabels]();
        uint32_t* fn = new uint32_t[numLabels]();
        uint8_t* votes = new uint8_t[numLabels]();  // Reuse this array
        
        uint32_t correct = 0, total = 0;

        Serial.printf("🔍 OOB Evaluation: %d samples, %d trees, %d labels\n", 
                    train_data.allSamples.size(), root.size(), numLabels);


        for (auto samplePair : train_data.allSamples) {
            uint16_t sampleId = samplePair.first;
            const Rf_sample& sample = samplePair.second;
            uint8_t actual = sample.label;

            // Validate actual label
            if (actual >= numLabels) {
                Serial.printf("⚠️ Invalid actual label %d for sample %d\n", actual, sampleId);
                continue;
            }

            // FIXED: Clear votes array for reuse
            memset(votes, 0, numLabels * sizeof(uint8_t));
            uint8_t voteCount = 0;

            // FIXED: Check OOB membership and collect votes
            for (size_t i = 0; i < root.size(); ++i) {
                // Check if this sample is OOB for tree i
                if (i < OOB.size() && OOB[i].find(sampleId) != OOB[i].end()) {
                    uint8_t pred = predictClassSample(root[i], const_cast<Rf_sample&>(sample));
                    
                    // Validate prediction
                    if (pred < numLabels) {
                        votes[pred]++;
                        voteCount++;
                    }
                }
            }

            // Skip samples with no OOB votes
            if (voteCount == 0) {
                continue;
            }

            // FIXED: Find majority vote
            uint8_t bestLabel = 0;
            uint8_t bestCount = 0;
            for (uint8_t l = 0; l < numLabels; ++l) {
                if (votes[l] > bestCount) {
                    bestCount = votes[l];
                    bestLabel = l;
                }
            }

            // FIXED: Update confusion matrix
            total++;
            if (bestLabel == actual) {
                correct++;
                tp[actual]++;
            } else {
                fp[bestLabel]++;
                fn[actual]++;
            }
        }

        // FIXED: Calculate metrics with proper validation
        float accuracy = (total == 0) ? 0.0f : static_cast<float>(correct) / total;

        float sum_prec = 0.0f, sum_rec = 0.0f, sum_f1 = 0.0f;
        int valid_labels = 0;

        for (int l = 0; l < numLabels; ++l) {
            uint32_t tpv = tp[l], fpv = fp[l], fnv = fn[l];
            
            // Only include labels that appear in the data
            if (tpv + fpv + fnv == 0) continue;
            
            float prec = (tpv + fpv == 0) ? 0.0f : float(tpv) / (tpv + fpv);
            float rec  = (tpv + fnv == 0) ? 0.0f : float(tpv) / (tpv + fnv);
            float f1   = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
            
            sum_prec += prec;
            sum_rec  += rec;
            sum_f1   += f1;
            valid_labels++;

            Serial.printf("Label %d: TP=%d, FP=%d, FN=%d, Prec=%.3f, Rec=%.3f, F1=%.3f\n", 
                        l, tpv, fpv, fnv, prec, rec, f1);
        }
        
        float precision = (valid_labels == 0) ? 0.0f : sum_prec / valid_labels;
        float recall    = (valid_labels == 0) ? 0.0f : sum_rec  / valid_labels;
        float f1_score  = (valid_labels == 0) ? 0.0f : sum_f1   / valid_labels;

        result[0] = accuracy;
        result[1] = precision;
        result[2] = recall;
        result[3] = f1_score;

        Serial.printf("📊 OOB Results: Acc=%.3f, Prec=%.3f, Rec=%.3f, F1=%.3f (from %d samples)\n", 
                    accuracy, precision, recall, f1_score, total);

        // FIXED: Clean up memory
        delete[] tp;
        delete[] fp;
        delete[] fn;
        delete[] votes;

        // FIXED: Restore train_data state
        if (!pre_load_data) {
            train_data.releaseData();
        }

        return result;
    }
    // helper function for training process
    pair<float, float> get_training_evaluation_index(trainingFlags flag){
        Serial.println("Get training evaluation index... ");
        float train_evaluation_index, oob_evalution_index;
        
        // Check if train_data is already loaded
        bool train_data_preloaded = train_data.isLoaded;
        if (!train_data_preloaded) {
            train_data.loadData(true);
        }
        
        // Check forest state and load if needed
        bool forest_preloaded = true;
        for (auto& tree : root) {
            if (!tree->isLoaded) {
                forest_preloaded = false;
                break;
            }
        }
        
        if (!forest_preloaded) {
            loadForest(forest_preloaded); // This will set forest_preloaded correctly
        }
        
        switch(flag){
            case trainingFlags::ACCURACY :
                train_evaluation_index = accuracy(train_data);
                oob_evalution_index = OOB_accuracy()[0];
                break;
            case trainingFlags::PRECISSION :
                train_evaluation_index = precision(train_data);
                oob_evalution_index = OOB_accuracy()[1];
                break;
            case trainingFlags::RECALL :
                train_evaluation_index = recall(train_data);
                oob_evalution_index = OOB_accuracy()[2];
                break;
            case trainingFlags::F1_SCORE :
                train_evaluation_index = f1_score(train_data);
                oob_evalution_index = OOB_accuracy()[3];
                break;
            default:
                train_evaluation_index = accuracy(train_data);
                oob_evalution_index = OOB_accuracy()[0];
                break;
        }
        checkHeapFragmentation();
        
        // Release resources only if they weren't preloaded
        if (!train_data_preloaded) {
            train_data.releaseData(true);
        }
        
        if (!forest_preloaded) {
            releaseForest(false);
        }
        
        return make_pair(train_evaluation_index, oob_evalution_index);
    }

    // Rebuild forest with existing data but new parameters - enhanced for SPIFFS
    void rebuildForest() {
        // Clear trees but preserve dataList structure
        for (size_t i = 0; i < root.size(); i++) {
            if (root[i]) {
                // Only delete tree structure, keep SPIFFS file for now
                if (root[i]->isLoaded) {
                    root[i]->clearChildren();
                    root[i]->isLoaded = false;
                }
                delete root[i];
                root[i] = nullptr;
            }
        }
        root.clear();
        
        for(uint8_t i = 0; i < dataList.size(); i++){
            dataList[i].loadData(true);
            Serial.printf("Rebuilding sub_tree: %d\n", i);
            checkHeapFragmentation();
            Tree_node* rootNode = buildTree(dataList[i], minSplit, maxDepth);
            
            // Create SPIFFS filename and save
            String treeFilename = "/tree_" + String(i) + ".bin";
            rootNode->filename = treeFilename;
            rootNode->releaseTree();
            
            dataList[i].releaseData(true);
            root.push_back(rootNode);
        }
        Serial.printf("RAM after forest rebuild: %d\n", ESP.getFreeHeap());
    }

    void loadForest(bool& preload){
        preload = true; // Assume all trees are already loaded
        for (auto& tree : root) {
            if (!tree->isLoaded) {
                tree->loadTree();
                preload = false; // At least one tree was not preloaded
            }
        }
    }

    // Fixed releaseForest function  
    void releaseForest(bool preload = false){
        if (!preload) {
            for(auto& tree : root){
                if (tree->isLoaded) {
                    tree->releaseTree();
                }
            }
        }
    }
  public:
    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------
    // train model : using OOB (Out of Bag) samples to evaluate the model
    // Enhanced training with incremental tree management
    // Fixed training function
    void training(int epochs, bool early_stop = false) {
        Serial.println("----------- Training started ----------");
        Serial.printf("Initial RAM: %d\n", ESP.getFreeHeap());
        checkHeapFragmentation();
        pair<float, float> initial_metrics = get_training_evaluation_index(trainFlag);
        float train_evaluation_index = initial_metrics.first;
        float oob_evaluation_index = initial_metrics.second;
        float overfit = train_evaluation_index - oob_evaluation_index;

        bool is_easy_dataset = (oob_evaluation_index > 0.85 && overfit < 0.15);
        float baseline_overfit = overfit;

        // Set adaptive satisfaction thresholds
        float satisfied_train_threshold, satisfied_overfit_threshold;
        if (is_easy_dataset) {
            satisfied_train_threshold = 0.98f;
            satisfied_overfit_threshold = 0.01f;
            Serial.println("Easy dataset detected - using conservative thresholds");
        } else {
            satisfied_train_threshold = 0.88f;
            satisfied_overfit_threshold = max(0.08f, baseline_overfit * 0.5f);
            Serial.println("Difficult dataset detected - using aggressive thresholds");
        }

        Serial.printf("Init train: %f\n", train_evaluation_index);
        Serial.printf("Initial OOB: %f\n", oob_evaluation_index);
        Serial.printf("Initial overfit: %f\n", overfit);

        // Check if already satisfied
        if (oob_evaluation_index >= satisfied_train_threshold && overfit <= satisfied_overfit_threshold) {
            Serial.println("Model already satisfies thresholds. Training complete.");
            return;
        }
        
        float best_train_metric = train_evaluation_index;
        float best_oob_metric = oob_evaluation_index;
        float best_overfit = overfit;

        // Store best configuration
        uint8_t best_minSplit = minSplit;
        uint16_t best_maxDepth = maxDepth;

        int patience = 0;
        int patience_limit = early_stop ? (is_easy_dataset ? 5 : 8) : 999;
        uint8_t epochs_since_improvement = 0;
        uint8_t consecutive_no_changes = 0; // Track consecutive epochs with no changes
        
        for ( uint16_t epoch = 0; epoch < epochs; epoch++) {
            Serial.printf("Epoch %d - Trees: %d (fixed), RAM: %d\n", epoch, numTree, ESP.getFreeHeap());
            
            // Save previous state
            uint8_t oldMinSplit = minSplit;
            uint16_t oldMaxDepth = maxDepth;
            
            bool params_changed = false;
            
            // Check satisfaction criteria early
            if (oob_evaluation_index >= satisfied_train_threshold && overfit <= satisfied_overfit_threshold) {
                Serial.printf("Satisfaction criteria met at epoch %d\n", epoch);
                break;
            }
            
            // Early stopping check
            if (!is_easy_dataset && epochs_since_improvement > 8) {
                Serial.println("Early stopping triggered");
                break;
            }
            
            // Aggressive adjustment if stuck too long
            bool force_aggressive = (consecutive_no_changes >= 3);
            if (force_aggressive) {
                Serial.printf("🔥 AGGRESSIVE MODE: No changes for %d epochs, forcing adjustments\n", consecutive_no_changes);
            }
            
            // Enhanced parameter adjustment logic - ONLY minSplit and maxDepth
            if (is_easy_dataset) {
                if (overfit > satisfied_overfit_threshold) {
                    if (minSplit < 10) {
                        minSplit = min(minSplit + 1, 10);
                        params_changed = true;
                    }
                    if (maxDepth > 5) {
                        maxDepth = max(maxDepth - 1, 5);
                        params_changed = true;
                    }
                } else if (oob_evaluation_index < satisfied_train_threshold) {
                    // Force parameter changes if stuck
                    if (force_aggressive) {
                        if (maxDepth < 12) {
                            maxDepth = min(maxDepth + 1, 12);
                            params_changed = true;
                            Serial.println("🔥 Forced maxDepth increase");
                        }
                        if (minSplit > 3) {
                            minSplit = max(minSplit - 1, 3);
                            params_changed = true;
                            Serial.println("🔥 Forced minSplit decrease");
                        }
                    }
                }
            } else {
                if (oob_evaluation_index < satisfied_train_threshold) {
                    if (oob_evaluation_index < 0.85f || force_aggressive) {
                        if (maxDepth < 10) {
                            maxDepth = min(maxDepth + 1, 10);
                            params_changed = true;
                        }
                        if (minSplit > 3) {
                            minSplit = max(minSplit - 1, 3);
                            params_changed = true;
                        }
                    }
                }
                if (overfit > satisfied_overfit_threshold * 1.5f || 
                    (force_aggressive && overfit > satisfied_overfit_threshold)) {
                    if (minSplit < 8) {
                        minSplit = min(minSplit + 1, 8);
                        params_changed = true;
                    }
                }
                
                // Last resort adjustments for difficult datasets
                if (force_aggressive && !params_changed) {
                    if (maxDepth < 12) {
                        maxDepth = min(maxDepth + 2, 12);
                        params_changed = true;
                        Serial.println("🔥 Forced aggressive maxDepth increase");
                    }
                    else if (minSplit > 2) {
                        minSplit = max(minSplit - 2, 2);
                        params_changed = true;
                        Serial.println("🔥 Forced aggressive minSplit decrease");
                    }
                }
            }
            
            // Handle parameter changes
            if (params_changed) {
                Serial.printf("Rebuilding forest due to parameter changes (depth: %d, split: %d)\n", maxDepth, minSplit);
                rebuildForest();
            }
            
            // Track consecutive no-change epochs
            if (!params_changed) {
                consecutive_no_changes++;
                epochs_since_improvement++;
                Serial.printf("No changes made in epoch %d (consecutive: %d)\n", epoch, consecutive_no_changes);
                
                // Break if stuck too long and close to satisfaction
                if (consecutive_no_changes >= 5 && 
                    oob_evaluation_index >= satisfied_train_threshold * 0.95f) {
                    Serial.printf("Breaking due to %d consecutive no-change epochs with decent performance\n", consecutive_no_changes);
                    break;
                }
                continue;
            } else {
                consecutive_no_changes = 0; // Reset counter when changes are made
            }
            
            // Ensure data is loaded before evaluation
            if (!train_data.isLoaded) {
                train_data.loadData(true);
            }
            auto new_metrics = get_training_evaluation_index(trainFlag);
            train_evaluation_index = new_metrics.first;
            oob_evaluation_index = new_metrics.second;
            overfit = train_evaluation_index - oob_evaluation_index;
            
            // Check for improvement
            bool improved = false;
            if (is_easy_dataset) {
                improved = (oob_evaluation_index > best_oob_metric) || 
                          (oob_evaluation_index >= best_oob_metric * 0.99f && overfit < best_overfit);
            } else {
                // More lenient improvement criteria for difficult datasets
                improved = (oob_evaluation_index > best_oob_metric + 0.002f) ||
                          (oob_evaluation_index >= best_oob_metric * 0.998f && overfit < best_overfit * 0.95f);
            }
            
            if (improved) {
                // Keep the improvement
                best_train_metric = train_evaluation_index;
                best_oob_metric = oob_evaluation_index;
                best_overfit = overfit;
                best_minSplit = minSplit;
                best_maxDepth = maxDepth;

                epochs_since_improvement = 0;
                patience = 0;
                
                Serial.printf("Epoch %u - IMPROVED: Train=%.4f, OOB=%.4f, Overfit=%.4f\n", 
                            epoch, train_evaluation_index, oob_evaluation_index, overfit);
            } else {
                // Revert parameter changes
                Serial.printf("Epoch %u - REVERTING: Train=%.4f, OOB=%.4f, Overfit=%.4f\n", 
                            epoch, train_evaluation_index, oob_evaluation_index, overfit);
                
                minSplit = oldMinSplit;
                maxDepth = oldMaxDepth;
                
                Serial.printf("Reverting parameters and rebuilding (depth: %d, split: %d)\n", maxDepth, minSplit);
                rebuildForest();
                
                // Restore best metrics
                train_evaluation_index = best_train_metric;
                oob_evaluation_index = best_oob_metric;
                overfit = best_overfit;
                epochs_since_improvement++;
                patience++;
                
                if (patience >= patience_limit) {
                    Serial.printf("No improvement for %u attempts. Stopping.\n", patience);
                    break;
                }
            }
            
            Serial.printf("End epoch %d - Trees: %d (fixed), RAM: %d\n", epoch, numTree, ESP.getFreeHeap());
        }
        
        // Final report with satisfaction analysis
        Serial.println("------- Training completed -------");
        Serial.printf("Final train metric: %.3f\n", best_train_metric);
        Serial.printf("Final OOB metric: %.3f\n", best_oob_metric);
        Serial.printf("Final overfit: %.3f\n", best_overfit);
        Serial.printf("Final trees: %d (fixed)\n", numTree);
        Serial.printf("Final RAM: %d\n", ESP.getFreeHeap());
        
        bool satisfied = (best_oob_metric >= satisfied_train_threshold &&
                          best_overfit <= satisfied_overfit_threshold);
        
        if (!satisfied) {
            Serial.printf("Satisfaction criteria NOT MET:\n");
            Serial.printf("  OOB: %.3f (target: %.3f) - %s\n", 
                        best_oob_metric, satisfied_train_threshold,
                        best_oob_metric >= satisfied_train_threshold ? "✅" : "❌");
            Serial.printf("  Overfit: %.3f (target: %.3f) - %s\n", 
                        best_overfit, satisfied_overfit_threshold,
                        best_overfit <= satisfied_overfit_threshold ? "✅" : "❌");
            
            // NEW: Provide performance assessment
            float performance_ratio = best_oob_metric / satisfied_train_threshold;
            if (performance_ratio >= 0.95f) {
                Serial.println("🟡 Model is very close to target performance");
            } else if (performance_ratio >= 0.90f) {
                Serial.println("🟠 Model has decent performance but needs improvement");
            } else {
                Serial.println("🔴 Model needs significant improvement");
            }
        } else {
            Serial.printf("Satisfaction criteria MET ✅\n");
        }
    }


    // New combined prediction metrics function
    vector<vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        bool pre_load_data = true;
        bool pre_load_tree = false;
        if(!data.isLoaded){
            data.loadData(true);
            pre_load_data = false;
        }
        loadForest(pre_load_tree);
      
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
        releaseForest(pre_load_tree);
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

};
// Define the static instance pointer outside the class
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

    // const char* filename = "/sample_data_2.csv";  // easy dataset, expected 0.98 accuracy, 0.02 overfit
    // const char* filename = "/categorical_data.csv";
    // const char* filename = "/walker_fall.csv";    // medium dataset, expected 0.89 accuracy, 0.08 overfit
    const char* filename = "/digit_data.csv"; // hard dataset, expected 0.80

    RandomForest forest = RandomForest(filename, 15 , 8, 8);

    forest.MakeForest();
    // forest.Prunning();
    forest.training(6); 

    auto result = forest.predict(forest.test_data);

    // Serial.printf("Accuracy in test set: %.3f\n", result[3]);

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

    // Serial.println("Training finished.");
    // Serial.println("Predicted and actual labels for each sample in TEST set:");
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
    Serial.println(freeHeap);
    Serial.print("Largest Free Block: ");
    Serial.println(largestBlock);
    Serial.print("Fragmentation: ");
    Serial.print(100 - (largestBlock * 100 / freeHeap));
    Serial.println("%");
}