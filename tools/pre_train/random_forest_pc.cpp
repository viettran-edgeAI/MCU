#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#define mkdir _mkdir
#endif

#include "pc_components.h"

#define VERSION "1.2.0"
#define temp_base_data "base_data.csv"

using namespace mcu;

std::string extract_model_name(const std::string& data_path) {
    size_t last_slash = data_path.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) ? data_path.substr(last_slash + 1) : data_path;
    size_t first_underscore = filename.find("_nml");
    return (first_underscore != std::string::npos) ? filename.substr(0, first_underscore) : filename;
}



class RandomForest{
public:
    Rf_data base_data;      // base data / baseFile
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data; // validation data, used for evaluating the model

    std::string model_name;

    Rf_config config;
    node_predictor pre;

private:
    vector<Rf_tree> root;                     // b_vector storing root nodes of trees (now manages SPIFFS filenames)
    b_vector<ID_vector<uint16_t,2>> dataList; // list of training data sample IDs for each tree
    Rf_random rng;

    std::string node_log_path;
    std::string node_predictor_path;
    std::string result_config_path;

public:

    // RandomForest(){};
    RandomForest() : config(config_path) {
        rng = Rf_random(config.random_seed, true); // Initialize RNG with loaded seed
        model_name = extract_model_name(config.data_path);
        std::cout << "🌲 Model name: " << model_name << std::endl;
        generateFilePaths();
        createDataBackup(config.data_path, temp_base_data);     // create a backup to avoid damaging the original data
        config.init(temp_base_data); // Load configuration from model_config.json first
        base_data.loadCSVData(temp_base_data, config.num_features);
        
        // OOB.reserve(numTree);
        dataList.reserve(config.num_trees);

        splitData(config.train_ratio);

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
        
        // Delete temporary backup file
        if (std::remove(temp_base_data) == 0) {
            std::cout << "🗑️ Removed temporary backup file: " << temp_base_data << std::endl;
        }

        // train node predictor
        pre.init(node_log_path);
        pre.train();
        float pre_ac = pre.get_accuracy();
        // Fix: get_accuracy() already returns percentage (0-100), don't multiply by 100 again
        pre.accuracy = static_cast<uint8_t>(std::min(100.0f, std::max(0.0f, pre_ac)));
        std::cout << "node predictor accuracy: " << pre_ac << std::endl;
        pre.save_model(node_predictor_path);
    }

    void MakeForest(){
        root.clear();
        root.reserve(config.num_trees);
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            // For PC training, no SPIFFS filename needed
            Rf_tree tree("");
            build_tree(tree, dataList[i]);
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
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxDepth) maxDepth = depth;
            if(depth < minDepth) minDepth = depth;
            
            // std::cout << "Tree " << (int)i << ": " 
            //           << nodeCount << " nodes (" 
            //           << leafCount << " leaves), depth " 
            //           << depth << "\n";
        }
        
        std::cout << "----------------------------------------\n";
        std::cout << "Total trees: " << (int)config.num_trees << "\n";
        std::cout << "Total nodes: " << totalNodes << "\n";
        std::cout << "Total leaf nodes: " << totalLeafNodes << "\n";
        std::cout << "Average nodes per tree: " << (float)totalNodes / config.num_trees << "\n";
        std::cout << "Average leaf nodes per tree: " << (float)totalLeafNodes / config.num_trees << "\n";
        std::cout << "Depth range: " << minDepth << " - " << maxDepth << "\n";
        std::cout << "Average depth: " << (float)(maxDepth + minDepth) / 2.0f << "\n";
        std::cout << "----------------------------------------\n";
    }
    
    private:
    // Create a backup copy of the original CSV data to avoid damaging the original
    void createDataBackup(const std::string& source_path, const std::string& backup_filename) {
        std::ifstream source(source_path, std::ios::binary);
        if (!source.is_open()) {
            std::cout << "⚠️ Warning: Could not open source file for backup: " << source_path << std::endl;
            return;
        }
        
        std::ofstream backup(backup_filename, std::ios::binary);
        if (!backup.is_open()) {
            std::cout << "⚠️ Warning: Could not create backup file: " << backup_filename << std::endl;
            source.close();
            return;
        }
        
        // Copy the file
        backup << source.rdbuf();
        
        source.close();
        backup.close();
        
        std::cout << "📋 Created data backup: " << backup_filename << std::endl;
    }

    void generateFilePaths() {
        node_log_path = model_name + "_node_log.csv";
        node_predictor_path = result_folder + model_name + "_node_pred.bin";
        result_config_path = result_folder + model_name + "_config.json";
    }

    // Split data into training and testing sets (or validation if enabled)
    void splitData(float trainRatio) {
        size_t maxID = config.num_samples;  // total number of samples
        uint16_t trainSize = static_cast<uint16_t>(maxID * config.train_ratio);
        uint16_t testSize = static_cast<uint16_t>(maxID * config.test_ratio);
        uint16_t validationSize = (config.training_score == "valid_score") ? static_cast<uint16_t>(maxID * config.valid_ratio) : 0;
        
        // create train_data 
        sampleID_set train_sampleIDs(maxID);
        while (train_sampleIDs.size() < trainSize) {
            uint16_t sampleId = static_cast<uint16_t>(rng.bounded(static_cast<uint32_t>(maxID)));
            train_sampleIDs.push_back(sampleId); 
        }

        // create test_data
        sampleID_set test_sampleIDs(maxID);
        while(test_sampleIDs.size() < testSize) {
            uint16_t i = static_cast<uint16_t>(rng.bounded(static_cast<uint32_t>(maxID)));
            if (!train_sampleIDs.contains(i)) {
                test_sampleIDs.push_back(i);
            }
        }

        // create validation_data
        sampleID_set validation_sampleIDs(maxID);
        if(config.training_score == "valid_score") {
            while(validation_sampleIDs.size() < validationSize) {
                uint16_t i = static_cast<uint16_t>(rng.bounded(static_cast<uint32_t>(maxID)));
                if (!train_sampleIDs.contains(i) && !test_sampleIDs.contains(i)) {
                    validation_sampleIDs.push_back(i);
                }
            }
        }
        


        for (uint16_t id  = 0; id < maxID; id++) {
            if(train_sampleIDs.contains(id)) {
                train_data.allSamples.push_back(base_data.allSamples[id]);
            } else if(test_sampleIDs.contains(id)) {
                test_data.allSamples.push_back(base_data.allSamples[id]);
            } else if(config.training_score == "valid_score") {
                validation_data.allSamples.push_back(base_data.allSamples[id]);
            }
        }
    }

    // ---------------------------------------------------------------------------------
    // create dataset for each tree from train set
    void ClonesData() {
        // Clear previous data
        dataList.clear();
        dataList.reserve(config.num_trees);
        // Use actual train set size for sampling consistency with ESP32
        const uint16_t numSample = static_cast<uint16_t>(train_data.allSamples.size());
        uint16_t numSubSample;
        if (config.use_bootstrap) {
            numSubSample = numSample; // draw N with replacement
        } else {
            numSubSample = static_cast<uint16_t>(numSample * config.boostrap_ratio);
        }
        // Track hashes of each tree dataset to avoid duplicates across trees
        unordered_set<uint64_t> seen_hashes;
        seen_hashes.reserve(config.num_trees * 2);

        for (uint8_t i = 0; i < config.num_trees; i++) {
            ID_vector<uint16_t,2> treeDataset;
            treeDataset.reserve(numSample);

            // Derive a deterministic per-tree RNG; retry with nonce if duplicate detected
            uint64_t nonce = 0;
        while (true) {
                treeDataset.clear();
                auto tree_rng = rng.deriveRNG(i, nonce);

                if (config.use_bootstrap) {
            // Bootstrap sampling: allow duplicates; bag draws = numSample
            for (uint16_t j = 0; j < numSubSample; ++j) {
                        uint16_t idx = static_cast<uint16_t>(tree_rng.bounded(numSample));
                        treeDataset.push_back(idx);
                    }
                } else {
                    vector<uint16_t> arr(numSample,0);
                    arr.resize(numSample);
                    for (uint16_t t = 0; t < numSample; ++t) arr[t] = t;
            for (uint16_t t = 0; t < numSubSample; ++t) {
                        uint16_t j = static_cast<uint16_t>(t + tree_rng.bounded(numSample - t));
                        uint16_t tmp = arr[t];
                        arr[t] = arr[j];
                        arr[j] = tmp;
                        treeDataset.push_back(arr[t]);
                    }
                }
                // Check for duplicate dataset across trees
                uint64_t h = rng.hashIDVector(treeDataset);
                if (seen_hashes.find(h) == seen_hashes.end()) {
                    seen_hashes.insert(h);
                    break; // unique, accept
                }
                nonce++;
                if (nonce > 8) {
                    auto temp_vec = treeDataset;  // Copy current state
                    treeDataset.clear();
                    
                    // Re-add samples with slight modifications
                    for (uint16_t k = 0; k < std::min(5, (int)temp_vec.size()); ++k) {
                        // Get a sample ID and modify it slightly
                        uint16_t original_id = k < temp_vec.size() ? k : 0; // Simplified access
                        uint16_t modified_id = static_cast<uint16_t>((original_id + k + i) % numSample);
                        treeDataset.push_back(modified_id);
                    }
                    
                    // Add remaining samples from original
                    for (uint16_t k = 5; k < std::min(numSubSample, (uint16_t)temp_vec.size()); ++k) {
                        uint16_t id = k % numSample; // Simplified access
                        treeDataset.push_back(id);
                    }
                    seen_hashes.insert(rng.hashIDVector(treeDataset));
                    break;
                }
            }
            dataList.push_back(std::move(treeDataset));
        }
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
        
        // New: analyze a slice [begin,end) over a shared indices array
        void analyzeSamplesRange(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
                                 uint8_t numLabels, const Rf_data& data) {
            totalSamples = (begin < end) ? (end - begin) : 0;
            uint16_t maxCount = 0;
            for (uint16_t k = begin; k < end; ++k) {
                uint16_t sampleID = indices[k];
                if (sampleID < data.allSamples.size()) {
                    uint8_t label = data.allSamples[sampleID].label;
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
            if (sid < train_data.allSamples.size()) {
                uint8_t lbl = train_data.allSamples[sid].label;
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
                if (sid < train_data.allSamples.size()) {
                    uint8_t lbl = train_data.allSamples[sid].label;
                    if (lbl < numLabels) {
                        uint8_t fv = train_data.allSamples[sid].features[featureID];
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

    // Breadth-first tree building for optimal node layout

    void build_tree(Rf_tree& tree,  ID_vector<uint16_t,2>& sampleIDs) {
        tree.nodes.clear();
        if (train_data.allSamples.empty()) return;
        
        // Queue for breadth-first processing
        b_vector<NodeToBuild> queue_nodes;
        queue_nodes.reserve(200); // Reserve space for efficiency

        // Build a single contiguous index array for this tree
        b_vector<uint16_t, 8> indices;
        indices.reserve(sampleIDs.size());
        for (const auto& sid : sampleIDs) indices.push_back(sid);
        
        // Create root node
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);
        queue_nodes.push_back(NodeToBuild(0, 0, static_cast<uint16_t>(indices.size()), 0));

        // ---- BFS queue peak tracking (for ESP32 RAM estimation) ----
        size_t peak_queue_size = 0;
        auto track_peak = [&](size_t cur) {
            if (cur > peak_queue_size) peak_queue_size = cur;
        };
        track_peak(queue_nodes.size());
        // ------------------------------------------------------------
        
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
                uint16_t t = static_cast<uint16_t>(rng.bounded(j + 1));
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
            
            // In-place partition of indices[current.begin, current.end) using the best feature
            uint16_t iLeft = current.begin;
            for (uint16_t k = current.begin; k < current.end; ++k) {
                uint16_t sid = indices[k];
                if (sid < train_data.allSamples.size() &&
                    train_data.allSamples[sid].features[bestSplit.featureID] <= bestSplit.threshold) {
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
            if(queue_nodes.size() > peak_queue_size) {
                peak_queue_size = queue_nodes.size();
            }
            track_peak(queue_nodes.size());
        }
        
        // Print BFS queue peak after the tree is built
        float peak_nodes_percent = (float)peak_queue_size / (float)tree.nodes.size() * 100.0f;
        pre.peak_nodes.push_back(peak_nodes_percent);
    }


    uint8_t predClassSample(const Rf_sample& s){
        int16_t totalPredict = 0;
        unordered_map<uint8_t, uint8_t> predictClass;
        
        // Use streaming prediction 
        for(auto& tree : root){

            uint8_t predict = tree.predictSample(s); // Uses streaming if not loaded
            if(predict < config.num_labels){
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
        if(certainty < config.unity_threshold) {
            return 255;
        }
        
        return mostPredict;
    }

    // Enhanced OOB evaluation with better statistics and reliability checks
    pair<float,float> get_training_evaluation_index(Rf_data& validation_data){
        // Initialize confusion matrices using stack arrays to avoid heap allocation
        uint16_t oob_tp[config.num_labels] = {0};
        uint16_t oob_fp[config.num_labels] = {0};
        uint16_t oob_fn[config.num_labels] = {0};

        uint16_t valid_tp[config.num_labels] = {0};
        uint16_t valid_fp[config.num_labels] = {0};
        uint16_t valid_fn[config.num_labels] = {0};

        uint16_t oob_correct = 0, oob_total = 0, oob_skipped = 0;
        uint16_t valid_correct = 0, valid_total = 0;
        
        // Track OOB coverage statistics
        uint16_t oob_votes_histogram[21] = {0}; // 0-20+ votes
        uint16_t min_votes_required = std::max(1, (int)(config.num_trees * 0.15f)); // At least 15% of trees

        // OOB evaluation with enhanced reliability
        uint16_t sampleId = 0;
        for(const auto& sample : train_data.allSamples){                
        
            // Find all trees whose OOB set contains this sampleId
            b_vector<uint8_t> activeTrees;
            activeTrees.reserve(config.num_trees);
            
            for(uint8_t i = 0; i < config.num_trees; i++){
                if(!dataList[i].contains(sampleId)){
                    activeTrees.push_back(i);
                }
            }
            sampleId++;
            
            // Enhanced reliability check: require minimum vote count
            if(activeTrees.size() < min_votes_required){
                oob_skipped++;
                continue; // Skip samples with too few OOB trees
            }
            
            // Track vote distribution for analysis
            uint16_t vote_bucket = std::min(20, (int)activeTrees.size());
            oob_votes_histogram[vote_bucket]++;
            
            // Predict using only the OOB trees for this sample
            uint8_t actualLabel = sample.label;
            unordered_map<uint8_t, uint8_t> oobPredictClass;
            uint16_t oobTotalPredict = 0;
            
            for(const uint8_t& treeIdx : activeTrees){
                uint8_t predict = root[treeIdx].predictSample(sample);
                if(predict < config.num_labels){
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
            
            // Adaptive certainty threshold based on available votes
            float base_threshold = config.unity_threshold;
            float adaptive_threshold = base_threshold * (1.0f - 0.3f * (float)activeTrees.size() / config.num_trees);
            adaptive_threshold = std::max(0.3f, adaptive_threshold); // Minimum threshold
            
            float certainty = static_cast<float>(maxVotes) / oobTotalPredict;
            if(certainty < adaptive_threshold) {
                oob_skipped++;
                continue; // Skip uncertain predictions
            }
            
            // Update confusion matrix
            oob_total++;
            if(oobPredictedLabel == actualLabel){
                oob_correct++;
                oob_tp[actualLabel]++;
            } else {
                oob_fn[actualLabel]++;
                if(oobPredictedLabel < config.num_labels){
                    oob_fp[oobPredictedLabel]++;
                }
            }
        }

        // Validation evaluation (unchanged)
        sampleId = 0;
        if(this->config.training_score == "valid_score"){   
            for(const auto& sample : validation_data.allSamples){
                uint8_t actualLabel = sample.label;
                sampleId++;

                unordered_map<uint8_t, uint8_t> validPredictClass;
                uint16_t validTotalPredict = 0;

                for(uint8_t i = 0; i < config.num_trees; i++){
                    uint8_t predict = root[i].predictSample(sample);
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
                    valid_tp[actualLabel]++;
                } else {
                    valid_fn[actualLabel]++;
                    if(validPredictedLabel < config.num_labels){
                        valid_fp[validPredictedLabel]++;
                    }
                }
            }
        }

        // Calculate the requested metric with the same logic as before
        float oob_result = 0.0f;
        float valid_result = 0.0f;
        float combined_oob_result = 0.0f;
        float combined_valid_result = 0.0f;
        uint8_t training_flag = static_cast<uint8_t>(config.training_flag);
        uint8_t numFlags = 0;
        
        if(oob_total == 0){
            return make_pair(0.0f, 0.0f);
        }
        
        if(training_flag & ACCURACY){
            oob_result = static_cast<float>(oob_correct) / oob_total;
            valid_result = (valid_total > 0) ? static_cast<float>(valid_correct) / valid_total : 0.0f;
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & PRECISION){
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
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }
            
        if(training_flag & F1_SCORE) {
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
            combined_oob_result += oob_result;
            combined_valid_result += valid_result;
            numFlags++;
        }

        return make_pair(
            (numFlags > 0) ? combined_oob_result / numFlags : 0.0f, 
            (numFlags > 0) ? combined_valid_result / numFlags : 0.0f
        );
    }

    // K-fold cross validation evaluation
    float get_cross_validation_score() {
        uint8_t k_folds = config.k_fold;
        if (k_folds < 2) k_folds = 4; // Default to 4-fold if not properly set
        
        // Create vector of all training sample indices for k-fold split
        b_vector<uint16_t> allTrainIndices;
        allTrainIndices.reserve(train_data.allSamples.size());
        for (uint16_t i = 0; i < train_data.allSamples.size(); i++) {
            allTrainIndices.push_back(i);
        }
        
        // Shuffle the sample indices for random k-fold split using our custom RNG
        for (uint16_t i = allTrainIndices.size() - 1; i > 0; i--) {
            uint16_t j = static_cast<uint16_t>(rng.bounded(i + 1));
            uint16_t temp = allTrainIndices[i];
            allTrainIndices[i] = allTrainIndices[j];
            allTrainIndices[j] = temp;
        }
        
        uint16_t fold_size = allTrainIndices.size() / k_folds;
        float total_cv_score = 0.0f;
        uint8_t valid_folds = 0;
        
        // Store original dataList to restore after CV
        auto original_dataList = dataList;
        
        // Perform k-fold cross validation
        for (uint8_t fold = 0; fold < k_folds; fold++) {
            // Create train and test index sets for this fold
            b_vector<uint16_t> cv_train_indices, cv_test_indices;
            
            uint16_t test_start = fold * fold_size;
            uint16_t test_end = (fold == k_folds - 1) ? allTrainIndices.size() : (fold + 1) * fold_size;
            
            // Split indices into train and test for this fold
            for (uint16_t i = 0; i < allTrainIndices.size(); i++) {
                uint16_t sampleIndex = allTrainIndices[i];
                if (sampleIndex < train_data.allSamples.size()) {
                    if (i >= test_start && i < test_end) {
                        cv_test_indices.push_back(sampleIndex);
                    } else {
                        cv_train_indices.push_back(sampleIndex);
                    }
                }
            }
            
            if (cv_train_indices.empty() || cv_test_indices.empty()) {
                continue; // Skip this fold if empty
            }
            
            // Rebuild dataList for this CV fold using cv_train_indices
            dataList.clear();
            dataList.reserve(config.num_trees);
            
            uint16_t cv_train_size = cv_train_indices.size();
            uint16_t bootstrap_sample_size = config.use_bootstrap ? 
                static_cast<uint16_t>(cv_train_size * config.boostrap_ratio) : cv_train_size;
            
            // Create bootstrap samples from cv_train_indices for each tree
            for (uint8_t tree_idx = 0; tree_idx < config.num_trees; tree_idx++) {
                ID_vector<uint16_t,2> cv_tree_dataset;
                cv_tree_dataset.reserve(bootstrap_sample_size);
                
                auto tree_rng = rng.deriveRNG(fold * 1000 + tree_idx);
                
                if (config.use_bootstrap) {
                    // Bootstrap sampling: allow duplicates
                    for (uint16_t j = 0; j < bootstrap_sample_size; j++) {
                        uint16_t idx_in_cv_train = static_cast<uint16_t>(tree_rng.bounded(cv_train_size));
                        uint16_t actual_sample_idx = cv_train_indices[idx_in_cv_train];
                        cv_tree_dataset.push_back(actual_sample_idx);
                    }
                } else {
                    // Random sampling without replacement
                    vector<uint16_t> indices_copy = cv_train_indices;
                    
                    for (uint16_t t = 0; t < bootstrap_sample_size; t++) {
                        uint16_t j = static_cast<uint16_t>(t + tree_rng.bounded(cv_train_size - t));
                        uint16_t tmp = indices_copy[t];
                        indices_copy[t] = indices_copy[j];
                        indices_copy[j] = tmp;
                        cv_tree_dataset.push_back(indices_copy[t]);
                    }
                }
                
                dataList.push_back(std::move(cv_tree_dataset));
            }
            
            // Ensure root vector has correct size before rebuilding
            if (root.size() != config.num_trees) {
                root.clear();
                root.reserve(config.num_trees);
                for(uint8_t i = 0; i < config.num_trees; i++){
                    root.push_back(Rf_tree(""));
                }
            }
            
            // Build forest for this fold
            MakeForest();
            
            // Create test data object for evaluation using cv_test_indices
            Rf_data cv_test_data;
            cv_test_data.allSamples.reserve(cv_test_indices.size());
            for (uint16_t idx : cv_test_indices) {
                if (idx < train_data.allSamples.size()) {
                    cv_test_data.allSamples.push_back(train_data.allSamples[idx]);
                }
            }
            
            // Evaluate on cv_test_data using the specified training flag
            float fold_score = predict(cv_test_data, config.training_flag);
            total_cv_score += fold_score;
            valid_folds++;
        }
        
        // Restore original dataList
        dataList = original_dataList;    
        return (valid_folds > 0) ? (total_cv_score / valid_folds) : 0.0f;
    }
        
  public:
    // -----------------------------------------------------------------------------------
    // Grid Search Training with Multiple Runs
    // -----------------------------------------------------------------------------------
    // Enhanced training with adaptive evaluation strategy
    void training(){
        std::cout << "\n🚀 Training Random Forest...\n";
        std::remove(node_log_path.c_str());
        std::ofstream file(node_log_path);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to create node_predictor log file\n";          
            return;
        }
        file << "min_split,max_depth,total_nodes\n";
        file.close();

        uint8_t best_min_split = config.min_split;
        uint16_t best_max_depth = config.max_depth;
        float best_score = -1.0f;

        // Determine evaluation mode and number of runs
        bool use_cv = (config.training_score == "k-fold_score");
        const int num_runs = use_cv ? 1 : 3; // 1 run for CV, 3 runs for OOB/validation
        
        if (use_cv) {
            std::cout << "📊 Using " << (int)config.k_fold << "-fold cross validation for evaluation\n";
        } else if (config.training_score == "valid_score") {
            std::cout << "📊 Using validation data for evaluation\n";
        } else {
            std::cout << "📊 Using OOB for evaluation\n";
        }

        // Create temporary directory for saving best forests during iterations
        std::string temp_folder = "temp_best_forest";
        std::string final_folder = result_folder;
        
        #ifdef _WIN32
            _mkdir(temp_folder.c_str());
            _mkdir(final_folder.c_str());
        #else
            mkdir(temp_folder.c_str(), 0755);
            mkdir(final_folder.c_str(), 0755);
        #endif

        // Calculate total iterations for progress bar
        uint32_t total_iterations = config.min_split_range.size() * config.max_depth_range.size() * num_runs;
        uint32_t current_iteration = 0;

        int avg_nodes;

        // Grid search over min_split and max_depth ranges
        for ( uint8_t current_min_split : config.min_split_range) {
            for ( uint16_t current_max_depth : config.max_depth_range) {
                config.min_split = current_min_split;
                config.max_depth = current_max_depth;

                float total_run_score = 0.0f;
                float best_run_score = -1.0f;
                
                // Track best forest for this parameter combination
                bool best_forest_saved = false;
                avg_nodes = 0;

                for (int i = 0; i < num_runs; ++i) {
                    float combined_score = 0.0f;
                    
                    if (use_cv) {
                        // Cross validation mode
                        combined_score = get_cross_validation_score();
                        
                        // For CV mode, we need to rebuild with current parameters to save
                        ClonesData();
                        MakeForest();
                    } else {
                        // OOB or validation mode
                        ClonesData();
                        MakeForest();

                        pair<float, float> scores = get_training_evaluation_index(validation_data);
                        float oob_score = scores.first;
                        float validation_score = scores.second;

                        // Use the selected scoring method
                        if(config.training_score == "valid_score") {
                            combined_score = validation_score;
                        } else {
                            combined_score = oob_score; // Default to oob_score
                        }
                    }

                    // Calculate total_nodes for this parameter combination (sum of all nodes in all trees)
                    int total_nodes = 0;
                    for (uint8_t i = 0; i < config.num_trees; i++) {
                        total_nodes += root[i].countNodes();
                    }
                    avg_nodes += total_nodes / config.num_trees;

                    // Save the best forest of the 3 runs for this parameter combination
                    if (combined_score > best_run_score) {
                        best_run_score = combined_score;
                        // Save current forest to temporary folder (silently)
                        saveForest(temp_folder, true);
                        best_forest_saved = true;
                    }
                    
                    total_run_score += combined_score;
                    
                    // Update progress bar
                    current_iteration++;
                    float progress = (float)current_iteration / total_iterations;
                    int bar_width = 50;
                    int pos = bar_width * progress;
                    
                    std::cout << "\r[";
                    for (int j = 0; j < bar_width; ++j) {
                        if (j < pos) std::cout << "█";
                        else if (j == pos) std::cout << "▓";
                        else std::cout << "░";
                    }
                    std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% ";
                    std::cout << "(" << current_iteration << "/" << total_iterations << ") ";
                    std::cout << "Score: " << std::setprecision(3) << combined_score;
                    std::cout.flush();
                }
                avg_nodes /= num_runs;
                // Append min_split, max_depth, total_nodes to node_predictor log file
                if(avg_nodes > 0) {
                    std::ofstream log_file(node_log_path, std::ios::app);
                    if (log_file.is_open()) {
                        log_file << (int)config.min_split << "," 
                                 << (int)config.max_depth << "," 
                                 << avg_nodes << "\n";
                        log_file.close();
                    }
                }
                

                float avg_score = total_run_score / num_runs;

                // If this parameter combination gives better average score, copy to final folder
                if (avg_score > best_score && best_forest_saved) {
                    best_score = avg_score;
                    best_min_split = config.min_split;
                    best_max_depth = config.max_depth;
                    
                    // Copy best forest from temp folder to final folder
                    copyDirectory(temp_folder, final_folder);
                }
            }
        }

        std::cout << "\n✅ Training Complete! Best: min_split=" << (int)best_min_split 
                  << ", max_depth=" << (int)best_max_depth << ", score=" << best_score << "\n";

        // Load the best forest that was saved during training
        std::cout << "🔨 Loading best forest from saved files...\n";
        loadForest(final_folder);
        
        // Update config with best parameters found
        config.min_split = best_min_split;
        config.max_depth = best_max_depth;
        
        // Clean up temporary folder
        std::cout << "🧹 Cleaning up temporary files...\n";
        #ifdef _WIN32
            system(("rmdir /s /q " + temp_folder).c_str());
        #else
            system(("rm -rf " + temp_folder).c_str());
        #endif
    }

private:
    // Helper function to copy directory contents
    void copyDirectory(const std::string& source_path, const std::string& dest_path) {
        // Create destination directory if it doesn't exist
        #ifdef _WIN32
            _mkdir(dest_path.c_str());
        #else
            mkdir(dest_path.c_str(), 0755);
        #endif
        
        // Copy all tree files (now include model_name prefix)
        for(uint8_t i = 0; i < config.num_trees; i++){
            std::string src_file = source_path + "/" + model_name + "_tree_" + std::to_string(i) + ".bin";
            std::string dest_file = dest_path + "/" + model_name + "_tree_" + std::to_string(i) + ".bin";
            
            std::ifstream src(src_file, std::ios::binary);
            if (src.is_open()) {
                std::ofstream dest(dest_file, std::ios::binary);
                dest << src.rdbuf();
                src.close();
                dest.close();
            }
        }
        
        // Copy config files if they exist
        std::string config_json_src = source_path + model_name + "_config.json";

        std::string config_json_dest = dest_path + model_name + "_config.json";
        
        std::ifstream json_src(config_json_src, std::ios::binary);
        if (json_src.is_open()) {
            std::ofstream json_dest(config_json_dest, std::ios::binary);
            json_dest << json_src.rdbuf();
            json_src.close();
            json_dest.close();
        }
    }

public:
    void saveConfig() {
        config.saveConfig(result_config_path);
    }
    // Save the trained forest to files
    void saveForest(const std::string& folder_path = result_folder, bool silent = false) {
        if (!silent) {
            std::cout << "💾 Saving trained forest to " << folder_path << "...\n";
        }
        
        // Create directory if it doesn't exist
        #ifdef _WIN32
            _mkdir(folder_path.c_str());
        #else
            mkdir(folder_path.c_str(), 0755);
        #endif
        // Calculate forest statistics BEFORE saving (which purges the trees)
        uint32_t totalNodes = 0;
        uint32_t totalLeafNodes = 0;
        uint16_t maxTreeDepth = 0;
        uint16_t minTreeDepth = UINT16_MAX;
        
        for(uint8_t i = 0; i < config.num_trees; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxTreeDepth) maxTreeDepth = depth;
            if(depth < minTreeDepth) minTreeDepth = depth;
        }
        config.RAM_usage = (totalNodes + totalLeafNodes) * 4;
        
        // Save individual tree files (use model_name prefix)
        for(uint8_t i = 0; i < config.num_trees; i++){
            std::string filename = model_name + "_tree_" + std::to_string(i) + ".bin";
            root[i].filename = filename;
            root[i].saveTree(folder_path);
        }
        
        // Save config in both JSON and CSV formats
        config.saveConfig(result_config_path);
    }
    
    // Load the best trained forest from files (trees only, ignores config file)
    void loadForest(const std::string& folder_path = result_folder) {
        std::cout << "📂 Loading trained forest from " << folder_path << "...\n";
        
        uint8_t loaded_trees = 0;
        
        // Load individual tree files (expect model_name prefix)
        for(uint8_t i = 0; i < config.num_trees; i++){
            std::string tree_filename = folder_path + "/" + model_name + "_tree_" + std::to_string(i) + ".bin";
            
            // Check if file exists
            std::ifstream check_file(tree_filename);
            if (!check_file.is_open()) {
                std::cout << "⚠️  Tree file not found: " << tree_filename << "\n";
                continue;
            }
            check_file.close();
            
            // Load the tree
            root[i].filename = model_name + "_tree_" + std::to_string(i) + ".bin";
            root[i].loadTree(tree_filename);
            
            // Verify tree was loaded successfully
            if (!root[i].nodes.empty()) {
                loaded_trees++;
            } else {
                std::cout << "❌ Failed to load tree " << (int)i << " from " << tree_filename << "\n";
            }
        }
        
        if (loaded_trees == config.num_trees) {
            std::cout << "✅ Forest loaded successfully! (" << (int)loaded_trees << "/" << (int)config.num_trees << " trees)\n";
        } else if (loaded_trees > 0) {
            std::cout << "⚠️  Partial forest loaded: " << (int)loaded_trees << "/" << (int)config.num_trees << " trees\n";
        } else {
            std::cout << "❌ Failed to load any trees from " << folder_path << "\n";
        }
    }
    
    // combined prediction metrics function
    b_vector<b_vector<pair<uint8_t, float>>> predict(Rf_data& data) {
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
        
        // Single pass over samples
        for (const auto& sample : data.allSamples) {
            uint8_t actual = sample.label;
            uint8_t pred = predClassSample(sample);
            
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
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Random Forest PC Training\n";
    RandomForest forest = RandomForest(); // Use random_seed from config file
    // Build initial forest
    forest.MakeForest();
    // forest.printForestStatistics();
    
    // Train the forest to find optimal parameters (combine_ratio auto-calculated in first_scan)
    forest.training();

    //print forest statistics
    forest.printForestStatistics();
    
    
    std::cout << "Training complete! Model saved to 'trained_model' directory.\n";
    auto result = forest.predict(forest.test_data);

    // Calculate Precision
    std::cout << "Precision in test set:\n";
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
    std::cout << "Recall in test set:\n";
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
    std::cout << "F1 Score in test set:\n";
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
    std::cout << "Overall Accuracy in test set:\n";
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

    float result_score = forest.predict(forest.test_data, static_cast<Rf_training_flags>(forest.config.training_flag));
    forest.config.result_score = result_score;
    forest.saveConfig();
    std::cout << "result score: " << result_score << "\n";

    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Total time: " << elapsed.count() << " seconds\n ";
    return 0;
}