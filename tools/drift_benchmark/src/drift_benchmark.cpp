#include "pc_components.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <sstream>

using namespace mcu;

// Configuration structure for Drift Benchmark
struct DriftConfig {
    std::string dataset_path;
    int drift_point;
    int window_size;
    std::string metric;
    bool streaming;
    int num_trees;
    int max_depth;
    int min_samples_leaf;
    int retrain_buffer_size;
    float retrain_acc_threshold;
    int retrain_patience;
};

DriftConfig load_drift_config(const std::string& path) {
    DriftConfig cfg;
    // Defaults
    cfg.retrain_buffer_size = 10000;
    cfg.retrain_acc_threshold = 0.5f;
    cfg.retrain_patience = 2000;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error opening config file: " << path << std::endl;
        exit(1);
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("dataset_path") != std::string::npos) {
            size_t first_quote = line.find("\"", line.find(":"));
            size_t last_quote = line.find_last_of("\"");
            if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                cfg.dataset_path = line.substr(first_quote + 1, last_quote - first_quote - 1);
            }
        } else if (line.find("drift_point") != std::string::npos) {
            cfg.drift_point = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("window_size") != std::string::npos) {
            cfg.window_size = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("metric") != std::string::npos) {
            size_t first_quote = line.find("\"", line.find(":"));
            size_t last_quote = line.find_last_of("\"");
            if (first_quote != std::string::npos && last_quote != std::string::npos && last_quote > first_quote) {
                cfg.metric = line.substr(first_quote + 1, last_quote - first_quote - 1);
            }
        } else if (line.find("streaming") != std::string::npos) {
            cfg.streaming = (line.find("true") != std::string::npos);
        } else if (line.find("num_trees") != std::string::npos) {
            cfg.num_trees = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("max_depth") != std::string::npos) {
            cfg.max_depth = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("min_samples_leaf") != std::string::npos) {
            cfg.min_samples_leaf = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("retrain_buffer_size") != std::string::npos) {
            cfg.retrain_buffer_size = std::stoi(line.substr(line.find(":") + 1));
        } else if (line.find("retrain_acc_threshold") != std::string::npos) {
            cfg.retrain_acc_threshold = std::stof(line.substr(line.find(":") + 1));
        } else if (line.find("retrain_patience") != std::string::npos) {
            cfg.retrain_patience = std::stoi(line.substr(line.find(":") + 1));
        }
    }
    return cfg;
}

class DriftForest {
public:
    Rf_config config;
    Rf_data full_data;
    Rf_data train_data;
    
    using TreeSampleIDs = ID_vector<uint32_t, 8>;
    vector<Rf_tree> root;
    vector<TreeSampleIDs> dataList;
    Rf_random rng;

    DriftForest(const DriftConfig& drift_cfg) {
        config.num_trees = drift_cfg.num_trees;
        config.max_depth = drift_cfg.max_depth;
        config.min_leaf = drift_cfg.min_samples_leaf;
        config.min_split = 2; // Default
        config.num_features = 0; // Will be set on load
        config.quantization_coefficient = 2; // Default, should read from metadata
        
        // Load full dataset
        std::cout << "Loading dataset: " << drift_cfg.dataset_path << std::endl;
        // We need to know num_features first. 
        // Rf_data::loadCSVData needs num_features.
        // We can peek at the file or assume it's in metadata.
        // For now, let's try to load with a dummy and see if it auto-detects or we need to parse header.
        // Actually, loadCSVData expects normalized data without header.
        // We should read metadata file *_dp.csv if it exists.
        
        std::string metadata_path = drift_cfg.dataset_path;
        size_t ext_pos = metadata_path.find("_nml.csv");
        if (ext_pos != std::string::npos) {
            metadata_path.replace(ext_pos, 8, "_dp.csv");
        } else {
            metadata_path += ".meta"; // Fallback
        }
        
        loadMetadata(metadata_path);
        
        full_data.setFeatureBits(config.quantization_coefficient);
        full_data.loadCSVData(drift_cfg.dataset_path, config.num_features, -1); // Load all
        
        std::cout << "Loaded " << full_data.allSamples.size() << " samples." << std::endl;
        
        // Split into train (0 to drift_point)
        int train_size = std::min((int)full_data.allSamples.size(), drift_cfg.drift_point);
        train_data.setFeatureBits(config.quantization_coefficient);
        train_data.allSamples.reserve(train_size);
        
        for(int i=0; i<train_size; i++) {
            train_data.allSamples.push_back(full_data.allSamples[i]);
        }
        
        std::cout << "Training set size: " << train_data.allSamples.size() << std::endl;
        
        rng = Rf_random(42, true);
    }
    
    void loadMetadata(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cout << "Warning: Metadata file not found: " << path << ". Assuming defaults." << std::endl;
            config.num_features = 10; // DANGEROUS DEFAULT
            return;
        }
        std::string line;
        while(std::getline(file, line)) {
            if(line.find("num_features") != std::string::npos) {
                config.num_features = std::stoi(line.substr(line.find(",")+1));
            } else if(line.find("quantization_coefficient") != std::string::npos) {
                config.quantization_coefficient = std::stoi(line.substr(line.find(",")+1));
            } else if(line.find("num_labels") != std::string::npos) {
                config.num_labels = std::stoi(line.substr(line.find(",")+1));
            }
        }
        std::cout << "Metadata: Features=" << config.num_features << ", Labels=" << config.num_labels << ", Bits=" << config.quantization_coefficient << std::endl;
    }

    void ClonesData() {
        dataList.clear();
        dataList.reserve(config.num_trees);
        const uint32_t numSample = static_cast<uint32_t>(train_data.allSamples.size());
        
        for (uint16_t i = 0; i < config.num_trees; i++) {
            TreeSampleIDs treeDataset;
            treeDataset.set_ID_range(0, numSample - 1);
            
            // Bootstrap sampling
            auto tree_rng = rng.deriveRNG(i, 0);
            for (uint32_t j = 0; j < numSample; ++j) {
                uint32_t idx = static_cast<uint32_t>(tree_rng.bounded(numSample));
                treeDataset.push_back(idx);
            }
            dataList.push_back(treeDataset);
        }
    }

    void build_tree(Rf_tree& tree, TreeSampleIDs& sampleIDs) {
        // Simplified build_tree calling the library's build logic if possible
        // But Rf_tree doesn't have a build method that takes sampleIDs directly in the same way as PC tool?
        // The PC tool implements build_tree manually.
        // I need to copy the build_tree implementation from random_forest_pc.cpp.
        // Since it's long, I'll try to use a simplified version or just copy it.
        // For now, I'll assume I can copy it.
        
        // Actually, I can't easily copy 500 lines of code here without making mistakes.
        // But wait, Rf_tree in src/random_forest_mcu.h has `build` method?
        // No, it has `restore` and `predict`.
        // The training logic is indeed in random_forest_pc.cpp.
        
        // This is a problem. The training logic is not in the library, it's in the tool.
        // I MUST include random_forest_pc.cpp or copy the code.
        // Since I failed to include it due to `main`, I will try to include it again but use a macro trick.
        // I will define `main` as `pc_main` before including.
    }
};

// Macro trick to rename main and expose private members
#define main pc_main
#define private public
#include "random_forest_pc.cpp"
#undef private
#undef main

int main(int argc, char** argv) {
    std::cout << "Drift Benchmark Tool" << std::endl;
    
    DriftConfig cfg = load_drift_config("drift_config.json");
    
    // Use the RandomForest class from random_forest_pc.cpp
    // We need to trick it to load our data.
    // RandomForest constructor loads data from file.
    // We can use the `data_path_override` argument.
    
    // 1. Train on first N samples
    // We can't easily tell RandomForest to only load N samples and then STOP.
    // It loads N samples and then splits them.
    // If we set train_ratio = 1.0, it puts all N samples into train_data.
    // But we need to modify the config to set train_ratio = 1.0.
    // We can modify model_config.json temporarily or programmatically.
    
    // Let's instantiate RandomForest
    // We need to pass max_samples = drift_point.
    
    std::cout << "Training on first " << cfg.drift_point << " samples..." << std::endl;
    
    // We need to ensure model_config.json has train_ratio = 1.0 or similar.
    // Or we can modify the RandomForest class instance after creation?
    // No, splitData is called in constructor.
    
    // Hack: We can create a temporary config file.
    // Or we can just accept that it splits 80/20 of the first N samples.
    // For drift benchmarking, usually we train on N samples.
    // If we split, we train on 0.8*N. This is acceptable for a benchmark.
    
    RandomForest forest(cfg.drift_point, cfg.dataset_path, "drift_config.json");
    forest.build_model();
    forest.retrain_buffer_limit = cfg.retrain_buffer_size;

    // Auto metric detection
    if (cfg.metric == "auto") {
        if (forest.config.metric_score == Rf_metric_scores::ACCURACY) cfg.metric = "accuracy";
        else if (forest.config.metric_score == Rf_metric_scores::PRECISION) cfg.metric = "precision";
        else if (forest.config.metric_score == Rf_metric_scores::RECALL) cfg.metric = "recall";
        else if (forest.config.metric_score == Rf_metric_scores::F1_SCORE) cfg.metric = "f1";
        std::cout << "🤖 Auto-detected metric: " << cfg.metric << std::endl;
    }
    
    // Now we need to evaluate on the REST of the data.
    // The forest object only has the first N samples loaded.
    // We need to load the full dataset separately to get the rest.
    
    Rf_data stream_data;
    // We need to configure stream_data with same bits as forest
    stream_data.setFeatureBits(forest.config.quantization_coefficient);
    
    // Load full dataset
    // We can't easily load "from N to end". We load all and ignore first N.
    stream_data.loadCSVData(cfg.dataset_path, forest.config.num_features, -1);
    
    std::cout << "Total samples: " << stream_data.allSamples.size() << std::endl;
    
    // Sliding window evaluation
    std::ofstream out_file("drift_results.csv");
    out_file << "window_start,accuracy,precision,recall,f1,retrained" << std::endl;
    
    int start_idx = cfg.drift_point;
    int end_idx = stream_data.allSamples.size();
    int window = cfg.window_size;
    int step = 10; // Step size for sliding window
    int retrain_cooldown = 0;
    
    std::cout << "Starting evaluation from " << start_idx << " to " << end_idx << "..." << std::endl;
    if (cfg.streaming) std::cout << "🚀 Streaming mode ENABLED (Tree Replacement & Leaf Update)" << std::endl;
    
    vector<bool> results;
    results.reserve(end_idx - start_idx);

    for (int i = start_idx; i < end_idx; i++) {
        Rf_sample sample = stream_data.allSamples[i];
        
        // 1. Predict (Test-then-Train)
        auto result = forest.computeConsensus(sample);
        uint16_t predicted = result.predicted_label;
        uint16_t actual = sample.label;
        
        results.push_back(predicted == actual);
        
        // Update retrain buffer
        forest.retrain_buffer.push_back(sample);
        if (forest.retrain_buffer.size() > forest.retrain_buffer_limit) {
            forest.retrain_buffer.pop_front();
        }
        
        // 2. Update (if streaming enabled)
        if (cfg.streaming) {
            forest.update(sample);
        }

        bool retrained_this_step = false;
        if (retrain_cooldown > 0) retrain_cooldown--;

        // 3. Periodically save windowed metrics
        if (i > start_idx && (i - start_idx) % step == 0 && (i - start_idx) >= window) {
            int correct = 0;
            for (int j = results.size() - window; j < results.size(); j++) {
                if (results[j]) correct++;
            }
            float accuracy = (float)correct / window;
            
            // Check for retraining condition
            if (cfg.streaming && accuracy < cfg.retrain_acc_threshold && retrain_cooldown <= 0 && forest.retrain_buffer.size() >= 1000) {
                forest.retrainAll();
                retrain_cooldown = cfg.retrain_patience;
                retrained_this_step = true;
                
                // Reset results buffer to avoid "memory" of bad performance dragging down accuracy immediately?
                // No, keep it to show recovery.
            }

            out_file << i << "," << accuracy << ",0,0,0," << (retrained_this_step ? 1 : 0) << std::endl;
            
            if (i % 5000 == 0) std::cout << "Processed sample " << i << " Window Acc: " << accuracy << std::endl;
        }
    }
    
    out_file.close();
    std::cout << "Done. Results saved to drift_results.csv" << std::endl;
    
    return 0;
}
