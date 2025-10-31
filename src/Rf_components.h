#pragma once

#include "STL_MCU.h"  
#include "Rf_file_manager.h"
#include "esp_system.h"
#include <cstdlib>
#include <cstring>


#define GET_CURRENT_TIME_IN_MICROSECONDS() micros() // current time in microseconds
#define GET_CURRENT_TIME_IN_MILLISECONDS millis()// current time in milliseconds
#define GET_CURRENT_TIME() millis()

#ifdef DEV_STAGE
    #define ENABLE_TEST_DATA 1
#else
    #define ENABLE_TEST_DATA 0
#endif

#ifndef RF_ENABLE_TRAINING
    #define RF_ENABLE_TRAINING 1
#endif

#ifdef DISABLE_TRAINING
    #undef RF_ENABLE_TRAINING
    #define RF_ENABLE_TRAINING 0
#endif

using label_type    = uint8_t;  // type for label related operations
using sample_type   = uint16_t; // type for sample related operations

static constexpr uint8_t      RF_MAX_LABEL_LENGTH    = 32;     // max label length
static constexpr uint8_t      RF_PATH_BUFFER         = 64;     // buffer for file_path(limit to 2 level of file)
static constexpr uint8_t      RF_MAX_TREES           = 100;    // maximum number of trees in a forest
static constexpr label_type   RF_MAX_LABELS          = 255;    // maximum number of unique labels supported 
static constexpr uint16_t     RF_MAX_FEATURES        = 1023;   // maximum number of features
static constexpr uint16_t     RF_MAX_NODES           = 2047;   // Maximum nodes per tree 
static constexpr sample_type  RF_MAX_SAMPLES         = 65535;  // maximum number of samples in a dataset
#ifndef RF_USE_SDCARD 
static constexpr size_t       RF_MAX_DATASET_SIZE    = 150000; // Max dataset file size - 150kB
#elif defined(RF_PSRAM_AVAILABLE)       // allow larger dataset if PSRAM is enabled and using SD card
static constexpr size_t       RF_MAX_DATASET_SIZE    = 20000000; // Max dataset file size - 20MB for SD card
#endif

// define error label base on label_type
template<typename T>
struct Rf_err_label {
    static constexpr T value = static_cast<T>(~static_cast<T>(0));
};

static constexpr label_type RF_ERROR_LABEL = Rf_err_label<label_type>::value; 

/*
 NOTE : Forest file components (with each model)
    1. model_name_nml.bin       : base data (dataset)
    2. model_name_config.json   : model configuration file 
    3. model_name_ctg.csv       : quantizer (feature quantizer and label mapping)
    4. model_name_dp.csv        : information about dataset (num_features, num_labels...)
    5. model_name_forest.bin    : model file (all trees) in unified format
    6. model_name_tree_*.bin    : model files (tree files) in individual format. (Given from pc/use during training)
    7. model_name_node_pred.bin : node predictor file 
    8. model_name_node_log.csv  : node splitting log file during training (for retraining node predictor)
    9. model_name_infer_log.bin : inference log file (predictions, actual labels, metrics over time)
    10. model_name_time_log.csv     : time log file (detailed timing of forest events)
    11. model_name_memory_log.csv   : memory log file (detailed memory usage of forest events)
*/

namespace mcu {
    /*
    ------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ RF_COMPONENTS ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    // struct Rf_sample;           // single data sample
    // struct Tree_node;           // single tree node
    // ...

    class Rf_data;              // dataset object
    class Rf_tree;              // decision tree
    class Rf_config;            // forest configuration & dataset parameters
    class Rf_base;              // Manage and monitor the status of forest components and resources
    class Rf_node_predictor;    // estimate node per tree based on dataset & config.
    class Rf_quantizer;         // sample quantizer (quantize features and labels mapping)
    class Rf_logger;            // logging forest events timing, memory usage, messages, errors
    class Rf_random;            // random generator (for stability across platforms and runs)
    class Rf_matrix_score;      // confusion matrix and metrics calculator
    class Rf_tree_container;    // manages all trees at forest level
    class Rf_pending_data;      // manage pending data waiting for true labels from feedback action

    // enum Rf_metric_scores;      // flags for training process/score calculation (accuracy, precision, recall, f1_score)
    // enum Rf_training_score;     // score types for training process (oob, validation, k-fold)
    // ...

    static void buildThresholdCandidates(uint8_t bits, b_vector<uint16_t>& out) {
        out.clear();
        // santize bits input
        if (bits < 1) bits = 1;
        if (bits > 8) bits = 8;

        if (bits <= 1) {
            out.push_back(0);
            return;
        }
        if (bits == 2) {
            out.push_back(0);
            out.push_back(1);
            out.push_back(2);
            return;
        }

        uint16_t maxValue = static_cast<uint16_t>((1u << bits) - 1);
        uint16_t availableOdd = maxValue / 2;
        if (availableOdd == 0) {
            out.push_back(maxValue ? (maxValue - 1) : 0);
            return;
        }

        uint16_t desired = min<uint16_t>(8, availableOdd);
        for (uint16_t i = 0; i < desired; ++i) {
            uint32_t numerator = static_cast<uint32_t>(2 * i + 1) * static_cast<uint32_t>(availableOdd);
            uint16_t oddIndex = static_cast<uint16_t>(numerator / (2 * desired));
            if (oddIndex >= availableOdd) oddIndex = availableOdd - 1;
            uint16_t threshold = static_cast<uint16_t>(2 * oddIndex + 1);
            if (threshold >= maxValue) {
                threshold = static_cast<uint16_t>(maxValue - 1);
            }
            if (!out.empty() && threshold <= out.back()) {
                uint16_t candidate = static_cast<uint16_t>(out.back() + 2);
                if (candidate >= maxValue) candidate = static_cast<uint16_t>(maxValue - 1);
                threshold = candidate;
            }
            out.push_back(threshold);
        }
    }

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_BASE -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // flags indicating the status of member files
    typedef enum Rf_base_flags : uint16_t{
        BASE_DATA_EXIST         = 1 << 0,
        DP_FILE_EXIST           = 1 << 1,
        CTG_FILE_EXIST          = 1 << 2,
        CONFIG_FILE_EXIST       = 1 << 3,
        INFER_LOG_FILE_EXIST    = 1 << 4,
        UNIFIED_FOREST_EXIST    = 1 << 5,
        NODE_PRED_FILE_EXIST    = 1 << 6,
        ABLE_TO_INFERENCE       = 1 << 7,
        ABLE_TO_TRAINING        = 1 << 8,
        BASE_DATA_IS_CSV        = 1 << 9,
        SCANNED                 = 1 << 10
    } Rf_base_flags;

    // Base file management class for Random Forest project status
    class Rf_base {
    private:
        mutable uint16_t flags = 0; // flags indicating the status of member files
        char model_name[RF_PATH_BUFFER] ={0};
        
    public:
        // Helper to build file paths: buffer must be at least RF_PATH_BUFFER size
        // update: add parent folder : /model_name/model_name_suffix
        inline void build_file_path(char* buffer, const char* suffix, int buffer_size = RF_PATH_BUFFER) const {
            if (!buffer || buffer_size <= 0) return;
            if (buffer_size > RF_PATH_BUFFER) buffer_size = RF_PATH_BUFFER;
            snprintf(buffer, buffer_size, "/%s/%s%s", model_name, model_name, suffix);
        }

        Rf_base() : flags(static_cast<Rf_base_flags>(0)) {}
        Rf_base(const char* bn) : flags(static_cast<Rf_base_flags>(0)) {
            init(bn);
        }

    private:
        void scan_current_resource() {
            char filepath[RF_PATH_BUFFER];
            // check : base data exists (binary or csv)
            build_file_path(filepath, "_nml.bin");
            if (!RF_FS_EXISTS(filepath)) {
                // try to find csv file
                build_file_path(filepath, "_nml.csv");
                if (RF_FS_EXISTS(filepath)) {
                    RF_DEBUG(1, "🔄 Found csv dataset, need to be converted to binary format before use.");
                    flags |= static_cast<Rf_base_flags>(BASE_DATA_IS_CSV);
                }else{
                    RF_DEBUG(0, "❌ No base data file found: ", filepath);
                    this->model_name[0] = '\0';
                    return;
                }
            } else {
                RF_DEBUG(1, "✅ Found base data file: ", filepath);
                flags |= static_cast<Rf_base_flags>(BASE_DATA_EXIST);
            }

            // check : quantizer file exists
            build_file_path(filepath, "_ctg.csv");
            if (RF_FS_EXISTS(filepath)) {
                RF_DEBUG(1, "✅ Found quantizer file: ", filepath);
                flags |= static_cast<Rf_base_flags>(CTG_FILE_EXIST);
            } else {
                RF_DEBUG(0, "❌ No quantizer file found: ", filepath);
                this->model_name[0] = '\0';
                return;
            }
            
            // check : dp file exists
            build_file_path(filepath, "_dp.csv");
            if (RF_FS_EXISTS(filepath)) {
                RF_DEBUG(1, "✅ Found data_params file: ", filepath);
                flags |= static_cast<Rf_base_flags>(DP_FILE_EXIST);
            } else {
                RF_DEBUG(1, "⚠️ No data_params file found: ", filepath);
                RF_DEBUG(1, "🔂 Dataset will be scanned, which may take time...🕒");
            }

            // check : config file exists
            build_file_path(filepath, "_config.json");
            if (RF_FS_EXISTS(filepath)) {
                RF_DEBUG(1, "✅ Found config file: ", filepath);
                flags |= static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            } else {
                RF_DEBUG(1, "⚠️ No config file found: ", filepath);
                RF_DEBUG(1, "🔂 Switching to manual configuration");
            }
            
            // check : forest file exists (unified form)
            build_file_path(filepath, "_forest.bin");
            if (RF_FS_EXISTS(filepath)) {
                RF_DEBUG(1, "✅ Found unified forest model file: ", filepath);
                flags |= static_cast<Rf_base_flags>(UNIFIED_FOREST_EXIST);
            } else {
                RF_DEBUG(2, "⚠️ No unified forest model file found");
            }

            // check : node predictor file exists
            build_file_path(filepath, "_node_pred.bin");
            if (RF_FS_EXISTS(filepath)) {
                RF_DEBUG(1, "✅ Found node predictor file: ", filepath);
                flags |= static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            } else {
                RF_DEBUG(2, "⚠️ No node predictor file found: ", filepath);
                RF_DEBUG(2, "🔂 Switching to use default node_predictor");
            }

            // able to inference : forest file + quantizer
            if ((flags & UNIFIED_FOREST_EXIST) && (flags & CTG_FILE_EXIST)) {
                flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                RF_DEBUG(1, "✅ Model is ready for inference.");
            } else {
                RF_DEBUG(0, "⚠️ Model is NOT ready for inference.");
            }

            // able to re-training : base data + quantizer 
            if ((flags & BASE_DATA_EXIST) && (flags & CTG_FILE_EXIST)) {
                flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                RF_DEBUG(1, "✅ Model is ready for re-training.");
            } else {
                RF_DEBUG(0, "⚠️ Model is NOT ready for re-training.");
            }
            flags |= static_cast<Rf_base_flags>(SCANNED);
        }
        
    public:
        void init(const char* name) {
            RF_DEBUG(1, "🔧 Initializing model resource manager");
            if (!name || strlen(name) == 0) {
                RF_DEBUG(0, "❌ Model name is empty. The process is aborted.");
                return;
            }
            strncpy(this->model_name, name, RF_PATH_BUFFER - 1);
            this->model_name[RF_PATH_BUFFER - 1] = '\0';
            scan_current_resource();
        }

        void update_resource_status() {
            RF_DEBUG(1, "🔄 Updating model resource status");
            if (this->model_name[0] == '\0') {
                RF_DEBUG(0, "❌ Model name is empty. Cannot update resource status.");
                return;
            }
            flags = 0; 
            scan_current_resource();
        }

        //  operator =
        Rf_base& operator=(const Rf_base& other) {
            if (this != &other) {
                this->flags = other.flags;
                strncpy(this->model_name, other.model_name, RF_PATH_BUFFER - 1);
                this->model_name[RF_PATH_BUFFER - 1] = '\0';
            }
            return *this;
        }

        // copy constructor
        Rf_base(const Rf_base& other) {
            this->flags = other.flags;
            strncpy(this->model_name, other.model_name, RF_PATH_BUFFER - 1);
            this->model_name[RF_PATH_BUFFER - 1] = '\0';
        }

        // Get model name 
        inline void get_model_name(char* buffer, size_t bufferSize) const {
            if (buffer && bufferSize > 0) {
                strncpy(buffer, model_name, bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
            }
        }

        // build tree file path : /model_name/tree_<index>.bin
        void build_tree_file_path(char* buffer, uint8_t tree_index, int buffer_size = RF_PATH_BUFFER) const {
            if (!buffer || buffer_size <= 0) return;
            if (buffer_size > RF_PATH_BUFFER) buffer_size = RF_PATH_BUFFER;
            snprintf(buffer, buffer_size, "/%s/tree_%d.bin", model_name, tree_index);
        }

        // File path getters 
        inline void get_base_data_path(char* buffer, int buffer_size = RF_PATH_BUFFER )  
                        const  { build_file_path(buffer, "_nml.bin", buffer_size); }

        inline void get_dp_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_dp.csv", buffer_size); }

        inline void get_ctg_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_ctg.csv", buffer_size); }

        inline void get_infer_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_infer_log.bin", buffer_size); }

        inline void get_config_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_config.json", buffer_size); }

        inline void get_node_pred_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_node_pred.bin", buffer_size); }

        inline void get_node_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_node_log.csv", buffer_size); }

        inline void get_forest_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_forest.bin", buffer_size); }

        inline void get_time_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_time_log.csv", buffer_size); }

        inline void get_memory_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_memory_log.csv", buffer_size); }
        
        inline void get_temp_base_data_path(char* buffer, int buffer_size = RF_PATH_BUFFER)
                        const { build_file_path(buffer, "_cpy.bin", buffer_size); }

        // status checkers
        inline bool ready_to_use()          const { return (model_name[0] != '\0') && (flags & SCANNED); }
        inline bool dp_file_exists()        const { return (flags & DP_FILE_EXIST) != 0; }
        inline bool config_file_exists()    const { return (flags & CONFIG_FILE_EXIST) != 0; }
        inline bool node_pred_file_exists() const { return (flags & NODE_PRED_FILE_EXIST) != 0;}
        inline bool base_data_is_csv()      const { return (flags & BASE_DATA_IS_CSV) != 0; }
        inline bool forest_file_exist()     const { return (flags & UNIFIED_FOREST_EXIST) != 0; }
        inline bool able_to_training()      const { return (flags & ABLE_TO_TRAINING) != 0; }
        inline bool able_to_inference()     const { return (flags & ABLE_TO_INFERENCE) != 0; }

        // setters
        void set_model_name(const char* bn) {
            char old_model_name[RF_PATH_BUFFER];
            strncpy(old_model_name, model_name, RF_PATH_BUFFER - 1);
            old_model_name[RF_PATH_BUFFER - 1] = '\0';
            
            if (bn && strlen(bn) > 0) {
                strncpy(model_name, bn, RF_PATH_BUFFER - 1);
                model_name[RF_PATH_BUFFER - 1] = '\0';

                // find and rename all existing related files
                char old_file[RF_PATH_BUFFER], new_file[RF_PATH_BUFFER];
                auto rename_file = [&](const char* suffix) {
                    snprintf(old_file, RF_PATH_BUFFER, "/%s%s", old_model_name, suffix);
                    snprintf(new_file, RF_PATH_BUFFER, "/%s%s", model_name, suffix);
                    if (RF_FS_EXISTS(old_file)) {
                        cloneFile(old_file, new_file);
                        RF_FS_REMOVE(old_file);
                    }
                };

                // Rename all model files
                rename_file("_nml.bin");       // base file
                rename_file("_dp.csv");        // data_params file
                rename_file("_ctg.csv");       // quantizer file
                rename_file("_infer_log.bin"); // inference log file
                rename_file("_node_pred.bin"); // node predictor file
                rename_file("_node_log.bin");  // node predict log file
                rename_file("_config.json");   // config file
                rename_file("_memory_log.csv");// memory log file
                rename_file("_time_log.csv");  // time log file

                // tree files - handle both individual and unified formats
                snprintf(old_file, RF_PATH_BUFFER, "/%s_forest.bin", old_model_name);
                snprintf(new_file, RF_PATH_BUFFER, "/%s_forest.bin", model_name);
                
                if (RF_FS_EXISTS(old_file)) {
                    // Handle unified model format
                    cloneFile(old_file, new_file);
                    RF_FS_REMOVE(old_file);
                } else {
                    // Handle individual tree files
                    for(uint8_t i = 0; i < RF_MAX_TREES; i++) { // Max 50 trees check
                        snprintf(old_file, RF_PATH_BUFFER, "/%s_tree_%d.bin", old_model_name, i);
                        snprintf(new_file, RF_PATH_BUFFER, "/%s_tree_%d.bin", model_name, i);
                        if (RF_FS_EXISTS(old_file)) {
                            cloneFile(old_file, new_file);
                            RF_FS_REMOVE(old_file);
                        }else{
                            break; // Stop when we find a missing tree file
                        }
                    }
                }
                // Re-initialize flags based on new base name
                scan_current_resource();  
            }
        }

        bool set_config_status(bool exists) const {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            }
            return config_file_exists();
        }

        bool set_dp_status(bool exists) const {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(DP_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(DP_FILE_EXIST);
            }
            return dp_file_exists();
        }

        bool set_node_pred_status(bool exists) const {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            }
            return (flags & NODE_PRED_FILE_EXIST) != 0;
        }

        size_t memory_usage() const {
            return sizeof(Rf_base); 
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_CONFIG ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef enum Rf_metric_scores : uint8_t{
        ACCURACY    = 0x01,          // calculate accuracy of the model
        PRECISION   = 0x02,          // calculate precision of the model
        RECALL      = 0x04,          // calculate recall of the model
        F1_SCORE    = 0x08           // calculate F1 score of the model
    }Rf_metric_scores;


    typedef enum Rf_training_score : uint8_t {
        OOB_SCORE    = 0x00,   // default 
        VALID_SCORE  = 0x01,
        K_FOLD_SCORE = 0x02
    } Rf_training_score;

    // Configuration class : model configuration and dataset parameters
    // handle 2 files: model_name_config.json (config file) and model_name_dp.csv (dp file)
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

        bool extend_base_data;
        bool enable_retrain;
        bool enable_auto_config;   // change config based on dataset parameters (when base_data expands)

        // runtime parameters
        pair<uint8_t, uint8_t> min_split_range;
        pair<uint8_t, uint8_t> max_depth_range; 

        // Dataset parameters 
        sample_type num_samples;
        uint16_t num_features;
        label_type  num_labels;
        uint8_t  quantization_coefficient; // Bits per feature value (1-8)
        float lowest_distribution; 
        b_vector<sample_type> samples_per_label; // index = label, value = count

        void init(Rf_base* base) {
            base_ptr = base;
            isLoaded = false;

            // Set default values
            num_trees           = 20;
            random_seed         = 37;
            min_split           = 2;
            max_depth           = 13;
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
            extend_base_data    = true;
            enable_retrain      = true;
            enable_auto_config  = false;
            quantization_coefficient = 2; // Default 2 bits per value
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
            RF_DEBUG(1, "📊 Scanning base data: ", base_file_path);

            File file = RF_FS_OPEN(base_file_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open base data file for scanning: ", base_file_path);
                return false;
            }

            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
               file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read dataset header during scan", base_file_path);
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
            unordered_map<label_type, sample_type> label_counts;
            label_type max_label = 0;

            // Scan through all samples to collect label statistics
            for(sample_type i = 0; i < numSamples; i++) {
                label_type label;
                if(file.read(&label, sizeof(label)) != sizeof(label)) {
                    RF_DEBUG_2(0, "❌ Failed to read label of sample", i, ": ", base_file_path);
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
                    RF_DEBUG_2(0, "❌ Failed to skip features of sample", i, ": ", base_file_path);
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

            RF_DEBUG(1, "✅ Base data scan complete.");
            RF_DEBUG(1, "   📊 Samples: ", num_samples);
            RF_DEBUG(1, "   🔢 Features: ", num_features);
            RF_DEBUG(1, "   🏷️ Labels: ", num_labels);
            RF_DEBUG(1, "   📈 Samples per label: ");
            for (size_t i = 0; i < samples_per_label.size(); i++) {
                if(samples_per_label[i] > 0) {
                    RF_DEBUG_2(1, "   Lable ", i, ": ", samples_per_label[i]);
                }
            }
            return true;
        }
        
        // generate optimal ranges for min_split and max_depth based on dataset parameters
        void generate_ranges(){
            // Calculate optimal min_split, max_depth and ranges
            int baseline_minsplit_ratio = 100 * (num_samples / 500 + 1); 
            if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
            uint8_t min_minSplit = max(2, (int)(num_samples / baseline_minsplit_ratio) - 2);
            int dynamic_max_split = min(min_minSplit + 6, (int)(log2(num_samples) / 4 + num_features / 25.0f));
            uint8_t max_minSplit = min(24, dynamic_max_split) - 2; // Cap at 24 to prevent overly simple trees.
            if (max_minSplit <= min_minSplit) max_minSplit = min_minSplit + 4; // Ensure a valid range.


            int base_maxDepth = max((int)log2(num_samples * 2.0f), (int)(log2(num_features) * 2.5f));
            uint8_t max_maxDepth = max(6, base_maxDepth);
            int dynamic_min_depth = max(4, (int)(log2(num_features) + 2));
            uint8_t min_maxDepth = min((int)max_maxDepth - 2, dynamic_min_depth); // Ensure a valid range.
            if (min_maxDepth >= max_maxDepth) min_maxDepth = max_maxDepth - 2;
            if (min_maxDepth < 4) min_maxDepth = 4;

            if(min_split == 0 || max_depth == 0) {
                min_split = (min_minSplit + max_minSplit) / 2;
                max_depth = (min_maxDepth + max_maxDepth) / 2;
                RF_DEBUG_2(1, "Setting minSplit to ", min_split, "and maxDepth to ", max_depth);
            }

            RF_DEBUG_2(1, "⚙️ Setting minSplit range: ", min_minSplit, "to ", max_minSplit);
            RF_DEBUG_2(1, "⚙️ Setting maxDepth range: ", min_maxDepth, "to ", max_maxDepth);

            min_split_range = make_pair(min_minSplit, max_minSplit);
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

            double sample_factor_d = min(2.0, 0.75 + log2(max(2.0, static_cast<double>(num_samples))) / 12.0);
            float sample_factor = static_cast<float>(sample_factor_d);
            // Imbalance factor: reduce threshold for imbalanced data to allow splitting on rare classes
            float imbalance_factor = 1.0f - 0.5f * imbalance; // 0.5..1.0
            // Feature factor: with many features, weak splits are common; require slightly higher gain
            float feature_factor = 0.9f + 0.1f * min(1.0f, static_cast<float>(log2(max(2, static_cast<int>(num_features)))) / 8.0f);

            if (use_gini) {
                float max_gini = 1.0f - 1.0f / static_cast<float>(K);
                float base = 0.003f * max_gini; // very small base for Gini
                float thr = base * sample_factor * imbalance_factor * feature_factor;
                impurity_threshold = max(0.0005f, min(0.02f, thr));
            } else { // entropy
                float max_entropy = log2(static_cast<float>(K));
                float base = 0.02f * (max_entropy > 0.0f ? max_entropy : 1.0f); // larger than gini
                float thr = base * sample_factor * imbalance_factor * feature_factor;
                impurity_threshold = max(0.005f, min(0.2f, thr));
            }
            RF_DEBUG(1, "⚙️ Setting impurity_threshold to ", impurity_threshold);
        }
        
        // setup config manually (when no config file)
        void auto_config(){
            // set metric_score based on dataset balance
            if(samples_per_label.size() > 0){
                sample_type minorityCount = num_samples;
                sample_type majorityCount = 0;

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
                    RF_DEBUG_2(1, "⚠️ Highly imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to RECALL.", "");
                } else if (maxImbalanceRatio > 3.0f) {
                    metric_score = Rf_metric_scores::F1_SCORE;
                    RF_DEBUG_2(1, "⚠️ Moderately imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to F1_SCORE.", "");
                } else if (maxImbalanceRatio > 1.5f) {
                    metric_score = Rf_metric_scores::PRECISION;
                    RF_DEBUG_2(1, "⚠️ Slightly imbalanced dataset: ", maxImbalanceRatio, "Setting metric_score to PRECISION.", "");
                } else {
                    metric_score = Rf_metric_scores::ACCURACY;
                    RF_DEBUG_2(1, "✅ Balanced dataset (ratio: ", maxImbalanceRatio, "). Setting metric_score to ACCURACY.", "");
                }
            }

            sample_type avg_samples_per_label = num_samples / max(1, static_cast<int>(num_labels));
        
            // set training_score method
            if (avg_samples_per_label < 200){
                training_score = K_FOLD_SCORE;
            }else if (avg_samples_per_label < 500){
                training_score = OOB_SCORE;
            }else{
                training_score = VALID_SCORE;
            }
            validate_ratios();
            generate_ranges();
            generate_impurity_threshold(); // no prior distribution info
        }
        
        // read dataset parameters from /dataset_dp.csv and write to config
        bool loadDpFile() {
            char path[RF_PATH_BUFFER];
            base_ptr->get_dp_path(path);
            if (strlen(path) < 1) { 
                RF_DEBUG(0, "❌ load dp file failed: ", "dp path is empty");
                return false; 
            }
            File file = RF_FS_OPEN(path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data_params file for reading", path);
                return false;
            }

            // Skip header line
            file.readStringUntil('\n');

            // Initialize variables with defaults
            sample_type numSamples = 0;
            uint16_t numFeatures = 0;
            label_type numLabels = 0;
            uint8_t quantCoeff = 2;  // Default 2 bits
            unordered_map<label_type, sample_type> labelCounts; // label -> count
            uint8_t maxFeatureValue = 3;    // Default for 2-bit quantized data

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
                } else if (parameter == "max_feature_value") {
                    maxFeatureValue = value.toInt();
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
            for (label_type i = 0; i < numLabels; i++) {
                if (labelCounts.find(i) != labelCounts.end()) {
                    samples_per_label[i] = labelCounts[i];
                }
            }

            // Validate loaded parameters
            if (num_features == 0 || num_samples == 0 || num_labels == 0) {
                RF_DEBUG(0, "❌ Invalid dataset parameters in dp file", path);
                return false;
            }
            if (! validateSamplesPerLabel()) RF_DEBUG(1, "⚠️ samples_per_label data inconsistency detected");
            return true;
        }
        
        // write back to dataset_params file
        bool releaseDpFile() {
            char path[RF_PATH_BUFFER];
            base_ptr->get_dp_path(path);
            if (path[0] == '\0') return false;
            File file = RF_FS_OPEN(path, RF_FILE_WRITE);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data_params file for writing", path);
                return false;
            }
            file.println("parameter,value");
            file.printf("quantization_coefficient,%u\n", quantization_coefficient);
            
            label_type max_value = (quantization_coefficient >= 8) ? RF_ERROR_LABEL : ((1u << quantization_coefficient) - 1);
            uint8_t features_per_byte = (quantization_coefficient == 0) ? 0 : (8 / quantization_coefficient);
            
            file.printf("max_feature_value,%u\n", max_value);
            file.printf("features_per_byte,%u\n", features_per_byte);
            
            file.printf("num_features,%u\n", num_features);
            file.printf("num_samples,%u\n", num_samples);
            file.printf("num_labels,%u\n", num_labels);
        
            // Write actual label counts from samples_per_label vector
            for (label_type i = 0; i < samples_per_label.size(); i++) {
                file.printf("samples_label_%u,%u\n", i, samples_per_label[i]);
            }
            
            file.close();   
            base_ptr->set_dp_status(true);
            RF_DEBUG(1, "✅ Dataset parameters saved: ", path);
            return true;
        }

    public:
        // Load configuration from JSON file
        bool loadConfig() {
            if (isLoaded) return true;
            if (!has_base()) {
                RF_DEBUG(0, "❌ Base pointer is null or base not ready", "load config");
                return false;
            }

            // load dataset parameters session 
            bool dp_ok =  false;
            if(base_ptr->dp_file_exists()){
                if(!loadDpFile()){
                    RF_DEBUG(1, "⚠️ Cannot load dataset parameters from file, trying to scan base data");
                    if (scan_base_data()){         // try to scan manually 
                        RF_DEBUG(1, "✅ Base data scanned successfully");
                        dp_ok = true;
                    }
                }else dp_ok = true;
            }else{
                if(scan_base_data()){
                    RF_DEBUG(2, "✅ Base data scanned successfully");
                    dp_ok = true;
                }
            }
            if(!dp_ok){
                RF_DEBUG(1, "❌ Cannot load dataset parameters for configuration");
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
                    RF_DEBUG(1, "⚠️ Failed to open config file: ", file_path);
                    enable_auto_config = true; // Fallback to auto-config
                }
            }else{
                RF_DEBUG(1, "⚠️ No config file found, proceeding with auto-configuration");
                enable_auto_config = true; // Default to auto-config if no file
            }
            
            // Now decide loading strategy based on enable_auto_config
            if(enable_auto_config){
                RF_DEBUG(1, "🔧 Auto-config enabled: generating settings from dataset parameters");
                auto_config();
            } 
            if (!extend_base_data){
                enable_auto_config = false; // disable auto config if not extending base dataset
            }
            if constexpr (RF_DEBUG_LEVEL >1) print_config();
            isLoaded = true;
            return true;
        }
    
        // Save configuration to JSON file 
        bool releaseConfig() {
            if (!isLoaded || !has_base()){
                RF_DEBUG(0, "❌ Save config failed: Config not loaded or base not ready");
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
                RF_DEBUG(0, "❌ Failed to create config file: ", file_path);
                return false;
            }

            file.println("{");
            file.printf("  \"numTrees\": %d,\n", num_trees);
            file.printf("  \"randomSeed\": %d,\n", random_seed);
            file.printf("  \"train_ratio\": %.1f,\n", train_ratio);
            file.printf("  \"test_ratio\": %.2f,\n", test_ratio);
            file.printf("  \"valid_ratio\": %.2f,\n", valid_ratio);
            file.printf("  \"minSplit\": %d,\n", min_split);
            file.printf("  \"maxDepth\": %d,\n", max_depth);
            file.printf("  \"useBootstrap\": %s,\n", use_boostrap ? "true" : "false");
            file.printf("  \"boostrapRatio\": %.3f,\n", boostrap_ratio);
            file.printf("  \"criterion\": \"%s\",\n", use_gini ? "gini" : "entropy");
            file.printf("  \"trainingScore\": \"%s\",\n", getTrainingScoreString(training_score).c_str());
            file.printf("  \"k_folds\": %d,\n", k_folds);
            file.printf("  \"impurityThreshold\": %.4f,\n", impurity_threshold);
            file.printf("  \"metric_score\": \"%s\",\n", getFlagString(metric_score).c_str());
            file.printf("  \"extendBaseData\": %s,\n", extend_base_data ? "true" : "false");
            file.printf("  \"enableRetrain\": %s,\n", enable_retrain ? "true" : "false");
            file.printf("  \"enableAutoConfig\": %s,\n", enable_auto_config ? "true" : "false");
            file.printf("  \"resultScore\": %.4f,\n", result_score);
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
            RF_DEBUG(1, "✅ Configuration saved to: ", file_path);
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
            extend_base_data = extractBoolValue(jsonStr, "extendBaseData");
            enable_retrain = extractBoolValue(jsonStr, "enableRetrain");
            enable_auto_config = extractBoolValue(jsonStr, "enableAutoConfig");
            result_score = extractFloatValue(jsonStr, "resultScore");      
            estimatedRAM = extractIntValue(jsonStr, "Estimated RAM (bytes)");
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

        float extractFloatValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return 0.0;
            
            int colonIndex = json.indexOf(": ", keyIndex);
            if (colonIndex == -1) return 0.0;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.toFloat();
        }

        bool extractBoolValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return false;
            
            int colonIndex = json.indexOf(": ", keyIndex);
            if (colonIndex == -1) return false;
            
            int commaIndex = json.indexOf(",", colonIndex);
            if (commaIndex == -1) commaIndex = json.indexOf("}", colonIndex);
            
            String valueStr = json.substring(colonIndex + 1, commaIndex);
            valueStr.trim();
            return valueStr.indexOf("true") != -1;
        }

        String extractStringValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return "";
            
            int colonIndex = json.indexOf(": ", keyIndex);
            if (colonIndex == -1) return "";
            
            int firstQuoteIndex = json.indexOf("\"", colonIndex);
            if (firstQuoteIndex == -1) return "";
            
            int secondQuoteIndex = json.indexOf("\"", firstQuoteIndex + 1);
            if (secondQuoteIndex == -1) return "";
            
            return json.substring(firstQuoteIndex + 1, secondQuoteIndex);
        }
        
        String getMetricScoreString() const {
            return getFlagString(metric_score);
        }
        
        public:
        bool use_validation() const {
            // return valid_ratio > 0.0f;
            return training_score == VALID_SCORE;
        }

        // Method to validate that samples_per_label data is consistent
        bool validateSamplesPerLabel() const {
            if (samples_per_label.size() != num_labels) {
                return false;
            }
            sample_type totalSamples = 0;
            for (sample_type count : samples_per_label) {
                totalSamples += count;
            }
            return totalSamples == num_samples;
        }
        
        // make sure train, test and valid ratios valid and optimal 
        void validate_ratios(){
            // sample_type avg_samples_per_label = num_samples / max(1, static_cast<int>(num_labels));
            sample_type rarest_class = RF_MAX_SAMPLES;
            for(auto & count : samples_per_label){
                if (count < rarest_class){
                    rarest_class = count;
                }
            }
            if(enable_auto_config){
                if(rarest_class < 150){
                    train_ratio = 0.6f;
                    test_ratio = 0.2f;
                    valid_ratio = 0.2f;
                }else{
                    train_ratio = 0.7f;
                    test_ratio = 0.15f;
                    valid_ratio = 0.15f;
                }
            }
            if (training_score != VALID_SCORE){
                train_ratio += valid_ratio;
                valid_ratio = 0.0f;
            }
            else {
                if (valid_ratio < 0.1f){
                    if(rarest_class < 150)  valid_ratio = 0.2f;
                    else                    valid_ratio = 0.15f;
                    train_ratio -= valid_ratio;
                }
            }
            if (!ENABLE_TEST_DATA){
                train_ratio += test_ratio;
                test_ratio = 0.0f;
            }
            // ensure ratios sum to 1.0
            float total_ratio = train_ratio + test_ratio + valid_ratio;
            if (total_ratio > 1.0f) {
                train_ratio /= total_ratio;
                test_ratio /= total_ratio;
                valid_ratio /= total_ratio;
            }
        }
            
        void print_config() const {
            RF_DEBUG(1, "🛠️ Model configuration: ");
            RF_DEBUG(1, "   - Trees: ", num_trees);
            RF_DEBUG(1, "   - Random seed: ", random_seed);
            RF_DEBUG(1, "   - max_depth: ", max_depth);
            RF_DEBUG(1, "   - min_split: ", min_split);
            RF_DEBUG(1, "   - train_ratio: ", train_ratio);
            RF_DEBUG(1, "   - test_ratio: ", test_ratio);
            RF_DEBUG(1, "   - valid_ratio: ", valid_ratio);
            RF_DEBUG(1, "   - use_bootstrap: ", use_boostrap ? "true" : "false");
            RF_DEBUG(1, "   - bootstrap_ratio: ", boostrap_ratio);
            RF_DEBUG(1, "   - criterion: ", use_gini ? "gini" : "entropy");
            RF_DEBUG(1, "   - k_folds: ", k_folds);
            RF_DEBUG(1, "   - impurity_threshold: ", impurity_threshold);
            RF_DEBUG(1, "   - training_score: ", getTrainingScoreString(training_score).c_str());
            RF_DEBUG(1, "   - metric_score: ", getFlagString(metric_score).c_str());
            RF_DEBUG(1, "   - extend_base_data: ", extend_base_data ? "true" : "false");
            RF_DEBUG(1, "   - enable_retrain: ", enable_retrain ? "true" : "false");
            RF_DEBUG(1, "   - enable_auto_config: ", enable_auto_config ? "true" : "false");

            RF_DEBUG(1, "📊 Dataset Parameters: ");
            RF_DEBUG(1, "   - Samples: ", num_samples);
            RF_DEBUG(1, "   - Features: ", num_features);
            RF_DEBUG(1, "   - Labels: ", num_labels);
            RF_DEBUG(1, "   - Samples per label: ");

            for (size_t i = 0; i < samples_per_label.size(); i++) {
                if(samples_per_label[i] > 0) {
                    RF_DEBUG_2(1, "   🏷️ Label ", i, ": ", samples_per_label[i]);
                }
            }
        }
        
        size_t memory_usage() const {
            size_t total = sizeof(Rf_config);
            total += 4;   
            total += samples_per_label.size() * sizeof(sample_type); 
            return total;
        }
        
    };
   
    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_DATA ------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    using sampleID_set = ID_vector<sample_type>;       // set of unique sample IDs

    // single data sample structure
    struct Rf_sample{
        packed_vector<8> features;          // features stored 
        label_type label;                   // label of the sample 

        Rf_sample() : features(), label(0) {}
        
        // construct from parent packed_vector in Rf_data
        template<uint8_t bpv>
        Rf_sample(label_type label, const packed_vector<bpv>& source, size_t start, size_t end){
            this->label = label;
            features = packed_vector<8>(source, start, end);
        }
        
        template<uint8_t bpv>
        Rf_sample(const packed_vector<bpv>& features, label_type label) : features(features), label(label) {}
    };
    
    class Rf_data {
    private:
        static constexpr size_t MAX_CHUNKS_SIZE = 8192; // max bytes per chunk (8kB)

        // Chunked packed storage - eliminates both heap overhead per sample and large contiguous allocations
        vector<packed_vector<8>> sampleChunks;  // Multiple chunks of packed features (up to 8 bits per value)
        packed_vector<8> allLabels;                  // Labels storage 
        uint16_t bitsPerSample;                    // Number of bits per sample (numFeatures * quantization_coefficient)
        sample_type samplesEachChunk;                  // Maximum samples per chunk
        size_t size_;  
        uint8_t quantization_coefficient;              // Bits per feature value (1-8)
        char file_path[RF_PATH_BUFFER] = {0};          // dataset file_path 

        uint8_t num_labels_2_bpv(label_type num_labels) {
            if (num_labels <= 2) return 1;
            else if (num_labels <= 4) return 2;
            else if (num_labels <= 16) return 4;
            else if (num_labels <= 256) return 8;
            else return 16; // up to 65536 labels
        }

    public:
        bool isLoaded;      

        Rf_data() : isLoaded(false), size_(0), bitsPerSample(0), samplesEachChunk(0), quantization_coefficient(2) {}

        Rf_data(const char* path, Rf_config& config){ 
            init(path, config);
        }

        bool init(const char* file_path, Rf_config& config) {
            strncpy(this->file_path, file_path, RF_PATH_BUFFER);
            this->file_path[RF_PATH_BUFFER - 1] = '\0';
            quantization_coefficient = config.quantization_coefficient;
            bitsPerSample = config.num_features * quantization_coefficient;
            uint8_t label_bpv = num_labels_2_bpv(config.num_labels);
            allLabels.set_bits_per_value(label_bpv);
            updateSamplesEachChunk();
            RF_DEBUG_2(1, "ℹ️ Rf_data initialized (", samplesEachChunk, "samples/chunk): ", file_path);
            isLoaded = false;
            size_ = config.num_samples;
            sampleChunks.clear();
            allLabels.clear();
            return isProperlyInitialized();
        }

        // Iterator class (returns Rf_sample by value for read-only querying)
        class iterator {
        private:
            Rf_data* data_;
            size_t index_;

        public:
            iterator(Rf_data* data, size_t index) : data_(data), index_(index) {}

            Rf_sample operator*() const {
                return data_->getSample(index_);
            }

            iterator& operator++() {
                ++index_;
                return *this;
            }

            iterator operator++(int) {
                iterator temp = *this;
                ++index_;
                return temp;
            }

            bool operator==(const iterator& other) const {
                return data_ == other.data_ && index_ == other.index_;
            }

            bool operator!=(const iterator& other) const {
                return !(*this == other);
            }
        };

        // Iterator support
        iterator begin() { return iterator(this, 0); }
        iterator end() { return iterator(this, size_); }

        // Array access operator (return by value; read-only usage in algorithms)
        Rf_sample operator[](size_t index) {
            return getSample(index);
        }

        // Const version of array access operator
        Rf_sample operator[](size_t index) const {
            return getSample(index);
        }

        // Validate that the Rf_data has been properly initialized
        bool isProperlyInitialized() const {
            // return bitsPerSample > 0 && samplesEachChunk > 0 && file_path[0] != '\0';
            return bitsPerSample > 0 && samplesEachChunk > 0;
        }
    private:
        // Calculate maximum samples per chunk based on bitsPerSample
        void updateSamplesEachChunk() {
            if (bitsPerSample > 0) {
                // Each sample needs bitsPerSample bits, MAX_CHUNKS_SIZE is in bytes (8 bits each)
                samplesEachChunk = (MAX_CHUNKS_SIZE * 8) / bitsPerSample;
                if (samplesEachChunk == 0) samplesEachChunk = 1; // At least 1 sample per chunk
            }
        }

        // Get chunk index and local index within chunk for a given sample index
        pair<size_t, size_t> getChunkLocation(size_t sampleIndex) const {
            size_t chunkIndex = sampleIndex / samplesEachChunk;
            size_t localIndex = sampleIndex % samplesEachChunk;
            return make_pair(chunkIndex, localIndex);
        }

        // Ensure we have enough chunks to store the given number of samples
        void ensureChunkCapacity(size_t totalSamples) {
            size_t requiredChunks = (totalSamples + samplesEachChunk - 1) / samplesEachChunk;
            while (sampleChunks.size() < requiredChunks) {
                packed_vector<8> newChunk;
                // Reserve space for elements (each element uses quantization_coefficient bits)
                size_t elementsPerSample = bitsPerSample / quantization_coefficient;  // numFeatures
                newChunk.set_bits_per_value(quantization_coefficient);
                newChunk.reserve(samplesEachChunk * elementsPerSample);
                sampleChunks.push_back(newChunk); // Add new empty chunk
            }
        }

        // Helper method to reconstruct Rf_sample from chunked packed storage
        Rf_sample getSample(size_t sampleIndex) const {
            if (!isLoaded) {
                RF_DEBUG(2, "❌ Rf_data not loaded. Call loadData() first.");
                return Rf_sample();
            }
            if(sampleIndex >= size_){
                RF_DEBUG_2(2, "❌ Sample index out of bounds: ", sampleIndex, "size: ", size_);
                return Rf_sample();
            }
            pair<size_t, size_t> location = getChunkLocation(sampleIndex);
            size_t numFeatures = bitsPerSample / quantization_coefficient;
            return Rf_sample(
                allLabels[sampleIndex],
                sampleChunks[location.first],
                location.second * numFeatures,
                (location.second + 1) * numFeatures
            );    
        }

        // Helper method to store Rf_sample in chunked packed storage
        bool storeSample(const Rf_sample& sample, size_t sampleIndex) {
            if (!isProperlyInitialized()) {
                RF_DEBUG(2, "❌ Store sample failed: Rf_data not properly initialized.");
                return false;
            }
            
            // Store label
            if (sampleIndex == allLabels.size()) {
                // Appending in order (fast path)
                allLabels.push_back(sample.label);
            } else if (sampleIndex < allLabels.size()) {
                // Overwrite existing position
                allLabels.set(sampleIndex, sample.label);
            } else {
                // Rare case: out-of-order insert; fill gaps with 0
                allLabels.resize(sampleIndex + 1, 0);
                allLabels.push_back(sample.label);
            }
            
            // Ensure we have enough chunks
            ensureChunkCapacity(sampleIndex + 1);
            
            auto location = getChunkLocation(sampleIndex);
            size_t chunkIndex = location.first;
            size_t localIndex = location.second;
            
            // Store features in packed format within the specific chunk
            size_t elementsPerSample = bitsPerSample / quantization_coefficient;  // numFeatures
            size_t startElementIndex = localIndex * elementsPerSample;
            size_t requiredSizeInChunk = startElementIndex + elementsPerSample;
            
            if (sampleChunks[chunkIndex].size() < requiredSizeInChunk) {
                sampleChunks[chunkIndex].resize(requiredSizeInChunk);
            }
            
            // Store each feature as one element in the packed_vector (with variable bpv)
            for (size_t featureIdx = 0; featureIdx < sample.features.size(); featureIdx++) {
                size_t elementIndex = startElementIndex + featureIdx;
                uint8_t featureValue = sample.features[featureIdx];
                
                // Store value directly as one element (bpv determined by quantization_coefficient)
                if (elementIndex < sampleChunks[chunkIndex].size()) {
                    sampleChunks[chunkIndex].set(elementIndex, featureValue);
                }
            }
            return true;
        }

    private:
        // Load data from CSV format (used only once for initial dataset conversion)
        bool loadCSVData(const char* csvfile_path, uint16_t numFeatures) {
            if(isLoaded) {
                // clear existing data
                sampleChunks.clear();
                allLabels.clear();
                size_ = 0;
                isLoaded = false;
            }
            
            File file = RF_FS_OPEN(csvfile_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open CSV file for reading: ", csvfile_path);
                return false;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    RF_DEBUG(0, "❌ CSV file is empty or missing header: ", csvfile_path);
                    file.close();
                    return false;
                }
                int commaCount = 0;
                for (char c : line) {
                    if (c == ',') commaCount++;
                }
                numFeatures = commaCount;
            }

            // Set bitsPerSample and calculate chunk parameters only if not already initialized
            if (bitsPerSample == 0) {
                bitsPerSample = numFeatures * quantization_coefficient;
                updateSamplesEachChunk();
            } else {
                // Validate that the provided numFeatures matches the initialized bitsPerSample
                uint16_t expectedFeatures = bitsPerSample / quantization_coefficient;
                if (numFeatures != expectedFeatures) {
                    RF_DEBUG_2(0, "❌ Feature count mismatch: expected ", expectedFeatures, ", found ", numFeatures);   
                    file.close();
                    return false;
                }
            }
            
            sample_type linesProcessed = 0;
            sample_type emptyLines = 0;
            sample_type validSamples = 0;
            sample_type invalidSamples = 0;
            
            // Pre-allocate for efficiency
            allLabels.reserve(1000); // Initial capacity
            
            while (file.available()) {
                String line = file.readStringUntil('\n');
                line.trim();
                linesProcessed++;
                
                if (line.length() == 0) {
                    emptyLines++;
                    continue;
                }

                Rf_sample s;
                s.features.clear();
                s.features.reserve(numFeatures);

                label_type fieldIdx = 0;
                int start = 0;
                while (start < line.length()) {
                    int comma = line.indexOf(',', start);
                    if (comma < 0) comma = line.length();

                    String tok = line.substring(start, comma);
                    label_type v = (label_type)tok.toInt();

                    if (fieldIdx == 0) {
                        s.label = v;
                    } else {
                        s.features.push_back(v);
                    }

                    fieldIdx++;
                    start = comma + 1;
                }
                
                // Validate the sample
                if (fieldIdx != numFeatures + 1) {
                    RF_DEBUG_2(2, "❌ Invalid field count in line ", linesProcessed, ": expected ", numFeatures + 1);
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    RF_DEBUG_2(2, "❌ Feature count mismatch in line ", linesProcessed, ": expected ", numFeatures);
                    invalidSamples++;
                    continue;
                }
                
                s.features.fit();

                // Store in chunked packed format
                storeSample(s, validSamples);
                validSamples++;
                
                if (validSamples >= RF_MAX_SAMPLES) {
                    RF_DEBUG(1, "⚠️ Reached maximum sample limit");
                    break;
                }
            }
            size_ = validSamples;
            
            RF_DEBUG(1, "📋 CSV Processing Results: ");
            RF_DEBUG(1, "   Lines processed: ", linesProcessed);
            RF_DEBUG(1, "   Empty lines: ", emptyLines);
            RF_DEBUG(1, "   Valid samples: ", validSamples);
            RF_DEBUG(1, "   Invalid samples: ", invalidSamples);
            RF_DEBUG(1, "   Total samples in memory: ", size_);
            RF_DEBUG(1, "   Chunks used: ", sampleChunks.size());
            
            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            file.close();
            isLoaded = true;
            RF_FS_REMOVE(csvfile_path);
            RF_DEBUG(1, "✅ CSV data loaded and file removed: ", csvfile_path);
            return true;
        }

    public:
        uint8_t get_bits_per_label() const {
            return allLabels.get_bits_per_value();
        }

        int total_chunks() const {
            return size_/samplesEachChunk + (size_ % samplesEachChunk != 0 ? 1 : 0);
        }
        
        uint16_t total_features() const {
            return bitsPerSample / quantization_coefficient;
        }

        sample_type samplesPerChunk() const {
            return samplesEachChunk;
        }

        size_t size() const {
            return size_;
        }

        void setfile_path(const char* path) {
            strncpy(this->file_path, path, RF_PATH_BUFFER);
            this->file_path[RF_PATH_BUFFER - 1] = '\0';
        }

        // Fast accessors for training-time hot paths (avoid reconstructing Rf_sample)
        inline uint16_t num_features() const { return bitsPerSample / quantization_coefficient; }

        inline label_type getLabel(size_t sampleIndex) const {
            if (sampleIndex >= size_) return 0;
            return allLabels[sampleIndex];
        }

        inline uint16_t getFeature(size_t sampleIndex, uint16_t featureIndex) const {
            if (!isProperlyInitialized()) return 0;
            uint16_t nf = bitsPerSample / quantization_coefficient;
            if (featureIndex >= nf || sampleIndex >= size_) return 0;
            auto loc = getChunkLocation(sampleIndex);
            size_t chunkIndex = loc.first;
            size_t localIndex = loc.second;
            if (chunkIndex >= sampleChunks.size()) return 0;
            size_t elementsPerSample = nf;
            size_t startElementIndex = localIndex * elementsPerSample;
            size_t elementIndex = startElementIndex + featureIndex;
            if (elementIndex >= sampleChunks[chunkIndex].size()) return 0;
            return sampleChunks[chunkIndex][elementIndex];
        }

        // Reserve space for a specified number of samples
        void reserve(size_t numSamples) {
            if (!isProperlyInitialized()) {
                RF_DEBUG(1, "❌ Cannot reserve space: Rf_data not properly initialized", file_path);
                return;
            }
            allLabels.reserve(numSamples);
            ensureChunkCapacity(numSamples);
            RF_DEBUG_2(2, "📦 Reserved space for", numSamples, "samples, used chunks: ", sampleChunks.size());
        }

        bool convertCSVtoBinary(const char* csvfile_path, uint16_t numFeatures = 0) {
            RF_DEBUG(1, "🔄 Converting CSV to binary format from: ", csvfile_path);
            if(!loadCSVData(csvfile_path, numFeatures)) return false;
            if(!releaseData(false)) return false; 
            RF_DEBUG(1, "✅ CSV converted to binary and saved: ", file_path);
            return true;
        }

        /**
         * @brief Save data to file system in binary format and clear from RAM.
         * @param reuse If true, keeps data in RAM after saving; if false, clears data from RAM.
         * @note: after first time rf_data created, it must be releaseData(false) to save data
         */
        bool releaseData(bool reuse = true) {
            if(!isLoaded) return false;
            
            if(!reuse){
                RF_DEBUG(1, "💾 Saving data to file system and clearing from RAM...");
                // Remove any existing file
                if (RF_FS_EXISTS(file_path)) {
                    RF_FS_REMOVE(file_path);
                }
                File file = RF_FS_OPEN(file_path, RF_FILE_WRITE);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to open binary file for writing: ", file_path);
                    return false;
                }
                RF_DEBUG(2, "📂 Saving data to: ", file_path);

                // Write binary header
                uint32_t numSamples = size_;
                uint16_t numFeatures = bitsPerSample / quantization_coefficient;
                
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

                // Calculate packed bytes needed for features
                uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
                uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte

                // Write samples WITHOUT sample IDs (using vector indices)
                for (sample_type i = 0; i < size_; i++) {
                    // Reconstruct sample from chunked packed storage
                    Rf_sample s = getSample(i);
                    
                    // Write label only (no sample ID needed)
                    file.write(&s.label, sizeof(s.label));
                    
                    // Pack and write features
                    uint8_t packedBuffer[packedFeatureBytes];
                    // Initialize buffer to 0
                    for(uint16_t j = 0; j < packedFeatureBytes; j++) {
                        packedBuffer[j] = 0;
                    }
                    
                    // Pack features into bytes according to quantization_coefficient
                    for (size_t j = 0; j < s.features.size(); ++j) {
                        uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                        uint16_t byteIndex = bitPosition / 8;
                        uint8_t bitOffset = bitPosition % 8;
                        uint8_t feature_value = s.features[j] & ((1 << quantization_coefficient) - 1);
                        
                        if (bitOffset + quantization_coefficient <= 8) {
                            // Feature fits in single byte
                            packedBuffer[byteIndex] |= (feature_value << bitOffset);
                        } else {
                            // Feature spans two bytes
                            uint8_t bitsInFirstByte = 8 - bitOffset;
                            packedBuffer[byteIndex] |= (feature_value << bitOffset);
                            packedBuffer[byteIndex + 1] |= (feature_value >> bitsInFirstByte);
                        }
                    }
                    
                    file.write(packedBuffer, packedFeatureBytes);
                }
                file.close();
            }
            
            // Clear chunked memory
            sampleChunks.clear();
            sampleChunks.fit();
            allLabels.clear();
            allLabels.fit();
            isLoaded = false;
            RF_DEBUG_2(1, "✅ Data saved(", size_, "samples) to: ", file_path); 
            return true;
        }

        // Load data using sequential indices 
        bool loadData(bool re_use = true) {
            if(isLoaded || !isProperlyInitialized()) return false;
            RF_DEBUG(1, "📂 Loading data from: ", file_path);
            
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data file: ", file_path);
                if(RF_FS_EXISTS(file_path)) {
                    RF_FS_REMOVE(file_path);
                }
                return false;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read data header: ", file_path);
                file.close();
                return false;
            }

            if(numFeatures * quantization_coefficient != bitsPerSample) {
                RF_DEBUG_2(0, "❌ Feature count mismatch: expected ", bitsPerSample / quantization_coefficient, ",found ", numFeatures);
                file.close();
                return false;
            }
            size_ = numSamples;

            // Calculate sizes based on quantization_coefficient
            uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
            const uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte
            const size_t recordSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            const size_t elementsPerSample = numFeatures; // each feature is one element in packed_vector

            // Prepare storage: labels and chunks pre-sized to avoid per-sample resizing
            allLabels.clear();
            allLabels.reserve(numSamples);
            sampleChunks.clear();
            ensureChunkCapacity(numSamples);
            // Pre-size each chunk's element count and explicitly initialize to zero
            size_t remaining = numSamples;
            for (size_t ci = 0; ci < sampleChunks.size(); ++ci) {
                size_t chunkSamples = remaining > samplesEachChunk ? samplesEachChunk : remaining;
                size_t reqElems = chunkSamples * elementsPerSample;
                sampleChunks[ci].resize(reqElems, 0);  // Explicitly pass 0 as value
                remaining -= chunkSamples;
                if (remaining == 0) break;
            }

            // Batch read to reduce file I/O calls
            const size_t MAX_BATCH_BYTES = 2048; // conservative for MCU
            uint8_t* ioBuf = mem_alloc::allocate<uint8_t>(MAX_BATCH_BYTES);
            if (!ioBuf) {
                RF_DEBUG(1, "❌ Failed to allocate IO buffer");
                file.close();
                return false;
            }


            bool fallback_yet = false;
            size_t processed = 0;
            while (processed < numSamples) {
                size_t batchSamples;
                if (ioBuf) {
                    size_t maxSamplesByBuf = MAX_BATCH_BYTES / recordSize;
                    if (maxSamplesByBuf == 0) maxSamplesByBuf = 1;
                    batchSamples = (numSamples - processed) < maxSamplesByBuf ? (numSamples - processed) : maxSamplesByBuf;

                    size_t bytesToRead = batchSamples * recordSize;
                    size_t bytesRead = 0;
                    while (bytesRead < bytesToRead) {
                        int r = file.read(ioBuf + bytesRead, bytesToRead - bytesRead);
                        if (r <= 0) {
                            RF_DEBUG(0, "❌ Read batch failed: ", file_path);
                            if (ioBuf) mem_alloc::deallocate(ioBuf);
                            file.close();
                            return false;
                        }
                        bytesRead += r;
                    }

                    // Process buffer
                    for (size_t bi = 0; bi < batchSamples; ++bi) {
                        size_t off = bi * recordSize;
                        uint8_t lbl = ioBuf[off];
                        allLabels.push_back(lbl);

                        const uint8_t* packed = ioBuf + off + 1;
                        size_t sampleIndex = processed + bi;

                        // Locate chunk and base element index for this sample
                        auto loc = getChunkLocation(sampleIndex);
                        size_t chunkIndex = loc.first;
                        size_t localIndex = loc.second;
                        size_t startElementIndex = localIndex * elementsPerSample;

                        // Unpack features directly into chunk storage using set_unsafe for pre-sized storage
                        for (uint16_t j = 0; j < numFeatures; ++j) {
                            uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                            uint16_t byteIndex = bitPosition / 8;
                            uint8_t bitOffset = bitPosition % 8;
                            
                            uint8_t fv = 0;
                            if (bitOffset + quantization_coefficient <= 8) {
                                // Feature in single byte
                                uint8_t mask = ((1 << quantization_coefficient) - 1) << bitOffset;
                                fv = (packed[byteIndex] & mask) >> bitOffset;
                            } else {
                                // Feature spans two bytes
                                uint8_t bitsInFirstByte = 8 - bitOffset;
                                uint8_t bitsInSecondByte = quantization_coefficient - bitsInFirstByte;
                                uint8_t mask1 = ((1 << bitsInFirstByte) - 1) << bitOffset;
                                uint8_t mask2 = (1 << bitsInSecondByte) - 1;
                                fv = ((packed[byteIndex] & mask1) >> bitOffset) |
                                     ((packed[byteIndex + 1] & mask2) << bitsInFirstByte);
                            }
                            
                            size_t elemIndex = startElementIndex + j;
                            if (elemIndex >= sampleChunks[chunkIndex].size()) {
                                RF_DEBUG_2(0, "❌ Index out of bounds: elemIndex=", elemIndex, ", size=", sampleChunks[chunkIndex].size());
                            }
                            sampleChunks[chunkIndex].set_unsafe(elemIndex, fv);
                        }
                    }
                } else {
                    if (!fallback_yet) {
                        RF_DEBUG(2, "⚠️ IO buffer allocation failed, falling back to per-sample read");
                        fallback_yet = true;
                    }
                    // Fallback: per-sample small buffer
                    batchSamples = 1;
                    uint8_t lbl;
                    if (file.read(&lbl, sizeof(lbl)) != sizeof(lbl)) {
                        RF_DEBUG_2(0, "❌ Read label failed at sample: ", processed, ": ", file_path);
                        if (ioBuf) mem_alloc::deallocate(ioBuf);
                        file.close();
                        return false;
                    }
                    allLabels.push_back(lbl);
                    uint8_t packed[packedFeatureBytes] = {0};
                    if (file.read(packed, packedFeatureBytes) != packedFeatureBytes) {
                        RF_DEBUG_2(0, "❌ Read features failed at sample: ", processed, ": ", file_path);
                        if (ioBuf) mem_alloc::deallocate(ioBuf);
                        file.close();
                        return false;
                    }
                    auto loc = getChunkLocation(processed);
                    size_t chunkIndex = loc.first;
                    size_t localIndex = loc.second;
                    size_t startElementIndex = localIndex * elementsPerSample;
                    
                    // Unpack features according to quantization_coefficient
                    for (uint16_t j = 0; j < numFeatures; ++j) {
                        uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                        uint16_t byteIndex = bitPosition / 8;
                        uint8_t bitOffset = bitPosition % 8;
                        
                        uint8_t fv = 0;
                        if (bitOffset + quantization_coefficient <= 8) {
                            // Feature in single byte
                            uint8_t mask = ((1 << quantization_coefficient) - 1) << bitOffset;
                            fv = (packed[byteIndex] & mask) >> bitOffset;
                        } else {
                            // Feature spans two bytes
                            uint8_t bitsInFirstByte = 8 - bitOffset;
                            uint8_t bitsInSecondByte = quantization_coefficient - bitsInFirstByte;
                            uint8_t mask1 = ((1 << bitsInFirstByte) - 1) << bitOffset;
                            uint8_t mask2 = (1 << bitsInSecondByte) - 1;
                            fv = ((packed[byteIndex] & mask1) >> bitOffset) |
                                 ((packed[byteIndex + 1] & mask2) << bitsInFirstByte);
                        }
                        
                        size_t elemIndex = startElementIndex + j;
                        if (elemIndex < sampleChunks[chunkIndex].size()) {
                            sampleChunks[chunkIndex].set(elemIndex, fv);
                        }
                    }
                }
                processed += batchSamples;
            }

            if (ioBuf) mem_alloc::deallocate(ioBuf);

            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            isLoaded = true;
            file.close();
            if(!re_use) {
                RF_DEBUG(1, "♻️ Single-load mode: removing file after loading: ", file_path);
                RF_FS_REMOVE(file_path); // Remove file after loading in single mode
            }
            RF_DEBUG_2(1, "✅ Data loaded(", sampleChunks.size(), "chunks): ", file_path);
            return true;
        }

        /**
         * @brief Load specific samples from another Rf_data source by sample IDs.
         * @param source The source Rf_data to load samples from.
         * @param sample_IDs A sorted set of sample IDs to load from the source.
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: The state of the source data will be automatically restored, no need to reload.
         */
        bool loadData(Rf_data& source, const sampleID_set& sample_IDs, bool save_ram = true) {
            // Only the source must exist on file system; destination can be an in-memory buffer
            if (!RF_FS_EXISTS(source.file_path)) {
                RF_DEBUG(0, "❌ Source file does not exist: ", source.file_path);
                return false;
            }

            File file = RF_FS_OPEN(source.file_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open source file: ", source.file_path);
                return false;
            }
            bool pre_loaded = source.isLoaded;
            if(pre_loaded && save_ram) {
                source.releaseData();
            }
            // set all_labels bits_per_value according to source
            uint8_t bpl = source.get_bits_per_label();
            allLabels.set_bits_per_value(bpl);

            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read source header: ", source.file_path);
                file.close();
                return false;
            }

            // Clear current data and initialize parameters
            sampleChunks.clear();
            allLabels.clear();
            bitsPerSample = numFeatures * source.quantization_coefficient;
            quantization_coefficient = source.quantization_coefficient;
            updateSamplesEachChunk();

            // Calculate packed bytes needed for features
            uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
            uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte
            size_t sampleDataSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            
            // Reserve space for requested samples
            size_t numRequestedSamples = sample_IDs.size();
            allLabels.reserve(numRequestedSamples);
            
            RF_DEBUG_2(2, "📦 Loading ", numRequestedSamples, "samples from source: ", source.file_path);
            
            size_t addedSamples = 0;
            // Since sample_IDs are sorted in ascending order, we can read efficiently
            for(sample_type sampleIdx : sample_IDs) {
                if(sampleIdx >= numSamples) {
                    RF_DEBUG_2(2, "⚠️ Sample ID ", sampleIdx, "exceeds source sample count ", numSamples);
                    continue;
                }
                
                // Calculate file position for this sample
                size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);
                size_t sampleFilePos = headerSize + (sampleIdx * sampleDataSize);
                
                // Seek to the sample position
                if (!file.seek(sampleFilePos)) {
                    RF_DEBUG_2(2, "⚠️ Failed to seek to sample ", sampleIdx, "position ", sampleFilePos);
                    continue;
                }
                
                Rf_sample s;
                
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    RF_DEBUG(2, "⚠️ Failed to read label for sample ", sampleIdx);
                    continue;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    RF_DEBUG(2, "⚠️ Failed to read features for sample ", sampleIdx);
                    continue;
                }
                
                // Unpack features from bytes according to quantization_coefficient
                for(uint16_t j = 0; j < numFeatures; j++) {
                    // Calculate bit position for this feature
                    uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                    uint16_t byteIndex = bitPosition / 8;
                    uint8_t bitOffset = bitPosition % 8;
                    
                    // Extract the feature value (might span byte boundaries)
                    uint8_t feature = 0;
                    if (bitOffset + quantization_coefficient <= 8) {
                        // Feature fits in single byte
                        uint8_t mask = ((1 << quantization_coefficient) - 1) << bitOffset;
                        feature = (packedBuffer[byteIndex] & mask) >> bitOffset;
                    } else {
                        // Feature spans two bytes
                        uint8_t bitsInFirstByte = 8 - bitOffset;
                        uint8_t bitsInSecondByte = quantization_coefficient - bitsInFirstByte;
                        uint8_t mask1 = ((1 << bitsInFirstByte) - 1) << bitOffset;
                        uint8_t mask2 = (1 << bitsInSecondByte) - 1;
                        feature = ((packedBuffer[byteIndex] & mask1) >> bitOffset) |
                                  ((packedBuffer[byteIndex + 1] & mask2) << bitsInFirstByte);
                    }
                    s.features.push_back(feature);
                }
                s.features.fit();
                
                // Store in chunked packed format using addedSamples as the new index
                storeSample(s, addedSamples);
                addedSamples++;
            }
            
            size_ = addedSamples;
            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            isLoaded = true;
            file.close();
            if(pre_loaded && save_ram) {
                source.loadData();
            }
            RF_DEBUG_2(2, "✅ Loaded ", addedSamples, "samples from source: ", source.file_path);
            return true;
        }
        
        /**
         * @brief Load a specific chunk of samples from another Rf_data source.
         * @param source The source Rf_data to load samples from.
         * @param chunkIndex The index of the chunk to load (0-based).
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: this function will call loadData(source, chunkIDs) internally.
         */
        bool loadChunk(Rf_data& source, size_t chunkIndex, bool save_ram = true) {
            RF_DEBUG_2(2, "📂 Loading chunk ", chunkIndex, "from source: ", source.file_path);
            if(chunkIndex >= source.total_chunks()) {
                RF_DEBUG_2(2, "❌ Chunk index ", chunkIndex, "out of bounds : total chunks=", source.total_chunks());
                return false; 
            }
            bool pre_loaded = source.isLoaded;

            sample_type startSample = chunkIndex * source.samplesEachChunk;
            sample_type endSample = startSample + source.samplesEachChunk;
            if(endSample > source.size()) {
                endSample = source.size();
            }
            if(startSample >= endSample) {
                RF_DEBUG_2(2, "❌ Invalid chunk range: start ", startSample, ", end ", endSample);
                return false;
            }
            sampleID_set chunkIDs(startSample, endSample - 1);
            chunkIDs.fill();
            loadData(source, chunkIDs, save_ram);   
            return true;
        }

        /**
         *@brief: copy assignment (but not copy file_path to avoid file system over-writing)
         *@note : Rf_data will be put into release state. loadData() to reload into RAM if needed.
        */
        Rf_data& operator=(const Rf_data& other) {
            purgeData(); // Clear existing data safely
            if (this != &other) {
                if (RF_FS_EXISTS(other.file_path)) {
                    File testFile = RF_FS_OPEN(other.file_path, RF_FILE_READ);
                    if (testFile) {
                        uint32_t testNumSamples;
                        uint16_t testNumFeatures;
                        bool headerValid = (testFile.read((uint8_t*)&testNumSamples, sizeof(testNumSamples)) == sizeof(testNumSamples) &&
                                          testFile.read((uint8_t*)&testNumFeatures, sizeof(testNumFeatures)) == sizeof(testNumFeatures) &&
                                          testNumSamples > 0 && testNumFeatures > 0);
                        testFile.close();
                        
                        if (headerValid) {
                            if (!cloneFile(other.file_path, file_path)) {
                                RF_DEBUG(0, "❌ Failed to clone source file: ", other.file_path);
                            }
                        } else {
                            RF_DEBUG(0, "❌ Source file has invalid header: ", other.file_path);
                        }
                    } else {
                        RF_DEBUG(0, "❌ Cannot open source file: ", other.file_path);
                    }
                } else {
                    RF_DEBUG(0, "❌ Source file does not exist: ", other.file_path);
                }
                bitsPerSample = other.bitsPerSample;
                samplesEachChunk = other.samplesEachChunk;
                isLoaded = false; // Always start in unloaded state
                size_ = other.size_;
                // Deep copy of labels if loaded in memory
                allLabels = other.allLabels; // b_vector has its own copy semantics
            }
            return *this;   
        }

        // Clear data at both memory and file system
        void purgeData() {
            // Clear in-memory structures first
            sampleChunks.clear();
            sampleChunks.fit();
            allLabels.clear();
            allLabels.fit();
            isLoaded = false;
            size_ = 0;
            bitsPerSample = 0;
            samplesEachChunk = 0;

            // Then remove the file system file if one was specified
            if (RF_FS_EXISTS(file_path)) {
                RF_FS_REMOVE(file_path);
                RF_DEBUG(1, "🗑️ Deleted file: ", file_path);
            }
        }

        /**
         * @brief Add new data directly to file without loading into RAM
         * @param samples Vector of new samples to add
         * @param extend If false, keeps file size same (overwrites old data from start); 
         *               if true, appends new data while respecting size limits
         * @return : deleted labels
         * @note Directly writes to file system file to save RAM. File must exist and be properly initialized.
         */
        b_vector<label_type> addNewData(const b_vector<Rf_sample>& samples, bool extend = true) {
            b_vector<label_type> deletedLabels;

            if (!isProperlyInitialized()) {
                RF_DEBUG(0, "❌ Rf_data not properly initialized. Cannot add new data.");
                return deletedLabels;
            }
            if (!RF_FS_EXISTS(file_path)) {
                RF_DEBUG(0, "⚠️ File does not exist for adding new data: ", file_path);
                return deletedLabels;
            }
            if (samples.size() == 0) {
                RF_DEBUG(1, "⚠️ No samples to add");
                return deletedLabels;
            }

            // Read current file header to get existing info
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open file for adding new data: ", file_path);
                return deletedLabels;
            }

            uint32_t currentNumSamples;
            uint16_t numFeatures;
            
            if (file.read((uint8_t*)&currentNumSamples, sizeof(currentNumSamples)) != sizeof(currentNumSamples) ||
                file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read file header: ", file_path);
                file.close();
                return deletedLabels;
            }
            file.close();

            // Validate feature count compatibility
            if (samples.size() > 0 && samples[0].features.size() != numFeatures) {
                RF_DEBUG_2(0, "❌ Feature count mismatch: expected ", numFeatures, ", found ", samples[0].features.size());
                return deletedLabels;
            }

            // Calculate packed bytes needed for features
            uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
            uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte
            size_t sampleDataSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);

            uint32_t newNumSamples;
            size_t writePosition;
            
            if (extend) {
                // Append mode: add to existing samples
                newNumSamples = currentNumSamples + samples.size();
                
                // Check limits
                if (newNumSamples > RF_MAX_SAMPLES) {
                    size_t maxAddable = RF_MAX_SAMPLES - currentNumSamples;
                    RF_DEBUG(2, "⚠️ Reaching maximum sample limit, limiting to ", maxAddable);
                    newNumSamples = RF_MAX_SAMPLES;
                }
                
                size_t newFileSize = headerSize + (newNumSamples * sampleDataSize);
                if (newFileSize > RF_MAX_DATASET_SIZE) {
                    size_t maxSamplesBySize = (RF_MAX_DATASET_SIZE - headerSize) / sampleDataSize;
                    RF_DEBUG(2, "⚠️ Limiting samples by file size to ", maxSamplesBySize);
                    newNumSamples = maxSamplesBySize;
                }
                
                writePosition = headerSize + (currentNumSamples * sampleDataSize);
            } else {
                // Overwrite mode: keep same file size, write from beginning
                newNumSamples = currentNumSamples; // Keep same sample count - ALWAYS preserve original dataset size
                writePosition = headerSize; // Write from beginning of data section
            }

            // Calculate actual number of samples to write
            uint32_t samplesToWrite = extend ? 
                (newNumSamples - currentNumSamples) : 
                min((uint32_t)samples.size(), newNumSamples);

            RF_DEBUG_2(1, "📝 Adding ", samplesToWrite, "samples to ", file_path);
            RF_DEBUG_2(2, "📊 Dataset info: current=", currentNumSamples, ", new_total=", newNumSamples);

            // Open file for writing (r+ mode to update existing file)
            file = RF_FS_OPEN(file_path, "r+");
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open file for writing: ", file_path);
                return deletedLabels;
            }

            // In overwrite mode, read the labels that will be overwritten
            if (!extend && samplesToWrite > 0) {
                RF_DEBUG(2, "📋 Reading labels that will be overwritten: ", samplesToWrite);
                
                // Seek to the start of data section to read existing labels
                if (!file.seek(headerSize)) {
                    RF_DEBUG(0, "Seek to data section for reading labels: ", file_path);
                    file.close();
                    return deletedLabels;
                }
                
                // Reserve space for deleted labels
                deletedLabels.reserve(samplesToWrite);
                
                // Read labels that will be overwritten
                for (uint32_t i = 0; i < samplesToWrite; ++i) {
                    label_type existingLabel;
                    if (file.read(&existingLabel, sizeof(existingLabel)) != sizeof(existingLabel)) {
                        RF_DEBUG_2(0, "❌ Read existing label failed at index ", i, ": ", file_path);
                        break;
                    }
                    deletedLabels.push_back(existingLabel);
                    
                    // Skip the packed features to get to next label
                    if (!file.seek(file.position() + packedFeatureBytes)) {
                        RF_DEBUG_2(0, "❌ Seek past features failed at index ", i, ": ", file_path);
                        break;
                    }
                }
                
                RF_DEBUG_2(1, "📋 Collected ", deletedLabels.size(), " labels that will be overwritten", "");
            }

            // Update header with new sample count
            file.seek(0);
            file.write((uint8_t*)&newNumSamples, sizeof(newNumSamples));
            file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

            // Seek to write position
            if (!file.seek(writePosition)) {
                RF_DEBUG_2(0, "❌ Failed seek to write position ", writePosition, ": ", file_path);
                file.close();
                return deletedLabels;
            }

            // Write samples directly to file
            uint32_t written = 0;
            for (uint32_t i = 0; i < samplesToWrite && i < samples.size(); ++i) {
                const Rf_sample& sample = samples[i];
                
                // Validate sample feature count
                if (sample.features.size() != numFeatures) {
                    RF_DEBUG_2(2, "⚠️ Skipping sample ", i, " due to feature count mismatch: ", file_path);
                    continue;
                }

                // Write label
                if (file.write(&sample.label, sizeof(sample.label)) != sizeof(sample.label)) {
                    RF_DEBUG_2(0, "❌ Write label failed at sample ", i, ": ", file_path);
                    break;
                }

                // Pack and write features
                uint8_t packedBuffer[packedFeatureBytes];
                // Initialize buffer to 0
                for (uint16_t j = 0; j < packedFeatureBytes; j++) {
                    packedBuffer[j] = 0;
                }
                
                // Pack features according to quantization_coefficient
                for (size_t j = 0; j < sample.features.size(); ++j) {
                    uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                    uint16_t byteIndex = bitPosition / 8;
                    uint8_t bitOffset = bitPosition % 8;
                    uint8_t feature_value = sample.features[j] & ((1 << quantization_coefficient) - 1);
                    
                    if (bitOffset + quantization_coefficient <= 8) {
                        // Feature fits in single byte
                        packedBuffer[byteIndex] |= (feature_value << bitOffset);
                    } else {
                        // Feature spans two bytes
                        uint8_t bitsInFirstByte = 8 - bitOffset;
                        packedBuffer[byteIndex] |= (feature_value << bitOffset);
                        packedBuffer[byteIndex + 1] |= (feature_value >> bitsInFirstByte);
                    }
                }
                
                if (file.write(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    RF_DEBUG_2(0, "❌ Write features failed at sample ", i, ": ", file_path);
                    break;
                }
                
                written++;
            }

            file.close();

            // Update internal size if data is loaded in memory
            if (isLoaded) {
                size_ = newNumSamples;
                RF_DEBUG(1, "ℹ️ Data is loaded in memory. Consider reloading for consistency.");
            }

            RF_DEBUG_2(1, "✅ Successfully wrote ", written, "samples to: ", file_path);
            if (!extend && deletedLabels.size() > 0) {
                RF_DEBUG_2(1, "📊 Overwrote ", deletedLabels.size(), "samples with labels: ","");
            }
            
            return deletedLabels;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_data);
            total += allLabels.capacity() * sizeof(label_type);
            for (const auto& chunk : sampleChunks) {
                total += sizeof(packed_vector<8>);
                total += chunk.capacity() * sizeof(uint8_t); // stored in bytes regardless of bpv
            }
            return total;
        }
    };


    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_TREE -----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct node_layout{
        pair<uint8_t, uint8_t> featureID_layout; // start bit, length in bits
        pair<uint8_t, uint8_t> label_layout;     // start bit, length in bits
        pair<uint8_t, uint8_t> left_child_layout; // start bit, length in bits

        //default constructor
        node_layout() {
            featureID_layout  = make_pair(4, 10);       // 10 bits for featureID - max 1024 features
            label_layout      = make_pair(14, 8);       // 8 bits for label - max 256 classes
            left_child_layout = make_pair(22, 10);      // 10 bits for left child index - max 1024 nodes
        }
        // constructor
        node_layout(uint8_t feature_bits, uint8_t label_bits, uint8_t child_index_bits) {
            set_layout(feature_bits, label_bits, child_index_bits);
        }

        void set_layout(uint8_t feature_bits, uint8_t label_bits, uint8_t child_index_bits) {
            featureID_layout = make_pair(4, feature_bits);
            label_layout = make_pair(featureID_layout.first + featureID_layout.second, label_bits);
            left_child_layout = make_pair(label_layout.first + label_layout.second, child_index_bits);
        }
        // return max_features, allow exceed RF_MAX_FEATURES
        uint16_t max_features() const {
            uint16_t mf = 1 << featureID_layout.second;
            return mf > RF_MAX_FEATURES ? mf : RF_MAX_FEATURES;
        }
        // return max_labels,NOT allow exceed RF_MAX_CLASSES
        uint8_t max_labels() const {
            return (1 << label_layout.second);
        }
        // return max_nodes, allow exceed RF_MAX_NODES
        uint16_t max_nodes() const {
            uint16_t mn = 1 << left_child_layout.second;
            return mn > RF_MAX_NODES ? mn : RF_MAX_NODES;
        }
        uint8_t bits_per_node() const {
            return 1 + 3 + featureID_layout.second + label_layout.second + left_child_layout.second;
        }
    };

    struct Tree_node{
        uint32_t packed_data; 
        
        /** Bit layout optimized for breadth-first tree building:
            * bit   0      : is_leaf        
            * bits  1-3    : threshold slot
            * bits  4-o1   : featureID    (o1 - offset_1 = 4 + feature_bits)
            * bits  s1-o2  : label        (s1 - start_1 = o1 + 1, o2 - offset_2 = s1 + label_bits)
            * bits  s2-o3  : left child index   (s2 - start_2 = o2 + 1, o3 - offset_3 = s2 + child_index_bits)
        @note: right child index = left child index + 1 
        */

        Tree_node() : packed_data(0) {}

        inline bool getIsLeaf() const {
            return (packed_data >> 0) & 0x01;  // Bit 0
        }
        inline uint8_t getThresholdSlot() const {
            return (packed_data >> 1) & 0x07;  // Bits 1-3 (3 bits)
        }
        inline uint16_t getFeatureID(const pair<uint8_t, uint8_t>& layout) const noexcept{
            return (packed_data >> layout.first) & ((1 << layout.second) - 1);  
        }
        
        inline label_type getLabel(const pair<uint8_t, uint8_t>& layout) const noexcept{
            return (packed_data >> layout.first) & ((1 << layout.second) - 1);  
        }

        inline uint16_t getLeftChildIndex(const pair<uint8_t, uint8_t>& layout) const noexcept{
            return (packed_data >> layout.first) & ((1 << layout.second) - 1);  
        }
        
        inline uint16_t getRightChildIndex(const pair<uint8_t, uint8_t>& layout) const noexcept{
            return getLeftChildIndex(layout) + 1;  // Breadth-first property: right = left + 1
        }
        
        // Setter methods for packed data
        inline void setIsLeaf(bool isLeaf) {
            packed_data &= ~(0x01); // Clear bit 0
            packed_data |= (isLeaf ? 1 : 0) << 0; // Set bit 0
        }
        inline void setThresholdSlot(uint8_t slot) {
            packed_data &= ~(0x07 << 1); // Clear bits 1-3
            packed_data |= (slot & 0x07) << 1; // Set bits 1-3
        }
        inline void setFeatureID(uint16_t featureID, const pair<uint8_t, uint8_t>& layout) noexcept{
            packed_data &= ~(((1 << layout.second) - 1) << layout.first); // Clear featureID bits
            packed_data |= (featureID & ((1 << layout.second) - 1)) << layout.first; // Set featureID bits
        }
        inline void setLabel(label_type label, const pair<uint8_t, uint8_t>& layout) noexcept{
            packed_data &= ~(((1 << layout.second) - 1) << layout.first); // Clear label bits
            packed_data |= (label & ((1 << layout.second) - 1)) << layout.first; // Set label bits
        }
        inline void setLeftChildIndex(uint16_t index, const pair<uint8_t, uint8_t>& layout) noexcept{
            packed_data &= ~(((1 << layout.second) - 1) << layout.first); // Clear left child index bits
            packed_data |= (index & ((1 << layout.second) - 1)) << layout.first; // Set left child index bits
        }
    };


    class Rf_tree {
    public:
        packed_vector<32, Tree_node> nodes;
        node_layout* layout = nullptr;
        uint16_t depth;
        uint8_t index;
        bool isLoaded;

        Rf_tree() : nodes(), layout(nullptr), index(255), isLoaded(false) {}

        explicit Rf_tree(uint8_t idx) : nodes(), layout(nullptr), index(idx), isLoaded(false) {}

        Rf_tree(const Rf_tree& other)
            : nodes(other.nodes), layout(other.layout), index(other.index), isLoaded(other.isLoaded) {}

        Rf_tree& operator=(const Rf_tree& other) {
            if (this != &other) {
                nodes = other.nodes;
                layout = other.layout;
                index = other.index;
                isLoaded = other.isLoaded;
            }
            return *this;
        }

        Rf_tree(Rf_tree&& other) noexcept
            : nodes(std::move(other.nodes)),
              layout(other.layout),
              index(other.index),
              isLoaded(other.isLoaded) {
            other.layout = nullptr;
            other.index = 255;
            other.isLoaded = false;
        }

        Rf_tree& operator=(Rf_tree&& other) noexcept {
            if (this != &other) {
                nodes = std::move(other.nodes);
                layout = other.layout;
                index = other.index;
                isLoaded = other.isLoaded;
                other.layout = nullptr;
                other.index = 255;
                other.isLoaded = false;
            }
            return *this;
        }

        void set_layout(node_layout* layout_ptr, bool reset_storage = false) {
            layout = layout_ptr;
            if (reset_storage) {
                reset_node_storage();
            }
        }

        void reset_node_storage(size_t reserveCount = 0) {
            const uint8_t desired = desired_bits_per_node();
            if (nodes.get_bits_per_value() != desired) {
                nodes.set_bits_per_value(desired);
            } else {
                nodes.clear();
            }
            if (reserveCount > 0) {
                nodes.reserve(reserveCount);
            }
        }

        uint32_t countNodes() const {
            return static_cast<uint32_t>(nodes.size());
        }

        size_t memory_usage() const {
            return nodes.memory_usage() + sizeof(*this);
        }

        uint32_t countLeafNodes() const {
            uint32_t leafCount = 0;
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (nodes.get(i).getIsLeaf()) {
                    ++leafCount;
                }
            }
            return leafCount;
        }

        uint16_t getTreeDepth() const {
            return depth;
        }

        bool releaseTree(const char* path, bool re_use = false) {
            if (!re_use) {
                if (index > RF_MAX_TREES || nodes.empty()) {
                    RF_DEBUG(0, "❌ save tree failed, invalid tree index: ", index);
                    return false;
                }
                if (path == nullptr || strlen(path) == 0) {
                    RF_DEBUG(0, "❌ save tree failed, invalid path: ", path);
                    return false;
                }
                if (RF_FS_EXISTS(path)) {
                    if (!RF_FS_REMOVE(path)) {
                        RF_DEBUG(0, "❌ Failed to remove existing tree file: ", path);
                        return false;
                    }
                }
                File file = RF_FS_OPEN(path, FILE_WRITE);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to open tree file for writing: ", path);
                    return false;
                }

                uint32_t magic = 0x54524545; // "TREE"
                file.write(reinterpret_cast<uint8_t*>(&magic), sizeof(magic));

                uint32_t nodeCount = static_cast<uint32_t>(nodes.size());
                file.write(reinterpret_cast<uint8_t*>(&nodeCount), sizeof(nodeCount));

                if (nodeCount > 0) {
                    const size_t totalSize = nodeCount * sizeof(uint32_t);
                    uint8_t* buffer = mem_alloc::allocate<uint8_t>(totalSize);
                    if(buffer) {
                        for (uint32_t i = 0; i < nodeCount; ++i) {
                            const Tree_node node = nodes.get(i);
                            memcpy(buffer + (i * sizeof(uint32_t)),
                                   &node.packed_data,
                                   sizeof(uint32_t));
                        }
                        size_t written = file.write(buffer, totalSize);
                        mem_alloc::deallocate(buffer);
                        if (written != totalSize) {
                            RF_DEBUG(1, "⚠️ Incomplete tree write to file system");
                        }
                    } else {
                        for (uint32_t i = 0; i < nodeCount; ++i) {
                            const Tree_node node = nodes.get(i);
                            file.write(reinterpret_cast<const uint8_t*>(&node.packed_data),
                                       sizeof(node.packed_data));
                        }
                    }
                }
                file.close();
            }
            nodes.clear();
            nodes.fit();
            isLoaded = false;
            RF_DEBUG(2, "✅ Tree saved to file system: ", index);
            return true;
        }

        bool loadTree(const char* path, bool re_use = false) {
            if (isLoaded) {
                return true;
            }

            if (index >= RF_MAX_TREES) {
                RF_DEBUG(0, "❌ Invalid tree index: ", index);
                return false;
            }
            if (path == nullptr || strlen(path) == 0) {
                RF_DEBUG(0, "❌ Invalid path for loading tree: ", path);
                return false;
            }
            if (!RF_FS_EXISTS(path)) {
                RF_DEBUG(0, "❌ Tree file does not exist: ", path);
                return false;
            }
            File file = RF_FS_OPEN(path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(2, "❌ Failed to open tree file: ", path);
                return false;
            }

            uint32_t magic;
            if (file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) != sizeof(magic) ||
                magic != 0x54524545) {
                RF_DEBUG(0, "❌ Invalid tree file format: ", path);
                file.close();
                return false;
            }

            uint32_t nodeCount;
            if (file.read(reinterpret_cast<uint8_t*>(&nodeCount), sizeof(nodeCount)) != sizeof(nodeCount)) {
                RF_DEBUG(0, "❌ Failed to read node count: ", path);
                file.close();
                return false;
            }

            if (nodeCount == 0 || nodeCount > 2047) {
                RF_DEBUG(1, "❌ Invalid node count in tree file");
                file.close();
                return false;
            }

            reset_node_storage(nodeCount);
            for (uint32_t i = 0; i < nodeCount; ++i) {
                Tree_node node;
                if (file.read(reinterpret_cast<uint8_t*>(&node.packed_data), sizeof(node.packed_data))
                    != sizeof(node.packed_data)) {
                    RF_DEBUG(0, "❌ Failed to read node data");
                    nodes.clear();
                    file.close();
                    return false;
                }
                nodes.push_back(node);
            }

            file.close();

            isLoaded = true;
            RF_DEBUG_2(2, "✅ Tree loaded (", nodeCount, "nodes): ", path);

            if (!re_use) {
                RF_DEBUG(2, "♻️ Single-load mode: removing tree file after loading; ", path);
                RF_FS_REMOVE(path);
            }
            return true;
        }

        __attribute__((always_inline)) inline label_type predict_features(
            const packed_vector<8>& packed_features,
            const b_vector<uint16_t>& thresholds) const {
            if (!layout || nodes.empty()) {
                return RF_ERROR_LABEL;
            }

            if (thresholds.empty()) {
                return RF_ERROR_LABEL;
            }

            const auto& featureLayout = layout->featureID_layout;
            const auto& labelLayout = layout->label_layout;
            const auto& childLayout = layout->left_child_layout;

            uint16_t currentIndex = 0;
            const uint16_t nodeCount = static_cast<uint16_t>(nodes.size());

            while (__builtin_expect(currentIndex < nodeCount, 1)) {
                const Tree_node node = nodes.get(currentIndex);

                if (__builtin_expect(node.getIsLeaf(), 0)) {
                    return node.getLabel(labelLayout);
                }

                const uint16_t featureID = node.getFeatureID(featureLayout);
                const uint8_t thresholdSlot = node.getThresholdSlot();
                const uint16_t threshold = (thresholdSlot < thresholds.size())
                                               ? thresholds[thresholdSlot]
                                               : thresholds.back();

                const uint16_t featureValue = static_cast<uint16_t>(packed_features[featureID]);
                const uint16_t leftChild = node.getLeftChildIndex(childLayout);
                currentIndex = (featureValue <= threshold)
                                   ? leftChild
                                   : node.getRightChildIndex(childLayout);
            }
            
            return RF_ERROR_LABEL;
        }

        void clearTree(bool freeMemory = false) {
            (void)freeMemory;
            nodes.clear();
            nodes.fit();
            isLoaded = false;
        }

        void purgeTree(const char* path, bool rmf = true) {
            nodes.clear();
            nodes.fit();
            if (rmf && index < RF_MAX_TREES) {
                if (RF_FS_EXISTS(path)) {
                    RF_FS_REMOVE(path);
                    RF_DEBUG(2, "🗑️ Tree file removed: ", path);
                }
            }
            index = 255;
            isLoaded = false;
        }

    private:
        inline uint8_t desired_bits_per_node() const noexcept {
            uint8_t bits = layout ? layout->bits_per_node() : static_cast<uint8_t>(32);
            if (bits == 0 || bits > 32) {
                bits = 32;
            }
            return bits;
        }
    };
     
    /*
    ------------------------------------------------------------------------------------------------------------------
    ----------------------------------------------- RF_QUANTIZER ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    // Feature type definitions for CTG v2 format
    enum FeatureType { FT_DF = 0, FT_DC = 1, FT_CS = 2, FT_CU = 3 };
    
    // Packed feature reference structure (2 bytes per feature)
    struct FeatureRef {
        uint16_t packed; // bits 15..14: type, bits 13..8: aux, bits 7..0: offset
        
        FeatureRef() : packed(0) {}
        FeatureRef(FeatureType type, uint8_t aux, uint8_t offset) {
            packed = (static_cast<uint16_t>(type) << 14) | 
                    ((static_cast<uint16_t>(aux) & 0x3F) << 8) | 
                    static_cast<uint16_t>(offset);
        }
        
        FeatureType getType() const { return static_cast<FeatureType>(packed >> 14); }
        uint8_t getAux() const { return (packed >> 8) & 0x3F; }
        uint8_t getOffset() const { return packed & 0xFF; }
    };

    // Template trait to check supported vector types for Rf_quantizer
    template<typename T>
    struct is_supported_vector : std::false_type {};

    template<>
    struct is_supported_vector<vector<float>> : std::true_type {};

    template<>
    struct is_supported_vector<vector<int>> : std::true_type {};

    template<size_t sboSize>
    struct is_supported_vector<b_vector<float, sboSize>> : std::true_type {};

    template<size_t sboSize>
    struct is_supported_vector<b_vector<int, sboSize>> : std::true_type {};

    class Rf_quantizer {
    private:
        uint16_t numFeatures = 0;
        uint8_t groupsPerFeature = 0;
        label_type numLabels = 0;
        uint8_t quantization_coefficient = 2; // Bits per feature value (1-8)
        uint32_t scaleFactor = 50000;
        bool isLoaded = false;
        const Rf_base* base_ptr = nullptr;

        // Compact storage arrays
        b_vector<FeatureRef, 16> featureRefs;              // One per feature
        b_vector<uint16_t> sharedPatterns;             // Concatenated pattern edges
        b_vector<uint16_t, 32> allUniqueEdges;             // Concatenated unique edges
        b_vector<uint8_t> allDiscreteValues;           // Concatenated discrete values
        b_vector<uint16_t> labelOffsets;               // Offsets into labelStorage for each normalized label
        b_vector<uint16_t> labelLengths;               // Cached label lengths for faster copy
        b_vector<char, 256> labelStorage;                   // Contiguous storage for label strings (null terminated)
            
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }
        static constexpr size_t MAX_LINE_BUFFER = 1024;

        // Utility helpers for parsing text data without allocating Strings
        static void trim(char* str) {
            if (!str) {
                return;
            }
            size_t len = strlen(str);
            while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || str[len - 1] == ' ' || str[len - 1] == '\t')) {
                str[--len] = '\0';
            }

            size_t start = 0;
            while (str[start] == ' ' || str[start] == '\t' || str[start] == '\r') {
                start++;
            }
            if (start > 0) {
                memmove(str, str + start, strlen(str + start) + 1);
            }
        }

        static bool readLine(File& file, char* buffer, size_t bufferSize, size_t& outLength) {
            if (bufferSize == 0) {
                return false;
            }

            size_t readCount = file.readBytesUntil('\n', buffer, bufferSize - 1);
            outLength = readCount;

            if (outLength == 0 && !file.available()) {
                buffer[0] = '\0';
                return false; // EOF
            }

            buffer[outLength] = '\0';

            // Detect truncated line
            if (outLength >= bufferSize - 1 && file.available()) {
                // If next char isn't newline, line exceeded buffer
                int next = file.peek();
                if (next != '\n' && next != '\r') {
                    RF_DEBUG(0, "❌ Quantizer line exceeds buffer size");
                    return false;
                }
            }
            return true;
        }

        static size_t tokenize(char* line, char delimiter, char** tokens, size_t maxTokens) {
            size_t count = 0;
            char* current = line;
            while (current && *current != '\0' && count < maxTokens) {
                tokens[count++] = current;
                char* next = strchr(current, delimiter);
                if (!next) {
                    break;
                }
                *next = '\0';
                current = next + 1;
            }
            return count;
        }

        static bool readTrimmedLine(File& file, char* buffer, size_t bufferSize, size_t& outLength) {
            while (true) {
                if (!readLine(file, buffer, bufferSize, outLength)) {
                    buffer[0] = '\0';
                    return false;
                }
                trim(buffer);
                if (buffer[0] == '\0') {
                    if (!file.available()) {
                        return false;
                    }
                    continue;
                }
                return true;
            }
        }

        bool storeLabel(label_type id, const char* label) {
            if (id >= numLabels || label == nullptr) {
                return false;
            }

            size_t len = strlen(label);
            if (labelOffsets.size() < numLabels) {
                labelOffsets.resize(numLabels, UINT16_MAX);
            }
            if (labelLengths.size() < numLabels) {
                labelLengths.resize(numLabels, 0);
            }

            if (len > 0xFFFF) {
                len = 0xFFFF;
            }

            if (labelStorage.size() + len + 1 > 0xFFFF) {
                RF_DEBUG(0, "❌ Label storage overflow");
                return false;
            }

            uint16_t offset = static_cast<uint16_t>(labelStorage.size());
            labelOffsets[id] = offset;
            labelLengths[id] = static_cast<uint16_t>(len);
            for (size_t i = 0; i < len; ++i) {
                labelStorage.push_back(label[i]);
            }
            labelStorage.push_back('\0');
            return true;
        }
        
        // Optimized feature categorization - hot path, force inline
        __attribute__((always_inline)) inline uint8_t quantizeFeature(uint16_t featureIdx, float value) const {
            // Fast path: assume valid input during prediction (bounds checked during loading)
            const FeatureRef& ref = featureRefs[featureIdx];
            const uint8_t type = ref.getType();
            
            // FT_DF is most common, check first
            if (__builtin_expect(type == FT_DF, 1)) {
                // Full discrete range: clamp to 0..groupsPerFeature-1
                int intValue = static_cast<int>(value);
                return static_cast<uint8_t>((intValue < 0) ? 0 : ((intValue >= groupsPerFeature) ? (groupsPerFeature - 1) : intValue));
            }
            
            // Precompute scaled value for continuous features
            const uint32_t scaledValue = static_cast<uint32_t>(value * scaleFactor + 0.5f);
            
            if (type == FT_CS) {
                // Continuous shared pattern
                const uint16_t baseOffset = ref.getAux() * (groupsPerFeature - 1);
                const uint8_t limit = groupsPerFeature - 1;
                const uint16_t* patterns = &sharedPatterns[baseOffset];
                
                for (uint8_t bin = 0; bin < limit; ++bin) {
                    if (scaledValue < patterns[bin]) {
                        return bin;
                    }
                }
                return limit;
            }
            
            if (type == FT_CU) {
                // Continuous unique edges
                const uint8_t edgeCount = ref.getAux();
                const uint16_t baseOffset = ref.getOffset() * (groupsPerFeature - 1);
                const uint16_t* edges = &allUniqueEdges[baseOffset];
                
                for (uint8_t bin = 0; bin < edgeCount; ++bin) {
                    if (scaledValue < edges[bin]) {
                        return bin;
                    }
                }
                return edgeCount;
            }
            
            // FT_DC: Discrete custom values
            const uint8_t count = ref.getAux();
            const uint8_t offset = ref.getOffset();
            const uint8_t targetValue = static_cast<uint8_t>(value);
            const uint8_t* discreteVals = &allDiscreteValues[offset];
            
            for (uint8_t i = 0; i < count; ++i) {
                if (discreteVals[i] == targetValue) {
                    return i;
                }
            }
            return 0;
        }
         
    public:
        Rf_quantizer() = default;
        
        Rf_quantizer(Rf_base* base) {
            init(base);
        }
        ~Rf_quantizer() {
            base_ptr = nullptr;
            isLoaded = false;
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            labelOffsets.clear();
            labelLengths.clear();
            labelStorage.clear();
        }


        void init(Rf_base* base) {
            base_ptr = base;
            isLoaded = false;
        }
        
        // Load quantizer data from CTG v2 format without dynamic String allocations
        bool loadQuantizer() {
            if (isLoaded) {
                return true;
            }
            if (!has_base()) {
                RF_DEBUG(0, "❌ Load Quantizer failed: data pointer not ready");
                return false;
            }

            char file_path[RF_PATH_BUFFER];
            base_ptr->get_ctg_path(file_path);
            if (!RF_FS_EXISTS(file_path)) {
                RF_DEBUG(0, "❌ Quantizer file not found: ", file_path);
                return false;
            }

            File file = RF_FS_OPEN(file_path, "r");
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open Quantizer file: ", file_path);
                return false;
            }

            auto resetData = [&]() {
                numFeatures = 0;
                groupsPerFeature = 0;
                numLabels = 0;
                quantization_coefficient = 2;
                scaleFactor = 50000;
                isLoaded = false;
                featureRefs.clear();
                sharedPatterns.clear();
                allUniqueEdges.clear();
                allDiscreteValues.clear();
                labelOffsets.clear();
                labelLengths.clear();
                labelStorage.clear();
            };

            resetData();

            char line[MAX_LINE_BUFFER];
            size_t lineLength = 0;
            bool success = true;
            uint16_t numSharedPatterns = 0;

            do {
                if (!readTrimmedLine(file, line, MAX_LINE_BUFFER, lineLength)) {
                    RF_DEBUG(0, "❌ Empty Quantizer file: ", file_path);
                    success = false;
                    break;
                }

                char* tokens[64];
                size_t tokenCount = tokenize(line, ',', tokens, 64);
                if (tokenCount != 6) {
                    RF_DEBUG(0, "❌ Invalid Quantizer header token count");
                    success = false;
                    break;
                }
                for (size_t i = 0; i < tokenCount; ++i) {
                    trim(tokens[i]);
                }

                if (strcmp(tokens[0], "CTG2") != 0) {
                    RF_DEBUG(0, "❌ Unsupported Quantizer format identifier");
                    success = false;
                    break;
                }

                numFeatures = static_cast<uint16_t>(strtoul(tokens[1], nullptr, 10));
                groupsPerFeature = static_cast<uint8_t>(strtoul(tokens[2], nullptr, 10));
                numLabels = static_cast<uint8_t>(strtoul(tokens[3], nullptr, 10));
                numSharedPatterns = static_cast<uint16_t>(strtoul(tokens[4], nullptr, 10));
                scaleFactor = static_cast<uint32_t>(strtoul(tokens[5], nullptr, 10));

                // Calculate quantization_coefficient from groupsPerFeature
                if (groupsPerFeature == 0) {
                    RF_DEBUG(0, "❌ Invalid groupsPerFeature value in quantizer header");
                    success = false;
                    break;
                }
                
                // Compute bits needed: groupsPerFeature = 2^quantization_coefficient
                quantization_coefficient = 0;
                uint16_t temp = groupsPerFeature;
                while (temp > 1) {
                    temp >>= 1;
                    quantization_coefficient++;
                }
                // Clamp to valid range
                if (quantization_coefficient < 1) quantization_coefficient = 1;
                if (quantization_coefficient > 8) quantization_coefficient = 8;

                featureRefs.reserve(numFeatures);
                if (groupsPerFeature > 1) {
                    sharedPatterns.reserve(static_cast<size_t>(numSharedPatterns) * (groupsPerFeature - 1));
                    allUniqueEdges.reserve(static_cast<size_t>(numFeatures) * (groupsPerFeature - 1));
                }
                allDiscreteValues.reserve(static_cast<size_t>(numFeatures) * groupsPerFeature);
                labelOffsets.resize(numLabels, UINT16_MAX);
                labelLengths.resize(numLabels, 0);
                labelStorage.reserve(numLabels * 8);

                // Read label mappings: L,normalizedId,originalLabel
                while (true) {
                    size_t lineStart = file.position();
                    if (!readTrimmedLine(file, line, MAX_LINE_BUFFER, lineLength)) {
                        break;
                    }

                    if (strncmp(line, "L,", 2) != 0) {
                        file.seek(lineStart);
                        break;
                    }

                    char* idText = line + 2;
                    char* comma = strchr(idText, ',');
                    if (!comma) {
                        RF_DEBUG(0, "❌ Invalid label line format");
                        success = false;
                        break;
                    }
                    *comma = '\0';
                    trim(idText);

                    char* labelText = comma + 1;
                    trim(labelText);

                    label_type labelId = static_cast<label_type>(strtoul(idText, nullptr, 10));
                    if (!storeLabel(labelId, labelText)) {
                        RF_DEBUG(0, "❌ Failed to store label mapping");
                        success = false;
                        break;
                    }
                }
                if (!success) {
                    break;
                }

                // Read shared patterns: P,patternId,edgeCount,e1,e2,...
                char* patternTokens[64];
                for (uint16_t i = 0; i < numSharedPatterns; ++i) {
                    if (!readTrimmedLine(file, line, MAX_LINE_BUFFER, lineLength)) {
                        RF_DEBUG(0, "❌ Unexpected end of file while reading shared patterns");
                        success = false;
                        break;
                    }
                    size_t count = tokenize(line, ',', patternTokens, 64);
                    if (count < 3) {
                        RF_DEBUG(0, "❌ Invalid pattern line format");
                        success = false;
                        break;
                    }
                    for (size_t j = 0; j < count; ++j) {
                        trim(patternTokens[j]);
                    }
                    if (strcmp(patternTokens[0], "P") != 0) {
                        RF_DEBUG(0, "❌ Pattern line missing 'P' prefix");
                        success = false;
                        break;
                    }

                    (void)strtoul(patternTokens[1], nullptr, 10); // Pattern ID reserved for future validation
                    uint16_t edgeCount = static_cast<uint16_t>(strtoul(patternTokens[2], nullptr, 10));
                    if (count != static_cast<size_t>(3 + edgeCount)) {
                        RF_DEBUG_2(0, "❌ Pattern edge count mismatch - Expected: ", 3 + edgeCount, ", Found: ", count);
                        success = false;
                        break;
                    }

                    for (uint16_t j = 0; j < edgeCount; ++j) {
                        sharedPatterns.push_back(static_cast<uint16_t>(strtoul(patternTokens[3 + j], nullptr, 10)));
                    }
                }
                if (!success) {
                    break;
                }

                // Read feature definitions
                char* featureTokens[64];
                for (uint16_t i = 0; i < numFeatures; ++i) {
                    if (!readTrimmedLine(file, line, MAX_LINE_BUFFER, lineLength)) {
                        RF_DEBUG(0, "❌ Unexpected end of file while reading features");
                        success = false;
                        break;
                    }

                    size_t count = tokenize(line, ',', featureTokens, 64);
                    if (count == 0) {
                        RF_DEBUG(0, "❌ Empty feature line encountered");
                        success = false;
                        break;
                    }
                    for (size_t j = 0; j < count; ++j) {
                        trim(featureTokens[j]);
                    }

                    if (strcmp(featureTokens[0], "DF") == 0) {
                        featureRefs.push_back(FeatureRef(FT_DF, 0, 0));
                    } else if (strcmp(featureTokens[0], "DC") == 0) {
                        if (count < 2) {
                            RF_DEBUG(0, "❌ Invalid DC line format");
                            success = false;
                            break;
                        }
                        uint8_t customCount = static_cast<uint8_t>(strtoul(featureTokens[1], nullptr, 10));
                        if (count != static_cast<size_t>(2 + customCount)) {
                            RF_DEBUG_2(0, "❌ DC value count mismatch - Expected: ", 2 + customCount, ", Found: ", count);
                            success = false;
                            break;
                        }
                        uint8_t offset = static_cast<uint8_t>(allDiscreteValues.size());
                        for (uint8_t j = 0; j < customCount; ++j) {
                            allDiscreteValues.push_back(static_cast<uint8_t>(strtoul(featureTokens[2 + j], nullptr, 10)));
                        }
                        featureRefs.push_back(FeatureRef(FT_DC, customCount, offset));
                    } else if (strcmp(featureTokens[0], "CS") == 0) {
                        if (count != 2) {
                            RF_DEBUG(0, "❌ Invalid CS line format");
                            success = false;
                            break;
                        }
                        uint16_t patternId = static_cast<uint16_t>(strtoul(featureTokens[1], nullptr, 10));
                        featureRefs.push_back(FeatureRef(FT_CS, patternId, 0));
                    } else if (strcmp(featureTokens[0], "CU") == 0) {
                        if (count < 2) {
                            RF_DEBUG(0, "❌ Invalid CU line format");
                            success = false;
                            break;
                        }
                        uint8_t edgeCount = static_cast<uint8_t>(strtoul(featureTokens[1], nullptr, 10));
                        if (count != static_cast<size_t>(2 + edgeCount)) {
                            RF_DEBUG_2(0, "❌ CU edge count mismatch - Expected: ", 2 + edgeCount, ", Found: ", count);
                            success = false;
                            break;
                        }
                        if (groupsPerFeature <= 1) {
                            RF_DEBUG(0, "❌ Invalid groupsPerFeature for CU definition");
                            success = false;
                            break;
                        }
                        uint16_t divisor = static_cast<uint16_t>(groupsPerFeature - 1);
                        if (divisor == 0) {
                            RF_DEBUG(0, "❌ Division by zero in CU offset calculation");
                            success = false;
                            break;
                        }
                        uint8_t offset = static_cast<uint8_t>(allUniqueEdges.size() / divisor);
                        for (uint8_t j = 0; j < edgeCount; ++j) {
                            allUniqueEdges.push_back(static_cast<uint16_t>(strtoul(featureTokens[2 + j], nullptr, 10)));
                        }
                        featureRefs.push_back(FeatureRef(FT_CU, edgeCount, offset));
                    } else {
                        RF_DEBUG(0, "❌ Unknown feature type encountered");
                        success = false;
                        break;
                    }
                }
            } while (false);

            file.close();

            if (!success) {
                resetData();
                return false;
            }

            isLoaded = true;
            RF_DEBUG(1, "✅ Quantizer loaded successfully! : ", file_path);
            RF_DEBUG_2(2, "📊 Features: ", numFeatures, ", Groups: ", groupsPerFeature);
            RF_DEBUG_2(2, "   Labels: ", numLabels, ", Patterns: ", numSharedPatterns);
            RF_DEBUG(2, "   Scale Factor: ", scaleFactor);
            return true;
        }
        
        
        // Release loaded data from memory
        void releaseQuantizer(bool re_use = true) {
            if (!isLoaded) {
                return;
            }
            
            // Clear all data structures
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            labelOffsets.clear();
            labelLengths.clear();
            labelStorage.clear();
            isLoaded = false;
            RF_DEBUG(2, "🧹 Quantizer data released from memory");
        }

        // Core categorization function: write directly to pre-allocated buffer
        // This is the ONLY internal categorization method - optimized for zero allocations
        __attribute__((always_inline)) inline void quantizeFeatures(const float* features, packed_vector<8>& output) const {
            // Write directly to pre-allocated buffer
            for (uint16_t i = 0; i < numFeatures; ++i) {
                output.set(i, quantizeFeature(i, features[i]));
            }
        }
        
        size_t memory_usage() const {
            size_t usage = 0;
            
            // Basic members
            usage += sizeof(numFeatures) + sizeof(groupsPerFeature) + sizeof(numLabels) + 
                    sizeof(quantization_coefficient) + sizeof(scaleFactor) + sizeof(isLoaded);
            usage += 4;
            
            // Core data structures
            usage += featureRefs.size() * sizeof(FeatureRef);
            usage += sharedPatterns.size() * sizeof(uint16_t);
            usage += allUniqueEdges.size() * sizeof(uint16_t);
            usage += allDiscreteValues.size() * sizeof(uint8_t);
            usage += labelOffsets.size() * sizeof(uint16_t);
            usage += labelLengths.size() * sizeof(uint16_t);
            usage += labelStorage.size() * sizeof(char);
            
            return usage;
        }
        
        // Getters
        uint16_t getNumFeatures() const { return numFeatures; }
        uint8_t getGroupsPerFeature() const { return groupsPerFeature; }
        label_type getNumLabels() const { return numLabels; }
        uint8_t getQuantizationCoefficient() const { return quantization_coefficient; }
        uint32_t getScaleFactor() const { return scaleFactor; }
        bool loaded() const { return isLoaded; }

        const char* getOriginalLabelPtr(label_type normalizedLabel) const {
            if (normalizedLabel >= labelOffsets.size()) {
                return nullptr;
            }
            uint16_t offset = labelOffsets[normalizedLabel];
            if (offset == UINT16_MAX || offset >= labelStorage.size()) {
                return nullptr;
            }
            return &labelStorage[offset];
        }

        bool getOriginalLabelView(label_type normalizedLabel, const char** outData, uint16_t* outLength = nullptr) const {
            if (!outData) {
                return false;
            }
            const char* data = getOriginalLabelPtr(normalizedLabel);
            if (!data) {
                *outData = nullptr;
                if (outLength) {
                    *outLength = 0;
                }
                return false;
            }
            *outData = data;
            if (outLength) {
                uint16_t cached = (normalizedLabel < labelLengths.size()) ? labelLengths[normalizedLabel] : 0;
                // Fallback: if cache is empty (0), compute length once and update cache for future calls
                if (cached == 0) {
                    cached = static_cast<uint16_t>(strlen(data));
                    // Update cache if within bounds to avoid repeated strlen calls
                    if (normalizedLabel < labelLengths.size() && labelLengths[normalizedLabel] == 0) {
                        const_cast<b_vector<uint16_t>&>(labelLengths)[normalizedLabel] = cached;
                    }
                }
                *outLength = cached;
            }
            return true;
        }

        bool getOriginalLabel(label_type normalizedLabel, char* buffer, size_t bufferSize) const {
            if (!buffer || bufferSize == 0) {
                return false;
            }
            const char* labelPtr = getOriginalLabelPtr(normalizedLabel);
            if (!labelPtr) {
                buffer[0] = '\0';
                return false;
            }
            uint16_t length = (normalizedLabel < labelLengths.size()) ? labelLengths[normalizedLabel] : 0;
            if (length == 0) {
                buffer[0] = '\0';
                return true;
            }
            if (length >= bufferSize) {
                memcpy(buffer, labelPtr, bufferSize - 1);
                buffer[bufferSize - 1] = '\0';
                return false;
            }
            memcpy(buffer, labelPtr, length);
            buffer[length] = '\0';
            return true;
        }

        // mapping from original label to normalized label 
        label_type getNormalizedLabel(const char* originalLabel) const {
            if (!originalLabel || originalLabel[0] == '\0') {
                return RF_ERROR_LABEL;
            }
            for (label_type i = 0; i < labelOffsets.size(); ++i) {
                uint16_t offset = labelOffsets[i];
                if (offset == UINT16_MAX || offset >= labelStorage.size()) {
                    continue;
                }
                uint16_t length = (i < labelLengths.size()) ? labelLengths[i] : 0;
                if (length == 0) {
                    if (originalLabel[0] == '\0') {
                        return i;
                    }
                    continue;
                }
                if (strncmp(originalLabel, &labelStorage[offset], length) == 0 && originalLabel[length] == '\0') {
                    return i;
                }
            }
            return RF_ERROR_LABEL;
        }
    };
    
    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------- RF_NODE_PREDCITOR ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct node_data {
        uint32_t total_nodes;
        uint16_t max_depth;
        uint8_t min_split;

        node_data() : total_nodes(0), max_depth(0), min_split(0) {}
        node_data(uint8_t min_split, uint16_t max_depth) 
            : total_nodes(0), max_depth(max_depth), min_split(min_split) {}
        node_data(uint8_t min_split, uint16_t max_depth, uint32_t total_nodes) 
            : total_nodes(total_nodes), max_depth(max_depth), min_split(min_split) {}
    };

    class Rf_node_predictor {
    public:
        float coefficients[3];  // bias, min_split_coeff, max_depth_coeff
        bool is_trained;
        b_vector<node_data, 5> buffer;
    private:
        const Rf_base* base_ptr = nullptr;
        
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }

        float evaluate_formula(const node_data& data) const {
            if (!is_trained) {
                return manual_estimate(data); // Use manual estimate if not trained
            }
            
            float result = coefficients[0]; // bias
            result += coefficients[1] * static_cast<float>(data.min_split);
            result += coefficients[2] * static_cast<float>(data.max_depth);
            
            return result > 10.0f ? result : 10.0f; // ensure reasonable minimum
        }

        // if failed to load predictor, manual estimate will be used
        float manual_estimate(const node_data& data) const {
            if (data.min_split == 0 || data.max_depth == 0) {
                return 100.0f; 
            }
            // Simple heuristic: more nodes = better accuracy
            float estimate = 100.0f - data.min_split * 12 + data.max_depth * 3;
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
            return prediction; 
        }
        float raw_estimate(uint8_t min_split, uint16_t max_depth) {
            node_data data(min_split, max_depth);
            return raw_estimate(data);
        }
        

    public:
        uint8_t accuracy;      // in percentage
        uint8_t peak_percent;  // number of nodes at depth with maximum number of nodes / total number of nodes in tree
        
        Rf_node_predictor() : is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
        }

        Rf_node_predictor(Rf_base* base) : base_ptr(base), is_trained(false), accuracy(0), peak_percent(0) {
            RF_DEBUG(2, "🔧 Initializing node predictor");
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
        }

        ~Rf_node_predictor() {
            base_ptr = nullptr;
            is_trained = false;
            buffer.clear();
        }

        void init(Rf_base* base) {
            base_ptr = base;
            is_trained = false;
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
            // get logfile path from file_path : "/model_name_node_pred.bin" -> "/model_name_node_log.csv"
            char node_predictor_log[RF_PATH_BUFFER] = {0};
            if (base_ptr) {
                base_ptr->get_node_log_path(node_predictor_log);
            }
            // check if node_log file exists, if not create with header
            if (node_predictor_log[0] != '\0' && !RF_FS_EXISTS(node_predictor_log)) {
                File logFile = RF_FS_OPEN(node_predictor_log, FILE_WRITE);
                if (logFile) {
                    logFile.println("min_split,max_depth,total_nodes");
                    logFile.close();
                }
            }
        }
        
        // Load trained model from file system (updated format without version)
        bool loadPredictor() {
            if (!has_base()){
                RF_DEBUG(0, "❌ Load Predictor failed: base pointer not ready");
                return false;
            }
            char file_path[RF_PATH_BUFFER];
            base_ptr->get_node_pred_path(file_path);
            RF_DEBUG(2, "🔍 Loading node predictor from file: ", file_path);
            if(is_trained) return true;
            if (!RF_FS_EXISTS(file_path)) {
                RF_DEBUG(1, "⚠️  No predictor file found, using default predictor.");
                return false;
            }
            
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open predictor file: ", file_path);
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                RF_DEBUG(0, "❌ Invalid predictor file format: ", file_path);
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                RF_DEBUG(0, "❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                RF_DEBUG(2, "⚠️ Failed to read accuracy, using manual estimate node.");
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                RF_DEBUG(2, "⚠️ Failed to read peak_percent, using manual estimate node.");
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients) || num_coefficients != 3) {
                RF_DEBUG_2(2, "❌ Coefficient count mismatch - Expected: ", 3, ", Found: ", num_coefficients);
                file.close();
                return false;
            }
            
            // Read coefficients
            if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                RF_DEBUG(0, "❌ Failed to read coefficients");
                file.close();
                return false;
            }
            
            file.close();
            
            // Only set is_trained to true if the file was actually trained
            if (file_is_trained) {
                is_trained = true;
                if (peak_percent == 0) {
                    peak_percent = 30; // Use reasonable default for binary trees
                    RF_DEBUG(2, "⚠️  Fixed peak_percent from 0% to 30%");
                }
                RF_DEBUG(1, "✅ Node predictor loaded : ", file_path);
                RF_DEBUG(2, "bias: ", this->coefficients[0]);
                RF_DEBUG(2, "min_split effect: ", this->coefficients[1]);
                RF_DEBUG(2, "max_depth effect: ", this->coefficients[2]);
                RF_DEBUG(2, "accuracy: ", accuracy);
            } else {
                RF_DEBUG(1, "⚠️  Predictor file exists but is not trained, using default predictor.");
                is_trained = false;
            }
            return file_is_trained;
        }
        
        // Save trained predictor to file system
        bool releasePredictor() {
            if (!has_base()){
                RF_DEBUG(0, "❌ Release Predictor failed: base pointer not ready");
                return false;
            }
            if (!is_trained) {
                RF_DEBUG(1, "❌ Predictor is not trained, cannot save.");
                return false;
            }
            char file_path[RF_PATH_BUFFER];
            base_ptr->get_node_pred_path(file_path);
            if (RF_FS_EXISTS(file_path)) RF_FS_REMOVE(file_path);

            File file = RF_FS_OPEN(file_path, FILE_WRITE);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to create predictor file: ", file_path);
                return false;
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
            uint8_t num_coefficients = 3;
            file.write((uint8_t*)&num_coefficients, sizeof(num_coefficients));
            
            // Write coefficients
            file.write((uint8_t*)coefficients, sizeof(float) * 3);
            
            file.close();
            RF_DEBUG(1, "✅ Node predictor saved: ", file_path);
            return true;
        }
        
        // Add new training samples to buffer
        void add_new_samples(uint8_t min_split, uint16_t max_depth, uint32_t total_nodes) {
            if (min_split == 0 || max_depth == 0) return; // invalid sample
            if (buffer.size() >= 100) {
                RF_DEBUG(2, "⚠️ Node_pred buffer full, consider retraining soon.");
                return;
            }
            buffer.push_back(node_data(min_split, max_depth, total_nodes));
        }
        // Retrain the predictor using data from rf_tree_log.csv (synchronized with PC version)
        bool re_train(bool save_after_retrain = true) {
            if (!has_base()){
                RF_DEBUG(0, "❌ Base pointer is null, cannot retrain predictor.");
                return false;
            }
            if(buffer.size() > 0){
                flush_buffer();
            }
            buffer.clear();
            buffer.fit();

            if(!can_retrain()) {
                RF_DEBUG(2, "❌ No training data available for retraining.");
                return false;
            }

            char node_predictor_log[RF_PATH_BUFFER];
            base_ptr->get_node_log_path(node_predictor_log);
            RF_DEBUG(2, "🔂 Starting retraining of node predictor...");
            File file = RF_FS_OPEN(node_predictor_log, RF_FILE_READ);
            if (!file) {
                RF_DEBUG(1, "❌ Failed to open node_predictor log file: ", node_predictor_log);
                return false;
            }
            RF_DEBUG(2, "🔄 Retraining node predictor from CSV data...");
            
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
                
                // Parse CSV line: min_split,max_depth,total_nodes
                node_data sample;
                int comma1 = line.indexOf(',');
                int comma2 = line.indexOf(',', comma1 + 1);
                
                if (comma1 != -1 && comma2 != -1) {
                    String min_split_str = line.substring(0, comma1);
                    String max_depth_str = line.substring(comma1 + 1, comma2);
                    String total_nodes_str = line.substring(comma2 + 1);
                    
                    sample.min_split = min_split_str.toInt();
                    sample.max_depth = max_depth_str.toInt();
                    sample.total_nodes = total_nodes_str.toInt();
                    
                    // skip invalid samples
                    if (sample.min_split > 0 && sample.max_depth > 0 && sample.total_nodes > 0) {
                        training_data.push_back(sample);
                    }
                }
            }
            file.close();
            
            if (training_data.size() < 3) {
                return false;
            }
            
            // Collect all unique min_split and max_depth values
            b_vector<uint8_t> unique_min_splits;
            b_vector<uint16_t> unique_max_depths;
            
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
                
                // Add unique max_depth values
                bool found_depth = false;
                for (const auto& existing_depth : unique_max_depths) {
                    if (existing_depth == sample.max_depth) {
                        found_depth = true;
                        break;
                    }
                }
                if (!found_depth) {
                    unique_max_depths.push_back(sample.max_depth);
                }
            }
            
            // Sort vectors for easier processing
            unique_min_splits.sort();
            unique_max_depths.sort();
            
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
            
            // Calculate max_depth effect directly
            float depth_effect = 0.0f;
            if (unique_max_depths.size() >= 2) {
                // Calculate average nodes for first and last max_depth values
                float first_depth_avg = 0.0f;
                float last_depth_avg = 0.0f;
                int first_depth_count = 0;
                int last_depth_count = 0;
                
                uint16_t first_depth = unique_max_depths[0];
                uint16_t last_depth = unique_max_depths[unique_max_depths.size() - 1];
                
                // Calculate averages directly from training data
                for (const auto& sample : training_data) {
                    if (sample.max_depth == first_depth) {
                        first_depth_avg += sample.total_nodes;
                        first_depth_count++;
                    } else if (sample.max_depth == last_depth) {
                        last_depth_avg += sample.total_nodes;
                        last_depth_count++;
                    }
                }
                
                if (first_depth_count > 0 && last_depth_count > 0) {
                    first_depth_avg /= first_depth_count;
                    last_depth_avg /= last_depth_count;
                    
                    float depth_range = static_cast<float>(last_depth - first_depth);
                    if (depth_range > 0.01f) {
                        depth_effect = (last_depth_avg - first_depth_avg) / depth_range;
                    }
                }
            }
            
            // Calculate overall average as baseline
            float overall_avg = 0.0f;
            for (const auto& sample : training_data) {
                overall_avg += sample.total_nodes;
            }
            overall_avg /= training_data.size();
            
            // Build the simple linear predictor: nodes = bias + split_coeff * min_split + depth_coeff * max_depth
            // Calculate bias to center the predictor around the overall average
            float reference_split = unique_min_splits.empty() ? 3.0f : static_cast<float>(unique_min_splits[0]);
            float reference_depth = unique_max_depths.empty() ? 6.0f : static_cast<float>(unique_max_depths[0]);
            
            coefficients[0] = overall_avg - (split_effect * reference_split) - (depth_effect * reference_depth); // bias
            coefficients[1] = split_effect; // min_split coefficient
            coefficients[2] = depth_effect; // max_depth coefficient
            
            // Calculate accuracy using PC version's approach exactly
            float total_error = 0.0f;
            float total_actual = 0.0f;
            
            for (const auto& sample : training_data) {
                node_data data(sample.min_split, sample.max_depth);
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
            RF_DEBUG(2, "✅ Node predictor retraining complete!");
            RF_DEBUG_2(2, "   Accuracy: ", accuracy, "%, Peak (%): ", peak_percent);
            if(save_after_retrain) releasePredictor(); // Save the new predictor
            return true;
        }
        
        uint16_t estimate_nodes(uint8_t min_split, uint16_t max_depth) {
            float raw_est = raw_estimate(min_split, max_depth);
            float acc = accuracy;
            if(acc == 0) acc = 0.85f;
            uint16_t estimate = static_cast<uint16_t>(raw_est * 100 / accuracy);
            return estimate < RF_MAX_NODES ? estimate : 512;       // 2kB RAM
        }

        uint16_t estimate_nodes(const Rf_config& config) {
            uint8_t min_split = config.min_split;
            uint16_t max_depth = config.max_depth;
            float raw_est = raw_estimate(min_split, max_depth);
            float acc = accuracy;
            if(acc == 0) acc = 0.85f;
            uint16_t estimate = static_cast<uint16_t>(raw_est * 100 / accuracy);
            if(config.training_score == K_FOLD_SCORE){
                estimate = estimate * config.k_folds / (config.k_folds + 1);
            }
            return estimate < RF_MAX_NODES ? estimate : 512;       // 2kB RAM
        }

        uint16_t queue_peak_size(uint8_t min_split, uint16_t max_depth) {
            return min(120, estimate_nodes(min_split, max_depth) * peak_percent / 100);
        }

        uint16_t queue_peak_size(const Rf_config& config) {
            uint16_t est_nodes = estimate_nodes(config);
            if(config.training_score == K_FOLD_SCORE){
                est_nodes = est_nodes * config.k_folds / (config.k_folds + 1);
            }
            est_nodes = static_cast<uint16_t>(est_nodes * peak_percent / 100);
            uint16_t max_peak_theory = static_cast<uint16_t>(RF_MAX_NODES * 0.3f);
            uint16_t min_peak_theory = 30;
            if (est_nodes > max_peak_theory) return max_peak_theory;
            if (est_nodes < min_peak_theory) return min_peak_theory;
            return est_nodes;
        }

        void flush_buffer() {
            if (!has_base()){
                RF_DEBUG(0, "❌Failed to flush_buffer : base pointer is null");
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
            String header = "min_split,max_depth,total_nodes";
            if (lines.empty() || lines[0] != header) {
                lines.insert(0, header);
            }
            // Remove header for easier manipulation
            b_vector<String> data_lines;
            for (size_t i = 1; i < lines.size(); ++i) {
                data_lines.push_back(lines[i]);
            }
            // Prepend new samples
            for (int i = buffer.size() - 1; i >= 0; --i) {
                const node_data& nd = buffer[i];
                String row = String(nd.min_split) + "," + String(nd.max_depth) + "," + String(nd.total_nodes);
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
                RF_DEBUG(0, "❌ can_retrain check failed: base pointer not ready");
                return false;
            }
            char node_predictor_log[RF_PATH_BUFFER];
            base_ptr->get_node_log_path(node_predictor_log);
            if (!RF_FS_EXISTS(node_predictor_log)) {
                RF_DEBUG(2, "❌ No log file found for retraining.");
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
                RF_DEBUG(2, "❌ Not enough data for retraining (need > 3 samples).");
            }
            return result;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_node_predictor);
            total += buffer.capacity() * sizeof(node_data);
            return total + 4;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_RANDOM -----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    class Rf_random {
    private:
        struct PCG32 {
            uint64_t state = 0x853c49e6748fea9bULL;
            uint64_t inc   = 0xda3e39cb94b95bdbULL;

            inline void seed(uint64_t initstate, uint64_t initseq) {
                state = 0U;
                inc = (initseq << 1u) | 1u;
                next();
                state += initstate;
                next();
            }

            inline uint32_t next() {
                uint64_t oldstate = state;
                state = oldstate * 6364136223846793005ULL + inc;
                uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
                uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
                return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
            }

            inline uint32_t bounded(uint32_t bound) {
                if (bound == 0) return 0;
                uint32_t threshold = -bound % bound;
                while (true) {
                    uint32_t r = next();
                    if (r >= threshold) return r % bound;
                }
            }
        };

        static constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
        static constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
        static constexpr uint64_t SMIX_C1 = 0x9e3779b97f4a7c15ULL;
        static constexpr uint64_t SMIX_C2 = 0xbf58476d1ce4e5b9ULL;
        static constexpr uint64_t SMIX_C3 = 0x94d049bb133111ebULL;

        // Avoid ODR by using function-local statics for global seed state
        static uint64_t &global_seed() { static uint64_t v = 0ULL; return v; }
        static bool &has_global() { static bool v = false; return v; }

        uint64_t base_seed = 0;
        PCG32 engine;

        inline uint64_t splitmix64(uint64_t x) const {
            x += SMIX_C1;
            x = (x ^ (x >> 30)) * SMIX_C2;
            x = (x ^ (x >> 27)) * SMIX_C3;
            return x ^ (x >> 31);
        }

    public:
        Rf_random() {
            if (has_global()) {
                base_seed = global_seed();
            } else {
                uint64_t hw = (static_cast<uint64_t>(esp_random()) << 32) ^ static_cast<uint64_t>(esp_random());
                uint64_t cyc = static_cast<uint64_t>(ESP.getCycleCount());
                base_seed = splitmix64(hw ^ cyc);
            }
            engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
        }

        Rf_random(uint64_t seed) {
            init(seed, true);
        }

        void init(uint64_t seed, bool use_provided_seed = true) {
            if (use_provided_seed) {
                base_seed = seed;
            } else if (has_global()) {
                base_seed = global_seed();
            } else {
                uint64_t hw = (static_cast<uint64_t>(esp_random()) << 32) ^ static_cast<uint64_t>(esp_random());
                uint64_t cyc = static_cast<uint64_t>(ESP.getCycleCount());
                base_seed = splitmix64(hw ^ cyc ^ seed);
            }
            engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
        }

        // Global seed control
        static void setGlobalSeed(uint64_t seed) { global_seed() = seed; has_global() = true; }
        static void clearGlobalSeed() { has_global() = false; }
        static bool hasGlobalSeed() { return has_global(); }

        // Basic API
        inline uint32_t next() { return engine.next(); }
        inline uint32_t bounded(uint32_t bound) { return engine.bounded(bound); }
        inline float nextFloat() { return static_cast<float>(next()) / static_cast<float>(UINT32_MAX); }
        inline double nextDouble() { return static_cast<double>(next()) / static_cast<double>(UINT32_MAX); }

        void seed(uint64_t new_seed) {
            base_seed = new_seed;
            engine.seed(base_seed, base_seed ^ 0xda3e39cb94b95bdbULL);
        }
        inline uint64_t getBaseSeed() const { return base_seed; }

        // Deterministic substreams
        Rf_random deriveRNG(uint64_t stream, uint64_t nonce = 0) const {
            uint64_t s = splitmix64(base_seed ^ (stream * SMIX_C1 + nonce));
            uint64_t inc = splitmix64(base_seed + (stream << 1) + 0x632be59bd9b4e019ULL);
            Rf_random r;
            r.base_seed = s;
            r.engine.seed(s, inc);
            return r;
        }

        // Hash helpers (FNV-1a)
        static inline uint64_t hashString(const char* data) {
            uint64_t h = FNV_OFFSET;
            for (const unsigned char* p = reinterpret_cast<const unsigned char*>(data); *p; ++p) { h ^= *p; h *= FNV_PRIME; }
            return h;
        }
        static inline uint64_t hashBytes(const uint8_t* data, size_t len) {
            uint64_t h = FNV_OFFSET;
            for (size_t i = 0; i < len; ++i) { h ^= data[i]; h *= FNV_PRIME; }
            return h;
        }
        template <class IdVec>
        static uint64_t hashIDVector(const IdVec& ids) {
            uint64_t h = FNV_OFFSET;
            for (size_t i = 0; i < ids.size(); ++i) {
                uint16_t v = ids[i];
                h ^= static_cast<uint64_t>(v & 0xFF);
                h *= FNV_PRIME;
                h ^= static_cast<uint64_t>((v >> 8) & 0xFF);
                h *= FNV_PRIME;
            }
            h ^= static_cast<uint64_t>(ids.size() & 0xFF);
            h *= FNV_PRIME;
            h ^= static_cast<uint64_t>((ids.size() >> 8) & 0xFF);
            h *= FNV_PRIME;
            return h;
        }
        size_t memory_usage() const {
            size_t total = sizeof(Rf_random);
            return total;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------------------
    ------------------------------------------ CONFUSION MATRIX CACULATOR --------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------------------
    */

    class Rf_matrix_score{
        // Confusion matrix components
        b_vector<sample_type, 4> tp;
        b_vector<sample_type, 4> fp;
        b_vector<sample_type, 4> fn;

        sample_type total_predict = 0;
        sample_type correct_predict = 0;
        label_type num_labels;
        uint8_t metric_score;

        public:
        // Constructor
        Rf_matrix_score(label_type num_labels, uint8_t metric_score) 
            : num_labels(num_labels), metric_score(metric_score) {
            // Ensure vectors have logical length == num_labels and are zeroed
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (label_type i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }
        
        void init(label_type num_labels, uint8_t metric_score) {
            this->num_labels = num_labels;
            this->metric_score = metric_score;
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (label_type i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }

        // Reset all counters
        void reset() {
            total_predict = 0;
            correct_predict = 0;
            // Reset existing buffers safely; ensure length matches num_labels
            if (tp.size() != num_labels) {
                tp.clear(); tp.reserve(num_labels); for (label_type i = 0; i < num_labels; ++i) tp.push_back(0);
            } else { tp.fill(0); }
            if (fp.size() != num_labels) {
                fp.clear(); fp.reserve(num_labels); for (label_type i = 0; i < num_labels; ++i) fp.push_back(0);
            } else { fp.fill(0); }
            if (fn.size() != num_labels) {
                fn.clear(); fn.reserve(num_labels); for (label_type i = 0; i < num_labels; ++i) fn.push_back(0);
            } else { fn.fill(0); }
        }

        // Update confusion matrix with a prediction
        void update_prediction(label_type actual_label, label_type predicted_label) {
            if(actual_label >= num_labels || predicted_label >= num_labels) return;
            
            total_predict++;
            if(predicted_label == actual_label) {
                correct_predict++;
                tp[actual_label]++;
            } else {
                fn[actual_label]++;
                fp[predicted_label]++;
            }
        }

        // Get precision for all labels
        b_vector<pair<label_type, float>> get_precisions() {
            b_vector<pair<label_type, float>> precisions;
            precisions.reserve(num_labels);
            for(label_type label = 0; label < num_labels; label++) {
                float prec = (tp[label] + fp[label] == 0) ? 0.0f : 
                            static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                precisions.push_back(make_pair(label, prec));
            }
            return precisions;
        }

        // Get recall for all labels
        b_vector<pair<label_type, float>> get_recalls() {
            b_vector<pair<label_type, float>> recalls;
            recalls.reserve(num_labels);
            for(label_type label = 0; label < num_labels; label++) {
                float rec = (tp[label] + fn[label] == 0) ? 0.0f : 
                        static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                recalls.push_back(make_pair(label, rec));
            }
            return recalls;
        }

        // Get F1 scores for all labels
        b_vector<pair<label_type, float>> get_f1_scores() {
            b_vector<pair<label_type, float>> f1s;
            f1s.reserve(num_labels);
            for(label_type label = 0; label < num_labels; label++) {
                float prec = (tp[label] + fp[label] == 0) ? 0.0f : 
                            static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                float rec = (tp[label] + fn[label] == 0) ? 0.0f : 
                        static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                float f1 = (prec + rec == 0.0f) ? 0.0f : 2.0f * prec * rec / (prec + rec);
                f1s.push_back(make_pair(label, f1));
            }
            return f1s;
        }

        // Get accuracy for all labels (overall accuracy for multi-class)
        b_vector<pair<label_type, float>> get_accuracies() {
            b_vector<pair<label_type, float>> accuracies;
            accuracies.reserve(num_labels);
            float overall_accuracy = (total_predict == 0) ? 0.0f : 
                                    static_cast<float>(correct_predict) / total_predict;
            for(label_type label = 0; label < num_labels; label++) {
                accuracies.push_back(make_pair(label, overall_accuracy));
            }
            return accuracies;
        }

        // Calculate combined score based on training flags
        float calculate_score() {
            if(total_predict == 0) {
                RF_DEBUG(1, "❌ No valid predictions found!");
                return 0.0f;
            }

            float combined_result = 0.0f;
            uint8_t numFlags = 0;

            // Calculate accuracy
            if(metric_score & 0x01) { // ACCURACY 
                float accuracy = static_cast<float>(correct_predict) / total_predict;
                RF_DEBUG(2, "Accuracy: ", accuracy);
                combined_result += accuracy;
                numFlags++;
            }

            // Calculate precision
            if(metric_score & 0x02) { // PRECISION 
                float total_precision = 0.0f;
                label_type valid_labels = 0;
                
                for(label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0) {
                        total_precision += static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        valid_labels++;
                    }
                }
                
                float precision = valid_labels > 0 ? total_precision / valid_labels : 0.0f;
                RF_DEBUG(2, "Precision: ", precision);
                combined_result += precision;
                numFlags++;
            }

            // Calculate recall
            if(metric_score & 0x04) { // RECALL 
                float total_recall = 0.0f;
                label_type valid_labels = 0;
                
                for(label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fn[label] > 0) {
                        total_recall += static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        valid_labels++;
                    }
                }
                
                float recall = valid_labels > 0 ? total_recall / valid_labels : 0.0f;
                RF_DEBUG(2, "Recall: ", recall);
                combined_result += recall;
                numFlags++;
            }

            // Calculate F1-Score
            if(metric_score & 0x08) { // F1_SCORE 
                float total_f1 = 0.0f;
                label_type valid_labels = 0;
                
                for(label_type label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0 && tp[label] + fn[label] > 0) {
                        float precision = static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        float recall = static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        if(precision + recall > 0) {
                            float f1 = 2.0f * precision * recall / (precision + recall);
                            total_f1 += f1;
                            valid_labels++;
                        }
                    }
                }
                
                float f1_score = valid_labels > 0 ? total_f1 / valid_labels : 0.0f;
                RF_DEBUG(2, "F1-Score: ", f1_score);
                combined_result += f1_score;
                numFlags++;
            }

            // Return combined score
            return numFlags > 0 ? combined_result / numFlags : 0.0f;
        }

        size_t memory_usage() const {
            size_t usage = 0;
            usage += sizeof(total_predict) + sizeof(correct_predict) + sizeof(num_labels) + sizeof(metric_score);
            usage += tp.size() * sizeof(uint16_t);
            usage += fp.size() * sizeof(uint16_t);
            usage += fn.size() * sizeof(uint16_t);
            return usage;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ TREE_CONTAINER ------------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------------------
    */

    struct NodeToBuild {
        sample_type begin;   // inclusive
        sample_type end;     // exclusive
        uint16_t nodeIndex;
        uint16_t depth;
        
        NodeToBuild() : begin(0), end(0), nodeIndex(0), depth(0) {}
        NodeToBuild(uint16_t idx, sample_type b, sample_type e, uint8_t d) 
            : nodeIndex(idx), begin(b), end(e), depth(d) {}
    };


    class Rf_tree_container{
        private:
            // String model_name;
            const Rf_base* base_ptr = nullptr;
            const Rf_config* config_ptr = nullptr;
            Rf_node_predictor* node_pred_ptr = nullptr;
            char tree_path_buffer[RF_PATH_BUFFER] = {0}; // Buffer for tree file paths

            vector<Rf_tree> trees;        // b_vector storing root nodes of trees (now manages file system file_paths)
            node_layout layout;
            size_t   total_depths;       // store total depth of all trees
            size_t   total_nodes;        // store total nodes of all trees
            size_t   total_leaves;       // store total leaves of all trees
            b_vector<NodeToBuild> queue_nodes; // Queue for breadth-first tree building

            unordered_map<label_type, sample_type> predictClass; // Map to count predictions per class during inference

            bool is_unified = true;  // Default to unified form (used at the end of training and inference)

            void rebuild_tree_slots(uint8_t count, bool reset_storage = true) {
                trees.clear();
                trees.reserve(count);
                for (uint8_t i = 0; i < count; ++i) {
                    Rf_tree tree(i);
                    tree.set_layout(&layout, reset_storage);
                    trees.push_back(std::move(tree));
                }
            }

            void ensure_tree_slot(uint8_t index) {
                if (index < trees.size()) {
                    if (trees[index].layout != &layout) {
                        trees[index].set_layout(&layout);
                    }
                    if (trees[index].index == 255) {
                        trees[index].index = index;
                    }
                    return;
                }
                const size_t desired = static_cast<size_t>(index) + 1;
                trees.reserve(desired);
                while (trees.size() < desired) {
                    uint8_t new_index = static_cast<uint8_t>(trees.size());
                    Rf_tree tree(new_index);
                    tree.set_layout(&layout, true);
                    trees.push_back(std::move(tree));
                }
            }

            inline bool has_base() const { 
                return config_ptr!= nullptr && base_ptr != nullptr && base_ptr->ready_to_use(); 
            }

            uint8_t bits_required(uint32_t max_value) {
                uint8_t bits = 0;
                do {
                    ++bits;
                    max_value >>= 1;
                } while (max_value != 0 && bits < 32);
                return (bits == 0) ? static_cast<uint8_t>(1) : bits;
            }

            void calculate_layout(label_type num_label, uint16_t num_feature, uint16_t max_node){
                const uint32_t fallback_node_index = (RF_MAX_NODES > 0)
                    ? static_cast<uint32_t>(RF_MAX_NODES - 1)
                    : static_cast<uint32_t>(0);

                const uint32_t max_label_id   = (num_label  > 0) ? static_cast<uint32_t>(num_label  - 1) : 0;
                const uint32_t max_feature_id = (num_feature > 0) ? static_cast<uint32_t>(num_feature - 1) : 0;

                uint32_t max_node_index = (max_node   > 0) ? static_cast<uint32_t>(max_node   - 1) : 0;
                if (max_node_index > fallback_node_index) {
                    max_node_index = fallback_node_index;
                }

                uint8_t label_bits       = bits_required(max_label_id);
                uint8_t feature_bits     = bits_required(max_feature_id);
                uint8_t child_index_bits = bits_required(max_node_index);

                if (label_bits > 8) {
                    label_bits = 8;
                }
                if (feature_bits > 10) {
                    feature_bits = 10;
                }
                if (child_index_bits > 10) {
                    child_index_bits = 10;
                }

                layout.set_layout(feature_bits, label_bits, child_index_bits);
            }
            
        public:
            bool is_loaded = false;

            Rf_tree_container(){};
            Rf_tree_container(Rf_base* base, Rf_config* config, Rf_node_predictor* node_pred){
                init(base, config, node_pred);
            }

            void init(Rf_base* base, Rf_config* config, Rf_node_predictor* node_pred){
                base_ptr = base;
                config_ptr = config;
                node_pred_ptr = node_pred;
                if (!config_ptr) {
                    trees.clear();
                    return;
                }

                uint16_t est_nodes = node_pred_ptr
                    ? node_pred_ptr->estimate_nodes(*config_ptr)
                    : static_cast<uint16_t>(RF_MAX_NODES);
                calculate_layout(config_ptr->num_labels, config_ptr->num_features, est_nodes);
                rebuild_tree_slots(config_ptr->num_trees, true);
                predictClass.reserve(config_ptr->num_trees);
                queue_nodes.clear();
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
                is_loaded = false; // Initially in individual form
            }

            ~Rf_tree_container(){
                // save to file system in unified form 
                releaseForest();
                trees.clear();
                base_ptr = nullptr;
                config_ptr = nullptr;
                node_pred_ptr = nullptr;
            }


            // Clear all trees, old forest file and reset state to individual form (ready for rebuilding)
            void clearForest() {
                RF_DEBUG(1, "🧹 Clearing forest..");
                if(!has_base()) {
                    RF_DEBUG(0, "❌ Cannot clear forest: base or config pointer is null.");
                    return;
                }
                for (size_t i = 0; i < trees.size(); i++) {
                    base_ptr->build_tree_file_path(tree_path_buffer, trees[i].index);
                    trees[i].purgeTree(tree_path_buffer); 
                    // Force yield to allow garbage collection
                    yield();        
                    delay(10);
                }
                if (config_ptr) {
                    uint16_t est_nodes = node_pred_ptr
                        ? node_pred_ptr->estimate_nodes(*config_ptr)
                        : static_cast<uint16_t>(RF_MAX_NODES);
                    calculate_layout(config_ptr->num_labels, config_ptr->num_features, est_nodes);
                }
                rebuild_tree_slots(config_ptr->num_trees, true);
                is_loaded = false;
                // Remove old forest file to ensure clean slate
                char oldForestFile[RF_PATH_BUFFER];
                base_ptr->get_forest_path(oldForestFile);
                if(RF_FS_EXISTS(oldForestFile)) {
                    RF_FS_REMOVE(oldForestFile);
                    RF_DEBUG(2, "🗑️ Removed old forest file: ", oldForestFile);
                }
                is_unified = false; // Now in individual form
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
            }
            
            bool add_tree(Rf_tree&& tree){
                if(!tree.isLoaded) RF_DEBUG(2, "🟡 Warning: Adding an unloaded tree to the container.");
                if(tree.index != 255 && tree.index < config_ptr->num_trees) {
                    // tree.set_layout(&layout);
                    // Serial.println("here 3.1");
                    uint8_t index = tree.index;
                    ensure_tree_slot(index);
                    uint16_t d = tree.getTreeDepth();
                    // Serial.println("here 3.3");
                    uint16_t n = tree.countNodes();
                    // Serial.println("here 3.4");
                    uint16_t l = tree.countLeafNodes();
                    // Serial.println("here 3.5");

                    total_depths += d;
                    total_nodes  += n;
                    total_leaves += l;

                    base_ptr->build_tree_file_path(tree_path_buffer, index);
                    // Serial.println("here 3.6");
                    tree.releaseTree(tree_path_buffer); // Release tree nodes from memory after adding to container
                    // Serial.println("here 3.7");
                    // slot = std::move(tree);
                    trees[tree.index] = std::move(tree);
                    RF_DEBUG_2(1, "🌲 Added tree index: ", index, "- nodes: ", n);
                    // slot.set_layout(&layout);
                } else {
                    RF_DEBUG(0, "❌ Invalid tree index: ",tree.index);
                    return false;
                }
                return true;
            }

            label_type predict_features(const packed_vector<8>& features, const b_vector<uint16_t>& thresholds) {
                if(__builtin_expect(trees.empty() || !is_loaded, 0)) {
                    RF_DEBUG(2, "❌ Forest not loaded or empty, cannot predict.");
                    return RF_ERROR_LABEL; // Unknown class
                }
                
                const label_type numLabels = config_ptr->num_labels;
                
                // Use stack array only for small label sets to avoid stack overflow
                // For larger label sets, use heap-allocated map
                if(__builtin_expect(numLabels <= 32, 1)) {
                    // Fast path: small label count - use stack array (32 bytes max)
                    uint8_t votes[32] = {0};
                    
                    const uint8_t numTrees = trees.size();
                    for(uint8_t t = 0; t < numTrees; ++t) {
                        label_type predict = trees[t].predict_features(features, thresholds);
                        if(__builtin_expect(predict < numLabels, 1)) {
                            votes[predict]++;
                        }
                    }
                    
                    uint8_t maxVotes = 0;
                    uint8_t mostPredict = 0;
                    for(uint8_t label = 0; label < numLabels; ++label) {
                        if(votes[label] > maxVotes) {
                            maxVotes = votes[label];
                            mostPredict = label;
                        }
                    }
                    
                    return (maxVotes > 0) ? mostPredict : RF_ERROR_LABEL;
                } else {
                    // Slow path: large label count - use map to avoid stack overflow
                    predictClass.clear();
                    
                    const uint8_t numTrees = trees.size();
                    for(uint8_t t = 0; t < numTrees; ++t) {
                        label_type predict = trees[t].predict_features(features, thresholds);
                        if(__builtin_expect(predict < numLabels, 1)) {
                            predictClass[predict]++;
                        }
                    }
                    
                    uint8_t maxVotes = 0;
                    uint8_t mostPredict = RF_ERROR_LABEL;
                    for(const auto& entry : predictClass) {
                        if(entry.second > maxVotes) {
                            maxVotes = entry.second;
                            mostPredict = entry.first;
                        }
                    }
                    
                    return (maxVotes > 0) ? mostPredict : RF_ERROR_LABEL;
                }
            }

            class iterator {
                using self_type = iterator;
                using value_type = Rf_tree;
                using reference = Rf_tree&;
                using pointer = Rf_tree*;
                using difference_type = std::ptrdiff_t;
                using iterator_category = std::forward_iterator_tag;
                
            public:
                iterator(Rf_tree_container* parent = nullptr, size_t idx = 0) : parent(parent), idx(idx) {}
                reference operator*() const { return parent->trees[idx]; }
                pointer operator->() const { return &parent->trees[idx]; }

                // Prefix ++
                self_type& operator++() { ++idx; return *this; }
                // Postfix ++
                self_type operator++(int) { self_type tmp = *this; ++(*this); return tmp; }

                bool operator==(const self_type& other) const {
                    return parent == other.parent && idx == other.idx;
                }
                bool operator!=(const self_type& other) const {
                    return !(*this == other);
                }

            private:
                Rf_tree_container* parent;
                size_t idx;
            };

            // begin / end to support range-based for and STL-style iteration
            iterator begin() { return iterator(this, 0); }
            iterator end()   { return iterator(this, size()); }

            // Forest loading functionality - dispatcher based on is_unified flag
            bool loadForest() {
                if (is_loaded) {
                    RF_DEBUG(2, "✅ Forest already loaded, skipping load.");
                    return true;
                }
                if(!has_base()) {
                    RF_DEBUG(0, "❌ Base pointer is null", "load forest");
                    return false;
                }
                // Ensure container is properly sized before loading
                if(trees.size() != config_ptr->num_trees) {
                    RF_DEBUG_2(2, "🔧 Adjusting container size from", trees.size(), "to", config_ptr->num_trees);
                    if(config_ptr->num_trees > 0) {
                        ensure_tree_slot(static_cast<uint8_t>(config_ptr->num_trees - 1));
                    } else {
                        trees.clear();
                    }
                }
                // Memory safety check
                size_t freeMemory = ESP.getFreeHeap();
                if(freeMemory < config_ptr->estimatedRAM + 8000) {
                    RF_DEBUG_2(1, "❌ Insufficient memory to load forest (need", 
                                config_ptr->estimatedRAM + 8000, "bytes, have", freeMemory);
                    return false;
                }
                if (is_unified) {
                    return loadForestUnified();
                } else {
                    return loadForestIndividual();
                }
            }

        private:
            bool check_valid_after_load(){
                // Verify trees are actually loaded
                uint8_t loadedTrees = 0;
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
                for(const auto& tree : trees) {
                    if(tree.isLoaded && !tree.nodes.empty()) {
                        loadedTrees++;
                        total_depths += tree.getTreeDepth();
                        total_nodes += tree.countNodes();
                        total_leaves += tree.countLeafNodes();
                    }
                }
                
                if(loadedTrees != config_ptr->num_trees) {
                    RF_DEBUG_2(1, "❌ Loaded trees mismatch: ", loadedTrees, "expected: ", config_ptr->num_trees);
                    is_loaded = false;
                    return false;
                }
                
                is_loaded = true;
                RF_DEBUG_2(1, "✅ Forest loaded(", loadedTrees, "trees). Total nodes:", total_nodes);
                return true;
            }

            // Load forest from unified format (single file containing all trees)
            bool loadForestUnified() {
                char unifiedfile_path[RF_PATH_BUFFER];
                base_ptr->get_forest_path(unifiedfile_path);
                if(unifiedfile_path[0] == '\0' || !RF_FS_EXISTS(unifiedfile_path)) {
                    RF_DEBUG(0, "❌ Unified forest file not found: ", unifiedfile_path);
                    return false;
                }
                
                // Load from unified file (optimized format)
                File file = RF_FS_OPEN(unifiedfile_path, RF_FILE_READ);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to open unified forest file: ", unifiedfile_path);
                    return false;
                }
                
                // Read forest header with error checking
                uint32_t magic;
                if(file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    RF_DEBUG(0, "❌ Failed to read magic number from: ", unifiedfile_path);
                    file.close();
                    return false;
                }
                
                if(magic != 0x464F5253) { // "FORS" 
                    RF_DEBUG(0, "❌ Invalid forest file format (bad magic): ", unifiedfile_path);
                    file.close();
                    return false;
                }
                
                uint8_t treeCount;
                if(file.read((uint8_t*)&treeCount, sizeof(treeCount)) != sizeof(treeCount)) {
                    RF_DEBUG(0, "❌ Failed to read tree count from: ", unifiedfile_path);
                    file.close();
                    return false;
                }
                if(treeCount != config_ptr->num_trees) {
                    RF_DEBUG_2(1, "⚠️ Tree count mismatch in unified file: ", treeCount, "expected: ", config_ptr->num_trees);
                    // config_ptr->num_trees = treeCount; // Adjust expected count
                    file.close();
                    return false;
                }
                RF_DEBUG(1, "📁 Loading from unified forest file", unifiedfile_path);
                
                // Read all trees with comprehensive error checking
                uint8_t successfullyLoaded = 0;
                for(uint8_t i = 0; i < treeCount; i++) {
                    // Memory check during loading
                    if(ESP.getFreeHeap() < 10000) { // 10KB safety buffer
                        RF_DEBUG(1, "⚠️ Insufficient memory during tree loading, stopping.");
                        break;
                    }
                    
                    uint8_t treeIndex;
                    if(file.read((uint8_t*)&treeIndex, sizeof(treeIndex)) != sizeof(treeIndex)) {
                        RF_DEBUG(1, "❌ Failed to read tree index for tree: ", treeIndex);
                        break;
                    }
                    
                    uint32_t nodeCount;
                    if(file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                        RF_DEBUG(1, "❌ Failed to read node count for tree: ", treeIndex);
                        break;
                    }
                    
                    // Validate node count
                    if(nodeCount == 0 || nodeCount > 2047) {
                        RF_DEBUG(1, "❌ Invalid node count for tree: ", treeIndex);
                        // Skip this tree's data
                        file.seek(file.position() + nodeCount * sizeof(uint32_t));
                        continue;
                    }
                    
                    ensure_tree_slot(treeIndex);
                    auto& tree = trees[treeIndex];
                    tree.set_layout(&layout);
                    tree.reset_node_storage(nodeCount);

                    bool nodeReadSuccess = true;
                    uint32_t nodesRead = 0;
                    for(uint32_t j = 0; j < nodeCount; j++) {
                        Tree_node node;
                        if(file.read(reinterpret_cast<uint8_t*>(&node.packed_data), sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                            RF_DEBUG_2(1, "❌ Failed to read node", j, "in tree_", treeIndex);
                            nodeReadSuccess = false;
                            break;
                        }
                        tree.nodes.push_back(node);
                        ++nodesRead;
                    }

                    if(nodeReadSuccess) {
                        tree.nodes.fit();
                        tree.isLoaded = true;
                        successfullyLoaded++;

                        total_depths += tree.getTreeDepth();
                        total_nodes += tree.countNodes();
                        total_leaves += tree.countLeafNodes();
                    } else {
                        const size_t remaining = (nodesRead < nodeCount)
                            ? static_cast<size_t>(nodeCount - nodesRead) * sizeof(uint32_t)
                            : 0;
                        if(remaining > 0) {
                            file.seek(file.position() + remaining);
                        }
                        tree.nodes.clear();
                        tree.nodes.fit();
                        tree.isLoaded = false;
                    }
                }
                
                file.close();
                return check_valid_after_load();
            }

            // Load forest from individual tree files (used during training)
            bool loadForestIndividual() {
                RF_DEBUG(1, "📁 Loading from individual tree files...");
                
                char model_name[RF_PATH_BUFFER];
                base_ptr->get_model_name(model_name, RF_PATH_BUFFER);
                
                uint8_t successfullyLoaded = 0;
                for (auto& tree : trees) {
                    if (!tree.isLoaded) {
                        try {
                            tree.set_layout(&layout);
                            // Construct tree file path
                            base_ptr->build_tree_file_path(tree_path_buffer, tree.index);
                            tree.loadTree(tree_path_buffer);
                            if(tree.isLoaded) successfullyLoaded++;
                        } catch (...) {
                            RF_DEBUG(1, "❌ Exception loading tree: ", tree.index);
                            tree.isLoaded = false;
                        }
                    }
                }
                return check_valid_after_load();
            }

        public:
            // Release forest to unified format (single file containing all trees)
            bool releaseForest() {
                if(!is_loaded || trees.empty()) {
                    RF_DEBUG(2, "✅ Forest is not loaded in memory, nothing to release.");
                    return true; // Nothing to do
                }    
                // Count loaded trees
                uint8_t loadedCount = 0;
                uint32_t totalNodes = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && !tree.nodes.empty()) {
                        loadedCount++;
                        totalNodes += tree.nodes.size();
                    }
                }
                
                if(loadedCount == 0) {
                    RF_DEBUG(1, "❌ No loaded trees to release");
                    is_loaded = false;
                    return false;
                }
                
                // Check available file system space before writing
                size_t totalFS = RF_TOTAL_BYTES();
                size_t usedFS = RF_USED_BYTES();
                size_t freeFS = totalFS - usedFS;
                size_t estimatedSize = totalNodes * sizeof(uint32_t) + 100; // nodes + headers
                
                if(freeFS < estimatedSize) {
                    RF_DEBUG_2(1, "❌ Insufficient file system space to release forest (need ~", 
                                estimatedSize, "bytes, have", freeFS);
                    return false;
                }
                
                // Single file approach - write all trees to unified forest file
                char unifiedfile_path[RF_PATH_BUFFER];
                base_ptr->get_forest_path(unifiedfile_path);
                if(unifiedfile_path[0] == '\0') {
                    RF_DEBUG(0, "❌ Cannot release forest: no base reference for file management");
                    return false;
                }
                
                unsigned long fileStart = GET_CURRENT_TIME_IN_MILLISECONDS;
                File file = RF_FS_OPEN(unifiedfile_path, FILE_WRITE);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to create unified forest file: ", unifiedfile_path);
                    return false;
                }
                
                // Write forest header
                uint32_t magic = 0x464F5253; // "FORS" in hex (forest)
                if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    RF_DEBUG(0, "❌ Failed to write magic number to: ", unifiedfile_path);
                    file.close();
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }
                
                if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
                    RF_DEBUG(0, "❌ Failed to write tree count to: ", unifiedfile_path);
                    file.close();
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }
                
                size_t totalBytes = 0;
                
                // Write all trees in sequence with error checking
                uint8_t savedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && tree.index != 255 && !tree.nodes.empty()) {
                        tree.set_layout(&layout);
                        // Write tree header
                        if(file.write((uint8_t*)&tree.index, sizeof(tree.index)) != sizeof(tree.index)) {
                            RF_DEBUG(1, "❌ Failed to write tree index: ", tree.index);
                            break;
                        }
                        
                        uint32_t nodeCount = tree.nodes.size();
                        if(file.write((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                            RF_DEBUG(1, "❌ Failed to write node count for tree ", tree.index);
                            break;
                        }
                        
                        // Write all nodes with progress tracking
                        bool writeSuccess = true;
                        for (uint32_t i = 0; i < tree.nodes.size(); i++) {
                            const Tree_node node = tree.nodes.get(i);
                            if(file.write(reinterpret_cast<const uint8_t*>(&node.packed_data), sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                RF_DEBUG_2(1, "❌ Failed to write node ", i, "for tree ", tree.index);
                                writeSuccess = false;
                                break;
                            }
                            totalBytes += sizeof(node.packed_data);
                            
                            // Check for memory issues during write
                            if(ESP.getFreeHeap() < 5000) { // 5KB safety threshold
                                RF_DEBUG(1, "⚠️ Low memory during write, stopping.");
                                writeSuccess = false;
                                break;
                            }
                        }
                        
                        if(!writeSuccess) {
                            RF_DEBUG(0, "❌ Failed to save tree ", tree.index);
                            break;
                        }
                        
                        savedCount++;
                    }
                }
                file.close();
                
                // Verify file was written correctly
                if(savedCount != loadedCount) {
                    RF_DEBUG_2(1, "❌ Save incomplete: ", savedCount, "/", loadedCount);
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }
                
                // Only clear trees from RAM after successful save
                uint8_t clearedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded) {
                        tree.nodes.clear();
                        tree.nodes.fit();
                        tree.isLoaded = false;
                        clearedCount++;
                    }
                }
                
                is_loaded = false;
                is_unified = true; // forest always in unified form after first time release
                
                RF_DEBUG_2(1, "✅ Released ", clearedCount, "trees to unified format: ", unifiedfile_path);
                return true;
            }

            void end_training_phase() {
                queue_nodes.clear();
                queue_nodes.fit();
            }

            Rf_tree& operator[](uint8_t index){
                return trees[index];
            }

            node_layout* layout_ptr() {
                return &layout;
            }

            const node_layout* layout_ptr() const {
                return &layout;
            }

            const node_layout& get_layout() const {
                return layout;
            }

            size_t get_total_nodes() const {
                return total_nodes;
            }

            size_t get_total_leaves() const {
                return total_leaves;
            }

            float avg_depth() const {
                return static_cast<float>(total_depths) / config_ptr->num_trees;
            }

            float avg_nodes() const {
                return static_cast<float>(total_nodes) / config_ptr->num_trees;
            }

            float avg_leaves() const {
                return static_cast<float>(total_leaves) / config_ptr->num_trees;
            }

            // Get the number of trees
            size_t size() const {
                if(config_ptr){
                    return config_ptr->num_trees;
                }else{
                    return trees.size();
                }
            }

            uint8_t bits_per_node() const {
                return layout.bits_per_node();
            }

            //  model size in ram 
            size_t size_in_ram() const {     
                size_t size = 0;
                size += sizeof(*this);                           
                size += config_ptr->num_trees * sizeof(Rf_tree);    
                size += (total_nodes * layout.bits_per_node() + 7) / 8;  
                size += predictClass.memory_usage();
                size += queue_nodes.memory_usage();
                return size;
            }

            // Check if container is empty
            bool empty() const {
                return trees.empty();
            }

            // Get queue_nodes for tree building
            b_vector<NodeToBuild>& getQueueNodes() {
                return queue_nodes;
            }

            void set_to_unified_form() {
                is_unified = true;
            }

            void set_to_individual_form() {
                is_unified = false;
            }

            // Get the maximum depth among all trees
            uint16_t max_depth_tree() const {
                uint16_t maxDepth = 0;
                for (const auto& tree : trees) {
                    uint16_t depth = tree.getTreeDepth();
                    if (depth > maxDepth) {
                        maxDepth = depth;
                    }
                }
                return maxDepth;
            }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ RF_PENDING_DATA ------------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------------------
    */

    class Rf_pending_data{
    #ifndef RF_USE_SDCARD
        constexpr static uint16_t MAX_INFER_LOGFILE_SIZE = 2048;   // Max log file size in bytes (1000 inferences)
    #else 
        constexpr static uint16_t MAX_INFER_LOGFILE_SIZE = 20480;  // Max log file size in bytes (10000 inferences)
    #endif
        b_vector<Rf_sample> pending_samples; // buffer for pending samples
        b_vector<label_type> actual_labels; // true labels of the samples
        uint16_t max_pending_samples; // max number of pending samples in buffer

        // interval between 2 inferences. If after this interval the actual label is not provided, the currently labeled waiting sample will be skipped.
        long unsigned max_wait_time; // max wait time for true label in ms 
        long unsigned last_time_received_actual_label;   
        bool first_label_received = false; // flag to indicate if the first actual label has been received 

        const Rf_base* base_ptr = nullptr; // pointer to base data, used for auto-flush
        Rf_config* config_ptr = nullptr; // pointer to config, used for auto-flush

        inline bool ptr_ready() const {
            return base_ptr != nullptr && config_ptr != nullptr && base_ptr->ready_to_use();
        }

        public:
        Rf_pending_data() {
            init(nullptr, nullptr);
        }
        // destructor
        ~Rf_pending_data() {
            base_ptr = nullptr;
            config_ptr = nullptr;
            pending_samples.clear();
            actual_labels.clear();
        }

        void init(Rf_base* base, Rf_config* config){
            base_ptr = base;
            config_ptr = config;
            pending_samples.clear();
            actual_labels.clear();
            set_max_pending_samples(100);
            max_wait_time = 2147483647; // ~24 days
        }

        // add pending sample to buffer, including the label predicted by the model
        void add_pending_sample(const Rf_sample& sample, Rf_data& base_data){
            pending_samples.push_back(sample);
            if(pending_samples.size() > max_pending_samples){
                // Auto-flush if parameters are provided
                if(ptr_ready()){
                    flush_pending_data(base_data);
                } else {
                    pending_samples.clear();
                    actual_labels.clear();
                }
            }
        }

        void add_actual_label(label_type true_label){
            uint16_t ignore_index = (GET_CURRENT_TIME_IN_MILLISECONDS - last_time_received_actual_label) / max_wait_time;
            if(!first_label_received){
                ignore_index = 0;
                first_label_received = true;
            }
            while(ignore_index-- > 0) actual_labels.push_back(RF_ERROR_LABEL); // push error label for ignored samples

            // all pending samples have been labeled, ignore this label
            if(actual_labels.size() >= pending_samples.size()) return;

            actual_labels.push_back(true_label);
            last_time_received_actual_label = GET_CURRENT_TIME_IN_MILLISECONDS;
        }

        void set_max_pending_samples(sample_type max_samples){
            max_pending_samples = max_samples;
        }

        void set_max_wait_time(long unsigned wait_time_ms){
            max_wait_time = wait_time_ms;
        }

        // write valid samples to base_data file
        bool write_to_base_data(Rf_data& base_data){
            if(pending_samples.empty()) {
                RF_DEBUG(1, "⚠️ No pending samples to write to base data");
                return false;
            }
            if (!ptr_ready()) {
                RF_DEBUG(1, "❌ Cannot write to base data: data pointers not ready");
                return false;
            }
            // first scan 
            sample_type valid_samples_count = 0;
            b_vector<Rf_sample> valid_samples;
            for(sample_type i = 0; i < pending_samples.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < RF_ERROR_LABEL) { // Valid actual label provided
                    valid_samples_count++;
                    Rf_sample sample(pending_samples[i].features, actual_labels[i]);
                    valid_samples.push_back(sample);
                }
            }
            
            if(valid_samples_count == 0) {
                return false; // No valid samples to add
            }

            auto deleted_labels = base_data.addNewData(valid_samples, config_ptr->extend_base_data);

            // update config 
            if(config_ptr->extend_base_data) {
                config_ptr->num_samples += valid_samples_count;
                if(config_ptr->num_samples > RF_MAX_SAMPLES) 
                    config_ptr->num_samples = RF_MAX_SAMPLES;
            }

            for(sample_type i = 0; i < pending_samples.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < RF_ERROR_LABEL) { // Valid actual label provided
                    config_ptr->samples_per_label[actual_labels[i]]++;
                }
            }

            for(auto& lbl : deleted_labels) {
                if(lbl < RF_ERROR_LABEL && lbl < config_ptr->num_labels && config_ptr->samples_per_label[lbl] > 0) {
                    config_ptr->samples_per_label[lbl]--;
                }
            }
            char data_path[RF_PATH_BUFFER];
            base_ptr->get_base_data_path(data_path);
            RF_DEBUG_2(1, "✅ Added", valid_samples_count, "new samples to base data", data_path);
            return true;
        }

        // Write prediction which given actual label (0 < actual_label < RF_ERROR_LABEL) to the inference log file
        bool write_to_infer_log(){
            if(pending_samples.empty()) return false;
            if(!ptr_ready()){
                RF_DEBUG(1, "❌ Cannot write to inference log: data pointers not ready");
                return false;
            };
            char infer_log_path[RF_PATH_BUFFER];
            base_ptr->get_infer_log_path(infer_log_path);
            if(infer_log_path[0] == '\0') {
                RF_DEBUG(1, "❌ Cannot write to inference log: no base reference for file management");
                return false;
            }
            bool file_exists = RF_FS_EXISTS(infer_log_path);
            uint32_t current_prediction_count = 0;
            
            // If file exists, read current prediction count from header
            if(file_exists) {
                File read_file = RF_FS_OPEN(infer_log_path, RF_FILE_READ);
                if(read_file && read_file.size() >= 8) {
                    uint8_t magic_bytes[4];
                    read_file.read(magic_bytes, 4);
                    // Verify magic number
                    if(magic_bytes[0] == 0x49 && magic_bytes[1] == 0x4E && 
                       magic_bytes[2] == 0x46 && magic_bytes[3] == 0x4C) {
                        read_file.read((uint8_t*)&current_prediction_count, 4);
                    }
                }
                read_file.close();
            }
            
            File file = RF_FS_OPEN(infer_log_path, file_exists ? FILE_APPEND : FILE_WRITE);
            if(!file) {
                RF_DEBUG(1, "❌ Failed to open inference log file: ", infer_log_path);
                return false;
            }
            
            // Write header if new file
            if(!file_exists) {
                // Magic number (4 bytes): 'I', 'N', 'F', 'L' (INFL)
                uint8_t magic_bytes[4] = {0x49, 0x4E, 0x46, 0x4C};
                size_t written = file.write(magic_bytes, 4);
                if(written != 4) {
                    RF_DEBUG(1, "❌ Failed to write magic number to inference log");
                }
                
                // Write initial prediction count (4 bytes)
                uint32_t initial_count = 0;
                written = file.write((uint8_t*)&initial_count, 4);
                if(written != 4) {
                    RF_DEBUG(1, "❌ Failed to write initial prediction count to inference log");
                }
                
                file.flush();
            }
            
            // Collect and write prediction pairs for valid samples
            b_vector<label_type> prediction_pairs;
            uint32_t new_predictions = 0;
            
            for(sample_type i = 0; i < pending_samples.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] != RF_ERROR_LABEL) { // Valid actual label provided
                    label_type predicted_label = pending_samples[i].label;
                    label_type actual_label = actual_labels[i];
                    
                    // Write predicted_label followed by actual_label
                    prediction_pairs.push_back(predicted_label);
                    prediction_pairs.push_back(actual_label);
                    new_predictions++;
                }
            }
            
            if(!prediction_pairs.empty()) {
                // Write prediction pairs to end of file
                size_t written = file.write(prediction_pairs.data(), prediction_pairs.size());
                if(written != prediction_pairs.size()) {
                    RF_DEBUG_2(1, "❌ Failed to write all prediction pairs to inference log: ", 
                                 written, "/", prediction_pairs.size());
                }
                
                file.flush();
                file.close();
                
                // Update prediction count in header - read entire file and rewrite
                File read_file = RF_FS_OPEN(infer_log_path, RF_FILE_READ);
                if(read_file) {
                    size_t file_size = read_file.size();
                    b_vector<uint8_t> file_data(file_size);
                    read_file.read(file_data.data(), file_size);
                    read_file.close();
                    
                    // Update prediction count in the header (bytes 4-7)
                    uint32_t updated_count = current_prediction_count + new_predictions;
                    memcpy(&file_data[4], &updated_count, 4);
                    
                    // Write back the entire file
                    File write_file = RF_FS_OPEN(infer_log_path, FILE_WRITE);
                    if(write_file) {
                        write_file.write(file_data.data(), file_data.size());
                        write_file.flush();
                        write_file.close();

                        RF_DEBUG_2(1, "✅ Added", new_predictions, "prediction pairs to log: ", updated_count);
                    }
                }
            } else {
                file.close();
            }
            // Trim file if it exceeds max size
            return trim_log_file(infer_log_path);
        }

        // Public method to flush pending data when buffer is full or on demand
        void flush_pending_data(Rf_data& base_data) {
            if(pending_samples.empty()) return;
            
            write_to_base_data(base_data);
            write_to_infer_log();
            
            // Clear buffers after processing
            pending_samples.clear();
            actual_labels.clear();
        }

    private:
        // trim log file if it exceeds max size (MAX_INFER_LOGFILE_SIZE)
        bool trim_log_file(const char* infer_log_path) {
            if(!RF_FS_EXISTS(infer_log_path)) return false;
            
            File file = RF_FS_OPEN(infer_log_path, RF_FILE_READ);
            if(!file) return false;
            
            size_t file_size = file.size();
            file.close();
            
            if(file_size <= MAX_INFER_LOGFILE_SIZE) return true; // No trimming needed;
            
            // File is too large, trim from the beginning (keep most recent data)
            file = RF_FS_OPEN(infer_log_path, RF_FILE_READ);
            if(!file) return false;
            
            // Read and verify header
            uint8_t magic_bytes[4];
            uint32_t total_predictions;
            
            if(file.read(magic_bytes, 4) != 4 || 
               magic_bytes[0] != 0x49 || magic_bytes[1] != 0x4E || 
               magic_bytes[2] != 0x46 || magic_bytes[3] != 0x4C) {
                file.close();
                RF_DEBUG(1, "❌ Invalid magic number in infer log file: ", infer_log_path);
                return false;
            }
            
            if(file.read((uint8_t*)&total_predictions, 4) != 4) {
                file.close();
                RF_DEBUG(1, "❌ Failed to read prediction count from infer log file: ", infer_log_path);
                return false;
            }
            
            size_t header_size = 8; // magic (4) + prediction_count (4)
            size_t data_size = file_size - header_size;
            size_t prediction_pairs_count = data_size / 2; // Each prediction is 2 bytes (predicted + actual)
            
            // Calculate how many prediction pairs to keep
            size_t max_data_size = MAX_INFER_LOGFILE_SIZE - header_size;
            size_t max_pairs_to_keep = max_data_size / 2;
            
            if(prediction_pairs_count <= max_pairs_to_keep) {
                file.close();
                return true; // No trimming needed
            }
            
            // Keep the most recent prediction pairs
            size_t pairs_to_keep = max_pairs_to_keep / 2; // Keep half to allow room for growth
            size_t pairs_to_skip = prediction_pairs_count - pairs_to_keep;
            size_t bytes_to_skip = pairs_to_skip * 2;
            
            // Skip to the position we want to keep from
            file.seek(header_size + bytes_to_skip);
            
            // Read remaining prediction pairs
            size_t remaining_data_size = pairs_to_keep * 2;
            b_vector<uint8_t> remaining_data(remaining_data_size);
            size_t bytes_read = file.read(remaining_data.data(), remaining_data_size);
            file.close();
            
            if(bytes_read != remaining_data_size) {
                RF_DEBUG(1, "❌ Failed to read remaining data from infer log file: ", infer_log_path);
                return false;
            }
            
            // Rewrite file with header and trimmed data
            file = RF_FS_OPEN(infer_log_path, FILE_WRITE);
            if(!file) {
                RF_DEBUG(1, "❌ Failed to reopen log file for writing: ", infer_log_path);
                return false;
            }
            
            // Write header with updated prediction count
            file.write(magic_bytes, 4);
            uint32_t new_prediction_count = pairs_to_keep;
            file.write((uint8_t*)&new_prediction_count, 4);
            
            // Write remaining prediction pairs
            file.write(remaining_data.data(), remaining_data.size());
            file.flush();
            file.close();
            return true;
        }

    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ RF_LOGGER -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef struct time_anchor{
        long unsigned anchor_time;
        uint16_t index;
    };

    class Rf_logger {
        char time_log_path[RF_PATH_BUFFER] = {'\0'};
        char memory_log_path[RF_PATH_BUFFER] = {'\0'};
        b_vector<time_anchor> time_anchors;
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        long unsigned starting_time;
        uint8_t fragmentation;
        uint32_t lowest_ram;
        uint32_t lowest_rom; 
        uint32_t freeDisk;
        float log_time;

    
    public:
        Rf_logger() : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
        }

        Rf_logger(Rf_base* base, bool keep_old_file = false) : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
            init(base, keep_old_file);
        }
        
        void init(Rf_base* base, bool keep_old_file = false){
            RF_DEBUG(2, "🔧 Initializing logger");
            time_anchors.clear();
            starting_time = GET_CURRENT_TIME_IN_MILLISECONDS;
            drop_anchor(); // initial anchor at index 0

            lowest_ram = UINT32_MAX;
            lowest_rom = UINT32_MAX;

            base->get_time_log_path(this->time_log_path);
            base->get_memory_log_path(this->memory_log_path);

            if(time_log_path[0] == '\0' || memory_log_path[0] == '\0'){
                RF_DEBUG(1, "❌ Cannot init logger: log file paths not set correctly");
                return;
            }

            if(!keep_old_file){
                if(RF_FS_EXISTS(time_log_path)){
                    RF_FS_REMOVE(time_log_path); 
                }
                // write header to time log file
                File logFile = RF_FS_OPEN(time_log_path, FILE_WRITE);
                if (logFile) {
                    logFile.println("Event,\t\tTime(ms),duration,Unit");
                    logFile.close();
                }
            }
            t_log("init tracker"); // Initial log without printing

            if(!keep_old_file){                
                // clear file system log file if it exists
                if(RF_FS_EXISTS(memory_log_path)){
                    RF_FS_REMOVE(memory_log_path); 
                }
                // write header to log file
                File logFile = RF_FS_OPEN(memory_log_path, FILE_WRITE);
                if (logFile) {
                    logFile.println("Time(s),FreeHeap,Largest_Block,FreeDisk");
                    logFile.close();
                } 
            }
            m_log("init tracker", true); // Initial log without printing
        }

        void m_log(const char* msg, bool log = true){
        // #ifndef RF_PSRAM_AVAILABLE
            freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        // #else
        //     if(esp_psram_is_initialized()){
        //         freeHeap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        //         largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        //     } else {
        //         freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        //         largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        //     }

        // #endif
            freeDisk = RF_TOTAL_BYTES() - RF_USED_BYTES();

            if(freeHeap < lowest_ram) lowest_ram = freeHeap;
            if(freeDisk < lowest_rom) lowest_rom = freeDisk;

            fragmentation = 100 - (largestBlock * 100 / freeHeap);

            // Log to file with timestamp
            if(log) {        
                log_time = (GET_CURRENT_TIME_IN_MILLISECONDS - starting_time)/1000.0f; 
                File logFile = RF_FS_OPEN(memory_log_path, FILE_APPEND);
                if (logFile) {
                    logFile.printf("%.2f,\t%u,\t%u,\t%u",
                                    log_time, freeHeap, largestBlock, freeDisk);
                    if(msg && strlen(msg) > 0){
                        logFile.printf(",\t%s\n", msg);
                    } else {
                        logFile.println();
                    }
                    logFile.close();
                } else RF_DEBUG(1, "❌ Failed to open memory log file for appending: ", memory_log_path);
            }
        }

        // fast log : just for measure and update : lowest ram and fragmentation
        void m_log(){
            m_log("", false);
        }
        
        uint16_t drop_anchor(){
            time_anchor anchor;
            anchor.anchor_time = GET_CURRENT_TIME_IN_MILLISECONDS;
            anchor.index = time_anchors.size();
            time_anchors.push_back(anchor);
            return anchor.index;
        }

        uint16_t current_anchor() const {
            return time_anchors.size() > 0 ? time_anchors.back().index : 0;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_logger);
            return total;
        }

        // for durtion measurement between two anchors
        long unsigned t_log(const char* msg, size_t begin_anchor_index, size_t end_anchor_idex, const char* unit = "ms"){
            float ratio = 1;  // default to ms 
            if(strcmp(unit, "s") == 0 || strcmp(unit, "second") == 0) ratio = 1000.0f;
            else if(strcmp(unit, "us") == 0 || strcmp(unit, "microsecond") == 0) ratio = 0.001f;

            if(time_anchors.size() == 0) return 0; // no anchors set
            if(begin_anchor_index >= time_anchors.size() || end_anchor_idex >= time_anchors.size()) return 0; // invalid index
            if(end_anchor_idex <= begin_anchor_index) {
                std::swap(begin_anchor_index, end_anchor_idex);
            }

            long unsigned begin_time = time_anchors[begin_anchor_index].anchor_time;
            long unsigned end_time = time_anchors[end_anchor_idex].anchor_time;
            float elapsed = (end_time - begin_time)/ratio;

            // Log to file with timestamp      ; 
            File logFile = RF_FS_OPEN(time_log_path, FILE_APPEND);
            if (logFile) {
                if(msg && strlen(msg) > 0){
                    logFile.printf("%s,\t%.1f,\t%.2f,\t%s\n", msg, begin_time/1000.0f, elapsed, unit);     // time always in s
                } else {
                    if(ratio > 1.1f)
                        logFile.printf("unknown event,\t%.1f,\t%.2f,\t%s\n", begin_time/1000.0f, elapsed, unit); 
                    else 
                        logFile.printf("unknown event,\t%.1f,\t%lu,\t%s\n", begin_time/1000.0f, (long unsigned)elapsed, unit);
                }
                logFile.close();
            }else{
                RF_DEBUG(1, "❌ Failed to open time log file: ", time_log_path);
            }

            time_anchors[end_anchor_idex].anchor_time = GET_CURRENT_TIME_IN_MILLISECONDS; // reset end anchor to current time
            return (long unsigned)elapsed;
        }
    
        /**
         * @brief for duration measurement from an anchor to now
         * @param msg name of the event
         * @param begin_anchor_index index of the begin anchor
         * @param unit time unit, "ms" (default), "s", "us" 
         * @param print whether to print to // Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will create a new anchor at the current time
         */
        long unsigned t_log(const char* msg, size_t begin_anchor_index, const char* unit = "ms"){
            time_anchor end_anchor;
            end_anchor.anchor_time = GET_CURRENT_TIME_IN_MILLISECONDS;
            end_anchor.index = time_anchors.size();
            time_anchors.push_back(end_anchor);
            return t_log(msg, begin_anchor_index, end_anchor.index, unit);
        }

        /**
         * @brief log time from starting point to now
         * @param msg name of the event
         * @param print whether to print to // Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will NOT create a new anchor
         */
        long unsigned t_log(const char* msg){
            long unsigned current_time = GET_CURRENT_TIME_IN_MILLISECONDS - starting_time;

            // Log to file with timestamp
            File logFile = RF_FS_OPEN(time_log_path, FILE_APPEND);
            if (logFile) {
                if(msg && strlen(msg) > 0){
                    logFile.printf("%s,\t%.1f,\t_,\tms\n", msg, current_time/1000.0f); // time always in s
                } else {
                    logFile.printf("unknown event,\t%.1f,\t_,\tms\n", current_time/1000.0f); // time always in s
                }
                logFile.close();
            }else{
                RF_DEBUG(1, "❌ Failed to open time log file: ", time_log_path);
            }
            return current_time;
        }
        
        // print out memory_log file to // Serial
        void print_m_log(){
            if(memory_log_path[0] == '\0'){
                RF_DEBUG(1, "❌ Cannot print memory log: log file path not set correctly");
                return;
            }
            if(!RF_FS_EXISTS(memory_log_path)){
                RF_DEBUG(1, "❌ Cannot print memory log: log file does not exist");
                return;
            }
            File file = RF_FS_OPEN(memory_log_path, RF_FILE_READ);
            if(!file){
                RF_DEBUG(1, "❌ Cannot open memory log file for reading: ", memory_log_path);
                return;
            }
            String line;
            while(file.available()){
                line = file.readStringUntil('\n');
                RF_DEBUG(0, line.c_str());
            }
            file.close();
        }

        // print out time_log file to // Serial
        void print_t_log(){
            if(time_log_path[0] == '\0'){
                RF_DEBUG(1, "❌ Cannot print time log: log file path not set correctly");
                return;
            }
            if(!RF_FS_EXISTS(time_log_path)){
                RF_DEBUG(1, "❌ Cannot print time log: log file does not exist");
                return;
            }
            File file = RF_FS_OPEN(time_log_path, RF_FILE_READ);
            if(!file){
                RF_DEBUG(1, "❌ Cannot open time log file for reading: ", time_log_path);
                return;
            }
            String line;
            while(file.available()){
                line = file.readStringUntil('\n');
                RF_DEBUG(0, line.c_str());
            }
            file.close();
        }
    };

} // namespace mcu