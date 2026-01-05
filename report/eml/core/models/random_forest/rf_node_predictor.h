#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Rf_node_predictor {
    public:
        float coefficients[4];  // bias, min_split_coeff, min_leaf_coeff, max_depth_coeff
        bool is_trained;
        b_vector<node_data, 12> buffer;
    private:
        const Rf_base* base_ptr = nullptr;
        const Rf_config* config_ptr = nullptr;
        uint32_t trained_sample_count = 0;   // Samples present when coefficients were derived
        bool dataset_warning_emitted = false;
        bool dataset_drift_emitted = false;
        
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }

        float evaluate_formula(const node_data& data) const {
            if (!is_trained) {
                return manual_estimate(data); // Use manual estimate if not trained
            }
            
            float result = coefficients[0]; // bias
            result += coefficients[1] * static_cast<float>(data.min_split);
            result += coefficients[2] * static_cast<float>(data.min_leaf);
            result += coefficients[3] * static_cast<float>(data.max_depth);
            
            return result > 10.0f ? result : 10.0f; // ensure reasonable minimum
        }

        // if failed to load predictor, manual estimate will be used
        float manual_estimate(const node_data& data) const {
            if (data.min_split == 0) {
                return 100.0f; 
            }
            
            // Enhanced heuristic considering dataset complexity
            // Base estimate accounts for tree structure parameters
            float safe_leaf = max(1.0f, static_cast<float>(data.min_leaf));
            float leaf_adjustment = 60.0f / safe_leaf;
            float depth_factor = min(250.0f, static_cast<float>(data.max_depth)) / 50.0f;
            
            // Dataset complexity factors
            float sample_factor = 1.0f;
            float feature_factor = 1.0f;
            float label_factor = 1.0f;
            
            if (config_ptr) {
                // More samples → more potential nodes (logarithmic growth)
                if (config_ptr->num_samples > 100) {
                    sample_factor = 1.0f + 0.5f * log2(static_cast<float>(config_ptr->num_samples) / 100.0f);
                    sample_factor = min(2.5f, sample_factor); // Cap at 2.5x
                }
                
                // More features → more splitting opportunities (sublinear)
                if (config_ptr->num_features > 10) {
                    feature_factor = 1.0f + 0.3f * log2(static_cast<float>(config_ptr->num_features) / 10.0f);
                    feature_factor = min(2.0f, feature_factor); // Cap at 2.0x
                }
                
                // More labels → more complex decision boundaries (linear)
                if (config_ptr->num_labels > 2) {
                    label_factor = 0.8f + 0.2f * static_cast<float>(config_ptr->num_labels) / 10.0f;
                    label_factor = min(1.5f, label_factor); // Cap at 1.5x
                }
            }
            
            float estimate = 120.0f - data.min_split * 10.0f + leaf_adjustment + depth_factor * 15.0f;
            estimate *= (sample_factor * feature_factor * label_factor);
            
            return estimate < 10.0f ? 10.0f : estimate; // ensure reasonable minimum
        }

        // Predict number of nodes for given parameters
        float raw_estimate(const node_data& data) {
            if(!is_trained) {
                if(!loadPredictor()){
                    return manual_estimate(data); // Use manual estimate if predictor is disabled 
                }
            }
            float prediction = evaluate_formula(data);
            if (is_trained && config_ptr) {
                uint32_t current_samples = config_ptr->num_samples;
                if (trained_sample_count > 0 && current_samples > 0) {
                    float ratio = static_cast<float>(current_samples) / static_cast<float>(trained_sample_count);
                    if (ratio > 1.75f || ratio < 0.5f) {
                        if (!dataset_drift_emitted) {
                            eml_debug_2(1, "⚠️ Node predictor dataset drift detected. Trained on ", trained_sample_count, ", current samples: ", current_samples);
                            eml_debug(1, "   Recommendation: retrain node predictor to refresh coefficients.");
                            dataset_drift_emitted = true;
                        }
                        return manual_estimate(data);
                    }

                    if ((ratio > 1.05f || ratio < 0.95f) && !dataset_warning_emitted) {
                        eml_debug(1, "ℹ️ Adjusting node estimate for sample count change.");
                        eml_debug_2(1, "   factor: ", ratio, "", "");
                        dataset_warning_emitted = true;
                    }

                    float clamped_ratio = ratio;
                    if (clamped_ratio > 1.35f) clamped_ratio = 1.35f;
                    if (clamped_ratio < 0.75f) clamped_ratio = 0.75f;
                    prediction *= clamped_ratio;
                }
            }
            return prediction; 
        }
        

    public:
        uint8_t accuracy;      // in percentage
        uint8_t peak_percent;  // number of nodes at depth with maximum number of nodes / total number of nodes in tree
        
        Rf_node_predictor() : is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 4; i++) {
                coefficients[i] = 0.0f;
            }
            trained_sample_count = 0;
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            base_ptr = nullptr;
            config_ptr = nullptr;
        }

        Rf_node_predictor(Rf_base* base) : base_ptr(base), is_trained(false), accuracy(0), peak_percent(0) {
            eml_debug(2, "🔧 Initializing node predictor");
            for (int i = 0; i < 4; i++) {
                coefficients[i] = 0.0f;
            }
            trained_sample_count = 0;
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            config_ptr = nullptr;
        }

        ~Rf_node_predictor() {
            base_ptr = nullptr;
            config_ptr = nullptr;
            is_trained = false;
            buffer.clear();
            trained_sample_count = 0;
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
        }

        void init(Rf_base* base, const Rf_config* config = nullptr) {
            base_ptr = base;
            config_ptr = config;
            is_trained = false;
            trained_sample_count = 0;
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            for (int i = 0; i < 4; i++) {
                coefficients[i] = 0.0f;
            }
            char node_predictor_log[RF_PATH_BUFFER] = {0};
            if (base_ptr) {
                base_ptr->get_node_log_path(node_predictor_log);
            }
            // Create new file with correct header if it doesn't exist
            if (node_predictor_log[0] != '\0' && !RF_FS_EXISTS(node_predictor_log)) {
                File logFile = RF_FS_OPEN(node_predictor_log, FILE_WRITE);
                if (logFile) {
                    logFile.println("min_split,min_leaf,max_depth,max_nodes");
                    logFile.close();
                }
            }
        }
        
        // Load trained model from file system (updated format without version)
        bool loadPredictor() {
            if (!has_base()){
                eml_debug(0, "❌ Load Predictor failed: base pointer not ready");
                return false;
            }
            char file_path[RF_PATH_BUFFER];
            base_ptr->get_node_pred_path(file_path);
            eml_debug(2, "🔍 Loading node predictor from file: ", file_path);
            if(is_trained) return true;
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            if (!RF_FS_EXISTS(file_path)) {
                eml_debug(1, "⚠️  No predictor file found, using default predictor.");
                return false;
            }
            
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open predictor file: ", file_path);
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                eml_debug(0, "❌ Invalid predictor file format: ", file_path);
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                eml_debug(0, "❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                eml_debug(2, "⚠️ Failed to read accuracy, using manual estimate node.");
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                eml_debug(2, "⚠️ Failed to read peak_percent, using manual estimate node.");
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients)) {
                eml_debug(0, "❌ Failed to read coefficient count");
                file.close();
                return false;
            }

            if (num_coefficients == 3) {
                if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                    eml_debug(0, "❌ Failed to read legacy coefficients");
                    file.close();
                    return false;
                }
                coefficients[3] = 0.0f; // Legacy files omitted depth coefficient
            } else if (num_coefficients == 4) {
                if (file.read((uint8_t*)coefficients, sizeof(float) * 4) != sizeof(float) * 4) {
                    eml_debug(0, "❌ Failed to read coefficients");
                    file.close();
                    return false;
                }
            } else {
                eml_debug_2(2, "❌ Unsupported coefficient count: ", num_coefficients, "", "");
                file.close();
                return false;
            }

            // Optional sample count metadata (available in new format)
            trained_sample_count = 0;
            uint32_t stored_samples = 0;
            size_t bytes_read = file.read((uint8_t*)&stored_samples, sizeof(stored_samples));
            if (bytes_read == sizeof(stored_samples)) {
                trained_sample_count = stored_samples;
            }
            
            file.close();
            
            // Only set is_trained to true if the file was actually trained
            if (file_is_trained) {
                is_trained = true;
                if (peak_percent == 0) {
                    peak_percent = 30; // Use reasonable default for binary trees
                    eml_debug(2, "⚠️  Fixed peak_percent from 0% to 30%");
                }
                eml_debug(1, "✅ Node predictor loaded : ", file_path);
                eml_debug(2, "bias: ", this->coefficients[0]);
                eml_debug(2, "min_split effect: ", this->coefficients[1]);
                eml_debug(2, "min_leaf effect: ", this->coefficients[2]);
                eml_debug(2, "accuracy: ", accuracy);
                dataset_warning_emitted = false;
                dataset_drift_emitted = false;
                if (trained_sample_count == 0) {
                    eml_debug(2, "ℹ️ Predictor file missing sample count metadata (legacy format).");
                } else {
                    eml_debug_2(2, "   Predictor trained on samples: ", trained_sample_count, "", "");
                }
            } else {
                eml_debug(1, "⚠️  Predictor file exists but is not trained, using default predictor.");
                is_trained = false;
                trained_sample_count = 0;
                dataset_warning_emitted = false;
                dataset_drift_emitted = false;
            }
            return file_is_trained;
        }
        
        // Save trained predictor to file system
        bool releasePredictor() {
            if (!has_base()){
                eml_debug(0, "❌ Release Predictor failed: base pointer not ready");
                return false;
            }
            if (!is_trained) {
                eml_debug(1, "❌ Predictor is not trained, cannot save.");
                return false;
            }
            char file_path[RF_PATH_BUFFER];
            base_ptr->get_node_pred_path(file_path);
            if (RF_FS_EXISTS(file_path)) RF_FS_REMOVE(file_path);

            File file = RF_FS_OPEN(file_path, FILE_WRITE);
            if (!file) {
                eml_debug(0, "❌ Failed to create predictor file: ", file_path);
                return false;
            }
            
            if (config_ptr) {
                trained_sample_count = static_cast<uint32_t>(config_ptr->num_samples);
            }

            // Write magic number
            uint32_t magic = 0x4E4F4445; // "NODE" in hex
            file.write((uint8_t*)&magic, sizeof(magic));
            
            // Write training status
            file.write((uint8_t*)&is_trained, sizeof(is_trained));
            
            // Write accuracy and peak_percent
            file.write((uint8_t*)&accuracy, sizeof(accuracy));
            file.write((uint8_t*)&peak_percent, sizeof(peak_percent));
            
            // Write number of coefficients
            uint8_t num_coefficients = 4;
            file.write((uint8_t*)&num_coefficients, sizeof(num_coefficients));
            
            // Write coefficients
            file.write((uint8_t*)coefficients, sizeof(float) * 4);

            // Write dataset sample count metadata
            file.write((uint8_t*)&trained_sample_count, sizeof(trained_sample_count));
            
            file.close();
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            eml_debug(1, "✅ Node predictor saved: ", file_path);
            return true;
        }
        
        // Add new training samples to buffer
        void add_new_samples(uint8_t min_split, uint8_t min_leaf, uint16_t max_depth, uint32_t total_nodes) {
            if (min_split == 0 || min_leaf == 0) return; // invalid sample
            if (buffer.size() >= 100) {
                eml_debug(2, "⚠️ Node_pred buffer full, consider retraining soon.");
                return;
            }
            buffer.push_back(node_data(min_split, min_leaf, max_depth, total_nodes));
        }
        // Retrain the predictor using data from rf_tree_log.csv (synchronized with PC version)
        bool re_train(bool save_after_retrain = true) {
            if (!has_base()){
                eml_debug(0, "❌ Base pointer is null, cannot retrain predictor.");
                return false;
            }
            if(buffer.size() > 0){
                flush_buffer();
            }
            buffer.clear();
            buffer.fit();

            if(!can_retrain()) {
                eml_debug(2, "❌ No training data available for retraining.");
                return false;
            }

            char node_predictor_log[RF_PATH_BUFFER];
            base_ptr->get_node_log_path(node_predictor_log);
            eml_debug(2, "🔂 Starting retraining of node predictor...");
            File file = RF_FS_OPEN(node_predictor_log, RF_FILE_READ);
            if (!file) {
                eml_debug(1, "❌ Failed to open node_predictor log file: ", node_predictor_log);
                return false;
            }
            eml_debug(2, "🔄 Retraining node predictor from CSV data...");
            
            b_vector<node_data> training_data;
            training_data.reserve(50); // Reserve space for training samples
            
            String line;
            bool first_line = true;
            
            // Read CSV data
            while (file.available()) {
                line = file.readStringUntil('\n');
                line.trim();
                
                if (line.length() == 0 || first_line) {
                    first_line = false;
                    continue; // Skip header line
                }
                
                // Parse CSV line: min_split,min_leaf,max_depth,total_nodes
                node_data sample;
                int comma1 = line.indexOf(',');
                int comma2 = line.indexOf(',', comma1 + 1);
                int comma3 = line.indexOf(',', comma2 + 1);
                
                if (comma1 != -1 && comma2 != -1 && comma3 != -1) {
                    String min_split_str = line.substring(0, comma1);
                    String min_leaf_str = line.substring(comma1 + 1, comma2);
                    String max_depth_str = line.substring(comma2 + 1, comma3);
                    String total_nodes_str = line.substring(comma3 + 1);

                    int parsed_split = min_split_str.toInt();
                    if (parsed_split < 0) parsed_split = 0;
                    if (parsed_split > 255) parsed_split = 255;
                    sample.min_split = static_cast<uint8_t>(parsed_split);

                    int parsed_leaf = min_leaf_str.toInt();
                    if (parsed_leaf < 0) parsed_leaf = 0;
                    if (parsed_leaf > 255) parsed_leaf = 255;
                    sample.min_leaf = static_cast<uint8_t>(parsed_leaf);
                    
                    int parsed_depth = max_depth_str.toInt();
                    if (parsed_depth < 0) parsed_depth = 0;
                    if (parsed_depth > 65535) parsed_depth = 65535;
                    sample.max_depth = static_cast<uint16_t>(parsed_depth);
                    
                    sample.total_nodes = total_nodes_str.toInt();
                    
                    // skip invalid samples
                    if (sample.min_split > 0 && sample.min_leaf > 0 && sample.max_depth > 0 && sample.total_nodes > 0) {
                        training_data.push_back(sample);
                    }
                }
            }
            file.close();
            
            if (training_data.size() < 3) {
                return false;
            }
            
            // Collect all unique min_split and min_leaf values
            b_vector<uint8_t> unique_min_splits;
            b_vector<uint8_t> unique_min_leafs;
            
            for (const auto& sample : training_data) {
                // Add unique min_split values
                bool found_split = false;
                for (const auto& existing_split : unique_min_splits) {
                    if (existing_split == sample.min_split) {
                        found_split = true;
                        break;
                    }
                }
                if (!found_split) {
                    unique_min_splits.push_back(sample.min_split);
                }
                
                // Add unique min_leaf values
                bool found_leaf = false;
                for (const auto& existing_leaf : unique_min_leafs) {
                    if (existing_leaf == sample.min_leaf) {
                        found_leaf = true;
                        break;
                    }
                }
                if (!found_leaf) {
                    unique_min_leafs.push_back(sample.min_leaf);
                }
            }
            
            // Sort vectors for easier processing
            unique_min_splits.sort();
            unique_min_leafs.sort();
            
            // Calculate effects using a simpler approach without large intermediate vectors
            // Calculate min_split effect directly
            float split_effect = 0.0f;
            if (unique_min_splits.size() >= 2) {
                // Calculate average nodes for first and last min_split values
                float first_split_avg = 0.0f;
                float last_split_avg = 0.0f;
                int first_split_count = 0;
                int last_split_count = 0;
                
                uint8_t first_split = unique_min_splits[0];
                uint8_t last_split = unique_min_splits[unique_min_splits.size() - 1];
                
                // Calculate averages directly from training data
                for (const auto& sample : training_data) {
                    if (sample.min_split == first_split) {
                        first_split_avg += sample.total_nodes;
                        first_split_count++;
                    } else if (sample.min_split == last_split) {
                        last_split_avg += sample.total_nodes;
                        last_split_count++;
                    }
                }
                
                if (first_split_count > 0 && last_split_count > 0) {
                    first_split_avg /= first_split_count;
                    last_split_avg /= last_split_count;
                    
                    float split_range = static_cast<float>(last_split - first_split);
                    if (split_range > 0.01f) {
                        split_effect = (last_split_avg - first_split_avg) / split_range;
                    }
                }
            }
            
            // Calculate min_leaf effect directly
            float leaf_effect = 0.0f;
            if (unique_min_leafs.size() >= 2) {
                // Calculate average nodes for first and last min_leaf values
                float first_leaf_avg = 0.0f;
                float last_leaf_avg = 0.0f;
                int first_leaf_count = 0;
                int last_leaf_count = 0;

                uint8_t first_leaf = unique_min_leafs[0];
                uint8_t last_leaf = unique_min_leafs[unique_min_leafs.size() - 1];

                // Calculate averages directly from training data
                for (const auto& sample : training_data) {
                    if (sample.min_leaf == first_leaf) {
                        first_leaf_avg += sample.total_nodes;
                        first_leaf_count++;
                    } else if (sample.min_leaf == last_leaf) {
                        last_leaf_avg += sample.total_nodes;
                        last_leaf_count++;
                    }
                }

                if (first_leaf_count > 0 && last_leaf_count > 0) {
                    first_leaf_avg /= first_leaf_count;
                    last_leaf_avg /= last_leaf_count;

                    float leaf_range = static_cast<float>(last_leaf - first_leaf);
                    if (leaf_range > 0.01f) {
                        leaf_effect = (last_leaf_avg - first_leaf_avg) / leaf_range;
                    }
                }
            }
            
            // Calculate overall average as baseline
            float overall_avg = 0.0f;
            for (const auto& sample : training_data) {
                overall_avg += sample.total_nodes;
            }
            overall_avg /= training_data.size();
            
            // Build the simple linear predictor: nodes = bias + split_coeff * min_split + leaf_coeff * min_leaf
            // Calculate bias to center the predictor around the overall average
            float reference_split = unique_min_splits.empty() ? 3.0f : static_cast<float>(unique_min_splits[0]);
            float reference_leaf = unique_min_leafs.empty() ? 2.0f : static_cast<float>(unique_min_leafs[0]);
            
            coefficients[0] = overall_avg - (split_effect * reference_split) - (leaf_effect * reference_leaf); // bias
            coefficients[1] = split_effect; // min_split coefficient
            coefficients[2] = leaf_effect; // min_leaf coefficient
            
            // Calculate accuracy using PC version's approach exactly
            float total_error = 0.0f;
            float total_actual = 0.0f;
            
            for (const auto& sample : training_data) {
                node_data data(sample.min_split, sample.min_leaf);
                float predicted = evaluate_formula(data);
                float actual = static_cast<float>(sample.total_nodes);
                float error = fabs(predicted - actual);
                total_error += error;
                total_actual += actual;
            }
            
            float mae = total_error / training_data.size(); // Mean Absolute Error
            float mape = (total_error / total_actual) * 100.0f; // Mean Absolute Percentage Error
            
            float get_accuracy_result = fmax(0.0f, 100.0f - mape);
            accuracy = static_cast<uint8_t>(fmin(255.0f, get_accuracy_result * 100.0f / 100.0f)); // Simplify to match intent
            
            // Actually, let's just match the logical intent rather than the bug
            accuracy = static_cast<uint8_t>(fmin(100.0f, fmax(0.0f, 100.0f - mape)));
            
            peak_percent = 30; // A reasonable default for binary tree structures
            
            is_trained = true;
            if (config_ptr) {
                trained_sample_count = static_cast<uint32_t>(config_ptr->num_samples);
            }
            dataset_warning_emitted = false;
            dataset_drift_emitted = false;
            eml_debug(2, "✅ Node predictor retraining complete!");
            eml_debug_2(2, "   Accuracy: ", accuracy, "%, Peak (%): ", peak_percent);
            if(save_after_retrain) {
                releasePredictor(); // Save the new predictor
            }
            return true;
        }
        
        uint16_t estimate_nodes(uint8_t min_split, uint8_t min_leaf, uint16_t max_depth = 50) {
            if (min_leaf == 0) {
                min_leaf = 1;
            }
            node_data data(min_split, min_leaf, max_depth);
            float raw_est = raw_estimate(data);
            float acc = accuracy;
            if(acc < 90.0f) acc = 90.0f;
            uint16_t estimate = static_cast<uint16_t>(raw_est * 100 / acc);
            uint16_t safe_estimate;
            if(config_ptr->num_samples < 2024) safe_estimate = 512;
            else safe_estimate = max(static_cast<rf_sample_type>((config_ptr->num_samples / config_ptr->min_leaf)), RF_MAX_NODES);
            return estimate < RF_MAX_NODES ? estimate : safe_estimate;       
        }

        uint16_t estimate_nodes(const Rf_config& config) {
            uint8_t min_split = config.min_split;
            uint8_t min_leaf = config.min_leaf > 0 ? config.min_leaf : static_cast<uint8_t>(1);
            uint16_t max_depth = config.max_depth > 0 ? config.max_depth : 25;
            return estimate_nodes(min_split, min_leaf, max_depth);
        }

        uint16_t queue_peak_size(uint8_t min_split, uint8_t min_leaf, uint16_t max_depth = 250) {
            return min(120, estimate_nodes(min_split, min_leaf, max_depth) * peak_percent / 100);
        }

        uint16_t queue_peak_size(const Rf_config& config) {
            uint16_t est_nodes = estimate_nodes(config);
            if(config.training_score == K_FOLD_SCORE){
                est_nodes = est_nodes * config.k_folds / (config.k_folds + 1);
            }
            est_nodes = static_cast<uint16_t>(est_nodes * peak_percent / 100);
            uint16_t max_peak_theory = max(static_cast<rf_sample_type>((config_ptr->num_samples / config_ptr->min_leaf)), RF_MAX_NODES) * 0.3; // 30% of theoretical max nodes
            uint16_t min_peak_theory = 30;
            if (est_nodes > max_peak_theory) return max_peak_theory;
            if (est_nodes < min_peak_theory) return min_peak_theory;
            return est_nodes;
        }

        void flush_buffer() {
            if (!has_base()){
                eml_debug(0, "❌Failed to flush_buffer : base pointer is null");
                return;
            }
            char node_predictor_log[RF_PATH_BUFFER];
            base_ptr->get_node_log_path(node_predictor_log);
            if (buffer.size() == 0) return;
            // Read all existing lines
            b_vector<String> lines;
            File file = RF_FS_OPEN(node_predictor_log, RF_FILE_READ);
            if (file) {
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0) lines.push_back(line);
                }
                file.close();
            }
            // Ensure header is present
            String header = "min_split,min_leaf,max_depth,total_nodes";
            if (lines.empty() || lines[0] != header) {
                lines.insert(0, header);
            }
            // Remove header for easier manipulation and discard legacy headers
            b_vector<String> data_lines;
            for (size_t i = 1; i < lines.size(); ++i) {
                const String& existing = lines[i];
                if (existing == header) {
                    continue;
                }
                if (existing.length() == 0) {
                    continue;
                }
                char first_char = existing.charAt(0);
                if (first_char < '0' || first_char > '9') {
                    continue;
                }
                data_lines.push_back(existing);
            }
            // Prepend new samples
            for (int i = buffer.size() - 1; i >= 0; --i) {
                const node_data& nd = buffer[i];
                String row = String(nd.min_split) + "," + String(nd.min_leaf) + "," + String(nd.max_depth) + "," + String(nd.total_nodes);
                data_lines.insert(0, row);
            }
            // Limit to 50 rows
            while (data_lines.size() > 50) {
                data_lines.pop_back();
            }
            // Write back to file
            RF_FS_REMOVE(node_predictor_log);
            file = RF_FS_OPEN(node_predictor_log, FILE_WRITE);
            if (file) {
                file.println(header);
                for (const auto& row : data_lines) {
                    file.println(row);
                }
                file.close();
            }
        }

        bool can_retrain() const {
            if (!has_base()) {
                eml_debug(0, "❌ can_retrain check failed: base pointer not ready");
                return false;
            }
            char node_predictor_log[RF_PATH_BUFFER];
            base_ptr->get_node_log_path(node_predictor_log);
            if (!RF_FS_EXISTS(node_predictor_log)) {
                eml_debug(2, "❌ No log file found for retraining.");
                return false;
            }
            File file = RF_FS_OPEN(node_predictor_log, RF_FILE_READ);
            bool result = file && file.size() > 0;
            // only retrain if log file has more 4 samples (excluding header)
            if (result) {
                size_t line_count = 0;
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.length() > 0) line_count++;
                }
                result = line_count > 4; // more than header + 3 samples
            }
            if (file) file.close();
            if(!result){
                eml_debug(2, "❌ Not enough data for retraining (need > 3 samples).");
            }
            return result;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_node_predictor);
            total += buffer.capacity() * sizeof(node_data);
            return total + 4;
        }
    };

} // namespace eml
