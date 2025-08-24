#include "STL_MCU.h"  
#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#include <vector>
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
        // std::cout << "here 5.0\n";
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
};

class Rf_data {
  public:
    ChainedUnorderedMap<uint16_t, Rf_sample> allSamples;    // all sample and it's ID 

    Rf_data(){}

    // Load data from CSV format (used only once for initial dataset conversion)
    void loadCSVData(std::string csvFilename, uint8_t num_features) {
        std::ifstream file(csvFilename);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open CSV file for reading: " << csvFilename << std::endl;
            return;
        }

        // std::cout << "📊 Loading CSV: " << csvFilename << " (expecting " << (int)num_features << " features per sample)" << std::endl;
        
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
            s.features.reserve(num_features);

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
            if (fieldIdx != num_features + 1) {
                std::cout << "❌ Line " << linesProcessed << ": Expected " << (int)(num_features + 1) << " fields, got " << (int)fieldIdx << std::endl;
                invalidSamples++;
                continue;
            }
            if (s.features.size() != num_features) {
                std::cout << "❌ Line " << linesProcessed << ": Expected " << (int)num_features << " features, got " << s.features.size() << std::endl;
                invalidSamples++;
                continue;
            }
            
            s.features.fit();

            allSamples[sampleID] = s;
            sampleID++;
            validSamples++;
        }
        
        // std::cout << "📋 CSV Processing Results:" << std::endl;
        // std::cout << "   Lines processed: " << linesProcessed << std::endl;
        // std::cout << "   Empty lines: " << emptyLines << std::endl;
        // std::cout << "   Valid samples: " << validSamples << std::endl;
        // std::cout << "   Invalid samples: " << invalidSamples << std::endl;
        // std::cout << "   Total samples in memory: " << allSamples.size() << std::endl;
        
        allSamples.fit();
        file.close();
        std::cout << "✅ CSV data loaded successfully." << std::endl;
    }

    // repeat a number of samples to reach a certain number of samples: boostrap sampling
    void boostrapData(uint16_t num_samples, uint16_t maxSamples){
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
        if(currentSize >= num_samples) {
            std::cout << "Data already has " << currentSize << " samples, no need to bootstrap.\n";
            return;
        }
        
        allSamples.reserve(num_samples);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, currentSize - 1);
        
        while(allSamples.size() < num_samples) {
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

struct ModelConfig{
    // model parameters
    uint8_t num_trees = 20;
    uint16_t max_depth = 3;
    uint8_t min_split = 5; 
    uint8_t num_features;  
    uint8_t num_labels;
    uint16_t num_samples;  // number of samples in the base data
    int epochs = 20;    // number of epochs for inner training

    float train_ratio = 0.6f; // ratio of training data to total data, default is 0.6
    float valid_ratio = 0.2f; // ratio of validation data to total data, default is 0.2
    float boostrap_ratio = 0.632f; // ratio of samples taken from train data to create subdata

    b_vector<uint8_t> max_depth_range;      // for training
    b_vector<uint8_t> min_split_range;      // for training 

    Rf_training_flags training_flag;
    std::string data_path;
    
    // model configurations
    float unity_threshold = 0.5f;   // unity_threshold  for classification, effect to precision and recall - default value
    float impurity_threshold = 0.01f; // threshold for impurity, default is 0.01
    float combine_ratio = 0.5f;      // default combine ratio

    bool use_gini = false; // use Gini impurity for training
    bool use_validation = true; // use validation set for training
    bool use_bootstrap = true; // use bootstrap sampling for training

    ModelConfig(){};

    // update model configuration
    void updateConfig(float new_unity_threshold, float new_impurity_threshold, float new_combine_ratio,
                      bool new_use_bootstrap, bool new_use_gini, bool new_use_validation, float new_train_ratio,
                      float new_valid_ratio) {
        unity_threshold = new_unity_threshold;
        impurity_threshold = new_impurity_threshold;
        use_gini = new_use_gini;
        use_validation = new_use_validation;
        use_bootstrap = new_use_bootstrap;
        combine_ratio = new_combine_ratio;
        train_ratio = new_train_ratio;
        valid_ratio = new_valid_ratio;
    }
};

// -------------------------------------------------------------------------------- 
class RandomForest{
public:
    Rf_data a;      // base data / baseFile
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    ModelConfig model_config; // Configuration for the model

private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<pair<Rf_data, OOB_set>> dataList; // b_vector of pairs: Rf_data and OOB set for each tree
    b_vector<uint16_t> train_backup;   // backup of training set sample IDs 
    b_vector<uint16_t> test_backup;    // backup of testing set sample IDs
    b_vector<uint16_t> validation_backup; // backup of validation set sample IDs

public:
    uint8_t trainFlag = EARLY_STOP;    // flags for training, early stop enabled by default
    static RandomForest* instance_ptr;      // Pointer to the single instance

    RandomForest(){};
    RandomForest(ModelConfig config) : model_config(config) {
        a.loadCSVData(model_config.data_path, model_config.num_features);
    
        dataList.reserve(model_config.num_trees);

        splitData(); // Split data into train, test, and validation sets

        ClonesData(); // Clone data for each tree
    }

    // clean and reset all data (except base data)
    void init(ModelConfig config) {
        model_config = config;

        // Clear previous data
        root.clear();
        dataList.clear();
        train_data.allSamples.clear();
        test_data.allSamples.clear();
        validation_data.allSamples.clear();
        train_backup.clear();
        test_backup.clear();
        validation_backup.clear();
        
        if(!a.allSamples.size()) a.loadCSVData(model_config.data_path, model_config.num_features);
        dataList.reserve(model_config.num_trees);

        splitData(); // Split data into train, test, and validation sets
        ClonesData(); // Clone data for each tree

        MakeForest(); 
    }
    
    // Enhanced destructor
    ~RandomForest(){
        // Clear forest safely
        // std::cout << "🧹 Cleaning files... \n";
        for(auto& tree : root){
            tree.purgeTree();
        }
        // Clear data safely
        dataList.clear();
    }


    void MakeForest(){
        root.clear();
        root.reserve(model_config.num_trees);
        uint8_t min_split = model_config.min_split;
        uint8_t max_depth = model_config.max_depth;
        bool use_gini = model_config.use_gini;
        
        for(uint8_t i = 0; i < model_config.num_trees; i++){
            Tree_node* rootNode = buildTree(dataList[i].first, min_split, max_depth, use_gini);
            
            // For PC training, no SPIFFS filename needed
            Rf_tree tree("");
            tree.root = rootNode;
            root.push_back(tree);
        }
    }

        // ----------------------------------------------------------------------------------
    // Split data into training and testing sets
    void splitData() {
        // clear previous data
        train_data.allSamples.clear();
        test_data.allSamples.clear();
        validation_data.allSamples.clear();

        bool use_validation = model_config.use_validation;

        uint16_t totalSamples = this->a.allSamples.size();
        uint16_t trainSize = static_cast<uint16_t>(totalSamples * model_config.train_ratio);
        uint16_t testSize;
        if(use_validation){ 
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
        if(use_validation) validation_data.allSamples.clear();
        
        train_data.allSamples.reserve(trainSize);
        test_data.allSamples.reserve(testSize);
        if(use_validation) validation_data.allSamples.reserve(validationSize);

        // Split samples based on shuffled indices
        for(uint16_t i = 0; i < totalSamples; i++) {
            uint16_t sampleID = allSampleIDs[i];
            const Rf_sample& sample = this->a.allSamples[sampleID];
            
            if(i < trainSize) {
                train_data.allSamples[sampleID] = sample;
            } else if(i < trainSize + testSize) {
                test_data.allSamples[sampleID] = sample;
            } else if(use_validation && i < trainSize + testSize + validationSize) {
                validation_data.allSamples[sampleID] = sample;
            }
        }
        
        // Fit the containers to optimize memory usage
        train_data.allSamples.fit();
        test_data.allSamples.fit();
        if(use_validation) validation_data.allSamples.fit();
    }

    // ---------------------------------------------------------------------------------
    void ClonesData() {
        // Clear previous data
        dataList.clear();
        dataList.reserve(model_config.num_trees);
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

        for (uint8_t i = 0; i < model_config.num_trees; i++) {
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
            if(model_config.use_bootstrap) sub_data.boostrapData(numSample, model_config.num_samples);     // boostrap sampling
            
            // Create OOB set with samples not used in this tree
            for (uint16_t id : allSampleIds) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id);
                }
            }
            dataList.push_back(make_pair(sub_data, oob_set)); // Store pair of subset data and OOB set
        }
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
        vector<uint16_t> baseLabelCounts(model_config.num_labels, 0);
        for (const auto& entry : data.allSamples) {
            // Bounds check to prevent memory corruption
            if (entry.second.label < model_config.num_labels) {
                baseLabelCounts[entry.second.label]++;
            }
        }

        float baseImpurity;
        if (use_Gini) {
            baseImpurity = 1.0f;
            for (uint8_t i = 0; i < model_config.num_labels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * p;
                }
            }
        } else { // Entropy
            baseImpurity = 0.0f;
            for (uint8_t i = 0; i < model_config.num_labels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        // Iterate through the randomly selected features
        for (const auto& featureID : selectedFeatures) {
            // Use a flat vector for the contingency table to avoid non-standard 2D VLAs.
            vector<uint16_t> counts(4 * model_config.num_labels, 0);
            uint32_t value_totals[4] = {0};

            for (const auto& entry : data.allSamples) {
                const Rf_sample& sample = entry.second;
                uint8_t feature_val = sample.features[featureID];
                // Bounds check for both feature value and label
                if (feature_val < 4 && sample.label < model_config.num_labels) {
                    counts[feature_val * model_config.num_labels + sample.label]++;
                    value_totals[feature_val]++;
                }
            }

            // Test all possible binary splits (thresholds 0, 1, 2)
            for (uint8_t threshold = 0; threshold <= 2; threshold++) {
                // Use vector for safety.
                vector<uint16_t> left_counts(model_config.num_labels, 0);
                vector<uint16_t> right_counts(model_config.num_labels, 0);
                uint32_t left_total = 0;
                uint32_t right_total = 0;

                // Aggregate counts for left/right sides from the contingency table
                for (uint8_t val = 0; val < 4; val++) {
                    if (val <= threshold) {
                        for (uint8_t label = 0; label < model_config.num_labels; label++) {
                            left_counts[label] += counts[val * model_config.num_labels + label];
                        }
                        left_total += value_totals[val];
                    } else {
                        for (uint8_t label = 0; label < model_config.num_labels; label++) {
                            right_counts[label] += counts[val * model_config.num_labels + label];
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
                    for (uint8_t i = 0; i < model_config.num_labels; i++) {
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
                    for (uint8_t i = 0; i < model_config.num_labels; i++) {
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
        uint16_t labelCounts[model_config.num_labels] = {0};
        for (const auto& entry : data.allSamples) {
            if (entry.second.label < model_config.num_labels) {
                labelCounts[entry.second.label]++;
            }
        }

        // Pass 2: Find the label with the highest count.
        // This deterministically finds the majority and breaks ties by choosing the lower-indexed label.
        uint16_t maxCount = 0;
        uint8_t majorityLabel = 0; 
        for (uint8_t i = 0; i < model_config.num_labels; i++) {
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

        uint8_t num_selected_features = static_cast<uint8_t>(sqrt(model_config.num_features));
        if (num_selected_features == 0) num_selected_features = 1; // always select at least one feature

        unordered_set<uint16_t> selectedFeatures;
        selectedFeatures.reserve(num_selected_features);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> feature_dis(0, model_config.num_features - 1);
        while (selectedFeatures.size() < num_selected_features) {
            uint16_t idx = feature_dis(gen);
            selectedFeatures.insert(idx);
        }
        
        // OPTIMIZED: Find the best split (feature and threshold) in one go.
        SplitInfo bestSplit = findBestSplit(a, selectedFeatures, use_Gini);

        // Poor split - create leaf. Gain for the true binary split is smaller than the
        // old multi-way gain, so the threshold must be adjusted.
        float gain_threshold = use_Gini ? model_config.impurity_threshold/2 : model_config.impurity_threshold;
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

        // std::cout << "here 4.0" << std::endl;
        
        // Use streaming prediction 
        for(auto& tree : root){
            if(!tree.root) {
                std::cout << "❌ No tree loaded for prediction." << std::endl;
                return 255; // No tree loaded, return invalid class
            }
            uint8_t predict = tree.predictSample(s); // Uses streaming if not loaded
            // std::cout << "here 4.1" << std::endl;
            if(predict < model_config.num_labels){
                predictClass[predict]++;
                totalPredict++;
            }
        }
        // std::cout  << "here 4.2" << std::endl;
        
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
        if(certainty < model_config.unity_threshold) {
            return 255;
        }
        
        return mostPredict;
    }

    // hellper function: evaluate the entire forest, using OOB_score : iterate over all samples in 
    // the train set and evaluate by trees whose OOB set contains its ID
    pair<float,float> get_training_evaluation_index(){

        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[model_config.num_labels] = {0};
        uint16_t oob_fp[model_config.num_labels] = {0};
        uint16_t oob_fn[model_config.num_labels] = {0};

        uint16_t valid_tp[model_config.num_labels] = {0};
        uint16_t valid_fp[model_config.num_labels] = {0};
        uint16_t valid_fn[model_config.num_labels] = {0};

        uint16_t oob_correct = 0, oob_total = 0,
                 valid_correct = 0, valid_total = 0;

        // OOB evaluation: iterate through all training samples directly
        for(const auto& sample : train_data.allSamples){                
            uint16_t sampleId = sample.first;  // Get the sample ID
         
            // Find all trees whose OOB set contains this sampleId
            b_vector<uint8_t, SMALL> activeTrees;
            activeTrees.reserve(model_config.num_trees);
            
            for(uint8_t i = 0; i < model_config.num_trees; i++){
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
                if(predict < model_config.num_labels){
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
            if(certainty < model_config.unity_threshold) {
                continue; // Skip uncertain predictions
            }
            
            // Update confusion matrix
            oob_total++;
            if(oobPredictedLabel == actualLabel){
                oob_correct++;
                oob_tp[actualLabel]++;
            } else {
                oob_fn[actualLabel]++;
                if(oobPredictedLabel < model_config.num_labels){
                    oob_fp[oobPredictedLabel]++;
                }
            }
        }

        // Validation part: if validation is enabled, evaluate on the validation set
        if(model_config.use_validation){   
            for(const auto& sample : validation_data.allSamples){
                uint16_t sampleId = sample.first;  // Get the sample ID
                uint8_t actualLabel = sample.second.label;

                // Predict using all trees
                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                for(uint8_t i = 0; i < model_config.num_trees; i++){
                    uint8_t predict = root[i].predictSample(sample.second);
                    if(predict < model_config.num_labels){
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
                if(certainty < model_config.unity_threshold) {
                    continue; // Skip uncertain predictions
                }

                // Update confusion matrix
                valid_total++;
                if(validPredictedLabel == actualLabel){
                    valid_correct++;
                    valid_tp[actualLabel]++;
                } else {
                    valid_fn[actualLabel]++;
                    if(validPredictedLabel < model_config.num_labels){
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
            for(uint8_t label = 0; label < model_config.num_labels; label++){
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
            for(uint8_t label = 0; label < model_config.num_labels; label++){
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
            for(uint8_t label = 0; label < model_config.num_labels; label++){
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
        for(uint8_t i = 0; i < model_config.num_trees; i++){
            // Build new tree
            Tree_node* rootNode = buildTree(dataList[i].first, model_config.min_split, model_config.max_depth, model_config.use_gini);
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
    }

    public:
    // -----------------------------------------------------------------------------------
    // -----------------------------------------------------------------------------------
    // combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        // Counters for each label
        unordered_map<uint8_t, uint32_t> tp, fp, fn, totalPred, correctPred;
        
        // Initialize counters for all actual labels
        for (uint8_t label=0; label < model_config.num_labels; label++) {
            tp[label] = 0;
            fp[label] = 0; 
            fn[label] = 0;
            totalPred[label] = 0;
            correctPred[label] = 0;
        }
        // std::cout << "here 3.0\n";
        
        // Single pass over samples
        for (const auto& kv : data.allSamples) {
            uint8_t actual = kv.second.label;
            // std::cout << "here 3.1\n";
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(kv.second));
            
            totalPred[actual]++;
            
            if (pred == actual) {
                tp[actual]++;
                correctPred[actual]++;
            } else {
                if (pred < model_config.num_labels && pred >=0) {
                    fp[pred]++;
                }
                fn[actual]++;
            }
        }
        // std::cout << "here 3\n";
        
        // Build metric vectors using ONLY actual labels
        b_vector<pair<uint8_t, float>> precisions, recalls, f1s, accuracies;
        
        for (uint8_t label = 0; label < model_config.num_labels; label++) {
            uint32_t tpv = tp[label], fpv = fp[label], fnv = fn[label];
            
            float prec = (tpv + fpv == 0) ? 0.0f : float(tpv) / (tpv + fpv);
            float rec  = (tpv + fnv == 0) ? 0.0f : float(tpv) / (tpv + fnv);
            float f1   = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
            float acc  = (totalPred[label] == 0) ? 0.0f : float(correctPred[label]) / totalPred[label];
            
            precisions.push_back(make_pair(label, prec));
            recalls.push_back(make_pair(label, rec));
            f1s.push_back(make_pair(label, f1));
            accuracies.push_back(make_pair(label, acc));
            
            // std::cout << "Label " << (int)label << ": "
            //           << "TP=" << tpv << ", FP=" << fpv << ", FN=" << fnv
            //           << ", Prec=" << prec << ", Rec=" << rec
            //           << ", F1=" << f1 << ", Acc=" << acc << "\n";
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

        // Save the trained forest to files
    void saveForest(const std::string& folder_path = "trained_model") {
        // std::cout << "💾 Saving trained forest to " << folder_path << "...\n";
        
        // Create directory if it doesn't exist
        #ifdef _WIN32
            _mkdir(folder_path.c_str());
        #else
            mkdir(folder_path.c_str(), 0755);
        #endif
        
        for(uint8_t i = 0; i < model_config.num_trees; i++){
            std::string filename = "tree_" + std::to_string(i) + ".bin";
            root[i].filename = filename;
            root[i].saveTree(folder_path);
        }
        std::cout << "✅ Forest saved successfully!\n";
    }
};

    void training(RandomForest &forest) {
        // std::cout << "----------- Training started ----------\n";
        
        int epochs = forest.model_config.epochs;
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
        uint8_t min_minSplit = forest.model_config.min_split_range.front();
        uint8_t max_minSplit = forest.model_config.min_split_range.back();
        uint16_t min_maxDepth = forest.model_config.max_depth_range.front();
        uint16_t max_maxDepth = forest.model_config.max_depth_range.back();
        
        // Best state storage
        uint8_t best_minSplit = forest.model_config.min_split;
        uint16_t best_maxDepth = forest.model_config.max_depth;
        
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
        uint8_t prev_minSplit = forest.model_config.min_split;
        uint16_t prev_maxDepth = forest.model_config.max_depth;

        // Rebuild forest first to ensure it uses the updated configuration
        forest.rebuildForest();

        // Get initial evaluation with double-check for stability
        pair<float,float> eval1 = forest.get_training_evaluation_index();
        forest.rebuildForest(); // Rebuild to account for randomness
        pair<float,float> eval2 = forest.get_training_evaluation_index();
        
        // Use average of two evaluations for more stable baseline
        current_oob_score = (eval1.first + eval2.first) / 2.0f;
        current_valid_score = (eval1.second + eval2.second) / 2.0f;
        
        // Dynamic combine ratio based on dataset analysis
        // if(!forest.model_config.use_validation){
        //     current_combined_score = current_oob_score;
        // } else {
        //     current_combined_score = current_valid_score * forest.model_config.combine_ratio + current_oob_score * (1.0f - forest.model_config.combine_ratio);
        // }
        current_combined_score = current_valid_score * forest.model_config.combine_ratio + current_oob_score * (1.0f - forest.model_config.combine_ratio);

        float score_variance = abs(eval1.first - eval2.first) + abs(eval1.second - eval2.second);
        // Determine dataset difficulty using both scores
        if(forest.model_config.use_validation) {
            is_difficult_dataset = (current_oob_score < difficult_threshold) || 
                                (current_valid_score < difficult_threshold) ||
                                (score_variance > 0.1f); // High variance indicates difficulty
        } else {
            is_difficult_dataset = (current_oob_score < difficult_threshold) || (score_variance > 0.1f);
        }
        
        // Initialize best scores
        best_oob_score = current_oob_score;
        best_valid_score = current_valid_score;
        best_combined_score = current_combined_score;
        
        for(int epoch = 1; epoch <= epochs; epoch++){   
            bool should_change_parameter = (evaluation_phase == 0) && !parameters_optimal;
            
            // Parameter adjustment phase
            if(should_change_parameter){
                prev_minSplit = forest.model_config.min_split;
                prev_maxDepth = forest.model_config.max_depth;
                
                if(adjusting_minSplit && !minSplit_reached_optimal){
                    
                    if(is_difficult_dataset){
                        if(forest.model_config.min_split < max_minSplit){
                            forest.model_config.min_split++;
                            parameter_changed_this_cycle = true;
                        } else {
                            minSplit_reached_optimal = true;
                        }
                    } else {
                        if(forest.model_config.min_split > min_minSplit){
                            forest.model_config.min_split--;
                            parameter_changed_this_cycle = true;
                        } else {
                            minSplit_reached_optimal = true;
                        }
                    }
                } else if(!maxDepth_reached_optimal){
                    adjusting_minSplit = false;
                    if(is_difficult_dataset){
                        if(forest.model_config.max_depth > min_maxDepth){
                            forest.model_config.max_depth--;
                            parameter_changed_this_cycle = true;
                        } else {
                            maxDepth_reached_optimal = true;
                        }
                    } else {
                        if(forest.model_config.max_depth < max_maxDepth){
                            forest.model_config.max_depth++;
                            parameter_changed_this_cycle = true;
                            maxDepth_reached_optimal = true;
                        }
                    }
                } else {
                    parameters_optimal = true;
                }
                
                if(parameter_changed_this_cycle){
                    evaluation_phase = 1; // Start double evaluation
                }
            } 
            // Build and evaluate
            forest.rebuildForest();
            
            pair<float,float> evaluation_result = forest.get_training_evaluation_index();
            float eval_oob = evaluation_result.first;
            float eval_valid = evaluation_result.second;
            float eval_combined;

            // if(forest.model_config.use_validation){
            //   eval_combined = eval_valid * forest.model_config.combine_ratio + eval_oob * (1.0f - forest.model_config.combine_ratio);
            // }else{
            //   eval_combined =  eval_oob;
            // }       
            eval_combined = eval_valid * forest.model_config.combine_ratio + eval_oob * (1.0f - forest.model_config.combine_ratio);
            
            // Handle evaluation phases
            if(evaluation_phase == 1){
                // First evaluation after parameter change
                first_eval_score = eval_combined;
                evaluation_phase = 2;
                continue; // Go to next epoch for second evaluation
                
            } else if(evaluation_phase == 2){
                // Second evaluation after parameter change
                second_eval_score = eval_combined;
                evaluation_phase = 0; // Reset for next cycle
                
                // Use average of two evaluations for decision
                float avg_eval_score = (first_eval_score + second_eval_score) / 2.0f;
                float eval_variance = abs(first_eval_score - second_eval_score);
                
                // High variance indicates unreliable results - be more conservative
                float effective_improvement = avg_eval_score - best_combined_score;
                if(eval_variance > 0.05f) {
                    effective_improvement -= (eval_variance * 0.5f); // Penalty for high variance
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
                    best_minSplit = forest.model_config.min_split;
                    best_maxDepth = forest.model_config.max_depth;
                    no_improvement_count = 0;
                } else {
                    // Parameter change was not beneficial - revert
                    forest.model_config.min_split = prev_minSplit;
                    forest.model_config.max_depth = prev_maxDepth;
                    
                    // Mark parameter as reached optimal
                    if(adjusting_minSplit){
                        minSplit_reached_optimal = true;
                        adjusting_minSplit = false;
                    } else {
                        maxDepth_reached_optimal = true;
                        parameters_optimal = true;
                    }
                    
                    // Restore best state - just rebuild with best parameters for PC
                    forest.model_config.min_split = best_minSplit;
                    forest.model_config.max_depth = best_maxDepth;
                    forest.rebuildForest();
                    current_combined_score = best_combined_score;
                    current_oob_score = best_oob_score;
                    current_valid_score = best_valid_score;

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
                    best_minSplit = forest.model_config.min_split;
                    best_maxDepth = forest.model_config.max_depth;
                    no_improvement_count = 0;
                } else {
                    if(parameters_optimal){
                        no_improvement_count++;
                    }
                }
            }
            
            // Early stopping (only in final optimization phase)
            if(parameters_optimal && no_improvement_count >= early_stop_patience){
                break;
            }
            // Progress report
            const char* phase_str = "final optimization";
            if(!parameters_optimal){
                if(evaluation_phase > 0){
                    phase_str = "evaluating change";
                } else if(adjusting_minSplit){
                    phase_str = "optimizing min_split";
                } else {
                    phase_str = "optimizing max_depth";
                }
            }
        }
        
        // Final restoration if needed
        if(current_combined_score < best_combined_score - min_improvement){
            // std::cout << "📥 Final restoration to best state...\n";
            forest.model_config.min_split = best_minSplit;
            forest.model_config.max_depth = best_maxDepth;
            forest.rebuildForest(); // Rebuild with best parameters
            
            pair<float,float> final_eval = forest.get_training_evaluation_index();
            current_oob_score = final_eval.first;
            current_valid_score = final_eval.second;
            // if(forest.model_config.use_validation) current_combined_score = current_valid_score * forest.model_config.combine_ratio + current_oob_score * (1.0f - forest.model_config.combine_ratio);
            // else current_combined_score = current_oob_score;
            current_combined_score = current_valid_score * forest.model_config.combine_ratio + current_oob_score * (1.0f - forest.model_config.combine_ratio);
        }
    }
    

class main_controler{
public:
    constexpr static int num_forests = 3;
    ModelConfig model_config;
    // set of 3 forests
    b_vector<RandomForest> forest_set{num_forests};

    // training configuration ranges
    b_vector<float> unity_threshold_range;
    b_vector<float> impurity_threshold_range = MAKE_FLOAT_LIST(0.05f, 0.1f, 0.2f); 
    b_vector<float> combine_ratio_range;
    b_vector<bool> bootstrap_range = MAKE_BOOL_LIST(true, false);
    b_vector<bool> use_gini_range = MAKE_BOOL_LIST(true, false);
    b_vector<bool> use_validation_range = MAKE_BOOL_LIST(false, true);

    // final configuration
    float best_unity_threshold;
    float best_impurity_threshold;
    float best_combine_ratio;  
    bool  final_use_bootstrap;
    bool  final_use_gini;
    bool  final_use_validation;
    uint8_t best_min_split;
    uint8_t best_max_depth;

    b_vector<b_vector<pair<uint8_t, float>>> result_metrics; // stores the result metrics for each forest
    std::string timestamp;

    main_controler(std::string data_path, bool header = false){
        std::ifstream file(data_path);
        if (!file.is_open()) {
            std::cout << "❌ Failed to open file: " << data_path << "\n";
            return;
        }
        unordered_map<uint8_t, uint16_t> labelCounts;
        unordered_set<uint8_t> featureValues;

        uint16_t num_samples = 0;
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
                num_samples++;
                if (num_samples >= 10000) break;
            }
        }

        this->model_config.num_features = maxFeatures;
        this->model_config.num_samples = num_samples;
        this->model_config.num_labels = labelCounts.size();

        // Analyze label distribution and set appropriate training flags
        if (labelCounts.size() > 0) {
            uint16_t minorityCount = num_samples;
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
                this->model_config.training_flag = Rf_training_flags::RECALL;
                std::cout << "📉 Imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to RECALL.\n";
            } else if (maxImbalanceRatio > 3.0f) {
                this->model_config.training_flag = Rf_training_flags::F1_SCORE;
                std::cout << "⚖️ Moderately imbalanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to F1_SCORE.\n";
            } else if (maxImbalanceRatio > 1.5f) {
                this->model_config.training_flag = Rf_training_flags::PRECISION;
                std::cout << "🟨 Slight imbalance (ratio: " << maxImbalanceRatio << "). Setting trainFlag to PRECISION.\n";
            } else {
                this->model_config.training_flag = Rf_training_flags::ACCURACY;
                std::cout << "✅ Balanced dataset (ratio: " << maxImbalanceRatio << "). Setting trainFlag to ACCURACY.\n";
            }
        }

        // Dataset summary
        std::cout << "📊 Dataset Summary:\n";
        std::cout << "  Total samples: " << num_samples << "\n";
        std::cout << "  Total features: " << maxFeatures << "\n";
        std::cout << "  Unique labels: " << labelCounts.size() << "\n";

        std::cout << "  Label distribution:\n";
        float lowestDistribution = 100.0f;
        for (auto& label : labelCounts) {
            float percent = (float)label.second / num_samples * 100.0f;
            if (percent < lowestDistribution) {
                lowestDistribution = percent;
            }
            std::cout << "    Label " << (int)label.first << ": " << label.second << " samples (" << percent << "%)\n";
        }
        // this->model_config.lowest_distribution = lowestDistribution / 100.0f; // Store as fraction
        
        std::cout << "\n";
        file.close();

        set_config_ranges(num_samples, maxFeatures, labelCounts.size(), lowestDistribution);
        this->model_config.data_path = data_path;

        // init forests
        for(int i=0; i< num_forests; i++){
            forest_set[i].init(model_config);
            forest_set[i].MakeForest();
        }

        // printout initial configuration
        std::cout << "Initial configuration:\n";
        std::cout << "  Unity Threshold: " << model_config.unity_threshold << "\n";
        std::cout << "  Impurity Threshold: " << model_config.impurity_threshold << "\n";
        std::cout << "  Combine Ratio: " << model_config.combine_ratio << "\n";
        std::cout << "  Train Ratio: " << model_config.train_ratio << "\n";
        std::cout << "  Valid Ratio: " << model_config.valid_ratio << "\n";
        std::cout << "  Use Bootstrap: " << (model_config.use_bootstrap ? "Yes" : "No") << "\n";
        std::cout << "  Use Gini: " << (model_config.use_gini ? "Yes" : "No") << "\n";
        std::cout << "  Use Validation: " << (model_config.use_validation ? "Yes" : "No") << "\n";
        std::cout << "  Min Split: " << (int)model_config.min_split << "\n";
        std::cout << "  Max Depth: " << (int)model_config.max_depth << "\n";
        std::cout << "  Training Flag: " << (int)model_config.training_flag << "\n";

        float initial_score = 0;
        for(auto& forest : forest_set) {
            initial_score += forest.predict(forest.test_data, forest.model_config.training_flag);
        }
        initial_score /= num_forests;
        std::cout << "Initial score : " << initial_score << "\n";
    }

    void set_config_ranges(uint16_t num_samples, uint8_t num_features, uint8_t num_labels, float lowest_distribution){
        float baseline_unity = 1.25f / static_cast<float>(num_labels);
        uint16_t baseline_ratio = 100 * (num_samples / 500 + 1);
        if (baseline_ratio > 500) baseline_ratio = 500;
        uint8_t min_minSplit = std::max(3, (int)(num_samples / baseline_ratio));
        uint8_t max_minSplit = std::max(12, (int)(num_samples / 50));
        uint8_t base_depth = std::min(static_cast<uint8_t>(log2(num_samples)), 
                                static_cast<uint8_t>(log2(num_features) * 1.5f));
        uint8_t max_maxDepth = std::min(6, (int)base_depth);
        uint8_t min_maxDepth = 3;

        float size_factor = std::min(1.0f, num_samples / 5000.0f);
        float label_balance = lowest_distribution * num_labels / 100.0f;
        float baseline_combine = 0.4f + (0.4f * size_factor) + (0.2f * label_balance);

        // Set initial model configuration to baseline values
        model_config.unity_threshold = baseline_unity;
        model_config.combine_ratio = baseline_combine;
        model_config.min_split = (min_minSplit + max_minSplit) / 2;
        model_config.max_depth = (min_maxDepth + max_maxDepth) / 2;

        for(float i = baseline_unity - 0.2f; i < baseline_unity + 0.2f; i += 0.1f) {
            if(i < 0.1f || i > 0.95f) continue; // Avoid too low unity thresholds
            unity_threshold_range.push_back(i);
        }
        for(uint8_t i = min_minSplit; i <= max_minSplit; i++) {
            model_config.min_split_range.push_back(i);
        }
        for(uint8_t i = min_maxDepth; i <= max_maxDepth; i++) {
            model_config.max_depth_range.push_back(i);
        }
        for(float i = baseline_combine - 0.2f; i < baseline_combine + 0.2f; i += 0.1f) {
            if(i < 0.1f || i > 0.95f) continue; // Avoid too low combine ratios
            combine_ratio_range.push_back(i);
        }

    }

    // global training to find best configurations by using grid search
    void global_training(){
        std::cout << "🌍 Global training started...\n" ;
        bool old_use_bootstrap = model_config.use_bootstrap;
        bool old_use_validation = model_config.use_validation;


        int total_combinations = unity_threshold_range.size() * impurity_threshold_range.size() *
                                  combine_ratio_range.size() * bootstrap_range.size() *
                                  use_gini_range.size() * use_validation_range.size();
        std::cout << "Total parameter combinations: " << total_combinations << "\n";
        
        int combination_count = 0;
        float best_score = 0.0f;

        int low_score_count = 0;

        // printout all ranges size
        std::cout << "Unity Threshold Range: " ;
        for(float ut : unity_threshold_range) {
            std::cout << ut << " ";
        }
        std::cout << "\nImpurity Threshold Range: " ;
        for(float it : impurity_threshold_range) {
            std::cout << it << " ";
        }
        std::cout << "\nCombine Ratio Range: " ;
        for(float cr : combine_ratio_range) {
            std::cout << cr << " ";
        }
        std::cout << "\nBootstrap Range: " ;
        for(bool b : bootstrap_range) {
            std::cout << (b ? "Yes" : "No") << " ";
        }
        std::cout << "\nUse Gini Range: " ;
        for(bool g : use_gini_range) {
            std::cout << (g ? "Yes" : "No") << " ";
        }
        std::cout << "\nUse Validation Range: " ;
        for(bool v : use_validation_range) {
            std::cout << (v ? "Yes" : "No") << " ";
        }
        std::cout << "\n\n";


        for(float unity_threshold : unity_threshold_range) {
            for(float impurity_threshold : impurity_threshold_range) {
                for(float combine_ratio : combine_ratio_range) {
                    for(bool use_bootstrap : bootstrap_range) {
                        for(bool use_gini : use_gini_range) {
                            for(bool use_validation : use_validation_range) {
                                combination_count++;
                                //  if(combination_count > 100) break;
                                std::cout << "combination " << combination_count << "/" << total_combinations << std::endl;
                                // Set model configuration

                                float train_ratio = 0.65f; // Default training ratio
                                float valid_ratio = 0.15f; // Default validation ratio

                                if (!use_validation) {
                                    train_ratio = 0.75f; // More training data
                                    valid_ratio = 0.0f; // Less validation data
                                    combine_ratio = 0.0f; // Use OOB only - update model_config too
                                }
                                model_config.updateConfig(
                                    unity_threshold,
                                    impurity_threshold,
                                    combine_ratio,  
                                    use_bootstrap,
                                    use_gini,
                                    use_validation,
                                    train_ratio,
                                    valid_ratio
                                );

                                for(int i=0; i< num_forests; i++){
                                    forest_set[i].init(model_config);
                                    training(forest_set[i]);
                                }

                                // std::cout << "here 2" << std::endl;

                                // get scores from each forest
                                float total_score = 0;
                                pair<uint8_t, float> best_s_score{0,0.0f};
                                for(uint8_t i=0; i< num_forests; i++){
                                    float c_score = forest_set[i].predict(forest_set[i].test_data, forest_set[i].model_config.training_flag);
                                    total_score += c_score;
                                    if(c_score > best_s_score.second) {
                                        best_s_score.first = i;
                                        best_s_score.second = c_score;
                                    }
                                    std::cout << c_score << " - ";
                                }
                                total_score /= num_forests;
                                if(total_score < 0.4f) {
                                    low_score_count++;
                                    // printout cofiguration (in one line)
                                    std::cout << "\nConfiguration with low score (" << total_score << "): "
                                              << "Unity: " << unity_threshold 
                                              << ", Impurity: " << impurity_threshold 
                                              << ", Combine: " << combine_ratio 
                                              << ", Bootstrap: " << (use_bootstrap ? "Yes" : "No") 
                                              << ", Gini: " << (use_gini ? "Yes" : "No") 
                                              << ", Validation: " << (use_validation ? "Yes" : "No") 
                                              << ", Min Split: " << (int)model_config.min_split 
                                              << ", Max Depth: " << (int)model_config.max_depth << "\n";
                                }
                                std::cout << "\nScore for this combination: " << total_score << "\n";
                                if(total_score > best_score) {
                                    best_score = total_score;
                                    best_unity_threshold = unity_threshold;
                                    best_impurity_threshold = impurity_threshold;
                                    best_combine_ratio = model_config.combine_ratio;  // Use actual value used in training
                                    final_use_bootstrap = use_bootstrap;
                                    final_use_gini = use_gini;
                                    final_use_validation = use_validation;
                                    best_min_split = model_config.min_split;
                                    best_max_depth = model_config.max_depth;

                                    result_metrics = forest_set[best_s_score.first].predict(forest_set[best_s_score.first].test_data);
                                    forest_set[best_s_score.first].saveForest("trained_model/best_forest");

                                    std::cout << "🏆 New best score: " << best_score 
                                              << " with params - Unity: " << best_unity_threshold 
                                              << ", Impurity: " << best_impurity_threshold 
                                              << ", Combine: " << best_combine_ratio 
                                              << ", Bootstrap: " << (final_use_bootstrap ? "Yes" : "No") 
                                              << ", Gini: " << (final_use_gini ? "Yes" : "No") 
                                              << ", Validation: " << (final_use_validation ? "Yes" : "No") 
                                              << ", Min Split: " << (int)best_min_split 
                                              << ", Max Depth: " << (int)best_max_depth << "\n";
                                }
                            }
                        }
                    }
                }
            }
        }
        std::cout << "low_score_count: " << low_score_count << std::endl;

        // save best model configurationin json file
        std::cout << "🌟 Global training completed. Best score: " << best_score << "\n";
        std::cout << "Best configuration:\n";
        std::cout << "  Unity Threshold: " << best_unity_threshold << "\n";
        std::cout << "  Impurity Threshold: " << best_impurity_threshold << "\n";
        std::cout << "  Combine Ratio: " << best_combine_ratio << "\n";
        std::cout << "  Use Bootstrap: " << (final_use_bootstrap ? "Yes" : "No") << "\n";
        std::cout << "  Use Gini: " << (final_use_gini ? "Yes" : "No") << "\n";
        std::cout << "  Use Validation: " << (final_use_validation ? "Yes" : "No") << "\n";
        std::cout << "  Best Min Split: " << (int)best_min_split << "\n";
        std::cout << "  Best Max Depth: " << (int)best_max_depth << "\n";   
        std::cout << "Results saved to 'trained_model/best_forest'.\n";
        // Save the best model configuration to a JSON file
        std::ofstream configFile("trained_model/best_config.json");
        if (configFile.is_open()) {
            configFile << "{\n";
            configFile << "  \"unity_threshold\": " << best_unity_threshold << ",\n";
            configFile << "  \"impurity_threshold\": " << best_impurity_threshold << ",\n";
            configFile << "  \"combine_ratio\": " << best_combine_ratio << ",\n";
            configFile << "  \"use_bootstrap\": " << (final_use_bootstrap ? "true" : "false") << ",\n";
            configFile << "  \"use_gini\": " << (final_use_gini ? "true" : "false") << ",\n";
            configFile << "  \"use_validation\": " << (final_use_validation ? "true" : "false") << ",\n";
            configFile << "  \"best_min_split\": " << (int)best_min_split << ",\n"; 
            configFile << "  \"best_max_depth\": " << (int)best_max_depth << ",\n";

            // timestamp
            std::time_t now = std::time(nullptr);
            std::tm* localTime = std::localtime(&now);
            char buffer[80];
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);

            configFile << "  \"num_trees\": " << (int)model_config.num_trees << ",\n";
            configFile << "  \"training_flag\": " << (int)model_config.training_flag << ",\n";
            configFile << "  \"result_scores\": " << best_score << ",\n";
            configFile << "  \"timestamp\": \"" << buffer << "\",\n";
            configFile << "  \"author\": \"Viettran\",\n";
            configFile << "  \"gmail\": \"tranvaviet@gmail.com\"\n";
            configFile << "}\n";
            configFile.close();
            std::cout << "Best configuration saved to 'trained_model/best_config.json'.\n";
        } else {
            std::cout << "❌ Failed to save best configuration to 'trained_model/best_config.json'.\n";
        }

        //printout the result metrics
        std::cout << "----------------- FINAL_RESULT ----------------\n";
        std::cout << "Training flags :" << (model_config.training_flag == Rf_training_flags::RECALL ? "RECALL" : 
                                                model_config.training_flag == Rf_training_flags::F1_SCORE ? "F1_SCORE" : 
                                                model_config.training_flag == Rf_training_flags::PRECISION ? "PRECISION" : 
                                                "ACCURACY") << "\n";
        std::cout << "📊 Result Metrics:\n";
        std::cout << "Precision:\n";
        b_vector<pair<uint8_t, float>> precision = result_metrics[0];
        for (const auto& p : precision) {
            std::cout << "  Label: " << (int)p.first << " - Precision: " << p.second << "\n";
        }
        std::cout << "Recall:\n";
        b_vector<pair<uint8_t, float>> recall = result_metrics[1];  
        for (const auto& r : recall) {
            std::cout << "  Label: " << (int)r.first << " - Recall: " << r.second << "\n";
        }
        std::cout << "F1 Score:\n";
        b_vector<pair<uint8_t, float>> f1_scores = result_metrics[2];
        for (const auto& f1 : f1_scores) {
            std::cout << "  Label: " << (int)f1.first << " - F1 Score: " << f1.second << "\n";
        }
        std::cout << "Overall Accuracy:\n";
        b_vector<pair<uint8_t, float>> accuracies = result_metrics[3];
        for (const auto& acc : accuracies) {
            std::cout << "  Label: " << (int)acc.first << " - Accuracy: " << acc.second << "\n";
        }   
    }
};

int main() {
    std::cout << "Random Forest PC Training\n";
    std::string data_path = "/home/viettran/Arduino/libraries/STL_MCU/tools/data_transfer/data/result/digit_data_nml.csv"; 
    auto start = std::chrono::high_resolution_clock::now();
    main_controler controller(data_path);
    controller.global_training();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end - start;
    std::cout << "Total training time: " << duration.count() << " seconds\n";
    return 0;
}