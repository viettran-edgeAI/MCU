#pragma once

#include "Rf_components.h"
#include <cstdio>

namespace mcu{

    class RandomForest{
#if RF_ENABLE_TRAINING
        struct TrainingContext {
            Rf_data base_data;
            Rf_data train_data;
            Rf_data test_data;
            Rf_data validation_data;
            Rf_random random_generator;
            Rf_node_predictor node_pred;
            vector<ID_vector<uint16_t,2>> dataList;
            bool build_model;
            bool data_prepared;

            TrainingContext() : data_prepared(false), build_model(true) {}
        };

        TrainingContext* training_ctx = nullptr;
#endif

        Rf_base base;
        Rf_config config;
        Rf_logger logger;
        Rf_quantizer quantizer;
        Rf_tree_container forest_container;

        Rf_pending_data* pending_data = nullptr;
        Rf_data* base_data_stub = nullptr;

        // Optimization: Pre-allocated buffers for inference
        packed_vector<8> categorization_buffer;
        b_vector<uint16_t> threshold_cache;

#if RF_ENABLE_TRAINING
        inline TrainingContext* ensure_training_context(){
            if(!training_ctx){
                release_base_data_stub();
                training_ctx = new TrainingContext();
                training_ctx->node_pred.init(&base);
                training_ctx->random_generator.seed(config.random_seed);
            }
            return training_ctx;
        }

        inline void destroy_training_context(){
            if(training_ctx){
                delete training_ctx;
                training_ctx = nullptr;
            }
        }
#endif

        inline Rf_pending_data* ensure_pending_data(){
            if(!pending_data){
                pending_data = new Rf_pending_data();
                pending_data->init(&base, &config);
            }
            return pending_data;
        }

        inline void release_pending_data(){
            if(pending_data){
                delete pending_data;
                pending_data = nullptr;
            }
        }

        inline Rf_data* ensure_base_data_stub(){
#if RF_ENABLE_TRAINING
            if(training_ctx){
                if(training_ctx->base_data.isProperlyInitialized()){
                    return &training_ctx->base_data;
                }
            }
#endif
            if(!base_data_stub){
                base_data_stub = new Rf_data();
                char base_path[RF_PATH_BUFFER];
                base.get_base_data_path(base_path, sizeof(base_path));
                if(config.num_features > 0){
                    base_data_stub->init(base_path, config);
                } else {
                    base_data_stub->setfile_path(base_path);
                }
            }
            return base_data_stub;
        }

        inline void release_base_data_stub(){
            if(base_data_stub){
                delete base_data_stub;
                base_data_stub = nullptr;
            }
        }

    public:
        RandomForest(){};
        RandomForest(const char* model_name) {
            init(model_name);
        }

        void init(const char* model_name){
        #if defined(ESP32) && (RF_DEBUG_LEVEL > 0)
            // Check stack size to prevent overflow on ESP32-C3
            UBaseType_t stackRemaining = uxTaskGetStackHighWaterMark(NULL);
            size_t stackBytes = stackRemaining * sizeof(StackType_t);
            if(stackBytes < 2048) {
                RF_DEBUG_2(0, "⚠️ WARNING: Low stack space (", stackBytes, "bytes", ". May cause crash!");
                RF_DEBUG(0, "   Solution: Increase CONFIG_ARDUINO_LOOP_STACK_SIZE to 16384");
                RF_DEBUG(0, "   See docs/ESP32_Stack_Fix.md for details");
            }
        #endif
            // initial components
            base.init(model_name);      // base must be init first

            logger.init(&base);
            config.init(&base);
            quantizer.init(&base);
            forest_container.init(&base, &config);               

            // load resources
            config.loadConfig();
            quantizer.loadQuantizer();  // Load quantizer first to get quantization_coefficient
            
            // Synchronize quantization_coefficient from quantizer to config if not already set
            if (quantizer.loaded() && config.quantization_coefficient != quantizer.getQuantizationCoefficient()) {
                config.quantization_coefficient = quantizer.getQuantizationCoefficient();
                RF_DEBUG(1, "✅ Synchronized quantization_coefficient: ", config.quantization_coefficient);
            }

            if(config.enable_retrain){
                ensure_pending_data();
                ensure_base_data_stub();
            }

            // Initialize inference optimization buffers
            categorization_buffer.set_bits_per_value(config.quantization_coefficient);
            categorization_buffer.resize(config.num_features, 0);
            buildThresholdCandidates(config.quantization_coefficient, threshold_cache);
            if (threshold_cache.empty()) {
                threshold_cache.push_back(0);
            }
        }
        
        // Enhanced destructor
        ~RandomForest(){
            end_training_session();
            forest_container.releaseForest();
            release_base_data_stub();
            release_pending_data();
        }

        bool begin_training_session(){
#if RF_ENABLE_TRAINING
            auto ctx = ensure_training_context();
            ctx->data_prepared = false;
            return true;
#else
            return false;
#endif
        }

        void end_training_session(){
#if RF_ENABLE_TRAINING
            cleanup_training_data();
            destroy_training_context();
#endif
            release_base_data_stub();
        }

        
        void cleanup_training_data(){
#if RF_ENABLE_TRAINING
            if(!training_ctx){
                return;
            }

            RF_DEBUG(0, "🧹 Cleaning up training session... ");

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

            // Purge all Rf_data to free resources
            training_ctx->base_data.purgeData();
            training_ctx->train_data.purgeData();
            training_ctx->test_data.purgeData();
            if(config.use_validation())
            training_ctx->validation_data.purgeData();
            training_ctx->dataList.clear();

            // re_train node predictor after training session
            if(!training_ctx->build_model){
                training_ctx->node_pred.re_train(); 
            }else{
                training_ctx->node_pred.flush_buffer();
            }
#endif
        }

        bool build_model(){
#if RF_ENABLE_TRAINING
            RF_DEBUG(0, "🌲 Building model... ");
            if(!base.able_to_training()){
                RF_DEBUG(0, "❌ Model not set for training");
                return false;
            }

            if(!begin_training_session()){
                RF_DEBUG(0, "❌ Unable to allocate training context");
                return false;
            }

            bool success = true;
            auto* ctx = training_ctx;

            do {
                if(!ctx){
                    success = false;
                    break;
                }

                if(!prepare_forest_building_resource()){
                    success = false;
                    break;
                }

                // build forest
                if(!build_forest()){
                    RF_DEBUG(0, "❌ Error building forest");
                    success = false;
                }
            } while(false);

        #ifdef DEV_STAGE
            // Print model report on test data after building
            if(success){
                model_report();
            }
        #endif
            end_training_session();
            return success;
#else
            RF_DEBUG(0, "❌ Training disabled (RF_ENABLE_TRAINING = 0)");
            return false;
#endif
        }

    private:
#if RF_ENABLE_TRAINING
        bool build_forest(){
            auto* ctx = training_ctx;
            if(!ctx){
                return false;
            }

            size_t start = logger.drop_anchor();
            // Clear any existing forest first
            forest_container.clearForest();
            logger.m_log("start building forest");

            uint16_t estimated_nodes = ctx->node_pred.estimate_nodes(config);
            uint16_t peak_nodes = ctx->node_pred.queue_peak_size(config);
            auto& queue_nodes = forest_container.getQueueNodes();
            queue_nodes.clear();
            queue_nodes.reserve(peak_nodes); // Conservative estimate
            RF_DEBUG(2, "🌳 Estimated nodes per tree: ", estimated_nodes);

            if(!ctx->train_data.loadData()){
                RF_DEBUG(0, "❌ Error loading training data");
                return false;
            }
            logger.m_log("load train_data");
            for(uint8_t i = 0; i < config.num_trees; i++){
                Rf_tree tree(i);
                tree.nodes.reserve(estimated_nodes); // Reserve memory for nodes based on prediction
                queue_nodes.clear();                // Clear queue for each tree
                buildTree(tree, ctx->dataList[i], queue_nodes);
                tree.isLoaded = true;
                forest_container.add_tree(std::move(tree));
                logger.m_log("tree creation");
            }
            ctx->train_data.releaseData(); 
            forest_container.is_loaded = false;
            // forest_container.set_to_individual_form();
            ctx->node_pred.add_new_samples(config.min_split, config.max_depth, forest_container.avg_nodes());

            RF_DEBUG_2(0, "🌲 Forest built successfully: ", forest_container.get_total_nodes(), "nodes","");
            RF_DEBUG_2(1, "Min split: ", config.min_split, "- Max depth: ", config.max_depth);
            size_t end = logger.drop_anchor();
            long unsigned dur = logger.t_log("forest building time", start, end, "s");
            RF_DEBUG_2(1, "⏱️  Forest building time: ", dur, "s","");
            return true;
        }
        
        bool prepare_forest_building_resource(){
            auto* ctx = training_ctx;
            if(!ctx){
                return false;
            }

            // clone base_data to temp_base_data to avoid modifying original data
            char base_path[RF_PATH_BUFFER];
            base.get_base_data_path(base_path, sizeof(base_path));
            cloneFile(base_path, temp_base_data);
            if(!ctx->base_data.init(temp_base_data, config)){
                RF_DEBUG(0, "❌ Error initializing base data");
                return false;
            }

            // initialize data components
            ctx->dataList.clear();
            ctx->dataList.reserve(config.num_trees);
            char path[RF_PATH_BUFFER];

            base.build_data_file_path(path, "train_data");
            ctx->train_data.init(path, config);

            base.build_data_file_path(path, "test_data");
            ctx->test_data.init(path, config);

            if(config.use_validation()){
                base.build_data_file_path(path, "valid_data");
                ctx->validation_data.init(path, config);
            }

            // data splitting
            vector<pair<float, Rf_data*>> dest;
            dest.reserve(3);
            dest.push_back(make_pair(config.train_ratio, &ctx->train_data));
            dest.push_back(make_pair(config.test_ratio, &ctx->test_data));
            if(config.use_validation()){
                dest.push_back(make_pair(config.valid_ratio, &ctx->validation_data));
            }
            if(!splitData(ctx->base_data, dest)){
                return false;
            }
            dest.clear();
            ClonesData();
            return true;
        }

        // ----------------------------------------------------------------------------------
        // Split data into training and testing sets. Dest data must be init() before called by this function
        bool splitData(Rf_data& source, vector<pair<float, Rf_data*>>& dest) {
            auto* ctx = training_ctx;
            if(!ctx){
                return false;
            }
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
                    uint16_t sampleId = static_cast<uint16_t>(ctx->random_generator.bounded(static_cast<uint32_t>(maxID)));
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
            long unsigned dur = logger.t_log("split time", start, end, "s");
            RF_DEBUG_2(1, "⏱️  Data splitting time: ", dur, "s","");
            return true;
        }
        
        void ClonesData() {
            auto* ctx = training_ctx;
            if(!ctx){
                return;
            }
            size_t start = logger.drop_anchor();
            RF_DEBUG(1, "🔀 Cloning data for each tree...");
            ctx->dataList.clear();
            ctx->dataList.reserve(config.num_trees);
            uint16_t numSample = ctx->train_data.size();
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
                    auto tree_rng = ctx->random_generator.deriveRNG(i, nonce);

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
                    uint64_t h = ctx->random_generator.hashIDVector(sub_data);
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
                        seen_hashes.insert(ctx->random_generator.hashIDVector(sub_data));
                        break;
                    }
                }
                ctx->dataList.push_back(std::move(sub_data));
                logger.m_log("clone sub_data");
            }

            logger.m_log("clones data");  
            size_t end = logger.drop_anchor();
            long unsigned dur = logger.t_log("clones data time", start, end, "ms");
            RF_DEBUG_2(1, "🎉 Created ", ctx->dataList.size(), "datasets for trees","");
            RF_DEBUG_2(1, "⏱️  Created datasets time: ", dur, "ms","");
        }  
        
        typedef struct SplitInfo {
            float gain = -1.0f;
            uint16_t featureID = 0;
            uint8_t thresholdSlot = 0;  // Changed from threshold to thresholdSlot (0-7)
            uint16_t thresholdValue = 0; // Actual threshold value for splitting
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
            auto* ctx = training_ctx;
            if(!ctx){
                return bestSplit;
            }
            uint16_t totalSamples = (begin < end) ? (end - begin) : 0;
            if (totalSamples < 2) return bestSplit;

            // Get quantization info
            uint8_t quant_bits = config.quantization_coefficient;
            uint16_t maxFeatureValue = (quant_bits >= 8) ? 255 : ((1u << quant_bits) - 1);
            uint8_t numCandidates = static_cast<uint8_t>(threshold_cache.size());

            // Base label counts
            b_vector<uint16_t> baseLabelCounts(numLabels, 0);
            for (uint16_t k = begin; k < end; ++k) {
                uint16_t sid = indices[k];
                if (sid < ctx->train_data.size()) {
                    uint8_t lbl = ctx->train_data.getLabel(sid);
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

            // Pre-allocate count arrays (for up to 256 unique values)
            const uint16_t numPossibleValues = maxFeatureValue + 1;
            b_vector<uint16_t> counts;
            counts.resize(numPossibleValues * numLabels, 0);
            
            // Fast path for 1-bit quantization (only 2 values: 0 and 1)
            if (quant_bits == 1) {
                for (const auto& featureID : selectedFeatures) {
                    // Reset counts
                    for (size_t i = 0; i < counts.size(); i++) counts[i] = 0;
                    
                    // Collect counts for value 0 and value 1
                    for (uint16_t k = begin; k < end; ++k) {
                        uint16_t sid = indices[k];
                        if (sid < ctx->train_data.size()) {
                            uint8_t lbl = ctx->train_data.getLabel(sid);
                            if (lbl < numLabels) {
                                uint16_t fv = ctx->train_data.getFeature(sid, featureID);
                                if (fv <= 1) {
                                    counts[fv * numLabels + lbl]++;
                                }
                            }
                        }
                    }
                    
                    // Single threshold: 0 (left) vs 1 (right)
                    uint32_t leftTotal = 0, rightTotal = 0;
                    b_vector<uint16_t> leftCounts(numLabels, 0), rightCounts(numLabels, 0);
                    
                    // Left side: value == 0
                    for (uint8_t label = 0; label < numLabels; label++) {
                        leftCounts[label] = counts[label];  // value 0
                        leftTotal += leftCounts[label];
                        rightCounts[label] = counts[numLabels + label];  // value 1
                        rightTotal += rightCounts[label];
                    }
                    
                    if (leftTotal == 0 || rightTotal == 0) continue;
                    
                    // Calculate impurity and gain
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
                        bestSplit.thresholdSlot = 0;  // Only one threshold slot for 1-bit
                        bestSplit.thresholdValue = 0;  // Threshold value: 0 (<=0 goes left, >0 goes right)
                    }
                }
            } else {
                // General case: multi-bit quantization with multiple threshold candidates
                for (const auto& featureID : selectedFeatures) {
                    // Reset counts for this feature
                    for (size_t i = 0; i < counts.size(); i++) counts[i] = 0;
                    
                    b_vector<uint32_t> value_totals(numPossibleValues, 0);

                    // Collect feature value distributions
                    for (uint16_t k = begin; k < end; ++k) {
                        uint16_t sid = indices[k];
                        if (sid < ctx->train_data.size()) {
                            uint8_t lbl = ctx->train_data.getLabel(sid);
                            if (lbl < numLabels) {
                                uint16_t fv = ctx->train_data.getFeature(sid, featureID);
                                if (fv <= maxFeatureValue) {
                                    counts[fv * numLabels + lbl]++;
                                    value_totals[fv]++;
                                }
                            }
                        }
                    }

                    // Try each threshold candidate
                    for (uint8_t slot = 0; slot < numCandidates; slot++) {
                        uint16_t threshold = threshold_cache[slot];
                        
                        uint32_t leftTotal = 0, rightTotal = 0;
                        b_vector<uint16_t> leftCounts(numLabels, 0), rightCounts(numLabels, 0);
                        
                        // Split samples based on threshold
                        for (uint16_t value = 0; value <= maxFeatureValue; value++) {
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
                        bestSplit.thresholdSlot = slot;
                        bestSplit.thresholdValue = threshold;
                    }
                }
            }
            }  // End of else block for multi-bit quantization
            return bestSplit;
        }

        // Breadth-first tree building for optimal node layout - MEMORY OPTIMIZED
        void buildTree(Rf_tree& tree, ID_vector<uint16_t,2>& sampleIDs, b_vector<NodeToBuild>& queue_nodes) {
            auto* ctx = training_ctx;
            if(!ctx){
                return;
            }
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
                stats.analyzeSamples(indices, current.begin, current.end, config.num_labels, ctx->train_data);

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
                    uint16_t t = static_cast<uint16_t>(ctx->random_generator.bounded(j + 1));
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
                tree.nodes[current.nodeIndex].setThresholdSlot(bestSplit.thresholdSlot);
                tree.nodes[current.nodeIndex].setIsLeaf(false);
                
                // In-place partition of indices[current.begin, current.end) using actual threshold value
                uint16_t iLeft = current.begin;
                for (uint16_t k = current.begin; k < current.end; ++k) {
                    uint16_t sid = indices[k];
                    if (sid < ctx->train_data.size() && 
                        ctx->train_data.getFeature(sid, bestSplit.featureID) <= bestSplit.thresholdValue) {
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

            auto* ctx = training_ctx;
            if(!ctx){
                return 0.0f;
            }

            // Check if we have trained trees and data
            if(ctx->dataList.empty()){
                RF_DEBUG(0, "❌ No sub_data for validation");
                return 0.0f;
            }

            // Determine chunk size for memory-efficient processing
            uint16_t buffer_chunk = ctx->train_data.samplesPerChunk();

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
            for(size_t chunk_index = 0; chunk_index < ctx->train_data.total_chunks(); chunk_index++){
                train_samples_buffer.loadChunk(ctx->train_data, chunk_index, true);
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
                    for(uint8_t treeIdx = 0; treeIdx < config.num_trees && treeIdx < ctx->dataList.size(); treeIdx++){
                        // Check if this sample ID is NOT in the tree's training data
                        if(!ctx->dataList[treeIdx].contains(sampleID)){
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
                            uint8_t predict = forest_container[treeIdx].predict_features(sample.features, threshold_cache);
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
            auto* ctx = training_ctx;
            if(!ctx){
                return 0.0f;
            }
            if(!config.use_validation()){
                RF_DEBUG(1, "❌ Validation not enabled in config");
                return 0.0f;
            }
            // Load forest into memory for evaluation
            if(!forest_container.loadForest()){
                RF_DEBUG(0,"❌ Failed to load forest for validation evaluation!");
                return 0.0f;
            }
            if(!ctx->validation_data.loadData()){
                RF_DEBUG(0,"❌ Failed to load validation data for evaluation!");
                forest_container.releaseForest();
                return 0.0f;
            }
            // Initialize matrix score calculator for validation
            Rf_matrix_score valid_scorer(config.num_labels, static_cast<uint8_t>(config.metric_score));

            for(uint16_t i = 0; i < ctx->validation_data.size(); i++){
                const Rf_sample& sample = ctx->validation_data[i];
                uint8_t actualLabel = sample.label;

                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                // Use all trees for validation prediction
                for(uint8_t t = 0; t < config.num_trees && t < forest_container.size(); t++){
                    uint8_t predict = forest_container[t].predict_features(sample.features, threshold_cache);
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
            ctx->validation_data.releaseData(true);

            // Calculate and return validation score
            return valid_scorer.calculate_score();
        }

        // Performs k-fold cross validation on train_data
        // train_data -> base_data, fold_train_data ~ train_data, fold_valid_data ~ validation_data
        float get_cross_validation_score(){
            RF_DEBUG(1, "Get k-fold cross validation score... ");
            auto* ctx = training_ctx;
            if(!ctx){
                return 0.0f;
            }

            if(config.k_folds < 2 || config.k_folds > 10){
                RF_DEBUG(0, "❌ Invalid k_folds value! Must be between 2 and 10.");
                return 0.0f;
            }

            uint16_t totalSamples = ctx->base_data.size();
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
                ctx->validation_data.loadData(ctx->base_data, fold_valid_sampleIDs, true);
                ctx->validation_data.releaseData(false);
                ctx->train_data.loadData(ctx->base_data, fold_train_sampleIDs, true);      logger.m_log("load train_data");
                ctx->train_data.releaseData(false); 
                
                ClonesData();
                build_forest();
            
                ctx->validation_data.loadData();
                forest_container.loadForest();
                logger.m_log("fold evaluation");

                // Process all samples
                for(uint16_t i = 0; i < ctx->validation_data.size(); i++){
                    const Rf_sample& sample = ctx->validation_data[i];
                    uint8_t actual = sample.label;
                    uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
                    if(actual < config.num_labels && pred < config.num_labels) {
                        scorer.update_prediction(actual, pred);
                    }
                }
                
                ctx->validation_data.releaseData();
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
#endif

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
#if RF_ENABLE_TRAINING
            if(!base.able_to_training()){
                RF_DEBUG(0, "❌ Model not set for training");
                return;
            }

            if(!begin_training_session()){
                RF_DEBUG(0, "❌ Unable to allocate training context");
                return;
            }

            auto* ctx = training_ctx;
            if(!ctx){
                end_training_session();
                return;
            }

            if(!prepare_forest_building_resource()){
                end_training_session();
                return;
            }
            training_ctx->build_model = false;

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
                base.build_data_file_path(path, "temp_base_data");
                old_base_data.init(path, config);
                old_base_data = ctx->base_data; // backup
                ctx->base_data = ctx->train_data; 
                if(!ctx->validation_data.isProperlyInitialized()){
                    ctx->validation_data.init("/valid_data.bin", config);
                }
            }

            for(uint8_t min_split = min_ms; min_split <= max_ms; min_split += 2){
                for(uint8_t max_depth = min_md; max_depth <= max_md; max_depth += 2){
                    config.min_split = min_split;
                    config.max_depth = max_depth;
                    float score;
                    if(config.training_score == Rf_training_score::K_FOLD_SCORE){
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
                        if(config.training_score != Rf_training_score::K_FOLD_SCORE){
                            forest_container.releaseForest();
                        }
                    }
                    logger.m_log("epoch");
                    epochs--;
                    if(epochs <= 0) break;
                }
                if(epochs <= 0) break;
            }

            config.min_split = best_min_split;
            config.max_depth = best_max_depth;
            if constexpr (!ENABLE_TEST_DATA)
                config.result_score = best_score;

            if(config.training_score == Rf_training_score::K_FOLD_SCORE){
                ctx->train_data = ctx->base_data;     // restore train_data
                ctx->base_data = old_base_data;       // restore base_data
                old_base_data.purgeData();
                ClonesData();
                build_forest();
                forest_container.releaseForest();
            }
            RF_DEBUG(0,"🌲 Training complete.");
            RF_DEBUG_2(0, "Best parameters: min_split=", best_min_split, ", max_depth=", best_max_depth);
            RF_DEBUG(0, "Best score: ", best_score);

            size_t end = logger.drop_anchor();
            long unsigned dur = logger.t_log("total training time", start, end, "s");
            RF_DEBUG_2(0, "⏱️ Total training time: ", dur / 1000.0f, " seconds","");

        #ifdef DEV_STAGE
            model_report();
        #endif
            end_training_session();
#else
            (void)epochs;
            RF_DEBUG(0, "❌ Training disabled (RF_ENABLE_TRAINING = 0)");
#endif
        }

        // Public API: predict() fills the provided label buffer with the predicted label
        template<typename T>
        bool predict(const T& features, char* labelBuffer, size_t bufferSize) {
            static_assert(mcu::is_supported_vector<T>::value, "Unsupported type. Use mcu::vector or mcu::b_vector.");
            return predict(features.data(), features.size(), labelBuffer, bufferSize);
        }

        bool predict(const float* features, size_t length, char* labelBuffer, size_t bufferSize) {
            const bool copyLabel = (labelBuffer != nullptr && bufferSize > 0);

            if (__builtin_expect(length != config.num_features, 0)) {
                RF_DEBUG(0, "❌ Feature length mismatch!","");
                if (copyLabel) {
                    labelBuffer[0] = '\0';
                }
                return false;
            }

            // Optimized: write directly to pre-allocated buffer
            quantizer.quantizeFeatures(features, categorization_buffer);
            return predict(categorization_buffer, labelBuffer, bufferSize);
        }

        bool predict(const packed_vector<8>& c_features, char* labelBuffer, size_t bufferSize) {
            const bool copyLabel = (labelBuffer != nullptr && bufferSize > 0);

            // Use pre-computed threshold cache
            uint8_t i_label = forest_container.predict_features(c_features, threshold_cache);

            if (__builtin_expect(config.enable_retrain, 0)) {
                Rf_sample sample(c_features, i_label);
                if(auto* pd = ensure_pending_data()){
                    if(Rf_data* base_handle = ensure_base_data_stub()){
                        pd->add_pending_sample(sample, *base_handle);
                    }
                }
            }

            if (!copyLabel) {
                return true;
            }

            const char* labelPtr = nullptr;
            uint16_t labelLen = 0;
            if (__builtin_expect(!quantizer.getOriginalLabelView(i_label, &labelPtr, &labelLen), 0)) {
                labelBuffer[0] = '\0';
                return false;
            }

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

        // Convenience overload: returns the internal label index directly
        uint8_t predict(const float* features, size_t length) {
            if (__builtin_expect(length != config.num_features, 0)) {
                RF_DEBUG(0, "❌ Feature length mismatch!","");
                return 255;
            }
            quantizer.quantizeFeatures(features, categorization_buffer);
            return forest_container.predict_features(categorization_buffer, threshold_cache);
        }

        template<typename T>
        uint8_t predict(const T& features) {
            static_assert(mcu::is_supported_vector<T>::value, "Unsupported type. Use mcu::vector or mcu::b_vector.");
            return predict(features.data(), features.size());
        }

        /**
         * @brief: Set feedback timeout in milliseconds.
         * If actual label feedback is not received within this timeout after the previous feedback,
         * the corresponding pending sample will be discarded.
         * @param timeout: Timeout duration in milliseconds.
         */
        void set_feedback_timeout(long unsigned timeout){
            Rf_pending_data* pd = config.enable_retrain ? ensure_pending_data() : pending_data;
            if(pd){
                pd->set_max_wait_time(timeout);
            }
        }
#ifdef ARDUINO
        void add_actual_label(String label) {
            add_actual_label(label.c_str());
        }
#else
        void add_actual_label(std::string label) {
            add_actual_label(label.c_str());
        }
#endif

        void add_actual_label(const char* label){
            if (!label) {
                return;
            }
            uint8_t i_label = quantizer.getNormalizedLabel(label);
            Rf_pending_data* pd = config.enable_retrain ? ensure_pending_data() : pending_data;
            if(!pd){
                return;
            }
            if(i_label < config.num_labels){
                pd->add_actual_label(i_label);
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
            Rf_pending_data* pd = config.enable_retrain ? ensure_pending_data() : pending_data;
            if(!pd){
                return;
            }
            if(Rf_data* base_handle = ensure_base_data_stub()){
                pd->flush_pending_data(*base_handle);
            }
        }

        /**
         * @brief: write all pending samples with their actual labels (given from feedback) to base dataset
         * This function does NOT log the predictions to the inference log file. 
         * sample stack and real label stack remains in pending_data after this operation.
         */
        void write_pending_data_to_dataset(){
            Rf_pending_data* pd = config.enable_retrain ? ensure_pending_data() : pending_data;
            if(!pd){
                return;
            }
            if(Rf_data* base_handle = ensure_base_data_stub()){
                pd->write_to_base_data(*base_handle);
            }
        }

        /**
         * @brief: Log all pending samples' predictions to the inference log file.
         * @note : sample stack and real label stack remains in pending_data after this operation.
         */
        void log_pending_data() {
            Rf_pending_data* pd = config.enable_retrain ? ensure_pending_data() : pending_data;
            if(pd){
                pd->write_to_infer_log();
            }
        }

    // ----------------------------------------setters---------------------------------------

        void enable_retrain(){
            config.enable_retrain = true;
            ensure_pending_data();
            ensure_base_data_stub();
        }

        void disable_retrain(){
            config.enable_retrain = false;
            release_pending_data();
            release_base_data_stub();
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
            config.random_seed = seed;
#if RF_ENABLE_TRAINING
            if(training_ctx){
                training_ctx->random_generator.seed(seed);
            }
#endif
        }

        void use_default_seed() {
            set_random_seed(0ULL);
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

        uint8_t get_quantization_coefficient() const {
            return config.quantization_coefficient;
        }

        void get_model_name(char* name, size_t length) const {
            base.get_model_name(name, length);
        }

        bool get_label_view(uint8_t normalizedLabel, const char** outLabel, uint16_t* outLength = nullptr) const {
            return quantizer.getOriginalLabelView(normalizedLabel, outLabel, outLength);
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
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
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
            long unsigned dur = logger.t_log("data prediction time", start, end, "ms");
            RF_DEBUG_2(1, "⏱️ Data prediction time: ", dur / 1000.0f, " seconds","");
        }

        // print metrix score on test set
        void model_report(){
#if RF_ENABLE_TRAINING
            auto* ctx = training_ctx;
            if(!ctx || config.test_ratio == 0.0f || ctx->test_data.size() == 0){
                RF_DEBUG(0, "❌ No test set available for evaluation!", "");
                return;
            }
            auto result = predict(ctx->test_data);
#else
            RF_DEBUG(0, "❌ Training disabled (RF_ENABLE_TRAINING = 0)");
            return;
#endif
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
            RF_DEBUG(0, "Lowest RAM: ", logger.lowest_ram);
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
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
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
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
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
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
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
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                
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
#if RF_ENABLE_TRAINING
            auto* ctx = training_ctx;
            if(!ctx){
                RF_DEBUG(0, "❌ No training context available for visual_result!", "");
                return;
            }
            forest_container.loadForest(); // Ensure all trees are loaded before prediction
            ctx->test_data.loadData(); // Load test set data if not already loaded

            RF_DEBUG(0, "Predicted, Actual");
            for (uint16_t i = 0; i < ctx->test_data.size(); i++) {
                const Rf_sample& sample = ctx->test_data[i];
                uint8_t pred = forest_container.predict_features(sample.features, threshold_cache);
                RF_DEBUG_2(0, String(pred).c_str(), ", ", String(sample.label).c_str(), "");
            }
            ctx->test_data.releaseData(true); // Release test set data after use
            forest_container.releaseForest(); // Release all trees after prediction
#else
            RF_DEBUG(0, "❌ Training disabled (RF_ENABLE_TRAINING = 0)");
#endif
        }
    #endif
    
    };
} // End of namespace mcu