#define DEV_STAGE         // development stage - enable test_data 
#define RF_DEBUG_LEVEL 2

#include "Rf_components.h"

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
    b_vector<ID_vector<uint16_t,2>> dataList; // each ID_vector stores sample IDs of a sub_dataset, reference to index of allSamples vector in train_data
    unordered_map<uint8_t, uint8_t> predictClass;   // for predicting class of a sample

    Rf_pending_data pending_data; // pending data waiting for true labels from feedback action 
    bool clean_yet = false; 

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
        // cloneFile(base.get_base_data().c_str(), temp_base_data);
        // base_data.init(temp_base_data);

        base_data.init(base.get_base_data().c_str());


        predictClass.reserve(config.num_labels);
    }
    
    // Enhanced destructor
    ~RandomForest(){
        clean_forest();
    }

    void clean_forest(){
        if(!clean_yet){
            Serial.println("🧹 Cleaning files... ");
            // clear all Rf_data
            //clone temp data back to base data after add new samples
            // remove(base.get_base_data().c_str());
            // cloneFile(temp_base_data, base.get_base_data().c_str());
            // base_data.purgeData();
            train_data.purgeData();
            test_data.purgeData();
            validation_data.purgeData();
            // re_train node predictor after each training session
            node_predictor.re_train(); 
            clean_yet = true;
        }
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
        logger.m_log("before split data");
        size_t a1 = logger.drop_anchor();
        splitData(base_data, dest);
        size_t a2 = logger.drop_anchor();
        logger.t_log("split time", a1, a2, "s");
        ClonesData();
        size_t a3 = logger.drop_anchor();
        logger.t_log("clones time", a2, a3, "s");
        MakeForest();
        size_t a4 = logger.drop_anchor();
        logger.t_log("make forest", a3, a4, "s");
    }

private:
    void MakeForest(){
        // Clear any existing forest first
        forest_container.clearForest();
        logger.m_log("START MAKING FOREST...");

        // Get queue_nodes reference from container
        auto& queue_nodes = forest_container.getQueueNodes();
        
        // pre_allocate necessary resources
        queue_nodes.clear();
        uint16_t estimated_nodes = node_predictor.estimate(config.min_split, config.max_depth) * 100 / node_predictor.accuracy;
        uint16_t peak_nodes = min(120, estimated_nodes * node_predictor.peak_percent / 100);
        queue_nodes.reserve(peak_nodes); // Conservative estimate
        node_data n_data(config.min_split, config.max_depth,0);

        logger.m_log("after reserving queue nodes");

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
            dest[i].second->loadData(source, sink_IDs, true);
            dest[i].second->releaseData(false); // Write to binary SPIFFS, clear RAM
            if(i==dest.size()-1) logger.m_log("split data");
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
        b_vector<uint16_t> labelCounts; 
        uint8_t majorityLabel;
        uint16_t totalSamples;
        
        NodeStats(uint8_t numLabels) : majorityLabel(0), totalSamples(0) {
            labelCounts.reserve(numLabels);
            labelCounts.fill(0);
        }
        
        // analyze a slice [begin,end) over a shared indices array
        void analyzeSamplesRange(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
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
    SplitInfo findBestSplitRange(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
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
        b_vector<uint16_t, 8> indices;
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
        b_vector<uint8_t, 16> activeTrees;
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
            train_samples_buffer.loadChunk(train_data, chunk_index, true);
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
            validation_data.loadData(base_data, fold_valid_sampleIDs, true);
            validation_data.releaseData(false);
            train_data.loadData(base_data, fold_train_sampleIDs, true);
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
                // if(loop_count++ > 2) return;
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
        clean_forest();
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

    bool enable_extend_base_data(bool enable){
        config.extend_base_data = enable;
        return config.extend_base_data;
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

    void set_model_name(const String& name) {
        base.set_model_name(name.c_str());
    }

    // Public API: predict() takes raw float data and returns actual label (String)
    template<typename T>
    String predict(const T& features) {
        // Static assert to ensure T is the right type
        static_assert(mcu::is_supported_vector<T>::value, "Unsupported type. Use mcu::vector or mcu::b_vector.");
        return predict(features.data(), features.size());
    }

    // Public API: predict() takes raw float pointer and returns actual label (String)
    String predict(const float* features, size_t length = 0){
        if(length != config.num_features){
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.println("❌ Feature length mismatch!");
            return "ERROR";
        }

        packed_vector<2> c_features = categorizer.categorizeFeatures(features, length);
        uint8_t i_label = predClassSample(c_features);
        if(config.enable_retrain){
            // Add sample to pending data for potential retraining
            Rf_sample sample;
            sample.features = c_features;
            sample.label = i_label; // Store predicted label
            pending_data.add_pending_sample(sample, base_data, &config, base.get_inferenceLogFile().c_str());
        }
        return categorizer.getOriginalLabel(i_label);
    }

    public:
    // Get practical inference score based on logged predictions and actual labels
    float get_practical_inference_score(uint8_t flag) {
        String logFile = base.get_inferenceLogFile();
        if(!SPIFFS.exists(logFile.c_str())) {
            Serial.println("❌ Inference log file does not exist");
            return 0.0f;
        }
        
        File file = SPIFFS.open(logFile.c_str(), FILE_READ);
        if(!file) {
            Serial.println("❌ Failed to open inference log file");
            return 0.0f;
        }
        
        // Read header
        uint32_t magic;
        uint8_t num_labels;
        if(file.read((uint8_t*)&magic, 4) != 4 || 
           file.read((uint8_t*)&num_labels, 1) != 1 ||
           magic != 0x494E4652) { // "INFR" magic
            Serial.println("❌ Invalid inference log file header");
            file.close();
            return 0.0f;
        }
        
        // Read prediction counts for each label
        b_vector<uint16_t> prediction_counts(num_labels);
        for(int i = 0; i < num_labels; i++) {
            if(file.read((uint8_t*)&prediction_counts[i], 2) != 2) {
                Serial.println("❌ Failed to read prediction counts");
                file.close();
                return 0.0f;
            }
        }
        
        // Initialize matrix score calculator
        Rf_matrix_score scorer(num_labels, flag);
        
        // Read and process packed prediction data
        uint8_t packed_byte;
        uint8_t bit_position = 0;
        uint8_t current_label = 0;
        uint16_t label_prediction_index = 0;
        
        while(file.available() > 0 && file.read(&packed_byte, 1) == 1) {
            // Process each bit in the packed byte
            for(int bit = 0; bit < 8 && current_label < num_labels; bit++) {
                if(label_prediction_index >= prediction_counts[current_label]) {
                    // Move to next label
                    current_label++;
                    label_prediction_index = 0;
                    if(current_label >= num_labels) break;
                }
                
                // Extract prediction result (correct/incorrect)
                bool is_correct = (packed_byte & (1 << bit)) != 0;
                
                // For practical scoring, we need to simulate actual vs predicted
                // Since we only have correctness, we assume predicted_label = actual_label when correct
                // and predicted_label = (actual_label + 1) % num_labels when incorrect
                uint8_t actual_label = current_label;
                uint8_t predicted_label = is_correct ? actual_label : ((actual_label + 1) % num_labels);
                
                scorer.update_prediction(actual_label, predicted_label);
                label_prediction_index++;
            }
        }
        
        file.close();
        
        // Calculate and return score based on training flags
        return scorer.calculate_score("Practical");
    }

    //overload version without flag: uses model training_flag 
    float get_practical_inference_score() {
        return get_practical_inference_score(static_cast<uint8_t>(config.train_flag));
    }

    // get number of inference saved in log
    size_t get_logged_inference_count() const {
        String logFile = base.get_inferenceLogFile();
        if(!SPIFFS.exists(logFile.c_str())) {
            return 0;
        }   
        File file = SPIFFS.open(logFile.c_str(), FILE_READ);
        if(!file) {
            return 0;
        }
        size_t file_size = file.size();
        file.close();
        if(file_size < 5 + config.num_labels * 2) {
            return 0; // Invalid file size
        }
        size_t data_size = file_size - (5 + config.num_labels * 2);
        return data_size * 8; // Each byte contains 8 predictions
    } 

    void add_actual_label(const String& label){
        uint8_t i_label = categorizer.getNormalizedLabel(label);
        if(i_label < config.num_labels){
            pending_data.add_actual_label(i_label);
        } else {
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("❌ Unknown label: %s\n", label.c_str());
        }
    }
    // overload for another type of label input : const char*, char*, const char[N], int, float
    template<typename T>
    void add_actual_label(const T& label) {
        using U = std::decay_t<T>;
        static_assert(
            std::is_same_v<U, const char*> ||
            std::is_same_v<U, char*> ||
            (std::is_array_v<U> && std::is_same_v<std::remove_extent_t<U>, const char>) ||
            std::is_arithmetic_v<U>,
            "add_actual_label: T must be one of: String, const char*, or a numeric type"
        );
        add_actual_label(String(label));
    }

    // Manually flush pending data to base dataset and inference log
    void flush_pending_data() {
        pending_data.flush_pending_data(base_data, config, base.get_inferenceLogFile().c_str());
    }
    
    void set_feedback_timeout(long unsigned timeout){
        pending_data.set_max_wait_time(timeout);
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
            // base.log_inference(actual == pred);
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
    b_vector<b_vector<pair<uint8_t, float>>> result_on_test_set() {
        return predict(test_data);
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
    
    void visual_result() {
        forest_container.loadForest(); // Ensure all trees are loaded before prediction
        test_data.loadData(); // Load test set data if not already loaded
        Serial.println("SampleID, Predicted, Actual");
        for (uint16_t i = 0; i < test_data.size(); i++) {
            const Rf_sample& sample = test_data[i];
            uint8_t pred = predClassSample(sample.features);
            Serial.printf("%d, %d, %d\n", i, pred, sample.label);
        }
        test_data.releaseData(true); // Release test set data after use
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
    long unsigned start_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    // const char* filename = "/walker_fall.bin";    // medium dataset : use_Gini = false | boostrap = true; 92% - sklearn : 85%  (5,3)
    const char* filename = "digit_data"; // hard dataset : use_Gini = true/false | boostrap = true; 89/92% - sklearn : 90% (6,5);
    RandomForest forest = RandomForest(filename); // reproducible by default (can omit random_seed)
    forest.set_training_score(Rf_training_score::K_FOLD_SCORE);

    // printout size of forest object in stack
    Serial.printf("RandomForest object size: %d bytes\n", sizeof(forest));

    // forest.build_model();

    // forest.training();

    // auto result = forest.result_on_test_set();
    // Serial.printf("\nlowest RAM: %d\n", forest.logger.lowest_ram);
    // Serial.printf("lowest ROM: %d\n", forest.logger.lowest_rom);

    // // Calculate Precision
    // Serial.println("Precision in test set:");
    // b_vector<pair<uint8_t, float>> precision = result[0];
    // for (const auto& p : precision) {
    //   Serial.printf("Label: %d - %.3f\n", p.first, p.second);
    // }
    // float avgPrecision = 0.0f;
    // for (const auto& p : precision) {
    //   avgPrecision += p.second;
    // }
    // avgPrecision /= precision.size();
    // Serial.printf("Avg: %.3f\n", avgPrecision);

    // // Calculate Recall
    // Serial.println("Recall in test set:");
    // b_vector<pair<uint8_t, float>> recall = result[1];
    // for (const auto& r : recall) {
    //   Serial.printf("Label: %d - %.3f\n", r.first, r.second);
    // }
    // float avgRecall = 0.0f;
    // for (const auto& r : recall) {
    //   avgRecall += r.second;
    // }
    // avgRecall /= recall.size();
    // Serial.printf("Avg: %.3f\n", avgRecall);

    // // Calculate F1 Score
    // Serial.println("F1 Score in test set:");
    // b_vector<pair<uint8_t, float>> f1_scores = result[2];
    // for (const auto& f1 : f1_scores) {
    //   Serial.printf("Label: %d - %.3f\n", f1.first, f1.second);
    // }
    // float avgF1 = 0.0f;
    // for (const auto& f1 : f1_scores) {
    //   avgF1 += f1.second;
    // }
    // avgF1 /= f1_scores.size();
    // Serial.printf("Avg: %.3f\n", avgF1);

    // // Calculate Overall Accuracy
    // Serial.println("Overall Accuracy in test set:");
    // b_vector<pair<uint8_t, float>> accuracies = result[3];
    // for (const auto& acc : accuracies) {
    //   Serial.printf("Label: %d - %.3f\n", acc.first, acc.second);
    // }
    // float avgAccuracy = 0.0f;
    // for (const auto& acc : accuracies) {
    //   avgAccuracy += acc.second;
    // }
    // avgAccuracy /= accuracies.size();
    // Serial.printf("Avg: %.3f\n", avgAccuracy);

    // Serial.printf("\n📊 FINAL SUMMARY:\n");
    // Serial.printf("Dataset: %s\n", filename);
    // Serial.printf("Average Precision: %.3f\n", avgPrecision);
    // Serial.printf("Average Recall: %.3f\n", avgRecall);
    // Serial.printf("Average F1-Score: %.3f\n", avgF1);
    // Serial.printf("Average Accuracy: %.3f\n", avgAccuracy);

    forest.loadForest();
    forest.enable_extend_base_data(true);



    // // check actual prediction time 
    b_vector<float> sample_1 = MAKE_FLOAT_LIST(0,0,0,0,0,0,0.319597,0,0,0,0.0155578,0,0.602115,0,0.731487,0,0,0,0.0427323,0,0.100705,0.0183983,0.43727,0.279462,0.244308,0,0.55038,0,0.338926,0.284307,0.130816,0.378115,0.0443502,0.0208504,0.543977,0.166894,0.0697761,0,0,0.149815,0.551409,0.322199,0.338399,0.0939219,0.0580764,0,0,0.334588,0,0,0,0,0.329144,0,0.590239,0,0.00454205,0,0.393911,0,0.417964,0,0.428854,0.171716,0.0930952,0,0.399416,0,0.306163,0.17656,0.195359,0.337671,0.354709,0,0.45624,0.13206,0.0282058,0.209302,0.253028,0.307674,0.395338,0.204116,0.440053,0.128077,0.0669144,0,0,0.262297,0.137504,0.235855,0.372225,0.0570641,0.0712728,0.0407601,0.160617,0.519457,0,0,0.291677,0,0.638353,0,0.444923,0.0625388,0.0395925,0.22281,0.16253,0,0.370294,0.177272,0.20518,0.131503,0.450441,0,0.484771,0.0470593,0.0975183,0.284126,0.189368,0.357239,0.21504,0.214781,0.116269,0.089935,0.209008,0.245536,0.198381,0.221044,0.297168,0.321228,0.234775,0.0348076,0.0206753,0,0.0894389,0.493047,0.231743,0.303444,0.284468,0.0372155,0.402831,0.0840795,0.073505,0.312755);
    b_vector<float> sample_2 = MAKE_FLOAT_LIST(0,0,0,0,0,0,0,0,0,0,0,0,0.174492,0,0.984659,0,0,0,0,0,0,0,0.0841288,0.0219022,0,0,0.222325,0,0.57933,0.0204313,0.778785,0.0216055,0,0,0.0061709,0,0.00511314,0,0.068674,0.0573897,0.175482,0.0181766,0.811145,0,0.544805,0,0.0512002,0.0584059,0,0,0,0,0,0,0.179298,0,0,0,0.211284,0.00151423,0.528726,0.0103828,0.776789,0.20037,0,0,0.0142657,0,0.157209,0.00189016,0.457445,0.0266492,0.235635,0,0.507783,0.139537,0.514762,0.0178007,0.387424,0.129923,0.0192672,0.00214049,0.440721,0,0.322496,0,0.0934634,0.0902834,0.522127,0.0201582,0.571542,0.16255,0.210484,0,0,0.118699,0,0,0.0344402,0.00131337,0.4124,0.00900558,0.688182,0,0,0.16296,0.151445,0,0.0551359,0.401566,0.325127,0.188597,0.189033,0,0.396294,0.000995046,0.562154,0.018721,0.380578,0,0.0349963,0.359648,0.190518,0.134021,0.260099,0.189424,0.167082,0.172423,0.499019,0.0216759,0.58545,0,0.320087,0,0,0,0.272465,0.238068,0.298211,0.185451,0.109965,0.0963578,0,0.172309);
    b_vector<float> sample_3 = MAKE_FLOAT_LIST(0,0,0,0,0.0884918,0.401737,0.497088,0.181131,0,0,0,0,0.0509315,0.0918492,0.734739,0,0.0721371,0.226002,0.333084,0.0755246,0.16162,0.101488,0.124187,0.409836,0,0.0513397,0.3246,0,0.642339,0.0235343,0.293441,0,0.163562,0.14088,0.192565,0.422734,0.065712,0,0,0.0499875,0.621695,0.0524913,0.584353,0,0.00832314,0,0,0,0,0,0,0,0.0990201,0.550154,0.607643,0,0,0,0,0,0.326628,0.328588,0.321932,0,0.0763265,0.293448,0.181584,0,0.673102,0.132283,0.325746,0,0,0.234372,0.247478,0,0.426425,0.0199028,0.0016461,0,0.670186,0.206763,0.496628,0,0.0726361,0,0,0,0.422666,0.205566,0.187832,0,0,0,0,0,0,0,0,0,0.458715,0.460922,0.759692,0,0,0,0,0,0,0,0,0,0,0.314857,0.503596,0,0.80188,0.0267376,0.0594196,0,0,0,0,0,0,0,0,0,0.82105,0.285276,0.494465,0,0,0,0,0,0,0,0,0,0,0,0,0);
    b_vector<float> sample_4 = MAKE_FLOAT_LIST(0,0.0908824,0.104416,0,0.0755478,0.188405,0.618161,0.00479052,0,0.0124262,0,0,0.0939856,0.725348,0.149571,0,0.0591422,0.218639,0.561053,0.00375024,0.237376,0,0.00461243,0.259768,0.0735761,0.577562,0.100979,0,0.283351,0,0.0161121,0.283411,0.32871,0.0914897,0.179426,0.359717,0,0,0,0,0.392375,0.623118,0.160412,0.392457,0,0,0,0,0,0.104163,0.0268341,0,0.170936,0.830073,0.0611818,0,0,0,0,0,0,0.365051,0.364944,0,0.131691,0.719745,0.054991,0,0.281157,0,0.0128174,0.281268,0,0.281239,0.255798,0,0.281157,0,0.0253593,0.281167,0.34306,0.539029,0.0559995,0.343196,0,0,0,0,0.34306,0.34316,0.34306,0.343072,0,0,0,0,0,0,0,0,0,0.551749,0.435521,0,0,0,0,0,0,0.533102,0.470847,0,0,0.401093,0.29057,0,0.358815,0,0.0260317,0.35883,0,0.370335,0.289866,0,0.299172,0.0172022,0.0524152,0.42754,0.348799,0.389897,0.307764,0.348813,0,0,0,0,0.290821,0.37672,0.332727,0.415605,0,0,0,0);
    b_vector<float> sample_5 = MAKE_FLOAT_LIST(0,0,0.0257681,0,0,0,0.309993,0.505646,0,0,0.00933626,0,0,0.766647,0.244392,0,0,0,0.245919,0.228898,0,0,0,0.565774,0,0.420417,0.0258278,0,0.26965,0.344006,0.445602,0,0,0,0,0.492467,0,0,0,0.358769,0.0668883,0.422763,0.400113,0,0.530191,0,0.00500874,0.0658773,0,0,0.0304377,0,0,0.664732,0.200389,0.202783,0,0,0,0,0,0.421736,0.545955,0,0,0.408206,0.17382,0.0890006,0.0700646,0.334014,0.256451,0.407654,0,0.208234,0.275031,0,0.296742,0.328262,0.209232,0.297013,0,0.394298,0.210135,0.288849,0.39401,0,0.00467149,0.386712,0.147898,0.400611,0.331816,0.109612,0.103576,0,0.000111226,0.311318,0,0,0,0,0,0.164061,0.84378,0,0,0,0.094193,0.187499,0,0.46593,0,0,0,0.0776083,0.277926,0,0.370533,0.0140897,0.406248,0.288246,0,0.132818,0,0.0478734,0.000134187,0.710945,0.0748341,0.011894,0.162469,0.0493664,0.542014,0.12041,0.291901,0,0,0.183158,0,0.512065,0.00989686,0,0.0920044,0.236779,0.0721718,0.460531);
    b_vector<float> sample_6 = MAKE_FLOAT_LIST(0,0,0,0,0.110356,0.2904,0.746565,0.00741822,0,0,0,0,0,0.418218,0.413725,0,0.0800193,0.201106,0.45076,0.00292075,0.142429,0.0438233,0.178908,0.478748,0,0,0.270083,0,0.256167,0.352733,0.0788601,0.44423,0.219892,0.155945,0.453719,0.473245,0,0,0,0,0.253222,0.348679,0.344932,0.439124,0,0,0,0,0,0,0,0,0.0525942,0.466355,0.413942,0,0,0,0,0,0.104759,0.505657,0.584128,0.0230307,0.0363423,0.0756487,0.233643,0,0.164194,0.275148,0.0777276,0.46132,0,0.189648,0.293133,0.0156375,0.449763,0.318571,0.146253,0.411754,0.189863,0.303688,0.294798,0.436768,0,0,0,0,0.383877,0.448993,0.356127,0.333922,0.0419486,0.00217724,0,0.070723,0,0,0,0,0,0.331257,0.838403,0.0280708,0,0,0,0,0.127685,0.412624,0.000248414,0,0,0,0.433516,0.019445,0.321854,0.287001,0.209887,0.609271,0,0.235824,0.000190637,0,0.334644,0.207032,0,0,0.294638,0.2814,0.55393,0.525371,0.0209346,0,0,0.0910735,0.295027,0.392776,0,0,0.0330847,0.00280374,0,0);
    b_vector<float> sample_7 = MAKE_FLOAT_LIST(0,0,0,0,0.108834,0,0.437133,0.023986,0,0,0.168557,0,0.599053,0.019216,0.63085,0.104296,0.00267646,0,0.253298,0.00460033,0.191034,0.11192,0.193409,0.302132,0.370827,0,0.59976,0.0398637,0.179314,0.228867,0.14511,0.392041,0.245114,0.136185,0.381898,0.210643,0.00596307,0.00543424,0.0364883,0.350574,0.472319,0.196829,0.288241,0.397918,0.250737,0.0927714,0.0156852,0.178572,0,0,0.063585,0,0.48504,0,0.617058,0.0640887,0,0.0883194,0.316275,0,0.503125,0.0325585,0.105083,0.0525187,0.284177,0,0.531324,0.0147548,0.270062,0.215936,0.151484,0.203114,0.364786,0.0772039,0.360402,0.0449211,0.142917,0.251891,0.175516,0.259497,0.542759,0.197855,0.285101,0.270339,0.233413,0.0901442,0.0478907,0.203382,0.246279,0.338169,0.273016,0.094915,0.214486,0,0,0.317919,0,0.0317341,0.343292,0,0.564818,0.0354346,0.317224,0.0951488,0,0.163634,0.0926272,0,0.240753,0.441633,0.0974575,0.391571,0.33,0.0232024,0.408423,0.0302801,0.0784008,0.222093,0.103505,0.391034,0.100299,0.396753,0.0689499,0.234754,0.434848,0.0294039,0.29209,0.0336653,0.263316,0.32756,0.260274,0.353796,0.0744787,0.00353458,0,0.309082,0.418655,0.12424,0.432625,0.184899,0.342497,0,0,0);
    b_vector<float> sample_8 = MAKE_FLOAT_LIST(0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0.115915,0,0.480182,0,0.821055,0.286107,0,0,0,0,0,0,0,0,0.0176644,0,0.850128,0.0573638,0.482478,0,0,0.202226,0,0,0,0,0,0,0.12943,0,0,0,0.0183514,0,0.337471,0.0161549,0.932075,0,0,0,0,0,0.164763,0,0.40888,0.174718,0.148629,0,0.563784,0,0.610492,0.0148088,0.247736,0.0301245,0.00173257,0,0.481645,0.04548,0.243463,0,0,0.160332,0.526687,0.0174442,0.521358,0,0.359842,0,0,0.0442927,0,0,0,0,0.112102,0.0134528,0.634836,0,0,0.212109,0.239858,0,0.47967,0.201275,0.444878,0.114901,0.0119875,0,0.354204,0,0.521324,0.0130347,0.260678,0,0.37499,0.261378,0.351315,0.0873781,0.338345,0.0902905,0.213164,0.162583,0.218548,0.0152442,0.505454,0,0.417659,0,0,0,0.49842,0.147229,0.282697,0.126012,0.286362,0.0586704,0,0.265971);
    b_vector<float> sample_9 = MAKE_FLOAT_LIST(0,0.0956604,0.098247,0,0.417734,0.220911,0.311895,0.324612,0,0.0231785,0.176474,0,0.564824,0.26379,0.346738,0.121338,0.249292,0.178207,0.275597,0.211925,0.153124,0.295389,0.26327,0.215302,0.353519,0.154717,0.353768,0.0528112,0.154179,0.467658,0.0669753,0.187208,0.19745,0.391921,0.303989,0.158605,0.234772,0.00180255,0.0387419,0.299266,0.251029,0.405501,0.0871662,0.151347,0.130699,0.205678,0,0.475739,0,0.0554978,0.077552,0,0.395509,0.351504,0.332694,0.369725,0,0.191861,0.123412,0,0.152125,0.368935,0.140668,0.482139,0.231082,0.242545,0.249301,0.210294,0.218287,0.340427,0.242107,0.204602,0.109795,0.391003,0.118778,0.321611,0.165454,0.127155,0.436234,0.032522,0.236854,0.375758,0.313202,0.262977,0.258655,0.068365,0,0.326493,0.0739646,0.0689581,0.378467,0.177771,0.447888,0.1274,0.185436,0.14489,0,0.22332,0.233887,0,0.446044,0.122845,0.205414,0.509487,0,0.0106548,0,0,0.098534,0.597537,0.0810833,0.0784841,0.320192,0.251493,0.291953,0.34017,0.0168087,0.324574,0.407311,0.0504237,0,0.396094,0,0.0574232,0.410932,0.112823,0.131985,0,0.089675,0.199537,0.329405,0.1731,0.382723,0.265848,0.254029,0.391817,0.388143,0.338448,0.189057,0.0704277,0.269279,0.00718677,0,0);
    b_vector<float> sample_10 = MAKE_FLOAT_LIST(0,0,0,0,0.194082,0.0751852,0.499238,0.147238,0,0.0405393,0.236777,0,0.688561,0.00110666,0.39235,0,0.082613,0,0.333312,0.0264274,0.0623509,0.166592,0.0295297,0.456743,0.500441,0.0294637,0.457245,0,0.0352748,0.376655,0.127081,0.143553,0.193715,0.231421,0.198166,0.505423,0.00542692,0,0,0.144223,0.159241,0.52323,0.0786903,0,0.194092,0,0.0978441,0.490717,0,0,0.155868,0,0.656508,0.0694687,0.498978,0,0,0.146742,0.139485,0,0.241342,0.232652,0.0794702,0.362242,0.415125,0,0.466054,0,0.0783402,0.431813,0.0610619,0.263295,0.171763,0.270015,0.15583,0.257808,0.083755,0.0525776,0.376662,0,0.290653,0.553989,0.0411409,0.0619111,0.0972659,0,0.0371976,0.448568,0.0306256,0.0674538,0.374664,0.0311766,0.477389,0,0.10857,0.067192,0,0.148076,0.187634,0,0.35235,0.133719,0.132428,0.247639,0,0,0.0379139,0,0.0783612,0.727448,0.0579085,0.426452,0.26896,0.215103,0.244314,0.189031,0.0708566,0.116052,0.344431,0,0.0314912,0.48739,0.0446864,0.25373,0.452102,0.115131,0.348031,0.0717939,0.0179453,0.14906,0.290717,0,0.441574,0,0.151681,0.152095,0.314451,0.302861,0.446024,0.305773,0.410823,0,0.000996961,0);
    
    vector<b_vector<float>> samples;
    samples.push_back(sample_1);
    samples.push_back(sample_2);
    samples.push_back(sample_3);
    samples.push_back(sample_4);
    samples.push_back(sample_5);
    samples.push_back(sample_6);
    samples.push_back(sample_7);
    samples.push_back(sample_8);
    samples.push_back(sample_9);
    samples.push_back(sample_10);

    vector<uint8_t> true_labels = MAKE_UINT8_LIST(4, 4, 7, 1, 3, 1, 6, 4, 8, 0);

    for (int i = 0; i < samples.size(); i++){
        String label = forest.predict(samples[i]);
        forest.add_actual_label(true_labels[i]);
        Serial.printf("Sample %d - Predicted: %s - Actual: %d\n", i+1, label.c_str(), true_labels[i]);
    }    
    forest.releaseForest();
    forest.flush_pending_data();

    // forest.visual_result(forest.test_data); // Optional visualization


    long unsigned end_forest = GET_CURRENT_TIME_IN_MILLISECONDS;
    Serial.printf("\nTotal time: %lu ms\n", end_forest - start_forest);
}

void loop() {
    manageSPIFFSFiles();
}