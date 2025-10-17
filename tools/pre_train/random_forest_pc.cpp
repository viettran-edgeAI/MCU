#include <string>
#include <random>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <set>
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

static constexpr int RF_MAX_NODES = 1024; // Maximum nodes per tree


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

    RandomForest() : config(config_path) {
        rng = Rf_random(config.random_seed, true); // Initialize RNG with loaded seed
        model_name = extract_model_name(config.data_path);
        std::cout << "🌲 Model name: " << model_name << std::endl;

        std::string metadataPath = buildMetadataPath();
        uint8_t metadataBits = loadQuantizationFromMetadata(metadataPath);
        if (metadataBits != 0) {
            config.quantization_coefficient = metadataBits;
        }
        config.quantization_coefficient = QuantizationHelper::sanitizeBits(config.quantization_coefficient);

        generateFilePaths();
        createDataBackup(config.data_path, temp_base_data);     // create a backup to avoid damaging the original data
        config.init(temp_base_data); // Load configuration from model_config.json first
        base_data.setFeatureBits(config.quantization_coefficient);
        train_data.setFeatureBits(config.quantization_coefficient);
        test_data.setFeatureBits(config.quantization_coefficient);
        validation_data.setFeatureBits(config.quantization_coefficient);
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
        
        for(uint16_t i = 0; i < config.num_trees; i++){
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
        
        for(uint16_t i = 0; i < config.num_trees; i++){
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
        struct ConsensusResult {
            uint16_t predicted_label = 255;
            uint16_t votes = 0;
            uint16_t total_votes = 0;
            float consensus = 0.0f;
        };

        struct EvaluationSample {
            uint16_t actual_label = 0;
            uint16_t predicted_label = 255;
            uint16_t votes = 0;
            uint16_t total_votes = 0;
            float consensus = 0.0f;
        };

        struct MetricsSummary {
            float accuracy = 0.0f;
            float precision = 0.0f;
            float recall = 0.0f;
            float f1 = 0.0f;
            float f0_5 = 0.0f;
            float f2 = 0.0f;
            float coverage = 0.0f;
            uint32_t total_samples = 0;
            uint32_t predicted_samples = 0;
        };

        struct ThresholdSearchResult {
            float threshold = 0.0f;
            float score = -1.0f;
            MetricsSummary metrics;
        };

        ConsensusResult computeConsensus(const Rf_sample& sample, const b_vector<uint16_t>* tree_indices = nullptr);
        std::vector<EvaluationSample> collectOOBSamples(uint16_t min_votes_required, std::vector<uint16_t>* vote_histogram = nullptr);
        std::vector<EvaluationSample> collectValidationSamples(const Rf_data& dataset);
        std::vector<EvaluationSample> collectCrossValidationSamples(float& avg_nodes_out);
        MetricsSummary computeMetricsForThreshold(const std::vector<EvaluationSample>& samples, float threshold);
        float computeObjectiveScore(const MetricsSummary& metrics, Rf_metric_scores flags);
        ThresholdSearchResult findBestThreshold(const std::vector<EvaluationSample>& samples, Rf_metric_scores flags);

        std::string buildMetadataPath() const {
            if (config.data_path.empty()) {
                return "";
            }

            size_t lastSlash = config.data_path.find_last_of("/\\");
            std::string directory = (lastSlash == std::string::npos)
                                        ? ""
                                        : config.data_path.substr(0, lastSlash + 1);
            return directory + model_name + "_dp.csv";
        }

        uint8_t loadQuantizationFromMetadata(const std::string& metadataPath) const {
            if (metadataPath.empty()) {
                std::cout << "⚠️  Quantization metadata path is empty; using configuration value "
                          << static_cast<int>(config.quantization_coefficient) << std::endl;
                return 0;
            }

            std::ifstream metaFile(metadataPath);
            if (!metaFile.is_open()) {
                std::cout << "⚠️  Metadata file not found: " << metadataPath
                          << ". Using configuration quantization bits ("
                          << static_cast<int>(config.quantization_coefficient) << ")." << std::endl;
                return 0;
            }

            auto trim = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t\r\n\"");
                if (start == std::string::npos) {
                    s.clear();
                    return;
                }
                size_t end = s.find_last_not_of(" \t\r\n\"");
                s = s.substr(start, end - start + 1);
            };

            std::string line;
            uint8_t detectedBits = 0;
            while (std::getline(metaFile, line)) {
                if (line.empty()) continue;

                std::stringstream ss(line);
                std::string key;
                std::string value;
                if (!std::getline(ss, key, ',')) continue;
                if (!std::getline(ss, value)) continue;

                trim(key);
                trim(value);

                if (key == "quantization_coefficient") {
                    try {
                        int bits = std::stoi(value);
                        detectedBits = QuantizationHelper::sanitizeBits(bits);
                    } catch (...) {
                        std::cout << "⚠️  Failed to parse quantization bits from metadata value '"
                                  << value << "'. Using configuration setting." << std::endl;
                    }
                    break;
                }
            }

            if (detectedBits == 0) {
                std::cout << "⚠️  quantization_coefficient not found in metadata file "
                          << metadataPath << ". Using configuration value ("
                          << static_cast<int>(config.quantization_coefficient) << ")." << std::endl;
            } else {
                std::cout << "📦 Loaded quantization bits from metadata (" << metadataPath
                          << "): " << static_cast<int>(detectedBits) << std::endl;
            }

            return detectedBits;
        }

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
        node_log_path = result_folder + model_name + "_node_log.csv";
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

        for (uint16_t i = 0; i < config.num_trees; i++) {
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


    struct SplitInfo {
        float gain = -1.0f;
        uint16_t featureID = 0;
        uint8_t threshold_slot = 0;
        uint16_t threshold_value = 0;
        uint32_t leftCount = 0;
        uint32_t rightCount = 0;
    };

    struct NodeStats {
        unordered_set<uint16_t> labels;
        b_vector<uint16_t> labelCounts; 
        uint16_t majorityLabel;
        uint16_t totalSamples;
        
        NodeStats(uint16_t numLabels) : majorityLabel(0), totalSamples(0) {
            labelCounts.reserve(numLabels);
            labelCounts.fill(0);
        }
        
        // New: analyze a slice [begin,end) over a shared indices array
        void analyzeSamplesRange(const b_vector<uint16_t, 8>& indices, uint16_t begin, uint16_t end,
                                 uint16_t numLabels, const Rf_data& data) {
            totalSamples = (begin < end) ? (end - begin) : 0;
            uint16_t maxCount = 0;
            for (uint16_t k = begin; k < end; ++k) {
                uint16_t sampleID = indices[k];
                if (sampleID < data.allSamples.size()) {
                    uint16_t label = data.allSamples[sampleID].label;
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
                                 const unordered_set<uint16_t>& selectedFeatures, bool use_Gini, uint16_t numLabels) {
        SplitInfo bestSplit;
        uint32_t totalSamples = (begin < end) ? (end - begin) : 0;
        if (totalSamples < 2) return bestSplit;

        // Base label counts
        std::vector<uint16_t> baseLabelCounts(numLabels, 0);
        for (uint16_t k = begin; k < end; ++k) {
            uint16_t sid = indices[k];
            if (sid < train_data.allSamples.size()) {
                uint16_t lbl = train_data.allSamples[sid].label;
                if (lbl < numLabels) baseLabelCounts[lbl]++;
            }
        }

        float baseImpurity = 0.0f;
        if (use_Gini) {
            baseImpurity = 1.0f;
            for (uint16_t i = 0; i < numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * p;
                }
            }
        } else {
            for (uint16_t i = 0; i < numLabels; i++) {
                if (baseLabelCounts[i] > 0) {
                    float p = static_cast<float>(baseLabelCounts[i]) / totalSamples;
                    baseImpurity -= p * log2f(p);
                }
            }
        }

        uint8_t quantBits = config.quantization_coefficient;
        std::vector<uint16_t> thresholdCandidates;
        QuantizationHelper::buildThresholdCandidates(quantBits, thresholdCandidates);
        if (thresholdCandidates.empty()) {
            return bestSplit;
        }

        b_vector<uint16_t> leftCounts;
        b_vector<uint16_t> rightCounts;
        leftCounts.resize(numLabels, 0);
        rightCounts.resize(numLabels, 0);

        // Fast path for 1-bit quantization (only 2 values: 0 and 1)
        if (quantBits == 1) {
            for (const auto& featureID : selectedFeatures) {
                // Reset counts
                for (uint16_t i = 0; i < numLabels; ++i) {
                    leftCounts[i] = 0;
                    rightCounts[i] = 0;
                }

                uint32_t leftTotal = 0;
                uint32_t rightTotal = 0;

                // Collect counts for value 0 (left) and value 1 (right)
                for (uint16_t k = begin; k < end; ++k) {
                    uint16_t sid = indices[k];
                    if (sid >= train_data.allSamples.size()) continue;
                    const auto& sample = train_data.allSamples[sid];
                    uint16_t lbl = sample.label;
                    if (lbl >= numLabels) continue;
                    uint16_t fv = sample.features[featureID];
                    
                    if (fv == 0) {
                        leftCounts[lbl]++;
                        leftTotal++;
                    } else {
                        rightCounts[lbl]++;
                        rightTotal++;
                    }
                }

                if (leftTotal == 0 || rightTotal == 0) continue;

                // Calculate impurity and gain for the single threshold (0)
                float leftImpurity = 0.0f;
                float rightImpurity = 0.0f;
                if (use_Gini) {
                    leftImpurity = 1.0f;
                    rightImpurity = 1.0f;
                    for (uint16_t i = 0; i < numLabels; i++) {
                        if (leftCounts[i] > 0) {
                            float p = static_cast<float>(leftCounts[i]) / leftTotal;
                            leftImpurity -= p * p;
                        }
                        if (rightCounts[i] > 0) {
                            float p = static_cast<float>(rightCounts[i]) / rightTotal;
                            rightImpurity -= p * p;
                        }
                    }
                } else {
                    for (uint16_t i = 0; i < numLabels; i++) {
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
                    bestSplit.threshold_slot = 0;  // Only one threshold slot for 1-bit
                    bestSplit.threshold_value = 0;  // Threshold value: 0 (0 goes left, 1 goes right)
                    bestSplit.leftCount = leftTotal;
                    bestSplit.rightCount = rightTotal;
                }
            }
        } else {
            // General case: multi-bit quantization with multiple threshold candidates
            for (const auto& featureID : selectedFeatures) {
                for (size_t slot = 0; slot < thresholdCandidates.size(); ++slot) {
                    uint16_t thresholdValue = thresholdCandidates[slot];

                    uint32_t leftTotal = 0;
                    uint32_t rightTotal = 0;
                    for (uint16_t i = 0; i < numLabels; ++i) {
                        leftCounts[i] = 0;
                        rightCounts[i] = 0;
                    }

                    for (uint16_t k = begin; k < end; ++k) {
                        uint16_t sid = indices[k];
                        if (sid >= train_data.allSamples.size()) continue;
                        const auto& sample = train_data.allSamples[sid];
                        uint16_t lbl = sample.label;
                        if (lbl >= numLabels) continue;
                        uint16_t fv = sample.features[featureID];
                        if (fv <= thresholdValue) {
                            leftCounts[lbl]++;
                            leftTotal++;
                        } else {
                            rightCounts[lbl]++;
                            rightTotal++;
                        }
                    }

                    if (leftTotal == 0 || rightTotal == 0) {
                        continue;
                    }

                    float leftImpurity = 0.0f;
                    float rightImpurity = 0.0f;
                    if (use_Gini) {
                        leftImpurity = 1.0f;
                        rightImpurity = 1.0f;
                        for (uint16_t i = 0; i < numLabels; i++) {
                            if (leftCounts[i] > 0) {
                                float p = static_cast<float>(leftCounts[i]) / leftTotal;
                                leftImpurity -= p * p;
                            }
                            if (rightCounts[i] > 0) {
                                float p = static_cast<float>(rightCounts[i]) / rightTotal;
                                rightImpurity -= p * p;
                            }
                        }
                    } else {
                        for (uint16_t i = 0; i < numLabels; i++) {
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
                        bestSplit.threshold_slot = static_cast<uint8_t>(slot);
                        bestSplit.threshold_value = thresholdValue;
                        bestSplit.leftCount = leftTotal;
                        bestSplit.rightCount = rightTotal;
                    }
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

            if(current.nodeIndex >= RF_MAX_NODES){
                // force leaf if exceeding max nodes
                uint8_t leafLabel = stats.majorityLabel;
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            bool shouldBeLeaf = false;
            uint16_t leafLabel = stats.majorityLabel;
            
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
            uint16_t num_selected_features = static_cast<uint16_t>(sqrt(config.num_features));
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
            
            if (tree.nodes.size() + 2 > 1023) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }

            // Configure as internal node
            tree.nodes[current.nodeIndex].setFeatureID(bestSplit.featureID);
            tree.nodes[current.nodeIndex].setThresholdSlot(bestSplit.threshold_slot);
            tree.nodes[current.nodeIndex].setIsLeaf(false);
            
            // In-place partition of indices[current.begin, current.end) using the best feature
            uint16_t iLeft = current.begin;
            for (uint16_t k = current.begin; k < current.end; ++k) {
                uint16_t sid = indices[k];
                if (sid < train_data.allSamples.size() &&
                    train_data.allSamples[sid].features[bestSplit.featureID] <= bestSplit.threshold_value) {
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


    uint16_t predClassSample(const Rf_sample& s){
        ConsensusResult consensus = computeConsensus(s);
        if (consensus.total_votes == 0) {
            return 255;
        }
        return consensus.predicted_label;
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

        bool use_cv = (config.training_score == "k_fold_score");
        const int num_runs = use_cv ? 1 : 3;

        if (use_cv) {
            std::cout << "📊 Using " << (int)config.k_folds << "-fold cross validation for evaluation\n";
        } else if (config.training_score == "valid_score") {
            std::cout << "📊 Using validation data for evaluation\n";
        } else {
            std::cout << "📊 Using OOB for evaluation\n";
        }

        std::string temp_folder = "temp_best_forest";
        std::string final_folder = result_folder;

        #ifdef _WIN32
            _mkdir(temp_folder.c_str());
            _mkdir(final_folder.c_str());
        #else
            mkdir(temp_folder.c_str(), 0755);
            mkdir(final_folder.c_str(), 0755);
        #endif

        uint32_t candidate_count = config.min_split_range.size() * config.max_depth_range.size();
        if (candidate_count == 0) candidate_count = 1;
        uint32_t total_iterations = candidate_count * (use_cv ? 1 : num_runs);
        if (total_iterations == 0) total_iterations = 1;
        uint32_t current_iteration = 0;

        auto updateProgress = [&](float score, uint16_t min_split, uint16_t max_depth) {
            float progress = total_iterations ? static_cast<float>(current_iteration) / total_iterations : 1.0f;
            int bar_width = 50;
            int pos = static_cast<int>(bar_width * progress);
            std::cout << "\r[";
            for (int j = 0; j < bar_width; ++j) {
                if (j < pos) std::cout << "█";
                else if (j == pos) std::cout << "▓";
                else std::cout << "░";
            }
            std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0f) << "% ";
            std::cout << "(" << current_iteration << "/" << total_iterations << ") ";
            std::cout << "Score≈" << std::setprecision(3) << score;
            std::cout << " | split=" << min_split << ", depth=" << max_depth;
            std::cout.flush();
        };

        uint16_t best_min_split = config.min_split;
        uint16_t best_max_depth = config.max_depth;
        float best_score = -1.0f;
        MetricsSummary best_metrics;
        bool best_found = false;

        for (uint16_t current_min_split : config.min_split_range) {
            for (uint16_t current_max_depth : config.max_depth_range) {
                config.min_split = current_min_split;
                config.max_depth = current_max_depth;

                float avg_nodes = 0.0f;
                bool best_forest_saved = false;
                std::vector<EvaluationSample> aggregated_samples;
                aggregated_samples.reserve(train_data.allSamples.size());
                ThresholdSearchResult aggregated_result;
                aggregated_result.score = -1.0f;
                aggregated_result.threshold = 0.5f;

                if (use_cv) {
                    float cv_avg_nodes = 0.0f;
                    aggregated_samples = collectCrossValidationSamples(cv_avg_nodes);
                    avg_nodes = cv_avg_nodes;

                    ClonesData();
                    MakeForest();

                    int total_nodes = 0;
                    for (uint16_t t = 0; t < config.num_trees; ++t) {
                        total_nodes += root[t].countNodes();
                    }
                    if (config.num_trees > 0) {
                        avg_nodes = static_cast<float>(total_nodes) / config.num_trees;
                    }

                    aggregated_result = findBestThreshold(aggregated_samples, config.metric_score);

                    saveForest(temp_folder, true);
                    best_forest_saved = true;

                    current_iteration++;
                    updateProgress(aggregated_result.score, current_min_split, current_max_depth);
                } else {
                    float total_run_score = 0.0f;
                    float best_run_score = -1.0f;

                    for (int run = 0; run < num_runs; ++run) {
                        ClonesData();
                        MakeForest();

                        std::vector<EvaluationSample> run_samples;
                        if (config.training_score == "valid_score") {
                            run_samples = collectValidationSamples(validation_data);
                        } else {
                            uint16_t min_votes_required = std::max<uint16_t>(1, static_cast<uint16_t>(std::ceil(config.num_trees * 0.15f)));
                            run_samples = collectOOBSamples(min_votes_required);
                        }

                        aggregated_samples.insert(aggregated_samples.end(), run_samples.begin(), run_samples.end());

                        ThresholdSearchResult run_result = findBestThreshold(run_samples, config.metric_score);
                        if (run_result.score >= 0.0f) {
                            total_run_score += run_result.score;
                        }

                        int total_nodes = 0;
                        for (uint16_t t = 0; t < config.num_trees; ++t) {
                            total_nodes += root[t].countNodes();
                        }
                        if (config.num_trees > 0) {
                            avg_nodes += static_cast<float>(total_nodes) / config.num_trees;
                        }

                        if (run_result.score > best_run_score) {
                            best_run_score = run_result.score;
                            saveForest(temp_folder, true);
                            best_forest_saved = true;
                        }

                        current_iteration++;
                        updateProgress(run_result.score, current_min_split, current_max_depth);
                    }

                    if (num_runs > 0) {
                        avg_nodes /= num_runs;
                    }

                    aggregated_result = findBestThreshold(aggregated_samples, config.metric_score);
                }

                if (avg_nodes > 0.0f) {
                    std::ofstream log_file(node_log_path, std::ios::app);
                    if (log_file.is_open()) {
                        log_file << static_cast<int>(config.min_split) << ","
                                 << static_cast<int>(config.max_depth) << ","
                                 << static_cast<int>(std::round(avg_nodes)) << "\n";
                    }
                }

                if (aggregated_result.score > best_score && best_forest_saved) {
                    best_score = aggregated_result.score;
                    best_min_split = config.min_split;
                    best_max_depth = config.max_depth;
                    best_metrics = aggregated_result.metrics;
                    best_found = true;
                    copyDirectory(temp_folder, final_folder);
                }
            }
        }

        std::cout << std::endl;
        if (best_found) {
            std::cout << "✅ Training Complete! Best: min_split=" << (int)best_min_split
                      << ", max_depth=" << (int)best_max_depth
                      << ", score=" << std::setprecision(3) << best_score << "\n";
            std::cout << "   Precision=" << std::setprecision(3) << best_metrics.precision
                      << ", Recall=" << best_metrics.recall
                      << ", Coverage=" << best_metrics.coverage << "\n";
        } else {
            std::cout << "⚠️  No valid candidate found during training; retaining existing parameters.\n";
        }

        config.min_split = best_min_split;
        config.max_depth = best_max_depth;

        ClonesData();
        MakeForest();

        uint16_t min_votes_required = std::max<uint16_t>(1, static_cast<uint16_t>(std::ceil(config.num_trees * 0.15f)));
        std::vector<EvaluationSample> final_samples;
        if (config.training_score == "valid_score") {
            final_samples = collectValidationSamples(validation_data);
        } else {
            final_samples = collectOOBSamples(min_votes_required);
        }

        ThresholdSearchResult final_result = findBestThreshold(final_samples, config.metric_score);
        if (final_result.score >= 0.0f) {
            config.result_score = final_result.score;
        } else {
            config.result_score = 0.0f;
        }

        std::cout << "🎯 Final model score: " << std::fixed << std::setprecision(3) << config.result_score
                  << " (coverage=" << final_result.metrics.coverage << ")\n";

        saveForest(result_folder);

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
        
        // Copy unified forest file
        std::string forest_src = source_path + "/" + model_name + "_forest.bin";
        std::string forest_dest = dest_path + "/" + model_name + "_forest.bin";
        
        std::ifstream src(forest_src, std::ios::binary);
        if (src.is_open()) {
            std::ofstream dest(forest_dest, std::ios::binary);
            dest << src.rdbuf();
            src.close();
            dest.close();
        }
        
        // Copy config files if they exist
        std::string config_json_src = source_path + "/" + model_name + "_config.json";
        std::string config_json_dest = dest_path + "/" + model_name + "_config.json";
        
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
        
        // Calculate forest statistics BEFORE saving
        uint32_t totalNodes = 0;
        uint32_t totalLeafNodes = 0;
        uint16_t maxTreeDepth = 0;
        uint16_t minTreeDepth = UINT16_MAX;
        
        for(uint16_t i = 0; i < config.num_trees; i++){
            uint32_t nodeCount = root[i].countNodes();
            uint32_t leafCount = root[i].countLeafNodes();
            uint16_t depth = root[i].getTreeDepth();
            
            totalNodes += nodeCount;
            totalLeafNodes += leafCount;
            
            if(depth > maxTreeDepth) maxTreeDepth = depth;
            if(depth < minTreeDepth) minTreeDepth = depth;
        }
        config.RAM_usage = (totalNodes + totalLeafNodes) * 4;
        
        // Save config in both JSON and CSV formats
        config.saveConfig(result_config_path);

        // Save unified forest file directly from memory (no individual tree files)
        const uint32_t FOREST_MAGIC = 0x464F5253; // "FORS"
        std::string unified_path = folder_path + "/" + model_name + "_forest.bin";
        std::ofstream out(unified_path, std::ios::binary);
        if (!out.is_open()) {
            if (!silent) {
                std::cout << "❌ Failed to create unified forest file: " << unified_path << "\n";
            }
            return;
        }

        // Forest header: magic + tree count (u8)
        out.write(reinterpret_cast<const char*>(&FOREST_MAGIC), sizeof(FOREST_MAGIC));
        uint8_t tree_count_written = 0;
        out.write(reinterpret_cast<const char*>(&tree_count_written), sizeof(tree_count_written));

        uint32_t total_nodes_written = 0;
        for (uint16_t i = 0; i < config.num_trees; ++i) {
            if (root[i].nodes.empty()) {
                continue; // skip empty trees
            }
            
            uint32_t node_count = static_cast<uint32_t>(root[i].nodes.size());
            
            // Unified per-tree header: tree index (u8) + node_count (u32)
            uint8_t tree_index = static_cast<uint8_t>(i);
            out.write(reinterpret_cast<const char*>(&tree_index), sizeof(tree_index));
            out.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
            
            // Write node data directly from memory
            for (const auto& node : root[i].nodes) {
                out.write(reinterpret_cast<const char*>(&node.packed_data), sizeof(node.packed_data));
            }

            ++tree_count_written;
            total_nodes_written += node_count;
        }

        // Patch tree_count at byte offset sizeof(FOREST_MAGIC)
        out.seekp(static_cast<std::streamoff>(sizeof(FOREST_MAGIC)), std::ios::beg);
        out.write(reinterpret_cast<const char*>(&tree_count_written), sizeof(tree_count_written));
        out.close();

        if (!silent) {
            std::cout << "✅ Saved unified forest: " << (int)tree_count_written << "/" << (int)config.num_trees
                      << " trees (" << total_nodes_written << " nodes) -> " << unified_path << "\n";
        }
    }
    
    // Load the best trained forest from files (trees only, ignores config file)
    void loadForest(const std::string& folder_path = result_folder) {
        std::cout << "📂 Loading trained forest from " << folder_path << "...\n";
        
        uint16_t loaded_trees = 0;
        
        // Load individual tree files (expect model_name prefix)
        for(uint16_t i = 0; i < config.num_trees; i++){
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
    b_vector<b_vector<pair<uint16_t, float>>> predict(Rf_data& data) {
        // Counters for each label
        unordered_map<uint16_t, uint32_t> tp, fp, fn, totalPred, correctPred;
        
        // Initialize counters for all actual labels
        for (uint16_t label=0; label < config.num_labels; label++) {
            tp[label] = 0;
            fp[label] = 0; 
            fn[label] = 0;
            totalPred[label] = 0;
            correctPred[label] = 0;
        }
        
        // Single pass over samples
        for (const auto& sample : data.allSamples) {
            uint16_t actual = sample.label;
            uint16_t pred = predClassSample(sample);
            
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
        b_vector<pair<uint16_t, float>> precisions, recalls, f1s, accuracies;
        
        for (uint16_t label = 0; label < config.num_labels; label++) {
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
        
        b_vector<b_vector<pair<uint16_t, float>>> result;
        result.push_back(precisions);  // 0: precisions
        result.push_back(recalls);     // 1: recalls
        result.push_back(f1s);         // 2: F1 scores
        result.push_back(accuracies);  // 3: accuracies

        return result;
    }

    // get prediction score based on training flags
    float predict(Rf_data& data, Rf_metric_scores flags) {
        auto metrics = predict(data);

        float combined_score = 0.0f;
        uint16_t num_flags = 0;

        // Helper: average a vector of (label, value) pairs
        auto avg_metric = [](const b_vector<pair<uint16_t, float>>& vec) -> float {
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
    uint16_t predict(packed_vector<8>& features) {
        Rf_sample sample;
        features.set_bits_per_value(config.quantization_coefficient);
        sample.features = features;
        return predClassSample(sample);
    }
};

RandomForest::ConsensusResult RandomForest::computeConsensus(const Rf_sample& sample, const b_vector<uint16_t>* tree_indices) {
    ConsensusResult result;
    if (root.empty()) {
        return result;
    }

    std::vector<uint16_t> vote_counts(config.num_labels, 0);

    auto tally_tree = [&](uint16_t tree_index) {
        if (tree_index >= root.size()) {
            return;
        }
    uint16_t predict = root[tree_index].predictSample(sample, config.quantization_coefficient);
        if (predict < config.num_labels) {
            vote_counts[predict]++;
            result.total_votes++;
        }
    };

    if (tree_indices) {
        for (uint16_t idx : *tree_indices) {
            tally_tree(idx);
        }
    } else {
        for (uint16_t idx = 0; idx < root.size() && idx < config.num_trees; ++idx) {
            tally_tree(idx);
        }
    }

    if (result.total_votes == 0) {
        return result;
    }

    for (uint16_t label = 0; label < config.num_labels; ++label) {
        if (vote_counts[label] > result.votes) {
            result.votes = vote_counts[label];
            result.predicted_label = label;
        }
    }

    result.consensus = static_cast<float>(result.votes) / static_cast<float>(result.total_votes);
    return result;
}

std::vector<RandomForest::EvaluationSample> RandomForest::collectOOBSamples(uint16_t min_votes_required, std::vector<uint16_t>* vote_histogram) {
    std::vector<EvaluationSample> samples;
    samples.reserve(train_data.allSamples.size());

    if (vote_histogram) {
        vote_histogram->assign(21, 0);
    }

    uint16_t sampleId = 0;
    for (const auto& sample : train_data.allSamples) {
        b_vector<uint16_t> activeTrees;
        activeTrees.reserve(config.num_trees);
        for (uint16_t i = 0; i < config.num_trees; ++i) {
            if (!dataList[i].contains(sampleId)) {
                activeTrees.push_back(i);
            }
        }
        sampleId++;

        if (vote_histogram) {
            uint16_t bucket = std::min<uint16_t>(20, activeTrees.size());
            (*vote_histogram)[bucket]++;
        }

        if (activeTrees.empty() || activeTrees.size() < min_votes_required) {
            continue;
        }

        ConsensusResult consensus = computeConsensus(sample, &activeTrees);
        if (consensus.total_votes == 0) {
            continue;
        }

        EvaluationSample eval{};
        eval.actual_label = sample.label;
        eval.predicted_label = consensus.predicted_label;
        eval.votes = consensus.votes;
        eval.total_votes = consensus.total_votes;
        eval.consensus = consensus.consensus;
        samples.push_back(eval);
    }

    return samples;
}

std::vector<RandomForest::EvaluationSample> RandomForest::collectValidationSamples(const Rf_data& dataset) {
    std::vector<EvaluationSample> samples;
    samples.reserve(dataset.allSamples.size());

    for (const auto& sample : dataset.allSamples) {
        ConsensusResult consensus = computeConsensus(sample);
        if (consensus.total_votes == 0) {
            continue;
        }
        EvaluationSample eval{};
        eval.actual_label = sample.label;
        eval.predicted_label = consensus.predicted_label;
        eval.votes = consensus.votes;
        eval.total_votes = consensus.total_votes;
        eval.consensus = consensus.consensus;
        samples.push_back(eval);
    }

    return samples;
}

std::vector<RandomForest::EvaluationSample> RandomForest::collectCrossValidationSamples(float& avg_nodes_out) {
    std::vector<EvaluationSample> aggregated;
    aggregated.reserve(train_data.allSamples.size());
    avg_nodes_out = 0.0f;

    uint16_t k_folds = config.k_folds;
    if (k_folds < 2) {
        k_folds = 4;
    }

    b_vector<uint16_t> allTrainIndices;
    allTrainIndices.reserve(train_data.allSamples.size());
    for (uint16_t i = 0; i < train_data.allSamples.size(); ++i) {
        allTrainIndices.push_back(i);
    }

    for (uint16_t i = allTrainIndices.size(); i > 1; --i) {
        uint16_t j = static_cast<uint16_t>(rng.bounded(i));
        std::swap(allTrainIndices[i - 1], allTrainIndices[j]);
    }

    uint16_t fold_size = (k_folds > 0) ? static_cast<uint16_t>(allTrainIndices.size() / k_folds) : 0;
    if (fold_size == 0) {
        fold_size = allTrainIndices.size();
    }

    auto original_dataList = dataList;
    uint16_t valid_folds = 0;

    for (uint16_t fold = 0; fold < k_folds; ++fold) {
        b_vector<uint16_t> cv_train_indices;
        b_vector<uint16_t> cv_test_indices;

        uint16_t test_start = static_cast<uint16_t>(fold * fold_size);
        uint16_t test_end = (fold == k_folds - 1) ? static_cast<uint16_t>(allTrainIndices.size())
                                                 : static_cast<uint16_t>((fold + 1) * fold_size);

        for (uint16_t i = 0; i < allTrainIndices.size(); ++i) {
            uint16_t sampleIndex = allTrainIndices[i];
            if (i >= test_start && i < test_end) {
                cv_test_indices.push_back(sampleIndex);
            } else {
                cv_train_indices.push_back(sampleIndex);
            }
        }

        if (cv_train_indices.empty() || cv_test_indices.empty()) {
            continue;
        }

        dataList.clear();
        dataList.reserve(config.num_trees);

        uint16_t cv_train_size = cv_train_indices.size();
        uint16_t bootstrap_sample_size = cv_train_size;
        if (config.use_bootstrap) {
            float desired = cv_train_size * config.boostrap_ratio;
            bootstrap_sample_size = static_cast<uint16_t>(std::max<float>(1.0f, std::round(desired)));
        }

        for (uint16_t tree_idx = 0; tree_idx < config.num_trees; ++tree_idx) {
            ID_vector<uint16_t,2> cv_tree_dataset;
            cv_tree_dataset.reserve(bootstrap_sample_size);

            auto tree_rng = rng.deriveRNG(fold * 1000 + tree_idx);

            if (config.use_bootstrap) {
                for (uint16_t j = 0; j < bootstrap_sample_size; ++j) {
                    uint16_t idx_in_cv_train = static_cast<uint16_t>(tree_rng.bounded(cv_train_size));
                    cv_tree_dataset.push_back(cv_train_indices[idx_in_cv_train]);
                }
            } else {
                std::vector<uint16_t> indices_copy(cv_train_indices.begin(), cv_train_indices.end());
                for (uint16_t t = 0; t < bootstrap_sample_size; ++t) {
                    uint16_t j = static_cast<uint16_t>(t + tree_rng.bounded(cv_train_size - t));
                    std::swap(indices_copy[t], indices_copy[j]);
                    cv_tree_dataset.push_back(indices_copy[t]);
                }
            }

            dataList.push_back(std::move(cv_tree_dataset));
        }

        MakeForest();

        int total_nodes = 0;
        for (uint16_t i = 0; i < config.num_trees; ++i) {
            total_nodes += root[i].countNodes();
        }
        if (config.num_trees > 0) {
            avg_nodes_out += static_cast<float>(total_nodes) / config.num_trees;
        }

        for (uint16_t idx : cv_test_indices) {
            if (idx >= train_data.allSamples.size()) {
                continue;
            }
            const auto& sample = train_data.allSamples[idx];
            ConsensusResult consensus = computeConsensus(sample);
            if (consensus.total_votes == 0) {
                continue;
            }
            EvaluationSample eval{};
            eval.actual_label = sample.label;
            eval.predicted_label = consensus.predicted_label;
            eval.votes = consensus.votes;
            eval.total_votes = consensus.total_votes;
            eval.consensus = consensus.consensus;
            aggregated.push_back(eval);
        }

        valid_folds++;
    }

    if (valid_folds > 0) {
        avg_nodes_out /= valid_folds;
    } else {
        avg_nodes_out = 0.0f;
    }

    dataList = original_dataList;
    return aggregated;
}

RandomForest::MetricsSummary RandomForest::computeMetricsForThreshold(const std::vector<EvaluationSample>& samples, float threshold) {
    MetricsSummary metrics;
    if (samples.empty()) {
        return metrics;
    }

    std::vector<uint32_t> tp(config.num_labels, 0);
    std::vector<uint32_t> fp(config.num_labels, 0);
    std::vector<uint32_t> fn(config.num_labels, 0);

    uint32_t correct = 0;

    for (const auto& sample : samples) {
        if (sample.total_votes == 0) {
            continue;
        }
        metrics.total_samples++;

        bool accepted = sample.consensus >= threshold;
        if (!accepted) {
            if (sample.actual_label < config.num_labels) {
                fn[sample.actual_label]++;
            }
            continue;
        }

        metrics.predicted_samples++;

        if (sample.predicted_label == sample.actual_label && sample.predicted_label < config.num_labels) {
            tp[sample.actual_label]++;
            correct++;
        } else {
            if (sample.predicted_label < config.num_labels) {
                fp[sample.predicted_label]++;
            }
            if (sample.actual_label < config.num_labels) {
                fn[sample.actual_label]++;
            }
        }
    }

    uint64_t total_tp = 0;
    uint64_t total_fp = 0;
    uint64_t total_fn = 0;
    for (uint16_t label = 0; label < config.num_labels; ++label) {
        total_tp += tp[label];
        total_fp += fp[label];
        total_fn += fn[label];
    }

    metrics.coverage = metrics.total_samples ? static_cast<float>(metrics.predicted_samples) / metrics.total_samples : 0.0f;
    metrics.accuracy = metrics.total_samples ? static_cast<float>(correct) / metrics.total_samples : 0.0f;

    float precision = (total_tp + total_fp) ? static_cast<float>(total_tp) / static_cast<float>(total_tp + total_fp) : 0.0f;
    float recall = (total_tp + total_fn) ? static_cast<float>(total_tp) / static_cast<float>(total_tp + total_fn) : 0.0f;
    metrics.precision = precision;
    metrics.recall = recall;

    if (precision + recall > 0.0f) {
        metrics.f1 = 2.0f * precision * recall / (precision + recall);
    } else {
        metrics.f1 = 0.0f;
    }

    auto fbeta = [](float p, float r, float beta) {
        float beta_sq = beta * beta;
        float denom = (beta_sq * p) + r;
        if (denom <= 0.0f) {
            return 0.0f;
        }
        return (1.0f + beta_sq) * p * r / denom;
    };

    metrics.f0_5 = fbeta(precision, recall, 0.5f);
    metrics.f2 = fbeta(precision, recall, 2.0f);

    return metrics;
}

float RandomForest::computeObjectiveScore(const MetricsSummary& metrics, Rf_metric_scores flags) {
    uint16_t flag_value = static_cast<uint16_t>(flags);

    if (flag_value == PRECISION) {
        return metrics.f0_5;
    }
    if (flag_value == RECALL) {
        return metrics.f2;
    }
    if ((flag_value & PRECISION) && (flag_value & RECALL) && !(flag_value & F1_SCORE)) {
        return metrics.f1;
    }

    float total = 0.0f;
    int count = 0;
    if (flag_value & ACCURACY) {
        total += metrics.accuracy;
        count++;
    }
    if (flag_value & PRECISION) {
        total += metrics.precision;
        count++;
    }
    if (flag_value & RECALL) {
        total += metrics.recall;
        count++;
    }
    if (flag_value & F1_SCORE) {
        total += metrics.f1;
        count++;
    }

    if (count == 0) {
        return metrics.accuracy;
    }
    return total / count;
}

RandomForest::ThresholdSearchResult RandomForest::findBestThreshold(const std::vector<EvaluationSample>& samples, Rf_metric_scores flags) {
    ThresholdSearchResult result;
    result.threshold = 0.5f;
    result.score = -1.0f;

    if (samples.empty()) {
        return result;
    }

    std::set<float> candidate_thresholds;
    candidate_thresholds.insert(0.0f);
    candidate_thresholds.insert(1.0f);

    for (const auto& sample : samples) {
        if (sample.total_votes == 0) {
            continue;
        }
        candidate_thresholds.insert(sample.consensus);
    }

    for (float threshold : candidate_thresholds) {
        MetricsSummary metrics = computeMetricsForThreshold(samples, threshold);
        float score = computeObjectiveScore(metrics, flags);

        if (score > result.score + 1e-6f ||
            (std::fabs(score - result.score) <= 1e-6f && metrics.coverage > result.metrics.coverage + 1e-6f) ||
            (std::fabs(score - result.score) <= 1e-6f && std::fabs(metrics.coverage - result.metrics.coverage) <= 1e-6f && threshold < result.threshold)) {
            result.threshold = threshold;
            result.score = score;
            result.metrics = metrics;
        }
    }

    if (result.score < 0.0f) {
        result.metrics = computeMetricsForThreshold(samples, result.threshold);
        result.score = computeObjectiveScore(result.metrics, flags);
    }

    return result;
}


int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Random Forest PC Training\n";
    RandomForest forest;
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
    b_vector<pair<uint16_t, float>> precision = result[0];
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
    b_vector<pair<uint16_t, float>> recall = result[1];
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
    b_vector<pair<uint16_t, float>> f1_scores = result[2];
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
    b_vector<pair<uint16_t, float>> accuracies = result[3];
    for (const auto& acc : accuracies) {
      std::cout << "Label: " << (int)acc.first << " - " << acc.second << "\n";
    }
    float avgAccuracy = 0.0f;
    for (const auto& acc : accuracies) {
      avgAccuracy += acc.second;
    }
    avgAccuracy /= accuracies.size();
    std::cout << "Avg: " << avgAccuracy << "\n";

    float result_score = forest.predict(forest.test_data, static_cast<Rf_metric_scores>(forest.config.metric_score));
    forest.config.result_score = result_score;
    forest.saveConfig();
    std::cout << "result score: " << result_score << "\n";

    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Total time: " << elapsed.count() << " seconds\n ";
    return 0;
}