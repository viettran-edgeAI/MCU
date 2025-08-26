#include "Rf_components.h"

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
    Rf_categorizer categorizer; 
    Rf_memory_logger memory_tracker;
    Rf_node_predictor  node_predictor; // Node predictor number of nodes required for a tree based on min_split and max_depth
private:
    vector<Rf_tree, SMALL> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<ID_vector<uint16_t,2>, SMALL> dataList; // each ID_vector stores sample IDs of a sub_dataset, reference to index of allSamples vector in train_data
    b_vector<NodeToBuild, SMALL, 20> queue_nodes; // Queue for breadth-first tree building

    bool optimal_mode = false;  
    bool is_loaded = false;
public:

    RandomForest(){};
    RandomForest(const char* base_name){
        model_name = String(base_name);

        // initial components
        memory_tracker.init();
        base.init(base_name); // Initialize base with the provided base name
        config.init(base.get_configFile());
        categorizer.init(base.get_ctgFile());
        node_predictor.init(base.get_nodePredictFile());

        // load resources
        config.loadConfig();  // load config file
        first_scan(base.get_dpFile());   // load data parameters into config
        categorizer.loadCategorizer(); // load categorizer
        node_predictor.loadPredictor(); // load node predictor

        // load base data 
        String baseData = "/base_data.bin";         // make a copy of base data to avoid accidental deletion
        cloneFile(base.get_baseFile().c_str(), baseData.c_str());
        base_data.filename = baseData;
        
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
        train_data.loadData();
        
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
            uint16_t sampleId = static_cast<uint16_t>(esp_random() % maxID);
            train_sampleIDs.push_back(sampleId); 
        }
        train_data.allSamples = base_data.loadData(train_sampleIDs); // Load only training samples
        train_data.isLoaded = true;
        train_data.filename = "/train_data.bin";
        train_data.releaseData(false); // Write to binary SPIFFS, clear RAM
        Serial.println("Train data loaded and released.");
        // Serial.print("all samples: ");
        // for(auto id : train_sampleIDs) Serial.printf("%d, ", id);
        // Serial.println();
        
        // create test_data
        sampleID_set test_sampleIDs(maxID);
        while(test_sampleIDs.size() < testSize) {
            uint16_t i = static_cast<uint16_t>(esp_random() % maxID);
            if (!train_sampleIDs.contains(i)) {
                test_sampleIDs.push_back(i);
            }
        }
        test_data.allSamples = base_data.loadData(test_sampleIDs); // Load only testing samples
        test_data.isLoaded = true;
        test_data.filename = "/test_data.bin";
        test_data.releaseData(false); // Write to binary SPIFFS, clear RAM
        Serial.println("Test data loaded and released.");

        // create validation_data
        if(config.use_validation){
            sampleID_set validation_sampleIDs(maxID);
            while(validation_sampleIDs.size() < validationSize) {
                uint16_t i = static_cast<uint16_t>(esp_random() % maxID);
                if (!train_sampleIDs.contains(i) && !test_sampleIDs.contains(i)) {
                    validation_sampleIDs.push_back(i);
                }
            }
            validation_data.allSamples = base_data.loadData(validation_sampleIDs); // Load only validation samples
            validation_data.isLoaded = true;
            validation_data.filename = "/valid_data.bin";
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
        // Serial.printf("Total samples: %d, Sub-sample size: %d\n", numSample, numSubSample);

        // Seed random number generator with current time + some entropy
        uint32_t seed = millis() ^ ESP.getCycleCount() ^ esp_random();
        srand(seed);

        Serial.println("creating dataset for sub-tree : ");
        for (uint8_t i = 0; i < config.num_trees; i++) {
            Serial.printf("%d, ", i);
            // Create a new ID_vector for this tree
            ID_vector<uint16_t,2> treeDataset;
            treeDataset.reserve(numSubSample);

            // Add some randomness per tree to further diversify
            uint32_t tree_seed = seed + i * 1000 + millis();
            
            uint32_t attempts = 0;
            while(treeDataset.size() < numSubSample && attempts < numSubSample * 10) {
                // Use multiple sources of randomness
                uint32_t rand_val = (esp_random() ^ (tree_seed * (attempts + 1))) + ESP.getCycleCount();
                uint16_t idx = static_cast<uint16_t>(rand_val % numSample);
                if(!config.use_boostrap) {
                    // Without replacement, ensure unique samples
                    if(treeDataset.contains(idx)) {
                        attempts++;
                        continue;
                    }
                }
                treeDataset.push_back(idx);
                attempts++;
            }
            dataList.push_back(std::move(treeDataset));
            
            // Serial.printf("dataset size: %d\n", dataList[i].size());
            // Serial.printf("unique IDs: %d\n", dataList[i].unique_size());
            // Serial.print("sample IDs: ");
            // for(auto id : dataList[i]) Serial.printf("%d, ", id);
            // Serial.println();
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
        
        void analyzeSamples(const ID_vector<uint16_t,2>& sampleIDs, uint8_t numLabels, const Rf_data& data) {
            totalSamples = sampleIDs.size();
            uint16_t maxCount = 0;
            
            // Single pass through sample IDs for efficiency
            for (const auto& sampleID : sampleIDs) {
                if (sampleID < data.size()) {  // Direct index bounds check
                    uint8_t label = data.allSamples[sampleID].label;  // Direct indexing
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
    SplitInfo findBestSplit(const ID_vector<uint16_t,2>& sampleIDs, 
                                const unordered_set<uint16_t>& selectedFeatures, bool use_Gini, uint8_t numLabels) {
        SplitInfo bestSplit;
        uint32_t totalSamples = sampleIDs.size();
        if (totalSamples < 2) return bestSplit; // Cannot split less than 2 samples

        // Calculate base impurity using direct indexing
        vector<uint16_t> baseLabelCounts(numLabels, 0);
        for (const auto& sampleID : sampleIDs) {
            if (sampleID < train_data.size() && train_data.allSamples[sampleID].label < numLabels) {
                baseLabelCounts[train_data.allSamples[sampleID].label]++;
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

            // Build contingency table using direct indexing
            for (const auto& sampleID : sampleIDs) {
                if (sampleID < train_data.size()) {  // Direct index bounds check
                    const Rf_sample& sample = train_data.allSamples[sampleID];  // Direct indexing
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
    void buildTree(Rf_tree& tree, ID_vector<uint16_t,2>& sampleIDs) {
        tree.nodes.clear();
        if (sampleIDs.empty()) return;

        uint32_t initialRAM = ESP.getFreeHeap();
    
        // Create root node and initial sample ID list (all samples from the ID_vector)
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);
        
        // Use the ID_vector directly for root node - no need to extract IDs
        ID_vector<uint16_t,2> rootSampleIDs = sampleIDs; // Copy all sample IDs and move to root nod
        queue_nodes.push_back(NodeToBuild(0, std::move(rootSampleIDs), 0));
        
        // Process nodes breadth-first with periodic cleanup
        while (!queue_nodes.empty()) {
            NodeToBuild current = std::move(queue_nodes.front());
            queue_nodes.erase(0); // Remove first element
            
            // Analyze node samples efficiently using direct indexing
            NodeStats stats(config.num_labels);
            stats.analyzeSamples(current.sampleIDs, config.num_labels, train_data);
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
            // Use memory-efficient split finding with train_data.allSamples
            SplitInfo bestSplit = findBestSplit(current.sampleIDs, 
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
            ID_vector<uint16_t,2> leftSampleIDs, rightSampleIDs;
            // Pre-estimate split sizes to avoid reallocations
            uint16_t estimatedLeftSize = current.sampleIDs.size() / 2;
            uint16_t estimatedRightSize = current.sampleIDs.size() - estimatedLeftSize;
            leftSampleIDs.reserve(estimatedLeftSize);
            rightSampleIDs.reserve(estimatedRightSize);
            
            for (const auto& sampleID : current.sampleIDs) {
                if (sampleID < train_data.size()) {  // Direct index bounds check
                    if (train_data.allSamples[sampleID].features[bestSplit.featureID] <= bestSplit.threshold) {
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
        sample_set train_samples_buffer;
        sampleID_set sampleIDs_bag;
        b_vector<uint8_t, SMALL> activeTrees;
        unordered_map<uint8_t, uint8_t> oobPredictClass;

        train_samples_buffer.reserve(buffer_chunk);
        activeTrees.reserve(config.num_trees);
        oobPredictClass.reserve(config.num_labels);

        // Initialize confusion matrix components for OOB and validation
        uint8_t oobPredictVotes[config.num_labels];
        uint8_t validPredictVotes[config.num_labels];
        uint16_t oob_tp[config.num_labels];
        uint16_t oob_fp[config.num_labels];
        uint16_t oob_fn[config.num_labels];
        uint16_t valid_tp[config.num_labels];
        uint16_t valid_fp[config.num_labels];
        uint16_t valid_fn[config.num_labels];

        // Initialize arrays
        memset(oob_tp, 0, sizeof(oob_tp));
        memset(oob_fp, 0, sizeof(oob_fp));
        memset(oob_fn, 0, sizeof(oob_fn));
        memset(valid_tp, 0, sizeof(valid_tp));
        memset(valid_fp, 0, sizeof(valid_fp));
        memset(valid_fn, 0, sizeof(valid_fn));

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
            train_samples_buffer = train_data.loadData(sampleIDs_bag);
            if(train_samples_buffer.empty()){
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

            // Clean up chunk resources
            train_samples_buffer.clear();
            train_samples_buffer.fit();
        }

        // Validation evaluation if enabled
        if(config.use_validation){
            Serial.println("Evaluating on validation set...");
            validation_data.loadData();
            
            for(uint16_t i = 0; i < validation_data.allSamples.size(); i++){
                const Rf_sample& sampleData = validation_data.allSamples[i];
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

    void loadForest(){
        unsigned long start = millis();
        
        // Try to load from single forest file first
        char filename[32];
        snprintf(filename, sizeof(filename), "/%s_forest.bin", this->model_name.c_str());
        
        if(SPIFFS.exists(filename) && !is_loaded){ 
            // Load from single file (new optimized format)
            File file = SPIFFS.open(filename, FILE_READ);
            if(!file) {
                Serial.printf("❌ Failed to open forest file: %s\n", filename);
                return;
            }
            
            // Read forest header
            uint32_t magic;
            file.read((uint8_t*)&magic, sizeof(magic));
            if(magic != 0x464F5253) { // "FORS" 
                Serial.println("❌ Invalid forest file format");
                file.close();
                return;
            }
            
            uint8_t treeCount;
            file.read((uint8_t*)&treeCount, sizeof(treeCount));
            Serial.printf("Loading %d trees from forest file...\n", treeCount);
            
            // Read all trees
            for(uint8_t i = 0; i < treeCount; i++) {
                uint8_t treeIndex;
                file.read((uint8_t*)&treeIndex, sizeof(treeIndex));
                
                uint32_t nodeCount;
                file.read((uint8_t*)&nodeCount, sizeof(nodeCount));
                
                // Find the corresponding tree in root vector
                for(auto& tree : root) {
                    if(tree.index == treeIndex) {
                        tree.nodes.clear();
                        tree.nodes.reserve(nodeCount);
                        
                        // Read all nodes
                        for(uint32_t j = 0; j < nodeCount; j++) {
                            Tree_node node;
                            file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data));
                            tree.nodes.push_back(node);
                        }
                        
                        tree.isLoaded = true;
                        break;
                    }
                }
            }
            
            file.close();
        } else {
            // Fallback to individual tree files (legacy format)
            for (auto& tree : root) {
                if (!tree.isLoaded) {
                    tree.loadTree(this->model_name);
                }
            }
        }
        is_loaded = true;
        unsigned long end = millis();
        Serial.printf("Loaded forest in %lu ms\n", end - start);
    }

    // releaseForest: Release all trees from RAM into SPIFFS (optimized single-file approach)
    void releaseForest(){
        if(!is_loaded) {
            Serial.println("Forest is not loaded in memory, nothing to release.");
            return;
        }
        
        // Count loaded trees
        uint8_t loadedCount = 0;
        for(auto& tree : root) {
            if (tree.isLoaded) loadedCount++;
        }
        
        if(loadedCount == 0) {
            Serial.println("No loaded trees to release");
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
        file.write((uint8_t*)&magic, sizeof(magic));
        file.write((uint8_t*)&loadedCount, sizeof(loadedCount));
        
        unsigned long writeStart = millis();
        size_t totalBytes = 0;
        
        // Write all trees in sequence
        uint8_t savedCount = 0;
        for(auto& tree : root) {
            if (tree.isLoaded && tree.index != 255 && !tree.nodes.empty()) {
                // Write tree header
                file.write((uint8_t*)&tree.index, sizeof(tree.index));
                uint32_t nodeCount = tree.nodes.size();
                file.write((uint8_t*)&nodeCount, sizeof(nodeCount));
                
                // Write all nodes
                for (const auto& node : tree.nodes) {
                    file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data));
                    totalBytes += sizeof(node.packed_data);
                }
                
                // Clear from RAM
                tree.nodes.clear();
                tree.nodes.fit();
                tree.isLoaded = false;
                savedCount++;
            }
        }
        
        file.close();
        is_loaded = false;
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
        
        // Single pass over samples (using direct indexing)
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data.allSamples[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(const_cast<Rf_sample&>(sample));
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
            const Rf_sample& sample = testSet.allSamples[i];
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
    manageSPIFFSFiles();
    delay(1000);

    // const char* filename = "/walker_fall.bin";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "digit_data"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(filename);

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    forest.MakeForest();

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
    long unsigned start = micros();
    String pred = forest.predict(sample);
    long unsigned end = micros();
    Serial.printf("Prediction for sample took %u us.\n", end - start);


    // forest.visual_result(forest.test_data); // Optional visualization
}

void loop() {
    manageSPIFFSFiles();
}