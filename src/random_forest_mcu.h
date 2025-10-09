#pragma once

#include "Rf_components.h"
#include <cstdio>

namespace mcu{
    
    class RandomForest{
        Rf_data base_data;      //
        Rf_data train_data;     //
        Rf_data test_data;      //
        Rf_data validation_data;    //

        Rf_base base;
        Rf_config config;
        Rf_logger logger;
        Rf_random random_generator;     // 
        Rf_categorizer categorizer;     //
        Rf_node_predictor  node_pred;   //
        Rf_pending_data pending_data; 
        Rf_tree_container forest_container; 

        vector<ID_vector<uint16_t,2>> dataList;     // 

        bool clean_yet = false;
        bool print_log = false;

    public:
        RandomForest(){};
        RandomForest(const char* model_name) {
            init(model_name);
        }

        void init(const char* model_name){
            // initial components
            base.init(model_name);      // base must be init first

            logger.init(&base);
            config.init(&base);
            // node_pred.init(&base);
            categorizer.init(&base);
            pending_data.init(&base, &config);
            forest_container.init(&base, &config);               

            // load resources
            config.loadConfig();
            // node_pred.loadPredictor();
            categorizer.loadCategorizer(); 
            // random_generator.init(config.random_seed);

            if constexpr (RF_DEBUG_LEVEL > 2) print_log = true;
        }
        
        // Enhanced destructor
        ~RandomForest(){
            clean_forest();
            forest_container.releaseForest();
        }

        
        void clean_forest(){
            if(!clean_yet){
                RF_DEBUG(0, "🧹 Cleaning files... ");

                //clone temp data back to base data after add new samples
                // if temp_base_data file size > original base_data file size, replace original file
                char base_path[RF_PATH_BUFFER];
                base.get_base_data_path(base_path, sizeof(base_path));
                File tempFile = LittleFS.open(temp_base_data, FILE_READ);

                size_t tempSize = tempFile ? tempFile.size() : 0;
                tempFile.close();
                File baseFile = LittleFS.open(base_path, FILE_READ);
                size_t baseSize = baseFile ? baseFile.size() : 0;
                baseFile.close();

                if(tempSize > baseSize && config.enable_retrain){
                    remove(base_path);
                    cloneFile(temp_base_data, base_path);
                }

                // clear all Rf_data
                base_data.purgeData();
                train_data.purgeData();
                test_data.purgeData();
                validation_data.purgeData();
                // re_train node predictor after each training session
                node_pred.re_train(); 
                clean_yet = true;
            }
        }

        bool build_model(){
            RF_DEBUG(0, "🌲 Building model... ");
            if(!base.able_to_training()){
                RF_DEBUG(0, "❌ Model not set for training");
                return false;
            }
            // clone base_data to temp_base_data to avoid modifying original data
            char base_path[RF_PATH_BUFFER];
            base.get_base_data_path(base_path, sizeof(base_path));
            cloneFile(base_path, temp_base_data);
            if(!base_data.init(temp_base_data)){
                RF_DEBUG(0, "❌ Error initializing base data");
                return false;
            }

            // initialize data components
            dataList.reserve(config.num_trees);
            char path[RF_PATH_BUFFER];

            base.build_data_file_path(path, "train_data");
            train_data.init(path, config.num_features);

            base.build_data_file_path(path, "test_data");
            test_data.init(path, config.num_features);

            if(config.use_validation()){
                base.build_data_file_path(path, "valid_data");
                validation_data.init(path, config.num_features);
            }

            // data splitting
            vector<pair<float, Rf_data*>> dest;
            dest.reserve(3);
            dest.push_back(make_pair(config.train_ratio, &train_data));
            dest.push_back(make_pair(config.test_ratio, &test_data));
            if(config.use_validation()){
                dest.push_back(make_pair(config.valid_ratio, &validation_data));
            }
            splitData(base_data, dest);
            dest.clear(); 
            ClonesData();

            // build forest
            if(!build_forest()){
                RF_DEBUG(0, "❌ Error building forest");
                return false;
            }
            return true;
        }

    private:
        bool build_forest(){
            size_t start = logger.drop_anchor();
            // Clear any existing forest first
            forest_container.clearForest();
            logger.m_log("start building forest");

            uint16_t estimated_nodes = node_pred.estimate_nodes(config);
            uint16_t peak_nodes = node_pred.queue_peak_size(config);
            auto& queue_nodes = forest_container.getQueueNodes();
            queue_nodes.clear();
            queue_nodes.reserve(peak_nodes); // Conservative estimate
            RF_DEBUG(2, "🌳 Estimated nodes per tree: ", estimated_nodes);

            if(!train_data.loadData()){
                RF_DEBUG(0, "❌ Error loading training data");
                return false;
            }
            logger.m_log("load train_data");
            for(uint8_t i = 0; i < config.num_trees; i++){
                Rf_tree tree(i);
                tree.nodes.reserve(estimated_nodes); // Reserve memory for nodes based on prediction
                queue_nodes.clear();                // Clear queue for each tree
                buildTree(tree, dataList[i], queue_nodes);
                tree.isLoaded = true;
                forest_container.add_tree(std::move(tree));
                logger.m_log("tree creation");
            }
            train_data.releaseData(); 
            forest_container.is_loaded = false;
            // forest_container.set_to_individual_form();
            node_pred.add_new_samples(config.min_split, config.max_depth, forest_container.avg_nodes());

            RF_DEBUG_2(0, "🌲 Forest built successfully: ", forest_container.get_total_nodes(), "nodes","");
            RF_DEBUG_2(1, "Min split: ", config.min_split, "- Max depth: ", config.max_depth);
            size_t end = logger.drop_anchor();
            logger.t_log("forest building time", start, end, "s", print_log);
            return true;
        }
        
        // ----------------------------------------------------------------------------------
        // Split data into training and testing sets. Dest data must be init() before called by this function
        bool splitData(Rf_data& source, vector<pair<float, Rf_data*>>& dest, Rf_ra) {
            size_t start = logger.drop_anchor();
            RF_DEBUG(0, "🔀 splitting data...");
            if(dest.empty() || source.size() == 0) {
                RF_DEBUG(0, "❌ Error: No data to split or destination is empty.");
                return false;
            } 
            float total_ratio = 0.0f;
            for(auto& d : dest) {
                if(d.first <= 0.0f || d.first > 1.0f) {
                    RF_DEBUG_2(0, "❌ Error: Invalid ratio: ", d.first, ". Must be in (0.0, 1.0].","");
                    return false;
                }
                total_ratio += d.first;
                if(total_ratio > 1.0f) {
                    RF_DEBUG(0, "❌ Error: Total split ratios exceed 1.0: ", total_ratio);
                    return false;
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
                dest[i].second->releaseData(false); 
            }
            size_t end = logger.drop_anchor();
            logger.m_log("split data");
            logger.t_log("split time", start, end, "s", print_log);
            return true;
        }
        
        void ClonesData() {
            size_t start = logger.drop_anchor();
            RF_DEBUG(1, "🔀 Cloning data for each tree...");
            dataList.clear();
            dataList.reserve(config.num_trees);
            uint16_t numSample = train_data.size();
            uint16_t numSubSample;
            if(config.use_boostrap) {
                numSubSample = numSample; // Bootstrap sampling with replacement
                RF_DEBUG(2, "Using bootstrap, allowing duplicate sample IDs");
            } else {
                // Sub-sampling without replacement
                numSubSample = static_cast<uint16_t>(numSample * config.boostrap_ratio); 
                RF_DEBUG(2, "No bootstrap, unique sample IDs only");
            }

            // Track hashes of each tree dataset to avoid duplicates across trees
            unordered_set<uint64_t> seen_hashes;
            seen_hashes.reserve(config.num_trees * 2);

            for (uint8_t i = 0; i < config.num_trees; i++) {
                // Create a new ID_vector for this tree
                // ID_vector stores sample IDs as bits, so we need to set bits at sample positions
                ID_vector<uint16_t,2> sub_data;
                sub_data.reserve(numSample);

                // Derive a deterministic per-tree RNG; retry with nonce if duplicate detected
                uint64_t nonce = 0;
                while (true) {
                    sub_data.clear();
                    auto tree_rng = random_generator.deriveRNG(i, nonce);

                    if (config.use_boostrap) {
                        // Bootstrap sampling: allow duplicates, track occurrence count
                        for (uint16_t j = 0; j < numSubSample; ++j) {
                            uint16_t idx = static_cast<uint16_t>(tree_rng.bounded(numSample));
                            // For ID_vector with 2 bits per value, we can store up to count 3
                            // Add the sample ID, which will increment its count in the bit array
                            sub_data.push_back(idx);
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
                            sub_data.push_back(arr[t]);
                        }
                    }
                    uint64_t h = random_generator.hashIDVector(sub_data);
                    if (seen_hashes.find(h) == seen_hashes.end()) {
                        seen_hashes.insert(h);
                        break; // unique, accept
                    }
                    nonce++;
                    if (nonce > 8) {
                        auto temp_vec = sub_data;  // Copy current state
                        sub_data.clear();
                        
                        // Re-add samples with slight modifications
                        for (uint16_t k = 0; k < min(5, (int)temp_vec.size()); ++k) {
                            uint16_t original_id = k < temp_vec.size() ? k : 0; // Simplified access
                            uint16_t modified_id = static_cast<uint16_t>((original_id + k + i) % numSample);
                            sub_data.push_back(modified_id);
                        }
                        
                        // Add remaining samples from original
                        for (uint16_t k = 5; k < min(numSubSample, (uint16_t)temp_vec.size()); ++k) {
                            uint16_t id = k % numSample; // Simplified access
                            sub_data.push_back(id);
                        }
                        
                        // accept after tweak
                        seen_hashes.insert(random_generator.hashIDVector(sub_data));
                        break;
                    }
                }
                dataList.push_back(std::move(sub_data));
                logger.m_log("clone sub_data");
            }

            logger.m_log("clones data");  
            size_t end = logger.drop_anchor();
            logger.t_log("clones data time", start, end, "ms", print_log);
            RF_DEBUG_2(1, "🎉 Created ", dataList.size(), "datasets for trees","");
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
                labelCounts.resize(numLabels, static_cast<uint16_t>(0));
            }
            
            // analyze a slice [begin,end) over a shared indices array
            void analyzeSamples(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
                                    uint8_t numLabels, const Rf_data& data) {
                totalSamples = (begin < end) ? (end - begin) : 0;
                uint16_t maxCount = 0;
                for (uint16_t k = begin; k < end; ++k) {
                    uint16_t sampleID = indices[k];
                    if (sampleID < data.size()) {
                        uint8_t label = data.getLabel(sampleID);
                        labels.insert(label);
                        if (label < numLabels && label < RF_MAX_LABELS) {
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

        SplitInfo findBestSplit(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
                                    const unordered_set<uint16_t>& selectedFeatures, bool use_Gini, uint8_t numLabels) {
            SplitInfo bestSplit;
            uint16_t totalSamples = (begin < end) ? (end - begin) : 0;
            if (totalSamples < 2) return bestSplit;

            // Base label counts
            b_vector<uint16_t> baseLabelCounts(numLabels, 0);
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
            if (sampleIDs.empty()) {
                RF_DEBUG(1, "⚠️ Warning: sub_data is empty. Ignoring.. !");
                return;
            }
        
            // Create root node and initial sample index list once per tree
            Tree_node rootNode;
            tree.nodes.push_back(rootNode);

            // Build a single contiguous index array for this tree
            b_vector<uint16_t, 8> indices;
            indices.reserve(sampleIDs.size());
            for (const auto& sid : sampleIDs) indices.push_back(sid);
            
            // Root covers the whole slice
            queue_nodes.push_back(NodeToBuild(0, 0, static_cast<uint16_t>(indices.size()), 0));
            bool prinnted = false;
            // Process nodes breadth-first with minimal allocations
            while (!queue_nodes.empty()) {
                NodeToBuild current = std::move(queue_nodes.front());
                queue_nodes.erase(0);
                
                // Analyze node samples over the slice
                NodeStats stats(config.num_labels);
                stats.analyzeSamples(indices, current.begin, current.end, config.num_labels, train_data);

                if(current.nodeIndex >= RF_MAX_NODES){
                    RF_DEBUG(2, "⚠️ Warning: Exceeded maximum node limit. Forcing leaf node 🌿.");
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
                } else if (stats.totalSamples < config.min_split || current.depth >= config.max_depth - 1) {
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
                uint16_t N = config.num_features;
                uint16_t K = num_selected_features > N ? N : num_selected_features;
                for (uint16_t j = N - K; j < N; ++j) {
                    uint16_t t = static_cast<uint16_t>(random_generator.bounded(j + 1));
                    if (selectedFeatures.find(t) == selectedFeatures.end()) selectedFeatures.insert(t);
                    else selectedFeatures.insert(j);
                }
                
                // Find best split on the slice
                SplitInfo bestSplit = findBestSplit(indices, current.begin, current.end,
                                                selectedFeatures, config.use_gini, config.num_labels);
                
                if (bestSplit.gain <= config.impurity_threshold) {
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
        }
        
        // Helper function: evaluate the entire forest using OOB (Out-of-Bag) score
        // Iterates over all samples in training data and evaluates using trees that didn't see each sample
        float get_oob_score(){
            RF_DEBUG(1, "Getting OOB score..");

            // Check if we have trained trees and data
            if(dataList.empty()){
                RF_DEBUG(0, "❌ No sub_data for validation");
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
            Rf_matrix_score oob_scorer(config.num_labels, static_cast<uint8_t>(config.metric_score));

            // Load forest into memory for evaluation
            if(!forest_container.loadForest()){
                RF_DEBUG(0,"❌ Failed to load forest for OOB evaluation!");
                return 0.0f;
            }
            logger.m_log("get OOB score");

            // Process training samples in chunks for OOB evaluation
            for(size_t chunk_index = 0; chunk_index < train_data.total_chunks(); chunk_index++){
                train_samples_buffer.loadChunk(train_data, chunk_index, true);
                if(train_samples_buffer.size() == 0){
                    RF_DEBUG(0, "❌ Failed to load training samples chunk!");
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
                            uint8_t predict = forest_container[treeIdx].predict_features(sample.features);
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

                    // Update OOB metrics using matrix scorer
                    oob_scorer.update_prediction(actualLabel, oobPredictedLabel);
                }
                logger.m_log();
            }
            forest_container.releaseForest();
            train_samples_buffer.purgeData();

            return oob_scorer.calculate_score();
        }

        // Helper function: evaluate the entire forest using validation score
        // Evaluates using validation dataset if available
        float get_valid_score(){
            RF_DEBUG(1, "Get validation score... ");
            if(!config.use_validation()){
                RF_DEBUG(1, "❌ Validation not enabled in config");
                return 0.0f;
            }
            // Load forest into memory for evaluation
            if(!forest_container.loadForest()){
                RF_DEBUG(0,"❌ Failed to load forest for validation evaluation!");
                return 0.0f;
            }
            if(!validation_data.loadData()){
                RF_DEBUG(0,"❌ Failed to load validation data for evaluation!");
                forest_container.releaseForest();
                return 0.0f;
            }
            // Initialize matrix score calculator for validation
            Rf_matrix_score valid_scorer(config.num_labels, static_cast<uint8_t>(config.metric_score));

            for(uint16_t i = 0; i < validation_data.size(); i++){
                const Rf_sample& sample = validation_data[i];
                uint8_t actualLabel = sample.label;

                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                // Use all trees for validation prediction
                for(uint8_t t = 0; t < config.num_trees && t < forest_container.size(); t++){
                    uint8_t predict = forest_container[t].predict_features(sample.features);
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

                // Update validation metrics using matrix scorer
                valid_scorer.update_prediction(actualLabel, validPredictedLabel);
            }
            logger.m_log("get validation score");
            forest_container.releaseForest();
            validation_data.releaseData(true);

            // Calculate and return validation score
            return valid_scorer.calculate_score();
        }

        // Performs k-fold cross validation on train_data
        // train_data -> base_data, fold_train_data ~ train_data, fold_valid_data ~ validation_data
        float get_cross_validation_score(){
            RF_DEBUG(1, "Get k-fold cross validation score... ");

            if(config.k_folds < 2 || config.k_folds > 10){
                RF_DEBUG(0, "❌ Invalid k_folds value! Must be between 2 and 10.");
                return 0.0f;
            }

            uint16_t totalSamples = base_data.size();
            if(totalSamples < config.k_folds * config.num_labels * 2){
                RF_DEBUG(0, "❌ Not enough samples for k-fold cross validation!");
                return 0.0f;
            }
            Rf_matrix_score scorer(config.num_labels, config.metric_score);

            uint16_t foldSize = totalSamples / config.k_folds;
            float k_fold_score = 0.0f;
            logger.m_log("Perform k-fold");
            // Perform k-fold cross validation
            for(uint8_t fold = 0; fold < config.k_folds; fold++){
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
                train_data.loadData(base_data, fold_train_sampleIDs, true);      logger.m_log("load train_data");
                train_data.releaseData(false); 
                
                ClonesData();
                build_forest();
            
                validation_data.loadData();
                forest_container.loadForest();
                logger.m_log("fold evaluation");
                // Process all samples
                for(uint16_t i = 0; i < validation_data.size(); i++){
                    const Rf_sample& sample = validation_data[i];
                    uint8_t actual = sample.label;
                    uint8_t pred = forest_container.predict_features(sample.features);
                
                    if(actual < config.num_labels && pred < config.num_labels) {
                        scorer.update_prediction(actual, pred);
                    }
                }
                
                validation_data.releaseData();
                forest_container.releaseForest();
                
                k_fold_score += scorer.calculate_score();
            }
            k_fold_score /= config.k_folds;

            // Calculate and return k-fold score
            return k_fold_score;
        }
        
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

    // ---------------------------------- operations -----------------------------------
    public:
        // load forest into RAM
        bool loadForest() {
            bool success = forest_container.loadForest();
            if(success)
                RF_DEBUG_2(1, "✅ Forest loaded: ",config.num_trees, "trees. Total nodes: ", forest_container.get_total_nodes());
            else
                RF_DEBUG(0, "❌ Failed to load forest from LittleFS");
            return success;
        }
        
        // release forest from RAM to LittleFS
        bool releaseForest() {
            bool success = forest_container.releaseForest();
            if (!success) {
                RF_DEBUG(0, "❌ Failed to release forest to LittleFS");
            }else{
                RF_DEBUG_2(1, "✅ Forest released to LittleFS: ",config.num_trees, "trees. Total nodes: ", forest_container.get_total_nodes());
            }
            return success;
        }

        // Memory-Efficient Grid Search Training Function
        void training(int epochs = 99999) {
            size_t start = logger.drop_anchor();
            RF_DEBUG(0, "🌲 Starting training...");
            uint8_t min_ms = config.min_split_range.first;
            uint8_t max_ms = config.min_split_range.second;
            uint8_t min_md = config.max_depth_range.first;
            uint8_t max_md = config.max_depth_range.second;
            int total_combinations = ((max_ms - min_ms) / 2 + 1) * ((max_md - min_md) / 2 + 1);
            RF_DEBUG_2(1, "🔍 Hyperparameter tuning over ", total_combinations, "combinations","");
            int best_min_split = config.min_split;
            int best_max_depth = config.max_depth;

            float best_score = get_training_evaluation_index();
            
            char path[RF_PATH_BUFFER];
            Rf_data old_base_data;
            if(config.training_score == Rf_training_score::K_FOLD_SCORE){
                // convert train_data to base_data
                base.build_data_file_path(path, "temp_base_data");
                old_base_data.init(path, config.num_features);
                old_base_data = base_data; // backup
                base_data = train_data; 
                // init validation_data if not yet
                if(!validation_data.isProperlyInitialized()){
                    validation_data.init("/valid_data.bin", config.num_features);
                }
            }

            for(uint8_t min_split = min_ms; min_split <= max_ms; min_split += 2){
                for(uint8_t max_depth = min_md; max_depth <= max_md; max_depth += 2){
                    config.min_split = min_split;
                    config.max_depth = max_depth;
                    float score;
                    if(config.training_score == Rf_training_score::K_FOLD_SCORE){
                        // convert train_data to base_data
                        score = get_cross_validation_score();
                    }else{
                        build_forest();
                        score = get_training_evaluation_index();
                    }
                    RF_DEBUG_2(1, "Min_split: ", min_split, ", Max_depth: ", max_depth);
                    RF_DEBUG(1, " => Score: ", score);
                    RF_DEBUG(1, "best_score: ", best_score);
                    if(score > best_score){
                        RF_DEBUG(1, "🎉 New best score found!");
                        best_score = score;
                        best_min_split = min_split;
                        best_max_depth = max_depth;
                        config.result_score = best_score;
                        //save best forest
                        if(config.training_score != Rf_training_score::K_FOLD_SCORE){
                            // Save the best forest to LittleFS
                            forest_container.releaseForest(); // Release current trees from RAM
                        }
                    }
                    logger.m_log("epoch");
                    epochs--;
                    if(epochs <= 0) break;
                }
                if(epochs <= 0) break;
            }
            // Set config to best found parameters
            config.min_split = best_min_split;
            config.max_depth = best_max_depth;
            if constexpr (!ENABLE_TEST_DATA)
            config.result_score = best_score;

            // restore datas and rebuild model with best params if using K-FOLD
            if(config.training_score == Rf_training_score::K_FOLD_SCORE){
                train_data = base_data;     // restore train_data
                base_data = old_base_data;  // restore base_data
                old_base_data.purgeData();
                ClonesData();
                build_forest();
                forest_container.releaseForest();
            }
            RF_DEBUG(0,"🌲 Training complete.");
            RF_DEBUG_2(0, "Best parameters: min_split=", best_min_split, ", max_depth=", best_max_depth);
            RF_DEBUG(0, "Best score: ", best_score);

            size_t end = logger.drop_anchor();
            logger.t_log("total training time", start, end, "s", print_log);
            // clean_forest();
        }

        // Public API: predict() fills the provided label buffer with the predicted label and optionally returns the label index
        template<typename T>
        bool predict(const T& features, char* labelBuffer, size_t bufferSize, uint8_t* outLabel = nullptr) {
            static_assert(mcu::is_supported_vector<T>::value, "Unsupported type. Use mcu::vector or mcu::b_vector.");
            return predict(features.data(), features.size(), labelBuffer, bufferSize, outLabel);
        }

        bool predict(const float* features, size_t length, char* labelBuffer, size_t bufferSize, uint8_t* outLabel = nullptr) {
            const bool copyLabel = (labelBuffer != nullptr && bufferSize > 0);

            if (__builtin_expect(length != config.num_features, 0)) {
                RF_DEBUG(0, "❌ Feature length mismatch!","");
                if (copyLabel) {
                    labelBuffer[0] = '\0';
                }
                return false;
            }

            // Inline categorization to avoid packed_vector copy overhead
            packed_vector<2> c_features = categorizer.categorizeFeatures(features, length);
            uint8_t i_label = forest_container.predict_features(c_features);

            // Fast path: store label index first (most common operation)
            if (outLabel) {
                *outLabel = i_label;
            }

            // Deferred: only handle retraining if enabled (less common)
            if (__builtin_expect(config.enable_retrain, 0)) {
                Rf_sample sample;
                sample.features = std::move(c_features);
                sample.label = i_label;
                pending_data.add_pending_sample(sample, base_data);
            }

            // Fast path: if no label buffer requested, return early
            if (!copyLabel) {
                return true;
            }

            // Label string lookup and copy
            const char* labelPtr = nullptr;
            uint16_t labelLen = 0;
            if (__builtin_expect(!categorizer.getOriginalLabelView(i_label, &labelPtr, &labelLen), 0)) {
                labelBuffer[0] = '\0';
                return false;
            }

            // Optimized string copy
            if (labelLen >= bufferSize) {
                memcpy(labelBuffer, labelPtr, bufferSize - 1);
                labelBuffer[bufferSize - 1] = '\0';
                return false;
            }
            if (labelLen > 0) {
                memcpy(labelBuffer, labelPtr, labelLen);
            }
            labelBuffer[labelLen] = '\0';
            return true;
        }

        /**
         * @brief: Set feedback timeout in milliseconds.
         * If actual label feedback is not received within this timeout after the previous feedback,
         * the corresponding pending sample will be discarded.
         * @param timeout: Timeout duration in milliseconds.
         */
        void set_feedback_timeout(long unsigned timeout){
            pending_data.set_max_wait_time(timeout);
        }

        void add_actual_label(const char* label){
            if (!label) {
                return;
            }
            uint8_t i_label = categorizer.getNormalizedLabel(label);
            if(i_label < config.num_labels){
                pending_data.add_actual_label(i_label);
            } else {
                RF_DEBUG(1, "❌ Unknown label: ", label);
            }
        }

        // Add actual label into stack of pending data.
        template<typename T>
        void add_actual_label(const T& label) {
            using U = std::decay_t<T>;
            if constexpr (
                std::is_same_v<U, const char*> ||
                std::is_same_v<U, char*>
            ) {
                add_actual_label(static_cast<const char*>(label));
            } else if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_extent_t<T>, char>) {
                add_actual_label(&label[0]);
            } else if constexpr (std::is_integral_v<U>) {
                char buffer[32];
                long long value = static_cast<long long>(label);
                snprintf(buffer, sizeof(buffer), "%lld", value);
                add_actual_label(buffer);
            } else if constexpr (std::is_floating_point_v<U>) {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(label));
                add_actual_label(buffer);
            } else {
                static_assert(std::is_same_v<U, void>, "add_actual_label: unsupported label type");
            }
        }

        /**
         * @brief: Manual flush stack of pending data 
         * This function writes all pending samples with their actual labels (given from feedback) 
         * to the base dataset and logs the predictions to the inference log file.
         */
        void flush_pending_data() {
            pending_data.flush_pending_data(base_data);
        }

        /**
         * @brief: write all pending samples with their actual labels (given from feedback) to base dataset
         * This function does NOT log the predictions to the inference log file. 
         * sample stack and real label stack remains in pending_data after this operation.
         */
        void write_pending_data_to_dataset(){
            pending_data.write_to_base_data(base_data);
        }

        /**
         * @brief: Log all pending samples' predictions to the inference log file.
         * @note : sample stack and real label stack remains in pending_data after this operation.
         */
        void log_pending_data() {
            pending_data.write_to_infer_log();
        }

    // ----------------------------------------setters---------------------------------------

        void enable_retrain(){
            config.enable_retrain = true;
        }

        void disable_retrain(){
            config.enable_retrain = false;
        }

       // allow dataset to grow when new data is added, but limited to max_samples/max_dataset_size
        void enable_extend_base_data(){
            config.extend_base_data = true;
        }

        // keep dataset size fixed when new data is added (oldest data will be removed)
        void disable_extend_base_data(){
            config.extend_base_data = false;
        }

        void enable_auto_config(){
            config.enable_auto_config = true;
        }
        void disable_auto_config(){
            config.enable_auto_config = false;
        }

        // set impurity_threshold
        void set_impurity_threshold(float threshold) {
            config.impurity_threshold = threshold;
        }

        // set criterion
        void set_criterion(const char* criterion) {
            if (strcmp(criterion, "gini") == 0) {
                if(!config.use_gini){
                    config.use_gini = true;
                    config.impurity_threshold /= 4.0f; // adjust threshold for Gini
                }
            } else if (strcmp(criterion, "entropy") == 0) {
                if(config.use_gini){
                    config.use_gini = false;
                    config.impurity_threshold *= 4.0f; // adjust threshold for Entropy
                    if (config.impurity_threshold > 0.25f) {
                        config.impurity_threshold = 0.25f; // max for entropy
                    }
                }
            } else {
                RF_DEBUG(0, "❌ Invalid criterion! Use 'gini' or 'entropy'.");
            }
        }

        // overwrite metric_score
        void set_metric_score(Rf_metric_scores flag) {
            config.metric_score = flag;
        }

        //  combined metric_score with user input
        void add_metric_score(Rf_metric_scores flag) {
            config.metric_score |= flag;
        }

        // set training_score
        void set_training_score(Rf_training_score score) {
            config.training_score = score;
            config.validate_ratios();
        }

        void set_train_ratio(float ratio) {
            config.train_ratio = ratio;
            config.validate_ratios();
        }

        void set_valid_ratio(float ratio) {
            config.valid_ratio = ratio;
            config.validate_ratios();
        }

        // set random seed , default is 37
        void set_random_seed(uint32_t seed) {
            random_generator.seed(seed);
        }

        void use_default_seed() {
            random_generator.seed(0ULL);
        }
        //  rename model.  This action will rename all forest file components.
        void set_model_name(const char* name) {
            base.set_model_name(name);
        }
        void set_num_trees(uint8_t n_trees) {
            config.num_trees = n_trees;
        }
    // ----------------------------------------getters---------------------------------------
        /**
         * @brief: Calculate inference score based on the last N logged predictions.
         * @param num_inference: Number of recent predictions to consider for score calculation.
         * @param flag: Training flag indicating the scoring method (ACCURACY, PRECISSION, F1_SCORE, RECALL).
         * @note : if num_inference exceeds total logged predictions, all logged predictions will be used.
         */
        float get_last_n_inference_score(size_t num_inference, uint8_t flag) {
            char path[RF_PATH_BUFFER];
            base.get_infer_log_path(path, sizeof(path));
            if(!LittleFS.exists(path)) {
                RF_DEBUG(0, "❌ Inference log file does not exist: ", path);
                return 0.0f;
            }
            File file = LittleFS.open(path, FILE_READ);
            if(!file || !file.available()) {
                RF_DEBUG(0, "❌ Failed to open inference log file or file is empty: ", path );
                return 0.0f;
            }
            // Read and verify header - new format: magic (4) + prediction_count (4)
            uint8_t magic_bytes[4];
            uint32_t prediction_count;
            
            if(file.read(magic_bytes, 4) != 4) {
                RF_DEBUG(0, "❌ Failed to read magic number from inference log: ", path);
                file.close();
                return 0.0f;
            }
            
            if(file.read((uint8_t*)&prediction_count, 4) != 4) {
                RF_DEBUG(0, "❌ Failed to read prediction count from inference log: ", path);
                file.close();
                return 0.0f;
            }
            if(magic_bytes[0] != 0x49 || magic_bytes[1] != 0x4E || 
            magic_bytes[2] != 0x46 || magic_bytes[3] != 0x4C) {
                RF_DEBUG(0, "❌ Invalid magic number: ", path);
                file.close();
                return 0.0f;
            }
            
            if(prediction_count == 0) {
                RF_DEBUG(0, "⚠️ No predictions recorded in inference log.");
                file.close();
                return 0.0f;
            }
            
            // Initialize matrix score calculator
            Rf_matrix_score scorer(config.num_labels, flag);
            
            // Read prediction pairs: alternating predicted_label, actual_label
            for(uint32_t i = 0; i < prediction_count && i < num_inference; i++) {
                uint8_t predicted_label, actual_label;
                
                if(file.read(&predicted_label, 1) != 1) {
                    RF_DEBUG(1, "❌ Failed to read predicted label at prediction: ", i);
                    break;
                }
                
                if(file.read(&actual_label, 1) != 1) {
                    RF_DEBUG(1, "❌ Failed to read actual label at prediction: ", i);
                    break;
                }
                
                // Validate labels are within expected range
                if(predicted_label < config.num_labels && actual_label < config.num_labels) {
                    scorer.update_prediction(actual_label, predicted_label);
                }
            }
            
            file.close();
            
            // Calculate and return score based on training flags
            return scorer.calculate_score();
        }

        /**
         * @brief: Overloaded function to calculate inference score using current training flag.
         * @param num_inference: Number of recent predictions to consider for score calculation.
         * @note : if num_inference exceeds total logged predictions, all logged predictions will be used.
         */
        float get_last_n_inference_score(size_t num_inference) {
            return get_last_n_inference_score(num_inference, static_cast<uint8_t>(config.metric_score));
        }
        /**
         * @brief: Calculate practical inference score based on logged predictions and actual labels.
         * @param flag: Training flag indicating the scoring method (ACCURACY, PRECISSION, F1_SCORE, RECALL).
         */
        float get_practical_inference_score(uint8_t flag) {
            size_t total_logged = get_total_logged_inference();
            if(total_logged == 0) {
                RF_DEBUG(0, "⚠️ No logged inferences found for practical score calculation", "");
                return 0.0f;
            }
            return get_last_n_inference_score(total_logged, flag);
        }

        /**
         * @brief: Overloaded function to calculate practical inference score using current training flag.
         */
        float get_practical_inference_score() {
            return get_practical_inference_score(static_cast<uint8_t>(config.metric_score));
        }

        void get_model_name(char* name, size_t length) const {
            base.get_model_name(name, length);
        }

        bool get_label_view(uint8_t normalizedLabel, const char** outLabel, uint16_t* outLength = nullptr) const {
            return categorizer.getOriginalLabelView(normalizedLabel, outLabel, outLength);
        }

        size_t lowest_ram() const {
            return logger.lowest_ram;
        }
        size_t lowest_littlefs() const {
            return logger.lowest_rom;
        }

        // get total nodes in model (forest). Each node is 4 bytes
        size_t total_nodes() const {
            return forest_container.get_total_nodes();
        }
        
        //  get total leaves in model (forest)
        size_t total_leaves() const {
            return forest_container.get_total_leaves();
        }

        // get average nodes per tree
        float avg_nodes_per_tree() const {
            return forest_container.avg_nodes();
        }
        // get average leaves per tree
        float avg_leaves_per_tree() const {
            return forest_container.avg_leaves();
        }
        // get average depth per tree
        float avg_depth_per_tree() const {
            return forest_container.avg_depth();
        }

        // get tree which has maximum depth
        uint16_t max_depth_tree() const {
            return forest_container.max_depth_tree();
        }
        
        // get number of inference saved in log (which has actual label feedback)
        size_t get_total_logged_inference() const {
            char path[RF_PATH_BUFFER];
            base.get_infer_log_path(path, sizeof(path));
            if(!LittleFS.exists(path)) {
                RF_DEBUG(0, "❌ Inference log file does not exist: ", path);
                return 0;
            }   
            File file = LittleFS.open(path, FILE_READ);
            if(!file) {
                RF_DEBUG(0, "❌ Failed to open inference log file: ", path);
                return 0;
            }
            
            // Read header to get prediction count - new format: magic (4) + prediction_count (4)
            uint8_t magic_bytes[4];
            uint32_t prediction_count;
            
            if(file.read(magic_bytes, 4) != 4 || 
            file.read((uint8_t*)&prediction_count, 4) != 4) {
                file.close();
                return 0; // Failed to read header
            }
            
            // Verify magic number
            if(magic_bytes[0] != 0x49 || magic_bytes[1] != 0x4E || 
            magic_bytes[2] != 0x46 || magic_bytes[3] != 0x4C) {
                file.close();
                return 0; // Invalid header
            }
            
            file.close();
            return static_cast<size_t>(prediction_count);
        } 

        // methods for development stage - detailed metrics
    #ifdef DEV_STAGE
        // New combined prediction metrics function
        b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
            size_t start = logger.drop_anchor();
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
                uint8_t pred = forest_container.predict_features(sample.features);
                
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
            size_t end = logger.drop_anchor();
            logger.t_log("data prediction time", start, end, "ms", print_log);
        }

        // print metrix score on test set
        void training_report(){
            if(config.test_ratio == 0.0f || test_data.size() == 0){
                RF_DEBUG(0, "❌ No test set available for evaluation!", "");
                return;
            }
            auto result = predict(test_data);
            RF_DEBUG(0, "Precision in test set:");
            b_vector<pair<uint8_t, float>> precision = result[0];
            for (const auto& p : precision) {;
                RF_DEBUG_2(0, "Label: ", p.first, "- ", p.second);
            }
            float avgPrecision = 0.0f;
            for (const auto& p : precision) {
            avgPrecision += p.second;
            }
            avgPrecision /= precision.size();
            RF_DEBUG(0, "Avg: ", avgPrecision);

            // Calculate Recall
            RF_DEBUG(0, "Recall in test set:");
            b_vector<pair<uint8_t, float>> recall = result[1];
            for (const auto& r : recall) {
                RF_DEBUG_2(0, "Label: ", r.first, "- ", r.second);
            }
            float avgRecall = 0.0f;
            for (const auto& r : recall) {
            avgRecall += r.second;
            }
            avgRecall /= recall.size();
            RF_DEBUG(0, "Avg: ", avgRecall);

            // Calculate F1 Score;
            RF_DEBUG(0, "F1 Score in test set:");
            b_vector<pair<uint8_t, float>> f1_scores = result[2];
            for (const auto& f1 : f1_scores) {
                RF_DEBUG_2(0, "Label: ", f1.first, "- ", f1.second);
            }
            float avgF1 = 0.0f;
            for (const auto& f1 : f1_scores) {
            avgF1 += f1.second;
            }
            avgF1 /= f1_scores.size();
            RF_DEBUG(0, "Avg: ", avgF1);

            // Calculate Overall Accuracy
            b_vector<pair<uint8_t, float>> accuracies = result[3];
            float avgAccuracy = 0.0f;
            for (const auto& acc : accuracies) {
            avgAccuracy += acc.second;
            }
            avgAccuracy /= accuracies.size();

            // result score
            uint8_t total_scores = 0;
            float total_result_score = 0.0f;
            if(config.metric_score & Rf_metric_scores::PRECISION){
                total_result_score += avgPrecision;
                total_scores++;
            }
            if(config.metric_score & Rf_metric_scores::RECALL){
                total_result_score += avgRecall;
                total_scores++;
            }
            if(config.metric_score & Rf_metric_scores::F1_SCORE){
                total_result_score += avgF1;
                total_scores++;
            }
            if(config.metric_score & Rf_metric_scores::ACCURACY){
                total_result_score += avgAccuracy;
                total_scores++;
            }
            if(total_scores > 0){
                config.result_score = total_result_score / total_scores;
            } else {
                config.result_score = avgAccuracy; // fallback to accuracy
            }

            char path[RF_PATH_BUFFER];
            base.get_infer_log_path(path, sizeof(path));
            RF_DEBUG(0, "📊 FINAL SUMMARY:", "");
            RF_DEBUG(0, "Dataset: ", path);
            RF_DEBUG(0, "Average Precision: ", avgPrecision);
            RF_DEBUG(0, "Average Recall: ", avgRecall);
            RF_DEBUG(0, "Average F1-Score: ", avgF1);
            RF_DEBUG(0, "Accuracy: ", avgAccuracy);
            RF_DEBUG(0, "Result Score: ", config.result_score);
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
                uint8_t pred = forest_container.predict_features(sample.features);
                
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            data.releaseData();
            forest_container.releaseForest();
            
            return scorer.calculate_score();
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
                uint8_t pred = forest_container.predict_features(sample.features);
                
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            data.releaseData();
            forest_container.releaseForest();
            
            return scorer.calculate_score();
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
                uint8_t pred = forest_container.predict_features(sample.features);
                
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            data.releaseData();
            forest_container.releaseForest();
            
            return scorer.calculate_score();
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
                uint8_t pred = forest_container.predict_features(sample.features);
                
                if(actual < config.num_labels && pred < config.num_labels) {
                    scorer.update_prediction(actual, pred);
                }
            }
            
            data.releaseData();
            forest_container.releaseForest();
            
            return scorer.calculate_score();
        } 
        
        // printout predicted & actual label for each sample in test set
        void visual_result() {
            forest_container.loadForest(); // Ensure all trees are loaded before prediction
            test_data.loadData(); // Load test set data if not already loaded
            RF_DEBUG(0, "SampleID, Predicted, Actual");
            for (uint16_t i = 0; i < test_data.size(); i++) {
                const Rf_sample& sample = test_data[i];
                uint8_t pred = forest_container.predict_features(sample.features);
                Serial.printf("%d, %d, %d\n", i, pred, sample.label);
            }
            test_data.releaseData(true); // Release test set data after use
            forest_container.releaseForest(); // Release all trees after prediction
        }
    #endif
    
    };
} // End of namespace mcu
