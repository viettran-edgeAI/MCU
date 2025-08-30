#include "Rf_components.h"
#define SUPPORT_LABEL_MAPPING

using namespace mcu;

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
    String model_name;  // base_name

    Rf_data base_data;
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    Rf_base base;
    Rf_config config;
    Rf_random random_generator;
    Rf_categorizer categorizer; 
    Rf_memory_logger memory_tracker;
    Rf_node_predictor  node_predictor; // Node predictor number of nodes required for a tree based on min_split and max_depth

private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    b_vector<ID_vector<uint16_t,2>, SMALL> dataList; // each ID_vector stores sample IDs of a sub_dataset, reference to index of allSamples vector in train_data
    b_vector<NodeToBuild> queue_nodes; // Queue for breadth-first tree building

    bool optimal_mode = false;  
    bool is_loaded = false;

    // Random Number Generator (internal only)


public:
    RandomForest(){};
    RandomForest(const char* model_name, uint64_t random_seed = 42) : random_generator(random_seed, true) {
        this->model_name = String(model_name);

        // initial components
        memory_tracker.init();
        base.init(model_name); // Initialize base with the provided base name
        config.init(base.get_configFile());
        categorizer.init(base.get_ctgFile());
        node_predictor.init(base.get_nodePredictFile());

        // load resources
        config.loadConfig();  // load config file
        config.num_trees = 18;
        first_scan(base.get_dpFile());   // load data parameters into config
        categorizer.loadCategorizer(); // load categorizer
        node_predictor.loadPredictor(); // load node predictor

        // init base data (make a clone of base_data to avoid modifying original)
        cloneFile(base.get_baseFile().c_str(), base_data_file);
        base_data.init(base_data_file, config.num_features);

        // data separation
        dataList.reserve(config.num_trees);
        memory_tracker.log("forest init");
        splitData();
        ClonesData();
    }
    
    // Enhanced destructor
    ~RandomForest(){
        Serial.println("🧹 Cleaning files... ");

        // clear all trees
        for(auto& tree : root){
            tree.purgeTree(this->model_name);   // completely remove tree - for development stage 
            // tree.releaseTree();               // save tree to SPIFFS - for production stage
        }
          
        // clear all Rf_data
        train_data.purgeData();
        test_data.purgeData();
        base_data.purgeData();
        if(config.use_validation) validation_data.purgeData();

        // re_train node predictor after each training session
        node_predictor.re_train();
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
        
        // Load train_data once for all trees
        long unsigned a1 = micros();
        train_data.loadData();
        long unsigned a2 = micros();
        Serial.printf("Train data loaded in %lu us\n", a2 - a1);
        for(uint8_t i = 0; i < config.num_trees; i++){
            Serial.printf("%d, ", i);
            Rf_tree tree(i);
            tree.nodes.reserve(estimatedNodes); // Reserve memory for nodes based on prediction
            queue_nodes.clear(); // Clear queue for each tree

            buildTree(tree, dataList[i]);

            n_data.total_nodes += tree.countNodes();
            
            tree.isLoaded = true; 
            tree.releaseTree(this->model_name); // Save tree to SPIFFS
            root.push_back(tree);
        }
        
        // Release train_data after all trees are built
        train_data.releaseData(); // Keep metadata but release sample data
        n_data.total_nodes /= config.num_trees; // Average nodes per tree
        node_predictor.buffer.push_back(n_data); // Add node data to predictor buffer

        Serial.printf("Total nodes: %d, Average nodes per tree: %d\n", n_data.total_nodes * config.num_trees, n_data.total_nodes);

        Serial.printf("RAM after forest creation: %d\n", ESP.getFreeHeap());
    }
  private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets. create them from base_data in SPIFFS to avoid peak RAM usage
    void splitData() {
        Serial.println("<-- split data -->");
        
        size_t maxID = config.num_samples;  // total number of samples
        uint16_t trainSize = static_cast<uint16_t>(maxID * config.train_ratio);
        uint16_t testSize;
        if(config.use_validation)  testSize = static_cast<uint16_t>((maxID - trainSize) * 0.5);
        else  testSize = maxID - trainSize; // No validation set, use all remaining for testing
        uint16_t validationSize = maxID - trainSize - testSize;
        

        // create train_data 
        sampleID_set train_sampleIDs(maxID);
        while (train_sampleIDs.size() < trainSize) {
            uint16_t sampleId = static_cast<uint16_t>(random_generator.bounded(static_cast<uint32_t>(maxID)));
            train_sampleIDs.push_back(sampleId); 
        }
        train_data.init("/train_data.bin", config.num_features);
        train_data.loadData(base_data, train_sampleIDs); 


        train_data.releaseData(false); // Write to binary SPIFFS, clear RAM
        Serial.println("Train data loaded and released.");
        
        // create test_data
        sampleID_set test_sampleIDs(maxID);
        while(test_sampleIDs.size() < testSize) {
            uint16_t i = static_cast<uint16_t>(random_generator.bounded(static_cast<uint32_t>(maxID)));
            if (!train_sampleIDs.contains(i)) {
                test_sampleIDs.push_back(i);
            }
        }
        test_data.init("/test_data.bin", config.num_features);
        test_data.loadData(base_data, test_sampleIDs); // Load only test samples
        test_data.releaseData(false); // Write to binary SPIFFS, clear RAM
        Serial.println("Test data loaded and released.");

        // create validation_data
        if(config.use_validation){
            sampleID_set validation_sampleIDs(maxID);
            while(validation_sampleIDs.size() < validationSize) {
                uint16_t i = static_cast<uint16_t>(random_generator.bounded(static_cast<uint32_t>(maxID)));
                if (!train_sampleIDs.contains(i) && !test_sampleIDs.contains(i)) {
                    validation_sampleIDs.push_back(i);
                }
            }
            validation_data.init("/validation_data.bin", config.num_features);
            validation_data.loadData(base_data, validation_sampleIDs); // Load only validation samples
            validation_data.releaseData(false); // Write to binary SPIFFS, clear RAM
            Serial.println("Validation data loaded and released.");
        }
        memory_tracker.log("split data");
    }

    // ---------------------------------------------------------------------------------
    void ClonesData() {
        Serial.println("<- clones data ->");
        dataList.clear();
        dataList.reserve(config.num_trees);
        uint16_t numSample = train_data.size();
        uint16_t numSubSample;
        if(config.use_boostrap) {
            numSubSample = numSample; // Bootstrap sampling with replacement
            Serial.println("Using bootstrap sampling with replacement.");
        } else {
            // Sub-sampling without replacement
            numSubSample = static_cast<uint16_t>(numSample * config.boostrap_ratio); 
            Serial.println("No bootstrap, using unique samples IDs");
        }

        // Track hashes of each tree dataset to avoid duplicates across trees
        unordered_set<uint64_t> seen_hashes;
        seen_hashes.reserve(config.num_trees * 2);

        Serial.println("creating dataset for sub-tree : ");
        for (uint8_t i = 0; i < config.num_trees; i++) {
            Serial.printf("%d, ", i);

            // Create a new ID_vector for this tree
            // ID_vector stores sample IDs as bits, so we need to set bits at sample positions
            ID_vector<uint16_t,2> treeDataset;
            treeDataset.reserve(numSample);

            // Derive a deterministic per-tree RNG; retry with nonce if duplicate detected
            uint64_t nonce = 0;
            while (true) {
                treeDataset.clear();
                auto tree_rng = random_generator.deriveRNG(i, nonce);

                if (config.use_boostrap) {
                    // Bootstrap sampling: allow duplicates, track occurrence count
                    for (uint16_t j = 0; j < numSubSample; ++j) {
                        uint16_t idx = static_cast<uint16_t>(tree_rng.bounded(numSample));
                        // For ID_vector with 2 bits per value, we can store up to count 3
                        // Add the sample ID, which will increment its count in the bit array
                        treeDataset.push_back(idx);
                    }
                } else {
                    // Sample without replacement using partial Fisher-Yates
                    // Build an index array 0..numSample-1 (small uint16_t)
                    std::vector<uint16_t> arr;
                    arr.resize(numSample);
                    for (uint16_t t = 0; t < numSample; ++t) arr[t] = t;
                    for (uint16_t t = 0; t < numSubSample; ++t) {
                        uint16_t j = static_cast<uint16_t>(t + tree_rng.bounded(numSample - t));
                        uint16_t tmp = arr[t];
                        arr[t] = arr[j];
                        arr[j] = tmp;
                        // For unique sampling, just set bit once (count = 1)
                        treeDataset.push_back(arr[t]);
                    }
                }

                // Check for duplicate dataset across trees
                uint64_t h = random_generator.hashIDVector(treeDataset);
                if (seen_hashes.find(h) == seen_hashes.end()) {
                    seen_hashes.insert(h);
                    break; // unique, accept
                }
                // Otherwise, bump nonce and try a different deterministic variation
                nonce++;
                if (nonce > 8) {
                    // Fallback tweak: rotate a few indices deterministically
                    // Since ID_vector is a bit array, we need to modify the underlying data carefully
                    auto temp_vec = treeDataset;  // Copy current state
                    treeDataset.clear();
                    
                    // Re-add samples with slight modifications
                    for (uint16_t k = 0; k < min(5, (int)temp_vec.size()); ++k) {
                        // Get a sample ID and modify it slightly
                        uint16_t original_id = k < temp_vec.size() ? k : 0; // Simplified access
                        uint16_t modified_id = static_cast<uint16_t>((original_id + k + i) % numSample);
                        treeDataset.push_back(modified_id);
                    }
                    
                    // Add remaining samples from original
                    for (uint16_t k = 5; k < min(numSubSample, (uint16_t)temp_vec.size()); ++k) {
                        uint16_t id = k % numSample; // Simplified access
                        treeDataset.push_back(id);
                    }
                    
                    // accept after tweak
                    seen_hashes.insert(random_generator.hashIDVector(treeDataset));
                    break;
                }
            }
            dataList.push_back(std::move(treeDataset));
        }
        Serial.println();
        memory_tracker.log("after clones data");  
    }  
    
    // ------------------------------------------------------------------------------
    // read dataset parameters from /dataset_dp.csv and write to config
    void first_scan(const String& path) {
        // Read dataset parameters from /dataset_params.csv
        File file = SPIFFS.open(path.c_str(), "r");
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
        float expected_valid_samples = 0.0f;
        
        if (config.use_validation) {
            if (config.valid_ratio > 0.0f) {
                // Use explicit valid_ratio if set
                expected_valid_samples = (lowest_distribution / 100.0f) * numSamples * config.valid_ratio;
            } else {
                // Assume remaining samples after training are split 50/50 between test and validation
                float remaining_ratio = 1.0f - config.train_ratio;
                expected_valid_samples = (lowest_distribution / 100.0f) * numSamples * (remaining_ratio * 0.5f);
            }
            
            if (expected_valid_samples < 5.0f) {  // Need at least 5 samples per minority class for meaningful validation
                config.use_validation = false;
                Serial.printf("⚖️ Setting use_validation to false due to low sample count in validation set (%.1f expected samples for minority class).\n", expected_valid_samples);
                config.train_ratio = 0.7f; // Adjust train ratio to compensate
            }
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

        config.min_split = (min_minSplit + max_minSplit) / 2 - 2;
        config.max_depth = (min_maxDepth + max_maxDepth) / 2 + 6;

        Serial.printf("Setting minSplit to %u and maxDepth to %u based on dataset size.\n", 
                     config.min_split, config.max_depth);

        for(uint8_t i = min_minSplit; i <= max_minSplit; i = i+2) {
            config.min_split_range.push_back(i);
        }
        for(uint8_t i = min_maxDepth; i <= max_maxDepth; i = i+2) {
            config.max_depth_range.push_back(i);
        }
        memory_tracker.log("first scan");

        if(config.min_split_range.empty()) config.min_split_range.push_back(config.min_split); // Ensure at least one value
        if(config.max_depth_range.empty()) config.max_depth_range.push_back(config.max_depth); // Ensure at least one value
        Serial.println();
    }

    // FIXED: Enhanced forest cleanup
    void clearForest() {
        // Process trees one by one to avoid heap issues
        for (size_t i = 0; i < root.size(); i++) {
            root[i].purgeTree(this->model_name);
            // Force yield to allow garbage collection
            yield();        
            delay(10);
        }
        root.clear();
        // Remove old forest file to ensure clean slate
        char oldForestFile[32];
        snprintf(oldForestFile, sizeof(oldForestFile), "/%s_forest.bin", this->model_name.c_str());
        if(SPIFFS.exists(oldForestFile)) {
            SPIFFS.remove(oldForestFile);
            Serial.printf("🗑️ Removed old forest file: %s\n", oldForestFile);
        }
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
            labelCounts.reserve(numLabels);
            labelCounts.fill(0);
        }
        
        // New: analyze a slice [begin,end) over a shared indices array
        void analyzeSamplesRange(const b_vector<uint16_t, MEDIUM, 8>& indices, uint16_t begin, uint16_t end,
                                 uint8_t numLabels, const Rf_data& data) {
            totalSamples = (begin < end) ? (end - begin) : 0;
            uint16_t maxCount = 0;
            for (uint16_t k = begin; k < end; ++k) {
                uint16_t sampleID = indices[k];
                if (sampleID < data.size()) {
                    uint8_t label = data.getLabel(sampleID);
                    labels.insert(label);
                    if (label < numLabels && label < 32) {
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

    // New: Range-based variant operating on a shared indices array
    SplitInfo findBestSplitRange(const b_vector<uint16_t, MEDIUM, 8>& indices, uint16_t begin, uint16_t end,
                                 const unordered_set<uint16_t>& selectedFeatures, bool use_Gini, uint8_t numLabels) {
        SplitInfo bestSplit;
        uint32_t totalSamples = (begin < end) ? (end - begin) : 0;
        if (totalSamples < 2) return bestSplit;

        // Base label counts
        vector<uint16_t> baseLabelCounts(numLabels, 0);
        for (uint16_t k = begin; k < end; ++k) {
            uint16_t sid = indices[k];
            if (sid < train_data.size()) {
                uint8_t lbl = train_data.getLabel(sid);
                if (lbl < numLabels) baseLabelCounts[lbl]++;
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
        } else {
            baseImpurity = 0.0f;
            for (uint8_t i = 0; i < numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        for (const auto& featureID : selectedFeatures) {
            vector<uint16_t> counts(4 * numLabels, 0);
            uint32_t value_totals[4] = {0};

            for (uint16_t k = begin; k < end; ++k) {
                uint16_t sid = indices[k];
                if (sid < train_data.size()) {
                    uint8_t lbl = train_data.getLabel(sid);
                    if (lbl < numLabels) {
                        uint8_t fv = train_data.getFeature(sid, featureID);
                        if (fv < 4) {
                            counts[fv * numLabels + lbl]++;
                            value_totals[fv]++;
                        }
                    }
                }
            }

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

                float leftImpurity = 0.0f, rightImpurity = 0.0f;
                if (use_Gini) {
                    leftImpurity = 1.0f; rightImpurity = 1.0f;
                    for (uint8_t i = 0; i < numLabels; i++) {
                        if (leftCounts[i] > 0) { float p = static_cast<float>(leftCounts[i]) / leftTotal; leftImpurity -= p * p; }
                        if (rightCounts[i] > 0) { float p = static_cast<float>(rightCounts[i]) / rightTotal; rightImpurity -= p * p; }
                    }
                } else {
                    for (uint8_t i = 0; i < numLabels; i++) {
                        if (leftCounts[i] > 0) { float p = static_cast<float>(leftCounts[i]) / leftTotal; leftImpurity -= p * log2f(p); }
                        if (rightCounts[i] > 0) { float p = static_cast<float>(rightCounts[i]) / rightTotal; rightImpurity -= p * log2f(p); }
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
    void buildTree(Rf_tree& tree, ID_vector<uint16_t,2>& sampleIDs) {
        tree.nodes.clear();
        if (sampleIDs.empty()) return;

        uint32_t initialRAM = ESP.getFreeHeap();
    
        // Create root node and initial sample index list once per tree
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);

        // Build a single contiguous index array for this tree
        b_vector<uint16_t, MEDIUM, 8> indices;
        indices.reserve(sampleIDs.size());
        for (const auto& sid : sampleIDs) indices.push_back(sid);
        
        // Root covers the whole slice
        queue_nodes.push_back(NodeToBuild(0, 0, static_cast<uint16_t>(indices.size()), 0));
        
        // Process nodes breadth-first with minimal allocations
        while (!queue_nodes.empty()) {
            NodeToBuild current = std::move(queue_nodes.front());
            queue_nodes.erase(0);
            
            // Analyze node samples over the slice
            NodeStats stats(config.num_labels);
            stats.analyzeSamplesRange(indices, current.begin, current.end, config.num_labels, train_data);
            bool shouldBeLeaf = false;
            uint8_t leafLabel = stats.majorityLabel;
            
            if (stats.labels.size() == 1) {
                shouldBeLeaf = true;
                leafLabel = *stats.labels.begin();
            } else if (stats.totalSamples < config.min_split || current.depth >= config.max_depth) {
                shouldBeLeaf = true;
            }
            if (shouldBeLeaf) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            // Random feature subset
            uint8_t num_selected_features = static_cast<uint8_t>(sqrt(config.num_features));
            if (num_selected_features == 0) num_selected_features = 1;
            unordered_set<uint16_t> selectedFeatures;
            selectedFeatures.reserve(num_selected_features);
            uint16_t N = static_cast<uint16_t>(config.num_features);
            uint16_t K = num_selected_features > N ? N : num_selected_features;
            for (uint16_t j = N - K; j < N; ++j) {
                uint16_t t = static_cast<uint16_t>(random_generator.bounded(j + 1));
                if (selectedFeatures.find(t) == selectedFeatures.end()) selectedFeatures.insert(t);
                else selectedFeatures.insert(j);
            }
            
            // Find best split on the slice
            SplitInfo bestSplit = findBestSplitRange(indices, current.begin, current.end,
                                                     selectedFeatures, config.use_gini, config.num_labels);
            float gain_threshold = config.use_gini ? config.impurity_threshold/2 : config.impurity_threshold;
            
            if (bestSplit.gain <= gain_threshold) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            // Configure as internal node
            tree.nodes[current.nodeIndex].setFeatureID(bestSplit.featureID);
            tree.nodes[current.nodeIndex].setThreshold(bestSplit.threshold);
            tree.nodes[current.nodeIndex].setIsLeaf(false);
            
            // In-place partition of indices[current.begin, current.end)
            uint16_t iLeft = current.begin;
            for (uint16_t k = current.begin; k < current.end; ++k) {
                uint16_t sid = indices[k];
                if (sid < train_data.size() && 
                    train_data.getFeature(sid, bestSplit.featureID) <= bestSplit.threshold) {
                    if (k != iLeft) {
                        uint16_t tmp = indices[iLeft];
                        indices[iLeft] = indices[k];
                        indices[k] = tmp;
                    }
                    ++iLeft;
                }
            }
            uint16_t leftBegin = current.begin;
            uint16_t leftEnd = iLeft;
            uint16_t rightBegin = iLeft;
            uint16_t rightEnd = current.end;
            
            uint16_t leftChildIndex = tree.nodes.size();
            uint16_t rightChildIndex = leftChildIndex + 1;
            tree.nodes[current.nodeIndex].setLeftChildIndex(leftChildIndex);
            
            Tree_node leftChild; tree.nodes.push_back(leftChild);
            Tree_node rightChild; tree.nodes.push_back(rightChild);
            
            if (leftEnd > leftBegin) {
                queue_nodes.push_back(NodeToBuild(leftChildIndex, leftBegin, leftEnd, current.depth + 1));
            } else {
                tree.nodes[leftChildIndex].setIsLeaf(true);
                tree.nodes[leftChildIndex].setLabel(leafLabel);
                tree.nodes[leftChildIndex].setFeatureID(0);
            }
            if (rightEnd > rightBegin) {
                queue_nodes.push_back(NodeToBuild(rightChildIndex, rightBegin, rightEnd, current.depth + 1));
            } else {
                tree.nodes[rightChildIndex].setIsLeaf(true);
                tree.nodes[rightChildIndex].setLabel(leafLabel);
                tree.nodes[rightChildIndex].setFeatureID(0);
            }
        }
        tree.nodes.fit();
        memory_tracker.log("tree creation",false);
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
    
    // Helper function: evaluate the entire forest using OOB score and validation
    // Iterates over all samples in training data and evaluates using trees that didn't see each sample
    float get_training_evaluation_index(){
        Serial.println("Get training evaluation index... ");

        // Check if we have trained trees and data
        if(dataList.empty() || root.empty()){
            Serial.println("❌ No trained trees available for evaluation!");
            return 0.0f;
        }

        // Determine chunk size for memory-efficient processing
        uint16_t buffer_chunk = train_data.size() / 4;
        if(buffer_chunk < 130) buffer_chunk = 130;
        if(buffer_chunk > train_data.size()) buffer_chunk = train_data.size();

        // Pre-allocate evaluation resources
        Rf_data train_samples_buffer;
        sampleID_set sampleIDs_bag;
        b_vector<uint8_t, SMALL> activeTrees;
        unordered_map<uint8_t, uint8_t> oobPredictClass;

        train_samples_buffer.reserve(buffer_chunk);
        activeTrees.reserve(config.num_trees);
        oobPredictClass.reserve(config.num_labels);

        // Initialize confusion matrix components for OOB and validation
        uint8_t oobPredictVotes[config.num_labels];
        uint8_t validPredictVotes[config.num_labels];

        // using b_vector instead of stack array to avoid stack overflow 
        b_vector<uint16_t, SMALL, 4> oob_tp(config.num_labels,0);
        b_vector<uint16_t, SMALL, 4> oob_fp(config.num_labels,0);
        b_vector<uint16_t, SMALL, 4> oob_fn(config.num_labels,0);
        b_vector<uint16_t, SMALL, 4> valid_tp(config.num_labels,0);
        b_vector<uint16_t, SMALL, 4> valid_fp(config.num_labels,0);
        b_vector<uint16_t, SMALL, 4> valid_fn(config.num_labels,0);

        uint16_t oob_correct = 0, oob_total = 0, valid_correct = 0, valid_total = 0;

        // Load forest into memory for evaluation
        loadForest();
        memory_tracker.log("get training eval index");

        // Process training samples in chunks for OOB evaluation
        for(uint16_t start_pos = 0; start_pos < train_data.size(); start_pos += buffer_chunk){
            uint16_t end_pos = start_pos + buffer_chunk;
            if(end_pos > train_data.size()) end_pos = train_data.size();

            // Create chunk of sample IDs
            sampleIDs_bag.clear();
            sampleIDs_bag.set_ID_range(start_pos, end_pos);
            for(uint16_t i = start_pos; i < end_pos; i++){
                sampleIDs_bag.push_back(i);
            }

            // Load samples for this chunk
            train_samples_buffer.loadData(train_data, sampleIDs_bag);
            if(train_samples_buffer.size() == 0){
                Serial.println("❌ Failed to load training samples chunk!");
                continue;
            }

            // Process each sample in the chunk
            for(uint16_t idx = 0; idx < train_samples_buffer.size(); idx++){
                const Rf_sample& sampleData = train_samples_buffer[idx];
                uint16_t sampleID = start_pos + idx; // Calculate actual sample ID
                uint8_t actualLabel = sampleData.label;

                // Find trees that didn't use this sample (OOB trees)
                activeTrees.clear();
                for(uint8_t treeIdx = 0; treeIdx < config.num_trees && treeIdx < dataList.size(); treeIdx++){
                    // Check if this sample ID is NOT in the tree's training data
                    if(!dataList[treeIdx].contains(sampleID)){
                        activeTrees.push_back(treeIdx);
                    }
                }

                if(activeTrees.empty()){
                    continue; // No OOB trees for this sample
                }

                // Get OOB predictions
                oobPredictClass.clear();
                uint16_t oobTotalPredict = 0;

                for(const uint8_t& treeIdx : activeTrees){
                    if(treeIdx < root.size()){
                        uint8_t predict = root[treeIdx].predictSample(sampleData);
                        if(predict < config.num_labels){
                            oobPredictClass[predict]++;
                            oobTotalPredict++;
                        }
                    }
                }

                if(oobTotalPredict == 0) continue;

                // Find majority vote
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
                if(certainty < config.unity_threshold) {
                    continue;
                }

                // Update OOB metrics
                oob_total++;
                if(oobPredictedLabel == actualLabel){
                    oob_correct++;
                    if(actualLabel < config.num_labels) oob_tp[actualLabel]++;
                } else {
                    if(actualLabel < config.num_labels) oob_fn[actualLabel]++;
                    if(oobPredictedLabel < config.num_labels) oob_fp[oobPredictedLabel]++;
                }
            }
        }
        train_samples_buffer.purgeData();

        // Validation evaluation if enabled
        if(config.use_validation){
            Serial.println("Evaluating on validation set...");
            validation_data.loadData();
            
            for(uint16_t i = 0; i < validation_data.size(); i++){
                const Rf_sample& sampleData = validation_data[i];
                uint8_t actualLabel = sampleData.label;

                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                // Use all trees for validation prediction
                for(uint8_t i = 0; i < config.num_trees && i < root.size(); i++){
                    uint8_t predict = root[i].predictSample(sampleData);
                    if(predict < config.num_labels){
                        validPredictClass[predict]++;
                        validTotalPredict++;
                    }
                }

                if(validTotalPredict == 0) continue;

                // Find majority vote
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
                if(certainty < config.unity_threshold) {
                    continue;
                }

                // Update validation metrics
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
        
        Serial.printf("Memory before releasing trees: %d bytes\n", ESP.getFreeHeap());
        releaseForest(); // Release trees from RAM after evaluation
        Serial.printf("Memory after releasing trees: %d bytes\n", ESP.getFreeHeap());

        // Calculate metrics based on training flag
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

        // Calculate accuracy
        if(training_flag & 0x01){ // ACCURACY flag
            oob_result = static_cast<float>(oob_correct) / oob_total;
            valid_result = (valid_total > 0) ? static_cast<float>(valid_correct) / valid_total : 0.0f;
            Serial.printf("OOB Accuracy: %.3f (%d/%d)\n", oob_result, oob_correct, oob_total);
            if(config.use_validation){
                Serial.printf("Validation Accuracy: %.3f (%d/%d)\n", valid_result, valid_correct, valid_total);
            }
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        // Calculate precision
        if(training_flag & 0x02){ // PRECISION flag
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
            Serial.printf("OOB Precision: %.3f\n", oob_result);
            if(config.use_validation){
                Serial.printf("Validation Precision: %.3f\n", valid_result);
            }
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        // Calculate recall
        if(training_flag & 0x04){ // RECALL flag
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
            
            oob_result = oob_validLabels > 0 ? oob_totalRecall / oob_validLabels : 0.0f;
            valid_result = valid_validLabels > 0 ? valid_totalRecall / valid_validLabels : 0.0f;
            Serial.printf("OOB Recall: %.3f\n", oob_result);
            if(config.use_validation){
                Serial.printf("Validation Recall: %.3f\n", valid_result);
            }
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        // Calculate F1-Score
        if(training_flag & 0x08){ // F1_SCORE flag
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
            Serial.printf("OOB F1-Score: %.3f\n", oob_result);
            if(config.use_validation){
                Serial.printf("Validation F1-Score: %.3f\n", valid_result);
            }
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        // Combine results based on configuration
        float result = 0.0f;
        if(numFlags > 0){
            float oob_score = combined_oob_result / numFlags;
            float valid_score = combined_valid_result / numFlags;
            if(config.use_validation && valid_total > 0){
                result = oob_score * (1.0f - config.combine_ratio) + valid_score * config.combine_ratio;
                Serial.printf("Combined result (OOB: %.1f%%, Valid: %.1f%%): %.3f\n", 
                             (1.0f - config.combine_ratio) * 100, config.combine_ratio * 100, result);
            } else {
                result = oob_score;
            }
        }

        return result;
    }

    public:

    // -----------------------------------------------------------------------------------
    // Memory-Efficient Grid Search Training Function
    void training(){
        Serial.println("Starting training with memory-efficient grid search...");
        int total_combinations = config.min_split_range.size() * config.max_depth_range.size();
        Serial.printf("Total combinations: %d\n", total_combinations);

        float initial_score = get_training_evaluation_index();

        float best_score = initial_score;
        Serial.printf("Initial score: %.3f\n", initial_score);

        int loop_count = 0;

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
                if(loop_count++ > 3) return;
            }
        }
    }
    
    void loadForest(){
        // Safety check: Don't load if already loaded
        if(is_loaded) {
            Serial.println("✅ Forest already loaded in memory");
            return;
        }
        
        // Safety check: Ensure forest exists
        if(root.empty()) {
            Serial.println("❌ No trees in forest to load!");
            return;
        }
        
        // Memory safety check
        size_t freeMemory = ESP.getFreeHeap();
        size_t minMemoryRequired = 20000; // 20KB minimum threshold
        if(freeMemory < minMemoryRequired) {
            Serial.printf("❌ Insufficient memory to load forest (need %d bytes, have %d)\n", 
                         minMemoryRequired, freeMemory);
            return;
        }
        
        unsigned long start = millis();
        
        // Try to load from single forest file first
        char filename[32];
        snprintf(filename, sizeof(filename), "/%s_forest.bin", this->model_name.c_str());
        
        if(SPIFFS.exists(filename)){ 
            // Load from single file (new optimized format)
            File file = SPIFFS.open(filename, FILE_READ);
            if(!file) {
                Serial.printf("❌ Failed to open forest file: %s\n", filename);
                return;
            }
            
            // Read forest header with error checking
            uint32_t magic;
            if(file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                Serial.println("❌ Failed to read magic number");
                file.close();
                return;
            }
            
            if(magic != 0x464F5253) { // "FORS" 
                Serial.println("❌ Invalid forest file format");
                file.close();
                return;
            }
            
            uint8_t treeCount;
            if(file.read((uint8_t*)&treeCount, sizeof(treeCount)) != sizeof(treeCount)) {
                Serial.println("❌ Failed to read tree count");
                file.close();
                return;
            }
            
            Serial.printf("Loading %d trees from forest file...\n", treeCount);
            
            // Read all trees with comprehensive error checking
            uint8_t successfullyLoaded = 0;
            for(uint8_t i = 0; i < treeCount; i++) {
                // Memory check during loading
                if(ESP.getFreeHeap() < 10000) { // 10KB safety buffer
                    Serial.printf("❌ Insufficient memory during tree loading (have %d bytes)\n", ESP.getFreeHeap());
                    break;
                }
                
                uint8_t treeIndex;
                if(file.read((uint8_t*)&treeIndex, sizeof(treeIndex)) != sizeof(treeIndex)) {
                    Serial.printf("❌ Failed to read tree index for tree %d\n", i);
                    break;
                }
                
                uint32_t nodeCount;
                if(file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                    Serial.printf("❌ Failed to read node count for tree %d\n", treeIndex);
                    break;
                }
                
                // Validate node count
                if(nodeCount == 0 || nodeCount > 2047) {
                    Serial.printf("❌ Invalid node count %d for tree %d\n", nodeCount, treeIndex);
                    // Skip this tree's data
                    file.seek(file.position() + nodeCount * sizeof(uint32_t));
                    continue;
                }
                
                // Find the corresponding tree in root vector
                bool treeFound = false;
                for(auto& tree : root) {
                    if(tree.index == treeIndex) {
                        // Memory check before loading this tree
                        size_t requiredMemory = nodeCount * sizeof(Tree_node);
                        if(ESP.getFreeHeap() < requiredMemory + 5000) { // 5KB safety buffer per tree
                            Serial.printf("❌ Insufficient memory to load tree %d (need %d bytes, have %d)\n", 
                                        treeIndex, requiredMemory, ESP.getFreeHeap());
                            file.close();
                            return;
                        }
                        
                        tree.nodes.clear();
                        tree.nodes.reserve(nodeCount);
                        
                        // Read all nodes for this tree with error checking
                        bool nodeReadSuccess = true;
                        for(uint32_t j = 0; j < nodeCount; j++) {
                            Tree_node node;
                            if(file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                Serial.printf("❌ Failed to read node %d for tree %d\n", j, treeIndex);
                                nodeReadSuccess = false;
                                break;
                            }
                            tree.nodes.push_back(node);
                        }
                        
                        if(nodeReadSuccess) {
                            tree.nodes.fit();
                            tree.isLoaded = true;
                            successfullyLoaded++;
                        } else {
                            // Clean up failed tree
                            tree.nodes.clear();
                            tree.nodes.fit();
                            tree.isLoaded = false;
                        }
                        treeFound = true;
                        break;
                    }
                }
                
                if(!treeFound) {
                    Serial.printf("⚠️ Tree %d not found in forest structure, skipping\n", treeIndex);
                    // Skip the node data for this tree
                    file.seek(file.position() + nodeCount * sizeof(uint32_t));
                }
            }
            
            file.close();
            
            if(successfullyLoaded == 0) {
                Serial.println("❌ No trees successfully loaded from forest file!");
                return;
            }
            
        } else {
            // Fallback to individual tree files (legacy format)
            Serial.println("Using legacy individual tree files...");
            uint8_t successfullyLoaded = 0;
            for (auto& tree : root) {
                if (!tree.isLoaded) {
                    // Memory check before loading each tree
                    if(ESP.getFreeHeap() < 15000) { // 15KB threshold per tree
                        Serial.printf("❌ Insufficient memory to load more trees (have %d bytes)\n", ESP.getFreeHeap());
                        break;
                    }
                    try {
                        tree.loadTree(this->model_name);
                        if(tree.isLoaded) successfullyLoaded++;
                    } catch (...) {
                        Serial.printf("❌ Exception loading tree %d\n", tree.index);
                        tree.isLoaded = false;
                    }
                }
            }
            
            if(successfullyLoaded == 0) {
                Serial.println("❌ No trees successfully loaded from individual files!");
                return;
            }
        }
        
        // Verify trees are actually loaded
        uint8_t loadedTrees = 0;
        uint32_t totalNodes = 0;
        for(const auto& tree : root) {
            if(tree.isLoaded && !tree.nodes.empty()) {
                loadedTrees++;
                totalNodes += tree.nodes.size();
            }
        }
        
        if(loadedTrees == 0) {
            Serial.println("❌ No trees successfully loaded!");
            is_loaded = false;
            return;
        }
        
        is_loaded = true;
        unsigned long end = millis();
        Serial.printf("✅ Loaded %d/%d trees (%d nodes) in %lu ms (RAM: %d bytes)\n", 
                     loadedTrees, root.size(), totalNodes, end - start, ESP.getFreeHeap());
    }

    // releaseForest: Release all trees from RAM into SPIFFS (optimized single-file approach)
    void releaseForest(){
        if(!is_loaded) {
            Serial.println("✅ Forest is not loaded in memory, nothing to release.");
            return;
        }
        memory_tracker.log("before release forest");
        
        // Count loaded trees
        uint8_t loadedCount = 0;
        uint32_t totalNodes = 0;
        for(auto& tree : root) {
            if (tree.isLoaded && !tree.nodes.empty()) {
                loadedCount++;
                totalNodes += tree.nodes.size();
            }
        }
        
        if(loadedCount == 0) {
            Serial.println("✅ No loaded trees to release");
            is_loaded = false;
            return;
        }
        
        // Check available SPIFFS space before writing
        size_t totalFS = SPIFFS.totalBytes();
        size_t usedFS = SPIFFS.usedBytes();
        size_t freeFS = totalFS - usedFS;
        size_t estimatedSize = totalNodes * sizeof(uint32_t) + 100; // nodes + headers
        
        if(freeFS < estimatedSize) {
            Serial.printf("❌ Insufficient SPIFFS space (need ~%d bytes, have %d)\n", estimatedSize, freeFS);
            return;
        }
        
        // Single file approach - write all trees to one file
        char filename[32];
        snprintf(filename, sizeof(filename), "/%s_forest.bin", this->model_name.c_str());
        
        unsigned long fileStart = millis();
        File file = SPIFFS.open(filename, FILE_WRITE);
        if (!file) {
            Serial.printf("❌ Failed to create forest file: %s\n", filename);
            return;
        }
        
        // Write forest header
        uint32_t magic = 0x464F5253; // "FORS" in hex (forest)
        if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
            Serial.println("❌ Failed to write magic number");
            file.close();
            SPIFFS.remove(filename);
            return;
        }
        
        if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
            Serial.println("❌ Failed to write tree count");
            file.close();
            SPIFFS.remove(filename);
            return;
        }
        
        unsigned long writeStart = millis();
        size_t totalBytes = 0;
        
        // Write all trees in sequence with error checking
        uint8_t savedCount = 0;
        for(auto& tree : root) {
            if (tree.isLoaded && tree.index != 255 && !tree.nodes.empty()) {
                // Write tree header
                if(file.write((uint8_t*)&tree.index, sizeof(tree.index)) != sizeof(tree.index)) {
                    Serial.printf("❌ Failed to write tree index %d\n", tree.index);
                    break;
                }
                
                uint32_t nodeCount = tree.nodes.size();
                if(file.write((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                    Serial.printf("❌ Failed to write node count for tree %d\n", tree.index);
                    break;
                }
                
                // Write all nodes with progress tracking
                bool writeSuccess = true;
                for (uint32_t i = 0; i < tree.nodes.size(); i++) {
                    const auto& node = tree.nodes[i];
                    if(file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                        Serial.printf("❌ Failed to write node %d for tree %d\n", i, tree.index);
                        writeSuccess = false;
                        break;
                    }
                    totalBytes += sizeof(node.packed_data);
                    
                    // Check for memory issues during write
                    if(ESP.getFreeHeap() < 5000) { // 5KB safety threshold
                        Serial.printf("⚠️ Low memory during write (tree %d, node %d)\n", tree.index, i);
                    }
                }
                
                if(!writeSuccess) {
                    Serial.printf("❌ Failed to save tree %d completely\n", tree.index);
                    break;
                }
                
                savedCount++;
            }
        }
        
        file.close();
        
        // Verify file was written correctly
        if(savedCount != loadedCount) {
            Serial.printf("❌ Save incomplete: %d/%d trees saved\n", savedCount, loadedCount);
            SPIFFS.remove(filename);
            return;
        }
        
        // Only clear trees from RAM after successful save
        uint8_t clearedCount = 0;
        for(auto& tree : root) {
            if (tree.isLoaded) {
                tree.nodes.clear();
                tree.nodes.fit();
                tree.isLoaded = false;
                clearedCount++;
            }
        }
        
        is_loaded = false;
        memory_tracker.log("aftef release forest");
        
        unsigned long end = millis();
        Serial.printf("✅ Released %d trees (%d bytes) to SPIFFS in %lu ms (RAM: %d bytes)\n", 
                     clearedCount, totalBytes, end - fileStart, ESP.getFreeHeap());
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
        unordered_map<uint8_t, uint32_t> tp, fp, fn, totalActual;
        uint32_t totalSamples = 0;
        uint32_t totalCorrect = 0;
        
        // Initialize counters for all actual labels
        for (uint8_t label=0; label < config.num_labels; label++) {
            tp[label] = 0;
            fp[label] = 0; 
            fn[label] = 0;
            totalActual[label] = 0;
        }
        // Serial.println("here 1");
        
        // Single pass over samples (using direct indexing)
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            // Serial.println("here 1.5");
            
            totalActual[actual]++;
            totalSamples++;
            
            if (pred == actual) {
                tp[actual]++;
                totalCorrect++;
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
        
        // Calculate overall accuracy (same for all labels in multi-class)
        float overallAccuracy = (totalSamples == 0) ? 0.0f : float(totalCorrect) / totalSamples;
        
        for (uint8_t label = 0; label < config.num_labels; label++) {
            uint32_t tpv = tp[label], fpv = fp[label], fnv = fn[label];
            
            float prec = (tpv + fpv == 0) ? 0.0f : float(tpv) / (tpv + fpv);
            float rec  = (tpv + fnv == 0) ? 0.0f : float(tpv) / (tpv + fnv);
            float f1   = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
            
            // Per-label accuracy: use overall accuracy (standard approach)
            // Alternative: per-class accuracy = (TP + TN) / Total, but requires calculating TN
            float acc = overallAccuracy;  // Same overall accuracy for all labels
            
            precisions.push_back(make_pair(label, prec));
            recalls.push_back(make_pair(label, rec));
            f1s.push_back(make_pair(label, f1));
            accuracies.push_back(make_pair(label, acc));
            
            // Serial.printf("Label %d: TP=%d, FP=%d, FN=%d, Prec=%.3f, Rec=%.3f, F1=%.3f, Acc=%.3f\n", 
            //             label, tpv, fpv, fnv, prec, rec, f1, acc);
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
        for (uint16_t i = 0; i < testSet.size(); i++) {
            const Rf_sample& sample = testSet[i];
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            // std::cout << sampleId << "  " << (int)pred << " - " << (int)sample.label << std::endl;
            Serial.printf("%d, %d, %d\n", i, pred, sample.label);
        }
        testSet.releaseData(true); // Release test set data after use
        releaseForest(); // Release all trees after prediction
    }
};

void setup() {
    Serial.begin(115200);  
    while (!Serial);       // <-- Waits for Serial monitor to connect (important for USB CDC)

    delay(2000);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }
    // manageSPIFFSFiles();
    delay(1000);
    long unsigned start_forest = millis();
    // const char* filename = "/walker_fall.bin";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "digit_data"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(filename, 42); // reproducible by default (can omit random_seed)

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    forest.MakeForest();

    // forest.training();

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

    // // check actual prediction time 
    b_vector<float> sample = MAKE_FLOAT_LIST(0,0.000454539,0,0,0,0.510392,0.145854,0,0.115446,0,0.00516406,0.0914579,0.565657,0.523204,0.315898,0.0548166,0,0.310198,0.0193819,0,0,0.0634356,0.45749,0.00122793,0.493418,0.314128,0.150056,0.106594,0.321845,0.0745179,0.282953,0.353358,0,0.254502,0.502515,0.000288011,0,0,0.0756328,0.00226037,0.382164,0.261311,0.300058,0.261635,0.313706,0,0.0501838,0.450812,0.0947562,0.000373078,0.00211045,0.0744771,0.462151,0.715595,0.269004,0.0449925,0,0,0.00212813,0.000589888,0.420681,0.0574298,0.0717421,0,0.313605,0.339293,0.0629904,0.0675315,0.0618258,0.069364,0.41181,0.223367,0.0892957,0.0317173,0.0412844,0.000333441,0.733433,0.035459,0.000471556,0.00492559,0.103231,0.255209,0.411744,0.154244,0.0670255,0,0.0747003,0.271415,0.740801,0.0413177,0.000545948,0.00293495,0.31086,0.000711829,0.000690576,0.00328563,0.0109791,0,0.00179087,0.05755,0.281221,0.0908081,0.139806,0.0358642,0.0303179,0.0455232,0.000940401,0.000496404,0.933685,0.0312803,0.108249,0.0307203,0.0946534,0.0618412,0.0974416,0.0649112,0.677713,0.00266646,0.0009506,0.0560812,0.492166,0.0329419,0.0117499,0.0216917,0.379698,0.0638361,0.344801,0.00247299,0.568132,0.00436328,0.00107975,0.0635284,0.379419,0.000722445,0.000700875,0.0521259,0.635661,0.068638,0.299062,0.0238965,0.00382694,0.00504611,0.163862,0.0285841);
    forest.loadForest();
    long unsigned start = micros();
    String pred = forest.predict(sample);
    long unsigned end = micros();
    Serial.printf("Prediction for sample took %u us. label %s.\n", end - start, pred.c_str());
    forest.releaseForest();


    // forest.visual_result(forest.test_data); // Optional visualization


    long unsigned end_forest = millis();
    Serial.printf("\nTotal time: %lu ms\n", end_forest - start_forest);
}

void loop() {
    manageSPIFFSFiles();
}