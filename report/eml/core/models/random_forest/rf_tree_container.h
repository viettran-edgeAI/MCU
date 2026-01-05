#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Eml_tree_container{
        private:
            // String model_name;
            const Rf_base* base_ptr = nullptr;
            Rf_config* config_ptr = nullptr;
            Rf_node_predictor* node_pred_ptr = nullptr;
            char tree_path_buffer[RF_PATH_BUFFER] = {0}; // Buffer for tree file paths

            vector<Eml_tree> trees;        // stores tree slots and manages file system file_paths
            node_resource resources;
            size_t   total_depths;       // store total depth of all trees
            rf_node_type   total_nodes;        // store total nodes of all trees
            rf_node_type   total_leaves;       // store total leaves of all trees
            vector<NodeToBuild> queue_nodes; // Queue for breadth-first tree building
            unordered_map_s<rf_label_type, rf_sample_type> predictClass; // Map to count predictions per class during inference
            bool is_unified = true;  // Default to unified form (used at the end of training and inference)

            inline bool has_base() const { 
                return config_ptr!= nullptr && base_ptr != nullptr && base_ptr->ready_to_use(); 
            }

            void rebuild_tree_slots(uint8_t count, bool reset_storage = true) {
                trees.clear();
                trees.reserve(count);
                for (uint8_t i = 0; i < count; ++i) {
                    Eml_tree tree(i);
                    tree.set_resource(&resources, reset_storage);
                    trees.push_back(std::move(tree));
                }
            }

            void ensure_tree_slot(uint8_t index) {
                if (index < trees.size()) {
                    if (trees[index].resource != &resources) {
                        trees[index].set_resource(&resources);
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
                    Eml_tree tree(new_index);
                    tree.set_resource(&resources, true);
                    trees.push_back(std::move(tree));
                }
            }

            uint8_t bits_required(uint32_t max_value) {
                uint8_t bits = 0;
                do {
                    ++bits;
                    max_value >>= 1;
                } while (max_value != 0 && bits < 32);
                return (bits == 0) ? static_cast<uint8_t>(1) : bits;
            }

        public:
            void calculate_layout(rf_label_type num_label, uint16_t num_feature, rf_node_type max_node){
                const rf_node_type fallback_node_index = 8191;  // safety fallback 13 bits 

                const rf_label_type max_label_id   = (num_label  > 0) ? static_cast<rf_label_type>(num_label  - 1) : 0;
                const uint32_t max_feature_id = (num_feature > 0) ? static_cast<uint32_t>(num_feature - 1) : 0;

                rf_node_type max_node_index = (max_node   > 0) ? static_cast<uint32_t>(max_node   - 1) : 0;
                if (max_node_index > fallback_node_index) {
                    max_node_index = fallback_node_index;
                }

                uint8_t label_bits       = bits_required(max_label_id);
                uint8_t feature_bits     = bits_required(max_feature_id);
                uint8_t child_index_bits = bits_required(max_node_index);
                uint8_t threshold_bits   = config_ptr ? config_ptr->quantization_coefficient : 1;
                if (threshold_bits < 1) {
                    threshold_bits = 1;
                } else if (threshold_bits > 8) {
                    threshold_bits = 8;
                }

                if (label_bits > 8) {
                    label_bits = 8;
                }
                if (feature_bits > 10) {
                    feature_bits = 10;
                }

                const uint8_t max_child_bits_limit = bits_required(fallback_node_index);

                auto compute_available_child_bits = [&](uint8_t tb) -> uint8_t {
                    if ((1 + tb + feature_bits + label_bits) >= 32) {
                        return static_cast<uint8_t>(0);
                    }
                    return static_cast<uint8_t>(32 - (1 + tb + feature_bits + label_bits));
                };

                uint8_t desired_child_bits = child_index_bits;
                if (desired_child_bits > max_child_bits_limit) {
                    desired_child_bits = max_child_bits_limit;
                }
                if (desired_child_bits == 0) {
                    desired_child_bits = 1;
                }

                uint8_t available_child_bits = compute_available_child_bits(threshold_bits);
                while (threshold_bits > 1 && available_child_bits < desired_child_bits) {
                    --threshold_bits;
                    available_child_bits = compute_available_child_bits(threshold_bits);
                }
                if (available_child_bits == 0) {
                    threshold_bits = 1;
                    available_child_bits = compute_available_child_bits(threshold_bits);
                }

                if (config_ptr && threshold_bits < config_ptr->quantization_coefficient) {
                    eml_debug_2(2, "⚙️ Adjusted threshold bits from ", static_cast<int>(config_ptr->quantization_coefficient),
                             " to ", static_cast<int>(threshold_bits));
                }

                const uint8_t max_child_bits_word = available_child_bits;

                if (max_child_bits_word == 0) {
                    child_index_bits = 1;
                } else {
                    if (child_index_bits > max_child_bits_word) {
                        child_index_bits = max_child_bits_word;
                    }
                    if (child_index_bits > max_child_bits_limit) {
                        child_index_bits = max_child_bits_limit;
                    }
                    if (child_index_bits == 0) {
                        child_index_bits = 1;
                    }
                }

                eml_debug(1, "📐 Calculated node resources :");
                eml_debug(1, "   - Threshold bits : ", static_cast<int>(threshold_bits));
                eml_debug(1, "   - Feature bits   : ", static_cast<int>(feature_bits));
                eml_debug(1, "   - Label bits     : ", static_cast<int>(label_bits));
                eml_debug(1, "   - Child index bits: ", static_cast<int>(child_index_bits));

                resources.set_bits(feature_bits, label_bits, child_index_bits, threshold_bits);
            }
            
            bool is_loaded = false;

            Eml_tree_container(){};
            Eml_tree_container(Rf_base* base, Rf_config* config, Rf_node_predictor* node_pred){
                init(base, config, node_pred);
            }

            bool init(Rf_base* base, Rf_config* config, Rf_node_predictor* node_pred){
                base_ptr = base;
                config_ptr = config;
                node_pred_ptr = node_pred;
                if (!config_ptr) {
                    trees.clear();
                    eml_debug(0, "❌ Cannot initialize tree container: config pointer is null.");
                    return false;
                }
                
                // Check if layout bits are provided in config 
                if (config_ptr->threshold_bits > 0 && config_ptr->feature_bits > 0 && 
                    config_ptr->label_bits > 0 && config_ptr->child_bits > 0) {
                    eml_debug(2, "📐 Setting node layout from config file");
                    resources.set_bits(config_ptr->feature_bits, config_ptr->label_bits,
                                       config_ptr->child_bits, config_ptr->threshold_bits);
                } else {
                    eml_debug(0, "❌ Cannot initialize tree container: layout bits missing in config.");
                    return false; 
                }

                rebuild_tree_slots(config_ptr->num_trees, true);
                predictClass.reserve(config_ptr->num_trees);
                queue_nodes.clear();
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
                is_loaded = false; // Initially in individual form
                return true;
            }
            
            ~Eml_tree_container(){
                // save to file system in unified form 
                releaseForest();
                trees.clear();
                base_ptr = nullptr;
                config_ptr = nullptr;
                node_pred_ptr = nullptr;
            }


            // Clear all trees, old forest file and reset state to individual form (ready for rebuilding)
            void clearForest() {
                eml_debug(1, "🧹 Clearing forest..");
                if(!has_base()) {
                    eml_debug(0, "❌ Cannot clear forest: base or config pointer is null.");
                    return;
                }
                for (size_t i = 0; i < trees.size(); i++) {
                    base_ptr->build_tree_file_path(tree_path_buffer, trees[i].index);
                    trees[i].purgeTree(tree_path_buffer); 
                    yield();        
                    delay(10);
                }
                if (config_ptr) {
                    // Use predictor only if it's trained; otherwise use safe default of 2046 nodes
                    uint16_t est_nodes;
                    if (node_pred_ptr && node_pred_ptr->is_trained) {
                        est_nodes = node_pred_ptr->estimate_nodes(*config_ptr);
                    } else {
                        eml_debug(2, "⚠️ Node predictor not available or not trained, using safe default for layout calculation.");
                        est_nodes = static_cast<uint16_t>(2046); // Safe default when predictor not available/trained
                    }
                    calculate_layout(config_ptr->num_labels, config_ptr->num_features, est_nodes);
                }
                rebuild_tree_slots(config_ptr->num_trees, true);
                is_loaded = false;
                // Remove old forest file to ensure clean slate
                char oldForestFile[RF_PATH_BUFFER];
                base_ptr->get_forest_path(oldForestFile);
                if(RF_FS_EXISTS(oldForestFile)) {
                    RF_FS_REMOVE(oldForestFile);
                    eml_debug(2, "🗑️ Removed old forest file: ", oldForestFile);
                }
                is_unified = false; // Now in individual form
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
            }
            
            bool add_tree(Eml_tree&& tree){
                if(!tree.isLoaded) eml_debug(2, "🟡 Warning: Adding an unloaded tree to the container.");
                if(tree.index != 255 && tree.index < config_ptr->num_trees) {
                    uint8_t index = tree.index;
                    ensure_tree_slot(index);
                    uint16_t d = tree.getTreeDepth();
                    uint16_t n = tree.countNodes();
                    uint16_t l = tree.countLeafNodes();

                    total_depths += d;
                    total_nodes  += n;
                    total_leaves += l;

                    base_ptr->build_tree_file_path(tree_path_buffer, index);
                    // Ensure the tree has access to resource before saving.
                    tree.set_resource(&resources);
                    tree.releaseTree(tree_path_buffer); // Release tree nodes from memory after adding to container
                    trees[tree.index] = std::move(tree);
                    eml_debug_2(1, "🌲 Added tree index: ", index, "- nodes: ", n);
                    // slot.set_layout(&layout);
                } else {
                    eml_debug(0, "❌ Invalid tree index: ",tree.index);
                    return false;
                }
                return true;
            }

            rf_label_type predict_features(const packed_vector<8>& features) {
                if(__builtin_expect(trees.empty() || !is_loaded, 0)) {
                    eml_debug(2, "❌ Forest not loaded or empty, cannot predict.");
                    return RF_ERROR_LABEL; // Unknown class
                }
                
                const rf_label_type numLabels = config_ptr->num_labels;
                
                // Use stack array only for small label sets to avoid stack overflow
                // For larger label sets, use heap-allocated map
                if(__builtin_expect(numLabels <= 32, 1)) {
                    // Fast path: small label count - use stack array (32 bytes max)
                    uint8_t votes[32] = {0};
                    
                    const uint8_t numTrees = trees.size();
                    for(uint8_t t = 0; t < numTrees; ++t) {
                        rf_label_type predict = trees[t].predict_features(features);
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
                        rf_label_type predict = trees[t].predict_features(features);
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
                using value_type = Eml_tree;
                using reference = Eml_tree&;
                using pointer = Eml_tree*;
                using difference_type = std::ptrdiff_t;
                using iterator_category = std::forward_iterator_tag;
                
            public:
                iterator(Eml_tree_container* parent = nullptr, size_t idx = 0) : parent(parent), idx(idx) {}
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
                Eml_tree_container* parent;
                size_t idx;
            };

            // begin / end to support range-based for and STL-style iteration
            iterator begin() { return iterator(this, 0); }
            iterator end()   { return iterator(this, size()); }

            // Forest loading functionality - dispatcher based on is_unified flag
            bool loadForest() {
                if (is_loaded) {
                    eml_debug(2, "✅ Forest already loaded, skipping load.");
                    return true;
                }
                if(!has_base()) {
                    eml_debug(0, "❌ Base pointer is null", "load forest");
                    return false;
                }
                // Ensure container is properly sized before loading
                if(trees.size() != config_ptr->num_trees) {
                    eml_debug_2(2, "🔧 Adjusting container size from", trees.size(), "to", config_ptr->num_trees);
                    if(config_ptr->num_trees > 0) {
                        ensure_tree_slot(static_cast<uint8_t>(config_ptr->num_trees - 1));
                    } else {
                        trees.clear();
                    }
                }
                // Memory safety check
                size_t freeMemory = eml_memory_status().first;
                if(freeMemory < config_ptr->estimatedRAM + 8000) {
                    eml_debug_2(1, "❌ Insufficient memory to load forest (need", 
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
                    if(tree.isLoaded && (tree.leaf_nodes.size() > 0 || tree.branch_kind.size() > 0 || !tree.nodes.empty())) {
                        loadedTrees++;
                        total_depths += tree.getTreeDepth();
                        total_nodes += tree.countNodes();
                        total_leaves += tree.countLeafNodes();
                    }
                }
                
                if(loadedTrees != config_ptr->num_trees) {
                    eml_debug_2(1, "❌ Loaded trees mismatch: ", loadedTrees, "expected: ", config_ptr->num_trees);
                    is_loaded = false;
                    return false;
                }
                
                is_loaded = true;
                return true;
            }

            // Load forest from unified format (single file containing all trees)
            bool loadForestUnified() {
                char unifiedfile_path[RF_PATH_BUFFER];
                base_ptr->get_forest_path(unifiedfile_path);
                if(unifiedfile_path[0] == '\0' || !RF_FS_EXISTS(unifiedfile_path)) {
                    eml_debug(0, "❌ Unified forest file not found: ", unifiedfile_path);
                    return false;
                }
                
                // Load from unified file (optimized format)
                File file = RF_FS_OPEN(unifiedfile_path, RF_FILE_READ);
                if (!file) {
                    eml_debug(0, "❌ Failed to open unified forest file: ", unifiedfile_path);
                    return false;
                }
                
                // Read forest header with error checking
                uint32_t magic;
                if(file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    eml_debug(0, "❌ Failed to read magic number from: ", unifiedfile_path);
                    file.close();
                    return false;
                }
                
                if (magic != 0x33435246) { // "FRC3"
                    eml_debug(0, "❌ Invalid forest file format (expected FRC3): ", unifiedfile_path);
                    file.close();
                    return false;
                }

                {
                    uint8_t version = 0;
                    if (file.read(reinterpret_cast<uint8_t*>(&version), 1) != 1 || version != 3) {
                        file.close();
                        return false;
                    }

                    auto read_u32 = [&](uint32_t& out) -> bool {
                        return file.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
                    };
                    auto read_le = [&](uint64_t& out, uint8_t bytes) -> bool {
                        out = 0;
                        for (uint8_t b = 0; b < bytes; ++b) {
                            uint8_t byte = 0;
                            if (file.read(reinterpret_cast<uint8_t*>(&byte), 1) != 1) {
                                return false;
                            }
                            out |= (static_cast<uint64_t>(byte) << (8u * b));
                        }
                        return true;
                    };

                    uint8_t treeCount = 0;
                    if (file.read(reinterpret_cast<uint8_t*>(&treeCount), sizeof(treeCount)) != sizeof(treeCount)) {
                        file.close();
                        return false;
                    }
                    if (treeCount != config_ptr->num_trees) {
                        eml_debug_2(1, "⚠️ Tree count mismatch in unified file: ", treeCount, "expected: ", config_ptr->num_trees);
                        file.close();
                        return false;
                    }

                    // Bit widths for container
                    uint8_t tBits = 0, fBits = 0, lBits = 0, cBits = 0;
                    if (file.read(reinterpret_cast<uint8_t*>(&tBits), 1) != 1 ||
                        file.read(reinterpret_cast<uint8_t*>(&fBits), 1) != 1 ||
                        file.read(reinterpret_cast<uint8_t*>(&lBits), 1) != 1 ||
                        file.read(reinterpret_cast<uint8_t*>(&cBits), 1) != 1) {
                        file.close();
                        return false;
                    }
                    resources.set_bits(fBits, lBits, cBits, tBits);

                    eml_debug(1, "📁 Loading from unified compact forest file", unifiedfile_path);

                    uint8_t successfullyLoaded = 0;
                    for (uint8_t i = 0; i < treeCount; ++i) {
                        uint8_t treeIndex = 0;
                        if (file.read(reinterpret_cast<uint8_t*>(&treeIndex), 1) != 1) {
                            break;
                        }
                        ensure_tree_slot(treeIndex);
                        auto& tree = trees[treeIndex];
                        tree.set_resource(&resources);

                        uint8_t rootLeaf = 0;
                        if (file.read(reinterpret_cast<uint8_t*>(&rootLeaf), 1) != 1) {
                            break;
                        }
                        tree.root_is_leaf = (rootLeaf != 0);
                        uint32_t rootIndexU32 = 0;
                        if (!read_u32(rootIndexU32)) {
                            break;
                        }
                        tree.root_index = static_cast<rf_node_type>(rootIndexU32);

                        uint32_t branchCountU32 = 0, internalCountU32 = 0, mixedCountU32 = 0, leafCountU32 = 0;
                        if (!read_u32(branchCountU32) || !read_u32(internalCountU32) || !read_u32(mixedCountU32) || !read_u32(leafCountU32)) {
                            break;
                        }

                        uint8_t inBits = 0, mxBits = 0, lfBits = 0;
                        if (file.read(reinterpret_cast<uint8_t*>(&inBits), 1) != 1 ||
                            file.read(reinterpret_cast<uint8_t*>(&mxBits), 1) != 1 ||
                            file.read(reinterpret_cast<uint8_t*>(&lfBits), 1) != 1) {
                            break;
                        }
                        const uint8_t inBytes = static_cast<uint8_t>((inBits + 7) / 8);
                        const uint8_t mxBytes = static_cast<uint8_t>((mxBits + 7) / 8);
                        const uint8_t lfBytes = static_cast<uint8_t>((lfBits + 7) / 8);

                        tree.internal_nodes.set_bits_per_value(inBits);
                        tree.mixed_nodes.set_bits_per_value(mxBits);
                        tree.leaf_nodes.set_bits_per_value(lfBits);
                        tree.branch_kind.set_bits_per_value(1);

                        tree.internal_nodes.clear();
                        tree.mixed_nodes.clear();
                        tree.leaf_nodes.clear();
                        tree.branch_kind.clear();
                        tree.mixed_prefix.clear();

                        uint32_t kindBytes = 0;
                        if (!read_u32(kindBytes)) {
                            break;
                        }
                        tree.branch_kind.resize(static_cast<rf_node_type>(branchCountU32), 0);
                        for (uint32_t byteIndex = 0; byteIndex < kindBytes; ++byteIndex) {
                            uint8_t in = 0;
                            if (file.read(reinterpret_cast<uint8_t*>(&in), 1) != 1) {
                                break;
                            }
                            const uint32_t base = byteIndex * 8u;
                            for (uint8_t bit = 0; bit < 8; ++bit) {
                                const uint32_t idx = base + static_cast<uint32_t>(bit);
                                if (idx < branchCountU32) {
                                    tree.branch_kind.set(static_cast<rf_node_type>(idx), static_cast<uint8_t>((in >> bit) & 1u));
                                }
                            }
                        }

                        tree.internal_nodes.reserve(static_cast<rf_node_type>(internalCountU32));
                        for (uint32_t k = 0; k < internalCountU32; ++k) {
                            uint64_t raw = 0;
                            if (!read_le(raw, inBytes)) {
                                break;
                            }
                            Internal_node n;
                            n.packed_data = static_cast<size_t>(raw);
                            tree.internal_nodes.push_back(n);
                        }

                        tree.mixed_nodes.reserve(static_cast<rf_node_type>(mixedCountU32));
                        for (uint32_t k = 0; k < mixedCountU32; ++k) {
                            uint64_t raw = 0;
                            if (!read_le(raw, mxBytes)) {
                                break;
                            }
                            Mixed_node n;
                            n.packed_data = static_cast<size_t>(raw);
                            tree.mixed_nodes.push_back(n);
                        }

                        tree.leaf_nodes.reserve(static_cast<rf_node_type>(leafCountU32));
                        for (uint32_t k = 0; k < leafCountU32; ++k) {
                            uint64_t raw = 0;
                            if (!read_le(raw, lfBytes)) {
                                break;
                            }
                            tree.leaf_nodes.push_back(static_cast<rf_label_type>(raw));
                        }

                        tree.isLoaded = true;
                        tree.nodes.clear();
                        tree.nodes.fit();
                        tree.rebuild_compact_index();
                        successfullyLoaded++;
                    }
                    file.close();
                    (void)successfullyLoaded;
                    return check_valid_after_load();
                }
            }

            // Load forest from individual tree files (used during training)
            bool loadForestIndividual() {
                eml_debug(1, "📁 Loading from individual tree files...");
                
                char model_name[RF_PATH_BUFFER];
                base_ptr->get_model_name(model_name, RF_PATH_BUFFER);
                
                uint8_t successfullyLoaded = 0;
                for (auto& tree : trees) {
                    if (!tree.isLoaded) {
                        try {
                            tree.set_resource(&resources);
                            // Construct tree file path
                            base_ptr->build_tree_file_path(tree_path_buffer, tree.index);
                            tree.loadTree(tree_path_buffer);
                            if(tree.isLoaded) successfullyLoaded++;
                        } catch (...) {
                            eml_debug(1, "❌ Exception loading tree: ", tree.index);
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
                    eml_debug(2, "✅ Forest is not loaded in memory, nothing to release.");
                    return true; // Nothing to do
                }    
                // Count loaded trees
                uint8_t loadedCount = 0;
                rf_node_type totalNodes = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && (tree.leaf_nodes.size() > 0 || tree.branch_kind.size() > 0 || !tree.nodes.empty())) {
                        loadedCount++;
                        totalNodes += tree.countNodes();
                    }
                }
                
                if(loadedCount == 0) {
                    eml_debug(1, "❌ No loaded trees to release");
                    is_loaded = false;
                    return false;
                }
                
                // Check available file system space before writing
                size_t totalFS = RF_TOTAL_BYTES();
                size_t usedFS = RF_USED_BYTES();
                size_t freeFS = totalFS - usedFS;
                // Estimate size using compact structure (rough): assume internal nodes ~half, leaf nodes ~half.
                uint8_t estLeafBits = resources.bits_per_leaf_node();
                uint8_t estInBits = resources.bits_per_internal_node();
                uint8_t estLeafBytes = static_cast<uint8_t>((estLeafBits + 7) / 8);
                uint8_t estInBytes = static_cast<uint8_t>((estInBits + 7) / 8);
                size_t estimatedSize = static_cast<size_t>(totalNodes / 2) * estInBytes + static_cast<size_t>(totalNodes / 2) * estLeafBytes + 256;
                
                if(freeFS < estimatedSize) {
                    eml_debug_2(1, "❌ Insufficient file system space to release forest (need ~", 
                                estimatedSize, "bytes, have", freeFS);
                    return false;
                }
                
                // Single file approach - write all trees to unified forest file
                char unifiedfile_path[RF_PATH_BUFFER];
                base_ptr->get_forest_path(unifiedfile_path);
                if(unifiedfile_path[0] == '\0') {
                    eml_debug(0, "❌ Cannot release forest: no base reference for file management");
                    return false;
                }
                
                unsigned long fileStart = rf_time_now(MILLISECONDS);
                File file = RF_FS_OPEN(unifiedfile_path, FILE_WRITE);
                if (!file) {
                    eml_debug(0, "❌ Failed to create unified forest file: ", unifiedfile_path);
                    return false;
                }
                
                // Write forest header: FRC3 (portable)
                uint32_t magic = 0x33435246; // 'F''R''C''3'
                if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    eml_debug(0, "❌ Failed to write magic number to: ", unifiedfile_path);
                    file.close();
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }

                const uint8_t version = 3;
                if (file.write(reinterpret_cast<const uint8_t*>(&version), 1) != 1) {
                    file.close();
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }

                auto write_u32 = [&](uint32_t v) {
                    file.write(reinterpret_cast<const uint8_t*>(&v), sizeof(v));
                };
                auto write_le = [&](uint64_t v, uint8_t bytes) {
                    for (uint8_t b = 0; b < bytes; ++b) {
                        const uint8_t byte = static_cast<uint8_t>((v >> (8u * b)) & 0xFFu);
                        file.write(reinterpret_cast<const uint8_t*>(&byte), 1);
                    }
                };

                if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
                    eml_debug(0, "❌ Failed to write tree count to: ", unifiedfile_path);
                    file.close();
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }

                // Persist bit widths once per forest
                file.write(reinterpret_cast<const uint8_t*>(&resources.threshold_bits), 1);
                file.write(reinterpret_cast<const uint8_t*>(&resources.feature_bits), 1);
                file.write(reinterpret_cast<const uint8_t*>(&resources.label_bits), 1);
                file.write(reinterpret_cast<const uint8_t*>(&resources.child_bits), 1);
                
                size_t totalBytes = 0;
                
                // Write all trees in sequence with error checking
                uint8_t savedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && tree.index != 255 && (tree.leaf_nodes.size() > 0 || tree.branch_kind.size() > 0 || !tree.nodes.empty())) {
                        tree.set_resource(&resources);
                        if ((tree.internal_nodes.size() + tree.mixed_nodes.size() + tree.leaf_nodes.size()) == 0) {
                            (void)tree.convert_to_compact();
                        }
                        // Write tree header
                        if(file.write((uint8_t*)&tree.index, sizeof(tree.index)) != sizeof(tree.index)) {
                            eml_debug(1, "❌ Failed to write tree index: ", tree.index);
                            break;
                        }

                        const uint8_t rootLeaf = tree.root_is_leaf ? 1 : 0;
                        file.write(reinterpret_cast<const uint8_t*>(&rootLeaf), 1);
                        write_u32(static_cast<uint32_t>(tree.root_index));

                        const uint32_t branchCount = static_cast<uint32_t>(tree.branch_kind.size());
                        const uint32_t internalCount = static_cast<uint32_t>(tree.internal_nodes.size());
                        const uint32_t mixedCount = static_cast<uint32_t>(tree.mixed_nodes.size());
                        const uint32_t leafCount = static_cast<uint32_t>(tree.leaf_nodes.size());
                        write_u32(branchCount);
                        write_u32(internalCount);
                        write_u32(mixedCount);
                        write_u32(leafCount);

                        const uint8_t inBits = tree.internal_nodes.get_bits_per_value();
                        const uint8_t mxBits = tree.mixed_nodes.get_bits_per_value();
                        const uint8_t lfBits = tree.leaf_nodes.get_bits_per_value();
                        const uint8_t inBytes = static_cast<uint8_t>((inBits + 7) / 8);
                        const uint8_t mxBytes = static_cast<uint8_t>((mxBits + 7) / 8);
                        const uint8_t lfBytes = static_cast<uint8_t>((lfBits + 7) / 8);
                        file.write(reinterpret_cast<const uint8_t*>(&inBits), 1);
                        file.write(reinterpret_cast<const uint8_t*>(&mxBits), 1);
                        file.write(reinterpret_cast<const uint8_t*>(&lfBits), 1);

                        const uint32_t kindBytes = (branchCount + 7u) / 8u;
                        write_u32(kindBytes);
                        for (uint32_t byteIndex = 0; byteIndex < kindBytes; ++byteIndex) {
                            uint8_t out = 0;
                            const uint32_t base = byteIndex * 8u;
                            for (uint8_t bit = 0; bit < 8; ++bit) {
                                const uint32_t i = base + static_cast<uint32_t>(bit);
                                if (i < branchCount) {
                                    out |= (static_cast<uint8_t>(tree.branch_kind.get(i) & 1u) << bit);
                                }
                            }
                            file.write(reinterpret_cast<const uint8_t*>(&out), 1);
                        }
                        totalBytes += kindBytes;

                        for (uint32_t i = 0; i < internalCount; ++i) {
                            const Internal_node n = tree.internal_nodes.get(static_cast<rf_node_type>(i));
                            write_le(static_cast<uint64_t>(n.packed_data), inBytes);
                            totalBytes += inBytes;
                        }
                        for (uint32_t i = 0; i < mixedCount; ++i) {
                            const Mixed_node n = tree.mixed_nodes.get(static_cast<rf_node_type>(i));
                            write_le(static_cast<uint64_t>(n.packed_data), mxBytes);
                            totalBytes += mxBytes;
                        }
                        for (uint32_t i = 0; i < leafCount; ++i) {
                            const rf_label_type lbl = tree.leaf_nodes.get(static_cast<rf_node_type>(i));
                            write_le(static_cast<uint64_t>(lbl), lfBytes);
                            totalBytes += lfBytes;
                        }

                        savedCount++;
                    }
                }
                file.close();
                
                // Verify file was written correctly
                if(savedCount != loadedCount) {
                    eml_debug_2(1, "❌ Save incomplete: ", savedCount, "/", loadedCount);
                    RF_FS_REMOVE(unifiedfile_path);
                    return false;
                }
                
                // Only clear trees from RAM after successful save
                uint8_t clearedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded) {
                        tree.clearTree();
                        tree.isLoaded = false;
                        clearedCount++;
                    }
                }
                
                is_loaded = false;
                is_unified = true; // forest always in unified form after first time release
                
                eml_debug_2(1, "✅ Released ", clearedCount, "trees to unified format: ", unifiedfile_path);
                return true;
            }

            void end_training_phase() {
                queue_nodes.clear();
                queue_nodes.fit();
            }

            Eml_tree& operator[](uint8_t index){
                return trees[index];
            }

            node_resource* resource_ptr() {
                return &resources;
            }

            const node_resource* resource_ptr() const {
                return &resources;
            }

            const node_resource& get_resource() const {
                return resources;
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
                return resources.bits_per_building_node();
            }

            //  model size in ram 
            size_t size_in_ram() const {     
                size_t size = 0;
                size += sizeof(*this);                           
                size += config_ptr->num_trees * sizeof(Eml_tree);    
                // Approximate: internal nodes + mixed nodes + leaf nodes are already packed.
                size += (total_nodes * resources.bits_per_internal_node() + 7) / 8;
                size += predictClass.memory_usage();
                size += queue_nodes.memory_usage();
                return size;
            }

            // Check if container is empty
            bool empty() const {
                return trees.empty();
            }

            // Get queue_nodes for tree building
            vector<NodeToBuild>& getQueueNodes() {
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

} // namespace eml
