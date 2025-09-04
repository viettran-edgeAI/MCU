#include "STL_MCU.h"  
#include "Rf_file_manager.h"
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"

#ifdef DEV_STAGE
    #define ENABLE_TEST_DATA 1
#else
    #define ENABLE_TEST_DATA 0
#endif

#ifndef RF_DEBUG
    #define RF_DEBUG_LEVEL 0
#else
    #ifndef RF_DEBUG_LEVEL
        #define RF_DEBUG_LEVEL 2 // default debug level
    #endif
#endif

static constexpr uint16_t MAX_LABELS = 256; // maximum number of unique labels supported
static constexpr uint16_t MAX_NUM_FEATURES = 1024; // maximum number of features

using sampleID_set = mcu::ID_vector<uint16_t>;      // set of unique sample IDs

class RandomForest;
// Forward declaration for callback 
namespace mcu {
    /*
    ------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ RF_COMPONENTS ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct Rf_sample{
        mcu::packed_vector<2> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
        uint8_t label;                     // label of the sample 

        Rf_sample() : features(), label(0) {}
        Rf_sample(uint8_t label, const mcu::packed_vector<2, mcu::LARGE>& source, size_t start, size_t end){
            this->label = label;
            features = packed_vector<2>(source, start, end);
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_TREE -----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct Tree_node{
        uint32_t packed_data; 
        
        /** Bit layout optimize for breadth-first tree building:
            * Bits 0-9  :  featureID        (10 bits)     -> 0 - 1023 features
            * Bits 10-17:  label            (8 bits)      -> 0 - 255 classes  
            * Bits 18-19:  threshold        (2 bits)      -> 0 | 1 | 2 | 3
            * Bit 20    :  is_leaf          (1 bit)       -> 0/1 
            * Bits 21-31:  left child index (11 bits)     -> 0 - 2047 nodes (max 8kB RAM per tree) 
        @note: right child index = left child index + 1 
        */

        // Constructor
        Tree_node() : packed_data(0) {}

        // Getter methods for packed data
        uint16_t getFeatureID() const {
            return packed_data & 0x3FF;  // Bits 0-9 (10 bits)
        }
        
        uint8_t getLabel() const {
            return (packed_data >> 10) & 0xFF;  // Bits 10-17 (8 bits)
        }
        
        uint8_t getThreshold() const {
            return (packed_data >> 18) & 0x03;  // Bits 18-19 (2 bits)
        }
        
        bool getIsLeaf() const {
            return (packed_data >> 20) & 0x01;  // Bit 20
        }
        
        uint16_t getLeftChildIndex() const {
            return (packed_data >> 21) & 0x7FF;  // Bits 21-31 (11 bits)
        }
        
        uint16_t getRightChildIndex() const {
            return getLeftChildIndex() + 1;  // Breadth-first property: right = left + 1
        }
        
        // Setter methods for packed data
        void setFeatureID(uint16_t featureID) {
            packed_data = (packed_data & 0xFFFFFC00) | (featureID & 0x3FF);  // Bits 0-9
        }
        
        void setLabel(uint8_t label) {
            packed_data = (packed_data & 0xFFFC03FF) | ((uint32_t)(label & 0xFF) << 10);  // Bits 10-17
        }
        
        void setThreshold(uint8_t threshold) {
            packed_data = (packed_data & 0xFFF3FFFF) | ((uint32_t)(threshold & 0x03) << 18);  // Bits 18-19
        }
        
        void setIsLeaf(bool isLeaf) {
            packed_data = (packed_data & 0xFFEFFFFF) | ((uint32_t)(isLeaf ? 1 : 0) << 20);  // Bit 20
        }
        
        void setLeftChildIndex(uint16_t index) {
            packed_data = (packed_data & 0x001FFFFF) | ((uint32_t)(index & 0x7FF) << 21);  // Bits 21-31
        }
    };

    struct NodeToBuild {
        uint16_t nodeIndex;
        uint16_t begin;   // inclusive
        uint16_t end;     // exclusive
        uint8_t depth;
        
        NodeToBuild() : nodeIndex(0), begin(0), end(0), depth(0) {}
        NodeToBuild(uint16_t idx, uint16_t b, uint16_t e, uint8_t d) 
            : nodeIndex(idx), begin(b), end(e), depth(d) {}
    };

    class Rf_tree {
    public:
        mcu::b_vector<Tree_node> nodes;  // Vector-based tree storage
        uint8_t index;
        bool isLoaded;

        Rf_tree() : index(255), isLoaded(false) {}
        
        Rf_tree(uint8_t idx) : index(idx), isLoaded(false) {}

        // Count total number of nodes in the tree (including leaf nodes)
        uint32_t countNodes() const {
            return nodes.size();
        }

        size_t memory_usage() const {
            return nodes.size() * 4 + sizeof(*this);
        }

        // Count leaf nodes in the tree
        uint32_t countLeafNodes() const {
            uint32_t leafCount = 0;
            for (const auto& node : nodes) {
                if (node.getIsLeaf()) {
                    leafCount++;
                }
            }
            return leafCount;
        }

        // Get tree depth
        uint16_t getTreeDepth() const {
            if (nodes.empty()) return 0;
            return getTreeDepthRecursive(0);
        }

        // Save tree to SPIFFS for ESP32
        void releaseTree(String model_name,  bool re_use = false) {
            if(!re_use){
                if (index == 255 || nodes.empty()) return;

                char filename[32];  // filename = "/"+ model_name + "tree_" + index + ".bin"
                snprintf(filename, sizeof(filename), "/%s_tree_%d.bin", model_name.c_str(), index);
                
                // Skip exists/remove check - just overwrite directly for performance
                File file = SPIFFS.open(filename, FILE_WRITE);
                if (!file) {
                    Serial.printf("❌ Failed to save tree: %s\n", filename);
                    return;
                }
                
                // Write header - magic number for validation
                uint32_t magic = 0x54524545; // "TREE" in hex
                file.write((uint8_t*)&magic, sizeof(magic));
                
                // Write number of nodes
                uint32_t nodeCount = nodes.size();
                file.write((uint8_t*)&nodeCount, sizeof(nodeCount));
                
                // Batch write all nodes for better performance
                if(nodeCount > 0) {
                    // Calculate total size needed
                    size_t totalSize = nodeCount * sizeof(nodes[0].packed_data);
                    
                    // Create buffer for batch write
                    uint8_t* buffer = (uint8_t*)malloc(totalSize);
                    if(buffer) {
                        // Copy all packed_data to buffer
                        for(uint32_t i = 0; i < nodeCount; i++) {
                            memcpy(buffer + (i * sizeof(nodes[0].packed_data)), 
                                   &nodes[i].packed_data, sizeof(nodes[0].packed_data));
                        }
                        
                        // Single write operation
                        size_t written = file.write(buffer, totalSize);
                        free(buffer);
                        
                        if(written != totalSize) {
                            Serial.printf("⚠️ Incomplete write: %d/%d bytes\n", written, totalSize);
                        }
                    } else {
                        // Fallback to individual writes if malloc fails
                        for (const auto& node : nodes) {
                            file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data));
                        }
                    }
                }    
                file.close();
            }

            // uint16_t countLeaf = countLeafNodes();
            // Serial.printf("✅ Tree saved: %s (%d nodes, %d leaves, %d bytes)\n", 
            //             filename, nodeCount, countLeaf, nodeCount * 4);

            nodes.clear(); // Clear nodes to free memory
            nodes.fit(); // Release excess memory
            isLoaded = false; // Mark as unloaded
        }

        // Load tree from SPIFFS into RAM for ESP32
        void loadTree(String model_name = "", bool re_use = false) {
            if (isLoaded) return;
            
            if (index == 255) {
                Serial.println("❌ No valid index specified for tree loading");
                return;
            }
            
            char path_to_use[32];
            snprintf(path_to_use, sizeof(path_to_use), "/%s_tree_%d.bin", model_name.c_str(), index);
            
            File file = SPIFFS.open(path_to_use, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open tree file: %s\n", path_to_use);
                return;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x54524545) {
                Serial.printf("❌ Invalid tree file format: %s\n", path_to_use);
                file.close();
                return;
            }
            
            // Read number of nodes
            uint32_t nodeCount;
            if (file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                Serial.printf("❌ Failed to read node count: %s\n", path_to_use);
                file.close();
                return;
            }
            
            if (nodeCount == 0 || nodeCount > 2047) { // 11-bit limit for child indices
                Serial.printf("❌ Invalid node count: %d\n", nodeCount);
                file.close();
                return;
            }
            
            // Clear existing nodes and reserve space
            nodes.clear();
            nodes.reserve(nodeCount);
            
            // Load all nodes
            for (uint32_t i = 0; i < nodeCount; i++) {
                Tree_node node;
                if (file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                    Serial.printf("❌ Failed to read node %d from: %s\n", i, path_to_use);
                    nodes.clear();
                    file.close();
                    return;
                }
                nodes.push_back(node);
            }
            
            file.close();
            
            // Update state
            isLoaded = true;
            
            // Serial.printf("✅ Tree loaded: %s (%d nodes, %d bytes)\n", 
            //             path_to_use, nodeCount, get_memory_usage());
            if (!re_use) {
                SPIFFS.remove(path_to_use); // Remove file after loading in single mode
            }
        }



        uint8_t predictSample(const Rf_sample& sample) const {
            if (nodes.empty() || !isLoaded) return 0;
            
            uint16_t currentIndex = 0;  // Start from root
            
            while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
                // Bounds check for feature access
                if (nodes[currentIndex].getFeatureID() >= sample.features.size()) {
                    return 0; // Invalid feature access
                }
                
                uint8_t featureValue = sample.features[nodes[currentIndex].getFeatureID()];
                
                if (featureValue <= nodes[currentIndex].getThreshold()) {
                    // Go to left child
                    currentIndex = nodes[currentIndex].getLeftChildIndex();
                } else {
                    // Go to right child
                    currentIndex = nodes[currentIndex].getRightChildIndex();
                }
                
                // Bounds check for child indices
                if (currentIndex >= nodes.size()) {
                    return 0; // Invalid child index
                }
            }
            
            return (currentIndex < nodes.size()) ? nodes[currentIndex].getLabel() : 0;
        }

        void clearTree(bool freeMemory = false) {
            nodes.clear();
            nodes.fit();
            if(freeMemory) nodes.fit(); // Release excess memory
            isLoaded = false;
        }

        void purgeTree(String model_name = "", bool rmf = true) {
            nodes.clear();
            nodes.fit(); // Release excess memory
            if(rmf && index != 255) {
                char filename[32];
                snprintf(filename, sizeof(filename), "/%s_tree_%d.bin", model_name.c_str(), index);
                if (SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                    // Serial.printf("✅ Tree file removed: %s\n", filename);
                } 
            }
            index = 255;
            isLoaded = false;
        }

        // overload methods : for single_model mode
        void releaseTree(bool re_use) {
            releaseTree("", re_use);
        }
        void loadTree(bool re_use) {
            loadTree("", re_use);
        }
        void purgeTree(bool rmf) {
            purgeTree("", rmf);
        }

    private:
        // Recursive helper to get tree depth
        uint16_t getTreeDepthRecursive(uint16_t nodeIndex) const {
            if (nodeIndex >= nodes.size()) return 0;
            if (nodes[nodeIndex].getIsLeaf()) return 1;
            
            uint16_t leftIndex = nodes[nodeIndex].getLeftChildIndex();
            uint16_t rightIndex = nodes[nodeIndex].getRightChildIndex();
            
            uint16_t leftDepth = getTreeDepthRecursive(leftIndex);
            uint16_t rightDepth = getTreeDepthRecursive(rightIndex);
            
            return 1 + (leftDepth > rightDepth ? leftDepth : rightDepth);
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_DATA ------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    class Rf_data {
    private:
        static constexpr size_t MAX_CHUNKS_SIZE = 8192; // max bytes per chunk (8kB)

        // Chunked packed storage - eliminates both heap overhead per sample and large contiguous allocations
        mcu::vector<mcu::packed_vector<2, mcu::LARGE>> sampleChunks;  // Multiple chunks of packed features
        mcu::b_vector<uint8_t> allLabels;              // Labels storage (still contiguous for simplicity)
        uint16_t bitsPerSample;                        // Number of bits per sample (numFeatures * 2)
        uint16_t samplesEachChunk;                     // Maximum samples per chunk
        size_t size_;  
        String filename = "";

    public:
        bool isLoaded;      

        Rf_data() : isLoaded(false), size_(0), bitsPerSample(0), samplesEachChunk(0) {}
        // Constructor with filename and numFeatures - properly initializes chunk parameters
        Rf_data(const String& fname, uint16_t numFeatures) 
            : filename(fname), isLoaded(false), size_(0) {
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
        }

        // standard init 
        void init(const String& fname, uint16_t numFeatures) {
            filename = fname;
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            Serial.printf("ℹ️ Rf_data initialized: %s with %d features (%d bits/sample, %d samples/chunk)\n", 
                          filename.c_str(), numFeatures, bitsPerSample, samplesEachChunk);
            isLoaded = false;
            size_ = 0;
            sampleChunks.clear();
            allLabels.clear();
        }

        // init without numFeatures - read header file to get numFeatures and size_
        void init(const String& fname) {
            filename = fname;
            isLoaded = false;
            sampleChunks.clear();
            allLabels.clear();
            
            // read header to get size_ and bitsPerSample
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open binary file for reading.");
                if(SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }
                return;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                Serial.println("❌ Failed to read binary header.");
                file.close();
                return;
            }
            size_ = numSamples;
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            file.close();
            Serial.printf("ℹ️ Rf_data initialized: %s with %d features (%d bits/sample, %d samples/chunk, %d samples total)\n", 
                          filename.c_str(), numFeatures, bitsPerSample, samplesEachChunk, size_);
        }

        // for temporary Rf_data (without saving to SPIFFS)
        void init(uint16_t numFeatures) {   
            filename = "";
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            Serial.printf("ℹ️ Rf_data initialized with %d features (%d bits/sample, %d samples/chunk)\n", 
                          numFeatures, bitsPerSample, samplesEachChunk);
            isLoaded = false;
            size_ = 0;
            sampleChunks.clear();
            allLabels.clear();
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
        mcu::pair<size_t, size_t> getChunkLocation(size_t sampleIndex) const {
            size_t chunkIndex = sampleIndex / samplesEachChunk;
            size_t localIndex = sampleIndex % samplesEachChunk;
            return mcu::make_pair(chunkIndex, localIndex);
        }

        // Ensure we have enough chunks to store the given number of samples
        void ensureChunkCapacity(size_t totalSamples) {
            size_t requiredChunks = (totalSamples + samplesEachChunk - 1) / samplesEachChunk;
            while (sampleChunks.size() < requiredChunks) {
                mcu::packed_vector<2, mcu::LARGE> newChunk;
                // Reserve space for elements (each feature is 1 element in packed_vector<2>)
                size_t elementsPerSample = bitsPerSample / 2;  // numFeatures
                newChunk.reserve(samplesEachChunk * elementsPerSample);
                sampleChunks.push_back(newChunk); // Add new empty chunk
            }
        }

        // Helper method to reconstruct Rf_sample from chunked packed storage
        Rf_sample getSample(size_t sampleIndex) const {
            if (!isLoaded) {
                Serial.println("❌ Rf_data not loaded. Call loadData() first.");
                return Rf_sample();
            }
            if(sampleIndex >= size_){
                Serial.printf("❌ Sample index %d out of bounds (size=%d)\n", sampleIndex, size_);
                return Rf_sample();
            }
            pair<size_t, size_t> location = getChunkLocation(sampleIndex);
            return Rf_sample(
                allLabels[sampleIndex],
                sampleChunks[location.first],
                location.second * (bitsPerSample / 2),
                (location.second + 1) * (bitsPerSample / 2)
            );    
        }

        // Helper method to store Rf_sample in chunked packed storage
        void storeSample(const Rf_sample& sample, size_t sampleIndex) {
            if (!isProperlyInitialized()) {
                Serial.println("❌ Rf_data not properly initialized. Use constructor with numFeatures or loadData from another Rf_data.");
                return;
            }
            
            // Store label
            // Ensure size() reflects the highest written index. The b_vector::resize
            // changes capacity only in this library; use push_back to grow size.
            if (sampleIndex == allLabels.size()) {
                // Appending in order (fast path)
                allLabels.push_back(sample.label);
            } else if (sampleIndex < allLabels.size()) {
                // Overwrite existing position
                allLabels[sampleIndex] = sample.label;
            } else {
                // Rare case: out-of-order insert; fill gaps with 0
                allLabels.reserve(sampleIndex + 1);
                allLabels.fill(0);
                allLabels.push_back(sample.label);
            }
            
            // Ensure we have enough chunks
            ensureChunkCapacity(sampleIndex + 1);
            
            auto location = getChunkLocation(sampleIndex);
            size_t chunkIndex = location.first;
            size_t localIndex = location.second;
            
            // Store features in packed format within the specific chunk
            size_t elementsPerSample = bitsPerSample / 2;  // numFeatures
            size_t startElementIndex = localIndex * elementsPerSample;
            size_t requiredSizeInChunk = startElementIndex + elementsPerSample;
            
            if (sampleChunks[chunkIndex].size() < requiredSizeInChunk) {
                sampleChunks[chunkIndex].resize(requiredSizeInChunk);
            }
            
            // Store each feature as one element in the packed_vector<2>
            for (size_t featureIdx = 0; featureIdx < sample.features.size(); featureIdx++) {
                size_t elementIndex = startElementIndex + featureIdx;
                uint8_t featureValue = sample.features[featureIdx] & 0x03; // 2-bit mask
                
                // Store 2-bit value directly as one element
                if (elementIndex < sampleChunks[chunkIndex].size()) {
                    sampleChunks[chunkIndex].set(elementIndex, featureValue);
                }
            }
        }

    private:
        // Load data from CSV format (used only once for initial dataset conversion)
        void loadCSVData(String csvFilename, uint8_t numFeatures) {
            if(isLoaded) return;
            
            File file = SPIFFS.open(csvFilename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open CSV file for reading.");
                return;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    Serial.println("❌ CSV file is empty or missing header.");
                    file.close();
                    return;
                }
                int commaCount = 0;
                for (char c : line) {
                    if (c == ',') commaCount++;
                }
                numFeatures = commaCount;
            }

            // Set bitsPerSample and calculate chunk parameters only if not already initialized
            if (bitsPerSample == 0) {
                bitsPerSample = numFeatures * 2;
                updateSamplesEachChunk();
            } else {
                // Validate that the provided numFeatures matches the initialized bitsPerSample
                uint16_t expectedFeatures = bitsPerSample / 2;
                if (numFeatures != expectedFeatures) {
                    Serial.printf("❌ Feature count mismatch: initialized for %d features, CSV has %d\n", 
                                expectedFeatures, numFeatures);
                    file.close();
                    return;
                }
            }

            Serial.printf("📊 Loading CSV: %s (expecting %d features per sample)\n", csvFilename.c_str(), numFeatures);
            Serial.printf("📦 Chunk configuration: %d samples per chunk (%d bytes max)\n", samplesEachChunk, MAX_CHUNKS_SIZE);
            
            uint16_t linesProcessed = 0;
            uint16_t emptyLines = 0;
            uint16_t validSamples = 0;
            uint16_t invalidSamples = 0;
            
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

                uint8_t fieldIdx = 0;
                int start = 0;
                while (start < line.length()) {
                    int comma = line.indexOf(',', start);
                    if (comma < 0) comma = line.length();

                    String tok = line.substring(start, comma);
                    uint8_t v = (uint8_t)tok.toInt();

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
                    Serial.printf("❌ Line %d: Expected %d fields, got %d\n", linesProcessed, numFeatures + 1, fieldIdx);
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    Serial.printf("❌ Line %d: Expected %d features, got %d\n", linesProcessed, numFeatures, s.features.size());
                    invalidSamples++;
                    continue;
                }
                
                s.features.fit();

                // Store in chunked packed format
                storeSample(s, validSamples);
                validSamples++;
                
                if (validSamples >= 50000) {
                    Serial.println("⚠️  Reached sample limit (50000)");
                    break;
                }
            }
            size_ = validSamples;
            
            Serial.printf("📋 CSV Processing Results:\n");
            Serial.printf("   Lines processed: %d\n", linesProcessed);
            Serial.printf("   Empty lines: %d\n", emptyLines);
            Serial.printf("   Valid samples: %d\n", validSamples);
            Serial.printf("   Invalid samples: %d\n", invalidSamples);
            Serial.printf("   Total samples in memory: %d\n", size_);
            Serial.printf("   Chunks used: %d\n", sampleChunks.size());
            
            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            file.close();
            isLoaded = true;
            SPIFFS.remove(csvFilename);
            Serial.println("✅ CSV data loaded and file removed.");
        }

    public:
        int total_chunks() const {
            return size_/samplesEachChunk + (size_ % samplesEachChunk != 0 ? 1 : 0);
        }
        uint16_t total_features() const {
            return bitsPerSample / 2;
        }

        uint16_t samplesPerChunk() const {
            return samplesEachChunk;
        }

        size_t size() const {
            return size_;
        }

        // Fast accessors for training-time hot paths (avoid reconstructing Rf_sample)
        inline uint16_t num_features() const { return bitsPerSample / 2; }

        inline uint8_t getLabel(size_t sampleIndex) const {
            if (sampleIndex >= size_) return 0;
            return allLabels[sampleIndex];
        }

        inline uint8_t getFeature(size_t sampleIndex, uint16_t featureIndex) const {
            if (!isProperlyInitialized()) return 0;
            uint16_t nf = bitsPerSample / 2;
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
                Serial.println("❌ Cannot reserve space: Rf_data not properly initialized");
                return;
            }

            // Reserve space for labels
            allLabels.reserve(numSamples);

            // Ensure we have enough chunks for the requested number of samples
            ensureChunkCapacity(numSamples);

            Serial.printf("📦 Reserved space for %d samples (%d chunks)\n", 
                        numSamples, sampleChunks.size());
        }

        void convertCSVtoBinary(String csvFilename, uint8_t numFeatures = 0) {
            loadCSVData(csvFilename, numFeatures);
            releaseData(false); // Save to binary and clear memory
        }

        /**
         * @brief Save data to SPIFFS in binary format and clear from RAM.
         * @param reuse If true, keeps data in RAM after saving; if false, clears data from RAM.
         * @note: after first time rf_data created, it must be releaseData(false) to save data
         */
        void releaseData(bool reuse = true) {
            if(!isLoaded) return;
            
            if(!reuse){
                // Remove any existing file
                if (SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }

                File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
                if (!file) {
                    Serial.println("❌ Failed to open binary file for writing.");
                    return;
                }
                Serial.printf("📂 Saving data to: %s\n", filename.c_str());

                // Write binary header
                uint32_t numSamples = size_;
                uint16_t numFeatures = bitsPerSample / 2;
                
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

                // Calculate packed bytes needed for features (4 values per byte)
                uint16_t packedFeatureBytes = (numFeatures + 3) / 4;

                // Write samples WITHOUT sample IDs (using vector indices)
                for (uint32_t i = 0; i < size_; i++) {
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
                    
                    // Pack 4 feature values into each byte
                    for (size_t j = 0; j < s.features.size(); ++j) {
                        uint16_t byteIndex = j / 4;
                        uint8_t bitOffset = (j % 4) * 2;
                        uint8_t feature_value = s.features[j] & 0x03;
                        packedBuffer[byteIndex] |= (feature_value << bitOffset);
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
        }

        // Load data using sequential indices 
        void loadData(bool re_use = true) {
            if(isLoaded || filename.length() < 1) return;
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.println("❌ Failed to open binary file for reading.");
                if(SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }
                return;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                Serial.println("❌ Failed to read binary header.");
                file.close();
                return;
            }

            if(numFeatures * 2 != bitsPerSample) {
                Serial.printf("❌ Feature count mismatch: expected %d features, file has %d\n", 
                            bitsPerSample / 2, numFeatures);
                file.close();
                return;
            }
            size_ = numSamples;

            // Calculate sizes
            const uint16_t packedFeatureBytes = (numFeatures + 3) / 4; // 4 values per byte
            const size_t recordSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            const size_t elementsPerSample = numFeatures; // each feature is one element in packed_vector<2>

            // Prepare storage: labels and chunks pre-sized to avoid per-sample resizing
            allLabels.clear();
            allLabels.reserve(numSamples);
            sampleChunks.clear();
            ensureChunkCapacity(numSamples);
            // Pre-size each chunk's element count
            size_t remaining = numSamples;
            for (size_t ci = 0; ci < sampleChunks.size(); ++ci) {
                size_t chunkSamples = remaining > samplesEachChunk ? samplesEachChunk : remaining;
                size_t reqElems = chunkSamples * elementsPerSample;
                sampleChunks[ci].resize(reqElems);
                remaining -= chunkSamples;
                if (remaining == 0) break;
            }

            // Batch read to reduce SPIFFS overhead
            const size_t MAX_BATCH_BYTES = 2048; // conservative for MCU
            uint8_t* ioBuf = (uint8_t*)malloc(MAX_BATCH_BYTES);
            if (!ioBuf) {
                Serial.println("⚠️ Falling back to scalar load (no IO buffer)");
            }

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
                            Serial.println("❌ Failed to read batch from file");
                            if (ioBuf) free(ioBuf);
                            file.close();
                            return;
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

                        // Unpack features directly into chunk storage
                        for (uint16_t j = 0; j < numFeatures; ++j) {
                            uint16_t byteIndex = j / 4;
                            uint8_t bitOffset = (j % 4) * 2;
                            uint8_t fv = (packed[byteIndex] >> bitOffset) & 0x03;
                            sampleChunks[chunkIndex].set(startElementIndex + j, fv);
                        }
                    }
                } else {
                    // Fallback: per-sample small buffer
                    batchSamples = 1;
                    uint8_t lbl;
                    if (file.read(&lbl, sizeof(lbl)) != sizeof(lbl)) {
                        Serial.printf("❌ Failed to read label for sample %u\n", (unsigned)processed);
                        file.close();
                        return;
                    }
                    allLabels.push_back(lbl);
                    uint8_t packed[packedFeatureBytes];
                    if (file.read(packed, packedFeatureBytes) != packedFeatureBytes) {
                        Serial.printf("❌ Failed to read packed features for sample %u\n", (unsigned)processed);
                        file.close();
                        return;
                    }
                    auto loc = getChunkLocation(processed);
                    size_t chunkIndex = loc.first;
                    size_t localIndex = loc.second;
                    size_t startElementIndex = localIndex * elementsPerSample;
                    for (uint16_t j = 0; j < numFeatures; ++j) {
                        uint16_t byteIndex = j / 4;
                        uint8_t bitOffset = (j % 4) * 2;
                        uint8_t fv = (packed[byteIndex] >> bitOffset) & 0x03;
                        sampleChunks[chunkIndex].set(startElementIndex + j, fv);
                    }
                }

                processed += batchSamples;
            }

            if (ioBuf) free(ioBuf);

            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            isLoaded = true;
            file.close();
            if(!re_use) {
                SPIFFS.remove(filename.c_str()); // Remove file after loading in single mode
            }
            // Serial.printf("✅ Data loaded fast: %s (using %d chunks)\n", filename.c_str(), sampleChunks.size());
        }

        /**
         * @brief Load specific samples from another Rf_data source by sample IDs.
         * @param source The source Rf_data to load samples from.
         * @param sample_IDs A sorted set of sample IDs to load from the source.
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: The state of the source data will be automatically restored, no need to reload.
         */
        void loadData(Rf_data& source, const sampleID_set& sample_IDs, bool save_ram = true) {
            if (source.filename.length() == 0) {
                Serial.println("❌ Source Rf_data has no filename specified for SPIFFS loading.");
                return;
            }

            if (!SPIFFS.exists(source.filename.c_str())) {
                Serial.printf("❌ Source file does not exist: %s\n", source.filename.c_str());
                return;
            }

            File file = SPIFFS.open(source.filename.c_str(), FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open source file: %s\n", source.filename.c_str());
                return;
            }
            bool pre_loaded = source.isLoaded;
            if(pre_loaded && save_ram) {
                source.releaseData();
            }

            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                Serial.println("❌ Failed to read binary header from source file.");
                file.close();
                return;
            }

            // Clear current data and initialize parameters
            sampleChunks.clear();
            allLabels.clear();
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();

            // Calculate packed bytes needed for features (4 values per byte)
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
            size_t sampleDataSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            
            // Reserve space for requested samples
            size_t numRequestedSamples = sample_IDs.size();
            allLabels.reserve(numRequestedSamples);
            
            Serial.printf("📦 Loading %d samples from SPIFFS: %s\n", numRequestedSamples, source.filename.c_str());
            
            size_t addedSamples = 0;
            // Since sample_IDs are sorted in ascending order, we can read efficiently
            for(uint16_t sampleIdx : sample_IDs) {
                if(sampleIdx >= numSamples) {
                    Serial.printf("❌ Sample ID %d exceeds file sample count %d\n", sampleIdx, numSamples);
                    continue;
                }
                
                // Calculate file position for this sample
                size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);
                size_t sampleFilePos = headerSize + (sampleIdx * sampleDataSize);
                
                // Seek to the sample position
                if (!file.seek(sampleFilePos)) {
                    Serial.printf("❌ Failed to seek to sample %d position %d\n", sampleIdx, sampleFilePos);
                    continue;
                }
                
                Rf_sample s;
                
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    Serial.printf("❌ Failed to read label for sample %d\n", sampleIdx);
                    continue;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    Serial.printf("❌ Failed to read packed features for sample %d\n", sampleIdx);
                    continue;
                }
                
                // Unpack features from bytes
                for(uint16_t j = 0; j < numFeatures; j++) {
                    uint16_t byteIndex = j / 4;
                    uint8_t bitOffset = (j % 4) * 2;
                    uint8_t mask = 0x03 << bitOffset;
                    uint8_t feature = (packedBuffer[byteIndex] & mask) >> bitOffset;
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
                source.loadData(); // reload source if it was pre-loaded
            }
            Serial.printf("✅ Loaded %d samples from SPIFFS file: %s (using %d chunks)\n", 
                        addedSamples, source.filename.c_str(), sampleChunks.size());
        }
        
        /**
         * @brief Load a specific chunk of samples from another Rf_data source.
         * @param source The source Rf_data to load samples from.
         * @param chunkIndex The index of the chunk to load (0-based).
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: this function will call loadData(source, chunkIDs) internally.
         */
        void loadChunk(Rf_data& source, size_t chunkIndex, bool save_ram = true) {
            if(chunkIndex >= source.total_chunks()) {
                Serial.printf("❌ Chunk index %d out of bounds (total chunks=%d)\n", chunkIndex, source.total_chunks());
                return; 
            }
            bool pre_loaded = source.isLoaded;

            uint16_t startSample = chunkIndex * source.samplesEachChunk;
            uint16_t endSample = startSample + source.samplesEachChunk;
            if(endSample > source.size()) {
                endSample = source.size();
            }
            if(startSample >= endSample) {
                Serial.printf("❌ Invalid chunk range: start %d, end %d\n", startSample, endSample);
                return;
            }
            sampleID_set chunkIDs(startSample, endSample - 1);
            chunkIDs.fill();
            loadData(source, chunkIDs, save_ram);   
        }

        // add new sample to the end of the dataset
        bool add_new_sample(const Rf_sample& sample) {
            // If data is loaded, add to in-memory chunked packed storage
            if (isLoaded) {
                // Set bitsPerSample if not set yet
                if (bitsPerSample == 0) {
                    bitsPerSample = sample.features.size() * 2;
                    updateSamplesEachChunk();
                }
                
                // Validate feature count
                if (sample.features.size() * 2 != bitsPerSample) {
                    Serial.printf("❌ Feature count mismatch: expected %d, got %d\n", 
                                bitsPerSample / 2, sample.features.size());
                    return false;
                }
                
                storeSample(sample, size_);
                size_++;
                allLabels.fit();
                for (auto& chunk : sampleChunks) {
                    chunk.fit();
                }
                releaseData(false); // Save to SPIFFS
                return true;
            }

            File file;
            uint16_t numFeatures = sample.features.size();
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;

            if (SPIFFS.exists(filename.c_str())) {
                // File exists, open to read header and then append
                file = SPIFFS.open(filename.c_str(), "r+");
                if (!file) {
                    Serial.printf("❌ Failed to open existing file for update: %s\n", filename.c_str());
                    return false;
                }

                // Read current sample count and update it
                uint32_t currentSamples;
                file.read((uint8_t*)&currentSamples, sizeof(currentSamples));
                currentSamples++;
                
                // Rewind and write the new sample count
                file.seek(0);
                file.write((uint8_t*)&currentSamples, sizeof(currentSamples));
                file.close();

                // Re-open in append mode to add the new sample
                file = SPIFFS.open(filename.c_str(), "a");

            } else {
                // File doesn't exist, create it and write the header
                file = SPIFFS.open(filename.c_str(), "w");
                if (!file) {
                    Serial.printf("❌ Failed to create new file: %s\n", filename.c_str());
                    return false;
                }
                uint32_t numSamples = 1;
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));
                
                // Set bitsPerSample for future use
                bitsPerSample = numFeatures * 2;
                updateSamplesEachChunk();
                size_ = 1;
            }

            if (!file) {
                Serial.printf("❌ File operation failed for: %s\n", filename.c_str());
                return false;
            }

            // Append the new sample data (no sample ID needed)
            file.write(&sample.label, sizeof(sample.label));

            // Pack and write features
            uint8_t packedBuffer[packedFeatureBytes];
            memset(packedBuffer, 0, packedFeatureBytes);

            for (size_t i = 0; i < numFeatures; ++i) {
                uint16_t byteIndex = i / 4;
                uint8_t bitOffset = (i % 4) * 2;
                uint8_t feature_value = sample.features[i] & 0x03;
                packedBuffer[byteIndex] |= (feature_value << bitOffset);
            }
            file.write(packedBuffer, packedFeatureBytes);
            file.close();
            return true;
        }

        /**
         *@brief: copy assignment (but not copy filename to avoid SPIFFS over-writing)
         *@note : Rf_data will be put into release state. loadData() to reload into RAM if needed.
        */
        Rf_data& operator=(const Rf_data& other) {
            purgeData(); // Clear existing data safely
            if (this != &other) {
                cloneFile(other.filename, filename);
                bitsPerSample = other.bitsPerSample;
                samplesEachChunk = other.samplesEachChunk;
                size_ = other.size_;
                // Deep copy of labels
                allLabels = other.allLabels; // b_vector has its own copy semantics
            }
            return *this;   
        }

        // FIXED: Safe data purging
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

            // Then remove the SPIFFS file if one was specified
            if (filename.length() > 0 && SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                Serial.printf("🗑️ Deleted file %s\n", filename.c_str());
            }
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_data);
            total += allLabels.capacity() * sizeof(uint8_t);
            for (const auto& chunk : sampleChunks) {
                total += sizeof(mcu::packed_vector<2, mcu::LARGE>);
                total += chunk.capacity() * sizeof(uint8_t); // each element is 2 bits, but stored in bytes
            }
            return total;
        }
    };
   /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_CONFIG ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef enum Rf_training_score : uint8_t {
        OOB_SCORE = 0x00,   // default 
        VALID_SCORE = 0x01,
        K_FOLD_SCORE = 0x02
    } Rf_training_score;

    // Configuration class for Random Forest parameters
    class Rf_config {
    public:
        // Core model configuration
        uint8_t num_trees;
        uint32_t random_seed;
        uint8_t min_split;
        uint8_t max_depth;
        bool use_boostrap;
        bool use_gini;
        uint8_t k_fold;
        float boostrap_ratio; // Ratio of bootstrap samples to original size
        float unity_threshold;
        float impurity_threshold;
        float train_ratio;
        float test_ratio;
        float valid_ratio;
        Rf_training_score training_score;
        uint8_t train_flag;
        float result_score;
        bool keep_trees_in_memory; // Performance flag to avoid releasing trees during evaluation
        uint32_t estimatedRAM;

        // Dataset parameters (set after loading data)
        uint16_t num_samples;
        uint16_t num_features;
        uint8_t num_labels;
        mcu::b_vector<uint8_t, mcu::SMALL> min_split_range;
        mcu::b_vector<uint8_t, mcu::SMALL> max_depth_range;  
        
        String filename;
        bool isLoaded;

        Rf_config() : isLoaded(false) {
            // Set default values
            num_trees = 20;
            random_seed = 37;
            min_split = 2;
            max_depth = 13;
            use_boostrap = true;
            boostrap_ratio = 0.632f; // Default bootstrap ratio
            use_gini = false;
            k_fold = 4;
            unity_threshold = 0.125;
            impurity_threshold = 0.1;
            train_ratio = 0.7;
            test_ratio = 0.15;
            valid_ratio = 0.15;
            training_score = OOB_SCORE;
            train_flag = 0x01; // ACCURACY
            result_score = 0.0;
            keep_trees_in_memory = true; // Default to keeping trees for better performance
            estimatedRAM = 0;
            filename = "";
        }

        void init(const String& fn){
            filename = fn;
            isLoaded = false;
        }

        // Load configuration from JSON file in SPIFFS
        void loadConfig(bool re_use = true) {
            if (isLoaded) return;

            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open config file: %s\n", filename.c_str());
                Serial.println("Switching to default configuration.");
                return;
            }

            String jsonString = file.readString();
            file.close();

            // Parse JSON manually (simple parsing for known structure)
            parseJSONConfig(jsonString);
            isLoaded = true;
            
            Serial.printf("✅ Config loaded: %s\n", filename.c_str());
            Serial.printf("   Trees: %d, max_depth: %d, min_split: %d\n", num_trees, max_depth, min_split);
            Serial.printf("   Estimated RAM: %d bytes\n", estimatedRAM);

            if(!re_use) {
                SPIFFS.remove(filename.c_str()); // Remove file after loading in single mode
            }
        }

        // Save configuration to JSON file in SPIFFS  
        void releaseConfig(bool re_use = true) {
            // Read existing file to preserve timestamp and author
            if(!re_use){                
                String existingTimestamp = "";
                String existingAuthor = "Viettran";
                
                if (SPIFFS.exists(filename.c_str())) {
                    File readFile = SPIFFS.open(filename.c_str(), FILE_READ);
                    if (readFile) {
                        String jsonContent = readFile.readString();
                        readFile.close();
                        existingTimestamp = extractStringValue(jsonContent, "timestamp");
                        existingAuthor = extractStringValue(jsonContent, "author");
                    }
                    SPIFFS.remove(filename.c_str());
                }

                File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
                if (!file) {
                    Serial.printf("❌ Failed to create config file: %s\n", filename.c_str());
                    return;
                }

                // Write JSON format preserving timestamp and author
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
                file.printf("  \"useGini\": %s,\n", use_gini ? "true" : "false");
                file.printf("  \"trainingScore\": \"%s\",\n", getTrainingScoreString(training_score).c_str());
                file.printf("  \"k_fold\": %d,\n", k_fold);
                file.printf("  \"unityThreshold\": %.3f,\n", unity_threshold);
                file.printf("  \"impurityThreshold\": %.1f,\n", impurity_threshold);
                file.printf("  \"trainFlag\": \"%s\",\n", getFlagString(train_flag).c_str());
                file.printf("  \"resultScore\": %.6f,\n", result_score);
                file.printf("  \"Estimated RAM (bytes)\": %d,\n", estimatedRAM);
                
                // Preserve existing timestamp and author
                if (existingTimestamp.length() > 0) {
                    file.printf("  \"timestamp\": \"%s\",\n", existingTimestamp.c_str());
                }
                if (existingAuthor.length() > 0) {
                    file.printf("  \"author\": \"%s\"\n", existingAuthor.c_str());
                } else {
                    // Remove trailing comma if no author
                    file.seek(file.position() - 2); // Go back to remove ",\n"
                    file.println("");
                }
                
                file.println("}");
                file.close();
            }
            
            // Clear from RAM  
            purgeConfig();
            
            Serial.printf("✅ Config saved to: %s\n", filename.c_str());
        }

        // Update timestamp in the JSON file
        void update_timestamp() {
            if (!SPIFFS.exists(filename.c_str())) return;
            
            // Read existing file
            File readFile = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!readFile) return;
            
            String jsonContent = readFile.readString();
            readFile.close();
            
            // Get current timestamp
            String currentTime = String(millis()); // Simple timestamp, can be improved
            
            // Find and replace timestamp
            int timestampStart = jsonContent.indexOf("\"timestamp\":");
            if (timestampStart != -1) {
                int valueStart = jsonContent.indexOf("\"", timestampStart + 12);
                int valueEnd = jsonContent.indexOf("\"", valueStart + 1);
                if (valueStart != -1 && valueEnd != -1) {
                    String newContent = jsonContent.substring(0, valueStart + 1) + 
                                    currentTime + 
                                    jsonContent.substring(valueEnd);
                    
                    // Write updated content
                    SPIFFS.remove(filename.c_str());
                    File writeFile = SPIFFS.open(filename.c_str(), FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(newContent);
                        writeFile.close();
                    }
                }
            }
        }

        void purgeConfig() {
            isLoaded = false;
        }

    private:
        // Simple JSON parser for configuration
        void parseJSONConfig(const String& jsonStr) {
            // Use the actual keys from digit_data_config.json
            num_trees = extractIntValue(jsonStr, "numTrees");              // ✅ Fixed
            random_seed = extractIntValue(jsonStr, "randomSeed");          // ✅ New parameter
            min_split = extractIntValue(jsonStr, "minSplit");              // ✅ Fixed  
            max_depth = extractIntValue(jsonStr, "maxDepth");              // ✅ Fixed
            use_boostrap = extractBoolValue(jsonStr, "useBootstrap");      // ✅ Fixed
            boostrap_ratio = extractFloatValue(jsonStr, "boostrapRatio");  // ✅ Fixed
            use_gini = extractBoolValue(jsonStr, "useGini");               // ✅ Fixed
            k_fold = extractIntValue(jsonStr, "k_fold");                   // ✅ Already correct
            unity_threshold = extractFloatValue(jsonStr, "unityThreshold"); // ✅ Fixed
            impurity_threshold = extractFloatValue(jsonStr, "impurityThreshold"); // ✅ Fixed
            train_ratio = extractFloatValue(jsonStr, "train_ratio");       // ✅ Fixed
            test_ratio = extractFloatValue(jsonStr, "test_ratio");         // ✅ New parameter
            valid_ratio = extractFloatValue(jsonStr, "valid_ratio");       // ✅ Fixed
            training_score = parseTrainingScore(extractStringValue(jsonStr, "trainingScore")); // ✅ New parameter
            train_flag = parseFlagValue(extractStringValue(jsonStr, "trainFlag")); // ✅ Fixed
            result_score = extractFloatValue(jsonStr, "resultScore");       // ✅ Fixed
            estimatedRAM = extractIntValue(jsonStr, "Estimated RAM (bytes)"); // ✅ Already correct
        }

        // Convert flag string to uint8_t
        uint8_t parseFlagValue(const String& flagStr) {
            if (flagStr == "ACCURACY") return 0x01;
            if (flagStr == "PRECISION") return 0x02;
            if (flagStr == "RECALL") return 0x04;
            if (flagStr == "F1_SCORE") return 0x08;
            if (flagStr == "EARLY_STOP") return 0x00;
            return 0x01; // Default to ACCURACY
        }

        // Convert uint8_t flag to string
        String getFlagString(uint8_t flag) {
            switch(flag) {
                case 0x01: return "ACCURACY";
                case 0x02: return "PRECISION";
                case 0x04: return "RECALL";
                case 0x08: return "F1_SCORE";
                case 0x00: return "EARLY_STOP";
                default: return "ACCURACY";
            }
        }

        // Convert string to Rf_training_score enum
        Rf_training_score parseTrainingScore(const String& scoreStr) {
            if (scoreStr == "oob_score") return OOB_SCORE;
            if (scoreStr == "valid_score") return VALID_SCORE;
            if (scoreStr == "k-fold_score") return K_FOLD_SCORE;
            return VALID_SCORE; // Default to VALID_SCORE
        }

        // Convert Rf_training_score enum to string
        String getTrainingScoreString(Rf_training_score score) {
            switch(score) {
                case OOB_SCORE: return "oob_score";
                case VALID_SCORE: return "valid_score";
                case K_FOLD_SCORE: return "k_fold_score";
                default: return "valid_score";
            }
        }

        uint32_t extractIntValue(const String& json, const String& key) {
            int keyIndex = json.indexOf("\"" + key + "\"");
            if (keyIndex == -1) return 0;
            
            int colonIndex = json.indexOf(":", keyIndex);
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
            
            int colonIndex = json.indexOf(":", keyIndex);
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
            
            int colonIndex = json.indexOf(":", keyIndex);
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
            
            int colonIndex = json.indexOf(":", keyIndex);
            if (colonIndex == -1) return "";
            
            int firstQuoteIndex = json.indexOf("\"", colonIndex);
            if (firstQuoteIndex == -1) return "";
            
            int secondQuoteIndex = json.indexOf("\"", firstQuoteIndex + 1);
            if (secondQuoteIndex == -1) return "";
            
            return json.substring(firstQuoteIndex + 1, secondQuoteIndex);
        }
        public:
        bool use_validation() const {
            return valid_ratio > 0.0f;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_config);
            total += min_split_range.capacity() * sizeof(uint8_t);
            total += max_depth_range.capacity() * sizeof(uint8_t);
            return total;
        }
    };


    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_BASE -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // flags indicating the status of member files
    typedef enum Rf_base_flags : uint8_t{
        EXIST_BASE_DATA         = 0x01,           // base data file exists
        EXIST_DATA_PARAMS       = 0x02,          // data_params file exists
        EXIST_CATEGORIZER       = 0x04,         // categorizer file exists
        ABLE_TO_INFERENCE       = 0x08,        // able to run inference (all tree files exist)
        ABLE_TO_TRAINING        = 0x10,        // able to re_train
        UNIFIED_MODEL_FORMAT    = 0x20         // model uses unified format (model_name_forest.bin)
    } Rf_base_flags;

    // Base file management class for Random Forest project status
    class Rf_base {
        constexpr static size_t MAX_INFER_LOGFILE_SIZE = 2048; // Max log file size in bytes - about 16000 inferences (1 bits per inference)
    private:
        uint8_t flags; // flags indicating the status of member files
        mcu::b_vector<bool> buffer; //buffer for logging inference results (with internal 16 slots on stack - fast access)
        String model_name;
    public:
        Rf_base() : flags(static_cast<Rf_base_flags>(0)), model_name("") {}
        Rf_base(const char* bn) : flags(static_cast<Rf_base_flags>(0)), model_name(bn) {
            init(bn);
        }

        void init(const char* model_name){
            if (!model_name || strlen(model_name) == 0) {
                Serial.println("❌ Base name is empty.");
                return;
            } else {
                this->model_name = String(model_name);
                
                // BaseFile will now always have the structure "/model_name_nml.bin"
                String baseFile_bin = "/" + this->model_name + "_nml.bin";
                
                // Check if binary base file exists
                if(SPIFFS.exists(baseFile_bin.c_str())) {
                    // setup flags to indicate base file exists
                    flags = static_cast<Rf_base_flags>(EXIST_BASE_DATA);
                    
                    // generate filenames for data_params and categorizer based on model_name
                    String dataParamsFile = "/" + this->model_name + "_dp.csv"; // data_params file
                    String categorizerFile = "/" + this->model_name + "_ctg.csv"; // categorizer file

                    //  check categorizer file
                    if(SPIFFS.exists(categorizerFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_CATEGORIZER);
                    } else {
                        Serial.printf("❌ No categorizer file found : %s\n", categorizerFile.c_str());
                        Serial.println("-> Model still able to re_train or run inference, but cannot re_train with new data later.");
                    }
                    // check data_params file
                    if(SPIFFS.exists(dataParamsFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_DATA_PARAMS);
                        // If data_params exists, able to training
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                    } else {
                        Serial.printf("❌ No data_parameters file found: %s\n", dataParamsFile.c_str());
                        Serial.println("Re_training and inference are not available..\n");
                    }

                    // Check for tree files to set ABLE_TO_INFERENCE flag
                    // Support both individual trees and unified model format
                    String unifiedModelFile = String("/") + model_name + "_forest.bin";
                    bool all_trees_exist = false;
                    uint8_t tree_count = 0;
                    
                    // First check for unified model format
                    if (SPIFFS.exists(unifiedModelFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(UNIFIED_MODEL_FORMAT);
                        all_trees_exist = true;
                        Serial.printf("✅ Found unified model file: %s\n", unifiedModelFile.c_str());
                    } else {
                        // Check for individual tree files starting from tree_0.bin
                        for(uint8_t i = 0; i < 100; i++) { // Max 100 trees check
                            String treeFile = String("/") + model_name + "_tree_" + String(i) + ".bin";
                            if (SPIFFS.exists(treeFile.c_str())) {
                                tree_count++;
                            } else {
                                break; // Stop when we find a missing tree file
                            }
                        }
                        
                        if (tree_count > 0) {
                            all_trees_exist = true;
                            Serial.printf("✅ Found %d individual tree files\n", tree_count);
                        }
                    }
                    
                    if(all_trees_exist && (flags & EXIST_CATEGORIZER)) {
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                        if (flags & UNIFIED_MODEL_FORMAT) {
                            Serial.println("✅ Inference enabled (unified model format)");
                        } else {
                            Serial.printf("✅ Inference enabled (%d individual tree files)\n", tree_count);
                        }
                    } 
                    
                } else {
                    Serial.printf("❌ Base file does not exist: %s\n", baseFile_bin.c_str());
                    // setup flags to indicate no base file
                    flags = static_cast<Rf_base_flags>(0);
                    return;
                }
            }
        }

        // Get base name
        String get_model_name() const {
            return model_name;
        }

        void set_model_name(const char* bn) {
            String old_model_name = model_name;
            if (bn && strlen(bn) > 0) {
                model_name = String(bn);
                // find and rename all existing related files

                // base file
                String old_baseFile = "/" + old_model_name + "_nml.bin";
                String new_baseFile = "/" + model_name + "_nml.bin";
                cloneFile(old_baseFile, new_baseFile);
                SPIFFS.remove(old_baseFile.c_str());

                // data_params file
                String old_dpFile = "/" + old_model_name + "_dp.csv";
                String new_dpFile = "/" + model_name + "_dp.csv";
                cloneFile(old_dpFile, new_dpFile);
                SPIFFS.remove(old_dpFile.c_str());

                // categorizer file
                String old_ctgFile = "/" + old_model_name + "_ctg.csv";
                String new_ctgFile = "/" + model_name + "_ctg.csv";
                cloneFile(old_ctgFile, new_ctgFile);
                SPIFFS.remove(old_ctgFile.c_str());

                // inference log file
                String old_logFile = "/" + old_model_name + "_infer_log.bin";
                String new_logFile = "/" + model_name + "_infer_log.bin";
                cloneFile(old_logFile, new_logFile);
                SPIFFS.remove(old_logFile.c_str());

                // node predictor file
                String old_nodePredFile = "/" + old_model_name + "_node_pred.bin";
                String new_nodePredFile = "/" + model_name + "_node_pred.bin";
                cloneFile(old_nodePredFile, new_nodePredFile);
                SPIFFS.remove(old_nodePredFile.c_str());

                // config file
                String old_configFile = "/" + old_model_name + "_config.json";
                String new_configFile = "/" + model_name + "_config.json";
                cloneFile(old_configFile, new_configFile);
                SPIFFS.remove(old_configFile.c_str());

                // tree files - handle both individual and unified formats
                String old_unifiedFile = "/" + old_model_name + "_forest.bin";
                String new_unifiedFile = "/" + model_name + "_forest.bin";
                
                if (SPIFFS.exists(old_unifiedFile.c_str())) {
                    // Handle unified model format
                    cloneFile(old_unifiedFile, new_unifiedFile);
                    SPIFFS.remove(old_unifiedFile.c_str());
                } else {
                    // Handle individual tree files
                    for(uint8_t i = 0; i < 50; i++) { // Max 50 trees check
                        String old_treeFile = "/" + old_model_name + "_tree_" + String(i) + ".bin";
                        String new_treeFile = "/" + model_name + "_tree_" + String(i) + ".bin";
                        if (SPIFFS.exists(old_treeFile.c_str())) {
                            cloneFile(old_treeFile, new_treeFile);
                            SPIFFS.remove(old_treeFile.c_str());
                        }else{
                            break; // Stop when we find a missing tree file
                        }
                    }
                }
                // Re-initialize flags based on new base name
                init(model_name.c_str());  
            }
        }

        // for Rf_data: baseData 
        String get_baseFile() const {
            return "/" + model_name + "_nml.bin";
        }

        // for Rf_config 
        String get_dpFile() const {
            return "/" + model_name + "_dp.csv";
        }

        // for Rf_categorizer
        String get_ctgFile() const {
            return "/" + model_name + "_ctg.csv";
        }

        // for Rf_base 
        String get_inferenceLogFile() const {
            return "/" + model_name + "_infer_log.bin";
        }

        // for Rf_config
        String get_configFile() const {
            return "/" + model_name + "_config.json";
        }

        // for Rf_node_predictor
        String get_nodePredictFile() const {
            return "/" + model_name + "_node_pred.bin";
        }

        // for unified model format (all trees in one file)
        String get_unifiedModelFile() const {
            return "/" + model_name + "_forest.bin";
        }

        // for individual tree files (tree index required)
        String get_treeFile(uint8_t tree_index) const {
            return "/" + model_name + "_tree_" + String(tree_index) + ".bin";
        }

        // Check if base file is in CSV format (always false now as we only use binary)
        bool baseFile_is_csv() const {
            return false; // Always binary format now
        }

        // Check if model uses unified format (model_name_forest.bin)
        bool is_unified_model() const {
            return (flags & UNIFIED_MODEL_FORMAT) != 0;
        }

        bool baseFile_exists() const {
            return (flags & EXIST_BASE_DATA) != 0;
        }

        bool dataParams_exists() const {
            return (flags & EXIST_DATA_PARAMS) != 0;
        }
        
        bool categorizer_exists() const {
            return (flags & EXIST_CATEGORIZER) != 0;
        }

        // Fast check for able to training
        bool able_to_training() const {
            return (flags & ABLE_TO_TRAINING) != 0;
        }

        // fast check for able to inference 
        bool able_to_inference(uint8_t num_trees) const {
            return (flags & ABLE_TO_INFERENCE) != 0;
        }

        // overloaded method for unified models (no tree count needed)
        bool able_to_inference() const {
            return (flags & ABLE_TO_INFERENCE) != 0;
        }

        // Get information about the model format and files
        void print_model_info() const {
            Serial.printf("📋 Model: %s\n", model_name.c_str());
            Serial.printf("   Base data: %s\n", baseFile_exists() ? "✅" : "❌");
            Serial.printf("   Data params: %s\n", dataParams_exists() ? "✅" : "❌");
            Serial.printf("   Categorizer: %s\n", categorizer_exists() ? "✅" : "❌");
            Serial.printf("   Training ready: %s\n", able_to_training() ? "✅" : "❌");
            Serial.printf("   Inference ready: %s\n", able_to_inference() ? "✅" : "❌");
            
            if (is_unified_model()) {
                Serial.printf("   Model file: %s\n", get_unifiedModelFile().c_str());
            } else if (able_to_inference()) {
                Serial.println("   Individual tree files found");
            }
        }

    private:
        // Helper method to trim log file by removing oldest bytes
        void trim_log_file(const String& logFile, size_t bytes_to_remove) {
            if(!SPIFFS.exists(logFile.c_str())) {
                return;
            }
            
            File originalFile = SPIFFS.open(logFile.c_str(), FILE_READ);
            if(!originalFile) {
                Serial.printf("❌ Failed to open log file for trimming: %s\n", logFile.c_str());
                return;
            }
            
            size_t original_size = originalFile.size();
            if(original_size <= bytes_to_remove) {
                // If file is smaller than or equal to bytes to remove, just delete it
                originalFile.close();
                SPIFFS.remove(logFile.c_str());
                Serial.printf("🗑️ Removed entire log file (%zu bytes <= %zu bytes to remove)\n", 
                            original_size, bytes_to_remove);
                return;
            }
            
            // Create temporary file for trimmed data
            String tempFile = logFile + ".tmp";
            File trimmedFile = SPIFFS.open(tempFile.c_str(), FILE_WRITE);
            if(!trimmedFile) {
                Serial.printf("❌ Failed to create temporary file: %s\n", tempFile.c_str());
                originalFile.close();
                return;
            }
            
            // Skip the first bytes_to_remove bytes (oldest data)
            originalFile.seek(bytes_to_remove);
            
            // Copy remaining data to temporary file
            uint8_t buffer_chunk[64]; // Use small buffer for ESP32 memory constraints
            size_t bytes_copied = 0;
            while(originalFile.available()) {
                size_t bytes_to_read = min(sizeof(buffer_chunk), (size_t)originalFile.available());
                size_t bytes_read = originalFile.read(buffer_chunk, bytes_to_read);
                if(bytes_read > 0) {
                    trimmedFile.write(buffer_chunk, bytes_read);
                    bytes_copied += bytes_read;
                } else {
                    break;
                }
            }
            
            originalFile.close();
            trimmedFile.close();
            
            // Replace original file with trimmed file
            SPIFFS.remove(logFile.c_str());
            if(SPIFFS.rename(tempFile.c_str(), logFile.c_str())) {
                Serial.printf("✂️ Trimmed log file: removed %zu bytes, kept %zu bytes\n", 
                            bytes_to_remove, bytes_copied);
            } else {
                Serial.printf("❌ Failed to rename trimmed file from %s to %s\n", 
                            tempFile.c_str(), logFile.c_str());
                SPIFFS.remove(tempFile.c_str()); // Clean up temp file
            }
        }

    public:
        void log_inference(bool result){
            if(buffer.size() < 16){     // just log when buffer is full(16 results), avoid file write too often
                buffer.push_back(result);       
            } else {
                // Buffer full, write to file and clear buffer
                String logFile = get_inferenceLogFile();
                
                // Check if log file needs size management
                bool need_trim = false;
                if(SPIFFS.exists(logFile.c_str())) {
                    File checkFile = SPIFFS.open(logFile.c_str(), FILE_READ);
                    if(checkFile) {
                        size_t current_size = checkFile.size();
                        checkFile.close();
                        
                        if(current_size >= MAX_INFER_LOGFILE_SIZE) {
                            need_trim = true;
                            Serial.printf("📏 Log file size (%zu bytes) exceeds limit (%zu bytes), trimming oldest 128 results\n", 
                                        current_size, MAX_INFER_LOGFILE_SIZE);
                        }
                    }
                }
                
                // If file needs trimming, remove oldest 128 inference results (16 bytes)
                if(need_trim) {
                    trim_log_file(logFile, 16); // Remove 16 bytes (128 bits = 128 inference results)
                }
                
                File file = SPIFFS.open(logFile.c_str(), FILE_APPEND);
                if (!file) {
                    // If file doesn't exist, create it
                    file = SPIFFS.open(logFile.c_str(), FILE_WRITE);
                    if (!file) {
                        Serial.printf("❌ Failed to create inference log file: %s\n", logFile.c_str());
                        return;
                    }
                }

                // Pack 16 boolean values into 2 bytes (8 bits per byte)
                uint8_t packed_bytes[2] = {0, 0};
                for(uint8_t i = 0; i < 16; i++) {
                    if(buffer[i]) {
                        uint8_t byte_index = i / 8;      // Which byte (0 or 1)
                        uint8_t bit_index = i % 8;       // Which bit in that byte
                        packed_bytes[byte_index] |= (1 << bit_index);
                    }
                }
                
                // Write packed data to file
                file.write(packed_bytes, 2);
                file.close();
                
                // Clear buffer and add current result
                buffer.clear();
                buffer.push_back(result);
                
                Serial.printf("📝 Logged 16 inference results to %s\n", logFile.c_str());
            }
        }

        // Get inference statistics: total inferences and correct inferences
        mcu::pair<size_t, size_t> get_inference_stats() const {
            size_t total_inferences = 0;
            size_t correct_inferences = 0;
            
            String logFile = get_inferenceLogFile();
            if(!SPIFFS.exists(logFile.c_str())) {
                return mcu::make_pair(total_inferences, correct_inferences);
            }
            
            File file = SPIFFS.open(logFile.c_str(), FILE_READ);
            if(!file) {
                return mcu::make_pair(total_inferences, correct_inferences);
            }
            
            uint8_t packed_bytes[2];
            while(file.available() >= 2) {
                if(file.read(packed_bytes, 2) == 2) {
                    // Unpack and count
                    for(uint8_t i = 0; i < 16; i++) {
                        uint8_t byte_index = i / 8;
                        uint8_t bit_index = i % 8;
                        bool result = (packed_bytes[byte_index] & (1 << bit_index)) != 0;
                        
                        total_inferences++;
                        if(result) {
                            correct_inferences++;
                        }
                    }
                }
            }
            
            // Add current buffer results
            for(uint8_t i = 0; i < buffer.size(); i++) {
                total_inferences++;
                if(buffer[i]) {
                    correct_inferences++;
                } 
            }
            file.close();
            return mcu::make_pair(total_inferences, correct_inferences);
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_base);
            total += buffer.capacity() * sizeof(bool);
            total += model_name.length() * sizeof(char);
            return total;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------- RF_NODE_PREDCITOR ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    struct node_data {
        uint8_t min_split;
        uint16_t max_depth;
        uint16_t total_nodes;
        
        node_data() : min_split(0), max_depth(0), total_nodes(0) {}
        node_data(uint8_t min_split, uint16_t max_depth) 
            : min_split(min_split), max_depth(max_depth), total_nodes(0) {}
        node_data(uint8_t min_split, uint16_t max_depth, uint16_t total_nodes) 
            : min_split(min_split), max_depth(max_depth), total_nodes(total_nodes) {}
    };

    class Rf_node_predictor {
    public:
        float coefficients[3];  // bias, min_split_coeff, max_depth_coeff
        bool is_trained;
        b_vector<node_data> buffer;
    private:
        String filename;
        String node_predictor_log;
        
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
                return 100.0f; // default estimate
            }
            // Simple heuristic: more nodes = better accuracy
            float estimate = 100.0f - data.min_split * 12 + data.max_depth * 3;
            return estimate < 10.0f ? 10.0f : estimate; // ensure reasonable minimum
        }

    public:
        uint8_t accuracy;      // in percentage
        uint8_t peak_percent;  // number of nodes at depth with maximum number of nodes / total number of nodes in tree
        
        Rf_node_predictor() : filename(""), is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
        }

        Rf_node_predictor(const char* fn) : filename(fn), is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
            // get logfile path from filename : "/model_name_node_pred.bin" -> "/model_name_node_log.csv"
            node_predictor_log = "";
            if (filename.length() > 0) {
                const int suf_len = 14; // "_node_pred.bin"
                if (filename.length() >= suf_len &&
                    filename.substring(filename.length() - suf_len) == "_node_pred.bin") {
                    node_predictor_log = filename.substring(0, filename.length() - suf_len) + "_node_log.csv";
                } else {
                    // Fallback: strip extension and append _node_log.csv
                    int dot = filename.lastIndexOf('.');
                    String base = (dot >= 0) ? filename.substring(0, dot) : filename;
                    if (base.length() >= 10 && base.substring(base.length() - 10) == "_node_pred") {
                        base.remove(base.length() - 10);
                    }
                    node_predictor_log = base + "_node_log.csv";
                }
            }
        }

        void init(const String& fn) {
            filename = fn;
            is_trained = false;
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
            // get logfile path from filename : "/model_name_node_pred.bin" -> "/model_name_node_log.csv"
            node_predictor_log = "";
            if (filename.length() > 0) {
                const int suf_len = 14; // "_node_pred.bin"
                if (filename.length() >= suf_len &&
                    filename.substring(filename.length() - suf_len) == "_node_pred.bin") {
                    node_predictor_log = filename.substring(0, filename.length() - suf_len) + "_node_log.csv";
                } else {
                    // Fallback: strip extension and append _node_log.csv
                    int dot = filename.lastIndexOf('.');
                    String base = (dot >= 0) ? filename.substring(0, dot) : filename;
                    if (base.length() >= 10 && base.substring(base.length() - 10) == "_node_pred") {
                        base.remove(base.length() - 10);
                    }
                    node_predictor_log = base + "_node_log.csv";
                }
            }
            // check if node_log file exists, if not create with header
            if (node_predictor_log.length() > 0 && !SPIFFS.exists(node_predictor_log.c_str())) {
                File logFile = SPIFFS.open(node_predictor_log.c_str(), FILE_WRITE);
                if (logFile) {
                    logFile.println("min_split,max_depth,total_nodes");
                    logFile.close();
                }
            }
        }
        
        // Load trained model from SPIFFS (updated format without version)
        bool loadPredictor() {
            if(is_trained) return true;
            if (!SPIFFS.exists(filename.c_str())) {
                Serial.printf("❌ No predictor file found: %s !\n", filename.c_str());
                Serial.println("Switching to use default predictor.");
                return false;
            }
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open predictor file: %s\n", filename.c_str());
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                Serial.printf("❌ Invalid predictor file format: %s\n", filename.c_str());
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                Serial.println("❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                Serial.println("❌ Failed to read accuracy");
                file.close();
                return false;
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                Serial.println("❌ Failed to read peak_percent");
                file.close();
                return false;
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients) || num_coefficients != 3) {
                Serial.printf("❌ Invalid coefficient count: %d (expected 3)\n", num_coefficients);
                file.close();
                return false;
            }
            
            // Read coefficients
            if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                Serial.println("❌ Failed to read coefficients");
                file.close();
                return false;
            }
            
            file.close();
            
            // Only set is_trained to true if the file was actually trained
            if (file_is_trained) {
                is_trained = true;
                
                // Fix PC version's peak_percent bug where it often saves 0%
                if (peak_percent == 0) {
                    peak_percent = 30; // Use reasonable default for binary trees
                    Serial.printf("⚠️  Fixed peak_percent from 0%% to 30%% (PC version bug)\n");
                }
                
                Serial.printf("✅ Node_predictor loaded: %s (accuracy: %d%%, peak: %d%%)\n", 
                            filename.c_str(), accuracy, peak_percent);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
            } else {
                Serial.printf("⚠️  predictor file exists but is not trained: %s\n", filename.c_str());
                is_trained = false;
            }
            
            return file_is_trained;
        }
        
        // Save trained predictor to SPIFFS
        bool savePredictor() {
            // Remove existing file
            if (SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
            }
            
            File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
            if (!file) {
                Serial.printf("❌ Failed to create node_predictor file: %s\n", filename.c_str());
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
            
            Serial.printf("✅ Node_predictor saved: %s (%d bytes, accuracy: %d%%, peak: %d%%)\n", 
                        filename.c_str(), 
                        sizeof(magic) + sizeof(is_trained) + sizeof(accuracy) + sizeof(peak_percent) + 
                        sizeof(num_coefficients) + sizeof(float) * 3, accuracy, peak_percent);
            return true;
        }
        
        // Predict number of nodes for given parameters
        uint16_t estimate(const node_data& data) {
            if(!is_trained) {
                if(!loadPredictor()){
                    return manual_estimate(data); // Use manual estimate if predictor is disabled 
                }
            }
            
            float prediction = evaluate_formula(data);
            return static_cast<uint16_t>(prediction + 0.5f); // Round to nearest integer
        }
        uint16_t estimate(uint8_t min_split, uint16_t max_depth) {
            node_data data(min_split, max_depth);
            return estimate(data);
        }
        
        // Retrain the predictor using data from rf_tree_log.csv (synchronized with PC version)
        bool re_train(bool save_after_retrain = true) {
            if(!can_retrain()) {
                Serial.println("❌ No training data available for retraining.");
                return false;
            }
            if(buffer.size() > 0){
                add_new_samples(buffer);
            }
            buffer.clear();
            buffer.fit();

            File file = SPIFFS.open(node_predictor_log, FILE_READ);
            if (!file) {
                Serial.printf("❌ Failed to open training log: %s\n", node_predictor_log);
                return false;
            }
            
            Serial.println("🔄 Retraining node predictor from CSV data...");
            

            mcu::b_vector<node_data> training_data;
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
                Serial.printf("❌ Insufficient training data: %d samples (need at least 3)\n", training_data.size());
                return false;
            }
            
            // Use PC version's trend analysis approach instead of complex regression
            // Collect all unique min_split and max_depth values
            mcu::b_vector<uint8_t> unique_min_splits;
            mcu::b_vector<uint16_t> unique_max_depths;
            
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
            
            Serial.printf("   Found %d unique min_splits, %d unique max_depths\n", 
                        unique_min_splits.size(), unique_max_depths.size());
            
            // Sort vectors for easier processing
            // Simple bubble sort for small vectors
            for (size_t i = 0; i < unique_min_splits.size(); i++) {
                for (size_t j = i + 1; j < unique_min_splits.size(); j++) {
                    if (unique_min_splits[i] > unique_min_splits[j]) {
                        uint8_t temp = unique_min_splits[i];
                        unique_min_splits[i] = unique_min_splits[j];
                        unique_min_splits[j] = temp;
                    }
                }
            }
            
            for (size_t i = 0; i < unique_max_depths.size(); i++) {
                for (size_t j = i + 1; j < unique_max_depths.size(); j++) {
                    if (unique_max_depths[i] > unique_max_depths[j]) {
                        uint16_t temp = unique_max_depths[i];
                        unique_max_depths[i] = unique_max_depths[j];
                        unique_max_depths[j] = temp;
                    }
                }
            }
            
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
            
            // PC version returns (100.0f - mape) which gives 0-100, then multiplies by 100 in training code
            // Let's follow the same logic but fix the overflow issue
            float get_accuracy_result = fmax(0.0f, 100.0f - mape);
            accuracy = static_cast<uint8_t>(fmin(255.0f, get_accuracy_result * 100.0f / 100.0f)); // Simplify to match intent
            
            // Actually, let's just match the logical intent rather than the bug
            accuracy = static_cast<uint8_t>(fmin(100.0f, fmax(0.0f, 100.0f - mape)));
            
            peak_percent = 30; // A reasonable default for binary tree structures
            
            is_trained = true;
            
            Serial.printf("✅ Retraining complete! Accuracy: %d%%, Peak: %d%%\n", accuracy, peak_percent);
            Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                        coefficients[0], coefficients[1], coefficients[2]);
            Serial.printf("   MAE: %.2f, MAPE: %.2f%%\n", mae, mape);
            Serial.printf("   Split effect: %.2f, Depth effect: %.2f\n", split_effect, depth_effect);

            if(save_after_retrain) savePredictor(); // Save the new predictor
            return true;
        }
        /// @brief  add new samples to the beginning of the node_predictor_log file. 
        // If the file has more than 50 samples (rows, not including header), 
        // remove the samples at the end of the file (limit the file to the 50 latest samples)
        void add_new_samples(b_vector<node_data>& new_samples) {
            // Read all existing lines
            mcu::b_vector<String> lines;
            File file = SPIFFS.open(node_predictor_log, FILE_READ);
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
            mcu::b_vector<String> data_lines;
            for (size_t i = 1; i < lines.size(); ++i) {
                data_lines.push_back(lines[i]);
            }
            // Prepend new samples
            for (int i = new_samples.size() - 1; i >= 0; --i) {
                const node_data& nd = new_samples[i];
                String row = String(nd.min_split) + "," + String(nd.max_depth) + "," + String(nd.total_nodes);
                data_lines.insert(0, row);
            }
            // Limit to 50 rows
            while (data_lines.size() > 50) {
                data_lines.pop_back();
            }
            // Write back to file
            SPIFFS.remove(node_predictor_log);
            file = SPIFFS.open(node_predictor_log, FILE_WRITE);
            if (file) {
                file.println(header);
                for (const auto& row : data_lines) {
                    file.println(row);
                }
                file.close();
            }
        }

        // Check if training log is available and size of the file is greater than 0
        bool can_retrain() const {
            if (!SPIFFS.exists(node_predictor_log)) return false;
            File file = SPIFFS.open(node_predictor_log, FILE_READ);
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
            return result;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_node_predictor);
            total += buffer.capacity() * sizeof(node_data);
            total += filename.length() * sizeof(char) + 12;
            total += node_predictor_log.length() * sizeof(char) + 12;
            return total;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    --------------------------------------------- RF_MEMORY_LOGGER ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef struct time_anchor{
        long unsigned anchor_time;
        size_t index;
    };

    class Rf_logger {
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        uint32_t starting_time;
        uint8_t fragmentation;
        uint32_t lowest_ram;
        uint32_t lowest_rom; 
        uint32_t freeDisk;
        float log_time;
        mcu::b_vector<time_anchor> time_anchors;

        String memory_log_file;
        String time_log_file;

        Rf_logger() : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
        }

        Rf_logger(const String& model_name, bool keep_old_file = false) : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
            init(model_name);
        }
        
        void init(const String& model_name, bool keep_old_file = false){
            time_anchors.clear();
            starting_time = millis();
            drop_anchor(); // initial anchor at index 0

            lowest_ram = UINT32_MAX;
            lowest_rom = UINT32_MAX;

            //generate log file name based on model name
            memory_log_file = "/" + model_name + "_memory_log.csv";
            time_log_file = "/" + model_name + "_time_log.csv";

            if(!keep_old_file){
                if(SPIFFS.exists(time_log_file.c_str())){
                    SPIFFS.remove(time_log_file.c_str()); 
                }
                // write header to time log file
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_WRITE);
                if (logFile) {
                    logFile.println("Event,\t\tTime(s)");
                    logFile.close();
                }
            }
            t_log("init tracker", 0, 0, false); // Initial log without printing

            if(!keep_old_file){                
                // clear SPIFFS log file if it exists
                if(SPIFFS.exists(memory_log_file.c_str())){
                    SPIFFS.remove(memory_log_file.c_str()); 
                }
                // write header to log file
                File logFile = SPIFFS.open(memory_log_file.c_str(), FILE_WRITE);
                if (logFile) {
                    logFile.println("Time(s),FreeHeap,Largest_Block,FreeDisk");
                    logFile.close();
                } 
            }
            m_log("init tracker", false, true); // Initial log without printing

        }

        void m_log(const char* msg = "", bool print = true, bool log = true){
            freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            freeDisk = SPIFFS.totalBytes() - SPIFFS.usedBytes();

            if(freeHeap < lowest_ram) lowest_ram = freeHeap;
            if(freeDisk < lowest_rom) lowest_rom = freeDisk;

            largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            fragmentation = 100 - (largestBlock * 100 / freeHeap);
            if(print){       
                if(msg && strlen(msg) > 0){
                    Serial.print("📋 ");
                    Serial.println(msg);
                }
                Serial.print("--> RAM LEFT (heap): ");
                Serial.println(freeHeap);
                Serial.print("Largest Free Block: ");
                Serial.println(largestBlock);
                Serial.printf("Fragmentation: %d", fragmentation);
                Serial.println("%");
            }

            // Log to file with timestamp
            if(log) {        
                log_time = (millis() - starting_time)/1000.0f; 
                File logFile = SPIFFS.open(memory_log_file.c_str(), FILE_APPEND);
                if (logFile) {
                    logFile.printf("%.2f,\t%u,\t%u,\t%u",
                                    log_time, freeHeap, largestBlock, freeDisk);
                    if(msg && strlen(msg) > 0){
                        logFile.printf(",\t%s\n", msg);
                    } else {
                        logFile.println();
                    }
                    logFile.close();
                }
            }
        }

        size_t drop_anchor(){
            time_anchor anchor;
            anchor.anchor_time = millis();
            anchor.index = time_anchors.size();
            time_anchors.push_back(anchor);
            return anchor.index;
        }

        size_t current_anchor() const {
            return time_anchors.size() > 0 ? time_anchors.back().index : 0;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_logger);
            return total;
        }

        void t_log(const char* msg, size_t begin_anchor_index, size_t end_anchor_idex , bool print = true){
            if(time_anchors.size() == 0) return; // no anchors set
            if(begin_anchor_index >= time_anchors.size() || end_anchor_idex >= time_anchors.size()) return; // invalid index
            if(end_anchor_idex <= begin_anchor_index) {
                std::swap(begin_anchor_index, end_anchor_idex);
            }

            long unsigned begin_time = time_anchors[begin_anchor_index].anchor_time;
            long unsigned end_time = time_anchors[end_anchor_idex].anchor_time;
            float elapsed = (end_time - begin_time)/1000.0f; // in seconds
            if(print){
                if(msg && strlen(msg) > 0){
                    Serial.print("⏱️  ");
                    Serial.print(msg);
                    Serial.print(": ");
                } else {
                    Serial.print("⏱️  Elapsed: ");
                }
            }
            // Log to file with timestamp
            if(time_log_file.length() > 0) {        
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_APPEND);
                if (logFile) {
                    if(msg && strlen(msg) > 0){
                        logFile.printf("%s,\t%.2f\n", msg, elapsed);
                    } else {
                        logFile.printf("Elapsed,\t%.2f\n", elapsed);
                    }
                    logFile.close();
                }
            }
            time_anchors[end_anchor_idex].anchor_time = millis(); // reset end anchor to current time
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ----------------------------------------------- RF_CATEGORIZER ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    // Optional compile-time flag to disable label mapping (saves memory)
    #ifndef DISABLE_LABEL_MAPPING
    #define SUPPORT_LABEL_MAPPING 1
    #else
    #define SUPPORT_LABEL_MAPPING 0
    #endif

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

    class Rf_categorizer {
    private:
        uint16_t numFeatures = 0;
        uint8_t groupsPerFeature = 0;
        uint8_t numLabels = 0;
        uint32_t scaleFactor = 50000;
    public:
        String filename = "";
        bool isLoaded = false;
    private:

        // Compact storage arrays
        mcu::vector<FeatureRef> featureRefs;              // One per feature
        mcu::vector<uint16_t> sharedPatterns;             // Concatenated pattern edges
        mcu::vector<uint16_t> allUniqueEdges;             // Concatenated unique edges
        mcu::vector<uint8_t> allDiscreteValues;           // Concatenated discrete values
        
        #if SUPPORT_LABEL_MAPPING
        mcu::b_vector<String, mcu::SMALL, 8> labelMapping; // Optional label reverse mapping
        #endif
        
        // Helper function to split CSV line
        mcu::b_vector<String, mcu::SMALL> split(const String& line, char delimiter = ',') {
            mcu::b_vector<String, mcu::SMALL> result;
            int start = 0;
            int end = line.indexOf(delimiter);
            
            while (end != -1) {
                result.push_back(line.substring(start, end));
                start = end + 1;
                end = line.indexOf(delimiter, start);
            }
            result.push_back(line.substring(start));
            
            return result;
        }

        // Optimized feature categorization
        uint8_t categorizeFeature(uint16_t featureIdx, float value) const {
            if (!isLoaded || featureIdx >= numFeatures) {
                return 0;
            }
            
            const FeatureRef& ref = featureRefs[featureIdx];
            uint32_t scaledValue = static_cast<uint32_t>(value * scaleFactor + 0.5f);
            
            switch (ref.getType()) {
                case FT_DF:
                    // Full discrete range: clamp to 0..groupsPerFeature-1
                    return static_cast<uint8_t>(std::min(static_cast<int>(value), static_cast<int>(groupsPerFeature - 1)));
                    
                case FT_DC: {
                    // Discrete custom values: linear search
                    uint8_t count = ref.getAux();
                    uint8_t offset = ref.getOffset();
                    uint8_t targetValue = static_cast<uint8_t>(value);
                    
                    for (uint8_t i = 0; i < count; ++i) {
                        if (allDiscreteValues[offset + i] == targetValue) {
                            return i;
                        }
                    }
                    return 0; // Default if not found
                }
                
                case FT_CS: {
                    // Continuous shared pattern
                    uint8_t patternId = ref.getAux();
                    uint16_t baseOffset = patternId * (groupsPerFeature - 1);
                    
                    for (uint8_t bin = 0; bin < (groupsPerFeature - 1); ++bin) {
                        if (scaledValue < sharedPatterns[baseOffset + bin]) {
                            return bin;
                        }
                    }
                    return groupsPerFeature - 1; // Last bin
                }
                
                case FT_CU: {
                    // Continuous unique edges
                    uint8_t edgeCount = ref.getAux();
                    uint8_t offset = ref.getOffset();
                    uint16_t baseOffset = offset * (groupsPerFeature - 1);
                    
                    for (uint8_t bin = 0; bin < edgeCount; ++bin) {
                        if (scaledValue < allUniqueEdges[baseOffset + bin]) {
                            return bin;
                        }
                    }
                    return edgeCount; // Last bin
                }
            }
            
            return 0; // Fallback
        }
         
    public:
        Rf_categorizer() = default;
        
        Rf_categorizer(const String& csvFilename) : filename(csvFilename), isLoaded(false) {}

        void init(const String& csvFilename) {
            filename = csvFilename;
            isLoaded = false;
        }
        
        // Load categorizer data from CTG v2 format
        bool loadCategorizer(bool re_use = true) {
            if (!SPIFFS.exists(filename)) {
                Serial.println("❌ CTG2 file not found: " + filename);
                return false;
            }
            
            File file = SPIFFS.open(filename, "r");
            if (!file) {
                Serial.println("❌ Failed to open CTG2 file: " + filename);
                return false;
            }
            
            Serial.println("📂 Loading CTG2 from: " + filename);
            
            try {
                // Read header: CTG2,numFeatures,groupsPerFeature,numLabels,numSharedPatterns,scaleFactor
                if (!file.available()) {
                    Serial.println("❌ Empty CTG2 file");
                    file.close();
                    return false;
                }
                
                String headerLine = file.readStringUntil('\n');
                headerLine.trim();
                auto headerParts = split(headerLine, ',');
                
                if (headerParts.size() != 6 || headerParts[0] != "CTG2") {
                    Serial.println("❌ Invalid CTG2 header format");
                    file.close();
                    return false;
                }
                
                numFeatures = headerParts[1].toInt();
                groupsPerFeature = headerParts[2].toInt();
                numLabels = headerParts[3].toInt();
                uint16_t numSharedPatterns = headerParts[4].toInt();
                scaleFactor = headerParts[5].toInt();
                
                Serial.println("📊 Features: " + String(numFeatures) + ", Groups: " + String(groupsPerFeature) + 
                             ", Labels: " + String(numLabels) + ", Patterns: " + String(numSharedPatterns) + 
                             ", Scale: " + String(scaleFactor));
                
                // Clear existing data
                featureRefs.clear();
                sharedPatterns.clear();
                allUniqueEdges.clear();
                allDiscreteValues.clear();
                #if SUPPORT_LABEL_MAPPING
                labelMapping.clear();
                #endif
                
                // Reserve memory
                featureRefs.reserve(numFeatures);
                sharedPatterns.reserve(numSharedPatterns * (groupsPerFeature - 1));
                
                #if SUPPORT_LABEL_MAPPING
                labelMapping.reserve(numLabels);
                // Initialize label mapping with empty strings
                for (uint8_t i = 0; i < numLabels; i++) {
                    labelMapping.push_back("");
                }
                #endif
                
                // Read label mappings: L,normalizedId,originalLabel
                #if SUPPORT_LABEL_MAPPING
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.startsWith("L,")) {
                        auto parts = split(line, ',');
                        if (parts.size() >= 3) {
                            uint8_t id = parts[1].toInt();
                            String originalLabel = parts[2];
                            if (id < numLabels) {
                                labelMapping[id] = originalLabel;
                            }
                        }
                    } else {
                        // Rewind to read this line again as it's not a label
                        file.seek(file.position() - line.length() - 1);
                        break;
                    }
                }
                #else
                // Skip label lines
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (!line.startsWith("L,")) {
                        file.seek(file.position() - line.length() - 1);
                        break;
                    }
                }
                #endif
                
                // Read shared patterns: P,patternId,edgeCount,e1,e2,...
                for (uint16_t i = 0; i < numSharedPatterns; i++) {
                    if (!file.available()) {
                        Serial.println("❌ Unexpected end of file reading patterns");
                        file.close();
                        return false;
                    }
                    
                    String patternLine = file.readStringUntil('\n');
                    patternLine.trim();
                    auto parts = split(patternLine, ',');
                    
                    if (parts.size() < 3 || parts[0] != "P") {
                        Serial.println("❌ Invalid pattern line format");
                        file.close();
                        return false;
                    }
                    
                    uint16_t patternId = parts[1].toInt();
                    uint16_t edgeCount = parts[2].toInt();
                    
                    if (parts.size() != (3 + edgeCount)) {
                        Serial.println("❌ Pattern edge count mismatch");
                        file.close();
                        return false;
                    }
                    
                    // Store edges in shared pattern array
                    for (uint16_t j = 0; j < edgeCount; j++) {
                        sharedPatterns.push_back(parts[3 + j].toInt());
                    }
                }
                
                // Read feature definitions
                for (uint16_t i = 0; i < numFeatures; i++) {
                    if (!file.available()) {
                        Serial.println("❌ Unexpected end of file reading features");
                        file.close();
                        return false;
                    }
                    
                    String featureLine = file.readStringUntil('\n');
                    featureLine.trim();
                    auto parts = split(featureLine, ',');
                    
                    if (parts.size() < 1) {
                        Serial.println("❌ Invalid feature line");
                        file.close();
                        return false;
                    }
                    
                    if (parts[0] == "DF") {
                        // Discrete full range
                        featureRefs.push_back(FeatureRef(FT_DF, 0, 0));
                    } 
                    else if (parts[0] == "DC") {
                        // Discrete custom values
                        if (parts.size() < 2) {
                            Serial.println("❌ Invalid DC line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t count = parts[1].toInt();
                        if (parts.size() != (2 + count)) {
                            Serial.println("❌ DC value count mismatch");
                            file.close();
                            return false;
                        }
                        
                        uint8_t offset = allDiscreteValues.size();
                        for (uint8_t j = 0; j < count; j++) {
                            allDiscreteValues.push_back(parts[2 + j].toInt());
                        }
                        
                        featureRefs.push_back(FeatureRef(FT_DC, count, offset));
                    }
                    else if (parts[0] == "CS") {
                        // Continuous shared pattern
                        if (parts.size() != 2) {
                            Serial.println("❌ Invalid CS line format");
                            file.close();
                            return false;
                        }
                        
                        uint16_t patternId = parts[1].toInt();
                        featureRefs.push_back(FeatureRef(FT_CS, patternId, 0));
                    }
                    else if (parts[0] == "CU") {
                        // Continuous unique edges
                        if (parts.size() < 2) {
                            Serial.println("❌ Invalid CU line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t edgeCount = parts[1].toInt();
                        if (parts.size() != (2 + edgeCount)) {
                            Serial.println("❌ CU edge count mismatch");
                            file.close();
                            return false;
                        }
                        
                        uint8_t offset = allUniqueEdges.size() / (groupsPerFeature - 1);
                        for (uint8_t j = 0; j < edgeCount; j++) {
                            allUniqueEdges.push_back(parts[2 + j].toInt());
                        }
                        
                        featureRefs.push_back(FeatureRef(FT_CU, edgeCount, offset));
                    }
                    else {
                        Serial.println("❌ Unknown feature type: " + parts[0]);
                        file.close();
                        return false;
                    }
                }
                
                file.close();
                isLoaded = true;
                
                Serial.println("✅ CTG2 loaded successfully!");
                Serial.println("   Memory usage: " + String(memory_usage()) + " bytes");
                
                // Clean up file if not reusing
                if (!re_use) {
                    SPIFFS.remove(filename);
                }
                
                return true;
                
            } catch (...) {
                Serial.println("❌ Error parsing CTG2 file");
                file.close();
                return false;
            }
        }
        
        
        // Release loaded data from memory
        void releaseCategorizer(bool re_use = true) {
            if (!isLoaded) {
                Serial.println("🧹 Categorizer already released");
                return;
            }
            
            // Clear all data structures
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            #if SUPPORT_LABEL_MAPPING
            labelMapping.clear();
            #endif
            
            isLoaded = false;
            Serial.println("🧹 Categorizer data released from memory");
        }
        // Categorize an entire sample
        mcu::packed_vector<2> categorizeSample(const mcu::b_vector<float>& sample) const {
            if(sample.size() != numFeatures) {
                Serial.println("❌ Sample size mismatch. Expected " + String(numFeatures) + 
                             " features, got " + String(sample.size()));
                return mcu::packed_vector<2>();
            }
            mcu::packed_vector<2> result;
            
            if (!isLoaded) {
                Serial.println("❌ Categorizer not loaded");
                return result;
            }
            
            if (sample.size() != numFeatures) {
                Serial.println("❌ Input sample size mismatch. Expected " + String(numFeatures) + 
                             " features, got " + String(sample.size()));
                return result;
            }
            
            result.reserve(numFeatures);
            
            for (uint16_t i = 0; i < numFeatures; ++i) {
                result.push_back(categorizeFeature(i, sample[i]));
            }
            
            return result;
        }
        
        // Debug methods
        void printInfo() const {
            Serial.println("=== Rf_categorizer Categorizer Info ===");
            Serial.println("File: " + filename);
            Serial.println("Loaded: " + String(isLoaded ? "Yes" : "No"));
            Serial.println("Features: " + String(numFeatures));
            Serial.println("Groups per feature: " + String(groupsPerFeature));
            Serial.println("Labels: " + String(numLabels));
            Serial.println("Scale factor: " + String(scaleFactor));
            Serial.println("Memory usage: " + String(memory_usage()) + " bytes");
            
            #if SUPPORT_LABEL_MAPPING
            if (isLoaded && labelMapping.size() > 0) {
                Serial.println("Label mappings:");
                for (uint8_t i = 0; i < labelMapping.size(); i++) {
                    if (labelMapping[i].length() > 0) {
                        Serial.printf("  %d -> %s\n", i, labelMapping[i].c_str());
                    } else {
                        Serial.printf("  %d: (empty)\n", i);
                    }
                }
            }
            #endif
            
            Serial.println("=================================");
        }
        
        size_t memory_usage() const {
            size_t usage = 0;
            
            // Basic members
            usage += sizeof(numFeatures) + sizeof(groupsPerFeature) + sizeof(numLabels) + 
                    sizeof(scaleFactor) + sizeof(isLoaded);
            usage += filename.length();
            
            // Core data structures
            usage += featureRefs.size() * sizeof(FeatureRef);
            usage += sharedPatterns.size() * sizeof(uint16_t);
            usage += allUniqueEdges.size() * sizeof(uint16_t);
            usage += allDiscreteValues.size() * sizeof(uint8_t);
            
            #if SUPPORT_LABEL_MAPPING
            // Label mappings
            for (const auto& label : labelMapping) {
                usage += label.length() + sizeof(String);
            }
            #endif
            
            return usage;
        }

        String inline getOriginalLabel(uint8_t normalizedLabel) const {
            if (normalizedLabel < labelMapping.size()) {
                return labelMapping[normalizedLabel];
            }
            return String(normalizedLabel);
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

        Rf_random(uint64_t seed, bool use_provided_seed) {
            init(seed, use_provided_seed);
        }

        void init(uint64_t seed, bool use_provided_seed) {
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
    ------------------------------------------------ CONFUSION MATRIX ------------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------------------
    */

    class Rf_matrix_score{
    public:
        // Confusion matrix components
        b_vector<uint16_t, SMALL, 4> tp;
        b_vector<uint16_t, SMALL, 4> fp;
        b_vector<uint16_t, SMALL, 4> fn;

        uint16_t total_predict = 0;
        uint16_t correct_predict = 0;
        uint8_t num_labels;
        uint8_t training_flag;

        // Constructor
        Rf_matrix_score(uint8_t num_labels, uint8_t training_flag) 
            : num_labels(num_labels), training_flag(training_flag) {
            // Ensure vectors have logical length == num_labels and are zeroed
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (uint8_t i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }
        
        void init(uint8_t num_labels, uint8_t training_flag) {
            this->num_labels = num_labels;
            this->training_flag = training_flag;
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (uint8_t i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }

        // Reset all counters
        void reset() {
            total_predict = 0;
            correct_predict = 0;
            // Reset existing buffers safely; ensure length matches num_labels
            if (tp.size() != num_labels) {
                tp.clear(); tp.reserve(num_labels); for (uint8_t i = 0; i < num_labels; ++i) tp.push_back(0);
            } else { tp.fill(0); }
            if (fp.size() != num_labels) {
                fp.clear(); fp.reserve(num_labels); for (uint8_t i = 0; i < num_labels; ++i) fp.push_back(0);
            } else { fp.fill(0); }
            if (fn.size() != num_labels) {
                fn.clear(); fn.reserve(num_labels); for (uint8_t i = 0; i < num_labels; ++i) fn.push_back(0);
            } else { fn.fill(0); }
        }

        // Update confusion matrix with a prediction
        void update_prediction(uint8_t actual_label, uint8_t predicted_label) {
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
        b_vector<pair<uint8_t, float>> get_precisions() {
            b_vector<pair<uint8_t, float>> precisions;
            precisions.reserve(num_labels);
            for(uint8_t label = 0; label < num_labels; label++) {
                float prec = (tp[label] + fp[label] == 0) ? 0.0f : 
                            static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                precisions.push_back(make_pair(label, prec));
            }
            return precisions;
        }

        // Get recall for all labels
        b_vector<pair<uint8_t, float>> get_recalls() {
            b_vector<pair<uint8_t, float>> recalls;
            recalls.reserve(num_labels);
            for(uint8_t label = 0; label < num_labels; label++) {
                float rec = (tp[label] + fn[label] == 0) ? 0.0f : 
                        static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                recalls.push_back(make_pair(label, rec));
            }
            return recalls;
        }

        // Get F1 scores for all labels
        b_vector<pair<uint8_t, float>> get_f1_scores() {
            b_vector<pair<uint8_t, float>> f1s;
            f1s.reserve(num_labels);
            for(uint8_t label = 0; label < num_labels; label++) {
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
        b_vector<pair<uint8_t, float>> get_accuracies() {
            b_vector<pair<uint8_t, float>> accuracies;
            accuracies.reserve(num_labels);
            float overall_accuracy = (total_predict == 0) ? 0.0f : 
                                    static_cast<float>(correct_predict) / total_predict;
            for(uint8_t label = 0; label < num_labels; label++) {
                accuracies.push_back(make_pair(label, overall_accuracy));
            }
            return accuracies;
        }

        // Calculate combined score based on training flags
        float calculate_score(const char* score_type = "Combined") {
            if(total_predict == 0) {
                Serial.printf("❌ No valid %s predictions found!\n", score_type);
                return 0.0f;
            }

            float combined_result = 0.0f;
            uint8_t numFlags = 0;

            // Calculate accuracy
            if(training_flag & 0x01) { // ACCURACY flag
                float accuracy = static_cast<float>(correct_predict) / total_predict;
                Serial.printf("%s Accuracy: %.3f (%d/%d)\n", score_type, accuracy, correct_predict, total_predict);
                combined_result += accuracy;
                numFlags++;
            }

            // Calculate precision
            if(training_flag & 0x02) { // PRECISION flag
                float total_precision = 0.0f;
                uint8_t valid_labels = 0;
                
                for(uint8_t label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0) {
                        total_precision += static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        valid_labels++;
                    }
                }
                
                float precision = valid_labels > 0 ? total_precision / valid_labels : 0.0f;
                Serial.printf("%s Precision: %.3f\n", score_type, precision);
                combined_result += precision;
                numFlags++;
            }

            // Calculate recall
            if(training_flag & 0x04) { // RECALL flag
                float total_recall = 0.0f;
                uint8_t valid_labels = 0;
                
                for(uint8_t label = 0; label < num_labels; label++) {
                    if(tp[label] + fn[label] > 0) {
                        total_recall += static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        valid_labels++;
                    }
                }
                
                float recall = valid_labels > 0 ? total_recall / valid_labels : 0.0f;
                Serial.printf("%s Recall: %.3f\n", score_type, recall);
                combined_result += recall;
                numFlags++;
            }

            // Calculate F1-Score
            if(training_flag & 0x08) { // F1_SCORE flag
                float total_f1 = 0.0f;
                uint8_t valid_labels = 0;
                
                for(uint8_t label = 0; label < num_labels; label++) {
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
                Serial.printf("%s F1-Score: %.3f\n", score_type, f1_score);
                combined_result += f1_score;
                numFlags++;
            }

            // Return combined score
            return numFlags > 0 ? combined_result / numFlags : 0.0f;
        }

        size_t memory_usage() const {
            size_t usage = 0;
            usage += sizeof(total_predict) + sizeof(correct_predict) + sizeof(num_labels) + sizeof(training_flag);
            usage += tp.size() * sizeof(uint16_t);
            usage += fp.size() * sizeof(uint16_t);
            usage += fn.size() * sizeof(uint16_t);
            return usage;
        }
    };


} // namespace mcu