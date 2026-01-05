#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Rf_base {
    private:
        mutable uint16_t flags = 0; // flags indicating the status of member files
        char model_name[RF_PATH_BUFFER] ={0};
        
    public:
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
                build_file_path(filepath, "_nml.csv");
                if (RF_FS_EXISTS(filepath)) {
                    eml_debug(1, "🔄 Found csv dataset, need to be converted to binary format before use.");
                    flags |= static_cast<Rf_base_flags>(BASE_DATA_IS_CSV);
                }else{
                    eml_debug(0, "❌ No base data file found: ", filepath);
                    this->model_name[0] = '\0';
                    return;
                }
            } else {
                eml_debug(1, "✅ Found base data file: ", filepath);
                flags |= static_cast<Rf_base_flags>(BASE_DATA_EXIST);
            }

            // check : quantizer file exists
            build_file_path(filepath, "_qtz.bin");
            if (RF_FS_EXISTS(filepath)) {
                eml_debug(1, "✅ Found quantizer file: ", filepath);
                flags |= static_cast<Rf_base_flags>(CTG_FILE_EXIST);
            } else {
                eml_debug(0, "❌ No quantizer file found: ", filepath);
                this->model_name[0] = '\0';
                return;
            }
            
            // check : dp file exists
            build_file_path(filepath, "_dp.csv");
            if (RF_FS_EXISTS(filepath)) {
                eml_debug(1, "✅ Found data_params file: ", filepath);
                flags |= static_cast<Rf_base_flags>(DP_FILE_EXIST);
            } else {
                eml_debug(1, "⚠️ No data_params file found: ", filepath);
                eml_debug(1, "🔂 Dataset will be scanned, which may take time...🕒");
            }

            // check : config file exists
            build_file_path(filepath, "_config.json");
            if (RF_FS_EXISTS(filepath)) {
                eml_debug(1, "✅ Found config file: ", filepath);
                flags |= static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            } else {
                eml_debug(1, "⚠️ No config file found: ", filepath);
                eml_debug(1, "🔂 Switching to manual configuration");
            }
            
            // check : forest file exists (unified form)
            build_file_path(filepath, "_forest.bin");
            if (RF_FS_EXISTS(filepath)) {
                eml_debug(1, "✅ Found unified forest model file: ", filepath);
                flags |= static_cast<Rf_base_flags>(UNIFIED_FOREST_EXIST);
            } else {
                eml_debug(2, "⚠️ No unified forest model file found");
            }

            // check : node predictor file exists
            build_file_path(filepath, "_npd.bin");
            if (RF_FS_EXISTS(filepath)) {
                eml_debug(1, "✅ Found node predictor file: ", filepath);
                flags |= static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            } else {
                eml_debug(2, "⚠️ No node predictor file found: ", filepath);
                eml_debug(2, "🔂 Switching to use default node_predictor");
            }

            // able to inference : forest file + quantizer
            if ((flags & UNIFIED_FOREST_EXIST) && (flags & CTG_FILE_EXIST)) {
                flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                eml_debug(1, "✅ Model is ready for inference.");
            } else {
                eml_debug(0, "⚠️ Model is NOT ready for inference.");
            }

            // able to re-training : base data + quantizer 
            if ((flags & BASE_DATA_EXIST) && (flags & CTG_FILE_EXIST)) {
                flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                eml_debug(1, "✅ Model is ready for re-training.");
            } else {
                eml_debug(0, "⚠️ Model is NOT ready for re-training.");
            }
            flags |= static_cast<Rf_base_flags>(SCANNED);
        }
        
    public:
        void init(const char* name) {
            eml_debug(1, "🔧 Initializing model resource manager");
            if (!name || strlen(name) == 0) {
                eml_debug(0, "❌ Model name is empty. The process is aborted.");
                return;
            }
            strncpy(this->model_name, name, RF_PATH_BUFFER - 1);
            this->model_name[RF_PATH_BUFFER - 1] = '\0';
            scan_current_resource();
        }

        void update_resource_status() {
            eml_debug(1, "🔄 Updating model resource status");
            if (this->model_name[0] == '\0') {
                eml_debug(0, "❌ Model name is empty. Cannot update resource status.");
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

        inline void get_qtz_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_qtz.bin", buffer_size); }

        inline void get_infer_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_ifl.bin", buffer_size); }

        inline void get_config_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_config.json", buffer_size); }

        inline void get_node_pred_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_npd.bin", buffer_size); }

        inline void get_node_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_nlg.csv", buffer_size); }

        inline void get_forest_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_forest.bin", buffer_size); }

        inline void get_time_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_tlog.csv", buffer_size); }

        inline void get_memory_log_path(char* buffer, int buffer_size = RF_PATH_BUFFER ) 
                        const { build_file_path(buffer, "_mlog.csv", buffer_size); }
        
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
                rename_file("_qtz.bin");       // quantizer file
                rename_file("_ifl.bin"); // inference log file
                rename_file("_npd.bin"); // node predictor file
                rename_file("_nlg.bin");  // node predict log file
                rename_file("_config.json");   // config file
                rename_file("_mlog.csv");// memory log file
                rename_file("_tlog.csv");  // time log file

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

} // namespace eml
