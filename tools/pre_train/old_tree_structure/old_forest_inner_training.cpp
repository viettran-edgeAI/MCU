#include "STL_MCU.h"  
#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
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
};

class Rf_tree {
  public:
    Tree_node* root = nullptr;
    std::string filename;

    Rf_tree() : filename("") {}
    
    Rf_tree(const std::string& fn) : filename(fn) {}

    // Count total number of nodes in the tree
    uint32_t countNodes() const {
        return countNodesRecursive(root);
    }

    // Count leaf nodes in the tree
    uint32_t countLeafNodes() const {
        return countLeafNodesRecursive(root);
    }

    // Get tree depth
    uint16_t getTreeDepth() const {
        return getTreeDepthRecursive(root);
    }

    // Save tree to disk using standard C++ file I/O
    void saveTree(std::string folder_path = "") {
        if (!filename.length() || !root) return;
        std::string full_path = folder_path.length() ? (folder_path + "/" + filename) : filename;
        std::ofstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "❌ Failed to save tree: " << full_path << std::endl;
            return;
        }
        // Write header
        uint32_t magic = 0x54524545;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        // Save tree structure
        saveNodeToFile(file, root);
        file.close();
        // Clear from RAM
        purgeTree();
    }

    uint8_t predictSample(const Rf_sample& sample) {
        Tree_node* current = root;
        while (current && !current->getIsLeaf()) {
            uint8_t featureValue = sample.features[current->featureID];
            if (featureValue <= current->getThreshold()) {
                current = current->children.first; // Go to left child
            } else {
                current = current->children.second; // Go to right child
            }
        }
        return current ? current->getLabel() : 0;
    }
    bool saveNodeToFile(std::ofstream& file, Tree_node* node) {
        file.put(static_cast<char>(node->featureID));
        file.put(static_cast<char>(node->packed_data));
        if (!node->getIsLeaf()) {
            if (!saveNodeToFile(file, node->children.first) ||
                !saveNodeToFile(file, node->children.second)) {
                return false;
            }
        }
        return true;
    }

  public:
    void purgeTree() {
        if (root) {
            clearTreeRecursive(root);
            root = nullptr;
        }
        filename = ""; // Clear filename
    }

  private:
    void clearTreeRecursive(Tree_node* node) {
        if (node) {
            clearTreeRecursive(node->children.first);
            clearTreeRecursive(node->children.second);
            delete node;
        }
    }

    // Recursive helper to count total nodes
    uint32_t countNodesRecursive(Tree_node* node) const {
        if (!node) return 0;
        return 1 + countNodesRecursive(node->children.first) + countNodesRecursive(node->children.second);
    }

    // Recursive helper to count leaf nodes
    uint32_t countLeafNodesRecursive(Tree_node* node) const {
        if (!node) return 0;
        if (node->getIsLeaf()) return 1;
        return countLeafNodesRecursive(node->children.first) + countLeafNodesRecursive(node->children.second);
    }

    // Recursive helper to get tree depth
    uint16_t getTreeDepthRecursive(Tree_node* node) const {
        if (!node) return 0;
        if (node->getIsLeaf()) return 1;
        uint16_t leftDepth = getTreeDepthRecursive(node->children.first);
        uint16_t rightDepth = getTreeDepthRecursive(node->children.second);
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

    // repeat a number of samples to reach a certain number of samples: boostrap sampling
    void boostrapData(uint16_t numSamples, uint16_t maxSamples){
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
            if(cursor < sampleIDs.size() && sampleIDs[cursor] == i){
                cursor++;
            } else {
                newSampleIDs.push_back(i); // Add missing IDs
            }
        }
        if(currentSize >= numSamples) {
            std::cout << "Data already has " << currentSize << " samples, no need to bootstrap.\n";
            return;
        }
        
        allSamples.reserve(numSamples);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, currentSize - 1);
        
        while(allSamples.size() < numSamples) {
            // Randomly select a sample ID from existing samples using PC random
            uint16_t pos = dis(gen);
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

    }
};

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
    bool use_validation = false; // use validation data, default is false

public:
    uint8_t trainFlag = EARLY_STOP;    // flags for training, early stop enabled by default
    static RandomForest* instance_ptr;      // Pointer to the single instance

    RandomForest(){};
    RandomForest(std::string data_path, int numtree, bool use_Gini = true, bool boostrap = true){
        first_scan(data_path);
        a.loadCSVData(data_path, numFeatures);

        unity_threshold  = 1.25f / static_cast<float>(numLabels);
        if(numFeatures == 2) unity_threshold  = 0.4f;

        this->numTree = numtree;
        this->use_Gini = use_Gini;
        this->boostrap = boostrap;
        
        // OOB.reserve(numTree);
        dataList.reserve(numTree);

        splitData(train_ratio);

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
        allFeaturesValue.clear();
    }


    void MakeForest(){
        root.clear();
        root.reserve(numTree);
        
        for(uint8_t i = 0; i < numTree; i++){
            Tree_node* rootNode = buildTree(dataList[i].first, minSplit, maxDepth, use_Gini);
            
            // For PC training, no SPIFFS filename needed
            Rf_tree tree("");
            tree.root = rootNode;
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
        
        for(uint8_t i = 0; i < numTree; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxDepth) maxDepth = depth;
            if(depth < minDepth) minDepth = depth;
            
            std::cout << "Tree " << (int)i << ": " 
                      << nodeCount << " nodes (" 
                      << leafCount << " leaves), depth " 
                      << depth << "\n";
        }
        
        std::cout << "----------------------------------------\n";
        std::cout << "Total trees: " << (int)numTree << "\n";
        std::cout << "Total nodes: " << totalNodes << "\n";
        std::cout << "Total leaf nodes: " << totalLeafNodes << "\n";
        std::cout << "Average nodes per tree: " << (float)totalNodes / numTree << "\n";
        std::cout << "Average leaf nodes per tree: " << (float)totalLeafNodes / numTree << "\n";
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
        if(this->use_validation){
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
        if(this->use_validation) validation_data.allSamples.clear();
        
        train_data.allSamples.reserve(trainSize);
        test_data.allSamples.reserve(testSize);
        if(this->use_validation) validation_data.allSamples.reserve(validationSize);

        // Split samples based on shuffled indices
        for(uint16_t i = 0; i < totalSamples; i++) {
            uint16_t sampleID = allSampleIDs[i];
            const Rf_sample& sample = this->a.allSamples[sampleID];
            
            if(i < trainSize) {
                train_data.allSamples[sampleID] = sample;
            } else if(i < trainSize + testSize) {
                test_data.allSamples[sampleID] = sample;
            } else if(this->use_validation && i < trainSize + testSize + validationSize) {
                validation_data.allSamples[sampleID] = sample;
            }
        }
        
        // Fit the containers to optimize memory usage
        train_data.allSamples.fit();
        test_data.allSamples.fit();
        if(this->use_validation) validation_data.allSamples.fit();
    }

    // ---------------------------------------------------------------------------------
    // create dataset for each tree from train set
    void ClonesData() {
        // Clear previous data
        dataList.clear();
        dataList.reserve(numTree);
        uint16_t numSample = train_data.allSamples.size();  
        uint16_t numSubSample = numSample * 0.632;
        uint16_t oob_size = numSample - numSubSample;

        // Create a b_vector of all sample IDs for efficient random access
        b_vector<uint16_t> allSampleIds;
        allSampleIds.reserve(numSample);
        for (const auto& sample : train_data.allSamples) {
            allSampleIds.push_back(sample.first);
        }

        std::uniform_int_distribution<uint16_t> dis(0, numSample - 1);

        for (uint8_t i = 0; i < numTree; i++) {
            Rf_data sub_data;
            sampleID_set inBagSamples;
            inBagSamples.reserve(numSubSample);

            OOB_set oob_set;
            oob_set.reserve(oob_size);

            // Initialize subset data
            sub_data.allSamples.clear();
            sub_data.allSamples.reserve(numSubSample);
            
            
            // Bootstrap sampling WITH replacement using PC random
            std::random_device rd;
            std::mt19937 gen(rd());
            while(sub_data.allSamples.size() < numSubSample){
                uint16_t idx = dis(gen);
                uint16_t sampleId = allSampleIds[idx];     
                
                inBagSamples.insert(sampleId);
                sub_data.allSamples[sampleId] = train_data.allSamples[sampleId];
            }
            sub_data.allSamples.fit();
            if(boostrap) sub_data.boostrapData(numSample, this->numSamples);     // boostrap sampling
            
            // Create OOB set with samples not used in this tree
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

        this->numFeatures = maxFeatures;
        this->numSamples = numSamples;
        this->numLabels = labelCounts.size();

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
            if (maxImbalanceRatio > 10.0f) {
                trainFlag = Rf_training_flags::RECALL;
                std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to RECALL.\n";
            } else if (maxImbalanceRatio > 3.0f) {
                trainFlag = Rf_training_flags::F1_SCORE;
                std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to F1_SCORE.\n";
            } else if (maxImbalanceRatio > 1.5f) {
                trainFlag = Rf_training_flags::PRECISION;
                std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Setting trainFlag to PRECISION.\n";
            } else {
                trainFlag = Rf_training_flags::ACCURACY;
                std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to ACCURACY.\n";
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
        
        this->lowest_distribution = lowestDistribution / 100.0f; // Store as fraction
        
        // Check if validation should be disabled due to low sample count
        if (lowestDistribution * numSamples * valid_ratio < 10) {
            use_validation = false;
            std::cout << "⚖️ Setting use_validation to false due to low sample count in validation set.\n";
            train_ratio = 0.7f; // Adjust train ratio to compensate
        }

        std::cout << "Feature values: ";
        for (uint8_t val : featureValues) {
            std::cout << (int)val << " ";
            this->allFeaturesValue.push_back(val);
        }
        std::cout << "\n";
        
        // Calculate optimal parameters based on dataset size
        int baseline_minsplit_ratio = 100 * (this->numSamples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint8_t min_minSplit = std::max(3, (int)(this->numSamples / baseline_minsplit_ratio));
        uint8_t max_minSplit = 12;
        int base_maxDepth = std::min((int)log2(this->numSamples), (int)(log2(this->numFeatures) * 1.5f));
        uint8_t max_maxDepth = std::min(8, base_maxDepth);
        uint8_t min_maxDepth = 3;

        this->minSplit = (min_minSplit + max_minSplit) / 2;
        this->maxDepth = (min_maxDepth + max_maxDepth) / 2;

        std::cout << "min minSplit: " << (int)min_minSplit << ", max minSplit: " << (int)max_minSplit << "\n";
        std::cout << "min maxDepth: " << (int)min_maxDepth << ", max maxDepth: " << (int)max_maxDepth << "\n";


        std::cout << "Setting minSplit to " << (int)this->minSplit << " and maxDepth to " << (int)this->maxDepth << " based on dataset size.\n";
        
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
            // Use a flat vector for the contingency table to avoid non-standard 2D VLAs.
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
                // Use vector for safety.
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
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> feature_dis(0, numFeatures - 1);
        while (selectedFeatures.size() < num_selected_features) {
            uint16_t idx = feature_dis(gen);
            selectedFeatures.insert(idx);
        }
        
        // OPTIMIZED: Find the best split (feature and threshold) in one go.
        SplitInfo bestSplit = findBestSplit(a, selectedFeatures, use_Gini);

        // Poor split - create leaf. Gain for the true binary split is smaller than the
        // old multi-way gain, so the threshold must be adjusted.
        float gain_threshold = use_Gini ? this->impurity_threshold/2 : this->impurity_threshold;
        // float gain_threshold = this->impurity_threshold;
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

        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[numLabels] = {0};
        uint16_t oob_fp[numLabels] = {0};
        uint16_t oob_fn[numLabels] = {0};

        uint16_t valid_tp[numLabels] = {0};
        uint16_t valid_fp[numLabels] = {0};
        uint16_t valid_fn[numLabels] = {0};

        uint16_t oob_correct = 0, oob_total = 0,
                 valid_correct = 0, valid_total = 0;

        // OOB evaluation: iterate through all training samples directly
        for(const auto& sample : train_data.allSamples){                
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
            
            for(const uint8_t& treeIdx : activeTrees){
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

        // Validation part: if validation is enabled, evaluate on the validation set
        if(this->use_validation){   
            for(const auto& sample : validation_data.allSamples){
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
            }
        }

        // Calculate the requested metric
        float oob_result = 0.0f;
        float valid_result = 0.0f;
        float combined_oob_result = 0.0f;
        float combined_valid_result = 0.0f;
        uint8_t training_flag = static_cast<uint8_t>(this->trainFlag);
        uint8_t numFlags = 0;
        
        if(oob_total == 0){
            std::cout << "❌ No valid OOB predictions found!\n";
            return make_pair(oob_result, valid_result);
        }
        
        if(training_flag & ACCURACY){
            oob_result = static_cast<float>(oob_correct) / oob_total;
            valid_result = static_cast<float>(valid_correct) / valid_total;
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
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        return make_pair(combined_oob_result / numFlags, 
                        combined_valid_result / numFlags);
    }

    void rebuildForest() {
        // Clear existing trees properly
        for (uint8_t i = 0; i < root.size(); i++) {
            if (root[i].root != nullptr) {
                root[i].purgeTree(); // Use purgeTree instead of clearTree
            }
        }
        for(uint8_t i = 0; i < numTree; i++){
            // Build new tree
            Tree_node* rootNode = buildTree(dataList[i].first, minSplit, maxDepth, use_Gini);
            Rf_tree& tree = root[i];
            
            // Clean up any existing root (safety check)
            if(tree.root != nullptr) {
                tree.purgeTree(); // Ensure complete cleanup
            }
            tree.root = rootNode;           // Assign the new root node
            // Verify tree was built successfully
            if (rootNode == nullptr) {
                std::cout << "❌ Failed to build tree " << (int)i << "\n";
                continue;
            }
        }
        
        // Print forest statistics after rebuilding
        printForestStatistics();
    }

  public:
    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------
    void training(int epochs = 1, float combine_ratio = 0.5, bool early_stop = true){
        // Use aggressive, scikit-learn-like parameters
        this->minSplit = 2; // scikit-learn default
        // Set a high maxDepth to simulate unlimited growth, constrained only by minSplit.
        // A value like 50 is effectively infinite for most datasets.
        this->maxDepth = 12; 

        std::cout << "\n----------- Training with scikit-learn like parameters ----------\n";
        std::cout << "Params: minSplit=" << (int)this->minSplit 
                 << ", maxDepth=" << (int)this->maxDepth 
                 << ", impurity_threshold=" << this->impurity_threshold << "\n";

        // A single build is enough since parameters are fixed.
        rebuildForest(); 
        
        pair<float,float> final_eval = get_training_evaluation_index();
        float final_oob_score = final_eval.first;
        float final_valid_score = final_eval.second;
        float final_combined_score;

        if(this->use_validation) {
            final_combined_score = final_valid_score * combine_ratio + final_oob_score * (1.0f - combine_ratio);
        } else {
            final_combined_score = final_oob_score;
        }
        
        // Training summary
        std::cout << "\n----------- Training completed ----------\n";
        std::cout << "Final scores - OOB: " << final_oob_score 
                 << ", Validation: " << final_valid_score 
                 << ", Combined: " << final_combined_score << "\n";

        if(this->use_validation) {
            float oob_valid_diff = abs(final_oob_score - final_valid_score);
            std::cout << "OOB-Validation difference: " << oob_valid_diff
                     << (oob_valid_diff > 0.1f ? " (high - may indicate overfitting)" : " (acceptable)") << "\n";
        }
    }
    // Save the trained forest to files
    void saveForest(const std::string& folder_path = "model_output") {
        std::cout << "💾 Saving trained forest to " << folder_path << "...\n";
        
        // Create directory if it doesn't exist
        #ifdef _WIN32
            _mkdir(folder_path.c_str());
        #else
            mkdir(folder_path.c_str(), 0755);
        #endif
        
        for(uint8_t i = 0; i < numTree; i++){
            std::string filename = "tree_" + std::to_string(i) + ".bin";
            root[i].filename = filename;
            root[i].saveTree(folder_path);
        }
        
        // Calculate forest statistics for saving
        uint32_t totalNodes = 0;
        uint32_t totalLeafNodes = 0;
        uint16_t maxDepth = 0;
        uint16_t minDepth = UINT16_MAX;
        
        for(uint8_t i = 0; i < numTree; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxDepth) maxDepth = depth;
            if(depth < minDepth) minDepth = depth;
        }
        
        // Save model configuration with forest statistics
        std::string config_path = folder_path + "/model_config.json";
        std::ofstream config_file(config_path);
        if(config_file.is_open()) {
            config_file << "{\n";
            config_file << "  \"numTrees\": " << (int)numTree << ",\n";
            config_file << "  \"numFeatures\": " << (int)numFeatures << ",\n";
            config_file << "  \"numLabels\": " << (int)numLabels << ",\n";
            config_file << "  \"minSplit\": " << (int)minSplit << ",\n";
            config_file << "  \"maxDepth\": " << (int)maxDepth << ",\n";
            config_file << "  \"useGini\": " << (use_Gini ? "true" : "false") << ",\n";
            config_file << "  \"unityThreshold\": " << unity_threshold << ",\n";
            config_file << "  \"forestStatistics\": {\n";
            config_file << "    \"totalNodes\": " << totalNodes << ",\n";
            config_file << "    \"totalLeafNodes\": " << totalLeafNodes << ",\n";
            config_file << "    \"avgNodesPerTree\": " << (float)totalNodes / numTree << ",\n";
            config_file << "    \"avgLeafNodesPerTree\": " << (float)totalLeafNodes / numTree << ",\n";
            config_file << "    \"minDepth\": " << minDepth << ",\n";
            config_file << "    \"maxDepth\": " << maxDepth << ",\n";
            config_file << "    \"avgDepth\": " << (float)(maxDepth + minDepth) / 2.0f << "\n";
            config_file << "  }\n";
            config_file << "}\n";
            config_file.close();
            std::cout << "✅ Model configuration saved to " << config_path << "\n";
        }
        
        std::cout << "✅ Forest saved successfully!\n";
    }
    // combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
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
        for (const auto& kv : data.allSamples) {
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
            
            std::cout << "Label " << (int)label << ": "
                      << "TP=" << tpv << ", FP=" << fpv << ", FN=" << fnv
                      << ", Prec=" << prec << ", Rec=" << rec
                      << ", F1=" << f1 << ", Acc=" << acc << "\n";
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
    std::cout << "Random Forest PC Training\n";
    std::string data_path = "/home/viettran/Arduino/libraries/STL_MCU/tools/data_transfer/data/result/digit_data_nml.csv"; 
    RandomForest forest = RandomForest(data_path, 20, false, true);
    
    // Build initial forest
    forest.MakeForest();
    
    // Print initial forest statistics
    forest.printForestStatistics();
    
    // Train the forest to find optimal parameters
    forest.training(20, 0.5, true); // 20 epochs, combine_ratio 0.5, early_stop enabled
    
    
    std::cout << "Training complete! Model saved to 'trained_model' directory.\n";
    auto result = forest.predict(forest.test_data);

    // Calculate Precision
    std::cout << "Precision in test set:";
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
    std::cout << "Recall in test set:";
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
    std::cout << "F1 Score in test set:";
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
    std::cout << "Overall Accuracy in test set:";
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


    std::cout << "\n📊 FINAL SUMMARY:\n";
    std::cout << "Dataset: " << data_path << "\n";
    std::cout << "Trees: " << (int)forest.numTree << ", Max Depth: " << (int)forest.maxDepth 
              << ", Min Split: " << (int)forest.minSplit << "\n";
    std::cout << "Labels in dataset: " << (int)forest.numLabels << "\n";
    std::cout << "Average Precision: " << avgPrecision << "\n";
    std::cout << "Average Recall: " << avgRecall << "\n";
    std::cout << "Average F1-Score: " << avgF1 << "\n";
    std::cout << "Average Accuracy: " << avgAccuracy << "\n";

    float result_score = forest.predict(forest.test_data, static_cast<Rf_training_flags>(forest.trainFlag));
    std::cout << "result score: " << result_score << "\n";


    // Save the trained model
    forest.saveForest("backup_model_output");
    return 0;
}