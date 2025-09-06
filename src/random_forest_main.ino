#define DEV_STAGE         // development stage - enable test_data 
#define RF_DEBUG_LEVEL 2

#include "Rf_components.h"
#define SUPPORT_LABEL_MAPPING

using namespace mcu;

class RandomForest{
public:
    String model_name;  // base_name

    Rf_data base_data;
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    Rf_base base;
    Rf_config config;
    Rf_logger logger;
    Rf_random random_generator;
    Rf_categorizer categorizer; 
    Rf_node_predictor node_predictor; // Node predictor number of nodes required for a tree based on min_split and max_depth

private:
    Rf_tree_container forest_container; // Tree container managing all trees and forest state
    b_vector<ID_vector<uint16_t,2>, SMALL> dataList; // each ID_vector stores sample IDs of a sub_dataset, reference to index of allSamples vector in train_data

    unordered_map<uint8_t, uint8_t> predictClass;   // for predicting class of a sample
    bool optimal_mode = true;

public:
    RandomForest(){};
    RandomForest(const char* model_name) {
        this->model_name = String(model_name);

        // initial components
        logger.init(model_name);
        base.init(model_name); // Initialize base with the provided base name
        config.init(base.get_configFile());
        categorizer.init(base.get_ctgFile());
        random_generator.init(config.random_seed, true);
        node_predictor.init(base.get_nodePredictFile());

        // load resources
        config.loadConfig();  // load config file
        categorizer.loadCategorizer(); // load categorizer
        node_predictor.loadPredictor(); // load node predictor
        forest_container.init(this->model_name, config.num_trees, config.num_labels, base, logger);

        // init base data (make a clone of base_data to avoid modifying original)
        cloneFile(base.get_baseFile().c_str(), base_data_file);
        base_data.init(base_data_file);
        predictClass.reserve(config.num_labels);
    }
    
    // Enhanced destructor
    ~RandomForest(){
        Serial.println("🧹 Cleaning files... ");

        // clear all Rf_data
        base_data.purgeData();
        train_data.purgeData();
        test_data.purgeData();
        validation_data.purgeData();

        // re_train node predictor after each training session
        node_predictor.re_train(); 
    }

    void build_model(){
        // initialize data
        dataList.reserve(config.num_trees);
        train_data.init("/train_data.bin", config.num_features);
        test_data.init("/test_data.bin", config.num_features);
        if(config.use_validation()){
            validation_data.init("/valid_data.bin", config.num_features);
        }
        logger.m_log("forest init");

        // data splitting
        vector<pair<float, Rf_data*>> dest;
        dest.push_back(make_pair(config.train_ratio, &train_data));
        dest.push_back(make_pair(config.test_ratio, &test_data));
        if(config.use_validation()){
            dest.push_back(make_pair(config.valid_ratio, &validation_data));
        }
        size_t a1 = logger.drop_anchor();
        splitData(base_data, dest);
        size_t a2 = logger.drop_anchor();
        logger.t_log("split time", a1, a2);
        ClonesData();
        size_t a3 = logger.drop_anchor();
        logger.t_log("clones time", a2, a3);
        MakeForest();
        size_t a4 = logger.drop_anchor();
        logger.t_log("make forest time", a3, a4);
    }

    void set_optimal_mode(bool optimal){
        this->optimal_mode = optimal;
    }

private:
    void MakeForest(){
        // Clear any existing forest first
        forest_container.clearForest();
        Serial.println("START MAKING FOREST...");

        // Get queue_nodes reference from container
        auto& queue_nodes = forest_container.getQueueNodes();
        
        // pre_allocate necessary resources
        queue_nodes.clear();
        // queue_nodes.fit();
        uint16_t estimated_nodes = node_predictor.estimate(config.min_split, config.max_depth) * 100 / node_predictor.accuracy;
        uint16_t peak_nodes = min(120, estimated_nodes * node_predictor.peak_percent / 100);
        queue_nodes.reserve(peak_nodes); // Conservative estimate
        node_data n_data(config.min_split, config.max_depth,0);

        Serial.print("building sub_tree: ");
        
        train_data.loadData();
        logger.m_log("after loading train data");
        for(uint8_t i = 0; i < config.num_trees; i++){
            Serial.printf("%d, ", i);
            Rf_tree tree(i);
            tree.nodes.reserve(estimated_nodes); // Reserve memory for nodes based on prediction
            queue_nodes.clear(); // Clear queue for each tree

            buildTree(tree, dataList[i], queue_nodes);
            n_data.total_nodes += tree.countNodes();
            tree.isLoaded = true; 
            tree.releaseTree(this->model_name); // Save tree to SPIFFS
            forest_container.add_tree(std::move(tree));
        }
        
        // Release train_data after all trees are built
        train_data.releaseData(); // Keep metadata but release sample data
        n_data.total_nodes /= config.num_trees; // Average nodes per tree
        node_predictor.buffer.push_back(n_data); // Add node data to predictor buffer

        Serial.printf("Total nodes: %d, Average nodes per tree: %d\n", n_data.total_nodes * config.num_trees, n_data.total_nodes);
    }
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
            dest[i].second->loadData(source, sink_IDs, optimal_mode);
            dest[i].second->releaseData(false); // Write to binary SPIFFS, clear RAM
            logger.m_log("after splitting data");
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
        logger.m_log("after clones data");  
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
        
        // analyze a slice [begin,end) over a shared indices array
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
    void buildTree(Rf_tree& tree, ID_vector<uint16_t,2>& sampleIDs, b_vector<NodeToBuild>& queue_nodes) {
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

            if(current.nodeIndex >= MAX_NODES){
                Serial.println("⚠️ Warning: Exceeded maximum node limit. Forcing leaf node 🌿.");
                uint8_t leafLabel = stats.majorityLabel;
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }

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
        logger.m_log("tree creation",false);
    }

    // Overload for raw array input (uint8_t* features)
    uint8_t predClassSample(const packed_vector<2>& features) {
        int16_t totalPredict = 0;
        predictClass.clear();
        
        // Use streaming prediction 
        for(auto& tree : forest_container){
            uint8_t predict = tree.predictSample(features); 
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
        if(dataList.empty() || forest_container.empty()){
            Serial.println("❌ No trained trees available for OOB evaluation!");
            return 0.0f;
        }

        // Determine chunk size for memory-efficient processing
        uint16_t buffer_chunk = train_data.samplesPerChunk();

        // Pre-allocate evaluation resources
        Rf_data train_samples_buffer;
        b_vector<uint8_t, SMALL> activeTrees;
        unordered_map<uint8_t, uint8_t> oobPredictClass;

        activeTrees.reserve(config.num_trees);
        oobPredictClass.reserve(config.num_labels);

        // Initialize matrix score calculator for OOB
        Rf_matrix_score oob_scorer(config.num_labels, static_cast<uint8_t>(config.train_flag));

        // Load forest into memory for evaluation
        forest_container.loadForest();
        logger.m_log("get OOB score");

        // Process training samples in chunks for OOB evaluation
        for(size_t chunk_index = 0; chunk_index < train_data.total_chunks(); chunk_index++){
            // Load samples for this chunk
            train_samples_buffer.loadChunk(train_data, chunk_index, optimal_mode);
            if(train_samples_buffer.size() == 0){
                Serial.println("❌ Failed to load training samples chunk!");
                continue;
            }

            // Process each sample in the chunk
            for(uint16_t idx = 0; idx < train_samples_buffer.size(); idx++){
                const Rf_sample& sample = train_samples_buffer[idx];
                uint16_t sampleID = chunk_index * buffer_chunk + idx;
                uint8_t actualLabel = sample.label;

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
                    if(treeIdx < forest_container.size()){
                        uint8_t predict = forest_container[treeIdx].predictSample(sample.features);
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

        if(forest_container.empty()){
            Serial.println("❌ No trained trees available for validation evaluation!");
            return 0.0f;
        }

        // Initialize matrix score calculator for validation
        Rf_matrix_score valid_scorer(config.num_labels, static_cast<uint8_t>(config.train_flag));

        // Load forest into memory for evaluation
        forest_container.loadForest();
        logger.m_log("get validation score");

        // Validation evaluation
        Serial.println("Evaluating on validation set...");
        validation_data.loadData();
        
        for(uint16_t i = 0; i < validation_data.size(); i++){
            const Rf_sample& sample = validation_data[i];
            uint8_t actualLabel = sample.label;

            unordered_map<uint8_t, uint8_t> validPredictClass;
            uint16_t validTotalPredict = 0;

            // Use all trees for validation prediction
            for(uint8_t t = 0; t < config.num_trees && t < forest_container.size(); t++){
                uint8_t predict = forest_container[t].predictSample(sample.features);
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
            validation_data.loadData(base_data, fold_valid_sampleIDs, optimal_mode);
            validation_data.releaseData(false);
            train_data.loadData(base_data, fold_train_sampleIDs, optimal_mode);
            train_data.releaseData(false); 
            
            ClonesData();
            MakeForest();
        
            validation_data.loadData();
            forest_container.loadForest();
            // Process all samples
            for(uint16_t i = 0; i < validation_data.size(); i++){
                const Rf_sample& sample = validation_data[i];
                uint8_t actual = sample.label;
                uint8_t pred = predClassSample(sample.features);
            
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            validation_data.releaseData();
            forest_container.releaseForest();
            
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
        uint8_t min_ms = config.min_split_range.first;
        uint8_t max_ms = config.min_split_range.second;
        uint8_t min_md = config.max_depth_range.first;
        uint8_t max_md = config.max_depth_range.second;
        int total_combinations = ((max_ms - min_ms) / 2 + 1) * ((max_md - min_md) / 2 + 1);
        Serial.printf("Total combinations: %d\n", total_combinations);

        int loop_count = 0;
        int best_min_split = config.min_split;
        int best_max_depth = config.max_depth;

        float best_score = get_training_evaluation_index();
        Serial.printf("training score : %s\n", 
                      (config.training_score == Rf_training_score::OOB_SCORE) ? "OOB" : 
                      (config.training_score == Rf_training_score::VALID_SCORE) ? "VALID" : 
                      (config.training_score == Rf_training_score::K_FOLD_SCORE) ? "K-FOLD" : "UNKNOWN");

        Serial.printf("Initial score with min_split=%d, max_depth=%d: %.3f\n", 
                      config.min_split, config.max_depth, best_score);

        if(config.training_score == Rf_training_score::K_FOLD_SCORE){
            // convert train_data to base_data
            base_data = train_data; 
            // init validation_data if not yet
            if(validation_data.isProperlyInitialized() == false){
                validation_data.init("/valid_data.bin", config.num_features);
            }
        }

        for(uint8_t min_split = min_ms; min_split < max_ms; min_split += 2){
            for(uint8_t max_depth = min_md; max_depth < max_md; max_depth += 2){
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
                        // Save the best forest to SPIFFS
                        forest_container.releaseForest(); // Release current trees from RAM
                    }
                }
                if(loop_count++ > 2) return;
            }
        }
        // Set config to best found parameters
        config.min_split = best_min_split;
        config.max_depth = best_max_depth;
        if(config.training_score == Rf_training_score::K_FOLD_SCORE){
            // rebuild model with full train_data 
            train_data = base_data;     // restore train_data
            ClonesData();
            MakeForest();
            forest_container.releaseForest();
        }
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
    
    // Public methods to access forest container functionality
    void loadForest() {
        forest_container.loadForest();
    }
    
    void releaseForest() {
        forest_container.releaseForest();
    }

public:
    // Public API: predict() takes raw float data and returns actual label (String)
    template<typename T>
    struct is_supported_vector : std::false_type {};

    template<index_size_flag SizeFlag>
    struct is_supported_vector<vector<float, SizeFlag>> : std::true_type {};

    template<index_size_flag SizeFlag>
    struct is_supported_vector<vector<int, SizeFlag>> : std::true_type {};

    template<index_size_flag SizeFlag, size_t sboSize>
    struct is_supported_vector<b_vector<float, SizeFlag, sboSize>> : std::true_type {};

    template<index_size_flag SizeFlag, size_t sboSize>
    struct is_supported_vector<b_vector<int, SizeFlag, sboSize>> : std::true_type {};

    template<typename T>
    String predict(const T& features) {
        // Static assert to ensure T is the right type
        static_assert(is_supported_vector<T>::value, 
            "Unsupported feature vector type. Use mcu::vector<float>, mcu::vector<int>, mcu::b_vector<float>, or mcu::b_vector<int>.");
        return predict(features.data(), features.size());
    }

    // Public API: predict() takes raw float pointer and returns actual label (String)
    String predict(const float* features, size_t length = 0){
        if(length != config.num_features){
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.println("❌ Feature length mismatch!");
            return "ERROR";
        }

        auto categorized_features = categorizer.categorizeFeatures(features, length);
        Serial.println();
        uint8_t internal_label = predClassSample(categorized_features);
        return categorizer.getOriginalLabel(internal_label);
    }

    // methods for development stage - detailed metrics
#ifdef DEV_STAGE
    // New combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
        bool pre_load_data = true;
        if(!data.isLoaded){
            data.loadData();
            pre_load_data = false;
        }
        forest_container.loadForest();
        
        // Initialize matrix score calculator
        Rf_matrix_score scorer(config.num_labels, 0xFF); // Use all flags for detailed metrics
        
        // Single pass over samples (using direct indexing)
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample.features);
            
            // Update metrics using matrix scorer
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
            base.log_inference(actual == pred);
        }
        
        // Build result vectors using matrix scorer
        b_vector<b_vector<pair<uint8_t, float>>> result;
        result.push_back(scorer.get_precisions());  // 0: precisions
        result.push_back(scorer.get_recalls());     // 1: recalls
        result.push_back(scorer.get_f1_scores());   // 2: F1 scores
        result.push_back(scorer.get_accuracies());  // 3: accuracies

        if(!pre_load_data) data.releaseData();
        forest_container.releaseForest();
        return result;
    }

    float precision(Rf_data& data) {
        // Create a temporary scorer to calculate precision
        Rf_matrix_score scorer(config.num_labels, 0x02); // PRECISION flag only
        
        if(!data.isLoaded) data.loadData();
        forest_container.loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample.features);
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        forest_container.releaseForest();
        
        return scorer.calculate_score("Precision");
    }

    float recall(Rf_data& data) {
        // Create a temporary scorer to calculate recall
        Rf_matrix_score scorer(config.num_labels, 0x04); // RECALL flag only
        
        if(!data.isLoaded) data.loadData();
        forest_container.loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample.features);
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        forest_container.releaseForest();
        
        return scorer.calculate_score("Recall");
    }

    float f1_score(Rf_data& data) {
        // Create a temporary scorer to calculate F1-score
        Rf_matrix_score scorer(config.num_labels, 0x08); // F1_SCORE flag only
        
        if(!data.isLoaded) data.loadData();
        forest_container.loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample.features);
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        forest_container.releaseForest();
        
        return scorer.calculate_score("F1-Score");
    }

    float accuracy(Rf_data& data) {
        // Create a temporary scorer to calculate accuracy
        Rf_matrix_score scorer(config.num_labels, 0x01); // ACCURACY flag only
        
        if(!data.isLoaded) data.loadData();
        forest_container.loadForest();
        
        // Process all samples
        for (uint16_t i = 0; i < data.size(); i++) {
            const Rf_sample& sample = data[i];
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample.features);
            
            if(actual < config.num_labels && pred < config.num_labels) {
                scorer.update_prediction(actual, pred);
            }
        }
        
        data.releaseData();
        forest_container.releaseForest();
        
        return scorer.calculate_score("Accuracy");
    } 
    
    void visual_result(Rf_data& testSet) {
        forest_container.loadForest(); // Ensure all trees are loaded before prediction
        testSet.loadData(); // Load test set data if not already loaded
        Serial.println("SampleID, Predicted, Actual");
        for (uint16_t i = 0; i < testSet.size(); i++) {
            const Rf_sample& sample = testSet[i];
            uint8_t pred = predClassSample(sample.features);
            Serial.printf("%d, %d, %d\n", i, pred, sample.label);
        }
        testSet.releaseData(true); // Release test set data after use
        forest_container.releaseForest(); // Release all trees after prediction
    }
#endif

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
    long unsigned start_forest = millis();
    // const char* filename = "/walker_fall.bin";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "digit_data"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(filename); // reproducible by default (can omit random_seed)
    forest.set_training_score(Rf_training_score::OOB_SCORE);

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    forest.build_model();

    // forest.training();

    auto result = forest.predict(forest.test_data);
    Serial.printf("\nlowest RAM: %d\n", forest.logger.lowest_ram);
    Serial.printf("lowest ROM: %d\n", forest.logger.lowest_rom);

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
    b_vector<float, SMALL> sample = MAKE_FLOAT_LIST(0,0.000454539,0,0,0,0.510392,0.145854,0,0.115446,0,0.00516406,0.0914579,0.565657,0.523204,0.315898,0.0548166,0,0.310198,0.0193819,0,0,0.0634356,0.45749,0.00122793,0.493418,0.314128,0.150056,0.106594,0.321845,0.0745179,0.282953,0.353358,0,0.254502,0.502515,0.000288011,0,0,0.0756328,0.00226037,0.382164,0.261311,0.300058,0.261635,0.313706,0,0.0501838,0.450812,0.0947562,0.000373078,0.00211045,0.0744771,0.462151,0.715595,0.269004,0.0449925,0,0,0.00212813,0.000589888,0.420681,0.0574298,0.0717421,0,0.313605,0.339293,0.0629904,0.0675315,0.0618258,0.069364,0.41181,0.223367,0.0892957,0.0317173,0.0412844,0.000333441,0.733433,0.035459,0.000471556,0.00492559,0.103231,0.255209,0.411744,0.154244,0.0670255,0,0.0747003,0.271415,0.740801,0.0413177,0.000545948,0.00293495,0.31086,0.000711829,0.000690576,0.00328563,0.0109791,0,0.00179087,0.05755,0.281221,0.0908081,0.139806,0.0358642,0.0303179,0.0455232,0.000940401,0.000496404,0.933685,0.0312803,0.108249,0.0307203,0.0946534,0.0618412,0.0974416,0.0649112,0.677713,0.00266646,0.0009506,0.0560812,0.492166,0.0329419,0.0117499,0.0216917,0.379698,0.0638361,0.344801,0.00247299,0.568132,0.00436328,0.00107975,0.0635284,0.379419,0.000722445,0.000700875,0.0521259,0.635661,0.068638,0.299062,0.0238965,0.00382694,0.00504611,0.163862,0.0285841);
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