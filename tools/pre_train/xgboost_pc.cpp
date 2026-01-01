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
#include <algorithm>
#include <limits>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "pc_components.h"  // First, for Rf_data, Rf_sample, Rf_random
#include "xg_components.h"  // Then XG components

#define VERSION "2.0.0"
#define temp_base_data "base_data_xgb.csv"

using namespace mcu;

std::string extract_model_name(const std::string& data_path) {
    size_t last_slash = data_path.find_last_of("/\\");
    std::string filename = (last_slash != std::string::npos) ? data_path.substr(last_slash + 1) : data_path;
    size_t first_underscore = filename.find("_nml");
    return (first_underscore != std::string::npos) ? filename.substr(0, first_underscore) : filename;
}

class XGBoost {
public:
    Rf_data base_data;
    Rf_data train_data;
    Rf_data test_data;
    
    std::string model_name;
    XG_config config;
    
private:
    vector<XG_tree> trees; // K * num_boost_rounds trees for multi-class
    vector<vector<float>> train_predictions; // [num_samples][num_labels]
    Rf_random rng;
    
    std::string result_config_path;
    std::string result_model_path;

    struct XG_SplitInfo {
        float gain = -1.0f;
        uint16_t featureID = 0;
        uint16_t threshold = 0;
        vector<uint32_t> left_indices;
        vector<uint32_t> right_indices;
    };

public:
    XGBoost(const std::string& config_file = xg_config_path) : config(config_file) {
        rng = Rf_random(config.random_seed, true);
        
        generateFilePaths();
        createDataBackup(config.data_path, temp_base_data);
        config.init(temp_base_data);
        
        model_name = extract_model_name(config.data_path);
        std::cout << "🚀 XGBoost Model: " << model_name << std::endl;
        
        base_data.setFeatureBits(config.quantization_coefficient);
        train_data.setFeatureBits(config.quantization_coefficient);
        test_data.setFeatureBits(config.quantization_coefficient);
        
        std::cout << "Loading dataset...\n";
        base_data.loadCSVData(temp_base_data, config.num_features);
        
        splitData(config.train_ratio);
        
        train_predictions.assign(train_data.allSamples.size(), vector<float>(config.num_labels, 0.0f));
        
        config.printSummary();
    }

    ~XGBoost() {
        std::remove(temp_base_data);
    }

    void generateFilePaths() {
        result_config_path = xg_result_folder + extract_model_name(config.data_path) + "_xgb_config.json";
        result_model_path = xg_result_folder + extract_model_name(config.data_path) + "_xgboost.bin";
    }

    void createDataBackup(const std::string& source_path, const std::string& backup_filename) {
        std::ifstream source(source_path, std::ios::binary);
        if (!source.is_open()) {
            std::cerr << "❌ Failed to open source: " << source_path << std::endl;
            return;
        }
        std::ofstream backup(backup_filename, std::ios::binary);
        backup << source.rdbuf();
    }

    void splitData(float trainRatio) {
        size_t total = base_data.allSamples.size();
        size_t trainSize = static_cast<size_t>(total * trainRatio);
        
        vector<size_t> indices(total);
        for(size_t i=0; i<total; ++i) indices[i] = i;
        
        for(size_t i=total-1; i>0; --i) {
            size_t j = rng.bounded(static_cast<uint32_t>(i+1));
            std::swap(indices[i], indices[j]);
        }
        
        for(size_t i=0; i<trainSize; ++i) {
            train_data.allSamples.push_back(base_data.allSamples[indices[i]]);
        }
        for(size_t i=trainSize; i<total; ++i) {
            test_data.allSamples.push_back(base_data.allSamples[indices[i]]);
        }
        
        std::cout << "✅ Data split: " << train_data.allSamples.size() << " train, " 
                  << test_data.allSamples.size() << " test\n";
    }

    void softmax(vector<float>& x) {
        if (x.empty()) return;
        float max_val = *std::max_element(x.begin(), x.end());
        float sum = 0;
        for (float& val : x) {
            val = std::exp(val - max_val);
            sum += val;
        }
        if (sum > 0) {
            for (float& val : x) val /= sum;
        }
    }

    void build_model() {
        std::cout << "\n🌳 Building XGBoost Model...\n";
        std::cout << "   " << config.num_boost_rounds << " boost rounds × " 
                  << config.num_labels << " classes = " 
                  << (config.num_boost_rounds * config.num_labels) << " trees\n\n";
        
        trees.clear();
        trees.reserve(config.num_boost_rounds * config.num_labels);
        train_predictions.assign(train_data.allSamples.size(), vector<float>(config.num_labels, 0.0f));
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (uint16_t round = 0; round < config.num_boost_rounds; ++round) {
            for (uint16_t k = 0; k < config.num_labels; ++k) {
                vector<float> g(train_data.allSamples.size());
                vector<float> h(train_data.allSamples.size());
                
                // Compute gradients and hessians
                for (size_t i = 0; i < train_data.allSamples.size(); ++i) {
                    vector<float> probs = train_predictions[i];
                    softmax(probs);
                    float y = (train_data.allSamples[i].label == k) ? 1.0f : 0.0f;
                    g[i] = probs[k] - y;
                    h[i] = std::max(probs[k] * (1.0f - probs[k]), 1e-6f);
                }
                
                // Build regression tree
                XG_tree tree("");
                build_regression_tree(tree, g, h);
                
                // Update predictions
                for (size_t i = 0; i < train_data.allSamples.size(); ++i) {
                    float weight = tree.predictSample(train_data.allSamples[i], config.quantization_coefficient);
                    train_predictions[i][k] += config.learning_rate * weight;
                }
                
                trees.push_back(std::move(tree));
            }
            
            // Progress bar
            float progress = static_cast<float>(round + 1) / config.num_boost_rounds;
            int bar_width = 50;
            int pos = static_cast<int>(bar_width * progress);
            std::cout << "\r[";
            for (int j = 0; j < bar_width; ++j) {
                if (j < pos) std::cout << "=";
                else if (j == pos) std::cout << ">";
                else std::cout << " ";
            }
            std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0f) << "% ";
            std::cout << "(" << (round + 1) << "/" << config.num_boost_rounds << " rounds)";
            std::cout.flush();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end_time - start_time;
        
        std::cout << "\n\n✅ Model built in " << elapsed.count() << " seconds\n";
        printModelStatistics();
    }

    void build_regression_tree(XG_tree& tree, const vector<float>& g, const vector<float>& h) {
        tree.nodes.clear();
        
        vector<uint32_t> root_indices(train_data.allSamples.size());
        for(uint32_t i=0; i<root_indices.size(); ++i) root_indices[i] = i;
        
        vector<XG_NodeToBuild> queue;
        tree.nodes.push_back(XG_node()); // Root
        queue.push_back(XG_NodeToBuild(0, root_indices, 0));
        
        size_t head = 0;
        while(head < queue.size()) {
            XG_NodeToBuild current = queue[head++];
            
            float G = 0, H = 0;
            for(uint32_t idx : current.indices) {
                G += g[idx];
                H += h[idx];
            }
            
            // Check stopping criteria
            if (current.depth >= config.max_depth || 
                H < config.min_child_weight || 
                current.indices.size() < 2) {
                float weight = -G / (H + config.lambda);
                tree.nodes[current.nodeIndex] = XG_node::makeLeafNode(weight);
                continue;
            }
            
            XG_SplitInfo bestSplit = findBestSplit(current.indices, g, h);
            
            if (bestSplit.gain <= config.gamma || bestSplit.left_indices.empty() || bestSplit.right_indices.empty()) {
                float weight = -G / (H + config.lambda);
                tree.nodes[current.nodeIndex] = XG_node::makeLeafNode(weight);
                continue;
            }
            
            // Create split node
            uint32_t leftIdx = static_cast<uint32_t>(tree.nodes.size());
            tree.nodes[current.nodeIndex] = XG_node::makeSplitNode(bestSplit.featureID, bestSplit.threshold, leftIdx);
            
            tree.nodes.push_back(XG_node()); // Left child
            tree.nodes.push_back(XG_node()); // Right child
            
            queue.push_back(XG_NodeToBuild(leftIdx, std::move(bestSplit.left_indices), current.depth + 1));
            queue.push_back(XG_NodeToBuild(leftIdx + 1, std::move(bestSplit.right_indices), current.depth + 1));
        }
    }

    XG_SplitInfo findBestSplit(const vector<uint32_t>& indices, const vector<float>& g, const vector<float>& h) {
        XG_SplitInfo best;
        float G_total = 0, H_total = 0;
        for(uint32_t idx : indices) {
            G_total += g[idx];
            H_total += h[idx];
        }
        
        float score_root = (G_total * G_total) / (H_total + config.lambda);
        
        const uint16_t numFeatures = config.num_features;
        const uint16_t numCandidates = 1 << config.quantization_coefficient;

        #ifdef _OPENMP
        #pragma omp parallel
        #endif
        {
            XG_SplitInfo local_best;
            #ifdef _OPENMP
            #pragma omp for schedule(dynamic)
            #endif
            for (int f = 0; f < (int)numFeatures; ++f) {
                for (uint16_t threshold = 0; threshold < numCandidates; ++threshold) {
                    float G_L = 0, H_L = 0;
                    uint32_t left_count = 0;
                    
                    for (uint32_t idx : indices) {
                        if (train_data.allSamples[idx].features[f] <= threshold) {
                            G_L += g[idx];
                            H_L += h[idx];
                            left_count++;
                        }
                    }
                    
                    if (left_count == 0 || left_count == indices.size()) continue;
                    if (H_L < config.min_child_weight) continue;
                    
                    float G_R = G_total - G_L;
                    float H_R = H_total - H_L;
                    if (H_R < config.min_child_weight) continue;
                    
                    float score_left = (G_L * G_L) / (H_L + config.lambda);
                    float score_right = (G_R * G_R) / (H_R + config.lambda);
                    float gain = 0.5f * (score_left + score_right - score_root) - config.gamma;
                    
                    if (gain > local_best.gain) {
                        local_best.gain = gain;
                        local_best.featureID = f;
                        local_best.threshold = threshold;
                    }
                }
            }
            
            #ifdef _OPENMP
            #pragma omp critical
            #endif
            {
                if (local_best.gain > best.gain) {
                    best = local_best;
                }
            }
        }
        
        // Collect indices for best split
        if (best.gain > 0) {
            for (uint32_t idx : indices) {
                if (train_data.allSamples[idx].features[best.featureID] <= best.threshold) {
                    best.left_indices.push_back(idx);
                } else {
                    best.right_indices.push_back(idx);
                }
            }
        }
        
        return best;
    }

    uint16_t predict_sample(const Rf_sample& sample) {
        vector<float> scores(config.num_labels, 0.0f);
        for (size_t i = 0; i < trees.size(); ++i) {
            uint16_t k = i % config.num_labels;
            scores[k] += config.learning_rate * trees[i].predictSample(sample, config.quantization_coefficient);
        }
        return static_cast<uint16_t>(std::distance(scores.begin(), std::max_element(scores.begin(), scores.end())));
    }

    void evaluate() {
        std::cout << "\n🧪 Evaluating XGBoost Model...\n";
        
        // Test set evaluation
        uint32_t correct = 0;
        for (const auto& sample : test_data.allSamples) {
            uint16_t pred = predict_sample(sample);
            if (pred == sample.label) correct++;
        }
        float test_accuracy = static_cast<float>(correct) / test_data.allSamples.size();
        
        // Train set evaluation
        uint32_t train_correct = 0;
        for (const auto& sample : train_data.allSamples) {
            uint16_t pred = predict_sample(sample);
            if (pred == sample.label) train_correct++;
        }
        float train_accuracy = static_cast<float>(train_correct) / train_data.allSamples.size();
        
        std::cout << "   Train Accuracy: " << std::fixed << std::setprecision(4) << train_accuracy << "\n";
        std::cout << "   Test Accuracy:  " << std::fixed << std::setprecision(4) << test_accuracy << "\n";
        std::cout << "   Train Samples:  " << train_correct << "/" << train_data.allSamples.size() << "\n";
        std::cout << "   Test Samples:   " << correct << "/" << test_data.allSamples.size() << "\n";
    }

    void printModelStatistics() {
        uint32_t totalNodes = 0;
        uint32_t totalLeafs = 0;
        uint16_t maxDepth = 0;
        
        for (const auto& tree : trees) {
            totalNodes += tree.countNodes();
            totalLeafs += tree.countLeafNodes();
            uint16_t depth = tree.getTreeDepth();
            if (depth > maxDepth) maxDepth = depth;
        }
        
        size_t memoryUsage = 0;
        for (const auto& tree : trees) {
            memoryUsage += tree.memoryUsage();
        }
        
        std::cout << "\n📊 Model Statistics:\n";
        std::cout << "   Total trees: " << trees.size() << "\n";
        std::cout << "   Total nodes: " << totalNodes << "\n";
        std::cout << "   Total leafs: " << totalLeafs << "\n";
        std::cout << "   Avg nodes/tree: " << std::fixed << std::setprecision(1) 
                  << (float)totalNodes / trees.size() << "\n";
        std::cout << "   Max depth: " << maxDepth << "\n";
        std::cout << "   Memory usage: " << memoryUsage << " bytes ("
                  << std::fixed << std::setprecision(2) << (memoryUsage / 1024.0) << " KB)\n";
        std::cout << "   Node size: " << sizeof(XG_node) << " bytes (64-bit packed)\n";
    }

    void saveModel() {
        std::cout << "\n💾 Saving XGBoost Model...\n";
        
        // Save configuration
        config.saveConfig(result_config_path);
        
        // Save model (binary format)
        std::ofstream file(result_model_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "❌ Failed to save model: " << result_model_path << std::endl;
            return;
        }
        
        // Write header
        uint32_t magic = 0x58474221; // "XGB!"
        uint32_t version = 1;
        uint32_t num_trees = static_cast<uint32_t>(trees.size());
        
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));
        file.write(reinterpret_cast<const char*>(&num_trees), sizeof(num_trees));
        
        // Write trees
        for (const auto& tree : trees) {
            uint32_t num_nodes = tree.countNodes();
            file.write(reinterpret_cast<const char*>(&num_nodes), sizeof(num_nodes));
            file.write(reinterpret_cast<const char*>(tree.nodes.data()), num_nodes * sizeof(XG_node));
        }
        
        file.close();
        std::cout << "   Model saved to: " << result_model_path << "\n";
        std::cout << "   Config saved to: " << result_config_path << "\n";
    }
};

int main(int argc, char** argv) {
    std::string config_file = xg_config_path;
    int num_threads = -1;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        }
    }
    
    #ifdef _OPENMP
    if (num_threads > 0) {
        omp_set_num_threads(num_threads);
        std::cout << "Using " << num_threads << " threads for parallel processing\n";
    }
    #endif
    
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "=================================================\n";
    std::cout << "XGBoost PC Training v" << VERSION << "\n";
    std::cout << "=================================================\n\n";
    
    XGBoost xgb(config_file);
    xgb.build_model();
    xgb.evaluate();
    xgb.saveModel();
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "\n=================================================\n";
    std::cout << "⏱️  Total training time: " << elapsed.count() << " seconds\n";
    std::cout << "=================================================\n";
    
    return 0;
}
