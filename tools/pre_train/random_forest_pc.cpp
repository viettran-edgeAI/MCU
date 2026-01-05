#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <set>
#include <sys/stat.h>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#ifdef _OPENMP
#include <omp.h>
#endif
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

static constexpr uint64_t RF_MAX_NODES = 0xFFFFFFFFu; 


class RandomForest{
public:
    Rf_data base_data;      // base data / baseFile
    Rf_data train_data;
    Rf_data test_data;
    Rf_data validation_data;

    std::string model_name;

    Rf_config config;
    node_predictor pre;

private:
    using TreeSampleIDs = ID_vector<uint32_t, 8>; // 3-bit allow up to 7 instances per sample ID
    vector<Rf_tree> root;                     // vector storing root nodes of trees (now manages SPIFFS filenames)
    vector<TreeSampleIDs> dataList; // list of training data sample IDs for each tree, matches MCU ID_vector layout
    Rf_random rng;
    mutable std::mutex peak_nodes_mutex;  // Mutex for thread-safe peak_nodes access

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
        std::cout << "Loading dataset...\n";
        base_data.loadCSVData(temp_base_data, config.num_features);
        
        // OOB.reserve(numTree);
        dataList.reserve(config.num_trees);
        std::cout << "Splitting dataset...\n";
        splitData(config.train_ratio);
        // std::cout << "Dataset split completed.\n";
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
        std::remove(temp_base_data);

        // train node predictor
        pre.init(node_log_path);
        pre.train();
        float pre_ac = pre.get_accuracy();
        pre.accuracy = static_cast<uint8_t>(std::min(100.0f, std::max(0.0f, pre_ac)));
        pre.trained_sample_count = static_cast<uint32_t>(config.num_samples);
        pre.save_model(node_predictor_path);
    }

    void MakeForest(){
        root.clear();
        root.resize(config.num_trees);  // Pre-allocate for parallel access
        // std::cout << "🌳 Building Random Forest with " << (int)config.num_trees << " trees...\n";
        
        #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic)
        #endif
        for(int i = 0; i < static_cast<int>(config.num_trees); i++){
            Rf_tree tree("");
            build_tree_parallel(tree, dataList[i]);
            root[i] = std::move(tree);
        }
        // uint32_t total_nodes = 0;
        // for(const auto& tree : root){
        //     total_nodes += tree.countNodes();   
        // }
        // std::cout << "Total nodes in forest: " << total_nodes << std::endl;
    }

    void build_model(){
        std::cout << "\n🌳 Building Random Forest Model...\n";
        
        // Set model parameters - use first value from range or config file value if enabled
        if (config.overwrite[0]) {
            // min_split is explicitly set in config
            std::cout << "   Using min_split from config: " << config.min_split << "\n";
        } else if (!config.min_split_range.empty()) {
            config.min_split = config.min_split_range.front();
            std::cout << "   Using min_split default: " << config.min_split << "\n";
        }
        
        if (config.overwrite[1]) {
            // min_leaf is explicitly set in config
            std::cout << "   Using min_leaf from config: " << config.min_leaf << "\n";
        } else if (!config.min_leaf_range.empty()) {
            config.min_leaf = config.min_leaf_range.front();
            std::cout << "   Using min_leaf default: " << config.min_leaf << "\n";
        }
        
        if (config.overwrite[2]) {
            // max_depth is explicitly set in config
            std::cout << "   Using max_depth from config: " << config.max_depth << "\n";
        } else if (!config.max_depth_range.empty()) {
            config.max_depth = config.max_depth_range.front();
            std::cout << "   Using max_depth default: " << config.max_depth << "\n";
        }

        // prepare node predictor log file
        std::remove(node_log_path.c_str());
        std::ofstream file(node_log_path);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to create node_predictor log file\n";
            return;
        }
        file << "min_split,min_leaf,max_depth,max_nodes\n";
        file.close();

        // Prepare data for forest construction
        ClonesData();
        
        // Build forest with parallel tree construction
        root.clear();
        root.resize(config.num_trees);  // Pre-allocate for parallel access
        std::atomic<uint32_t> max_nodes{0};
        std::atomic<uint32_t> trees_completed{0};
        
        #ifdef _OPENMP
        int num_threads = omp_get_max_threads();
        std::cout << "🧵 Using " << num_threads << " threads for parallel tree building\n";
        #pragma omp parallel for schedule(dynamic)
        #endif
        for(int i = 0; i < static_cast<int>(config.num_trees); i++){
            // Build tree
            Rf_tree tree("");
            build_tree_parallel(tree, dataList[i]);
            root[i] = std::move(tree);
            
            uint32_t node_count = root[i].countNodes();
            uint32_t current_max = max_nodes.load();
            while(node_count > current_max && !max_nodes.compare_exchange_weak(current_max, node_count)) {}
            
            // Thread-safe progress update
            uint32_t completed = ++trees_completed;
            #ifdef _OPENMP
            #pragma omp critical
            #endif
            {
                float progress = static_cast<float>(completed) / config.num_trees;
                int bar_width = 50;
                int pos = static_cast<int>(bar_width * progress);
                std::cout << "\r[";
                for (int j = 0; j < bar_width; ++j) {
                    if (j < pos) std::cout << "█";
                    else if (j == pos) std::cout << "▓";
                    else std::cout << "░";
                }
                std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0f) << "% ";
                std::cout << "(" << completed << "/" << config.num_trees << " trees)";
                std::cout.flush();
            }
        }
        
        if (max_nodes.load() > 0) {
            std::ofstream log_file(node_log_path, std::ios::app);
            if (log_file.is_open()) {
                log_file << static_cast<int>(config.min_split) << ","
                        << static_cast<int>(config.min_leaf) << ","
                        << static_cast<int>(config.max_depth) << ","
                        << static_cast<int>(max_nodes.load()) << "\n";
            }
        }

        std::cout << "\n✅ Forest construction complete!\n";
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
        std::cout << std::fixed << std::setprecision(3) << "Average nodes per tree: " << (float)totalNodes / config.num_trees << "\n";
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

        ConsensusResult computeConsensus(const Rf_sample& sample, const vector<uint16_t>* tree_indices = nullptr);
        vector<EvaluationSample> collectOOBSamples(uint16_t min_votes_required, vector<uint16_t>* vote_histogram = nullptr);
        vector<EvaluationSample> collectValidationSamples(const Rf_data& dataset);
        vector<EvaluationSample> collectCrossValidationSamples(float& max_nodes_out);
        MetricsSummary computeMetricsForThreshold(const vector<EvaluationSample>& samples, float threshold);
        float computeObjectiveScore(const MetricsSummary& metrics, Rf_metric_scores flags);
        ThresholdSearchResult findBestThreshold(const vector<EvaluationSample>& samples, Rf_metric_scores flags);

        std::string buildMetadataPath() const {
            if (config.data_path.empty()) {
                return "";
            }

            size_t lastSlash = config.data_path.find_last_of("/\\");
            std::string directory = (lastSlash == std::string::npos) ? ""
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
    }

    void generateFilePaths() {
        node_log_path = result_folder + model_name + "_nlg.csv";
        node_predictor_path = result_folder + model_name + "_npd.bin";
        result_config_path = result_folder + model_name + "_config.json";
    }

    // Split data into training and testing sets (or validation if enabled)
    // Synchronized with ESP32 version AND old_forest behavior: uses ALL samples
    void splitData(float trainRatio) {
        size_t maxID = config.num_samples;  // total number of samples
        uint32_t trainSize = static_cast<uint32_t>(maxID * config.train_ratio);
        uint32_t testSize = static_cast<uint32_t>(maxID * config.test_ratio);
        uint32_t validationSize = (config.training_score == "valid_score") ? static_cast<uint32_t>(maxID * config.valid_ratio) : 0;
        
        // Pre-allocate output vectors
        train_data.allSamples.reserve(trainSize);
        test_data.allSamples.reserve(maxID - trainSize);  // May be larger than testSize
        if (validationSize > 0) {
            validation_data.allSamples.reserve(validationSize);
        }
        
        // Use ID_vector with 1-bit (like old version) - tracks presence only, NOT counts
        // This means push_back() on duplicate IDs won't increase size()
        // Result: sampling WITHOUT replacement despite appearing to allow duplicates
        ID_vector<uint32_t, 1> train_sampleIDs;  // 1 bit: only tracks presence
        if (maxID > 0) {
            train_sampleIDs.set_ID_range(0, static_cast<uint32_t>(maxID - 1));
        }
        
        // Sample train data - appears to allow duplicates but 1-bit ID_vector prevents it
        while (train_sampleIDs.size() < trainSize) {
            uint32_t sampleId = static_cast<uint32_t>(rng.bounded(static_cast<uint32_t>(maxID)));
            train_sampleIDs.push_back(sampleId);  // Duplicate adds won't increase size()
        }
        
        // Sample test data WITHOUT REPLACEMENT from remaining samples
        ID_vector<uint32_t, 1> test_sampleIDs;
        if (maxID > 0) {
            test_sampleIDs.set_ID_range(0, static_cast<uint32_t>(maxID - 1));
        }
        while (test_sampleIDs.size() < testSize) {
            uint32_t i = static_cast<uint32_t>(rng.bounded(static_cast<uint32_t>(maxID)));
            if (!train_sampleIDs.contains(i)) {
                test_sampleIDs.push_back(i);
            }
        }
        
        // Sample validation data WITHOUT REPLACEMENT from remaining samples
        ID_vector<uint32_t, 1> validation_sampleIDs;
        if (config.training_score == "valid_score") {
            if (maxID > 0) {
                validation_sampleIDs.set_ID_range(0, static_cast<uint32_t>(maxID - 1));
            }
            while (validation_sampleIDs.size() < validationSize) {
                uint32_t i = static_cast<uint32_t>(rng.bounded(static_cast<uint32_t>(maxID)));
                if (!train_sampleIDs.contains(i) && !test_sampleIDs.contains(i)) {
                    validation_sampleIDs.push_back(i);
                }
            }
        }
        
        // Distribute ALL samples to train/test/validation like old version
        for (uint32_t id = 0; id < maxID; id++) {
            if (train_sampleIDs.contains(id)) {
                train_data.allSamples.push_back(base_data.allSamples[id]);
            } else if (test_sampleIDs.contains(id)) {
                test_data.allSamples.push_back(base_data.allSamples[id]);
            } else if (config.training_score == "valid_score") {
                validation_data.allSamples.push_back(base_data.allSamples[id]);
            } else {
                // Any remaining samples go to test set (maintains old behavior)
                test_data.allSamples.push_back(base_data.allSamples[id]);
            }
        }
        
        std::cout << "✅ Data split complete: " << train_data.allSamples.size() << " train, " 
                  << test_data.allSamples.size() << " test";
        if (config.training_score == "valid_score") {
            std::cout << ", " << validation_data.allSamples.size() << " validation";
        }
        std::cout << std::endl;
    }

    // ---------------------------------------------------------------------------------
    // create dataset for each tree from train set
    void ClonesData() {
        // Clear previous data
        dataList.clear();
        dataList.reserve(config.num_trees);
        // Use actual train set size for sampling consistency with ESP32
        const uint32_t numSample = static_cast<uint32_t>(train_data.allSamples.size());
        uint32_t numSubSample;
        if (config.use_bootstrap) {
            numSubSample = numSample; // draw N with replacement
        } else {
            numSubSample = static_cast<uint32_t>(numSample * config.boostrap_ratio);
        }
        
        
        // Track hashes of each tree dataset to avoid duplicates across trees
        unordered_set_s<uint64_t> seen_hashes;
        seen_hashes.reserve(config.num_trees * 2);

        for (uint16_t i = 0; i < config.num_trees; i++) {
            TreeSampleIDs treeDataset; // 3-bit counts keep PC sampling aligned with MCU encoding
            if (numSample > 1) {
                treeDataset.set_ID_range(0, numSample - 1);
            } else if (numSample == 1) {
                treeDataset.set_maxID(0);
            }

            // Derive a deterministic per-tree RNG; retry with nonce if duplicate detected
            uint64_t nonce = 0;
            while (true) {
                treeDataset.clear();
                auto tree_rng = rng.deriveRNG(i, nonce);

                if (config.use_bootstrap) {
                    // Bootstrap sampling: allow duplicates; bag draws = numSample
                    for (uint32_t j = 0; j < numSubSample; ++j) {
                        if (numSample == 0) {
                            break;
                        }
                        uint32_t idx = static_cast<uint32_t>(tree_rng.bounded(numSample));
                        treeDataset.push_back(idx);
                    }
                } else {
                    vector<uint32_t> arr(numSample);
                    for (uint32_t t = 0; t < numSample; ++t) {
                        arr[t] = t;
                    }
                    for (uint32_t t = 0; t < numSubSample; ++t) {
                        uint32_t remaining = numSample - t;
                        if (remaining == 0) {
                            break;
                        }
                        uint32_t j = static_cast<uint32_t>(t + tree_rng.bounded(remaining));
                        std::swap(arr[t], arr[j]);
                        treeDataset.push_back(arr[t]);
                    }
                }
                // Check for duplicate dataset across trees
                uint64_t h = Rf_random::hashIDVector(treeDataset);
                if (seen_hashes.find(h) == seen_hashes.end()) {
                    seen_hashes.insert(h);
                    break; // unique, accept
                }
                nonce++;
                if (nonce > 8) {
                    auto temp_vec = treeDataset;  // Copy current state
                    treeDataset.clear();
                    
                    // Re-add samples with slight modifications
                    const uint32_t max_seed = std::min<uint32_t>(5u, static_cast<uint32_t>(temp_vec.size()));
                    for (uint32_t k = 0; k < max_seed; ++k) {
                        uint32_t original_id = (k < temp_vec.size()) ? static_cast<uint32_t>(k) : 0u;
                        uint32_t span = (numSample == 0) ? 1u : numSample;
                        uint32_t modified_id = static_cast<uint32_t>((original_id + k + i) % span);
                        treeDataset.push_back(modified_id);
                    }
                    
                    // Add remaining samples from original
                    uint32_t limit = std::min<uint32_t>(numSubSample, static_cast<uint32_t>(temp_vec.size()));
                    for (uint32_t k = 5; k < limit; ++k) {
                        if (numSample == 0) {
                            break;
                        }
                        uint32_t id = k % numSample; // Simplified access
                        treeDataset.push_back(id);
                    }
                    seen_hashes.insert(Rf_random::hashIDVector(treeDataset));
                    break;
                }
            }
            dataList.push_back(treeDataset);
        }
    }


    struct SplitInfo {
        float gain = -1.0f;
        uint16_t featureID = 0;
        uint16_t threshold_slot = 0;
        uint16_t threshold_value = 0;
        uint32_t leftCount = 0;
        uint32_t rightCount = 0;
    };

    struct NodeStats {
        vector<uint32_t> labelCounts;
        uint16_t majorityLabel;
        uint32_t totalSamples;
        bool pure;

        NodeStats(uint16_t numLabels) : majorityLabel(0), totalSamples(0), pure(true) {
            labelCounts.resize(numLabels, 0);
        }

        void resetCounts(uint16_t numLabels) {
            if (labelCounts.size() < numLabels) {
                labelCounts.resize(numLabels, 0);
            } else {
                for (uint16_t i = 0; i < numLabels; ++i) {
                    labelCounts[i] = 0;
                }
            }
        }

        // New: analyze a slice [begin,end) over a shared indices array
        void analyzeSamplesRange(const vector<uint32_t>& indices, uint32_t begin, uint32_t end,
                                 uint16_t numLabels, const Rf_data& data) {
            totalSamples = (begin < end) ? (end - begin) : 0;
            pure = true;
            uint32_t maxCount = 0;
            bool hasLabel = false;
            uint16_t firstLabel = 0;

            resetCounts(numLabels);

            for (uint32_t k = begin; k < end; ++k) {
                uint32_t sampleID = indices[k];
                if (sampleID >= data.allSamples.size()) {
                    continue;
                }

                uint16_t label = data.allSamples[sampleID].label;
                if (label >= numLabels) {
                    continue;
                }

                if (!hasLabel) {
                    firstLabel = label;
                    hasLabel = true;
                } else if (pure && label != firstLabel) {
                    pure = false;
                }

                labelCounts[label]++;
                if (labelCounts[label] > maxCount) {
                    maxCount = labelCounts[label];
                    majorityLabel = label;
                }
            }
        }

        bool isPure() const {
            return pure;
        }
    };

    // New: Range-based variant operating on a shared indices array
    SplitInfo findBestSplitRange(const vector<uint32_t>& indices, uint32_t begin, uint32_t end,
                                 const vector<uint16_t>& selectedFeatures, bool use_Gini, uint16_t numLabels) {
        SplitInfo bestSplit;
        uint32_t totalSamples = (begin < end) ? (end - begin) : 0;
        if (totalSamples < 2) return bestSplit;

        // Base label counts
        b_vector<uint32_t,16> baseLabelCounts(numLabels, 0);
        for (uint32_t k = begin; k < end; ++k) {
            uint32_t sid = indices[k];
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

        const uint8_t quantBits = QuantizationHelper::sanitizeBits(config.quantization_coefficient);
        const uint16_t numCandidates = static_cast<uint16_t>(1u << quantBits);
        if (numCandidates == 0) {
            return bestSplit;
        }
        const uint16_t maxThresholdValue = static_cast<uint16_t>(numCandidates - 1u);

        b_vector<uint32_t, 16> leftCounts;
        b_vector<uint32_t, 16> rightCounts;
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
                for (uint32_t k = begin; k < end; ++k) {
                    uint32_t sid = indices[k];
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
                for (uint16_t slot = 0; slot < numCandidates; ++slot) {
                    const uint16_t thresholdValue = (slot > maxThresholdValue) ? maxThresholdValue : slot;

                    uint32_t leftTotal = 0;
                    uint32_t rightTotal = 0;
                    for (uint16_t i = 0; i < numLabels; ++i) {
                        leftCounts[i] = 0;
                        rightCounts[i] = 0;
                    }

                    for (uint32_t k = begin; k < end; ++k) {
                        uint32_t sid = indices[k];
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
                            bestSplit.threshold_slot = static_cast<uint16_t>(slot);
                        bestSplit.threshold_value = thresholdValue;
                        bestSplit.leftCount = leftTotal;
                        bestSplit.rightCount = rightTotal;
                    }
                }
            }
        }
        return bestSplit;
    }

    // Thread-safe version of build_tree for parallel execution
    void build_tree_parallel(Rf_tree& tree, TreeSampleIDs& sampleIDs) {
        tree.nodes.clear();
        if (train_data.allSamples.empty()) return;
        
        // Thread-local RNG for feature selection (avoid race conditions on shared rng)
        thread_local Rf_random local_rng(static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())), true);
        
        // Queue for breadth-first processing
        vector<NodeToBuild> queue_nodes;
        queue_nodes.reserve(200);

        // Build a single contiguous index array for this tree
        vector<uint32_t> indices;
        indices.reserve(static_cast<size_t>(sampleIDs.size()));
        for (auto sid : sampleIDs) {
            indices.push_back(static_cast<uint32_t>(sid));
        }
        
        // Create root node
        Tree_node rootNode;
        tree.nodes.push_back(rootNode);
        queue_nodes.push_back(NodeToBuild(0, 0, static_cast<uint32_t>(indices.size()), 0));

        size_t peak_queue_size = 0;
        auto track_peak = [&](size_t cur) {
            if (cur > peak_queue_size) peak_queue_size = cur;
        };
        track_peak(queue_nodes.size());
        
        while (!queue_nodes.empty()) {
            NodeToBuild current = std::move(queue_nodes.front());
            queue_nodes.erase(queue_nodes.begin());
            
            NodeStats stats(config.num_labels);
            stats.analyzeSamplesRange(indices, current.begin, current.end, config.num_labels, train_data);

            if(current.nodeIndex >= RF_MAX_NODES){
                uint8_t leafLabel = stats.majorityLabel;
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            bool shouldBeLeaf = false;
            uint16_t leafLabel = stats.majorityLabel;
            
            if (stats.isPure() && stats.totalSamples > 0) {
                shouldBeLeaf = true;
                leafLabel = stats.majorityLabel;
            } else if (stats.totalSamples < config.min_split || current.depth >= config.max_depth - 1) {
                shouldBeLeaf = true;
            }
            if (shouldBeLeaf) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            // Random feature subset using thread-local RNG
            uint16_t num_selected_features;
            if(config.num_trees > 1) num_selected_features = static_cast<uint16_t>(sqrt(config.num_features));
            else num_selected_features = config.num_features;

            if (num_selected_features == 0) num_selected_features = 1;
            vector<uint16_t> selectedFeatures;
            uint16_t N = static_cast<uint16_t>(config.num_features);
            if (N == 0) N = 1;
            uint16_t K = num_selected_features > N ? N : num_selected_features;
            selectedFeatures.reserve(K);
            vector<uint8_t> featureUsed(N, 0);
            for (uint16_t j = N - K; j < N; ++j) {
                uint16_t t = static_cast<uint16_t>(local_rng.bounded(j + 1));
                uint16_t candidate = (t < N) ? t : (N - 1);
                if (featureUsed[candidate] == 0) {
                    featureUsed[candidate] = 1;
                    selectedFeatures.push_back(candidate);
                    continue;
                }

                uint16_t fallback = (j < N) ? j : (N - 1);
                if (featureUsed[fallback] == 0) {
                    featureUsed[fallback] = 1;
                    selectedFeatures.push_back(fallback);
                    continue;
                }

                for (uint16_t scan = 0; scan < N; ++scan) {
                    if (featureUsed[scan] == 0) {
                        featureUsed[scan] = 1;
                        selectedFeatures.push_back(scan);
                        break;
                    }
                }
            }
            if (selectedFeatures.empty()) {
                selectedFeatures.push_back(0);
            }
            
            SplitInfo bestSplit = findBestSplitRange(indices, current.begin, current.end,
                                                     selectedFeatures, config.use_gini, config.num_labels);

            if (bestSplit.leftCount < config.min_leaf || bestSplit.rightCount < config.min_leaf) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }

            float adaptive_threshold = config.impurity_threshold;
            if (adaptive_threshold > 0.0f && stats.totalSamples > config.min_split) {
                double scale = 1.0 / (1.0 + std::log2(static_cast<double>(stats.totalSamples) + 1.0));
                if (!(scale > 0.0) || !std::isfinite(scale)) {
                    scale = 1.0;
                }
                adaptive_threshold = static_cast<float>(adaptive_threshold * scale);
                if (adaptive_threshold < 0.0001f) {
                    adaptive_threshold = 0.0001f;
                }
            }

            if (bestSplit.gain <= adaptive_threshold) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }
            
            if (tree.nodes.size() + 2 > static_cast<size_t>(RF_MAX_NODES)) {
                tree.nodes[current.nodeIndex].setIsLeaf(true);
                tree.nodes[current.nodeIndex].setLabel(leafLabel);
                tree.nodes[current.nodeIndex].setFeatureID(0);
                continue;
            }

            tree.nodes[current.nodeIndex].setFeatureID(bestSplit.featureID);
            tree.nodes[current.nodeIndex].setThresholdSlot(bestSplit.threshold_slot);
            tree.nodes[current.nodeIndex].setIsLeaf(false);
            
            uint32_t iLeft = current.begin;
            for (uint32_t k = current.begin; k < current.end; ++k) {
                uint32_t sid = indices[k];
                if (sid < train_data.allSamples.size() &&
                    train_data.allSamples[sid].features[bestSplit.featureID] <= bestSplit.threshold_value) {
                    if (k != iLeft) {
                        uint32_t tmp = indices[iLeft];
                        indices[iLeft] = indices[k];
                        indices[k] = tmp;
                    }
                    ++iLeft;
                }
            }
            uint32_t leftBegin = current.begin;
            uint32_t leftEnd = iLeft;
            uint32_t rightBegin = iLeft;
            uint32_t rightEnd = current.end;

            uint32_t leftChildIndex = static_cast<uint32_t>(tree.nodes.size());
            uint32_t rightChildIndex = leftChildIndex + 1;
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
            track_peak(queue_nodes.size());
        }
        
        // Thread-safe update of peak_nodes
        float peak_nodes_percent = (float)peak_queue_size / (float)tree.nodes.size() * 100.0f;
        {
            std::lock_guard<std::mutex> lock(peak_nodes_mutex);
            pre.peak_nodes.push_back(peak_nodes_percent);
        }
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
        
        // Decision tree mode: OOB evaluation is not applicable with single tree
        if (config.num_trees == 1) {
            std::cout << "⚠️  Decision Tree Mode: OOB evaluation requires multiple trees.\n";
        }
        
        bool use_cv = (config.training_score == "k_fold_score");
        const int num_runs = 1; // Single run with fixed seed is sufficient

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

        uint32_t candidate_count = config.min_split_range.size() * config.min_leaf_range.size() * config.max_depth_range.size();
        if (candidate_count == 0) candidate_count = 1;
        uint32_t total_iterations = candidate_count;
        if (total_iterations == 0) total_iterations = 1;
        uint32_t current_iteration = 0;

        auto updateProgress = [&](float score, uint16_t min_split, uint16_t min_leaf, uint16_t max_depth) {
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
            // std::cout << " | split=" << min_split << ", leaf=" << min_leaf << ", depth=" << max_depth;
            std::cout.flush();
        };

        uint16_t best_min_split = config.min_split;
        uint16_t best_min_leaf = config.min_leaf;
        uint16_t best_max_depth = config.max_depth;
        float best_score = -1.0f;
        MetricsSummary best_metrics;
        bool best_found = false;

        for (uint16_t current_min_split : config.min_split_range) {
            for (uint16_t current_min_leaf : config.min_leaf_range) {
                for (uint16_t current_max_depth : config.max_depth_range) {
                    config.min_split = current_min_split;
                    config.min_leaf = current_min_leaf;
                    config.max_depth = current_max_depth;

                    float max_nodes = 0.0f; // Track maximum nodes across runs
                    bool best_forest_saved = false;
                    vector<EvaluationSample> aggregated_samples;
                    aggregated_samples.reserve(train_data.allSamples.size());
                    ThresholdSearchResult aggregated_result;
                    aggregated_result.score = -1.0f;
                    aggregated_result.threshold = 0.5f;

                    if (use_cv) {
                        float cv_max_nodes = 0.0f;
                        aggregated_samples = collectCrossValidationSamples(cv_max_nodes);
                        max_nodes = cv_max_nodes;

                        ClonesData();
                        MakeForest();

                        uint32_t forest_max_nodes = 0;
                        for (uint16_t t = 0; t < config.num_trees; ++t) {
                            uint32_t tree_nodes = root[t].countNodes();
                            if (tree_nodes > forest_max_nodes) {
                                forest_max_nodes = tree_nodes;
                            }
                        }
                        max_nodes = static_cast<float>(forest_max_nodes);

                        aggregated_result = findBestThreshold(aggregated_samples, config.metric_score);

                        saveForest(temp_folder, true);
                        best_forest_saved = true;

                        current_iteration++;
                        updateProgress(aggregated_result.score, current_min_split, current_min_leaf, current_max_depth);
                    } else {
                        float total_run_score = 0.0f;
                        float best_run_score = -1.0f;

                        for (int run = 0; run < num_runs; ++run) {
                            ClonesData();
                            MakeForest();

                            vector<EvaluationSample> run_samples;
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

                            uint32_t run_max_nodes = 0;
                            for (uint16_t t = 0; t < config.num_trees; ++t) {
                                uint32_t tree_nodes = root[t].countNodes();
                                if (tree_nodes > run_max_nodes) {
                                    run_max_nodes = tree_nodes;
                                }
                            }
                            if (run_max_nodes > max_nodes) {
                                max_nodes = static_cast<float>(run_max_nodes);
                            }

                            if (run_result.score > best_run_score) {
                                best_run_score = run_result.score;
                                saveForest(temp_folder, true);
                                best_forest_saved = true;
                            }

                            current_iteration++;
                            updateProgress(run_result.score, current_min_split, current_min_leaf, current_max_depth);
                        }

                        aggregated_result = findBestThreshold(aggregated_samples, config.metric_score);
                    }

                    if (max_nodes > 0.0f) {
                        std::ofstream log_file(node_log_path, std::ios::app);
                        if (log_file.is_open()) {
                            log_file << static_cast<int>(config.min_split) << ","
                                    << static_cast<int>(config.min_leaf) << ","
                                    << static_cast<int>(config.max_depth) << ","
                                    << static_cast<int>(std::round(max_nodes)) << "\n";
                        }
                    }

                    if (aggregated_result.score > best_score && best_forest_saved) {
                        best_score = aggregated_result.score;
                        best_min_split = config.min_split;
                        best_min_leaf = config.min_leaf;
                        best_max_depth = config.max_depth;
                        best_metrics = aggregated_result.metrics;
                        best_found = true;
                        copyDirectory(temp_folder, final_folder);
                    }
                }
            }
        }

        std::cout << std::endl;
        if (best_found) {
            std::cout << "✅ Training Complete! " << std::endl;
            std::cout << "🏆 Best Score: " << best_score << std::endl;
            std::cout << "   - min_split: " << best_min_split << std::endl;
            std::cout << "   - min_leaf: " << best_min_leaf << std::endl;
            std::cout << "   - max_depth: " << best_max_depth << std::endl;
        } else {
            std::cout << "⚠️  No valid candidate found during training; retaining existing parameters.\n";
        }

        config.min_split = best_min_split;
        config.min_leaf = best_min_leaf;
        config.max_depth = best_max_depth;

        ClonesData();
        MakeForest();

        uint16_t min_votes_required = std::max<uint16_t>(1, static_cast<uint16_t>(std::ceil(config.num_trees * 0.15f)));
        vector<EvaluationSample> final_samples;
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

        // saveForest(result_folder);

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
        // Save config in both JSON and CSV formats
        config.saveConfig(result_config_path);

        // Determine MCU node layout requirements before writing
        auto bits_required = [](uint32_t value) -> uint8_t {
            uint8_t bits = 0;
            do {
                ++bits;
                value >>= 1;
            } while (value != 0 && bits < 32);
            return static_cast<uint8_t>(bits == 0 ? 1 : bits);
        };

        struct PackedLayout {
            uint8_t threshold_start = 1;
            uint8_t threshold_bits = 3;
            uint8_t feature_start = 4;
            uint8_t feature_bits = 1;
            uint8_t label_start = 5;
            uint8_t label_bits = 1;
            uint8_t child_start = 6;
            uint8_t child_bits = 1;

            uint8_t bits_per_node() const {
                return static_cast<uint8_t>(1 + threshold_bits + feature_bits + label_bits + child_bits);
            }

            void update_starts() {
                feature_start = static_cast<uint8_t>(threshold_start + threshold_bits);
                label_start = static_cast<uint8_t>(feature_start + feature_bits);
                child_start = static_cast<uint8_t>(label_start + label_bits);
            }
        } layout;

        uint32_t maxFeatureId = 0;
        uint32_t maxLabelId = 0;
        uint32_t maxChildIndex = 0;
        uint32_t maxNodesPerTree = 0;
        uint32_t maxThresholdSlot = 0;
        bool missingTree = false;

        for (uint16_t i = 0; i < config.num_trees; ++i) {
            const auto& tree = root[i];
            const size_t nodeCount = tree.nodes.size();
            if (nodeCount == 0) {
                missingTree = true;
                continue;
            }
            if (nodeCount > RF_MAX_NODES) {
                if (!silent) {
                    std::cout << "❌ Tree " << static_cast<int>(i)
                              << " exceeds MCU node limit (" << nodeCount << "/" << RF_MAX_NODES << ")\n";
                }
                return;
            }

            maxNodesPerTree = std::max(maxNodesPerTree, static_cast<uint32_t>(nodeCount));
            if (nodeCount > 0) {
                uint32_t upperIndex = static_cast<uint32_t>(nodeCount - 1);
                if (upperIndex > maxChildIndex) {
                    maxChildIndex = upperIndex;
                }
            }

            for (const auto& node : tree.nodes) {
                if (!node.getIsLeaf()) {
                    uint32_t leftIndex = node.getLeftChildIndex();
                    uint32_t rightIndex = node.getRightChildIndex();
                    if (leftIndex > maxChildIndex) {
                        maxChildIndex = leftIndex;
                    }
                    if (rightIndex > maxChildIndex) {
                        maxChildIndex = rightIndex;
                    }
                }
                uint32_t featureId = node.getFeatureID();
                uint32_t labelId = node.getLabel();
                uint32_t thresholdSlot = node.getThresholdSlot();
                if (featureId > maxFeatureId) {
                    maxFeatureId = featureId;
                }
                if (labelId > maxLabelId) {
                    maxLabelId = labelId;
                }
                if (thresholdSlot > maxThresholdSlot) {
                    maxThresholdSlot = thresholdSlot;
                }
            }
        }


        if (missingTree) {
            if (!silent) {
                std::cout << "❌ One or more trees are empty; aborting MCU export\n";
            }
            return;
        }

        if (config.num_features > 0) {
            uint32_t cfgMaxFeature = static_cast<uint32_t>(config.num_features - 1);
            if (cfgMaxFeature > maxFeatureId) {
                maxFeatureId = cfgMaxFeature;
            }
        }
        if (config.num_labels > 0) {
            uint32_t cfgMaxLabel = static_cast<uint32_t>(config.num_labels - 1);
            if (cfgMaxLabel > maxLabelId) {
                maxLabelId = cfgMaxLabel;
            }
        }

        if (maxNodesPerTree > 0) {
            uint32_t upperIndex = maxNodesPerTree - 1;
            if (upperIndex > maxChildIndex) {
                maxChildIndex = upperIndex;
            }
        }

        const uint32_t FOREST_MAGIC = 0x464F5253; // "FORS"
        std::string unified_path = folder_path + "/" + model_name + "_forest_mcu.bin";
        std::ofstream out(unified_path, std::ios::binary);
        if (!out.is_open()) {
            if (!silent) {
                std::cout << "❌ Failed to create unified forest file: " << unified_path << "\n";
            }
            return;
        }

        // Build a 64-bit layout for RAW format
        const uint8_t raw_word_bits = 64;
        uint8_t raw_feature_bits = std::max<uint8_t>(1, std::min<uint8_t>(16, bits_required(maxFeatureId)));
        uint8_t raw_label_bits = std::max<uint8_t>(1, std::min<uint8_t>(16, bits_required(maxLabelId)));
        uint8_t raw_threshold_bits = QuantizationHelper::sanitizeBits(static_cast<int>(config.quantization_coefficient));
        if (raw_threshold_bits == 0) raw_threshold_bits = 1;
        uint8_t remaining = static_cast<uint8_t>(raw_word_bits - 1 - raw_feature_bits - raw_label_bits - raw_threshold_bits);
        uint8_t raw_child_bits = remaining > 0 ? remaining : 1;

        // Compute starts
        uint8_t threshold_start64 = 1;
        uint8_t feature_start64 = static_cast<uint8_t>(threshold_start64 + raw_threshold_bits);
        uint8_t label_start64 = static_cast<uint8_t>(feature_start64 + raw_feature_bits);
        uint8_t child_start64 = static_cast<uint8_t>(label_start64 + raw_label_bits);

        // masks
        auto mask_for_bits64 = [](uint8_t bits) -> uint64_t {
            if (bits >= 64) return 0xFFFFFFFFFFFFFFFFull;
            if (bits == 0) return 0ull;
            return ((1ull << bits) - 1ull);
        };
        uint64_t featureMask64 = mask_for_bits64(raw_feature_bits);
        uint64_t labelMask64 = mask_for_bits64(raw_label_bits);
        uint64_t thresholdMask64 = mask_for_bits64(raw_threshold_bits);
        uint64_t childMask64 = mask_for_bits64(raw_child_bits);

        // header: magic + bits_per_node(u8) + tree_count(u8) + reserved byte
        out.write(reinterpret_cast<const char*>(&FOREST_MAGIC), sizeof(FOREST_MAGIC));
        uint8_t bits_per_node64 = static_cast<uint8_t>(1 + raw_threshold_bits + raw_feature_bits + raw_label_bits + raw_child_bits);
        if (bits_per_node64 == 0) bits_per_node64 = 64;
        uint8_t bytes_per_node64 = static_cast<uint8_t>((bits_per_node64 + 7) / 8);
        out.write(reinterpret_cast<const char*>(&bits_per_node64), sizeof(bits_per_node64));
        uint8_t tree_count_written = 0;
        out.write(reinterpret_cast<const char*>(&tree_count_written), sizeof(tree_count_written));

        uint32_t total_nodes_written = 0;
        for (uint16_t i = 0; i < config.num_trees; ++i) {
            if (root[i].nodes.empty()) continue;
            uint32_t node_count = static_cast<uint32_t>(root[i].nodes.size());

            // per-tree header: index + node_count
            uint8_t tree_index = static_cast<uint8_t>(i);
            out.write(reinterpret_cast<const char*>(&tree_index), sizeof(tree_index));
            out.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));

            for (const auto& node : root[i].nodes) {
                uint64_t thresholdSlot = node.getThresholdSlot();
                uint64_t featureId = node.getFeatureID();
                uint64_t labelId = node.getLabel();
                uint64_t leftIndex = node.getLeftChildIndex();

                if (thresholdSlot > thresholdMask64 || featureId > featureMask64 || labelId > labelMask64 || leftIndex > childMask64) {
                    if (!silent) {
                        std::cout << "❌ Node encoding overflow at tree " << static_cast<int>(i)
                                  << ", feature=" << (uint32_t)featureId
                                  << ", label=" << (uint32_t)labelId
                                  << ", leftIndex=" << (uint32_t)leftIndex << "\n";
                    }
                    out.close();
                    std::remove(unified_path.c_str());
                    return;
                }

                uint64_t packed64 = 0ull;
                packed64 |= static_cast<uint64_t>(node.getIsLeaf() ? 1ull : 0ull);
                packed64 |= (thresholdSlot & thresholdMask64) << threshold_start64;
                packed64 |= (featureId & featureMask64) << feature_start64;
                packed64 |= (labelId & labelMask64) << label_start64;
                packed64 |= (leftIndex & childMask64) << child_start64;

                out.write(reinterpret_cast<const char*>(&packed64), bytes_per_node64);
            }

            ++tree_count_written;
            total_nodes_written += node_count;
        }

        out.seekp(static_cast<std::streamoff>(sizeof(FOREST_MAGIC) + sizeof(bits_per_node64)), std::ios::beg);
        out.write(reinterpret_cast<const char*>(&tree_count_written), sizeof(tree_count_written));
        out.close();
    }

    // Convert and save forest in MCU-friendly 32-bit packed format
    bool convertForestToMCU(const std::string& folder_path = result_folder) {
        std::cout << "🔄 Converting model to MCU unified compact format (FRC3)...\n";

        auto bits_required = [](uint32_t value) -> uint8_t {
            uint8_t bits = 0;
            do {
                ++bits;
                value >>= 1;
            } while (value != 0 && bits < 32);
            return static_cast<uint8_t>(bits == 0 ? 1 : bits);
        };

        // MCU unified forest format expected by Rf_tree_container::loadForestUnified():
        // magic(u32)="FRC3" + version(u8)=3 + treeCount(u8) + tBits,fBits,lBits,cBits (u8 each)
        // then per tree:
        // treeIndex(u8) + rootLeaf(u8) + rootIndex(u32)
        // branchCount(u32) + internalCount(u32) + mixedCount(u32) + leafCount(u32)
        // inBits(u8)+mxBits(u8)+lfBits(u8)
        // kindBytes(u32) + kindBytes raw (bit-packed)
        // internal nodes (little-endian, inBytes each)
        // mixed nodes (little-endian, mxBytes each)
        // leaf labels (little-endian, lfBytes each)

        auto write_u8 = [](std::ofstream& out, uint8_t v) {
            out.write(reinterpret_cast<const char*>(&v), 1);
        };
        auto write_u32_le = [](std::ofstream& out, uint32_t v) {
            uint8_t b[4] = {
                static_cast<uint8_t>(v & 0xFFu),
                static_cast<uint8_t>((v >> 8) & 0xFFu),
                static_cast<uint8_t>((v >> 16) & 0xFFu),
                static_cast<uint8_t>((v >> 24) & 0xFFu),
            };
            out.write(reinterpret_cast<const char*>(b), 4);
        };
        auto write_le = [](std::ofstream& out, uint64_t v, uint8_t bytes) {
            for (uint8_t b = 0; b < bytes; ++b) {
                uint8_t byte = static_cast<uint8_t>((v >> (8u * b)) & 0xFFu);
                out.write(reinterpret_cast<const char*>(&byte), 1);
            }
        };

        struct NodeResourceBits {
            uint8_t threshold_bits = 1;
            uint8_t feature_bits = 1;
            uint8_t label_bits = 1;
            uint8_t child_bits = 1;

            uint8_t bits_per_internal_node() const {
                return static_cast<uint8_t>(1 + threshold_bits + feature_bits + child_bits);
            }
            uint8_t bits_per_mixed_node() const {
                return static_cast<uint8_t>(1 + threshold_bits + feature_bits + child_bits + child_bits);
            }
            uint8_t bits_per_leaf_node() const {
                return label_bits;
            }
        } res;

        struct CompactTree {
            uint8_t tree_index = 0;
            bool root_is_leaf = false;
            uint32_t root_index = 0;
            vector<uint8_t> branch_kind;      // 0=internal, 1=mixed (branch-index space)
            vector<uint64_t> internal_nodes;  // packed_data
            vector<uint64_t> mixed_nodes;     // packed_data
            vector<uint16_t> leaf_nodes;      // label ids
            uint32_t max_child_index = 0;
            uint32_t max_feature_id = 0;
            uint32_t max_label_id = 0;
            uint32_t max_threshold_slot = 0;
        };

        auto pack_internal = [&](bool children_are_leaf, uint16_t threshold_slot, uint16_t feature_id, uint32_t left_child) -> uint64_t {
            uint64_t packed = 0;
            packed |= static_cast<uint64_t>(children_are_leaf ? 1u : 0u);
            const uint8_t tStart = 1;
            const uint8_t fStart = static_cast<uint8_t>(tStart + res.threshold_bits);
            const uint8_t cStart = static_cast<uint8_t>(fStart + res.feature_bits);

            const uint64_t tMask = (res.threshold_bits >= 64) ? ~0ull : ((1ull << res.threshold_bits) - 1ull);
            const uint64_t fMask = (res.feature_bits >= 64) ? ~0ull : ((1ull << res.feature_bits) - 1ull);
            const uint64_t cMask = (res.child_bits >= 64) ? ~0ull : ((1ull << res.child_bits) - 1ull);

            packed |= (static_cast<uint64_t>(threshold_slot) & tMask) << tStart;
            packed |= (static_cast<uint64_t>(feature_id) & fMask) << fStart;
            packed |= (static_cast<uint64_t>(left_child) & cMask) << cStart;
            return packed;
        };

        auto pack_mixed = [&](bool left_is_leaf, uint16_t threshold_slot, uint16_t feature_id, uint32_t left_child, uint32_t right_child) -> uint64_t {
            uint64_t packed = 0;
            packed |= static_cast<uint64_t>(left_is_leaf ? 1u : 0u);
            const uint8_t tStart = 1;
            const uint8_t fStart = static_cast<uint8_t>(tStart + res.threshold_bits);
            const uint8_t lStart = static_cast<uint8_t>(fStart + res.feature_bits);
            const uint8_t rStart = static_cast<uint8_t>(lStart + res.child_bits);

            const uint64_t tMask = (res.threshold_bits >= 64) ? ~0ull : ((1ull << res.threshold_bits) - 1ull);
            const uint64_t fMask = (res.feature_bits >= 64) ? ~0ull : ((1ull << res.feature_bits) - 1ull);
            const uint64_t cMask = (res.child_bits >= 64) ? ~0ull : ((1ull << res.child_bits) - 1ull);

            packed |= (static_cast<uint64_t>(threshold_slot) & tMask) << tStart;
            packed |= (static_cast<uint64_t>(feature_id) & fMask) << fStart;
            packed |= (static_cast<uint64_t>(left_child) & cMask) << lStart;
            packed |= (static_cast<uint64_t>(right_child) & cMask) << rStart;
            return packed;
        };

        auto convert_tree_to_compact = [&](uint16_t tree_idx) -> CompactTree {
            CompactTree ct;
            ct.tree_index = static_cast<uint8_t>(tree_idx);

            const auto& tree = root[tree_idx];
            const uint32_t nodeCount = static_cast<uint32_t>(tree.nodes.size());
            if (nodeCount == 0) {
                return ct;
            }

            vector<uint32_t> old_to_leaf(nodeCount, 0xFFFFFFFFu);
            vector<uint32_t> old_to_branch(nodeCount, 0xFFFFFFFFu);

            for (uint32_t i = 0; i < nodeCount; ++i) {
                const auto& n = tree.nodes[i];
                ct.max_feature_id = std::max<uint32_t>(ct.max_feature_id, n.getFeatureID());
                ct.max_label_id = std::max<uint32_t>(ct.max_label_id, n.getLabel());
                ct.max_threshold_slot = std::max<uint32_t>(ct.max_threshold_slot, n.getThresholdSlot());

                if (n.getIsLeaf()) {
                    old_to_leaf[i] = static_cast<uint32_t>(ct.leaf_nodes.size());
                    ct.leaf_nodes.push_back(static_cast<uint16_t>(n.getLabel()));
                } else {
                    old_to_branch[i] = static_cast<uint32_t>(ct.branch_kind.size());
                    // Reserve branch_kind size later; we use branch_kind size as branchCounter during scan
                    ct.branch_kind.push_back(0);
                }
            }

            // branch_kind currently sized=branchCount but values are placeholders.
            // We'll rebuild it in correct old-order below.
            const uint32_t branchCount = static_cast<uint32_t>(ct.branch_kind.size());
            ct.branch_kind.clear();
            ct.branch_kind.reserve(branchCount);

            // Root mapping
            const auto& rootNode = tree.nodes[0];
            ct.root_is_leaf = rootNode.getIsLeaf();
            ct.root_index = ct.root_is_leaf ? old_to_leaf[0] : old_to_branch[0];

            // Build compact branch nodes in old-order filtering.
            for (uint32_t i = 0; i < nodeCount; ++i) {
                const auto& n = tree.nodes[i];
                if (n.getIsLeaf()) {
                    continue;
                }

                const uint32_t left_old = n.getLeftChildIndex();
                const uint32_t right_old = left_old + 1u;
                if (left_old >= nodeCount || right_old >= nodeCount) {
                    // Invalid tree layout
                    ct.branch_kind.clear();
                    ct.internal_nodes.clear();
                    ct.mixed_nodes.clear();
                    ct.leaf_nodes.clear();
                    return ct;
                }

                const bool left_leaf = tree.nodes[left_old].getIsLeaf();
                const bool right_leaf = tree.nodes[right_old].getIsLeaf();

                const uint16_t feature_id = static_cast<uint16_t>(n.getFeatureID());
                const uint16_t threshold_slot = static_cast<uint16_t>(n.getThresholdSlot());

                if (left_leaf == right_leaf) {
                    const bool children_are_leaf = left_leaf;
                    const uint32_t left_new = children_are_leaf ? old_to_leaf[left_old] : old_to_branch[left_old];
                    ct.internal_nodes.push_back(pack_internal(children_are_leaf, threshold_slot, feature_id, left_new));
                    ct.branch_kind.push_back(0);
                    ct.max_child_index = std::max<uint32_t>(ct.max_child_index, left_new + 1u); // right = left + 1
                } else {
                    const bool left_is_leaf = left_leaf;
                    const uint32_t left_new = left_is_leaf ? old_to_leaf[left_old] : old_to_branch[left_old];
                    const uint32_t right_new = right_leaf ? old_to_leaf[right_old] : old_to_branch[right_old];
                    ct.mixed_nodes.push_back(pack_mixed(left_is_leaf, threshold_slot, feature_id, left_new, right_new));
                    ct.branch_kind.push_back(1);
                    ct.max_child_index = std::max<uint32_t>(ct.max_child_index, std::max(left_new, right_new));
                }
            }

            // Update max_child_index for leaf root case
            if (ct.root_is_leaf) {
                ct.max_child_index = std::max<uint32_t>(ct.max_child_index, ct.root_index);
            } else {
                ct.max_child_index = std::max<uint32_t>(ct.max_child_index, ct.root_index);
            }

            return ct;
        };

        if (config.num_trees == 0) {
            std::cout << "❌ num_trees is 0; nothing to export\n";
            return false;
        }

        if (config.num_labels > 255) {
            std::cout << "❌ MCU rf_label_type is uint8_t; num_labels=" << config.num_labels << " exceeds 255\n";
            return false;
        }

        // First pass: collect global maxima for bit sizing.
        uint32_t maxFeatureId = 0;
        uint32_t maxLabelId = 0;
        uint32_t maxThresholdSlot = 0;
        uint32_t maxNodesInAnyTree = 0;
        bool missingTree = false;

        for (uint16_t i = 0; i < config.num_trees; ++i) {
            if (i >= root.size() || root[i].nodes.empty()) {
                missingTree = true;
                break;
            }
            maxNodesInAnyTree = std::max<uint32_t>(maxNodesInAnyTree, static_cast<uint32_t>(root[i].nodes.size()));
            for (const auto& n : root[i].nodes) {
                maxFeatureId = std::max(maxFeatureId, n.getFeatureID());
                maxLabelId = std::max(maxLabelId, n.getLabel());
                maxThresholdSlot = std::max(maxThresholdSlot, (uint32_t)n.getThresholdSlot());
            }
        }

        if (missingTree) {
            std::cout << "❌ One or more trees are empty/invalid; aborting MCU export\n";
            return false;
        }

        if (config.num_features > 0) {
            uint32_t cfgMaxFeature = static_cast<uint32_t>(config.num_features - 1);
            maxFeatureId = std::max(maxFeatureId, cfgMaxFeature);
        }
        if (config.num_labels > 0) {
            uint32_t cfgMaxLabel = static_cast<uint32_t>(config.num_labels - 1);
            maxLabelId = std::max(maxLabelId, cfgMaxLabel);
        }

        // Compute resource bit-widths (must match MCU node_resource expectations)
        res.threshold_bits = QuantizationHelper::sanitizeBits(static_cast<int>(config.quantization_coefficient));
        res.feature_bits = std::min<uint8_t>(10, std::max<uint8_t>(1, bits_required(maxFeatureId)));
        res.label_bits = std::min<uint8_t>(8, std::max<uint8_t>(1, bits_required(maxLabelId)));
        res.child_bits = std::max<uint8_t>(1, bits_required(maxNodesInAnyTree));

        // Second pass: convert trees using the calculated bit widths.
        vector<CompactTree> compact;
        compact.reserve(config.num_trees);

        for (uint16_t i = 0; i < config.num_trees; ++i) {
            CompactTree ct = convert_tree_to_compact(i);
            if (ct.leaf_nodes.empty() && ct.branch_kind.empty() && !ct.root_is_leaf) {
                std::cout << "❌ Tree " << i << " conversion failed\n";
                return false;
            }
            compact.push_back(std::move(ct));
        }

        // Enforce that the compact node payloads fit within 32 bits on ESP32 (size_t is 32-bit there).
        const uint8_t inBits = res.bits_per_internal_node();
        const uint8_t mxBits = res.bits_per_mixed_node();
        const uint8_t lfBits = res.bits_per_leaf_node();
        if (inBits > 32 || mxBits > 32 || lfBits > 8) {
            std::cout << "❌ Node layout exceeds MCU limits (internalBits=" << (int)inBits
                      << ", mixedBits=" << (int)mxBits
                      << ", leafBits=" << (int)lfBits << ").\n";
            std::cout << "   Try reducing num_features/num_nodes or quantization_coefficient.\n";
            return false;
        }

        // Update config layout bits for MCU init.
        config.threshold_bits = res.threshold_bits;
        config.feature_bits = res.feature_bits;
        config.label_bits = res.label_bits;
        config.child_bits = res.child_bits;

        const uint8_t inBytes = static_cast<uint8_t>((inBits + 7) / 8);
        const uint8_t mxBytes = static_cast<uint8_t>((mxBits + 7) / 8);
        const uint8_t lfBytes = static_cast<uint8_t>((lfBits + 7) / 8);

        // Create output directory if needed
        #ifdef _WIN32
            _mkdir(folder_path.c_str());
        #else
            mkdir(folder_path.c_str(), 0755);
        #endif

        std::string unified_path = folder_path + "/" + model_name + "_forest.bin";
        std::ofstream out(unified_path, std::ios::binary);
        if (!out.is_open()) {
            std::cout << "❌ Failed to create unified forest file: " << unified_path << "\n";
            return false;
        }

        // Header
        const uint32_t FOREST_MAGIC = 0x33435246; // "FRC3"
        write_u32_le(out, FOREST_MAGIC);
        write_u8(out, 3); // version
        write_u8(out, static_cast<uint8_t>(config.num_trees));
        write_u8(out, res.threshold_bits);
        write_u8(out, res.feature_bits);
        write_u8(out, res.label_bits);
        write_u8(out, res.child_bits);

        uint32_t total_branch = 0;
        uint32_t total_internal = 0;
        uint32_t total_mixed = 0;
        uint32_t total_leaf = 0;
        uint32_t total_kind_bytes = 0;

        for (const auto& ct : compact) {
            const uint32_t branchCount = static_cast<uint32_t>(ct.branch_kind.size());
            const uint32_t internalCount = static_cast<uint32_t>(ct.internal_nodes.size());
            const uint32_t mixedCount = static_cast<uint32_t>(ct.mixed_nodes.size());
            const uint32_t leafCount = static_cast<uint32_t>(ct.leaf_nodes.size());
            const uint32_t kindBytes = (branchCount + 7u) / 8u;

            write_u8(out, ct.tree_index);
            write_u8(out, ct.root_is_leaf ? 1u : 0u);
            write_u32_le(out, ct.root_index);

            write_u32_le(out, branchCount);
            write_u32_le(out, internalCount);
            write_u32_le(out, mixedCount);
            write_u32_le(out, leafCount);

            write_u8(out, inBits);
            write_u8(out, mxBits);
            write_u8(out, lfBits);

            write_u32_le(out, kindBytes);
            for (uint32_t byteIndex = 0; byteIndex < kindBytes; ++byteIndex) {
                uint8_t packed = 0;
                const uint32_t base = byteIndex * 8u;
                for (uint8_t bit = 0; bit < 8; ++bit) {
                    const uint32_t idx = base + static_cast<uint32_t>(bit);
                    if (idx < branchCount) {
                        packed |= static_cast<uint8_t>((ct.branch_kind[idx] & 1u) << bit);
                    }
                }
                write_u8(out, packed);
            }

            for (uint64_t raw : ct.internal_nodes) {
                write_le(out, raw, inBytes);
            }
            for (uint64_t raw : ct.mixed_nodes) {
                write_le(out, raw, mxBytes);
            }
            for (uint16_t lbl : ct.leaf_nodes) {
                write_le(out, static_cast<uint64_t>(lbl), lfBytes);
            }

            total_branch += branchCount;
            total_internal += internalCount;
            total_mixed += mixedCount;
            total_leaf += leafCount;
            total_kind_bytes += kindBytes;
        }

        out.close();

        std::ifstream in_size(unified_path, std::ifstream::ate | std::ifstream::binary);
        if (in_size.is_open()) {
            std::streamsize size = in_size.tellg();
            in_size.close();
            std::cout << "✅ Saved MCU unified forest (FRC3): " << unified_path << "\n";
            std::cout << "   - Trees: " << config.num_trees
                      << " | Branch: " << total_branch
                      << " | Internal: " << total_internal
                      << " | Mixed: " << total_mixed
                      << " | Leaf: " << total_leaf << "\n";
            std::cout << "   - File size: " << size << " bytes\n";
        }

        // Rough RAM estimate (payload bytes + per-tree overhead like MCU side)
        size_t payload_bytes = 0;
        payload_bytes += total_kind_bytes;
        payload_bytes += static_cast<size_t>(total_internal) * inBytes;
        payload_bytes += static_cast<size_t>(total_mixed) * mxBytes;
        payload_bytes += static_cast<size_t>(total_leaf) * lfBytes;
        size_t model_ram_size = payload_bytes;
        model_ram_size += 25 * config.num_trees;
        model_ram_size += 120;
        config.RAM_usage = model_ram_size;
        std::cout << "   - Node layout bits: t=" << (int)res.threshold_bits
                  << " f=" << (int)res.feature_bits
                  << " l=" << (int)res.label_bits
                  << " c=" << (int)res.child_bits << "\n";
        std::cout << "   - Compact bits: internal=" << (int)inBits << " mixed=" << (int)mxBits
                  << " leaf=" << (int)lfBits << "\n";
        std::cout << "   - Estimated RAM: ~" << model_ram_size << " bytes\n";

        saveConfig();
        return true;
    }
    
    // combined prediction metrics function
    b_vector<b_vector<pair<uint16_t, float>>> predict(Rf_data& data) {
        // Counters for each label
        unordered_map_s<uint16_t, uint32_t> tp, fp, fn, totalPred, correctPred;
        
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
        auto avg_metric = [](const b_vector<pair<uint16_t, float>, 16>& vec) -> float {
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
};

RandomForest::ConsensusResult RandomForest::computeConsensus(const Rf_sample& sample, const vector<uint16_t>* tree_indices) {
    ConsensusResult result;
    if (root.empty()) {
        return result;
    }

    vector<uint16_t> vote_counts(config.num_labels, 0);

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

vector<RandomForest::EvaluationSample> RandomForest::collectOOBSamples(uint16_t min_votes_required, vector<uint16_t>* vote_histogram) {
    vector<EvaluationSample> samples;
    const uint32_t total_samples = static_cast<uint32_t>(train_data.allSamples.size());
    samples.reserve(total_samples);

    if (vote_histogram) {
        vote_histogram->assign(21, 0);
    }

    if (total_samples == 0 || config.num_trees == 0 || dataList.empty()) {
        return samples;
    }

    // Build a per-sample list of trees that contain the sample to avoid O(N*T*bag_size) scans.
    vector<vector<uint16_t>> sample_in_trees(total_samples);
    const uint16_t sentinel = std::numeric_limits<uint16_t>::max();
    vector<uint16_t> last_inserted(total_samples, sentinel);

    const uint16_t trees_to_index = static_cast<uint16_t>(std::min<size_t>(config.num_trees, dataList.size()));
    for (uint16_t tree_idx = 0; tree_idx < trees_to_index; ++tree_idx) {
        const auto& tree_dataset = dataList[tree_idx];
        for (auto sid : tree_dataset) {
            uint32_t sample_id = static_cast<uint32_t>(sid);
            if (sample_id >= total_samples) {
                continue;
            }
            if (last_inserted[sample_id] != tree_idx) {
                last_inserted[sample_id] = tree_idx;
                sample_in_trees[sample_id].push_back(tree_idx);
            }
        }
    }

    vector<uint16_t> active_trees;
    active_trees.reserve(config.num_trees);

    uint32_t sample_id = 0;
    for (const auto& sample : train_data.allSamples) {
        active_trees.clear();

        const auto& included = sample_in_trees[sample_id];
        size_t include_pos = 0;

        for (uint16_t tree_idx = 0; tree_idx < config.num_trees; ++tree_idx) {
            if (include_pos < included.size() && included[include_pos] == tree_idx) {
                ++include_pos;
            } else {
                active_trees.push_back(tree_idx);
            }
        }

        if (vote_histogram) {
            uint16_t bucket = static_cast<uint16_t>(std::min<size_t>(20, active_trees.size()));
            (*vote_histogram)[bucket]++;
        }

        if (active_trees.empty() || active_trees.size() < min_votes_required) {
            ++sample_id;
            continue;
        }

        ConsensusResult consensus = computeConsensus(sample, &active_trees);
        if (consensus.total_votes == 0) {
            ++sample_id;
            continue;
        }

        EvaluationSample eval{};
        eval.actual_label = sample.label;
        eval.predicted_label = consensus.predicted_label;
        eval.votes = consensus.votes;
        eval.total_votes = consensus.total_votes;
        eval.consensus = consensus.consensus;
        samples.push_back(eval);

        ++sample_id;
    }

    return samples;
}

vector<RandomForest::EvaluationSample> RandomForest::collectValidationSamples(const Rf_data& dataset) {
    vector<EvaluationSample> samples;
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

vector<RandomForest::EvaluationSample> RandomForest::collectCrossValidationSamples(float& max_nodes_out) {
    vector<EvaluationSample> aggregated;
    aggregated.reserve(train_data.allSamples.size());
    max_nodes_out = 0.0f;

    uint16_t k_folds = config.k_folds;
    if (k_folds < 2) {
        k_folds = 4;
    }

    vector<uint32_t> allTrainIndices;
    allTrainIndices.reserve(train_data.allSamples.size());
    for (uint32_t i = 0; i < train_data.allSamples.size(); ++i) {
        allTrainIndices.push_back(i);
    }

    for (uint32_t i = allTrainIndices.size(); i > 1; --i) {
        uint32_t j = static_cast<uint32_t>(rng.bounded(i));
        std::swap(allTrainIndices[i - 1], allTrainIndices[j]);
    }

    uint32_t fold_size = (k_folds > 0) ? static_cast<uint32_t>(allTrainIndices.size() / k_folds) : 0;
    if (fold_size == 0) {
        fold_size = allTrainIndices.size();
    }

    auto original_dataList = dataList;
    uint16_t valid_folds = 0;

    for (uint16_t fold = 0; fold < k_folds; ++fold) {
        vector<uint32_t> cv_train_indices;
        vector<uint32_t> cv_test_indices;

        uint32_t test_start = static_cast<uint32_t>(fold * fold_size);
        uint32_t test_end = (fold == k_folds - 1) ? static_cast<uint32_t>(allTrainIndices.size())
                                                 : static_cast<uint32_t>((fold + 1) * fold_size);

        for (uint32_t i = 0; i < allTrainIndices.size(); ++i) {
            uint32_t sampleIndex = allTrainIndices[i];
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

        uint32_t cv_train_size = cv_train_indices.size();
        uint32_t bootstrap_sample_size = cv_train_size;
        if (config.use_bootstrap) {
            float desired = cv_train_size * config.boostrap_ratio;
            bootstrap_sample_size = static_cast<uint32_t>(std::max<float>(1.0f, std::round(desired)));
        }

        for (uint16_t tree_idx = 0; tree_idx < config.num_trees; ++tree_idx) {
            TreeSampleIDs cv_tree_dataset;
            if (!train_data.allSamples.empty()) {
                uint32_t max_id = static_cast<uint32_t>(train_data.allSamples.size() - 1);
                cv_tree_dataset.set_ID_range(0, max_id);
            }

            auto tree_rng = rng.deriveRNG(fold * 1000 + tree_idx);

            if (config.use_bootstrap) {
                for (uint32_t j = 0; j < bootstrap_sample_size; ++j) {
                    if (cv_train_size == 0) {
                        break;
                    }
                    uint32_t idx_in_cv_train = static_cast<uint32_t>(tree_rng.bounded(cv_train_size));
                    cv_tree_dataset.push_back(cv_train_indices[idx_in_cv_train]);
                }
            } else {
                vector<uint32_t> indices_copy;
                indices_copy.reserve(cv_train_indices.size());
                for (uint32_t idx_value : cv_train_indices) {
                    indices_copy.push_back(idx_value);
                }
                for (uint32_t t = 0; t < bootstrap_sample_size; ++t) {
                    uint32_t remaining = cv_train_size - t;
                    if (remaining == 0) {
                        break;
                    }
                    uint32_t j = static_cast<uint32_t>(t + tree_rng.bounded(remaining));
                    std::swap(indices_copy[t], indices_copy[j]);
                    cv_tree_dataset.push_back(indices_copy[t]);
                }
            }

            dataList.push_back(cv_tree_dataset);
        }

        MakeForest();

        uint32_t fold_max_nodes = 0;
        for (uint16_t i = 0; i < config.num_trees; ++i) {
            uint32_t tree_nodes = root[i].countNodes();
            if (tree_nodes > fold_max_nodes) {
                fold_max_nodes = tree_nodes;
            }
        }
        if (fold_max_nodes > max_nodes_out) {
            max_nodes_out = static_cast<float>(fold_max_nodes);
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

    dataList = original_dataList;
    return aggregated;
}

RandomForest::MetricsSummary RandomForest::computeMetricsForThreshold(const vector<EvaluationSample>& samples, float threshold) {
    MetricsSummary metrics;
    if (samples.empty()) {
        return metrics;
    }

    vector<uint32_t> tp(config.num_labels, 0);
    vector<uint32_t> fp(config.num_labels, 0);
    vector<uint32_t> fn(config.num_labels, 0);

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

RandomForest::ThresholdSearchResult RandomForest::findBestThreshold(const vector<EvaluationSample>& samples, Rf_metric_scores flags) {
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

void post_process_model(RandomForest& forest) {
    std::cout << "\n📊 Post-processing model...\n";
    
    // // Calculate node layout and save forest
    // std::cout << "💾 Saving forest to binary format...\n";
    // forest.saveForest(result_folder);
    
    // Print forest statistics
    forest.printForestStatistics();
    
    std::cout << "\n🧪 Evaluating model on test set...\n";
    auto result = forest.predict(forest.test_data);

    // Calculate Precision
    std::cout << "Precision in test set:\n";
    std::cout << std::fixed << std::setprecision(3);
    b_vector<pair<uint16_t, float>, 16> precision = result[0];
    for (const auto& p : precision) {
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
    b_vector<pair<uint16_t, float>, 16> recall = result[1];
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
    b_vector<pair<uint16_t, float>, 16> f1_scores = result[2];
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
    b_vector<pair<uint16_t, float>, 16> accuracies = result[3];
    for (const auto& acc : accuracies) {
        std::cout << "Label: " << (int)acc.first << " - " << acc.second << "\n";
    }
    float avgAccuracy = 0.0f;
    for (const auto& acc : accuracies) {
        avgAccuracy += acc.second;
    }
    avgAccuracy /= accuracies.size();
    std::cout << "Avg: " << avgAccuracy << "\n";

    // Calculate result score based on metric flags
    float result_score = forest.predict(forest.test_data, static_cast<Rf_metric_scores>(forest.config.metric_score));
    forest.config.result_score = result_score;
    std::cout << "\n✅ Result score: " << result_score << "\n";
    forest.convertForestToMCU(result_folder);
}

int main(int argc, char** argv) {
    // Parse command line arguments
    bool enable_training = true;
    int num_threads = -1; // -1 means use all available threads
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-skip_training" || arg == "--skip_training") {
            enable_training = false;
        } else if (arg == "-threads" || arg == "--threads") {
            if (i + 1 < argc) {
                try {
                    num_threads = std::stoi(argv[++i]);
                    if (num_threads < 1) {
                        std::cerr << "Error: Number of threads must be at least 1\n";
                        return 1;
                    }
                } catch (...) {
                    std::cerr << "Error: Invalid thread count\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: -threads requires a value\n";
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -skip_training, --skip_training    Skip training mode (build with default parameters)\n";
            std::cout << "  -threads N, --threads N            Set number of threads for parallel tree building (default: all available)\n";
            std::cout << "  -h, --help                         Show this help message\n";
            return 0;
        }
    }
    
    // Set OpenMP thread count if specified
    #ifdef _OPENMP
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
    }
    #endif
    
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Random Forest PC Training v" << VERSION << "\n";
    
    if (enable_training) {
        std::cout << "🔧 Mode: Training with grid search\n";
    } else {
        std::cout << "🔧 Mode: Build model only (skip training)\n";
    }
    
    RandomForest forest;
    
    // Build forest with initial/configured parameters
    forest.build_model();
    
    // Optionally perform training (grid search)
    if (enable_training) {
        forest.training();
    } else {
        std::cout << "\n⏭️  Skipping training (grid search).\n";
    }
    
    // Post-process: evaluate, save, and display results
    post_process_model(forest);
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "⏱️  Total time: " << elapsed.count() << " seconds\n";
    
    return 0;
}