#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Eml_tree {
        static constexpr uint8_t bits_per_node = sizeof(size_t) * 8;
    public:
        // Build-time representation (Building_node, breadth-first).
        packed_vector<bits_per_node, Building_node> nodes;

        // Compact inference-time representation.
        packed_vector<bits_per_node, Internal_node> internal_nodes;
        packed_vector<bits_per_node, Mixed_node> mixed_nodes;
        packed_vector<8, rf_label_type> leaf_nodes;
        packed_vector<1, uint8_t> branch_kind; // bpv=1; 0=internal, 1=mixed in branch-index space

        // Prefix sums over branch_kind words to map branch index -> internal/mixed local index in O(1)
        b_vector<uint16_t, 32> mixed_prefix;

        node_resource* resource = nullptr;      // Node layouts and bit widths

        // Root reference in compact form
        bool root_is_leaf = false;
        rf_node_type root_index = 0; // leaf index if root_is_leaf, else branch index

        uint16_t depth;
        uint8_t index;
        bool isLoaded;
        ID_vector<rf_sample_type, 3> bootstrapIDs;

        Eml_tree() : nodes(), internal_nodes(), mixed_nodes(), leaf_nodes(), branch_kind(), mixed_prefix(), resource(nullptr), index(255), isLoaded(false) {}

        explicit Eml_tree(uint8_t idx) : nodes(), internal_nodes(), mixed_nodes(), leaf_nodes(), branch_kind(), mixed_prefix(), resource(nullptr), index(idx), isLoaded(false) {}

        Eml_tree(const Eml_tree& other) : nodes(other.nodes),
            internal_nodes(other.internal_nodes),
            mixed_nodes(other.mixed_nodes),
            leaf_nodes(other.leaf_nodes),
            branch_kind(other.branch_kind),
            mixed_prefix(other.mixed_prefix),
            resource(other.resource),
            root_is_leaf(other.root_is_leaf),
            root_index(other.root_index),
            depth(other.depth),
            index(other.index),
            isLoaded(other.isLoaded),
            bootstrapIDs(other.bootstrapIDs) {}

        Eml_tree& operator=(const Eml_tree& other) {
            if (this != &other) {
                nodes = other.nodes;
                internal_nodes = other.internal_nodes;
                mixed_nodes = other.mixed_nodes;
                leaf_nodes = other.leaf_nodes;
                branch_kind = other.branch_kind;
                mixed_prefix = other.mixed_prefix;
                resource = other.resource;
                index = other.index;
                isLoaded = other.isLoaded;
                root_is_leaf = other.root_is_leaf;
                root_index = other.root_index;
                depth = other.depth;
                bootstrapIDs = other.bootstrapIDs;
            }
            return *this;
        }

        Eml_tree(Eml_tree&& other) noexcept
                : nodes(std::move(other.nodes)),
                    internal_nodes(std::move(other.internal_nodes)),
                    mixed_nodes(std::move(other.mixed_nodes)),
                    leaf_nodes(std::move(other.leaf_nodes)),
                    branch_kind(std::move(other.branch_kind)),
                    mixed_prefix(std::move(other.mixed_prefix)),
                    resource(other.resource),
                    root_is_leaf(other.root_is_leaf),
                    root_index(other.root_index),
                    depth(other.depth),
                    index(other.index),
                    isLoaded(other.isLoaded),
                    bootstrapIDs(std::move(other.bootstrapIDs)) {
            other.resource = nullptr;
            other.index = 255;
            other.isLoaded = false;
        }

        Eml_tree& operator=(Eml_tree&& other) noexcept {
            if (this != &other) {
                nodes = std::move(other.nodes);
                internal_nodes = std::move(other.internal_nodes);
                mixed_nodes = std::move(other.mixed_nodes);
                leaf_nodes = std::move(other.leaf_nodes);
                branch_kind = std::move(other.branch_kind);
                mixed_prefix = std::move(other.mixed_prefix);
                resource = other.resource;
                index = other.index;
                isLoaded = other.isLoaded;
                root_is_leaf = other.root_is_leaf;
                root_index = other.root_index;
                depth = other.depth;
                bootstrapIDs = std::move(other.bootstrapIDs);
                other.resource = nullptr;
                other.index = 255;
                other.isLoaded = false;
            }
            return *this;
        }

        void set_resource(node_resource* res_ptr, bool reset_storage = false) {
            resource = res_ptr;
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

            // Pre-allocate compact buffers too (heuristics: ~half leaves, ~half branch; mixed ~2%).
            if (resource) {
                const uint8_t inBits = resource->bits_per_internal_node();
                const uint8_t mxBits = resource->bits_per_mixed_node();
                const uint8_t lfBits = resource->bits_per_leaf_node();
                if (internal_nodes.get_bits_per_value() != inBits) {
                    internal_nodes.set_bits_per_value(inBits);
                } else {
                    internal_nodes.clear();
                }
                if (mixed_nodes.get_bits_per_value() != mxBits) {
                    mixed_nodes.set_bits_per_value(mxBits);
                } else {
                    mixed_nodes.clear();
                }
                if (leaf_nodes.get_bits_per_value() != lfBits) {
                    leaf_nodes.set_bits_per_value(lfBits);
                } else {
                    leaf_nodes.clear();
                }
                // branch_kind.set_bits_per_value(1);
                branch_kind.clear();
                mixed_prefix.clear();

                if (reserveCount > 0) {
                    const size_t half = reserveCount / 2;
                    internal_nodes.reserve(half);
                    leaf_nodes.reserve(reserveCount - half);
                    const size_t mx = (reserveCount > 50) ? (reserveCount * 2 / 100) : 1;
                    mixed_nodes.reserve(mx);
                    branch_kind.reserve(half + (reserveCount - half));
                }
            }
        }

        rf_node_type countNodes() const {
            // Prefer compact representation if present.
            const size_t compact_total = internal_nodes.size() + mixed_nodes.size() + leaf_nodes.size();
            if (compact_total > 0) {
                return static_cast<rf_node_type>(compact_total);
            }
            return static_cast<rf_node_type>(nodes.size());
        }

        size_t memory_usage() const {
            return nodes.memory_usage() + sizeof(*this);
        }

        rf_node_type countLeafNodes() const {
            if (leaf_nodes.size() > 0) {
                return static_cast<rf_node_type>(leaf_nodes.size());
            }
            rf_node_type leafCount = 0;
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

        // Convert build-time Building_node storage into compact storage.
        // After successful conversion, build nodes are cleared to free RAM.
        bool convert_to_compact() {
            if (!resource) {
                return false;
            }
            if (nodes.empty()) {
                return false;
            }

            // Ensure compact buffers are reset (do not assume prior reserve state)
            internal_nodes.clear();
            mixed_nodes.clear();
            leaf_nodes.clear();
            branch_kind.clear();
            mixed_prefix.clear();

            internal_nodes.set_bits_per_value(resource->bits_per_internal_node());
            mixed_nodes.set_bits_per_value(resource->bits_per_mixed_node());
            leaf_nodes.set_bits_per_value(resource->bits_per_leaf_node());
            branch_kind.set_bits_per_value(1);

            const auto& featureLayout = resource->get_Building_node_featureID_layout();
            const auto& labelLayout = resource->get_Building_node_label_layout();
            const auto& childLayout = resource->get_Building_node_left_child_layout();
            const auto& thresholdLayout = resource->get_Building_node_threshold_layout();

            const rf_node_type nodeCount = static_cast<rf_node_type>(nodes.size());
            if (nodeCount == 0) {
                return false;
            }

            // Map old indices -> leaf index / branch index (branch index is in old-order filtering)
            vector<rf_node_type> old_to_leaf;
            vector<rf_node_type> old_to_branch;
            old_to_leaf.resize(nodeCount, static_cast<rf_node_type>(0xFFFFFFFFu));
            old_to_branch.resize(nodeCount, static_cast<rf_node_type>(0xFFFFFFFFu));

            rf_node_type branchCount = 0;
            for (rf_node_type i = 0; i < nodeCount; ++i) {
                const Building_node n = nodes.get(i);
                if (n.getIsLeaf()) {
                    old_to_leaf[i] = static_cast<rf_node_type>(leaf_nodes.size());
                    leaf_nodes.push_back(n.getLabel(labelLayout));
                } else {
                    old_to_branch[i] = branchCount;
                    branchCount++;
                }
            }

            // Root
            const Building_node root = nodes.get(0);
            root_is_leaf = root.getIsLeaf();
            root_index = root_is_leaf ? old_to_leaf[0] : old_to_branch[0];

            // Build branch nodes in old-order filtering; branch_kind indicates which compact vector to use.
            for (rf_node_type i = 0; i < nodeCount; ++i) {
                const Building_node n = nodes.get(i);
                if (n.getIsLeaf()) {
                    continue;
                }
                const rf_node_type bidx = old_to_branch[i];
                const rf_node_type left_old = n.getLeftChildIndex(childLayout);
                const rf_node_type right_old = static_cast<rf_node_type>(left_old + 1);
                if (left_old >= nodeCount || right_old >= nodeCount) {
                    return false;
                }
                const Building_node left_n = nodes.get(left_old);
                const Building_node right_n = nodes.get(right_old);
                const bool left_leaf = left_n.getIsLeaf();
                const bool right_leaf = right_n.getIsLeaf();

                const uint16_t featureID = n.getFeatureID(featureLayout);
                const uint16_t threshold = n.getThresholdSlot(thresholdLayout);

                if (left_leaf == right_leaf) {
                    Internal_node inode;
                    inode.setChildrenAreLeaf(left_leaf);
                    inode.setThresholdSlot(threshold, *resource);
                    inode.setFeatureID(featureID, *resource);
                    const rf_node_type left_new = left_leaf ? old_to_leaf[left_old] : old_to_branch[left_old];
                    inode.setLeftChildIndex(left_new, *resource);

                    // Append internal node and mark kind=0
                    (void)bidx; // bidx used in branch_kind ordering only
                    internal_nodes.push_back(inode);
                    branch_kind.push_back(0);
                } else {
                    Mixed_node mnode;
                    mnode.setLeftIsLeaf(left_leaf);
                    mnode.setThresholdSlot(threshold, *resource);
                    mnode.setFeatureID(featureID, *resource);
                    const rf_node_type left_new = left_leaf ? old_to_leaf[left_old] : old_to_branch[left_old];
                    const rf_node_type right_new = right_leaf ? old_to_leaf[right_old] : old_to_branch[right_old];
                    mnode.setLeftChildIndex(left_new, *resource);
                    mnode.setRightChildIndex(right_new, *resource);

                    mixed_nodes.push_back(mnode);
                    branch_kind.push_back(1);
                }
            }

            // Build prefix sums for rank mapping
            build_mixed_prefix();

            // Drop build nodes to reclaim RAM
            nodes.clear();
            nodes.fit();

            return true;
        }

        // Rebuild auxiliary indices (rank prefix) after loading compact data.
        void rebuild_compact_index() {
            if (branch_kind.size() > 0) {
                build_mixed_prefix();
            } else {
                mixed_prefix.clear();
            }
        }


        bool releaseTree(const char* path, bool re_use = false) {
            if (!re_use) {
                if (index > RF_MAX_TREES || nodes.empty()) {
                    // In compact mode nodes may be empty; allow save if compact buffers exist.
                    const bool hasCompact = (internal_nodes.size() + mixed_nodes.size() + leaf_nodes.size()) > 0;
                    if (!hasCompact) {
                        eml_debug(0, "❌ save tree failed, invalid tree index: ", index);
                        return false;
                    }
                }
                if (path == nullptr || strlen(path) == 0) {
                    eml_debug(0, "❌ save tree failed, invalid path: ", path);
                    return false;
                }
                if (RF_FS_EXISTS(path)) {
                    if (!RF_FS_REMOVE(path)) {
                        eml_debug(0, "❌ Failed to remove existing tree file: ", path);
                        return false;
                    }
                }
                File file = RF_FS_OPEN(path, FILE_WRITE);
                if (!file) {
                    eml_debug(0, "❌ Failed to open tree file for writing: ", path);
                    return false;
                }

                // Prefer compact format; if build nodes exist, convert first.
                if ((internal_nodes.size() + mixed_nodes.size() + leaf_nodes.size()) == 0) {
                    (void)convert_to_compact();
                }
                if (!resource) {
                    eml_debug(0, "❌ save tree failed: node_resource not set");
                    file.close();
                    return false;
                }

                // Compact tree format: TRC3 (portable: fixed-width counters + byte-packed branch_kind)
                const uint32_t magic = 0x33524354; // 'T''R''C''3'
                file.write(reinterpret_cast<const uint8_t*>(&magic), sizeof(magic));

                const uint8_t version = 3;
                file.write(reinterpret_cast<const uint8_t*>(&version), sizeof(version));

                auto write_u32 = [&](uint32_t v) {
                    file.write(reinterpret_cast<const uint8_t*>(&v), sizeof(v));
                };
                auto write_le = [&](uint64_t v, uint8_t bytes) {
                    for (uint8_t b = 0; b < bytes; ++b) {
                        const uint8_t byte = static_cast<uint8_t>((v >> (8u * b)) & 0xFFu);
                        file.write(reinterpret_cast<const uint8_t*>(&byte), 1);
                    }
                };

                // Persist bit widths for robustness
                file.write(reinterpret_cast<const uint8_t*>(&resource->threshold_bits), sizeof(uint8_t));
                file.write(reinterpret_cast<const uint8_t*>(&resource->feature_bits), sizeof(uint8_t));
                file.write(reinterpret_cast<const uint8_t*>(&resource->label_bits), sizeof(uint8_t));
                file.write(reinterpret_cast<const uint8_t*>(&resource->child_bits), sizeof(uint8_t));

                const uint8_t rootLeaf = root_is_leaf ? 1 : 0;
                file.write(reinterpret_cast<const uint8_t*>(&rootLeaf), sizeof(rootLeaf));
                write_u32(static_cast<uint32_t>(root_index));

                const uint32_t branchCount = static_cast<uint32_t>(branch_kind.size());
                const uint32_t internalCount = static_cast<uint32_t>(internal_nodes.size());
                const uint32_t mixedCount = static_cast<uint32_t>(mixed_nodes.size());
                const uint32_t leafCount = static_cast<uint32_t>(leaf_nodes.size());
                write_u32(branchCount);
                write_u32(internalCount);
                write_u32(mixedCount);
                write_u32(leafCount);

                // Write packed payloads as raw little-endian bytes per element
                const uint8_t inBits = internal_nodes.get_bits_per_value();
                const uint8_t mxBits = mixed_nodes.get_bits_per_value();
                const uint8_t lfBits = leaf_nodes.get_bits_per_value();
                const uint8_t inBytes = static_cast<uint8_t>((inBits + 7) / 8);
                const uint8_t mxBytes = static_cast<uint8_t>((mxBits + 7) / 8);
                const uint8_t lfBytes = static_cast<uint8_t>((lfBits + 7) / 8);
                file.write(reinterpret_cast<const uint8_t*>(&inBits), sizeof(inBits));
                file.write(reinterpret_cast<const uint8_t*>(&mxBits), sizeof(mxBits));
                file.write(reinterpret_cast<const uint8_t*>(&lfBits), sizeof(lfBits));

                // branch_kind bits (bpv=1) as raw bytes (portable)
                const uint32_t kindBytes = (branchCount + 7u) / 8u;
                write_u32(kindBytes);
                for (uint32_t byteIndex = 0; byteIndex < kindBytes; ++byteIndex) {
                    uint8_t out = 0;
                    const uint32_t base = byteIndex * 8u;
                    for (uint8_t bit = 0; bit < 8; ++bit) {
                        const uint32_t i = base + static_cast<uint32_t>(bit);
                        if (i < branchCount) {
                            out |= (static_cast<uint8_t>(branch_kind.get(i) & 1u) << bit);
                        }
                    }
                    file.write(reinterpret_cast<const uint8_t*>(&out), 1);
                }

                // Internal nodes
                for (uint32_t i = 0; i < internalCount; ++i) {
                    const Internal_node n = internal_nodes.get(static_cast<rf_node_type>(i));
                    write_le(static_cast<uint64_t>(n.packed_data), inBytes);
                }

                // Mixed nodes
                for (uint32_t i = 0; i < mixedCount; ++i) {
                    const Mixed_node n = mixed_nodes.get(static_cast<rf_node_type>(i));
                    write_le(static_cast<uint64_t>(n.packed_data), mxBytes);
                }

                // Leaf nodes (labels)
                for (uint32_t i = 0; i < leafCount; ++i) {
                    const rf_label_type lbl = leaf_nodes.get(static_cast<rf_node_type>(i));
                    write_le(static_cast<uint64_t>(lbl), lfBytes);
                }
                file.close();
            }
            nodes.clear();
            nodes.fit();
            internal_nodes.clear();
            internal_nodes.fit();
            mixed_nodes.clear();
            mixed_nodes.fit();
            leaf_nodes.clear();
            leaf_nodes.fit();
            branch_kind.clear();
            branch_kind.fit();
            mixed_prefix.clear();
            mixed_prefix.fit();
            isLoaded = false;
            eml_debug(2, "✅ Tree saved to file system: ", index);
            return true;
        }

        bool loadTree(const char* path, bool re_use = false) {
            if (isLoaded) {
                return true;
            }

            if (index >= RF_MAX_TREES) {
                eml_debug(0, "❌ Invalid tree index: ", index);
                return false;
            }
            if (path == nullptr || strlen(path) == 0) {
                eml_debug(0, "❌ Invalid path for loading tree: ", path);
                return false;
            }
            if (!RF_FS_EXISTS(path)) {
                eml_debug(0, "❌ Tree file does not exist: ", path);
                return false;
            }
            File file = RF_FS_OPEN(path, RF_FILE_READ);
            if (!file) {
                eml_debug(2, "❌ Failed to open tree file: ", path);
                return false;
            }

            uint32_t magic = 0;
            if (file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) != sizeof(magic)) {
                file.close();
                return false;
            }

            if (magic != 0x33524354) { // "TRC3"
                eml_debug(0, "❌ Invalid tree file format (expected TRC3): ", path);
                file.close();
                return false;
            }

            {
                uint8_t version = 0;
                if (file.read(reinterpret_cast<uint8_t*>(&version), sizeof(version)) != sizeof(version) || version != 3) {
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

                uint8_t tBits = 0, fBits = 0, lBits = 0, cBits = 0;
                if (file.read(reinterpret_cast<uint8_t*>(&tBits), 1) != 1 ||
                    file.read(reinterpret_cast<uint8_t*>(&fBits), 1) != 1 ||
                    file.read(reinterpret_cast<uint8_t*>(&lBits), 1) != 1 ||
                    file.read(reinterpret_cast<uint8_t*>(&cBits), 1) != 1) {
                    file.close();
                    return false;
                }
                if (resource) {
                    resource->set_bits(fBits, lBits, cBits, tBits);
                }

                uint8_t rootLeaf = 0;
                if (file.read(reinterpret_cast<uint8_t*>(&rootLeaf), sizeof(rootLeaf)) != sizeof(rootLeaf)) {
                    file.close();
                    return false;
                }
                root_is_leaf = (rootLeaf != 0);

                uint32_t rootIndexU32 = 0;
                if (!read_u32(rootIndexU32)) {
                    file.close();
                    return false;
                }
                root_index = static_cast<rf_node_type>(rootIndexU32);

                uint32_t branchCountU32 = 0, internalCountU32 = 0, mixedCountU32 = 0, leafCountU32 = 0;
                if (!read_u32(branchCountU32) || !read_u32(internalCountU32) || !read_u32(mixedCountU32) || !read_u32(leafCountU32)) {
                    file.close();
                    return false;
                }

                uint8_t inBits = 0, mxBits = 0, lfBits = 0;
                if (file.read(reinterpret_cast<uint8_t*>(&inBits), 1) != 1 ||
                    file.read(reinterpret_cast<uint8_t*>(&mxBits), 1) != 1 ||
                    file.read(reinterpret_cast<uint8_t*>(&lfBits), 1) != 1) {
                    file.close();
                    return false;
                }
                const uint8_t inBytes = static_cast<uint8_t>((inBits + 7) / 8);
                const uint8_t mxBytes = static_cast<uint8_t>((mxBits + 7) / 8);
                const uint8_t lfBytes = static_cast<uint8_t>((lfBits + 7) / 8);

                internal_nodes.set_bits_per_value(inBits);
                mixed_nodes.set_bits_per_value(mxBits);
                leaf_nodes.set_bits_per_value(lfBits);
                branch_kind.set_bits_per_value(1);

                internal_nodes.clear();
                mixed_nodes.clear();
                leaf_nodes.clear();
                branch_kind.clear();
                mixed_prefix.clear();

                uint32_t kindBytes = 0;
                if (!read_u32(kindBytes)) {
                    file.close();
                    return false;
                }

                branch_kind.resize(static_cast<rf_node_type>(branchCountU32), 0);
                for (uint32_t byteIndex = 0; byteIndex < kindBytes; ++byteIndex) {
                    uint8_t in = 0;
                    if (file.read(reinterpret_cast<uint8_t*>(&in), 1) != 1) {
                        file.close();
                        return false;
                    }
                    const uint32_t base = byteIndex * 8u;
                    for (uint8_t bit = 0; bit < 8; ++bit) {
                        const uint32_t i = base + static_cast<uint32_t>(bit);
                        if (i < branchCountU32) {
                            branch_kind.set(static_cast<rf_node_type>(i), static_cast<uint8_t>((in >> bit) & 1u));
                        }
                    }
                }

                internal_nodes.reserve(static_cast<rf_node_type>(internalCountU32));
                for (uint32_t i = 0; i < internalCountU32; ++i) {
                    uint64_t raw = 0;
                    if (!read_le(raw, inBytes)) {
                        file.close();
                        return false;
                    }
                    Internal_node n;
                    n.packed_data = static_cast<size_t>(raw);
                    internal_nodes.push_back(n);
                }

                mixed_nodes.reserve(static_cast<rf_node_type>(mixedCountU32));
                for (uint32_t i = 0; i < mixedCountU32; ++i) {
                    uint64_t raw = 0;
                    if (!read_le(raw, mxBytes)) {
                        file.close();
                        return false;
                    }
                    Mixed_node n;
                    n.packed_data = static_cast<size_t>(raw);
                    mixed_nodes.push_back(n);
                }

                leaf_nodes.reserve(static_cast<rf_node_type>(leafCountU32));
                for (uint32_t i = 0; i < leafCountU32; ++i) {
                    uint64_t raw = 0;
                    if (!read_le(raw, lfBytes)) {
                        file.close();
                        return false;
                    }
                    leaf_nodes.push_back(static_cast<rf_label_type>(raw));
                }

                file.close();
                rebuild_compact_index();
                isLoaded = true;
            }

            if (!re_use) {
                eml_debug(2, "♻️ Single-load mode: removing tree file after loading; ", path);
                RF_FS_REMOVE(path);
            }
            return true;
        }

        bool saveBootstrapIDs(const char* path) {
            if (bootstrapIDs.empty()) {
                if (RF_FS_EXISTS(path)) RF_FS_REMOVE(path);
                return true;
            }
            File file = RF_FS_OPEN(path, RF_FILE_WRITE);
            if (!file) return false;
            
            uint32_t magic = 0x42544944; // "BTID"
            file.write((uint8_t*)&magic, 4);
            
            rf_sample_type min_id = bootstrapIDs.minID();
            rf_sample_type max_id = bootstrapIDs.maxID();
            uint32_t size = bootstrapIDs.size();
            
            file.write((uint8_t*)&min_id, sizeof(rf_sample_type));
            file.write((uint8_t*)&max_id, sizeof(rf_sample_type));
            file.write((uint8_t*)&size, sizeof(uint32_t));

            for (rf_sample_type id = min_id; id <= max_id; ++id) {
                uint32_t c = bootstrapIDs.count(id);
                if (c > 0) {
                    file.write((uint8_t*)&id, sizeof(rf_sample_type));
                    file.write((uint8_t*)&c, sizeof(uint32_t));
                }
            }
            file.close();
            return true;
        }

        bool loadBootstrapIDs(const char* path) {
            bootstrapIDs.clear();
            if (!RF_FS_EXISTS(path)) return true;
            
            File file = RF_FS_OPEN(path, RF_FILE_READ);
            if (!file) return false;
            
            uint32_t magic;
            if (file.read((uint8_t*)&magic, 4) != 4 || magic != 0x42544944) {
                file.close();
                return false;
            }
            
            rf_sample_type min_id, max_id;
            uint32_t size;
            file.read((uint8_t*)&min_id, sizeof(rf_sample_type));
            file.read((uint8_t*)&max_id, sizeof(rf_sample_type));
            file.read((uint8_t*)&size, sizeof(uint32_t));
            
            bootstrapIDs.set_ID_range(min_id, max_id);
            
            while (file.available()) {
                rf_sample_type id;
                uint32_t c;
                if (file.read((uint8_t*)&id, sizeof(rf_sample_type)) != sizeof(rf_sample_type)) break;
                if (file.read((uint8_t*)&c, sizeof(uint32_t)) != sizeof(uint32_t)) break;
                for (uint32_t i = 0; i < c; ++i) {
                    bootstrapIDs.push_back(id);
                }
            }
            file.close();
            return true;
        }

        __attribute__((always_inline)) inline rf_label_type predict_features(
            const packed_vector<8>& packed_features) const {
            // Early exit for empty tree
            if (__builtin_expect(!resource || leaf_nodes.size() == 0, 0)) {
                return RF_ERROR_LABEL;
            }

            // Handle leaf root
            if (__builtin_expect(root_is_leaf, 0)) {
                return (root_index < leaf_nodes.size()) ? leaf_nodes.get(root_index) : RF_ERROR_LABEL;
            }

            // Cache resource reference to avoid repeated pointer dereference
            const node_resource& res = *resource;
            
            rf_node_type currentBranch = root_index;
            const rf_node_type branchCount = static_cast<rf_node_type>(branch_kind.size());
            const rf_node_type leafCount = static_cast<rf_node_type>(leaf_nodes.size());
            
            uint16_t maxDepth = 100;
            while (__builtin_expect(maxDepth-- > 0, 1)) {
                if (__builtin_expect(currentBranch >= branchCount, 0)) {
                    return RF_ERROR_LABEL;
                }
                
                const uint8_t kind = branch_kind.get(currentBranch);
                if (__builtin_expect(kind == 0, 1)) {
                    // Internal node (common case)
                    const rf_node_type mixedBefore = rank_mixed(currentBranch);
                    const rf_node_type internalIndex = currentBranch - mixedBefore;
                    const Internal_node node = internal_nodes.get(internalIndex);
                    
                    const uint16_t featureID = node.getFeatureID(res);
                    const uint16_t featureValue = static_cast<uint16_t>(packed_features[featureID]);
                    const uint16_t threshold = node.getThresholdSlot(res);
                    const rf_node_type left = node.getLeftChildIndex(res);
                    const rf_node_type chosen = (featureValue <= threshold) ? left : (left + 1);
                    
                    if (__builtin_expect(node.childrenAreLeaf(), 0)) {
                        return (chosen < leafCount) ? leaf_nodes.get(chosen) : RF_ERROR_LABEL;
                    }
                    currentBranch = chosen;
                } else {
                    // Mixed node (less common)
                    const rf_node_type mixedIndex = rank_mixed(currentBranch);
                    const Mixed_node node = mixed_nodes.get(mixedIndex);
                    
                    const uint16_t featureID = node.getFeatureID(res);
                    const uint16_t featureValue = static_cast<uint16_t>(packed_features[featureID]);
                    const uint16_t threshold = node.getThresholdSlot(res);
                    const bool goLeft = (featureValue <= threshold);
                    const bool leftIsLeaf = node.leftIsLeaf();
                    
                    if (goLeft) {
                        const rf_node_type idx = node.getLeftChildIndex(res);
                        if (leftIsLeaf) {
                            return (idx < leafCount) ? leaf_nodes.get(idx) : RF_ERROR_LABEL;
                        }
                        currentBranch = idx;
                    } else {
                        const rf_node_type idx = node.getRightChildIndex(res);
                        if (!leftIsLeaf) {
                            return (idx < leafCount) ? leaf_nodes.get(idx) : RF_ERROR_LABEL;
                        }
                        currentBranch = idx;
                    }
                }
            }

            return RF_ERROR_LABEL;
        }

        void clearTree(bool freeMemory = false) {
            (void)freeMemory;
            nodes.clear();
            nodes.fit();
            internal_nodes.clear();
            internal_nodes.fit();
            mixed_nodes.clear();
            mixed_nodes.fit();
            leaf_nodes.clear();
            leaf_nodes.fit();
            branch_kind.clear();
            branch_kind.fit();
            mixed_prefix.clear();
            mixed_prefix.fit();
            isLoaded = false;
        }

        void purgeTree(const char* path, bool rmf = true) {
            nodes.clear();
            nodes.fit();
            internal_nodes.clear();
            internal_nodes.fit();
            mixed_nodes.clear();
            mixed_nodes.fit();
            leaf_nodes.clear();
            leaf_nodes.fit();
            branch_kind.clear();
            branch_kind.fit();
            mixed_prefix.clear();
            mixed_prefix.fit();
            if (rmf && index < RF_MAX_TREES) {
                if (RF_FS_EXISTS(path)) {
                    RF_FS_REMOVE(path);
                    eml_debug(2, "🗑️ Tree file removed: ", path);
                }
            }
            index = 255;
            isLoaded = false;
        }

    private:
        // Build mixed_prefix for rank queries
        inline void build_mixed_prefix() {
            mixed_prefix.clear();
            const size_t wcount = branch_kind.words();
            mixed_prefix.reserve(wcount + 1);
            mixed_prefix.push_back(0);
            uint16_t acc = 0;
            const auto* w = branch_kind.raw_data();
            for (size_t i = 0; i < wcount; ++i) {
                const size_t word = w ? w[i] : 0;
                if constexpr (sizeof(size_t) == 8) {
                    acc = static_cast<uint16_t>(acc + static_cast<uint16_t>(__builtin_popcountll(static_cast<unsigned long long>(word))));
                } else {
                    acc = static_cast<uint16_t>(acc + static_cast<uint16_t>(__builtin_popcount(static_cast<unsigned int>(word))));
                }
                mixed_prefix.push_back(acc);
            }
        }

        // Rank query: number of mixed nodes before branchIndex
        inline rf_node_type rank_mixed(rf_node_type branchIndex) const {
            // Number of mixed nodes strictly before branchIndex
            const size_t WORD_BITS = sizeof(size_t) * 8;
            const size_t wi = static_cast<size_t>(branchIndex) / WORD_BITS;
            const size_t bi = static_cast<size_t>(branchIndex) % WORD_BITS;
            if (wi >= mixed_prefix.size()) {
                return 0;
            }
            const uint16_t base = mixed_prefix[wi];
            const size_t* w = branch_kind.raw_data();
            if (!w) {
                return base;
            }
            size_t mask = 0;
            if (bi == 0) {
                mask = 0;
            } else if (bi >= WORD_BITS) {
                mask = static_cast<size_t>(~static_cast<size_t>(0));
            } else {
                mask = (static_cast<size_t>(1) << bi) - 1u;
            }
            const size_t word = w[wi] & mask;
            uint16_t pc = 0;
            if constexpr (sizeof(size_t) == 8) {
                pc = static_cast<uint16_t>(__builtin_popcountll(static_cast<unsigned long long>(word)));
            } else {
                pc = static_cast<uint16_t>(__builtin_popcount(static_cast<unsigned int>(word)));
            }
            return static_cast<rf_node_type>(base + pc);
        }

        inline uint8_t desired_bits_per_node() const noexcept {
            uint8_t bits = resource ? resource->bits_per_building_node() : static_cast<uint8_t>(32);
            if (bits == 0 || bits > 32) {
                bits = 32;
            }
            return bits;
        }
    };

} // namespace eml
