#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Rf_config {
        const Rf_base* base_ptr = nullptr;
        bool isLoaded = false; 

        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }
    public:
        // Core model configuration
        uint8_t     num_trees;
        uint32_t    random_seed;
        uint8_t     min_split;
        uint8_t     min_leaf;
        uint8_t     max_depth;
        bool        use_boostrap;
        bool        use_gini;
        uint8_t     k_folds;
        float       boostrap_ratio; 
        float       impurity_threshold;
        float       train_ratio;
        float       test_ratio;
        float       valid_ratio;
        uint8_t     metric_score;
        float       result_score;
        uint32_t    estimatedRAM;
        Rf_training_score training_score;

        bool enable_retrain;
        bool enable_auto_config;   // change config based on dataset parameters (when base_data expands)
        bool allow_new_labels;     // allow new labels to be added to the dataset (default: false)


        // runtime parameters
        pair<uint8_t, uint8_t> min_split_range;
        pair<uint8_t, uint8_t> min_leaf_range;
        pair<uint16_t, uint16_t> max_depth_range; 

        // Dataset parameters 
        rf_sample_type num_samples;
        rf_sample_type max_samples = 0; // Maximum samples allowed (0 = unlimited). When exceeded, oldest samples are removed
        uint16_t num_features;
        rf_label_type  num_labels;
        uint8_t  quantization_coefficient; // Bits per feature value (1-8)
        float lowest_distribution; 
        b_vector<rf_sample_type,8> samples_per_label; // index = label, value = count

        // MCU node layout bits (loaded from PC-trained model config)
        uint8_t threshold_bits = 0;
        uint8_t feature_bits = 0;
        uint8_t label_bits = 0;
        uint8_t child_bits = 0;

        void init(Rf_base* base) {
            base_ptr = base;
            isLoaded = false;

            // Set default values
            num_trees           = 20;
            random_seed         = 37;
            min_split           = 2;
            min_leaf            = 1;
            max_depth           = 250;
            use_boostrap        = true;
            boostrap_ratio      = 0.632f; 
            use_gini            = false;
            k_folds             = 4;
            impurity_threshold  = 0.0f;
            train_ratio         = 0.8f;
            test_ratio          = 0.0f;
            valid_ratio         = 0.0f;
            training_score      = OOB_SCORE;
            metric_score        = Rf_metric_scores::ACCURACY;
            result_score        = 0.0;
            estimatedRAM        = 0;
            enable_retrain      = true;
            enable_auto_config  = false;
            allow_new_labels    = false;
            quantization_coefficient = 2; 
            max_samples = 0; // unlimited by default
        }
        
        Rf_config() {
            init(nullptr);
        }
        Rf_config(Rf_base* base) {
            init(base);
        }

        ~Rf_config() {
            releaseConfig();
            base_ptr = nullptr;
        }

    private:
        //  scan base_data file to get dataset parameters (when no dp file found)
        bool scan_base_data(){
            char base_file_path[RF_PATH_BUFFER];
            base_ptr->get_base_data_path(base_file_path);
            eml_debug(1, "📊 Scanning base data: ", base_file_path);

            File file = RF_FS_OPEN(base_file_path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open base data file for scanning: ", base_file_path);
                return false;
            }

            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
               file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                eml_debug(0, "❌ Failed to read dataset header during scan", base_file_path);
                file.close();
                return false;
            }

            // Set basic parameters
            num_samples = numSamples;
            num_features = numFeatures;

            // Calculate packed feature bytes per sample (using existing quantization_coefficient)
            uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
            const uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte

            // Track unique labels and their counts
            unordered_map_s<rf_label_type, rf_sample_type> label_counts;
            rf_label_type max_label = 0;

            // Scan through all samples to collect label statistics
            for(rf_sample_type i = 0; i < numSamples; i++) {
                rf_label_type label;
                if(file.read(&label, sizeof(label)) != sizeof(label)) {
                    eml_debug_2(0, "❌ Failed to read label of sample", i, ": ", base_file_path);
                    file.close();
                    return false;
                }

                // Track label statistics
                auto it = label_counts.find(label);
                if(it != label_counts.end()) {
                    it->second++;
                } else {
                    label_counts[label] = 1;
                }

                if(label > max_label) {
                    max_label = label;
                }

                // Skip packed features for this sample
                if(file.seek(file.position() + packedFeatureBytes) == false) {
                    eml_debug_2(0, "❌ Failed to skip features of sample", i, ": ", base_file_path);
                    file.close();
                    return false;
                }
            }

            file.close();

            // Set number of labels
            num_labels = label_counts.size();

            // Initialize samples_per_label vector with proper size
            samples_per_label.clear();
            samples_per_label.resize(max_label + 1, 0);

            // Fill samples_per_label with counts
            for(auto& pair : label_counts) {
                samples_per_label[pair.first] = pair.second;
            }

            eml_debug(1, "✅ Base data scan complete.");
            eml_debug(1, "   📊 Samples: ", num_samples);
            eml_debug(1, "   🔢 Features: ", num_features);
            eml_debug(1, "   🏷️ Labels: ", num_labels);
            eml_debug(1, "   📈 Samples per label: ");
            for (size_t i = 0; i < samples_per_label.size(); i++) {
                if(samples_per_label[i] > 0) {
                    eml_debug_2(1, "   Lable ", i, ": ", samples_per_label[i]);
                }
            }
            return true;
        }
        
        // generate optimal ranges for min_split and min_leaf based on dataset parameters
        void generate_ranges(bool force = false) {
            int baseline_minsplit_ratio = 100 * (num_samples / 500 + 1);
            if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500;
            uint8_t min_minSplit = 2;

            int dynamic_max_split = min(min_minSplit + 6, (int)(log2(num_samples) / 4 + num_features / 25.0f));
            uint8_t max_minSplit = min<uint8_t>(16, dynamic_max_split);
            if (max_minSplit <= min_minSplit) {
                max_minSplit = static_cast<uint8_t>(min_minSplit + 4);
            }

            float samples_per_label = (num_labels > 0)
                                          ? static_cast<float>(num_samples) / static_cast<float>(num_labels)
                                          : static_cast<float>(num_samples);
            float density_factor = samples_per_label / 600.0f;
            if (density_factor < 0.3f) density_factor = 0.3f;
            if (density_factor > 3.0f) density_factor = 3.0f;

            float expected_min_pct = (num_labels > 0) ? (100.0f / static_cast<float>(num_labels)) : 100.0f;
            float deficit_pct = max(0.0f, expected_min_pct - lowest_distribution);
            float imbalance_ratio = (expected_min_pct > 0.0f) ? (deficit_pct / expected_min_pct) : 0.0f;
            if (imbalance_ratio > 0.5f) imbalance_ratio = 0.5f;
            float imbalance_factor = 1.0f - imbalance_ratio; // 0.5 .. 1.0

            float min_ratio = 0.12f + 0.05f * density_factor * imbalance_factor;
            if (min_ratio < 0.1f) min_ratio = 0.1f;
            if (min_ratio > 0.35f) min_ratio = 0.35f;

            float max_ratio = min_ratio + (0.12f + 0.04f * density_factor);
            float min_allowed = min_ratio + 0.1f;
            if (max_ratio < min_allowed) max_ratio = min_allowed;
            if (max_ratio > 0.6f) max_ratio = 0.6f;

            uint8_t max_cap = (max_minSplit > 1) ? static_cast<uint8_t>(max_minSplit - 1) : static_cast<uint8_t>(1);
            uint8_t min_minLeaf = static_cast<uint8_t>(floorf(static_cast<float>(min_minSplit) * min_ratio));
            if (min_minLeaf < 1) min_minLeaf = 1;
            if (min_minLeaf > max_cap) min_minLeaf = max_cap;

            uint8_t max_minLeaf = static_cast<uint8_t>(ceilf(static_cast<float>(max_minSplit) * max_ratio));
            if (max_minLeaf > max_cap) max_minLeaf = max_cap;
            if (max_minLeaf < min_minLeaf) {
                max_minLeaf = min_minLeaf;
            }

            int base_maxDepth = (int)(log2(num_samples) + log2(num_features)) + 1;
            uint16_t max_maxDepth = max(8, base_maxDepth);
            uint16_t min_maxDepth = max_maxDepth > 18 ? max_maxDepth - 6 : max_maxDepth > 12 ? max_maxDepth - 4 : max_maxDepth > 8 ? max_maxDepth - 2 : 4;

            if (min_split == 0 || force) {
                min_split = min_minSplit;
                eml_debug_2(1, "Setting minSplit to ", min_split, " (auto)", "");
            }
            if (min_leaf == 0 || force) {
                min_leaf = min_minLeaf;
                eml_debug_2(1, "Setting minLeaf to ", min_leaf, " (auto)", "");
            }

            if (max_depth == 0 || force) {
                max_depth = max_maxDepth;
                eml_debug_2(1, "Setting maxDepth to ", max_depth, " (auto)", "");
            } 
            
            eml_debug_2(1, "⚙️ Setting minSplit range: ", min_minSplit, "to ", max_minSplit);
            eml_debug_2(1, "⚙️ Setting minLeaf range: ", min_minLeaf, "to ", max_minLeaf);
            eml_debug_2(1, "⚙️ Setting maxDepth range: ", min_maxDepth, "to ", max_maxDepth);

            min_split_range = make_pair(min_minSplit, max_minSplit);
            min_leaf_range = make_pair(min_minLeaf, max_minLeaf);
            max_depth_range = make_pair(min_maxDepth, max_maxDepth);
        }

        void generate_impurity_threshold(){
            // find lowest distribution
            if (samples_per_label.size() == 0) {
                impurity_threshold = 0.0f;
                return;
            }
            int K = max(2, static_cast<int>(num_labels));
            float expected_min_pct = 100.0f / static_cast<float>(K);
            float deficit = max(0.0f, expected_min_pct - lowest_distribution);
            float imbalance = expected_min_pct > 0.0f ? min(1.0f, deficit / expected_min_pct) : 0.0f; // 0..1

            double log_samples = log2(max(2.0, static_cast<double>(num_samples)));
            double adjusted = max(0.0, log_samples - 10.0); // keep small datasets unaffected
            float sample_factor = static_cast<float>(1.0 / (1.0 + adjusted / 2.5));
            sample_factor = max(0.25f, min(1.15f, sample_factor));
            // Imbalance factor: reduce threshold for imbalanced data to allow splitting on rare classes
            float imbalance_factor = 1.0f - 0.5f * imbalance; // 0.5..1.0
            // Feature factor: with many features, weak splits are common; require slightly higher gain
            float feature_factor = 0.9f + 0.1f * min(1.0f, static_cast<float>(log2(max(2, static_cast<int>(num_features)))) / 8.0f);

            if (use_gini) {
                float max_gini = 1.0f - 1.0f / static_cast<float>(K);
                float base = 0.003f * max_gini; // very small base for Gini
                float thr = base * sample_factor * imbalance_factor * feature_factor;
                impurity_threshold = max(0.0003f, min(0.02f, thr));
            } else { // entropy
                float max_entropy = log2(static_cast<float>(K));
                float base = 0.02f * (max_entropy > 0.0f ? max_entropy : 1.0f); // larger than gini
                float thr = base * sample_factor * imbalance_factor * feature_factor;
                impurity_threshold = max(0.002f, min(0.2f, thr));
            }
            eml_debug(1, "⚙️ Setting impurity_threshold to ", impurity_threshold);
        }
        
        // setup config manually (when no config file)
        void auto_config(){
            // set metric_score based on dataset balance
            if(samples_per_label.size() > 0){
                rf_sample_type minorityCount = num_samples;
                rf_sample_type majorityCount = 0;

                for (auto& count : samples_per_label) {
                    if (count > majorityCount) {
                        majorityCount = count;
                    }
                    if (count < minorityCount) {
                        minorityCount = count;
                    }
                }

                float maxImbalanceRatio = 1/lowest_distribution * 100.0f; 

                if (maxImbalanceRatio > 10.0f) {
                    metric_score = Rf_metric_scores::RECALL;
                    eml_debug_2(1, "⚠️ Highly imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to RECALL.", "");
                } else if (maxImbalanceRatio > 3.0f) {
                    metric_score = Rf_metric_scores::F1_SCORE;
                    eml_debug_2(1, "⚠️ Moderately imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to F1_SCORE.", "");
                } else if (maxImbalanceRatio > 1.5f) {
                    metric_score = Rf_metric_scores::PRECISION;
                    eml_debug_2(1, "⚠️ Slightly imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to PRECISION.", "");
                } else {
                    metric_score = Rf_metric_scores::ACCURACY;
                    eml_debug_2(1, "✅ Balanced dataset (ratio: ", maxImbalanceRatio, "). Setting metric_score to ACCURACY.", "");
                }
            }

            rf_sample_type avg_samples_per_label = num_samples / max(1, static_cast<int>(num_labels));
        
            // set training_score method
            if (avg_samples_per_label < 200){
                training_score = K_FOLD_SCORE;
            }else if (avg_samples_per_label < 500){
                training_score = OOB_SCORE;
            }else{
                training_score = VALID_SCORE;
            }
            validate_ratios();
            generate_ranges(true); // force generate min_split, min_leaf, max_depth
            generate_impurity_threshold(); // no prior distribution info
        }
        
        // read dataset parameters from /dataset_dp.csv and write to config
        bool loadDpFile() {
            char path[RF_PATH_BUFFER];
            base_ptr->get_dp_path(path);
            if (strlen(path) < 1) { 
                eml_debug(0, "❌ load dp file failed: ", "dp path is empty");
                return false; 
            }
            File file = RF_FS_OPEN(path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open data_params file for reading", path);
                return false;
            }

            // Skip header line
            file.readStringUntil('\n');

            // Initialize variables with defaults
            rf_sample_type numSamples = 0;
            uint16_t numFeatures = 0;
            rf_label_type numLabels = 0;
            uint8_t quantCoeff = 2;  // Default 2 bits
            unordered_map_s<rf_label_type, rf_sample_type> labelCounts; // label -> count

            // Parse parameters from CSV
            while (file.available()) {
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) continue;

                int commaIndex = line.indexOf(',');
                if (commaIndex == -1) continue;

                String parameter = line.substring(0, commaIndex);
                String value = line.substring(commaIndex + 1);
                parameter.trim();
                value.trim();

                // Parse key parameters
                if (parameter == "num_features") {
                    numFeatures = value.toInt();
                } else if (parameter == "num_samples") {
                    numSamples = value.toInt();
                } else if (parameter == "num_labels") {
                    numLabels = value.toInt();
                } else if (parameter == "quantization_coefficient") {
                    quantCoeff = value.toInt();
                } else if (parameter.startsWith("samples_label_")) {
                    // Extract label index from parameter name
                    int labelIndex = parameter.substring(14).toInt(); // "samples_label_".length() = 14
                    if (labelIndex < 32) {
                        labelCounts[labelIndex] = value.toInt();
                    }
                } 
            }
            file.close();

            // Store parsed values
            num_features = numFeatures;
            num_samples = numSamples;
            num_labels = numLabels;
            quantization_coefficient = quantCoeff;
            
            // Initialize samples_per_label vector with the parsed label counts
            samples_per_label.clear();
            samples_per_label.resize(numLabels, 0);
            for (rf_label_type i = 0; i < numLabels; i++) {
                if (labelCounts.find(i) != labelCounts.end()) {
                    samples_per_label[i] = labelCounts[i];
                }
            }

            // Validate loaded parameters
            if (num_features == 0 || num_samples == 0 || num_labels == 0) {
                eml_debug(0, "❌ Invalid dataset parameters in dp file", path);
                return false;
            }
            if (! validateSamplesPerLabel()) eml_debug(1, "⚠️ samples_per_label data inconsistency detected");
            return true;
        }
        
        // write back to dataset_params file
        bool releaseDpFile() {
            char path[RF_PATH_BUFFER];
            base_ptr->get_dp_path(path);
            if (path[0] == '\0') return false;
            File file = RF_FS_OPEN(path, RF_FILE_WRITE);
            if (!file) {
                eml_debug(0, "❌ Failed to open data_params file for writing", path);
                return false;
            }
            file.println("parameter,value");
            file.printf("quantization_coefficient,%u\n", quantization_coefficient);
            
            rf_label_type max_value = (quantization_coefficient >= 8) ? RF_ERROR_LABEL : ((1u << quantization_coefficient) - 1);
            uint8_t features_per_byte = (quantization_coefficient == 0) ? 0 : (8 / quantization_coefficient);
            
            file.printf("max_feature_value,%u\n", max_value);
            file.printf("features_per_byte,%u\n", features_per_byte);
            
            file.printf("num_features,%u\n", num_features);
            file.printf("num_samples,%u\n", num_samples);
            file.printf("num_labels,%u\n", num_labels);
        
            // Write actual label counts from samples_per_label vector
            for (rf_label_type i = 0; i < samples_per_label.size(); i++) {
                file.printf("samples_label_%u,%u\n", i, samples_per_label[i]);
            }
            
            file.close();   
            base_ptr->set_dp_status(true);
            eml_debug(1, "✅ Dataset parameters saved: ", path);
            return true;
        }

    public:
        // Load configuration from JSON file
        bool loadConfig() {
            if (isLoaded) return true;
            if (!has_base()) {
                eml_debug(0, "❌ Base pointer is null or base not ready", "load config");
                return false;
            }

            // load dataset parameters session 
            bool dp_ok =  false;
            if(base_ptr->dp_file_exists()){
                if(!loadDpFile()){
                    eml_debug(1, "⚠️ Cannot load dataset parameters from file, trying to scan base data");
                    if (scan_base_data()){         // try to scan manually 
                        eml_debug(1, "✅ Base data scanned successfully");
                        dp_ok = true;
                    }
                }else dp_ok = true;
            }else{
                if(scan_base_data()){
                    eml_debug(2, "✅ Base data scanned successfully");
                    dp_ok = true;
                }
            }
            if(!dp_ok){
                eml_debug(1, "❌ Cannot load dataset parameters for configuration");
                return false;
            }
            // caculate lowest distribution 
            for(auto & count : samples_per_label) {
                if (count > 0) {
                    float pct = 100.0f * static_cast<float>(count) / static_cast<float>(num_samples);
                    if (pct < lowest_distribution) {
                        lowest_distribution = pct;
                    }
                }
            }
            
            // config session
            String jsonString = "";
            if(base_ptr->config_file_exists()){
                char file_path[RF_PATH_BUFFER];
                base_ptr->get_config_path(file_path);
                File file = RF_FS_OPEN(file_path, RF_FILE_READ);
                if (file) {
                    jsonString = file.readString();
                    file.close();
                    parseJSONConfig(jsonString);
                    validate_ratios();
                    generate_ranges();
                } else {
                    eml_debug(1, "⚠️ Failed to open config file: ", file_path);
                    enable_auto_config = true; // Fallback to auto-config
                }
            }else{
                eml_debug(1, "⚠️ No config file found, proceeding with auto-configuration");
                enable_auto_config = true; // Default to auto-config if no file
            }
            
            // Now decide loading strategy based on enable_auto_config
            if(enable_auto_config){
                eml_debug(1, "🔧 Auto-config enabled: generating settings from dataset parameters");
                auto_config();
            } 
            if constexpr (RF_DEBUG_LEVEL >1) print_config();
            isLoaded = true;
            return true;
        }
    
        // Save configuration to JSON file 
        bool releaseConfig() {
            if (!isLoaded || !has_base()){
                eml_debug(0, "❌ Save config failed: Config not loaded or base not ready");
                return false;
            }
            char file_path[RF_PATH_BUFFER];
            base_ptr->get_config_path(file_path);
            String existingTimestamp = "";
            String existingAuthor = "Viettran";
            
            if (RF_FS_EXISTS(file_path)) {
                File readFile = RF_FS_OPEN(file_path, RF_FILE_READ);
                if (readFile) {
                    String jsonContent = readFile.readString();
                    readFile.close();
                    existingTimestamp = extractStringValue(jsonContent, "timestamp");
                    existingAuthor = extractStringValue(jsonContent, "author");
                }
                RF_FS_REMOVE(file_path);
            }

            File file = RF_FS_OPEN(file_path, RF_FILE_WRITE);
            if (!file) {
                eml_debug(0, "❌ Failed to create config file: ", file_path);
                return false;
            }

            file.println("{");
            file.printf("  \"numTrees\": %d,\n", num_trees);
            file.printf("  \"randomSeed\": %d,\n", random_seed);
            file.printf("  \"train_ratio\": %.1f,\n", train_ratio);
            file.printf("  \"test_ratio\": %.2f,\n", test_ratio);
            file.printf("  \"valid_ratio\": %.2f,\n", valid_ratio);
            file.printf("  \"minSplit\": %d,\n", min_split);
            file.printf("  \"minLeaf\": %d,\n", min_leaf);
            file.printf("  \"maxDepth\": %d,\n", max_depth);
            file.printf("  \"useBootstrap\": %s,\n", use_boostrap ? "true" : "false");
            file.printf("  \"boostrapRatio\": %.3f,\n", boostrap_ratio);
            file.printf("  \"criterion\": \"%s\",\n", use_gini ? "gini" : "entropy");
            file.printf("  \"trainingScore\": \"%s\",\n", getTrainingScoreString(training_score).c_str());
            file.printf("  \"k_folds\": %d,\n", k_folds);
            file.printf("  \"impurityThreshold\": %.4f,\n", impurity_threshold);
            file.printf("  \"metric_score\": \"%s\",\n", getFlagString(metric_score).c_str());
            file.printf("  \"resultScore\": %.4f,\n", result_score);
            file.printf("  \"threshold_bits\": %d,\n", threshold_bits);
            file.printf("  \"feature_bits\": %d,\n", feature_bits);
            file.printf("  \"label_bits\": %d,\n", label_bits);
            file.printf("  \"child_bits\": %d,\n", child_bits);
            file.printf("  \"enableRetrain\": %s,\n", enable_retrain ? "true" : "false");
            file.printf("  \"enableAutoConfig\": %s,\n", enable_auto_config ? "true" : "false");
            file.printf("  \"max_samples\": %d,\n", max_samples);
            file.printf("  \"Estimated RAM (bytes)\": %d,\n", estimatedRAM);

            if (existingTimestamp.length() > 0) {
                file.printf("  \"timestamp\": \"%s\",\n", existingTimestamp.c_str());
            }
            if (existingAuthor.length() > 0) {
                file.printf("  \"author\": \"%s\"\n", existingAuthor.c_str());
            } else {
                file.seek(file.position() - 2); // Go back to remove ",\n"
                file.println("");
            }
            
            file.println("}");
            file.close();
            releaseDpFile();
            isLoaded = false;
            base_ptr->set_config_status(true);
            eml_debug(1, "✅ Configuration saved to: ", file_path);
            return true;
        }

        void purgeConfig() {
            isLoaded = false;
        }

    private:
        // Simple JSON parser for configuration
        void parseJSONConfig(const String& jsonStr) {
            // Use the actual keys from digit_data_config.json
            num_trees = extractIntValue(jsonStr, "numTrees");              
            random_seed = extractIntValue(jsonStr, "randomSeed");          
            min_split = extractIntValue(jsonStr, "minSplit");             
            min_leaf = extractIntValue(jsonStr, "minLeaf");
            if (min_leaf == 0) {
                min_leaf = 1;
            }
            max_depth = extractIntValue(jsonStr, "maxDepth");            
            use_boostrap = extractBoolValue(jsonStr, "useBootstrap");     
            boostrap_ratio = extractFloatValue(jsonStr, "boostrapRatio"); 
            
            String criterion = extractStringValue(jsonStr, "criterion");
            use_gini = (criterion == "gini");  // true for "gini", false for "entropy"
            
            k_folds = extractIntValue(jsonStr, "k_folds");                  
            impurity_threshold = extractFloatValue(jsonStr, "impurityThreshold");
            train_ratio = extractFloatValue(jsonStr, "train_ratio");      
            test_ratio = extractFloatValue(jsonStr, "test_ratio");        
            valid_ratio = extractFloatValue(jsonStr, "valid_ratio");     
            training_score = parseTrainingScore(extractStringValue(jsonStr, "trainingScore")); 
            metric_score = parseFlagValue(extractStringValue(jsonStr, "metric_score"));
            enable_retrain = extractBoolValue(jsonStr, "enableRetrain");
            enable_auto_config = extractBoolValue(jsonStr, "enableAutoConfig");
            result_score = extractFloatValue(jsonStr, "resultScore");      
            estimatedRAM = extractIntValue(jsonStr, "Estimated RAM (bytes)");
            
            // Load layout bits if available (from PC-trained model)
            threshold_bits = extractIntValue(jsonStr, "threshold_bits");
            feature_bits = extractIntValue(jsonStr, "feature_bits");
            label_bits = extractIntValue(jsonStr, "label_bits");
            child_bits = extractIntValue(jsonStr, "child_bits");
            max_samples = extractIntValue(jsonStr, "max_samples");

            if(num_trees == 1){     // decision tree mode 
                use_boostrap = false;
                boostrap_ratio = 1.0f; // disable bootstrap for single tree
                if(training_score == OOB_SCORE){
                    training_score = VALID_SCORE; // use validation score for single tree
                }
            }
        }

        // Convert flag string to uint8_t
        uint8_t parseFlagValue(const String& flagStr) {
            if (flagStr == "ACCURACY") return 0x01;
            if (flagStr == "PRECISION") return 0x02;
            if (flagStr == "RECALL") return 0x04;
            if (flagStr == "F1_SCORE") return 0x08;
            return 0x01; // Default to ACCURACY
        }

        // Convert uint8_t flag to string
        String getFlagString(uint8_t flag) const {
            switch(flag) {
                case 0x01: return "ACCURACY";
                case 0x02: return "PRECISION";
                case 0x04: return "RECALL";
                case 0x08: return "F1_SCORE";
                default: return "ACCURACY";
            }
        }

        // Convert string to Rf_training_score enum
        Rf_training_score parseTrainingScore(const String& scoreStr) {
            if (scoreStr == "oob_score") return OOB_SCORE;
            if (scoreStr == "valid_score") return VALID_SCORE;
            if (scoreStr == "k_fold_score") return K_FOLD_SCORE;
            return OOB_SCORE; 
        }

        // Convert Rf_training_score enum to string
        String getTrainingScoreString(Rf_training_score score) const {
            switch(score) {
                case OOB_SCORE: return "oob_score";
                case VALID_SCORE: return "valid_score";
                case K_FOLD_SCORE: return "k_fold_score";
                default: return "oob_score";
            }
        }

        uint32_t extractIntValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return 0;
            
            int colonIndex = json.indexOf(": ", keyIndex);
            if (colonIndex == -1) return 0;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.toInt();
        }

} // namespace eml
