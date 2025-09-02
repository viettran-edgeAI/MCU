#include "Rf_components.h"
#define SUPPORT_LABEL_MAPPING

#define DEV_STAGE  // development stage - for testing and debugging

#ifdef DEV_STAGE
#define ENABLE_TEST_DATA 1
#else
#define ENABLE_TEST_DATA 0
#endif

#define RF_DEBUG_LEVEL 6 // 0: no debug, 1: error, 2: warning, 3: info, 4: debug, 5: verbose, 6: trace


using namespace mcu;

typedef enum Rf_training_flags : uint8_t{
    EARLY_STOP  = 0x00,        // early stop training if accuracy is not improving
    ACCURACY    = 0x01,          // calculate accuracy of the model
    PRECISION   = 0x02,          // calculate precision of the model
    RECALL      = 0x04,            // calculate recall of the model
    F1_SCORE    = 0x08          // calculate F1 score of the model
}Rf_training_flags;

class RandomForest{
public:
    String model_name;  // base_name

    Rf_data base_data;
    Rf_data train_data;
    #if ENABLE_TEST_DATA
    Rf_data test_data;
    #endif
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

    unordered_map<uint8_t, uint8_t> predictClass;   // for predicting class of a sample
    bool is_loaded = false;

public:
    RandomForest(){};
    RandomForest(const char* model_name) {
        this->model_name = String(model_name);

        // initial components
        memory_tracker.init();
        base.init(model_name); // Initialize base with the provided base name
        config.init(base.get_configFile());
        categorizer.init(base.get_ctgFile());
        random_generator.init(config.random_seed, true);
        node_predictor.init(base.get_nodePredictFile());

        // load resources
        config.loadConfig();  // load config file
        first_scan(base.get_dpFile());   // load data parameters into config
        categorizer.loadCategorizer(); // load categorizer
        node_predictor.loadPredictor(); // load node predictor

        // init base data (make a clone of base_data to avoid modifying original)
        cloneFile(base.get_baseFile().c_str(), base_data_file);
        base_data.init(base_data_file);
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
        if(config.use_validation()) validation_data.purgeData();

        // re_train node predictor after each training session
        node_predictor.re_train();
        config.releaseConfig();     // save best config after retraining    
    }

    void build_model(){
        // initialize data
        dataList.reserve(config.num_trees);
        train_data.init("/train_data.bin", config.num_features);
        test_data.init("/test_data.bin", config.num_features);
        if(config.use_validation()){
            validation_data.init("/valid_data.bin", config.num_features);
        }
        memory_tracker.log("forest init");

        // data splitting
        vector<pair<float, Rf_data*>> dest;
        dest.push_back(make_pair(config.train_ratio, &train_data));
        dest.push_back(make_pair(config.test_ratio, &test_data));
        if(config.use_validation()){
            dest.push_back(make_pair(config.valid_ratio, &validation_data));
        }
        splitData(base_data, dest);
        ClonesData();
        MakeForest();
    }

    void MakeForest(){
        // Clear any existing forest first
        clearForest();
        // We're building a fresh forest; ensure load state reflects that
        is_loaded = false;
        Serial.println("START MAKING FOREST...");

        // pre_allocate nessessary resources
        root.reserve(config.num_trees);
        queue_nodes.clear();
        // queue_nodes.fit();
        uint16_t estimatedNodes = node_predictor.estimate(config.min_split, config.max_depth) * 100 / node_predictor.accuracy;
        queue_nodes.reserve(min(120, estimatedNodes * node_predictor.peak_percent / 100)); // Conservative estimate
        node_data n_data(config.min_split, config.max_depth,0);

        Serial.print("building sub_tree: ");
        
        train_data.loadData();
        memory_tracker.log("after loading train data");
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
    }
  private:
    // ----------------------------------------------------------------------------------
    // Split data into training and testing sets. Dest data must be init() before called by this function
    void splitData(Rf_data& source, vector<pair<float, Rf_data*>>& dest) {
        Serial.println("<-- split data -->");
        if(dest.empty() || source.size() == 0) {
            Serial.println("Error: No data to split or destination is empty.");
            return;
        } 
        float total_ratio = 0.0f;
        for(auto& d : dest) {
            if(d.first <= 0.0f || d.first > 1.0f) {
                Serial.printf("Error: Invalid ratio %.2f. Must be in (0.0, 1.0].\n", d.first);
                return;
            }
            total_ratio += d.first;
            if(total_ratio > 1.0f) {
                Serial.println("Error: Total split ratios exceed 1.0.");
                return;
            }
        }

        size_t maxID = source.size();
        sampleID_set used(maxID);       // Track used sample IDs to avoid overlap
        sampleID_set sink_IDs(maxID);   // sample IDs for each dest Rf_data

        for(uint8_t i=0; i< dest.size(); i++){
            sink_IDs.clear();
            uint16_t sink_require = static_cast<uint16_t>(static_cast<float>(maxID) * dest[i].first);
            while(sink_IDs.size() < sink_require) {
                uint16_t sampleId = static_cast<uint16_t>(random_generator.bounded(static_cast<uint32_t>(maxID)));
                if (!used.contains(sampleId)) {
                    sink_IDs.push_back(sampleId);
                    used.push_back(sampleId);
                }
            }
            dest[i].second->loadData(source, sink_IDs);
            dest[i].second->releaseData(false); // Write to binary SPIFFS, clear RAM
            memory_tracker.log("after splitting data");
        }
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
                    vector<uint16_t> arr(numSample,0);
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
                uint64_t h = random_generator.hashIDVector(treeDataset);
                if (seen_hashes.find(h) == seen_hashes.end()) {
                    seen_hashes.insert(h);
                    break; // unique, accept
                }
                nonce++;
                if (nonce > 8) {
                    auto temp_vec = treeDataset;  // Copy current state
                    treeDataset.clear();
                    
                    // Re-add samples with slight modifications
                    for (uint16_t k = 0; k < min(5, (int)temp_vec.size()); ++k) {
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
        
        if (config.use_validation()) {
            if (config.valid_ratio > 0.0f) {
                // Use explicit valid_ratio if set
                expected_valid_samples = (lowest_distribution / 100.0f) * numSamples * config.valid_ratio;
            } else {
                // Assume remaining samples after training are split 50/50 between test and validation
                float remaining_ratio = 1.0f - config.train_ratio;
                expected_valid_samples = (lowest_distribution / 100.0f) * numSamples * (remaining_ratio * 0.5f);
            }
            
            // if (expected_valid_samples < 5.0f) {  // Need at least 5 samples per minority class for meaningful validation
            //     config.use_validation() = false;
            //     Serial.printf("⚖️ Setting use_validation to false due to low sample count in validation set (%.1f expected samples for minority class).\n", expected_valid_samples);
            //     config.train_ratio = 0.7f; // Adjust train ratio to compensate
            // }
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
        config.max_depth = (min_maxDepth + max_maxDepth) / 2 + 2;

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
    // Mark forest as not loaded to force re-load after rebuilds
    is_loaded = false;
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

    template<typename T>
    uint8_t predClassSample(const T& sample_or_features){
        int16_t totalPredict = 0;
        predictClass.clear();
        
        // Use streaming prediction 
        for(auto& tree : root){
            uint8_t predict = tree.predictSample(sample_or_features); 
            if(predict < config.num_labels){
                predictClass[predict]++;
                totalPredict++;
            }
        }
        if(predictClass.empty() || totalPredict == 0) {
            return 255; // Unknown class
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
        if(certainty < config.unity_threshold) {
            return 255; // Unknown class if certainty is too low
        }
        return mostPredict;
    }
    // Helper function: evaluate the entire forest using OOB (Out-of-Bag) score
    // Iterates over all samples in training data and evaluates using trees that didn't see each sample
    float get_oob_score(){
        Serial.println("Get OOB score... ");

        // Check if we have trained trees and data
        if(dataList.empty() || root.empty()){
            Serial.println("❌ No trained trees available for OOB evaluation!");
            return 0.0f;
        }

        // Determine chunk size for memory-efficient processing
        uint16_t buffer_chunk = (train_data.size() + 3 ) / 4;
        if(buffer_chunk < 130) buffer_chunk = 130;
        if(buffer_chunk > train_data.size()) buffer_chunk = train_data.size();

        // Pre-allocate evaluation resources
        Rf_data train_samples_buffer;
        sampleID_set sampleIDs_bag;
        b_vector<uint8_t, SMALL> activeTrees;
        unordered_map<uint8_t, uint8_t> oobPredictClass;

        train_samples_buffer.init(config.num_features); // Temporary init without file
        train_samples_buffer.reserve(buffer_chunk);
        activeTrees.reserve(config.num_trees);
        oobPredictClass.reserve(config.num_labels);

        // Initialize matrix score calculator for OOB
        Rf_matrix_score oob_scorer(config.num_labels, static_cast<uint8_t>(config.train_flag));

        // Load forest into memory for evaluation
        loadForest();
        memory_tracker.log("get OOB score");

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

                // Update OOB metrics using matrix scorer
                oob_scorer.update_prediction(actualLabel, oobPredictedLabel);
            }
        }
        train_samples_buffer.purgeData();

        // Calculate and return OOB score
        return oob_scorer.calculate_score("OOB");
    }

    // Helper function: evaluate the entire forest using validation score
    // Evaluates using validation dataset if available
    float get_valid_score(){
        Serial.println("Get validation score... ");

        // Check if validation is enabled and we have trained trees
        if(!config.use_validation()){
            Serial.println("❌ Validation is not enabled!");
            return 0.0f;
        }

        if(root.empty()){
            Serial.println("❌ No trained trees available for validation evaluation!");
            return 0.0f;
        }

        // Initialize matrix score calculator for validation
        Rf_matrix_score valid_scorer(config.num_labels, static_cast<uint8_t>(config.train_flag));

        // Load forest into memory for evaluation
        loadForest();
        memory_tracker.log("get validation score");

        // Validation evaluation
        Serial.println("Evaluating on validation set...");
        validation_data.loadData();
        
        for(uint16_t i = 0; i < validation_data.size(); i++){
            const Rf_sample& sampleData = validation_data[i];
            uint8_t actualLabel = sampleData.label;

            unordered_map<uint8_t, uint8_t> validPredictClass;
            uint16_t validTotalPredict = 0;

            // Use all trees for validation prediction
            for(uint8_t t = 0; t < config.num_trees && t < root.size(); t++){
                uint8_t predict = root[t].predictSample(sampleData);
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

            // Update validation metrics using matrix scorer
            valid_scorer.update_prediction(actualLabel, validPredictedLabel);
        }
        validation_data.releaseData(true);

        // Calculate and return validation score
        return valid_scorer.calculate_score("Validation");
    }

    // Performs k-fold cross validation on train_data
    // train_data -> base_data, fold_train_data ~ train_data, fold_valid_data ~ validation_data
    float get_cross_validation_score(){
        Serial.println("Get k-fold cross validation score... ");

        if(config.k_fold < 2 || config.k_fold > 10){
            Serial.println("❌ Invalid k_fold value! Must be between 2 and 10.");
            return 0.0f;
        }

        uint16_t totalSamples = base_data.size();
        if(totalSamples < config.k_fold * config.num_labels * 2){
            Serial.println("❌ Not enough samples for k-fold cross validation!");
            return 0.0f;
        }
        Rf_matrix_score scorer(config.num_labels, static_cast<uint8_t>(config.train_flag));

        uint16_t foldSize = totalSamples / config.k_fold;
        float k_fold_score = 0.0f;
        // Perform k-fold cross validation
        for(uint8_t fold = 0; fold < config.k_fold; fold++){
            scorer.reset();
            // Create sample ID sets for current fold
            sampleID_set fold_valid_sampleIDs(fold * foldSize, fold * foldSize + foldSize);
            sampleID_set fold_train_sampleIDs(totalSamples-1);
            fold_valid_sampleIDs.fill();
            fold_train_sampleIDs.fill();

            fold_train_sampleIDs -= fold_valid_sampleIDs;
            // create train_data and valid data for current fold 
            validation_data.loadData(base_data, fold_valid_sampleIDs);
            validation_data.releaseData(false);
            train_data.loadData(base_data, fold_train_sampleIDs);
            train_data.releaseData(false); 
            
            ClonesData();
            MakeForest();
        
            validation_data.loadData();
            loadForest();
            // Process all samples
            for(uint16_t i = 0; i < validation_data.size(); i++){
                const Rf_sample& sample = validation_data[i];
                uint8_t actual = sample.label;
                uint8_t pred = predClassSample(static_cast<Rf_sample&>(sample));
            
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            validation_data.releaseData();
            releaseForest();
            
            return scorer.calculate_score("K-fold score");
        }
        k_fold_score /= config.k_fold;
        Serial.printf("✅ K-fold cross validation completed.\n");

        // Calculate and return k-fold score
        return k_fold_score;
    }


    // Helper function: evaluate the entire forest using combined OOB and validation scores
    float get_training_evaluation_index(){
        if(config.training_score == Rf_training_score::OOB_SCORE){
            return get_oob_score();
        }
        if(config.training_score == Rf_training_score::VALID_SCORE){
            return get_valid_score();
        } 
        // Default fallback
        return get_oob_score();
    }

    public:
    // -----------------------------------------------------------------------------------
    // Memory-Efficient Grid Search Training Function
    void training(){
        Serial.println("Starting training with memory-efficient grid search...");
        int total_combinations = config.min_split_range.size() * config.max_depth_range.size();
        Serial.printf("Total combinations: %d\n", total_combinations);

        int loop_count = 0;
        int best_min_split = config.min_split;
        int best_max_depth = config.max_depth;

        float best_score = get_training_evaluation_index();
        Serial.printf("training score : %s\n", 
                      (config.training_score == Rf_training_score::OOB_SCORE) ? "OOB" : 
                      (config.training_score == Rf_training_score::VALID_SCORE) ? "VALID" : 
                      (config.training_score == Rf_training_score::K_FOLD_SCORE) ? "K-FOLD" : "UNKNOWN");

        if(config.training_score == Rf_training_score::K_FOLD_SCORE){
            // convert train_data to base_data
            base_data = train_data; 
            // init validation_data if not yet
            if(validation_data.isProperlyInitialized() == false){
                validation_data.init("/valid_data.bin", config.num_features);
            }
        }

        for(auto min_split : config.min_split_range){
            for(auto max_depth : config.max_depth_range){
                config.min_split = min_split;
                config.max_depth = max_depth;
                float score;
                if(config.training_score == Rf_training_score::K_FOLD_SCORE){
                    // convert train_data to base_data
                    score = get_cross_validation_score();
                }else{
                    MakeForest();
                    score = get_training_evaluation_index();
                }
                Serial.printf("Score with min_split=%d, max_depth=%d: %.3f\n", 
                              min_split, max_depth, score);
                if(score > best_score){
                    best_score = score;
                    best_min_split = min_split;
                    best_max_depth = max_depth;
                    config.result_score = best_score;
                    //save best forest
                    Serial.printf("New best score: %.3f with min_split=%d, max_depth=%d\n", 
                                  best_score, min_split, max_depth);
                    if(config.training_score != Rf_training_score::K_FOLD_SCORE){
                        // rebuild model with full train_data 
                        train_data = base_data;     // restore train_data
                        ClonesData();
                        MakeForest();
                    }
                        
                    // Save the best forest to SPIFFS
                    releaseForest(); // Release current trees from RAM
                }
                if(loop_count++ > 2) return;
            }
        }
        // Set config to best found parameters
        config.min_split = best_min_split;
        config.max_depth = best_max_depth;
        Serial.printf("Best parameters: min_split=%d, max_depth=%d with score=%.3f\n", 
                      best_min_split, best_max_depth, best_score);
    }

    // overwrite training_flag
    void set_training_flag(Rf_training_flags flag) {
        config.train_flag = flag;
    }

    //  combined training_flag with user input
    void add_training_flag(Rf_training_flags flag) {
        config.train_flag |= flag;
    }

    // set training_score
    void set_training_score(Rf_training_score score) {
        config.training_score = score;
        if(score == Rf_training_score::VALID_SCORE) {
            if(config.num_samples / config.num_labels > 150){
                config.train_ratio = 0.7f;
                config.test_ratio = 0.15f;
                config.valid_ratio = 0.15f;
            }else{
                config.train_ratio = 0.6f;
                config.test_ratio = 0.2f;
                config.valid_ratio = 0.2f;
            }
        } 
    }

    void set_ramdom_seed(uint32_t seed) {
        random_generator.seed(seed);
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
        
        // Try to load from unified forest file first (using base method for consistency)
        String unifiedFilename = base.get_unifiedModelFile();
        
        if(SPIFFS.exists(unifiedFilename.c_str())){ 
            // Load from unified file (optimized format)
            File file = SPIFFS.open(unifiedFilename.c_str(), FILE_READ);
            if(!file) {
                Serial.printf("❌ Failed to open unified forest file: %s\n", unifiedFilename.c_str());
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
                Serial.println("❌ No trees successfully loaded from unified forest file!");
                return;
            }
            
            Serial.printf("✅ Loaded %d trees from unified format\n", successfullyLoaded);
            
        } else {
            // Fallback to individual tree files (legacy format)
            Serial.println("📁 Unified format not found, using individual tree files...");
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
            
            Serial.printf("✅ Loaded %d trees from individual format\n", successfullyLoaded);
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
        String formatUsed = SPIFFS.exists(base.get_unifiedModelFile().c_str()) ? "unified" : "individual";
        Serial.printf("✅ Forest loaded (%s format): %d/%d trees (%d nodes) in %lu ms (RAM: %d bytes)\n", 
                     formatUsed.c_str(), loadedTrees, root.size(), totalNodes, end - start, ESP.getFreeHeap());
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
        
        // Single file approach - write all trees to unified forest file
        String unifiedFilename = base.get_unifiedModelFile();
        
        unsigned long fileStart = millis();
        File file = SPIFFS.open(unifiedFilename.c_str(), FILE_WRITE);
        if (!file) {
            Serial.printf("❌ Failed to create unified forest file: %s\n", unifiedFilename.c_str());
            return;
        }
        
        // Write forest header
        uint32_t magic = 0x464F5253; // "FORS" in hex (forest)
        if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
            Serial.println("❌ Failed to write magic number");
            file.close();
            SPIFFS.remove(unifiedFilename.c_str());
            return;
        }
        
        if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
            Serial.println("❌ Failed to write tree count");
            file.close();
            SPIFFS.remove(unifiedFilename.c_str());
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
            SPIFFS.remove(unifiedFilename.c_str());
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
        memory_tracker.log("after release forest");
        
        unsigned long end = millis();
        Serial.printf("✅ Released %d trees to unified format (%d bytes) in %lu ms (RAM: %d bytes)\n", 
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
        
        // Initialize matrix score calculator
        Rf_matrix_score scorer(config.num_labels, 0xFF); // Use all flags for detailed metrics
        
        // Single pass over samples (using direct indexing)
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            // Update metrics using matrix scorer
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        // Build result vectors using matrix scorer
        b_vector<b_vector<pair<uint8_t, float>>> result;
        result.push_back(scorer.get_precisions());  // 0: precisions
        result.push_back(scorer.get_recalls());     // 1: recalls
        result.push_back(scorer.get_f1_scores());   // 2: F1 scores
        result.push_back(scorer.get_accuracies());  // 3: accuracies

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
        // Create a matrix scorer with the specified flags
        Rf_matrix_score scorer(config.num_labels, static_cast<uint8_t>(flags));
        
        if(!data.isLoaded) data.loadData();
        loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        releaseForest();
        
        return scorer.calculate_score("Predict2");
    }

    float precision(Rf_data& data) {
        // Create a temporary scorer to calculate precision
        Rf_matrix_score scorer(config.num_labels, 0x02); // PRECISION flag only
        
        if(!data.isLoaded) data.loadData();
        loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        releaseForest();
        
        return scorer.calculate_score("Precision");
    }

    float recall(Rf_data& data) {
        // Create a temporary scorer to calculate recall
        Rf_matrix_score scorer(config.num_labels, 0x04); // RECALL flag only
        
        if(!data.isLoaded) data.loadData();
        loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        releaseForest();
        
        return scorer.calculate_score("Recall");
    }

    float f1_score(Rf_data& data) {
        // Create a temporary scorer to calculate F1-score
        Rf_matrix_score scorer(config.num_labels, 0x08); // F1_SCORE flag only
        
        if(!data.isLoaded) data.loadData();
        loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        releaseForest();
        
        return scorer.calculate_score("F1-Score");
    }

    float accuracy(Rf_data& data) {
        // Create a temporary scorer to calculate accuracy
        Rf_matrix_score scorer(config.num_labels, 0x01); // ACCURACY flag only
        
        if(!data.isLoaded) data.loadData();
        loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        releaseForest();
        
        return scorer.calculate_score("Accuracy");
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
    RandomForest forest = RandomForest(filename); // reproducible by default (can omit random_seed)
    // forest.set_training_score(Rf_training_score::K_FOLD_SCORE);

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    forest.build_model();

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