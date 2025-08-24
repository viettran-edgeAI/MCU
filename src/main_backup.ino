#include "Rf_components.h"

using namespace mcu;

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
    Rf_data base_data;
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    Rf_base base;
    Rf_config config;
    Rf_categorizer categorizer; 
    Rf_memory_logger memory_tracker;
    Rf_node_predictor  node_predictor; // Node predictor number of nodes required for a tree based on min_split and max_depth

private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<pair<Rf_data, OOB_set>> dataList; // b_vector of pairs: Rf_data and OOB set for each tree
    b_vector<uint16_t> train_backup;   // backup of training set sample IDs 
    b_vector<uint16_t> test_backup;    // backup of testing set sample IDs
    b_vector<uint16_t> validation_backup; // backup of validation set sample IDs
    b_vector<NodeToBuild> queue_nodes; // Queue for breadth-first processing

    bool optimal_mode = false;  


public:
    static RandomForest* instance_ptr;      // Pointer to the single instance

    RandomForest(){};
    RandomForest(const char* baseFile){
        // initial components
        memory_tracker.init();
        base.init(baseFile); // Initialize base with the provided base file
        config.loadConfig();
        node_predictor.loadPredictor(); 

        // Extract data_params file from baseFile, load forest parameters
        String dpFile = base.get_dpFile();
        first_scan(dpFile.c_str());
        config.num_trees = 5;


        String ctgFile = base.get_ctgFile();
        categorizer.init(ctgFile); // Initialize categorizer with the provided file
        categorizer.loadCategorizer();

        
        // Set up a pointer connection between forest - data for restore callback mechanism
        instance_ptr = this; // Set the static instance pointer
        Rf_data::restore_data_callback = &RandomForest::static_restore_data;

        // load base data 
        base_data.flag = Rf_data_flags::BASE_DATA;
        if(base.baseFile_is_csv()) 
            base_data.loadCSVData(baseFile, config.num_features);   // load and convert to bin format
        else 
            base_data.loadData(true,baseFile);   // loading base data and copy into new file, keep the original file intact   
        base_data.releaseData(false); 
        
        // resource separation
        dataList.reserve(config.num_trees);
        splitData();
        ClonesData();
    }
    
    // Enhanced destructor
    ~RandomForest(){
        Serial.println("🧹 Cleaning files... ");

        // clear all trees
        for(auto& tree : root){
            tree.purgeTree();       // completely remove tree - for development stage 
            // tree.releaseTree(); // save tree to SPIFFS - for production stage
        }
          
        // clear all Rf_data
        train_data.purgeData();
        test_data.purgeData();
        base_data.purgeData();
        if(config.use_validation) validation_data.purgeData();
        
        // clear sub-data
        for(auto& data : dataList){
            data.first.purgeData();
        }

        node_predictor.re_train()
    }

    void MakeForest(){
        // Clear any existing forest first
        clearForest();
        Serial.println("START MAKING FOREST...");

        // pre_allocate nessessary resources
        root.reserve(config.num_trees);
        queue_nodes.clear();
        queue_nodes.fit();
        uint16_t estimatedNodes = node_predictor.estimate(config.min_split, config.max_depth) * 100 / node_predictor.accuracy;
        queue_nodes.reserve(min(120, estimatedNodes * node_predictor.peak_percent / 100)); // Conservative estimate
        node_data n_data(config.min_split, config.max_depth,0);

        Serial.print("building sub_tree: ");
        for(uint8_t i = 0; i < config.num_trees; i++){
            dataList[i].first.loadData();
            Serial.printf("%d, ", i);
            Rf_tree tree(i);
            tree.nodes.reserve(estimatedNodes); // Reserve memory for nodes based on prediction
            queue_nodes.clear(); // Clear queue for each tree

            buildTree(tree, dataList[i].first);

            n_data.total_nodes += tree.countNodes();
            
            tree.isLoaded = true; 
            tree.releaseTree(); // Save tree to SPIFFS
            root.push_back(tree);
            
            // Release sub-data after tree creation
            dataList[i].first.releaseData(true);
        }
        n_data.total_nodes /= config.num_trees; // Average nodes per tree
        node_predictor.buffer.push_back(n_data); // Add node data to predictor buffer

        Serial.printf("RAM after forest creation: %d\n", ESP.getFreeHeap());
    }
  private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets
    void splitData() {
        Serial.println("<-- split data -->");
    
        uint16_t totalSamples = config.num_samples;
        uint16_t trainSize = static_cast<uint16_t>(totalSamples * config.train_ratio);
        uint16_t testSize;
        if(config.use_validation){
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
        for(const auto& sampleID : train_sampleIDs){
          train_backup.push_back(sampleID);
        }
        train_backup.sort();

        while(test_sampleIDs.size() < testSize) {
            uint16_t i = static_cast<uint16_t>(esp_random() % totalSamples);
            if (train_sampleIDs.find(i) == train_sampleIDs.end()) {
                test_sampleIDs.insert(i);
            }
        }
        test_sampleIDs.fit();
        for(const auto& sampleID : test_sampleIDs){
          test_backup.push_back(sampleID);
        }
        test_backup.sort();
        if(config.use_validation) {
            // Create validation set from remaining samples    
            while(validation_sampleIDs.size() < validationSize) {
                uint16_t i = static_cast<uint16_t>(esp_random() % totalSamples);
                if (train_sampleIDs.find(i) == train_sampleIDs.end() && test_sampleIDs.find(i) == test_sampleIDs.end()) {
                    validation_sampleIDs.insert(i);
                }
            }
            validation_sampleIDs.fit();
            for(const auto& sampleID : validation_sampleIDs){
            validation_backup.push_back(sampleID);
            }
            validation_backup.sort();
        }

        if(config.use_validation) {
            validation_data.isLoaded = true;
            validation_data.flag = VALID_DATA;
        }

        train_data.flag = TRAIN_DATA;
        test_data.flag = TEST_DATA;

        train_data.isLoaded = true;
        test_data.isLoaded = true;

        train_data.allSamples = base_data.loadData(train_backup); // Load only training samples
        memory_tracker.log();   // check heap fragmentation at highest RAM usage point
        train_sampleIDs.clear(); // Clear sample IDs set to free memory
        train_sampleIDs.fit(); // Fit the set to release unused memory
        train_data.releaseData(false); // Write to binary SPIFFS, clear RAM

        test_data.allSamples = base_data.loadData(test_backup); // Load only testing samples
        test_sampleIDs.clear(); // Clear sample IDs set to free memory
        test_sampleIDs.fit(); // Fit the set to release unused memory
        test_data.releaseData(false); // Write to binary SPIFFS, clear RAM

        if(config.use_validation) {
            validation_data.allSamples = base_data.loadData(validation_backup); // Load only validation samples
            validation_sampleIDs.clear(); // Clear sample IDs set to free memory
            validation_sampleIDs.fit(); // Fit the set to release unused memory
            validation_data.releaseData(false); // Write to binary SPIFFS, clear RAM
        }
    }

    // ---------------------------------------------------------------------------------
    void ClonesData() {
        Serial.println("<- clones data ->");
        dataList.clear();
        dataList.reserve(config.num_trees);
        uint16_t numSample = train_backup.size();
        uint16_t numSubSample = numSample * config.boostrap_ratio;
        uint16_t oob_size = numSample - numSubSample;

        b_vector<uint16_t> inBagSamplesVec;
        inBagSamplesVec.reserve(numSubSample);

        sampleID_set inBagSamples;
        inBagSamples.reserve(numSubSample);
        OOB_set oob_set;
        oob_set.reserve(oob_size);
        Rf_data sub_data;
        sub_data.allSamples.reserve(numSubSample);
        sub_data.flag = SUB_DATA;

        for (uint8_t i = 0; i < config.num_trees; i++) {
            Serial.printf("creating dataset for sub-tree : %d\n", i);

            sub_data.allSamples.clear();
            oob_set.clear();

            sub_data.index = i;
            sub_data.isLoaded = true;

            while(inBagSamples.size() < numSubSample) {
                uint16_t idx = static_cast<uint16_t>(esp_random() % numSample);
                uint16_t sampleId = train_backup[idx];
                
                if(inBagSamples.insert(sampleId)) { // Only insert if not already present
                    inBagSamplesVec.push_back(sampleId);
                }
            }
            sub_data.allSamples = train_data.loadData(inBagSamplesVec); // Load only in-bag samples
            inBagSamplesVec.clear(); // Clear vector to free memory
            sub_data.allSamples.fit();
            if(config.use_boostrap) {
                sub_data.boostrapData(numSample, config.num_samples);
            }
            sub_data.releaseData(false); // Save as binary
            
            // Create OOB set with samples not used in this tree
            for (uint16_t id : train_backup) {
                if (inBagSamples.find(id) == inBagSamples.end()) {
                    oob_set.insert(id);
                }
            }
            inBagSamples.clear(); // Clear in-bag samples set for next iteration
            dataList.push_back(make_pair(sub_data, oob_set));
            memory_tracker.log();
        }
    }
        
    // ------------------------------------------------------------------------------
    // read dataset parameters from /dataset_dp.csv and write to config
    void first_scan(const char* path) {
        // Read dataset parameters from /dataset_params.csv
        File file = SPIFFS.open(path, "r");
        if (!file) {
            Serial.println("❌ Failed to open data_params file.");
            return;
        }

        // Skip header line
        file.readStringUntil('\n');

        // Initialize variables with defaults
        uint16_t numSamples = 0;
        uint16_t numFeatures = 0;
        uint8_t numLabels = 0;
        uint16_t labelCounts[32] = {0}; // Support up to 32 labels
        uint8_t maxFeatureValue = 3;    // Default for 2-bit quantized data

        // Parse parameters from CSV
        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            int commaIndex = line.indexOf(',');
            if (commaIndex == -1) continue;

            String parameter = line.substring(0, commaIndex);
            String value = line.substring(commaIndex + 1);
            parameter.trim();
            value.trim();

            // Parse key parameters
            if (parameter == "num_features") {
                numFeatures = value.toInt();
            } else if (parameter == "num_samples") {
                numSamples = value.toInt();
            } else if (parameter == "num_labels") {
                numLabels = value.toInt();
            } else if (parameter == "max_feature_value") {
                maxFeatureValue = value.toInt();
            } else if (parameter.startsWith("samples_label_")) {
                // Extract label index from parameter name
                int labelIndex = parameter.substring(14).toInt(); // "samples_label_".length() = 14
                if (labelIndex < 32) {
                    labelCounts[labelIndex] = value.toInt();
                }
            } 
        }
        file.close();

        // Store parsed values
        config.num_features = numFeatures;
        config.num_samples = numSamples;
        config.num_labels = numLabels;

        // Analyze label distribution
        if (numLabels > 0) {
            uint16_t minorityCount = numSamples;
            uint16_t majorityCount = 0;

            for (uint8_t i = 0; i < numLabels; i++) {
                if (labelCounts[i] > majorityCount) {
                    majorityCount = labelCounts[i];
                }
                if (labelCounts[i] < minorityCount && labelCounts[i] > 0) {
                    minorityCount = labelCounts[i];
                }
            }

            float maxImbalanceRatio = 0.0f;
            if (minorityCount > 0) {
                maxImbalanceRatio = (float)majorityCount / minorityCount;
            }

            // Set training flags based on imbalance
            if (maxImbalanceRatio > 10.0f) {
                config.train_flag |= Rf_training_flags::RECALL;
                Serial.printf("📉 Imbalanced dataset (ratio: %.2f). Setting trainFlag to RECALL.\n", maxImbalanceRatio);
            } else if (maxImbalanceRatio > 3.0f) {
                config.train_flag |= Rf_training_flags::F1_SCORE;
                Serial.printf("⚖️ Moderately imbalanced dataset (ratio: %.2f). Setting trainFlag to F1_SCORE.\n", maxImbalanceRatio);
            } else if (maxImbalanceRatio > 1.5f) {
                config.train_flag |= Rf_training_flags::PRECISION;
                Serial.printf("🟨 Slight imbalance (ratio: %.2f). Setting trainFlag to PRECISION.\n", maxImbalanceRatio);
            } else {
                config.train_flag |= Rf_training_flags::ACCURACY;
                Serial.printf("✅ Balanced dataset (ratio: %.2f). Setting trainFlag to ACCURACY.\n", maxImbalanceRatio);
            }
        }

        // Dataset summary output
        Serial.printf("📊 Dataset Summary (from params file):\n");
        Serial.printf("  Total samples: %u\n", numSamples);
        Serial.printf("  Total features: %u\n", numFeatures);
        Serial.printf("  Unique labels: %u\n", numLabels);

        Serial.println("  Label distribution:");
        float lowest_distribution = 100.0f;
        for (uint8_t i = 0; i < numLabels; i++) {
            if (labelCounts[i] > 0) {
                float percent = (float)labelCounts[i] / numSamples * 100.0f;
                if (percent < lowest_distribution) {
                    lowest_distribution = percent;
                }
            }
        }
        // this->lowest_distribution = lowest_distribution / 100.0f; // Store as fraction
        
        // Check if validation should be disabled due to low sample count
        if (lowest_distribution * numSamples * config.valid_ratio < 10) {
            config.use_validation = false;
            Serial.println("⚖️ Setting use_validation to false due to low sample count in validation set.");
            config.train_ratio = 0.7f; // Adjust train ratio to compensate
        }
        Serial.println();

        // Calculate optimal parameters based on dataset size
        int baseline_minsplit_ratio = 100 * (config.num_samples / 500 + 1); 
        if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
        uint8_t min_minSplit = min(2, (int)(config.num_samples / baseline_minsplit_ratio));
        int dynamic_max_split = min(min_minSplit + 6, (int)(log2(config.num_samples) / 4 + config.num_features / 25.0f));
        uint8_t max_minSplit = min(24, dynamic_max_split); // Cap at 24 to prevent overly simple trees.
        if (max_minSplit <= min_minSplit) max_minSplit = min_minSplit + 4; // Ensure a valid range.


        int base_maxDepth = max((int)log2(config.num_samples * 2.0f), (int)(log2(config.num_features) * 2.5f));
        uint8_t max_maxDepth = max(6, base_maxDepth);
        int dynamic_min_depth = max(4, (int)(log2(config.num_features) + 2));
        uint8_t min_maxDepth = min((int)max_maxDepth - 2, dynamic_min_depth); // Ensure a valid range.
        if (min_maxDepth >= max_maxDepth) min_maxDepth = max_maxDepth - 2;
        if (min_maxDepth < 4) min_maxDepth = 4;

        config.min_split = (min_minSplit + max_minSplit) / 2 + 2;
        config.max_depth = (min_maxDepth + max_maxDepth) / 2 - 6;

        Serial.printf("Setting minSplit to %u and maxDepth to %u based on dataset size.\n", 
                     config.min_split, config.max_depth);

        for(uint8_t i = min_minSplit; i <= max_minSplit; i = i+2) {
            config.min_split_range.push_back(i);
        }
        for(uint8_t i = min_maxDepth; i <= max_maxDepth; i = i+2) {
            config.max_depth_range.push_back(i);
        }

        if(config.min_split_range.empty()) config.min_split_range.push_back(config.min_split); // Ensure at least one value
        if(config.max_depth_range.empty()) config.max_depth_range.push_back(config.max_depth); // Ensure at least one value
        Serial.println();
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
        if(data_flag == Rf_data_flags::TRAIN_DATA || data_flag == Rf_data_flags::TEST_DATA || data_flag == Rf_data_flags::VALID_DATA) {   
            // Restore train/test set from backup and base data / baseFile (a)
            Rf_data *restore_data;
            b_vector<uint16_t> *restore_backup;
            switch (data_flag) {
                case Rf_data_flags::TRAIN_DATA:
                {
                    if(train_backup.empty()) {
                        Serial.println("❌ No training backup available, cannot restore training data.");
                        return;
                    }
                    restore_data = &train_data;
                    restore_backup = &train_backup;
                }
                break;
                case Rf_data_flags::TEST_DATA:
                {
                    if(test_backup.empty()) {
                        Serial.println("❌ No testing backup available, cannot restore testing data.");
                        return;
                    }
                    restore_data = &test_data;
                    restore_backup = &test_backup;
                }
                break;
                case Rf_data_flags::VALID_DATA:
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
            restore_data->allSamples = base_data.loadData(*restore_backup); // Load samples from base data using backup IDs
            if(restore_data->allSamples.empty()) {
                Serial.println("❌ Failed to restore data from backup.");
                return;
            }
            restore_data->isLoaded = true; // Mark as loaded
            Serial.printf("Training data restored with %d samples.\n", train_data.allSamples.size());
        }else if(data_flag == Rf_data_flags::SUB_DATA){
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
            uint16_t numSubSamples = train_backup.size() * config.boostrap_ratio; // Calculate number of samples for this subset
            sampleID_set inBagSamples;
            inBagSamples.reserve(numSubSamples);
            b_vector<uint16_t> inBagSamplesVec;
            inBagSamplesVec.reserve(numSubSamples);

            while(inBagSamples.size() < numSubSamples) {
                uint16_t idx = static_cast<uint16_t>(esp_random() % train_backup.size());
                uint16_t sampleId = train_backup[idx]; // Get sample ID from backup
                if(inBagSamples.insert(sampleId)) { // Only insert if not already present
                    inBagSamplesVec.push_back(sampleId);
                }
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
            if(config.use_boostrap) {
                subsetData.boostrapData(numSubSamples, config.num_samples); // Apply boostrap sampling if enabled
            } 
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
            // Force yield to allow garbage collection
            yield();        
            delay(10);
        }
        root.clear();
    }
    
    typedef struct SplitInfo {
        float gain = -1.0f;
        uint16_t featureID = 0;
        uint8_t threshold = 0;
    } SplitInfo;

    struct NodeStats {
        unordered_set<uint8_t> labels;
        b_vector<uint16_t, SMALL> labelCounts; 
        uint8_t majorityLabel;
        uint16_t totalSamples;
        
        NodeStats(uint8_t numLabels) : majorityLabel(0), totalSamples(0) {
            for(int i = 0; i < numLabels; i++) labelCounts[i] = 0;
        }
        
        void analyzeSamples(const b_vector<uint16_t>& sampleIDs, sample_set& allSamples, uint8_t numLabels) {
            totalSamples = sampleIDs.size();
            uint16_t maxCount = 0;
            
            // Single pass through sample IDs for efficiency
            for (const auto& sampleID : sampleIDs) {
                auto it = allSamples.find(sampleID);
                if (it != allSamples.end()) {
                    uint8_t label = it->second.label;
                    labels.insert(label);
                    if (label < numLabels && label < 32) { // Bounds check
                        labelCounts[label]++;
                        if (labelCounts[label] > maxCount) {
                            maxCount = labelCounts[label];
                            majorityLabel = label;
                        }
                    }
                }
            }
        }
    };

    // Memory-efficient findBestSplit that works directly with sample IDs
    SplitInfo findBestSplit(const b_vector<uint16_t>& sampleIDs, sample_set& allSamples, 
                                const unordered_set<uint16_t>& selectedFeatures, bool use_Gini, uint8_t numLabels) {
        SplitInfo bestSplit;
        uint32_t totalSamples = sampleIDs.size();
        if (totalSamples < 2) return bestSplit; // Cannot split less than 2 samples

        // Calculate base impurity
        vector<uint16_t> baseLabelCounts(numLabels, 0);
        for (const auto& sampleID : sampleIDs) {
            auto it = allSamples.find(sampleID);
            if (it != allSamples.end() && it->second.label < numLabels) {
                baseLabelCounts[it->second.label]++;
            }
        }

        float baseImpurity;
        if (use_Gini) {
            baseImpurity = 1.0f;
            for (uint8_t i = 0; i < numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * p;
                }
            }
        } else { // Entropy
            baseImpurity = 0.0f;
            for (uint8_t i = 0; i < numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        // Iterate through the randomly selected features
        for (const auto& featureID : selectedFeatures) {
            // Use a flat vector for the contingency table
            vector<uint16_t> counts(4 * numLabels, 0);
            uint32_t value_totals[4] = {0};

            // Build contingency table
            for (const auto& sampleID : sampleIDs) {
                auto it = allSamples.find(sampleID);
                if (it != allSamples.end()) {
                    const Rf_sample& sample = it->second;
                    if (featureID < sample.features.size() && sample.label < numLabels) {
                        uint8_t featureValue = sample.features[featureID];
                        if (featureValue < 4) {
                            counts[featureValue * numLabels + sample.label]++;
                            value_totals[featureValue]++;
                        }
                    }
                }
            }

            // Test split thresholds (0, 1, 2)
            for (uint8_t threshold = 0; threshold < 3; threshold++) {
                uint32_t leftTotal = 0, rightTotal = 0;
                vector<uint16_t> leftCounts(numLabels, 0), rightCounts(numLabels, 0);
                
                for (uint8_t value = 0; value < 4; value++) {
                    for (uint8_t label = 0; label < numLabels; label++) {
                        uint16_t count = counts[value * numLabels + label];
                        if (value <= threshold) {
                            leftCounts[label] += count;
                            leftTotal += count;
                        } else {
                            rightCounts[label] += count;
                            rightTotal += count;
                        }
                    }
                }

                if (leftTotal == 0 || rightTotal == 0) continue;

                // Calculate weighted impurity
                float leftImpurity = 0.0f, rightImpurity = 0.0f;
                
                if (use_Gini) {
                    leftImpurity = 1.0f;
                    rightImpurity = 1.0f;
                    for (uint8_t i = 0; i < numLabels; i++) {
                        if (leftCounts[i] > 0) {
                            float p = static_cast<float>(leftCounts[i]) / leftTotal;
                            leftImpurity -= p * p;
                        }
                        if (rightCounts[i] > 0) {
                            float p = static_cast<float>(rightCounts[i]) / rightTotal;
                            rightImpurity -= p * p;
                        }
                    }
                } else { // Entropy
                    for (uint8_t i = 0; i < numLabels; i++) {
                        if (leftCounts[i] > 0) {
                            float p = static_cast<float>(leftCounts[i]) / leftTotal;
                            leftImpurity -= p * log2f(p);
                        }
                        if (rightCounts[i] > 0) {
                            float p = static_cast<float>(rightCounts[i]) / rightTotal;
                            rightImpurity -= p * log2f(p);
                        }
                    }
                }

                float weightedImpurity = (static_cast<float>(leftTotal) / totalSamples) * leftImpurity + 
                                       (static_cast<float>(rightTotal) / totalSamples) * rightImpurity;
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

    // Breadth-first tree building for optimal node layout - MEMORY OPTIMIZED
    void buildTree(Rf_tree& tree, Rf_data& data) {
        tree.nodes.clear();
        if (data.allSamples.empty()) return;

        uint32_t initialRAM = ESP.getFreeHeap();
    
        // Create root node and initial sample ID list
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);
        
        b_vector<uint16_t> rootSampleIDs;
        rootSampleIDs.reserve(data.allSamples.size());
        for (const auto& entry : data.allSamples) {
            rootSampleIDs.push_back(entry.first);
        }

        queue_nodes.push_back(NodeToBuild(0, std::move(rootSampleIDs), 0));
        memory_tracker.log(false); // Log memory usage after initial setup
        
        // Process nodes breadth-first with periodic cleanup
        while (!queue_nodes.empty()) {
            NodeToBuild current = std::move(queue_nodes.front());
            queue_nodes.erase(0); // Remove first element
            
            // Analyze node samples efficiently
            NodeStats stats(config.num_labels);
            stats.analyzeSamples(current.sampleIDs, data.allSamples, config.num_labels);
            bool shouldBeLeaf = false;
            uint8_t leafLabel = stats.majorityLabel;
            
            // Determine if this should be a leaf
            if (stats.labels.size() == 1) {
                shouldBeLeaf = true;
                leafLabel = *stats.labels.begin();
            } else if (stats.totalSamples < config.min_split || current.depth >= config.max_depth) {
                shouldBeLeaf = true;
                // leafLabel already set to majority label
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
            
            while (selectedFeatures.size() < num_selected_features) {
                uint16_t idx = static_cast<uint16_t>(esp_random() % config.num_features);
                selectedFeatures.insert(idx);
            } 
            // Use memory-efficient split finding
            SplitInfo bestSplit = findBestSplit(current.sampleIDs, data.allSamples, 
                                                   selectedFeatures, config.use_gini, config.num_labels);
            float gain_threshold = config.use_gini ? config.impurity_threshold/2 : config.impurity_threshold;
            
            if (bestSplit.gain <= gain_threshold) {
                // Make it a leaf with majority label (already calculated)
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            // Configure as internal node
            tree.nodes[current.nodeIndex].setFeatureID(bestSplit.featureID);
            tree.nodes[current.nodeIndex].setThreshold(bestSplit.threshold);
            tree.nodes[current.nodeIndex].setIsLeaf(false);
            
            // Split sample IDs for children (memory efficient with pre-allocation)
            b_vector<uint16_t> leftSampleIDs, rightSampleIDs;
            // Pre-estimate split sizes to avoid reallocations
            uint16_t estimatedLeftSize = current.sampleIDs.size() / 2;
            uint16_t estimatedRightSize = current.sampleIDs.size() - estimatedLeftSize;
            leftSampleIDs.reserve(estimatedLeftSize);
            rightSampleIDs.reserve(estimatedRightSize);
            
            for (const auto& sampleID : current.sampleIDs) {
                auto it = data.allSamples.find(sampleID);
                if (it != data.allSamples.end()) {
                    if (it->second.features[bestSplit.featureID] <= bestSplit.threshold) {
                        leftSampleIDs.push_back(sampleID);
                    } else {
                        rightSampleIDs.push_back(sampleID);
                    }
                }
            }
            
            // Shrink vectors to actual size to save memory
            leftSampleIDs.fit();
            rightSampleIDs.fit();
            
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
            if (!leftSampleIDs.empty()) {
                queue_nodes.push_back(NodeToBuild(leftChildIndex, std::move(leftSampleIDs), current.depth + 1));
            } else {
                // Empty left child becomes a leaf with majority label
                tree.nodes[leftChildIndex].setIsLeaf(true);
                tree.nodes[leftChildIndex].setLabel(leafLabel);
                tree.nodes[leftChildIndex].setFeatureID(0);
            }
            
            if (!rightSampleIDs.empty()) {
                queue_nodes.push_back(NodeToBuild(rightChildIndex, std::move(rightSampleIDs), current.depth + 1));
            } else {
                // Empty right child becomes a leaf with majority label
                tree.nodes[rightChildIndex].setIsLeaf(true);
                tree.nodes[rightChildIndex].setLabel(leafLabel);
                tree.nodes[rightChildIndex].setFeatureID(0);
            }
        }
    }

    uint8_t predClassSample(Rf_sample& s){
        int16_t totalPredict = 0;
        unordered_map<uint8_t, uint8_t> predictClass;
        // Serial.println("here 1.6");
        
        // Use streaming prediction 
        for(auto& tree : root){
            uint8_t predict = tree.predictSample(s); 
            if(predict < config.num_labels){
                predictClass[predict]++;
                totalPredict++;
            }
        }
        // Serial.println("here 1.7");
        
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
        // Serial.println("here 1.8"); 
        // Check certainty threshold
        float certainty = static_cast<float>(max) / totalPredict;
        if(certainty < config.unity_threshold) {
            return 255;
        }
        return mostPredict;
    }

    // hellper function: evaluate the entire forest, using OOB_score : iterate over all samples in 
    // the train set and evaluate by trees whose OOB set contains its ID
    float get_training_evaluation_index(){
        Serial.println("Get training evaluation index... ");

        // chunk size for processing
        uint16_t buffer_chunk;
        if(train_backup.size() == 0){
            Serial.println("❌ No training samples available for evaluation!");
            return 0.0f;
        } else buffer_chunk = train_backup.size() / 4;
        if(buffer_chunk < 130) buffer_chunk = 130; 

        // early preparing resources
        sample_set train_samples_buffer;
        b_vector<uint16_t> sampleIDs_bag;
        b_vector<uint8_t, SMALL> activeTrees;
        unordered_map<uint8_t, uint8_t> oobPredictClass;

        train_samples_buffer.reserve(buffer_chunk);
        sampleIDs_bag.reserve(buffer_chunk);
        activeTrees.reserve(config.num_trees);
        oobPredictClass.reserve(config.num_labels);

        // Initialize OOB and validation matrices
        b_vector<uint16_t, SMALL> oob_tp(config.num_labels, 0); 
        b_vector<uint16_t, SMALL> oob_fp(config.num_labels, 0);
        b_vector<uint16_t, SMALL> oob_fn(config.num_labels, 0);
        b_vector<uint16_t, SMALL> valid_tp(config.num_labels, 0);
        b_vector<uint16_t, SMALL> valid_fp(config.num_labels, 0);
        b_vector<uint16_t, SMALL> valid_fn(config.num_labels, 0);

        uint16_t oob_correct = 0, oob_total = 0, valid_correct = 0, valid_total = 0;
        uint16_t start_pos = 0 , end_pos = 0;

        loadForest();
        memory_tracker.log();
        train_backup.sort();

        // OOB part 
        for(start_pos = 0; start_pos < train_backup.size(); start_pos += buffer_chunk){
            end_pos = start_pos + buffer_chunk;
            if(end_pos > train_backup.size()) end_pos = train_backup.size();

            sampleIDs_bag.clear();
            for(uint16_t i = start_pos; i < end_pos; i++){
                sampleIDs_bag.push_back(train_backup[i]);
            }
            train_samples_buffer.clear();
            train_samples_buffer.fit();
            train_samples_buffer = train_data.loadData(sampleIDs_bag);
            if(train_samples_buffer.empty()){
                Serial.println("❌ No training samples found in the buffer!");
                Serial.println("Switching to plan B: loading all training data into RAM...");
                releaseForest();
                bool preloaded = train_data.isLoaded;
                if(!preloaded) train_data.loadData(); // FIX: load only if not already loaded
                train_samples_buffer = train_data.allSamples;
                if(train_samples_buffer.empty()){
                    Serial.println("❌ No training samples found in RAM!");
                    return 0.0f;
                }
                // reset OOB matrices and counters
                for(uint16_t i=0;i<config.num_labels;i++){ oob_tp[i]=oob_fp[i]=oob_fn[i]=0; }
                oob_correct = 0; oob_total = 0;

                // End chunk loop; process the full set once
                end_pos = train_backup.size();
                if(!preloaded) train_data.releaseData(true);
                memory_tracker.log();
                loadForest();
            }

            for(const auto& sample : train_samples_buffer){      
                activeTrees.clear();
                oobPredictClass.clear();    

                uint16_t sampleId = sample.first;
                uint8_t actualLabel = sample.second.label;
                
                for(uint8_t i = 0; i < config.num_trees; i++){
                    if(dataList[i].second.find(sampleId) != dataList[i].second.end()){
                        activeTrees.push_back(i);
                    }
                }
                if(activeTrees.empty()){
                    continue;
                }

                uint16_t oobTotalPredict = 0; 
                for(const uint8_t& treeIdx : activeTrees){
                    uint8_t predict = root[treeIdx].predictSample(sample.second);
                    if(predict < config.num_labels){
                        oobPredictClass[predict]++;
                        oobTotalPredict++;
                    }
                }
                
                if(oobTotalPredict == 0) continue;
                
                uint8_t oobPredictedLabel = 255;
                uint16_t maxVotes = 0;
                for(const auto& predict : oobPredictClass){
                    if(predict.second > maxVotes){
                        maxVotes = predict.second;
                        oobPredictedLabel = predict.first;
                    }
                }
                
                float certainty = static_cast<float>(maxVotes) / oobTotalPredict;
                if(certainty < config.unity_threshold) {
                    continue;
                }
                
                oob_total++;
                if(oobPredictedLabel == actualLabel){
                    oob_correct++;
                    if(actualLabel < config.num_labels) oob_tp[actualLabel]++;
                } else {
                    if(actualLabel < config.num_labels) oob_fn[actualLabel]++;
                    if(oobPredictedLabel < config.num_labels) oob_fp[oobPredictedLabel]++;
                }
            }
            sampleIDs_bag.fit();
        }

        if(config.use_validation){
            validation_data.loadData();
            for(const auto& sample : validation_data.allSamples){
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
                    if(actualLabel < config.num_labels) valid_tp[actualLabel]++;
                } else {
                    if(actualLabel < config.num_labels) valid_fn[actualLabel]++;
                    if(validPredictedLabel < config.num_labels) valid_fp[validPredictedLabel]++;
                }
            }
            validation_data.releaseData(true);
        }
        
        Serial.printf("Ram before releasing trees: %d\n", ESP.getFreeHeap());
        releaseForest(); // Release trees from RAM after evaluation
        Serial.printf("Ram after releasing trees: %d\n", ESP.getFreeHeap());

        // Calculate the requested metric
        float oob_result = 0.0f;
        float valid_result = 0.0f;
        float combined_oob_result = 0.0f;
        float combined_valid_result = 0.0f;
        uint8_t training_flag = static_cast<uint8_t>(config.train_flag);
        uint8_t numFlags = 0;
        
        if(oob_total == 0){
            Serial.println("❌ No valid OOB predictions found!");
            return 0.0f;
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
            Serial.printf("OOB Precision: %.3f\n", oob_result);
            Serial.printf("Validation Precision: %.3f\n", valid_result);
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
            Serial.printf("Validation F1-Score: %.3f\n", valid_result);
            Serial.printf("OOB F1-Score: %.3f\n", oob_result);
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
        float result = 0.0f;
        float oob_score = combined_oob_result / numFlags;
        float valid_score = combined_valid_result / numFlags;
        if(config.use_validation) result = oob_score * (1.0f - config.combine_ratio) + valid_score * config.combine_ratio;
        else result = oob_score; // If no validation, use only OOB score

        return result;
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
    // Memory-Efficient Grid Search Training Function
    void training(){
        Serial.println("Starting training with memory-efficient grid search...");

        float initial_score = get_training_evaluation_index();

        float best_score = initial_score;
        Serial.printf("Initial score: %.3f\n", initial_score);

        for(auto min_split : config.min_split_range){
            for(auto max_depth : config.max_depth_range){
                config.min_split = min_split;
                config.max_depth = max_depth;

                MakeForest(); // Rebuild forest with new parameters
                float score = get_training_evaluation_index();
                Serial.printf("Score with min_split=%d, max_depth=%d: %.3f\n", 
                              min_split, max_depth, score);
                if(score > best_score){
                    best_score = score;
                    //save best forest
                    Serial.printf("New best score: %.3f with min_split=%d, max_depth=%d\n", 
                                  best_score, min_split, max_depth);
                    // Save the best forest to SPIFFS
                    releaseForest(); // Release current trees from RAM
                }
            }
        }
    }

  public:

    // New combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        bool pre_load_data = true;
        if(!data.isLoaded){
            data.loadData();
            pre_load_data = false;
        }
        loadForest();
        // Serial.println("here 0");
      
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
        // Serial.println("here 1");
        
        // Single pass over samples
        for (const auto& kv : data.allSamples) {
            uint8_t actual = kv.second.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(kv.second));
            // Serial.println("here 1.5");
            
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
        // Serial.println("here 2");
        
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
            
            // Serial.printf("Label %d: TP=%d, FP=%d, FN=%d, Prec=%.3f, Rec=%.3f, F1=%.3f\n", 
            //             label, tpv, fpv, fnv, prec, rec, f1);
        }
        // Serial.println("here 3");
        
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

    String predict(b_vector<float>& features){
        auto p_sample = categorizer.categorizeSample(features);
        return categorizer.getOriginalLabel(predict(p_sample));
    }

    // get prediction score based on training flags
    float predict2(Rf_data& data, Rf_training_flags flags) {
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
        for (const auto& entry : acc) {
            total_acc += entry.second;
        }
        return total_acc / acc.size();
    } 
    void visual_result(Rf_data& testSet) {
        loadForest(); // Ensure all trees are loaded before prediction
        testSet.loadData(); // Load test set data if not already loaded
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
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    delay(2000);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }
    manageSPIFFSFiles();
    delay(1000);
    Serial.printf("===> ROM left: %d\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());;

    // const char* filename = "/walker_fall.bin";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "/digit_data_nml.bin"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(filename);

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    forest.MakeForest();

    // float initial_score = forest.predict(forest.test_data, static_cast<Rf_training_flags>(forest.config.train_flag));
    // Serial.printf("Initial score: %.3f\n", initial_score);

    forest.training(); 

    auto result = forest.predict(forest.test_data);
    Serial.printf("\nlowest RAM: %d\n", forest.memory_tracker.lowest_ram);
    Serial.printf("lowest ROM: %d\n", forest.memory_tracker.lowest_rom);

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
    Serial.printf("Trees: %d, Max Depth: %d, Min Split: %d\n", forest.config.num_trees, forest.config.max_depth, forest.config.min_split);
    Serial.printf("Labels in dataset: %d\n", forest.config.num_labels);
    Serial.printf("Average Precision: %.3f\n", avgPrecision);
    Serial.printf("Average Recall: %.3f\n", avgRecall);
    Serial.printf("Average F1-Score: %.3f\n", avgF1);
    Serial.printf("Average Accuracy: %.3f\n", avgAccuracy);

    // check actual prediction time 
    b_vector<float> sample = MAKE_FLOAT_LIST(0,0,0.014528,0.000782722,0.817357,0.277754,0.137853,0.0161738,0,0,0,0,0.252639,0.0556202,0.319129,0.257954,0.0149651,0.0648652,0.0220988,0.00182159,0.877327,0.00550756,0.133955,0.0187333,0.0275399,0,0,0.0248582,0.0363544,0.134907,0.406297,0.14517,0.660452,0.0193004,0.0872476,0.0170596,0.183969,0.0070201,0.0392839,0.00355617,0.0551359,0.124477,0.226905,0.0582358,0.0218746,0.00100708,0.254847,0.617445,0,0,0,0,0.768671,0.0542131,0.301566,0.249534,0.163292,0.215959,0.210117,0.111435,0.00786254,0,0.181712,0.300076,0.0661908,0.000662339,0,0.0419681,0.345039,0.220849,0.830398,0.239182,0.0471657,0.0858931,0.155634,0.120172,0.171163,0.00210992,0.00423227,0.0409593,0.255201,0.133301,0.314904,0.0781977,0.0096404,0.00762942,0.314006,0.42933,0.000130379,0.00131339,0.00315603,0,0.64453,0,0.000782274,0.336626,0,0.0176682,0.0297794,0,0,0,0.0138204,0,0.0573581,0.0521479,0.0586363,0.267836,0.00254183,0.216103,0.916323,0.176272,0,0.0117796,0.0287043,0,0.0182255,0.000955436,0.00278896,0.0780402,0.0260911,0.0408793,0.635657,0.265235,0.100382,0.709452,0.0494404,0.00125326,0.000117986,0,0.00284004,0.00127189,0.358305,0.000972935,0.000117986,0.523924,0,0.553178,0.243659,0.000263824,0.444076,0.185569,0.00661191,0.00721455
);
    long unsigned start = millis();
    String pred = forest.predict2(sample);
    long unsigned end = millis();
    Serial.printf("Prediction for sample took %lu ms: %s\n", end - start, pred.c_str());


    // forest.visual_result(forest.test_data); // Optional visualization
}

void loop() {
    manageSPIFFSFiles();
}