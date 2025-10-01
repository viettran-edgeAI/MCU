#include "STL_MCU.h"  
#include "Rf_file_manager.h"
#include "FS.h"
#include "SPIFFS.h"
#include "esp_system.h"


#define GET_CURRENT_TIME_IN_MICROSECONDS() esp_timer_get_time() // current time in microseconds
#define GET_CURRENT_TIME_IN_MILLISECONDS millis()// current time in milliseconds
#define GET_CURRENT_TIME() millis()

#ifdef DEV_STAGE
    #define ENABLE_TEST_DATA 1
#else
    #define ENABLE_TEST_DATA 0
#endif

#ifndef RF_DEBUG_LEVEL
    #define RF_DEBUG_LEVEL 1
#else
    #if RF_DEBUG_LEVEL > 3
        #undef RF_DEBUG_LEVEL
        #define RF_DEBUG_LEVEL 3
    #endif
#endif

/*
 RF_DEBUG_LEVEL :
    0 : silent mode - no messages
    1 : forest messages (start, end, major events) 
    2 : messages at components level + warnings
    3 : all memory and event timing messages & detailed info
 note: all errors messages (lead to failed process) will be enabled with RF_DEBUG_LEVEL >=1
*/

// general debug macro
#define RF_DEBUG(level, msg, object) \
    do { \
        if constexpr (RF_DEBUG_LEVEL > level) { \
            Serial.printf("%s ", msg); \
            Serial.println(object);
    } while (0);

// file read/write operation error
#define RF_OP_ERR(operation, index, filename) \
    do { \
        if constexpr (RF_DEBUG_LEVEL > 0) \
        Serial.printf("❌%s failed at index %d : %s\n", operation, index, filename); \
    } while (0);

// mismatch error
#define RF_MISMATCH_DEBUG(level, expected, found, component) \
    do { \
        if constexpr (RF_DEBUG_LEVEL > level)  \
        Serial.printf("❌ %s mismatch: expected %d, found %d\n", component, expected, found); \
    } while (0);

static constexpr uint8_t  CHAR_BUFFER            = 32;     // buffer for filename (32 is max filename length in SPIFFS)
static constexpr uint8_t  MAX_TREES              = 100;         // maximum number of trees in a forest
static constexpr uint16_t MAX_LABELS             = 255;         // maximum number of unique labels supported 
static constexpr uint16_t MAX_NUM_FEATURES       = 1023;       // maximum number of features
static constexpr uint16_t MAX_NUM_SAMPLES        = 65535;     // maximum number of samples in a dataset
static constexpr uint16_t MAX_NODES              = 2047;     // Maximum nodes per tree 
static constexpr size_t   MAX_DATASET_SIZE       = 150000;  // Max dataset file size - 150kB
constexpr static size_t   MAX_INFER_LOGFILE_SIZE = 2048;   // Max log file size in bytes (1000 inferences)


/*
 NOTE : Forest file components (with each model)
    1. model_name_nml.bin       : base data (dataset)
    2. model_name_config.json   : model configuration file 
    3. model_name_ctg.csv       : categorizer (feature quantizer and label mapping)
    4. model_name_dp.csv        : information about dataset (num_features, num_labels...)
    5. model_name_forest.bin    : model file (all trees) in unified format
    6. model_name_tree_*.bin    : model files (tree files) in individual format. (Given from pc/used during training)
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
    class Rf_node_predictor;    // Helper class: predict and pre-allocate resources needed before building/loading a tree
    class Rf_categorizer;       // sample quantizer (categorize features and labels mapping)
    class Rf_logger;            // logging forest events timing, memory usage, messages, errors
    class Rf_random;            // random generator (for stability across platforms and runs)
    class Rf_matrix_score;      // confusion matrix and metrics calculator
    class Rf_tree_container;    // manages all trees at forest level
    class Rf_pending_data;      // manage pending data waiting for true labels from feedback action

    // enum Rf_metric_scores;      // flags for training process/score calculation (accuracy, precision, recall, f1_score)
    // enum Rf_training_score;      // score types for training process (oob, validation, k-fold)
    // ...

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_DATA ------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */
    using sampleID_set = ID_vector<uint16_t>;       // set of unique sample IDs

    struct Rf_sample{
        packed_vector<2> features;           // set containing the values ​​of the features corresponding to that sample , 2 bit per value.
        uint8_t label;                     // label of the sample 

        Rf_sample() : features(), label(0) {}
        Rf_sample(uint8_t label, const packed_vector<2, LARGE>& source, size_t start, size_t end){
            this->label = label;
            features = packed_vector<2>(source, start, end);
        }
        Rf_sample(const packed_vector<2> features, uint8_t label) : features(features), label(label) {}
    };

    class Rf_data {
    private:
        static constexpr size_t MAX_CHUNKS_SIZE = 8192; // max bytes per chunk (8kB)

        // Chunked packed storage - eliminates both heap overhead per sample and large contiguous allocations
        vector<packed_vector<2, LARGE>> sampleChunks;  // Multiple chunks of packed features
        b_vector<uint8_t> allLabels;              // Labels storage (still contiguous for simplicity)
        uint16_t bitsPerSample;                        // Number of bits per sample (numFeatures * 2)
        uint16_t samplesEachChunk;                     // Maximum samples per chunk
        size_t size_;  
        char filename[CHAR_BUFFER];          // dataset filename (in SPIFFS)

    public:
        bool isLoaded;      

        Rf_data() : isLoaded(false), size_(0), bitsPerSample(0), samplesEachChunk(0) {}
        // Constructor with filename and numFeatures
        Rf_data(const char* fname, uint16_t numFeatures) {
            init(fname, numFeatures);
        }
        Rf_data(const char* fname){
            init(String(fname));
        }

        // standard init 
        bool init(const char* filename, uint16_t numFeatures) {
            strncpy(this->filename, filename, CHAR_BUFFER);
            this->filename[CHAR_BUFFER - 1] = '\0';
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("ℹ️ Rf_data initialized: %s with %d features (%d bits/sample, %d samples/chunk)\n", 
                       filename, numFeatures, bitsPerSample, samplesEachChunk);
            isLoaded = false;
            size_ = 0;
            sampleChunks.clear();
            allLabels.clear();
            return isProperlyInitialized();
        }

        // for temp base_data 
        bool init(const char* fname) {
            strncpy(this->filename, fname, CHAR_BUFFER);
            filename[CHAR_BUFFER - 1] = '\0';
            isLoaded = false;
            sampleChunks.clear();
            allLabels.clear();
            
            // read header to get size_ and bitsPerSample
            File file = SPIFFS.open(filename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open dataset file", filename);
                if(SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                }
                size_ = 0;
                bitsPerSample = 0;
                samplesEachChunk = 0;
                return false;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read dataset header", filename);
                file.close();
                return false;
            }
            size_ = numSamples;
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            file.close();
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("ℹ️ Rf_data initialized: %s with %d features (%d bits/sample, %d samples/chunk, %d samples total)\n", 
                          filename, numFeatures, bitsPerSample, samplesEachChunk, size_);
            return isProperlyInitialized();
        }

        // for temporary Rf_data (without saving to SPIFFS)
        bool init(uint16_t numFeatures) {   
            strncpy(this->filename, "temp_data", CHAR_BUFFER);
            filename[CHAR_BUFFER - 1] = '\0';
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("ℹ️ Rf_data initialized with %d features (%d bits/sample, %d samples/chunk)\n", 
                        numFeatures, bitsPerSample, samplesEachChunk);
            isLoaded = false;
            size_ = 0;
            sampleChunks.clear();
            allLabels.clear();
            return true;
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
        pair<size_t, size_t> getChunkLocation(size_t sampleIndex) const {
            size_t chunkIndex = sampleIndex / samplesEachChunk;
            size_t localIndex = sampleIndex % samplesEachChunk;
            return make_pair(chunkIndex, localIndex);
        }

        // Ensure we have enough chunks to store the given number of samples
        void ensureChunkCapacity(size_t totalSamples) {
            size_t requiredChunks = (totalSamples + samplesEachChunk - 1) / samplesEachChunk;
            while (sampleChunks.size() < requiredChunks) {
                packed_vector<2, LARGE> newChunk;
                // Reserve space for elements (each feature is 1 element in packed_vector<2>)
                size_t elementsPerSample = bitsPerSample / 2;  // numFeatures
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
                if constexpr(RF_DEBUG_LEVEL > 2)
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
        bool storeSample(const Rf_sample& sample, size_t sampleIndex) {
            if (!isProperlyInitialized()) {
                RF_DEBUG(2, "❌ Rf_data not properly initialized. Use constructor with numFeatures or loadData from another Rf_data.");
                return false;
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
            return true;
        }

    private:
        // Load data from CSV format (used only once for initial dataset conversion)
        bool loadCSVData(String csvFilename, uint8_t numFeatures) {
            if(isLoaded) {
                // clear existing data
                sampleChunks.clear();
                allLabels.clear();
                size_ = 0;
                isLoaded = false;
            }
            
            File file = SPIFFS.open(csvfilename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open CSV file for reading", csvfilename);
                return false;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    RF_DEBUG(0, "❌ CSV file is empty or missing header", csvfilename);
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
                bitsPerSample = numFeatures * 2;
                updateSamplesEachChunk();
            } else {
                // Validate that the provided numFeatures matches the initialized bitsPerSample
                uint16_t expectedFeatures = bitsPerSample / 2;
                if (numFeatures != expectedFeatures) {
                    RF_MISMATCH_DEBUG(0, expectedFeatures, numFeatures, "Feature count");
                    file.close();
                    return false;
                }
            }
            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("📊 Loading CSV: %s (expecting %d features per sample)\n", csvfilename, numFeatures);
                Serial.printf("📦 Chunk configuration: %d samples per chunk (%d bytes max)\n", samplesEachChunk, MAX_CHUNKS_SIZE);
            }
            
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
                    RF_MISMATCH_DEBUG(2, numFeatures + 1, fieldIdx, "Field count");
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    RF_MISMATCH_DEBUG(2, numFeatures, s.features.size(), "Feature count");
                    invalidSamples++;
                    continue;
                }
                
                s.features.fit();

                // Store in chunked packed format
                storeSample(s, validSamples);
                validSamples++;
                
                if (validSamples >= MAX_NUM_SAMPLES) {
                    RF_DEBUG(1, "⚠️ Reached maximum sample limit");
                    break;
                }
            }
            size_ = validSamples;
            
            if constexpr(RF_DEBUG_LEVEL > 1){
                Serial.printf("📋 CSV Processing Results:\n");
                Serial.printf("   Lines processed: %d\n", linesProcessed);
                Serial.printf("   Empty lines: %d\n", emptyLines);
                Serial.printf("   Valid samples: %d\n", validSamples);
                Serial.printf("   Invalid samples: %d\n", invalidSamples);
                Serial.printf("   Total samples in memory: %d\n", size_);
                Serial.printf("   Chunks used: %d\n", sampleChunks.size());
            }
            
            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            file.close();
            isLoaded = true;
            SPIFFS.remove(csvFilename);
            RF_DEBUG(1, "✅ CSV data loaded and file removed", csvfilename);
            return true;
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

        void setFilename(const String& fname) {
            filename = fname;
        }

        String getFilename() const {
            return filename;
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
                RF_DEBUG(1, "❌ Cannot reserve space: Rf_data not properly initialized", filename);
                return;
            }

            // Reserve space for labels
            allLabels.reserve(numSamples);

            // Ensure we have enough chunks for the requested number of samples
            ensureChunkCapacity(numSamples);
            if constexpr(RF_DEBUG_LEVEL > 2)
            Serial.printf("📦 Reserved space for %d samples (%d chunks)\n", 
                        numSamples, sampleChunks.size());
        }

        bool convertCSVtoBinary(String csvFilename, uint8_t numFeatures = 0) {
            if constexpr (RF_DEBUG_LEVEL > 1)
                Serial.println("🔄 Converting CSV to binary format...");
            if(!loadCSVData(csvFilename, numFeatures)) return false;
            if(!releaseData(false)) return false; // Save to binary and clear memory
            RF_DEBUG(1, "✅ CSV converted to binary and saved", filename);
            return true;
        }

        /**
         * @brief Save data to SPIFFS in binary format and clear from RAM.
         * @param reuse If true, keeps data in RAM after saving; if false, clears data from RAM.
         * @note: after first time rf_data created, it must be releaseData(false) to save data
         */
        bool releaseData(bool reuse = true) {
            if(!isLoaded) return false;
            
            if(!reuse){
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("💾 Saving data to SPIFFS and clearing from RAM...");
                // Remove any existing file
                if (SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                }

                File file = SPIFFS.open(filename, FILE_WRITE);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to open binary file for writing", filename);
                    return false;
                }
                if constexpr(RF_DEBUG_LEVEL > 2) 
                    Serial.printf("📂 Saving data to: %s\n", filename);

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

            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("✅ Data saved: %s (%d samples, %d features, %d bytes)\n", 
                            filename, size_, bitsPerSample / 2, memory_usage());
            return true;
        }

        // Load data using sequential indices 
        bool loadData(bool re_use = true) {
            if(isLoaded || filename.length() < 1) return false;
            RF_DEBUG(1, "📂 Loading data from", filename);
            
            File file = SPIFFS.open(filename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data file", filename);
                if(SPIFFS.exists(filename)) {
                    SPIFFS.remove(filename);
                }
                return false;
            }
   
            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read data header", filename);
                file.close();
                return false;
            }

            if(numFeatures * 2 != bitsPerSample) {
                RF_MISMATCH_DEBUG(0, bitsPerSample / 2, numFeatures, "Feature count");
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
                RF_DEBUG(2, "⚠️ Failed to allocate IO buffer, falling back to scalar load");
                ioBuf = nullptr;
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
                            RF_DEBUG(0, "❌ Read batch failed at sample", processed, filename);
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
                        RF_OP_ERR("Read label for sample", processed, filename);
                        if (ioBuf) free(ioBuf);
                        file.close();
                        return;
                    }
                    allLabels.push_back(lbl);
                    uint8_t packed[packedFeatureBytes];
                    if (file.read(packed, packedFeatureBytes) != packedFeatureBytes) {
                        RF_OP_ERR("Read features for sample", processed, filename);
                        if (ioBuf) free(ioBuf);
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
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("💾 Single-load mode: removing SPIFFS file after loading.");
                SPIFFS.remove(filename); // Remove file after loading in single mode
            }
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("✅ Data loaded %s (using %d chunks)\n", filename, sampleChunks.size());
        }

        /**
         * @brief Load specific samples from another Rf_data source by sample IDs.
         * @param source The source Rf_data to load samples from.
         * @param sample_IDs A sorted set of sample IDs to load from the source.
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: The state of the source data will be automatically restored, no need to reload.
         */
        void loadData(Rf_data& source, const sampleID_set& sample_IDs, bool save_ram = true) {
            if (source.getFilename().length() == 0 || !SPIFFS.exists(source.filename)) {
                RF_DEBUG(0, "❌ Source file does not exist", source.filename);
                return;
            }

            File file = SPIFFS.open(source.filename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open source file", source.filename);
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
                RF_DEBUG(0, "❌ Failed to read source header", source.filename);
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
            
            if constexpr(RF_DEBUG_LEVEL > 2)
            Serial.printf("📦 Loading %d samples from SPIFFS: %s\n", numRequestedSamples, source.filename);
            
            size_t addedSamples = 0;
            // Since sample_IDs are sorted in ascending order, we can read efficiently
            for(uint16_t sampleIdx : sample_IDs) {
                if(sampleIdx >= numSamples) {
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️ Sample ID %d exceeds file sample count %d\n", sampleIdx, numSamples);
                    continue;
                }
                
                // Calculate file position for this sample
                size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);
                size_t sampleFilePos = headerSize + (sampleIdx * sampleDataSize);
                
                // Seek to the sample position
                if (!file.seek(sampleFilePos)) {
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️ Failed to seek to sample %d position %d\n", sampleIdx, sampleFilePos);
                    continue;
                }
                
                Rf_sample s;
                
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️ Failed to read label for sample %d\n", sampleIdx);
                    continue;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️ Failed to read packed features for sample %d\n", sampleIdx);
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
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("♻️ Restoring source Rf_data state after loading.");
                source.loadData(); // reload source if it was pre-loaded
            }
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("✅ Loaded %d samples from SPIFFS file: %s (using %d chunks)\n", 
                        addedSamples, source.filename, sampleChunks.size());
        }
        
        /**
         * @brief Load a specific chunk of samples from another Rf_data source.
         * @param source The source Rf_data to load samples from.
         * @param chunkIndex The index of the chunk to load (0-based).
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: this function will call loadData(source, chunkIDs) internally.
         */
        void loadChunk(Rf_data& source, size_t chunkIndex, bool save_ram = true) {
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("📂 Loading chunk %d from source Rf_data: %s\n", 
                chunkIndex, source.filename);
            if(chunkIndex >= source.total_chunks()) {
                if constexpr(RF_DEBUG_LEVEL > 2)
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
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Invalid chunk range: start %d, end %d\n", startSample, endSample);
                return;
            }
            sampleID_set chunkIDs(startSample, endSample - 1);
            chunkIDs.fill();
            loadData(source, chunkIDs, save_ram);   
        }

        /**
         *@brief: copy assignment (but not copy filename to avoid SPIFFS over-writing)
         *@note : Rf_data will be put into release state. loadData() to reload into RAM if needed.
        */
        Rf_data& operator=(const Rf_data& other) {
            purgeData(); // Clear existing data safely
            if (this != &other) {
                if (other.filename.length() > 0 && SPIFFS.exists(other.filename)) {
                    File testFile = SPIFFS.open(other.filename, FILE_READ);
                    if (testFile) {
                        uint32_t testNumSamples;
                        uint16_t testNumFeatures;
                        bool headerValid = (testFile.read((uint8_t*)&testNumSamples, sizeof(testNumSamples)) == sizeof(testNumSamples) &&
                                          testFile.read((uint8_t*)&testNumFeatures, sizeof(testNumFeatures)) == sizeof(testNumFeatures) &&
                                          testNumSamples > 0 && testNumFeatures > 0);
                        testFile.close();
                        
                        if (headerValid) {
                            if (!cloneFile(other.filename, filename)) {
                                RF_DEBUG(0, "❌ Failed to clone source file", other.filename);
                            }
                        } else {
                            RF_DEBUG(0, "❌ Source file has invalid header", other.filename);
                        }
                    } else {
                        RF_DEBUG(0, "❌ Cannot open source file", other.filename);
                    }
                } else {
                    RF_DEBUG(0, "❌ Source file does not exist", other.filename);
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

        // Clear data at both memory and SPIFFS
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
            if (filename.length() > 0 && SPIFFS.exists(filename)) {
                SPIFFS.remove(filename);
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("🗑️ Deleted file %s\n", filename);
            }
        }

        /**
         * @brief Add new data directly to file without loading into RAM
         * @param samples Vector of new samples to add
         * @param extend If false, keeps file size same (overwrites old data from start); 
         *               if true, appends new data while respecting size limits
         * @return : deleted labels
         * @note Directly writes to SPIFFS file to save RAM. File must exist and be properly initialized.
         */
        b_vector<uint8_t> addNewData(const b_vector<Rf_sample>& samples, bool extend = true) {
            b_vector<uint8_t> deletedLabels;
            if (filename.length() == 0) {
                RF_DEBUG(0, "⚠️ No filename specified for adding new data");
                return deletedLabels;
            }

            if (!SPIFFS.exists(filename)) {
                RF_DEBUG(0, "⚠️ File does not exist for adding new data", filename);
                return deletedLabels;
            }

            if (samples.size() == 0) {
                RF_DEBUG(1, "⚠️ No samples to add");
                return deletedLabels;
            }

            // Read current file header to get existing info
            File file = SPIFFS.open(filename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open file for adding new data", filename);
                return deletedLabels;
            }

            uint32_t currentNumSamples;
            uint16_t numFeatures;
            
            if (file.read((uint8_t*)&currentNumSamples, sizeof(currentNumSamples)) != sizeof(currentNumSamples) ||
                file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read file header", filename);
                file.close();
                return deletedLabels;
            }
            file.close();

            // Validate feature count compatibility
            if (samples.size() > 0 && samples[0].features.size() != numFeatures) {
                RF_MISMATCH_DEBUG(2, numFeatures, samples[0].features.size(), "Feature count");
                return deletedLabels;
            }

            // Calculate packed bytes needed for features (4 values per byte)
            uint16_t packedFeatureBytes = (numFeatures + 3) / 4;
            size_t sampleDataSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);

            uint32_t newNumSamples;
            size_t writePosition;
            
            if (extend) {
                // Append mode: add to existing samples
                newNumSamples = currentNumSamples + samples.size();
                
                // Check limits
                if (newNumSamples > MAX_NUM_SAMPLES) {
                    size_t maxAddable = MAX_NUM_SAMPLES - currentNumSamples;
                    if constexpr(RF_DEBUG_LEVEL > 2)
                        Serial.printf("⚠️ Limiting samples to %d (max %d, current %d)\n", 
                                    maxAddable, MAX_NUM_SAMPLES, currentNumSamples);
                    newNumSamples = MAX_NUM_SAMPLES;
                }
                
                size_t newFileSize = headerSize + (newNumSamples * sampleDataSize);
                if (newFileSize > MAX_DATASET_SIZE) {
                    size_t maxSamplesBySize = (MAX_DATASET_SIZE - headerSize) / sampleDataSize;
                    if constexpr(RF_DEBUG_LEVEL > 2)
                        Serial.printf("⚠️ Limiting samples by file size to %d (max file size %d bytes)\n", 
                                    maxSamplesBySize, MAX_DATASET_SIZE);
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

            if constexpr(RF_DEBUG_LEVEL > 1) {
                Serial.printf("📝 Adding %d samples to %s (extend=%s)\n", 
                            samplesToWrite, filename, extend ? "true" : "false");
                Serial.printf("📊 Dataset info: current=%d, new_total=%d, samples_to_write=%d\n", 
                            currentNumSamples, newNumSamples, samplesToWrite);
            }

            // Open file for writing (r+ mode to update existing file)
            file = SPIFFS.open(filename, "r+");
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open file for writing", filename);
                return deletedLabels;
            }

            // In overwrite mode, read the labels that will be overwritten
            if (!extend && samplesToWrite > 0) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("📋 Reading %d labels that will be overwritten...\n", samplesToWrite);
                
                // Seek to the start of data section to read existing labels
                if (!file.seek(headerSize)) {
                    RF_DEBUG(0, "Seek to data section for reading labels", filename);
                    file.close();
                    return deletedLabels;
                }
                
                // Reserve space for deleted labels
                deletedLabels.reserve(samplesToWrite);
                
                // Read labels that will be overwritten
                for (uint32_t i = 0; i < samplesToWrite; ++i) {
                    uint8_t existingLabel;
                    if (file.read(&existingLabel, sizeof(existingLabel)) != sizeof(existingLabel)) {
                        RF_OP_ERR("Read existing label", i, filename);
                        break;
                    }
                    deletedLabels.push_back(existingLabel);
                    
                    // Skip the packed features to get to next label
                    if (!file.seek(file.position() + packedFeatureBytes)) {
                        RF_OP_ERR("Seek past features for sample", i, filename);
                        break;
                    }
                }
                
                if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("📋 Collected %d labels that will be overwritten\n", deletedLabels.size());
            }

            // Update header with new sample count
            file.seek(0);
            file.write((uint8_t*)&newNumSamples, sizeof(newNumSamples));
            file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

            // Seek to write position
            if (!file.seek(writePosition)) {
                RF_OP_ERR("Seek to write position", writePosition, filename);
                file.close();
                return deletedLabels;
            }

            // Write samples directly to file
            uint32_t written = 0;
            for (uint32_t i = 0; i < samplesToWrite && i < samples.size(); ++i) {
                const Rf_sample& sample = samples[i];
                
                // Validate sample feature count
                if (sample.features.size() != numFeatures) {
                    RF_MISMATCH_DEBUG(2, numFeatures, sample.features.size(), "Feature count");
                    continue;
                }

                // Write label
                if (file.write(&sample.label, sizeof(sample.label)) != sizeof(sample.label)) {
                    RF_OP_ERR("Write label for sample", i, filename);
                    break;
                }

                // Pack and write features
                uint8_t packedBuffer[packedFeatureBytes];
                // Initialize buffer to 0
                for (uint16_t j = 0; j < packedFeatureBytes; j++) {
                    packedBuffer[j] = 0;
                }
                
                // Pack 4 feature values into each byte
                for (size_t j = 0; j < sample.features.size(); ++j) {
                    uint16_t byteIndex = j / 4;
                    uint8_t bitOffset = (j % 4) * 2;
                    uint8_t feature_value = sample.features[j] & 0x03;
                    packedBuffer[byteIndex] |= (feature_value << bitOffset);
                }
                
                if (file.write(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    RF_OP_ERR("Write features for sample", i, filename);
                    break;
                }
                
                written++;
            }

            file.close();

            // Update internal size if data is loaded in memory
            if (isLoaded) {
                size_ = newNumSamples;
                if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.println("ℹ️ Updated internal size. Consider reloading data for consistency.");
            }

            if constexpr(RF_DEBUG_LEVEL > 1) {
                Serial.printf("✅ Successfully wrote %d samples to %s (total samples now: %d)\n", 
                            written, filename, newNumSamples);
                if (!extend && deletedLabels.size() > 0) {
                    Serial.printf("📊 Overwrote %d samples with labels: [", deletedLabels.size());
                    for (size_t i = 0; i < deletedLabels.size(); ++i) {
                        Serial.printf("%d", deletedLabels[i]);
                        if (i < deletedLabels.size() - 1) Serial.print(",");
                    }
                    Serial.println("]");
                }
            }
            
            return deletedLabels;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_data);
            total += allLabels.capacity() * sizeof(uint8_t);
            for (const auto& chunk : sampleChunks) {
                total += sizeof(packed_vector<2, LARGE>);
                total += chunk.capacity() * sizeof(uint8_t); // each element is 2 bits, but stored in bytes
            }
            return total;
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
        vector<Tree_node> nodes;  // Vector-based tree storage
        uint8_t index;
        bool isLoaded;

        Rf_tree() : index(255), isLoaded(false) {}
        
        Rf_tree(uint8_t idx) : index(idx), isLoaded(false) {}

        // Copy constructor
        Rf_tree(const Rf_tree& other) 
            : nodes(other.nodes), index(other.index), isLoaded(other.isLoaded) {}

        // Copy assignment operator
        Rf_tree& operator=(const Rf_tree& other) {
            if (this != &other) {
                nodes = other.nodes;
                index = other.index;
                isLoaded = other.isLoaded;
            }
            return *this;
        }

        // Move constructor
        Rf_tree(Rf_tree&& other) noexcept 
            : nodes(std::move(other.nodes)), index(other.index), isLoaded(other.isLoaded) {
            other.index = 255;
            other.isLoaded = false;
        }

        // Move assignment operator
        Rf_tree& operator=(Rf_tree&& other) noexcept {
            if (this != &other) {
                nodes = std::move(other.nodes);
                index = other.index;
                isLoaded = other.isLoaded;
                other.index = 255;
                other.isLoaded = false;
            }
            return *this;
        }

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
        bool releaseTree(String model_name,  bool re_use = false) {
            if(!re_use){
                if (index == 255 || nodes.empty()) {
                    RF_DEBUG(0, "❌ No valid index specified or tree is empty for saving", model_name.c_str());
                    return false;
                }

                char filename[32];  // filename = "/"+ model_name + "tree_" + index + ".bin"
                snprintf(filename, sizeof(filename), "/%s_tree_%d.bin", model_name.c_str(), index);
                
                // Skip exists/remove check - just overwrite directly for performance
                File file = SPIFFS.open(filename, FILE_WRITE);
                if (!file) {
                    RF_DEBUG(0, "❌ Failed to open tree file for writing", filename);
                    return false;
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
                            RF_DEBUG(1, "⚠️ Incomplete tree write to SPIFFS");
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
            nodes.clear(); // Clear nodes to free memory
            nodes.fit(); // Release excess memory
            isLoaded = false; // Mark as unloaded
            RF_DEBUG(2, "✅ Tree saved to SPIFFS: ", index);
            return true;
        }

        // Load tree from SPIFFS into RAM for ESP32
        bool loadTree(String model_name = "", bool re_use = false) {
            if (isLoaded) return false;
            
            if (index == 255) {
                RF_DEBUG(0, "❌ No valid index specified for tree loading", model_name.c_str());
                return false;
            }
            
            char path_to_use[32];
            snprintf(path_to_use, sizeof(path_to_use), "/%s_tree_%d.bin", model_name.c_str(), index);
            
            File file = SPIFFS.open(path_to_use, FILE_READ);
            if (!file) {
                RF_DEBUG(2, "❌ Failed to open tree file", path_to_use);
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x54524545) {
                RF_DEBUG(0, "❌ Invalid tree file format", path_to_use);
                file.close();
                return false;
            }
            
            // Read number of nodes
            uint32_t nodeCount;
            if (file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                RF_DEBUG(0, "❌ Failed to read node count", path_to_use);
                file.close();
                return false;
            }
            
            if (nodeCount == 0 || nodeCount > 2047) { // 11-bit limit for child indices
                RF_DEBUG(1, "❌ Invalid node count in tree file");
                file.close();
                return false;
            }
            
            // Clear existing nodes and reserve space
            nodes.clear();
            nodes.reserve(nodeCount);
            
            // Load all nodes
            for (uint32_t i = 0; i < nodeCount; i++) {
                Tree_node node;
                if (file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                    RF_DEBUG(0, "❌ Faile to read node data");
                    nodes.clear();
                    file.close();
                    return false;
                }
                nodes.push_back(node);
            }
            
            file.close();
            
            // Update state
            isLoaded = true;
            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("✅ Tree loaded: %s (%d nodes, %d bytes)\n", 
                        path_to_use, nodeCount, memory_usage());
            }
            if (!re_use) {
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("🗑️ Removing tree file after load: %s\n", path_to_use);    
                SPIFFS.remove(path_to_use); // Remove file after loading in single mode
            }
            return true;
        }

        // predict single (normalized)sample - packed features
        uint8_t predict_features(const packed_vector<2>& packed_features) const {
            if (nodes.empty() || !isLoaded) return 0;
            
            uint16_t currentIndex = 0;  // Start from root
            
            while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
                uint16_t featureID = nodes[currentIndex].getFeatureID();
                // Bounds check for feature access
                if (featureID >= packed_features.size()) {
                    RF_DEBUG(2, "❌ Feature ID out of bounds during prediction");
                    return 0; // Invalid feature access
                }
                
                uint8_t featureValue = packed_features[featureID];
                
                if (featureValue <= nodes[currentIndex].getThreshold()) {
                    // Go to left child
                    currentIndex = nodes[currentIndex].getLeftChildIndex();
                } else {
                    // Go to right child
                    currentIndex = nodes[currentIndex].getRightChildIndex();
                }
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
                    if constexpr(RF_DEBUG_LEVEL > 2) Serial.printf("✅ Tree file removed: %s\n", filename);
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
        BASE_DATA_IS_CSV        = 1 << 9
    } Rf_base_flags;

    // Base file management class for Random Forest project status
    class Rf_base {
    private:
        uint16_t flags = 0; // flags indicating the status of member files
        String model_name = "";
    public:
        Rf_base() : flags(static_cast<Rf_base_flags>(0)), model_name("") {}
        Rf_base(const char* bn) : flags(static_cast<Rf_base_flags>(0)), model_name(bn) {
            init(bn);
        }

        void init(const char* model_name){
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("🔧 Initializing model resource manager");
            if (!model_name || strlen(model_name) == 0) {
                RF_DEBUG(0, "❌ Model name is empty. The process is aborted.");
                return;
            } else {
                this->model_name = String(model_name);
                
                // check : base data exists (binary or csv)
                String base_data_file = get_base_data_file();
                if (!SPIFFS.exists(base_data_file.c_str())) {
                    // try to find csv file
                    String csv_file = "/" + this->model_name + "_nml.csv";
                    if (SPIFFS.exists(csv_file.c_str())) {
                        RF_DEBUG(1, "🔄 Found csv dataset, need to be converted to binary format before use.");
                        flags |= static_cast<Rf_base_flags>(BASE_DATA_IS_CSV);
                    }else{
                        RF_DEBUG(0, "❌ No base data file found", base_data_file);
                        this->model_name = "";
                    }
                }
                RF_DEBUG(1, "✅ Found base data file: ", base_data_file);
                flags |= static_cast<Rf_base_flags>(BASE_DATA_EXIST);

                // check : categorizer file exists
                String ctg_file = get_ctg_file();
                if (SPIFFS.exists(ctg_file.c_str())) {
                    RF_DEBUG(1, "✅ Found categorizer file: ", ctg_file);
                    flags |= static_cast<Rf_base_flags>(CTG_FILE_EXIST);
                } else {
                    RF_DEBUG(0, "❌ No categorizer file found", ctg_file.c_str());
                    this->model_name = "";
                }
                
                // check : dp file exists
                String dp_file = get_dp_file();
                if (SPIFFS.exists(dp_file.c_str())) {
                    RF_DEBUG(1, "✅ Found data_params file: ", dp_file);
                    flags |= static_cast<Rf_base_flags>(DP_FILE_EXIST);
                } else {
                    RF_DEBUG(1, "⚠️ No data_params file found", dp_file.c_str());
                    RF_DEBUG(1, "🔂 Dataset will be scanned, which may take time...🕒");
                }

                // check : config file exists
                String config_file = get_config_file();
                if (SPIFFS.exists(config_file.c_str())) {
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("✅ Found config file: %s\n", config_file.c_str());
                    flags |= static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
                } else {
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("⚠️ Warning: no config file found: %s\n", config_file.c_str());
                    Serial.println("🔂 Switching to manual configuration");
                }
                
                // check : forest file exists (unified form)
                String uni_forest = get_forest_file();
                if (SPIFFS.exists(uni_forest.c_str())) {
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("✅ Found unified forest file: %s\n", uni_forest.c_str());
                    flags |= static_cast<Rf_base_flags>(UNIFIED_FOREST_EXIST);
                } else {
                    RF_DEBUG(2, "⚠️ No unified forest model file found");
                }

                // check : node predictor file exists
                String node_pred_file = get_node_predict_file();
                if (SPIFFS.exists(node_pred_file.c_str())) {
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("✅ Found node predictor file: %s\n", node_pred_file.c_str());
                    flags |= static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
                } else {
                    if constexpr(RF_DEBUG_LEVEL > 2){
                        Serial.printf("⚠️ No node predictor file found: %s\n", node_pred_file.c_str());
                        Serial.println("🔂 Switching to use default node_predictor");
                    }
                }

                // able to inference : forest file + categorizer
                if ((flags & UNIFIED_FOREST_EXIST) && (flags & CTG_FILE_EXIST)) {
                    flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.println("✅ Model is ready for inference.");
                } else {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("⚠️ Model is NOT ready for inference.");
                }

                // able to re-training : base data + categorizer 
                if ((flags & BASE_DATA_EXIST) && (flags & CTG_FILE_EXIST)) {
                    flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.println("✅ Model is ready for re-training.");
                } else {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("⚠️ Model is NOT ready for re-training.");
                }
            }
        }

        //  operator =
        Rf_base& operator=(const Rf_base& other) {
            if (this != &other) {
                this->flags = other.flags;
                this->model_name = other.model_name;
            }
            return *this;
        }

        // copy constructor
        Rf_base(const Rf_base& other) {
            this->flags = other.flags;
            this->model_name = other.model_name;
        }

        // base loaded check
        inline bool ready_to_use() const {
            return model_name.length() > 0;
        }

        // Get mode name 
        inline String get_model_name() const {
            return model_name;
        }

        // for Rf_data: baseData 
        inline String get_base_data_file() const {
            return "/" + model_name + "_nml.bin";
        }

        // for Rf_config 
        inline String get_dp_file() const {
            return "/" + model_name + "_dp.csv";
        }

        // for Rf_categorizer
        inline String get_ctg_file() const {
            return "/" + model_name + "_ctg.csv";
        }

        // for Rf_base 
        inline String get_infer_log_file() const {
            return "/" + model_name + "_infer_log.bin";
        }

        // for Rf_config
        inline String get_config_file() const {
            return "/" + model_name + "_config.json";
        }

        // for Rf_node_predictor
        inline String get_node_predict_file() const {
            return "/" + model_name + "_node_pred.bin";
        }

        // for Rf_node_predictor log
        inline String get_node_log_file() const {
            return "/" + model_name + "_node_log.csv";
        }

        // for unified model format (all trees in one file)
        inline String get_forest_file() const {
            return "/" + model_name + "_forest.bin";
        }

        // for logger
        inline String get_time_log_file() const {
            return "/" + model_name + "_time_log.csv";
        }
        // for logger
        inline String get_memory_log_file() const {
            return "/" + model_name + "_memory_log.csv";
        }

        bool dp_file_exists() const {
            return (flags & DP_FILE_EXIST) != 0;
        }

        bool config_file_exists() const {
            return (flags & CONFIG_FILE_EXIST) != 0;
        }

        bool node_pred_file_exists() const {
            return (flags & NODE_PRED_FILE_EXIST) != 0;
        }

        // Check if base file is in CSV format (always false now as we only use binary)
        bool base_data_is_csv() const {
            return false; // Always binary format now
        }

        // Check if model uses unified format (model_name_forest.bin)
        inline bool forest_file_exist() const {
            return (flags & UNIFIED_FOREST_EXIST) != 0;
        }

        // Fast check for able to training
        inline bool able_to_training() const {
            return (flags & ABLE_TO_TRAINING) != 0;
        }

        // fast check for able to inference
        inline bool able_to_inference() const {
            return (flags & ABLE_TO_INFERENCE) != 0;
        }

        // set name of model 
        void set_model_name(const char* bn) {
            String old_model_name = model_name;
            if (bn && strlen(bn) > 0) {
                model_name = String(bn);
                // find and rename all existing related files

                String old_file, new_file;

                // base file
                old_file = "/" + old_model_name + "_nml.bin";
                new_file = "/" + model_name + "_nml.bin";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // data_params file
                old_file = "/" + old_model_name + "_dp.csv";
                new_file = "/" + model_name + "_dp.csv";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // categorizer file
                old_file = "/" + old_model_name + "_ctg.csv";
                new_file = "/" + model_name + "_ctg.csv";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // inference log file
                old_file = "/" + old_model_name + "_infer_log.bin";
                new_file = "/" + model_name + "_infer_log.bin";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // node predictor file
                old_file = "/" + old_model_name + "_node_pred.bin";
                new_file = "/" + model_name + "_node_pred.bin";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());
                
                // node predict log file
                old_file = "/" + old_model_name + "_node_log.bin";
                new_file = "/" + model_name + "_node_log.bin";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // config file
                old_file = "/" + old_model_name + "_config.json";
                new_file = "/" + model_name + "_config.json";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // tree files - handle both individual and unified formats
                old_file = "/" + old_model_name + "_forest.bin";
                new_file = "/" + model_name + "_forest.bin";
                
                if (SPIFFS.exists(old_file.c_str())) {
                    // Handle unified model format
                    cloneFile(old_file, new_file);
                    SPIFFS.remove(old_file.c_str());
                } else {
                    // Handle individual tree files
                    for(uint8_t i = 0; i < 50; i++) { // Max 50 trees check
                        old_file = "/" + old_model_name + "_tree_" + String(i) + ".bin";
                        new_file = "/" + model_name + "_tree_" + String(i) + ".bin";
                        if (SPIFFS.exists(old_file.c_str())) {
                            cloneFile(old_file, new_file);
                            SPIFFS.remove(old_file.c_str());
                        }else{
                            break; // Stop when we find a missing tree file
                        }
                    }
                }

                // log files - optional, not critical
                // model_name_memory_log.csv 
                old_file = "/" + old_model_name + "_memory_log.csv";
                new_file = "/" + model_name + "_memory_log.csv";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // model_name_time_log.csv
                old_file = "/" + old_model_name + "_time_log.csv";
                new_file = "/" + model_name + "_time_log.csv";
                cloneFile(old_file, new_file);
                SPIFFS.remove(old_file.c_str());

                // Re-initialize flags based on new base name
                init(model_name.c_str());  
            }
        }

        bool set_config_status(bool exists) {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(CONFIG_FILE_EXIST);
            }
            return config_file_exists();
        }

        bool set_dp_status(bool exists) {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(DP_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(DP_FILE_EXIST);
            }
            return dp_file_exists();
        }

        bool set_node_pred_status(bool exists) {
            if (exists) {
                flags |= static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            } else {
                flags &= ~static_cast<Rf_base_flags>(NODE_PRED_FILE_EXIST);
            }
            return (flags & NODE_PRED_FILE_EXIST) != 0;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Rf_base);
            total += model_name.length() * sizeof(char);
            return total + 2;
        }
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------------- RF_CONFIG ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef enum Rf_metric_scores : uint8_t{
        ACCURACY    = 0x00,          // calculate accuracy of the model
        PRECISION   = 0x01,          // calculate precision of the model
        RECALL      = 0x02,            // calculate recall of the model
        F1_SCORE    = 0x04          // calculate F1 score of the model
    }Rf_metric_scores;


    typedef enum Rf_training_score : uint8_t {
        OOB_SCORE = 0x00,   // default 
        VALID_SCORE = 0x01,
        K_FOLD_SCORE = 0x02
    } Rf_training_score;

    // Configuration class for Random Forest parameters
    // handle 2 files: model_name_config.json (config file) and model_name_dp.csv (dp file)
    class Rf_config {
        Rf_base* base_ptr = nullptr;
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }
        bool isLoaded;
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
        uint8_t metric_score;
        float result_score;
        uint32_t estimatedRAM;

        pair<uint8_t, uint8_t> min_split_range;
        pair<uint8_t, uint8_t> max_depth_range; 

        bool extend_base_data;
        bool enable_retrain;

        bool enable_auto_config;   // change config based on dataset parameters (when base_data expands)

        // Dataset parameters (set after loading data)
        uint16_t num_samples;
        uint16_t num_features;
        uint8_t num_labels; 

        b_vector<uint16_t> samples_per_label; // index = label, value = count

        void init(Rf_base* base) {
            base_ptr = base;
            isLoaded = false;
            // Set default values
            num_trees = 20;
            random_seed = 37;
            min_split = 2;
            max_depth = 13;
            use_boostrap = true;
            boostrap_ratio = 0.632f; 
            use_gini = true;
            k_fold = 4;
            unity_threshold = 0.125;
            impurity_threshold = 0.1;
            train_ratio = 0.7;
            test_ratio = 0.15;
            valid_ratio = 0.15;
            training_score = OOB_SCORE;
            metric_score = 0x01; // ACCURACY
            result_score = 0.0;
            estimatedRAM = 0;
            
            // Set defaults for new properties (not in initial config file)
            extend_base_data = true;
            enable_retrain = true;
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
            String base_filename = base_ptr->get_base_data_file();
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("📊 Scanning base data: %s\n", base_filename);

            File file = SPIFFS.open(base_filename, FILE_READ);
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open base data file for scanning", base_filename);
                return false;
            }

            // Read binary header
            uint32_t numSamples;
            uint16_t numFeatures;
            
            if(file.read((uint8_t*)&numSamples, sizeof(numSamples)) != sizeof(numSamples) ||
               file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                RF_DEBUG(0, "❌ Failed to read dataset header during scan", base_filename);
                file.close();
                return false;
            }

            // Set basic parameters
            num_samples = numSamples;
            num_features = numFeatures;

            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("📋 Header scan: %d samples, %d features\n", num_samples, num_features);

            // Calculate packed feature bytes per sample
            const uint16_t packedFeatureBytes = (numFeatures + 3) / 4; // 4 values per byte (2 bits each)
            const size_t recordSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features

            // Track unique labels and their counts
            unordered_map<uint8_t, uint16_t> label_counts;
            uint8_t max_label = 0;

            // Scan through all samples to collect label statistics
            for(uint32_t i = 0; i < numSamples; i++) {
                uint8_t label;
                if(file.read(&label, sizeof(label)) != sizeof(label)) {
                    RF_OP_ERR("Read label", i, base_filename);
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
                    RF_OP_ERR("Skip features", i, base_filename);
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

            if constexpr(RF_DEBUG_LEVEL > 2) {
                Serial.printf("✅ Base data scan complete:\n");
                Serial.printf("   📊 Samples: %d\n", num_samples);
                Serial.printf("   🔢 Features: %d\n", num_features);
                Serial.printf("   🏷️  Labels: %d (max: %d)\n", num_labels, max_label);
                Serial.printf("   📈 Samples per label: ");
                for(size_t i = 0; i < samples_per_label.size(); i++) {
                    if(samples_per_label[i] > 0) {
                        Serial.printf("L%d:%d ", i, samples_per_label[i]);
                    }
                }
                Serial.println();
            }
            return true;
        }
        
        // setup config manually (when no config file)
        void setup_auto_config(){
            // set metric_score based on dataset balance
            if(samples_per_label.size() > 0){
                uint16_t minorityCount = num_samples;
                uint16_t majorityCount = 0;

                for (auto& count : samples_per_label) {
                    if (count > majorityCount) {
                        majorityCount = count;
                    }
                    if (count < minorityCount) {
                        minorityCount = count;
                    }
                }

                float maxImbalanceRatio = 0.0f;
                if (minorityCount > 0) {
                    maxImbalanceRatio = (float)majorityCount / minorityCount;
                }

                if (maxImbalanceRatio > 10.0f) {
                    metric_score = Rf_metric_scores::RECALL;
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("⚠️ Highly imbalanced dataset (ratio: %.2f). Setting metric_score to RECALL.\n", maxImbalanceRatio);
                } else if (maxImbalanceRatio > 3.0f) {
                    metric_score = Rf_metric_scores::F1_SCORE;
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("⚠️ Moderately imbalanced dataset (ratio: %.2f). Setting metric_score to F1_SCORE.\n", maxImbalanceRatio);
                } else if (maxImbalanceRatio > 1.5f) {
                    metric_score = Rf_metric_scores::PRECISION;
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("⚠️ Slightly imbalanced dataset (ratio: %.2f). Setting metric_score to PRECISION.\n", maxImbalanceRatio);
                } else {
                    metric_score = Rf_metric_scores::ACCURACY;
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.printf("✅ Balanced dataset (ratio: %.2f). Setting metric_score to ACCURACY.\n", maxImbalanceRatio);
                }
            }

            uint16_t avg_samples_per_label = num_samples / max(1, static_cast<int>(num_labels));
        
            // set training_score method
            if (avg_samples_per_label < 200){
                training_score = K_FOLD_SCORE;
            }else if (avg_samples_per_label < 500){
                training_score = OOB_SCORE;
            }else{
                training_score = VALID_SCORE;
            }

            // set train/test/valid ratio
            if (avg_samples_per_label < 150){
                train_ratio = 0.6f;
                test_ratio = 0.2f;
                valid_ratio = 0.2f;
            }else{
                train_ratio = 0.7f;
                test_ratio = 0.15f;
                valid_ratio = 0.15f;
            }
            if (training_score != VALID_SCORE){
                train_ratio += valid_ratio;
                valid_ratio = 0.0f;
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
                if constexpr(RF_DEBUG_LEVEL > 1){
                    Serial.println("⚙️ Not found minSplit/maxDepth in config, setting to optimal values.");
                    Serial.printf("Setting minSplit to %u and maxDepth to %u based on dataset size.\n", 
                                min_split, max_depth);
                }
            }

            if constexpr(RF_DEBUG_LEVEL > 1){
                Serial.printf("⚙️ Setting minSplit range: %d to %d (current: %d)\n", 
                            min_minSplit, max_minSplit, min_split);
                Serial.printf("⚙️ Setting maxDepth range: %d to %d (current: %d)\n", 
                            min_maxDepth, max_maxDepth, max_depth);
            }

            min_split_range = make_pair(min_minSplit, max_minSplit);
            max_depth_range = make_pair(min_maxDepth, max_maxDepth);
        }

        
        // read dataset parameters from /dataset_dp.csv and write to config
        bool loadDpFile() {
            String path = base_ptr->get_dp_file();
            // Read dataset parameters from /dataset_params.csv
            File file = SPIFFS.open(path.c_str(), "r");
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data_params file for reading", path.c_str());
                return false;
            }

            // Skip header line
            file.readStringUntil('\n');

            // Initialize variables with defaults
            uint16_t numSamples = 0;
            uint16_t numFeatures = 0;
            uint8_t numLabels = 0;
            unordered_map<uint8_t, uint16_t> labelCounts; // label -> count
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
            
            // Initialize samples_per_label vector with the parsed label counts
            samples_per_label.clear();
            samples_per_label.resize(numLabels, 0);
            for (uint8_t i = 0; i < numLabels; i++) {
                if (labelCounts.find(i) != labelCounts.end()) {
                    samples_per_label[i] = labelCounts[i];
                }
            }

            // Dataset summary output
            if constexpr(RF_DEBUG_LEVEL > 1){
                Serial.printf("📊 Dataset Summary (from params file):\n");
                Serial.printf("  Total samples: %u\n", numSamples);
                Serial.printf("  Total features: %u\n", numFeatures);
                Serial.printf("  Unique labels: %u\n", numLabels);

                Serial.println("  Label distribution:");
                float lowest_distribution = 100.0f;
                for (uint8_t i = 0; i < numLabels; i++) {
                    if (samples_per_label[i] > 0) {
                        float percent = (float)samples_per_label[i] / numSamples * 100.0f;
                        Serial.printf("    Label %u: %u samples (%.1f%%)\n", i, samples_per_label[i], percent);
                        if (percent < lowest_distribution) {
                            lowest_distribution = percent;
                        }
                    }
                }
            }
            // this->lowest_distribution = lowest_distribution / 100.0f; // Store as fraction
            return true;
        }
        // write back to dataset_params file
        void releaseDpFile() {
            String path = base_ptr->get_dp_file();
            if (path.length() < 1) return;
            File file = SPIFFS.open(path.c_str(), "w");
            if (!file) {
                RF_DEBUG(0, "❌ Failed to open data_params file for writing", path.c_str());
                return;
            }
            file.println("parameter,value");
            file.println("quantization_coefficient,2");  // Fixed for 2-bit quantization
            file.println("max_feature_value,3");         // Fixed for 2-bit values (0-3)
            file.println("features_per_byte,4");          // Fixed for 2-bit packing (4 features per byte)
            
            file.printf("num_features,%u\n", num_features);
            file.printf("num_samples,%u\n", num_samples);
            file.printf("num_labels,%u\n", num_labels);
        
            // Write actual label counts from samples_per_label vector
            for (uint8_t i = 0; i < samples_per_label.size(); i++) {
                file.printf("samples_label_%u,%u\n", i, samples_per_label[i]);
            }
            
            file.close();   
            
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("✅ Dataset parameters saved successfully.");
        }
    public:
        // Load configuration from JSON file in SPIFFS
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
                    base_ptr->set_dp_status(true);
                    dp_ok = true;
                }
            }
            if(!dp_ok){
                RF_DEBUG(1, "❌ Cannot load dataset parameters for configuration");
                return false;
            }
            // load config session
            if(base_ptr->config_file_exists()){
                String filename = base_ptr->get_config_file();
                File file = SPIFFS.open(filename, FILE_READ);
                if (!file) {
                    if constexpr(RF_DEBUG_LEVEL > 2){
                        Serial.printf("⚠️ Failed to open config file: %s\n", filename);
                        Serial.println("Switching to default configuration.");
                    }
                    return false;
                }

                String jsonString = file.readString();
                file.close();

                // Parse JSON manually (simple parsing for known structure)
                parseJSONConfig(jsonString);
                if constexpr(RF_DEBUG_LEVEL > 1) {
                    Serial.printf("✅ Config loaded: %s\n", filename);
                    Serial.printf("   Trees: %d, max_depth: %d, min_split: %d\n", num_trees, max_depth, min_split);
                    Serial.printf("   Estimated RAM: %d bytes\n", estimatedRAM);
                    Serial.printf("   extend_base_data: %s, enable_retrain: %s\n", 
                                extend_base_data ? "true" : "false",
                                enable_retrain ? "true" : "false");
                }
            } else return false;
            // Validate configuration after loading
            if (! validateSamplesPerLabel()) RF_DEBUG(1, "⚠️ samples_per_label data inconsistency detected");
            isLoaded = true;
            RF_DEBUG(1, "✅ Configuration loaded successfully");
            return true;
        }
    
        // Save configuration to JSON file in SPIFFS  
        void releaseConfig() {
            if (!isLoaded || !has_base()){
                RF_DEBUG(0, "❌ Config not loaded or base not ready", "save config");
                return;
            }
            String filename = base_ptr->get_config_file();
            String existingTimestamp = "";
            String existingAuthor = "Viettran";
            
            if (SPIFFS.exists(filename)) {
                File readFile = SPIFFS.open(filename, FILE_READ);
                if (readFile) {
                    String jsonContent = readFile.readString();
                    readFile.close();
                    existingTimestamp = extractStringValue(jsonContent, "timestamp");
                    existingAuthor = extractStringValue(jsonContent, "author");
                }
                SPIFFS.remove(filename);
            }

            File file = SPIFFS.open(filename, FILE_WRITE);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Failed to create config file: %s\n", filename);
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
            file.printf("  \"metric_score\": \"%s\",\n", getFlagString(metric_score).c_str());
            file.printf("  \"resultScore\": %.6f,\n", result_score);
            file.printf("  \"Estimated RAM (bytes)\": %d,\n", estimatedRAM);
            
            // Add new properties to config file
            file.printf("  \"extendBaseData\": %s,\n", extend_base_data ? "true" : "false");
            file.printf("  \"enableRetrain\": %s,\n", enable_retrain ? "true" : "false");
            
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
        
            // Clear from RAM  
            purgeConfig();
            releaseDpFile();
            isLoaded = false;
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("✅ Config saved to: %s\n", filename);
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
            metric_score = parseFlagValue(extractStringValue(jsonStr, "metric_score")); // ✅ Fixed
            result_score = extractFloatValue(jsonStr, "resultScore");       // ✅ Fixed
            estimatedRAM = extractIntValue(jsonStr, "Estimated RAM (bytes)"); // ✅ Already correct
            
            // Handle new properties that might not be in initial config files
            // Check if they exist in JSON, if not, keep default values (true)
            String extendBaseDataStr = extractStringValue(jsonStr, "extendBaseData");
            if (extendBaseDataStr.length() > 0) {
                extend_base_data = extractBoolValue(jsonStr, "extendBaseData");
            }
            // If not found in JSON, extend_base_data keeps its default value (true)
            
            String enableRetrainStr = extractStringValue(jsonStr, "enableRetrain");
            if (enableRetrainStr.length() > 0) {
                enable_retrain = extractBoolValue(jsonStr, "enableRetrain");
            }
            // If not found in JSON, enable_retrain keeps its default value (true)
            
            if constexpr(RF_DEBUG_LEVEL > 1) {
                Serial.printf("   extend_base_data: %s, enable_retrain: %s\n", 
                            extend_base_data ? "true" : "false",
                            enable_retrain ? "true" : "false");
            }
        }

        // Convert flag string to uint8_t
        uint8_t parseFlagValue(const String& flagStr) {
            if (flagStr == "ACCURACY") return 0x00;
            if (flagStr == "PRECISION") return 0x01;
            if (flagStr == "RECALL") return 0x02;
            if (flagStr == "F1_SCORE") return 0x04;
            return 0x00; // Default to ACCURACY
        }

        // Convert uint8_t flag to string
        String getFlagString(uint8_t flag) {
            switch(flag) {
                case 0x00: return "ACCURACY";
                case 0x01: return "PRECISION";
                case 0x02: return "RECALL";
                case 0x04: return "F1_SCORE";
                default: return "ACCURACY";
            }
        }

        // Convert string to Rf_training_score enum
        Rf_training_score parseTrainingScore(const String& scoreStr) {
            if (scoreStr == "oob_score") return OOB_SCORE;
            if (scoreStr == "valid_score") return VALID_SCORE;
            if (scoreStr == "k_fold_score") return K_FOLD_SCORE;
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
            total += 4;    // min_split and max_depth ranges (2 pairs of uint8_t)
            total += samples_per_label.size() * sizeof(uint16_t); // samples_per_label vector
            return total;
        }
        
        // Method to validate that samples_per_label data is consistent
        bool validateSamplesPerLabel() const {
            if (samples_per_label.size() != num_labels) {
                return false;
            }
            
            uint32_t totalSamples = 0;
            for (uint16_t count : samples_per_label) {
                totalSamples += count;
            }
            
            return totalSamples == num_samples;
        }
    };
    
    /*
    ------------------------------------------------------------------------------------------------------------------
    ----------------------------------------------- RF_CATEGORIZER ---------------------------------------------------
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

    // Template trait to check supported vector types for Rf_categorizer
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

    class Rf_categorizer {
    private:
        uint16_t numFeatures = 0;
        uint8_t groupsPerFeature = 0;
        uint8_t numLabels = 0;
        uint32_t scaleFactor = 50000;
        bool isLoaded = false;
        Rf_base* base_ptr = nullptr;

        // Compact storage arrays
        vector<FeatureRef> featureRefs;              // One per feature
        vector<uint16_t> sharedPatterns;             // Concatenated pattern edges
        vector<uint16_t> allUniqueEdges;             // Concatenated unique edges
        vector<uint8_t> allDiscreteValues;           // Concatenated discrete values
        b_vector<String, 4> labelMapping; // Optional label reverse mapping
        
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }
        // Helper function to split CSV line
        b_vector<String, 4> split(const String& line, char delimiter = ',') {
            b_vector<String, 4> result;
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
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.println("❌ Categorizer not loaded or invalid feature index");
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
        
        Rf_categorizer(Rf_base* base) {
            init(base);
        }
        ~Rf_categorizer() {
            base_ptr = nullptr;
            isLoaded = false;
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            labelMapping.clear();
        }


        void init(Rf_base* base) {
            base_ptr = base;
            isLoaded = false;
        }
        
        // Load categorizer data from CTG v2 format
        bool loadCategorizer() {
            if (isLoaded) return true;
            if (!has_base()) {
                RF_DEBUG(0, "❌ Base pointer is null or base not ready", "load categorizer");
                return false;
            }
            String filename = base_ptr->get_ctg_file();
            if (!SPIFFS.exists(filename)) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.println("❌ Categorizer file not found: " + filename);
                return false;
            }
            File file = SPIFFS.open(filename, "r");
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.println("❌ Failed to open Categorizer file: " + filename);
                return false;
            }
            
            try {
                // Read header: Categorizer,numFeatures,groupsPerFeature,numLabels,numSharedPatterns,scaleFactor
                if (!file.available()) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("❌ Empty Categorizer file");
                    file.close();
                    return false;
                }
                
                String headerLine = file.readStringUntil('\n');
                headerLine.trim();
                auto headerParts = split(headerLine, ',');
                
                if (headerParts.size() != 6 || headerParts[0] != "CTG2") {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("❌ Invalid Categorizer header format");
                    file.close();
                    return false;
                }
                
                numFeatures = headerParts[1].toInt();
                groupsPerFeature = headerParts[2].toInt();
                numLabels = headerParts[3].toInt();
                uint16_t numSharedPatterns = headerParts[4].toInt();
                scaleFactor = headerParts[5].toInt();
                
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("📊 Features: " + String(numFeatures) + ", Groups: " + String(groupsPerFeature) + 
                             ", Labels: " + String(numLabels) + ", Patterns: " + String(numSharedPatterns) + 
                             ", Scale: " + String(scaleFactor));
                
                // Clear existing data
                featureRefs.clear();
                sharedPatterns.clear();
                allUniqueEdges.clear();
                allDiscreteValues.clear();
                labelMapping.clear();
                
                // Reserve memory
                featureRefs.reserve(numFeatures);
                sharedPatterns.reserve(numSharedPatterns * (groupsPerFeature - 1));
                
                labelMapping.reserve(numLabels);
                // Initialize label mapping with empty strings
                for (uint8_t i = 0; i < numLabels; i++) {
                    labelMapping.push_back("");
                }
                
                // Read label mappings: L,normalizedId,originalLabel
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
                // Skip label lines
                while (file.available()) {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (!line.startsWith("L,")) {
                        file.seek(file.position() - line.length() - 1);
                        break;
                    }
                }
                
                // Read shared patterns: P,patternId,edgeCount,e1,e2,...
                for (uint16_t i = 0; i < numSharedPatterns; i++) {
                    if (!file.available()) {
                        if constexpr(RF_DEBUG_LEVEL > 0)
                        Serial.println("❌ Unexpected end of file reading patterns");
                        file.close();
                        return false;
                    }
                    
                    String patternLine = file.readStringUntil('\n');
                    patternLine.trim();
                    auto parts = split(patternLine, ',');
                    
                    if (parts.size() < 3 || parts[0] != "P") {
                        if constexpr(RF_DEBUG_LEVEL > 0)
                        Serial.println("❌ Invalid pattern line format");
                        file.close();
                        return false;
                    }
                    
                    uint16_t patternId = parts[1].toInt();
                    uint16_t edgeCount = parts[2].toInt();
                    
                    if (parts.size() != (3 + edgeCount)) {
                        RF_MISMATCH_DEBUG(0, parts.size(), 3 + edgeCount, "Pattern edge count");
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
                        if constexpr(RF_DEBUG_LEVEL > 0)
                        Serial.println("❌ Unexpected end of file reading features");
                        file.close();
                        return false;
                    }
                    
                    String featureLine = file.readStringUntil('\n');
                    featureLine.trim();
                    auto parts = split(featureLine, ',');
                    
                    if (parts.size() < 1) {
                        if constexpr(RF_DEBUG_LEVEL > 0) Serial.println("❌ Invalid feature line");
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
                            if constexpr(RF_DEBUG_LEVEL > 0) Serial.println("❌ Invalid DC line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t count = parts[1].toInt();
                        if (parts.size() != (2 + count)) {
                            RF_MISMATCH_DEBUG(0, parts.size(), 2 + count, "DC value count");
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
                            if constexpr(RF_DEBUG_LEVEL > 0) Serial.println("❌ Invalid CS line format");
                            file.close();
                            return false;
                        }
                        
                        uint16_t patternId = parts[1].toInt();
                        featureRefs.push_back(FeatureRef(FT_CS, patternId, 0));
                    }
                    else if (parts[0] == "CU") {
                        // Continuous unique edges
                        if (parts.size() < 2) {
                            if constexpr(RF_DEBUG_LEVEL > 0) Serial.println("❌ Invalid CU line format");
                            file.close();
                            return false;
                        }
                        
                        uint8_t edgeCount = parts[1].toInt();
                        if (parts.size() != (2 + edgeCount)) {
                            RF_MISMATCH_DEBUG(0, parts.size(), 2 + edgeCount, "CU edge count");
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
                        if constexpr(RF_DEBUG_LEVEL > 0)
                        Serial.println("❌ Unknown feature type: " + parts[0]);
                        file.close();
                        return false;
                    }
                }
                
                file.close();
                isLoaded = true;
                
                if constexpr(RF_DEBUG_LEVEL > 1) {
                    Serial.println("✅ Categorizer loaded successfully!");
                    Serial.println("   Memory usage: " + String(memory_usage()) + " bytes");
                }
                
                return true;
                
            } catch (...) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.println("❌ Error parsing Categorizer file");
                file.close();
                return false;
            }
        }
        
        
        // Release loaded data from memory
        void releaseCategorizer(bool re_use = true) {
            if (!isLoaded) {
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("🧹 Categorizer already released");
                return;
            }
            
            // Clear all data structures
            featureRefs.clear();
            sharedPatterns.clear();
            allUniqueEdges.clear();
            allDiscreteValues.clear();
            labelMapping.clear();
            isLoaded = false;
            if constexpr(RF_DEBUG_LEVEL > 2)
            Serial.println("🧹 Categorizer data released from memory");
        }

        // categorize features array
        packed_vector<2> categorizeFeatures(const float* features, size_t featureCount = 0) const {
            if (featureCount == 0) {
                if constexpr (RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️ Feature count not provided, assuming %d\n", numFeatures);
                featureCount = numFeatures;
            }
            packed_vector<2> result;
            result.reserve(numFeatures);
            for (uint16_t i = 0; i < numFeatures; ++i) {
                result.push_back(categorizeFeature(i, features[i]));
            }
            return result;
        }

        // overload for vector input
        template<typename T>
        packed_vector<2> categorizeFeatures(const T& features) const {
            static_assert(is_supported_vector<T>::value, "Unsupported vector type for categorizeFeatures");
            if (features.size() != numFeatures) {
                RF_MISMATCH_DEBUG(2, features.size(), numFeatures, "Feature count");
                return packed_vector<2>();
            }
            return categorizeFeatures(features.data(), features.size());
        }

        // Debug methods
        void printInfo() const {
            Serial.println("=== Rf_categorizer Categorizer Info ===");
            Serial.println("File: " + String(base_ptr ? base_ptr->get_ctg_file().c_str() : "N/A"));
            Serial.println("Loaded: " + String(isLoaded ? "Yes" : "No"));
            Serial.println("Features: " + String(numFeatures));
            Serial.println("Groups per feature: " + String(groupsPerFeature));
            Serial.println("Labels: " + String(numLabels));
            Serial.println("Scale factor: " + String(scaleFactor));
            Serial.println("Memory usage: " + String(memory_usage()) + " bytes");
            
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
            
            Serial.println("=================================");
        }
        
        size_t memory_usage() const {
            size_t usage = 0;
            
            // Basic members
            usage += sizeof(numFeatures) + sizeof(groupsPerFeature) + sizeof(numLabels) + 
                    sizeof(scaleFactor) + sizeof(isLoaded);
            usage += 4;
            
            // Core data structures
            usage += featureRefs.size() * sizeof(FeatureRef);
            usage += sharedPatterns.size() * sizeof(uint16_t);
            usage += allUniqueEdges.size() * sizeof(uint16_t);
            usage += allDiscreteValues.size() * sizeof(uint8_t);
            
            // Label mappings
            for (const auto& label : labelMapping) {
                usage += label.length() + sizeof(String);
            }
            
            return usage;
        }

        String getOriginalLabel(uint8_t normalizedLabel) const {
            if (normalizedLabel < labelMapping.size()) {
                return labelMapping[normalizedLabel];
            }
            // return error if out of bounds
            return "ERROR";
        }
        // mapping from original label to normalized label 
        uint8_t getNormalizedLabel(const String& originalLabel) const {
            if (originalLabel == "ERROR" || originalLabel.length() == 0) return 255;
            if (labelMapping.size() == 0) return 255;
            for (uint8_t i = 0; i < labelMapping.size(); i++) {
                if (labelMapping[i] == originalLabel) {
                    return i;
                }
            }
            // return error if not found
            return 255;
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
        b_vector<node_data, 5> buffer;
    private:
        // String filename;
        // String node_predictor_log;
        Rf_base* base_ptr = nullptr;
        
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
                return 100.0f; // default estimate
            }
            // Simple heuristic: more nodes = better accuracy
            float estimate = 100.0f - data.min_split * 12 + data.max_depth * 3;
            return estimate < 10.0f ? 10.0f : estimate; // ensure reasonable minimum
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
        

    public:
        uint8_t accuracy;      // in percentage
        uint8_t peak_percent;  // number of nodes at depth with maximum number of nodes / total number of nodes in tree
        
        Rf_node_predictor() : is_trained(false), accuracy(0), peak_percent(0) {
            for (int i = 0; i < 3; i++) {
                coefficients[i] = 0.0f;
            }
        }

        Rf_node_predictor(Rf_base* base) : base_ptr(base), is_trained(false), accuracy(0), peak_percent(0) {
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("🔧 Initializing node predictor");
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
            // get logfile path from filename : "/model_name_node_pred.bin" -> "/model_name_node_log.csv"
            String node_predictor_log = base_ptr ? base_ptr->get_node_log_file() : "";
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
            if (!has_base()){
                RF_DEBUG(0, "❌ Base pointer is null, cannot load predictor.", "load node_predictor");
                return false;
            }
            String filename = base_ptr->get_node_predict_file();
            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("🔍 Loading node predictor from file: %s\n", filename);
            if(is_trained) return true;
            if (!SPIFFS.exists(filename)) {
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("❌ No predictor file found: %s !\n", filename);
                Serial.println("Switching to use default predictor.");
                return false;
            }
            
            File file = SPIFFS.open(filename, FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Failed to open predictor file: %s\n", filename);
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Invalid predictor file format: %s\n", filename);
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("❌ Failed to read accuracy");
                file.close();
                return false;
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.println("❌ Failed to read peak_percent");
                file.close();
                return false;
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients) || num_coefficients != 3) {
                RF_MISMATCH_DEBUG(2, num_coefficients, 3, "Coefficient count");
                file.close();
                return false;
            }
            
            // Read coefficients
            if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                if constexpr(RF_DEBUG_LEVEL > 1)
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
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("⚠️  Fixed peak_percent from 0%% to 30%% \n");
                }
                if constexpr(RF_DEBUG_LEVEL > 1){
                Serial.printf("✅ Node_predictor loaded: %s \n", 
                            filename);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
                }
            } else {
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("⚠️  predictor file exists but is not trained: %s\n", filename);
                is_trained = false;
            }
            
            return file_is_trained;
        }
        
        // Save trained predictor to SPIFFS
        bool releasePredictor() {
            if (!has_base()){
                RF_DEBUG(0, "❌ Base pointer is null, cannot save predictor.", "save node_predictor");
                return false;
            }
            if (!is_trained) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.println("❌ Predictor is not trained, cannot save.");
                return false;
            }
            String filename = base_ptr->get_node_predict_file();
            // Remove existing file
            if (SPIFFS.exists(filename)) {
                SPIFFS.remove(filename);
            }

            if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("💾 Saving node predictor to file: %s\n", filename);
            
            File file = SPIFFS.open(filename, FILE_WRITE);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Failed to create node_predictor file: %s\n", filename);
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
            
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.printf("✅ Node_predictor saved: %s \n", filename);
            return true;
        }
        
        // Add new training samples to buffer
        void add_new_samples(uint8_t min_split, uint16_t max_depth, uint16_t total_nodes = 0) {
            if (min_split == 0 || max_depth == 0) return; // invalid sample
            buffer.push_back(node_data(min_split, max_depth, total_nodes));
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("➕ Added training sample: split=%d, depth=%d, nodes=%d (buffer size=%d)\n", 
                            min_split, max_depth, total_nodes, buffer.size());
        }
        // Retrain the predictor using data from rf_tree_log.csv (synchronized with PC version)
        bool re_train(bool save_after_retrain = true) {
            if (!has_base()){
                RF_DEBUG(0, "❌ Base pointer is null, cannot retrain predictor.", "retrain node_predictor");
                return false;
            }
            String node_predictor_log = base_ptr->get_node_log_file();
            RF_DEBUG(2, "🔂 Starting retraining of node predictor...");
            if(!can_retrain()) {
                RF_DEBUG(2, "❌ No training data available for retraining.");
                return false;
            }
            if(buffer.size() > 0){
                add_buffer(buffer);
            }
            buffer.clear();
            buffer.fit();

            File file = SPIFFS.open(node_predictor_log.c_str(), FILE_READ);
            if (!file) {
                RF_DEBUG(1, "❌ Failed to open node_predictor log file.", node_predictor_log.c_str());
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
                RF_DEBUG(1, "❌ Insufficient training data for retraining.", 
                            String(training_data.size()) + " samples (need at least 3)");
                return false;
            }
            
            // Use PC version's trend analysis approach instead of complex regression
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

            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("✅ Node predictor retraining complete! Accuracy: %d%%, Peak: %d%%\n", accuracy, peak_percent);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
                Serial.printf("   MAE: %.2f, MAPE: %.2f%%\n", mae, mape);
                Serial.printf("   Split effect: %.2f, Depth effect: %.2f\n", split_effect, depth_effect);
            }

            if(save_after_retrain) releasePredictor(); // Save the new predictor
            return true;
        }
        
        uint16_t estimate_nodes(uint8_t min_split, uint16_t max_depth) {
            return estimate(min_split, max_depth) * 100 / accuracy;
        }

        uint16_t queue_peak_size(uint8_t min_split, uint16_t max_depth) {
            return min(120, estimate_nodes(min_split, max_depth) * peak_percent / 100);
        }

        /// @brief  add new samples to the beginning of the node_predictor_log file. 
        // If the file has more than 50 samples (rows, not including header), 
        // remove the samples at the end of the file (limit the file to the 50 latest samples)
        void add_buffer(b_vector<node_data,5>& new_samples) {
            if (!has_base()){
                RF_DEBUG(0, "❌ Base pointer is null", "add buffer to node_predictor");
                return;
            }
            String node_predictor_log = base_ptr->get_node_log_file();
            if (new_samples.size() == 0) return;
            // Read all existing lines
            b_vector<String> lines;
            File file = SPIFFS.open(node_predictor_log.c_str(), FILE_READ);
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
            SPIFFS.remove(node_predictor_log.c_str());
            file = SPIFFS.open(node_predictor_log.c_str(), FILE_WRITE);
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
            if (!has_base()) return false;
            String node_predictor_log = base_ptr->get_node_log_file();
            if (!SPIFFS.exists(node_predictor_log.c_str())) return false;
            File file = SPIFFS.open(node_predictor_log.c_str(), FILE_READ);
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
        b_vector<uint16_t, 4> tp;
        b_vector<uint16_t, 4> fp;
        b_vector<uint16_t, 4> fn;

        uint16_t total_predict = 0;
        uint16_t correct_predict = 0;
        uint8_t num_labels;
        uint8_t metric_score;

        public:
        // Constructor
        Rf_matrix_score(uint8_t num_labels, uint8_t metric_score) 
            : num_labels(num_labels), metric_score(metric_score) {
            // Ensure vectors have logical length == num_labels and are zeroed
            tp.clear(); fp.clear(); fn.clear();
            tp.reserve(num_labels); fp.reserve(num_labels); fn.reserve(num_labels);
            for (uint8_t i = 0; i < num_labels; ++i) { tp.push_back(0); fp.push_back(0); fn.push_back(0); }
            total_predict = 0;
            correct_predict = 0;
        }
        
        void init(uint8_t num_labels, uint8_t metric_score) {
            this->num_labels = num_labels;
            this->metric_score = metric_score;
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
        float calculate_score() {
            if(total_predict == 0) {
                RF_DEBUG(1, "❌ No valid predictions found!");
                return 0.0f;
            }

            float combined_result = 0.0f;
            uint8_t numFlags = 0;

            // Calculate accuracy
            if(metric_score & 0x01) { // ACCURACY flag
                float accuracy = static_cast<float>(correct_predict) / total_predict;
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("Accuracy: %.3f (%d/%d)\n", accuracy, correct_predict, total_predict);
                combined_result += accuracy;
                numFlags++;
            }

            // Calculate precision
            if(metric_score & 0x02) { // PRECISION flag
                float total_precision = 0.0f;
                uint8_t valid_labels = 0;
                
                for(uint8_t label = 0; label < num_labels; label++) {
                    if(tp[label] + fp[label] > 0) {
                        total_precision += static_cast<float>(tp[label]) / (tp[label] + fp[label]);
                        valid_labels++;
                    }
                }
                
                float precision = valid_labels > 0 ? total_precision / valid_labels : 0.0f;
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("Precision: %.3f\n", precision);
                combined_result += precision;
                numFlags++;
            }

            // Calculate recall
            if(metric_score & 0x04) { // RECALL flag
                float total_recall = 0.0f;
                uint8_t valid_labels = 0;
                
                for(uint8_t label = 0; label < num_labels; label++) {
                    if(tp[label] + fn[label] > 0) {
                        total_recall += static_cast<float>(tp[label]) / (tp[label] + fn[label]);
                        valid_labels++;
                    }
                }
                
                float recall = valid_labels > 0 ? total_recall / valid_labels : 0.0f;
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("Recall: %.3f\n", recall);
                combined_result += recall;
                numFlags++;
            }

            // Calculate F1-Score
            if(metric_score & 0x08) { // F1_SCORE flag
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
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("F1-Score: %.3f\n",  f1_score);
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

    class Rf_tree_container{
        private:
            // String model_name;
            Rf_base* base_ptr = nullptr;
            Rf_config* config_ptr = nullptr;

            vector<Rf_tree> trees;        // b_vector storing root nodes of trees (now manages SPIFFS filenames)
            size_t   total_depths;       // store total depth of all trees
            size_t   total_nodes;        // store total nodes of all trees
            size_t   total_leaves;       // store total leaves of all trees
            b_vector<NodeToBuild> queue_nodes; // Queue for breadth-first tree building

            unordered_map<uint8_t, uint16_t> predictClass; // Map to count predictions per class during inference

            bool is_unified = true;  // Default to unified form (used at the end of training and inference)

            inline bool has_base() const { 
                return config_ptr!= nullptr && base_ptr != nullptr && base_ptr->ready_to_use(); 
            }
            
        public:
            bool is_loaded = false;

            Rf_tree_container(){};
            Rf_tree_container(Rf_base* base, Rf_config* config){
                init(base, config);
            }

            void init(Rf_base* base, Rf_config* config){
                base_ptr = base;
                config_ptr = config;
                trees.reserve(config_ptr->num_trees);
                predictClass.reserve(config_ptr->num_trees);
                is_loaded = false; // Initially in individual form
            }

            ~Rf_tree_container(){
                // save to SPIFFS in unified form 
                releaseForest();
                trees.clear();
                base_ptr = nullptr;
                config_ptr = nullptr;
            }


            // Clear all trees, old forest file and reset state to individual form (ready for rebuilding)
            void clearForest() {
                if(!has_base()) {
                    RF_DEBUG(0, "❌ Base pointer is null", "clear forest");
                    return;
                }
                
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("🧹 Clearing forest (current size: %d, target: %d)\n", 
                            trees.size(), config_ptr->num_trees);
                
                String model_name = base_ptr->get_model_name();
                // Process trees one by one to avoid heap issues
                for (size_t i = 0; i < trees.size(); i++) {
                    trees[i].purgeTree(model_name);
                    // Force yield to allow garbage collection
                    yield();        
                    delay(10);
                }
                trees.clear();
                trees.fit(); // Ensure vector is completely cleared
                // Reserve space for the expected number of trees but don't pre-size
                trees.reserve(config_ptr->num_trees);
                is_loaded = false;
                // Remove old forest file to ensure clean slate
                String oldForestFile = base_ptr->get_forest_file();
                if(SPIFFS.exists(oldForestFile.c_str())) {
                    SPIFFS.remove(oldForestFile.c_str());
                    RF_DEBUG(2, "🗑️ Removed old forest file", oldForestFile.c_str());
                }
                is_unified = false; // Now in individual form
                total_depths = 0;
                total_nodes = 0;
                total_leaves = 0;
            }
            
            void add_tree(Rf_tree&& tree){
                if(!tree.isLoaded){
                    RF_DEBUG(2, "🟡 Warning: Adding an unloaded tree to the container.");
                }
                if(tree.index != 255 && tree.index < config_ptr->num_trees) {
                    // Resize vector if needed to accommodate this tree's index
                    if(trees.size() <= tree.index) {
                        trees.resize(tree.index + 1);
                    }
                    // Check if slot is already occupied
                    if(trees[tree.index].isLoaded || trees[tree.index].index != 255) {
                        RF_DEBUG(2, "⚠️ Warning: Overwriting tree at index", tree.index);
                        trees[tree.index].purgeTree(base_ptr->get_model_name());
                    }
                    uint16_t d = tree.getTreeDepth();
                    uint16_t n = tree.countNodes();
                    uint16_t l = tree.countLeafNodes();
                    if constexpr (RF_DEBUG_LEVEL > 0)
                    Serial.printf("🌲 tree %d : %d nodes, depth %d\n", tree.index, n, d);
                    total_depths += d;
                    total_nodes  += n;
                    total_leaves += l;

                    tree.releaseTree(base_ptr->get_model_name()); // Release tree nodes from memory after adding to container
                    trees[tree.index] = std::move(tree);
                } else {
                    RF_DEBUG(0, "❌ Invalid tree index:",tree.index);
                }
            }

            // Finalize container after all trees are added - ensure proper sizing
            void finalizeContainer() {
                if(config_ptr && trees.size() != config_ptr->num_trees) {
                    trees.resize(config_ptr->num_trees);
                    RF_DEBUG(2, "🔧 Finalized container size to", config_ptr->num_trees);
                }
            }

            uint8_t predict_features(const packed_vector<2>& features) {
                if(trees.empty() || !is_loaded) {
                    RF_DEBUG(2, "❌ Forest not loaded or empty, cannot predict.");
                    return 255; // Unknown class
                }
                int16_t totalPredict = 0;
                predictClass.clear();
                
                // Use streaming prediction 
                for(auto& tree : trees){
                    uint8_t predict = tree.predict_features(features); 
                    if(predict < config_ptr->num_labels){
                        predictClass[predict]++;
                        totalPredict++;
                    }
                }
                if(totalPredict == 0) {
                    return 255; 
                }

                int16_t max = -1;
                uint8_t mostPredict = 255;
                for(const auto& predict : predictClass){
                    if(predict.second > max){
                        max = predict.second;
                        mostPredict = predict.first;
                    }
                }
                
                // Check certainty threshold
                float certainty = static_cast<float>(max) / totalPredict;
                if(certainty < config_ptr->unity_threshold) {
                    return 255; 
                }
                return mostPredict;
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
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("🔧 Adjusting container size from %d to %d trees\n", 
                                trees.size(), config_ptr->num_trees);
                    trees.resize(config_ptr->num_trees);
                }
                // Memory safety check
                size_t freeMemory = ESP.getFreeHeap();
                if(freeMemory < config_ptr->estimatedRAM + 8000) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Insufficient memory to load forest (need %d bytes, have %d)\n", 
                                config_ptr->estimatedRAM + 8000, freeMemory);
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
                    RF_MISMATCH_DEBUG(1, config_ptr->num_trees, loadedTrees, "trees loaded");
                    is_loaded = false;
                    return false;
                }
                
                is_loaded = true;
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("✅ Forest loaded: %d/%d trees (%d nodes)\n", loadedTrees, trees.size(), total_nodes);
                return true;
            }

            // Load forest from unified format (single file containing all trees)
            bool loadForestUnified() {
                String unifiedFilename = base_ptr->get_forest_file();
                if(unifiedFilename.isEmpty() || !SPIFFS.exists(unifiedfilename)) {
                    RF_DEBUG(0, "❌ Unified forest file not found", unifiedfilename);
                    return false;
                }
                
                // Load from unified file (optimized format)
                File file = SPIFFS.open(unifiedfilename, FILE_READ);
                    RF_DEBUG(0, "❌ Failed to open unified forest file", unifiedfilename);
                    return false;
                }
                
                // Read forest header with error checking
                uint32_t magic;
                if(file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    RF_DEBUG(0, "❌ Failed to read magic number from", unifiedfilename);
                    file.close();
                    return false;
                }
                
                if(magic != 0x464F5253) { // "FORS" 
                    RF_DEBUG(0, "❌ Invalid forest file format (bad magic)", unifiedfilename);
                    file.close();
                    return false;
                }
                
                uint8_t treeCount;
                if(file.read((uint8_t*)&treeCount, sizeof(treeCount)) != sizeof(treeCount)) {
                    RF_DEBUG(0, "❌ Failed to read tree count from", unifiedfilename);
                    file.close();
                    return false;
                }
                if(treeCount != config_ptr->num_trees) {
                    RF_MISMATCH_DEBUG(0, config_ptr->num_trees, treeCount, "trees in unified file");
                    file.close();
                    return false;
                }
                RF_DEBUG(1, "📁 Loading from unified forest file", unifiedfilename);
                
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
                        RF_DEBUG(1, "❌ Failed to read tree index for tree:", treeIndex);
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
                    
                    // Find the corresponding tree in trees vector
                    bool treeFound = false;
                    for(size_t treeIdx = 0; treeIdx < trees.size(); treeIdx++) {
                        auto& tree = trees[treeIdx];
                        if(tree.index == treeIndex) {
                            tree.nodes.clear();
                            tree.nodes.reserve(nodeCount);
                            
                            // Read all nodes for this tree with error checking
                            bool nodeReadSuccess = true;
                            for(uint32_t j = 0; j < nodeCount; j++) {
                                Tree_node node;
                                if(file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                    RF_OP_ERR(1, "❌ Failed to read node in tree", j, String(treeIndex).c_str());
                                    nodeReadSuccess = false;
                                    break;
                                }
                                tree.nodes.push_back(node);
                            }
                            
                            if(nodeReadSuccess) {
                                tree.nodes.fit();
                                tree.isLoaded = true;
                                successfullyLoaded++;
                                
                                // Update metadata vectors with actual values after loading
                                total_depths += tree.getTreeDepth();
                                total_nodes += tree.countNodes();
                                total_leaves += tree.countLeafNodes();
                            } else {
                                // Clean up failed tree
                                tree.nodes.clear();
                                tree.nodes.fit();
                                tree.isLoaded = false;
                            }
                            treeFound = true;
                            break;
                        }
                    }
                    
                    if(!treeFound) {
                        RF_DEBUG(1, "⚠️ Skipping tree not found in forest structure:", tree_index);
                        // Skip the node data for this tree
                        file.seek(file.position() + nodeCount * sizeof(uint32_t));
                    }
                }
                
                file.close();
                return check_valid_after_load();
            }

            // Load forest from individual tree files (used during training)
            bool loadForestIndividual() {
                RF_DEBUG(1, "📁 Loading from individual tree files...");
                
                uint8_t successfullyLoaded = 0;
                for (auto& tree : trees) {
                    if (!tree.isLoaded) {
                        try {
                            tree.loadTree(base_ptr->get_model_name());
                            if(tree.isLoaded) successfullyLoaded++;
                        } catch (...) {
                            // if constexpr(RF_DEBUG_LEVEL > 1)
                            // Serial.printf("❌ Exception loading tree %d\n", tree.index);
                            RF_
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
                    if constexpr (RF_DEBUG_LEVEL > 2) 
                        Serial.println("✅ Forest is not loaded in memory, nothing to release.");
                    return false;
                }
                if(has_base()) {
                    // Forest release is allowed - no training check needed
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
                    if constexpr(RF_DEBUG_LEVEL > 1)
                    Serial.println("❌ No loaded trees to release");
                    is_loaded = false;
                    return false;
                }
                
                // Check available SPIFFS space before writing
                size_t totalFS = SPIFFS.totalBytes();
                size_t usedFS = SPIFFS.usedBytes();
                size_t freeFS = totalFS - usedFS;
                size_t estimatedSize = totalNodes * sizeof(uint32_t) + 100; // nodes + headers
                
                if(freeFS < estimatedSize) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Insufficient SPIFFS space (need ~%d bytes, have %d)\n", estimatedSize, freeFS);
                    return false;
                }
                
                // Single file approach - write all trees to unified forest file
                String unifiedFilename = base_ptr->get_forest_file();
                if(unifiedFilename.isEmpty()) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("❌ Cannot release forest: no base reference for file management");
                    return false;
                }
                
                unsigned long fileStart = GET_CURRENT_TIME_IN_MILLISECONDS;
                File file = SPIFFS.open(unifiedfilename, FILE_WRITE);
                if (!file) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Failed to create unified forest file: %s\n", unifiedfilename);
                    return false;
                }
                
                // Write forest header
                uint32_t magic = 0x464F5253; // "FORS" in hex (forest)
                if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("❌ Failed to write magic number");
                    file.close();
                    SPIFFS.remove(unifiedfilename);
                    return false;
                }
                
                if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.println("❌ Failed to write tree count");
                    file.close();
                    SPIFFS.remove(unifiedfilename);
                    return false;
                }
                
                size_t totalBytes = 0;
                
                // Write all trees in sequence with error checking
                uint8_t savedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && tree.index != 255 && !tree.nodes.empty()) {
                        // Write tree header
                        if(file.write((uint8_t*)&tree.index, sizeof(tree.index)) != sizeof(tree.index)) {
                            if constexpr(RF_DEBUG_LEVEL > 1)
                            Serial.printf("❌ Failed to write tree index %d\n", tree.index);
                            break;
                        }
                        
                        uint32_t nodeCount = tree.nodes.size();
                        if(file.write((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                            if constexpr(RF_DEBUG_LEVEL > 1)
                            Serial.printf("❌ Failed to write node count for tree %d\n", tree.index);
                            break;
                        }
                        
                        // Write all nodes with progress tracking
                        bool writeSuccess = true;
                        for (uint32_t i = 0; i < tree.nodes.size(); i++) {
                            const auto& node = tree.nodes[i];
                            if(file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                if constexpr(RF_DEBUG_LEVEL > 1)    
                                Serial.printf("❌ Failed to write node %d for tree %d\n", i, tree.index);
                                writeSuccess = false;
                                break;
                            }
                            totalBytes += sizeof(node.packed_data);
                            
                            // Check for memory issues during write
                            if(ESP.getFreeHeap() < 5000) { // 5KB safety threshold
                                if constexpr(RF_DEBUG_LEVEL > 1)
                                Serial.printf("⚠️ Low memory during write (tree %d, node %d)\n", tree.index, i);
                            }
                        }
                        
                        if(!writeSuccess) {
                            if constexpr(RF_DEBUG_LEVEL > 1)
                            Serial.printf("❌ Failed to save tree %d \n", tree.index);
                            break;
                        }
                        
                        savedCount++;
                    }
                }
                file.close();
                
                // Verify file was written correctly
                if(savedCount != loadedCount) {
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Save incomplete: %d/%d trees saved\n", savedCount, loadedCount);
                    SPIFFS.remove(unifiedfilename);
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
                
                unsigned long end = GET_CURRENT_TIME_IN_MILLISECONDS;
                if constexpr(RF_DEBUG_LEVEL > 1)
                Serial.printf("✅ Released %d trees to unified format (%d bytes) in %lu ms\n", 
                            clearedCount, totalBytes, end - fileStart);
                return true;
            }

            public:
            Rf_tree& operator[](uint8_t index){
                return trees[index];
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
        b_vector<Rf_sample> buffer; // buffer for pending samples
        b_vector<uint8_t> actual_labels; // true labels of the samples, default 255 (unknown/error label)

        uint16_t max_pending_samples; // max number of pending samples in buffer

        // interval between 2 inferences. If after this interval the actual label is not provided, the currently labeled waiting sample will be skipped.
        long unsigned max_wait_time; // max wait time for true label in ms 
        long unsigned last_time_received_actual_label;   
        bool first_label_received = false; // flag to indicate if the first actual label has been received 

        Rf_base* base_ptr = nullptr; // pointer to base data, used for auto-flush
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
            buffer.clear();
            actual_labels.clear();
        }

        void init(Rf_base* base, Rf_config* config){
            base_ptr = base;
            config_ptr = config;
            buffer.clear();
            actual_labels.clear();
            set_max_pending_samples(100);
            max_wait_time = 2147483647; // ~24 days
        }

        // add pending sample to buffer, including the label predicted by the model
        void add_pending_sample(const Rf_sample& sample, Rf_data& base_data){
            buffer.push_back(sample);
            if(buffer.size() > max_pending_samples){
                // Auto-flush if parameters are provided
                if(ptr_ready()){
                    flush_pending_data(base_data);
                } else {
                    buffer.clear();
                    actual_labels.clear();
                }
            }
        }

        void add_actual_label(uint8_t true_label){
            uint16_t ignore_index = (GET_CURRENT_TIME_IN_MILLISECONDS - last_time_received_actual_label) / max_wait_time;
            if(!first_label_received){
                ignore_index = 0;
                first_label_received = true;
            }
            while(ignore_index-- > 0) actual_labels.push_back(255); // push error label for ignored samples

            // all pending samples have been labeled, ignore this label
            if(actual_labels.size() >= buffer.size()) return;

            actual_labels.push_back(true_label);
            last_time_received_actual_label = GET_CURRENT_TIME_IN_MILLISECONDS;
        }

        void set_max_pending_samples(uint16_t max_samples){
            max_pending_samples = max_samples;
        }

        void set_max_wait_time(long unsigned wait_time_ms){
            max_wait_time = wait_time_ms;
        }

        // write valid samples (with 0 < actual_label < 255) to base_data file
        bool write_to_base_data(Rf_data& base_data){
            if(buffer.empty()) {
                if constexpr (RF_DEBUG_LEVEL >= 1)
                Serial.println("⚠️ No pending samples to write to base data");
                return false;
            }
            if (!ptr_ready()) {
                if constexpr (RF_DEBUG_LEVEL >= 1)
                Serial.println("❌ Base or config pointer not set or base data not ready");
                return false;
            }
            // first scan 
            uint16_t valid_samples_count = 0;
            b_vector<Rf_sample> valid_samples;
            for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < 255) { // Valid actual label provided
                    valid_samples_count++;
                    Rf_sample sample(buffer[i].features, actual_labels[i]);
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
                if(config_ptr->num_samples > MAX_NUM_SAMPLES) 
                    config_ptr->num_samples = MAX_NUM_SAMPLES;
            }

            for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < 255) { // Valid actual label provided
                    config_ptr->samples_per_label[actual_labels[i]]++;
                }
            }

            for(auto& lbl : deleted_labels) {
                if(lbl < 255 && lbl < config_ptr->num_labels && config_ptr->samples_per_label[lbl] > 0) {
                    config_ptr->samples_per_label[lbl]--;
                }
            }

            if constexpr (RF_DEBUG_LEVEL >= 1) {
                Serial.printf("✅ Added %d new samples to base data\n", valid_samples_count);
            }
            return true;
        }

        // Write prediction which given actual label (0 < actual_label < 255) to the inference log file
        // New format: magic (4 bytes) + prediction_count (4 bytes) + pairs of (predicted_label, actual_label) as uint8_t each
        bool write_to_infer_log(){
            if(buffer.empty()) return false;
            if(!ptr_ready()){
                if constexpr (RF_DEBUG_LEVEL >= 1)
                Serial.println("❌ Base or config pointer not set or base data not ready");
                return false;
            }
            // Check if file exists to determine if header needs to be written
            String infer_log_file = base_ptr->get_infer_log_file();
            bool file_exists = SPIFFS.exists(infer_log_file.c_str());
            uint32_t current_prediction_count = 0;
            
            // If file exists, read current prediction count from header
            if(file_exists) {
                File read_file = SPIFFS.open(infer_log_file.c_str(), FILE_READ);
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
            
            File file = SPIFFS.open(infer_log_file.c_str(), file_exists ? FILE_APPEND : FILE_WRITE);
            if(!file) {
                if constexpr (RF_DEBUG_LEVEL > 0)
                Serial.printf("❌ Failed to open inference log file: %s\n", infer_log_file.c_str());
                return false;
            }
            
            // Write header if new file
            if(!file_exists) {
                // Magic number (4 bytes): 'I', 'N', 'F', 'L' (INFL)
                uint8_t magic_bytes[4] = {0x49, 0x4E, 0x46, 0x4C};
                size_t written = file.write(magic_bytes, 4);
                if(written != 4) {
                    if constexpr (RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Failed to write magic number: wrote %d bytes instead of 4\n", written);
                }
                
                // Write initial prediction count (4 bytes)
                uint32_t initial_count = 0;
                written = file.write((uint8_t*)&initial_count, 4);
                if(written != 4) {
                    if constexpr (RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Failed to write prediction count: wrote %d bytes instead of 4\n", written);
                }
                
                file.flush();
                if constexpr (RF_DEBUG_LEVEL >= 2)
                Serial.printf("✅ Wrote inference log header: magic=[0x%02X,0x%02X,0x%02X,0x%02X], count=%u\n", 
                             magic_bytes[0], magic_bytes[1], magic_bytes[2], magic_bytes[3], initial_count);
            }
            
            // Collect and write prediction pairs for valid samples
            b_vector<uint8_t> prediction_pairs;
            uint32_t new_predictions = 0;
            
            for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < 255) { // Valid actual label provided
                    uint8_t predicted_label = buffer[i].label;
                    uint8_t actual_label = actual_labels[i];
                    
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
                    if constexpr (RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Failed to write prediction pairs: wrote %d bytes instead of %d\n", 
                                 written, prediction_pairs.size());
                }
                
                file.flush();
                file.close();
                
                // Update prediction count in header - read entire file and rewrite
                File read_file = SPIFFS.open(infer_log_file.c_str(), FILE_READ);
                if(read_file) {
                    size_t file_size = read_file.size();
                    b_vector<uint8_t> file_data(file_size);
                    read_file.read(file_data.data(), file_size);
                    read_file.close();
                    
                    // Update prediction count in the header (bytes 4-7)
                    uint32_t updated_count = current_prediction_count + new_predictions;
                    memcpy(&file_data[4], &updated_count, 4);
                    
                    // Write back the entire file
                    File write_file = SPIFFS.open(infer_log_file.c_str(), FILE_WRITE);
                    if(write_file) {
                        write_file.write(file_data.data(), file_data.size());
                        write_file.flush();
                        write_file.close();
                        
                        if constexpr (RF_DEBUG_LEVEL >= 2) {
                            Serial.printf("✅ Added %u prediction pairs to log (total: %u)\n", 
                                         new_predictions, updated_count);
                        }
                    }
                }
            } else {
                file.close();
            }
            // Trim file if it exceeds max size
            return trim_log_file(infer_log_file.c_str());
        }

        // Public method to flush pending data when buffer is full or on demand
        void flush_pending_data(Rf_data& base_data) {
            if(buffer.empty()) return;
            
            write_to_base_data(base_data);
            write_to_infer_log();
            
            // Clear buffers after processing
            buffer.clear();
            actual_labels.clear();
        }

    private:
        // trim log file if it exceeds max size (MAX_INFER_LOGFILE_SIZE)
        bool trim_log_file(const char* infer_log_file) {
            if(!SPIFFS.exists(infer_log_file)) return false;
            
            File file = SPIFFS.open(infer_log_file, FILE_READ);
            if(!file) return false;
            
            size_t file_size = file.size();
            file.close();
            
            if(file_size <= MAX_INFER_LOGFILE_SIZE) return true; // No trimming needed;
            
            // File is too large, trim from the beginning (keep most recent data)
            file = SPIFFS.open(infer_log_file, FILE_READ);
            if(!file) return false;
            
            // Read and verify header
            uint8_t magic_bytes[4];
            uint32_t total_predictions;
            
            if(file.read(magic_bytes, 4) != 4 || 
               magic_bytes[0] != 0x49 || magic_bytes[1] != 0x4E || 
               magic_bytes[2] != 0x46 || magic_bytes[3] != 0x4C) {
                file.close();
                if constexpr (RF_DEBUG_LEVEL > 1)
                Serial.printf("❌ Invalid magic number in infer log file: %s\n", infer_log_file);
                return false;
            }
            
            if(file.read((uint8_t*)&total_predictions, 4) != 4) {
                file.close();
                if constexpr (RF_DEBUG_LEVEL > 1)
                Serial.printf("❌ Failed to read prediction count from infer log file: %s\n", infer_log_file);
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
                if constexpr (RF_DEBUG_LEVEL > 1)
                Serial.printf("❌ Failed to read remaining data: read %d bytes instead of %d\n", 
                             bytes_read, remaining_data_size);
                return false;
            }
            
            // Rewrite file with header and trimmed data
            file = SPIFFS.open(infer_log_file, FILE_WRITE);
            if(!file) {
                if constexpr (RF_DEBUG_LEVEL > 1)
                Serial.printf("❌ Failed to reopen log file for writing: %s\n", infer_log_file);
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
            
            if constexpr (RF_DEBUG_LEVEL >= 2) {
                Serial.printf("✅ Trimmed log file: %u -> %u predictions (removed %u oldest)\n", 
                             total_predictions, new_prediction_count, pairs_to_skip);
            }
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
        Rf_base* base_ptr = nullptr; // pointer to base data, used for file names
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }   
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        long unsigned starting_time;
        uint8_t fragmentation;
        uint32_t lowest_ram;
        uint32_t lowest_rom; 
        uint32_t freeDisk;
        float log_time;
        b_vector<time_anchor> time_anchors;
    
    public:
        Rf_logger() : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
        }

        Rf_logger(Rf_base* base, bool keep_old_file = false) : freeHeap(0), largestBlock(0), starting_time(0), fragmentation(0), log_time(0.0f) {
            init(base, keep_old_file);
        }

        ~Rf_logger(){
            base_ptr = nullptr;
            time_anchors.clear();
        }
        
        void init(Rf_base* base, bool keep_old_file = false){
            if constexpr(RF_DEBUG_LEVEL > 1)
            Serial.println("🔧 Initializing logger");

            base_ptr = base;
            time_anchors.clear();
            starting_time = GET_CURRENT_TIME_IN_MILLISECONDS;
            drop_anchor(); // initial anchor at index 0

            lowest_ram = UINT32_MAX;
            lowest_rom = UINT32_MAX;

            String time_log_file = base_ptr->get_time_log_file();
            String memory_log_file = base_ptr->get_memory_log_file();
            if(!keep_old_file){
                if(SPIFFS.exists(time_log_file.c_str())){
                    SPIFFS.remove(time_log_file.c_str()); 
                }
                // write header to time log file
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_WRITE);
                if (logFile) {
                    logFile.println("Event,\t\tTime(ms),duration,Unit");
                    logFile.close();
                }
            }
            t_log("init tracker"); // Initial log without printing

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

        void m_log(const char* msg, bool print = true, bool log = true){
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
                if constexpr(RF_DEBUG_LEVEL > 1) {
                    Serial.print("--> RAM LEFT (heap): ");
                    Serial.println(freeHeap);
                    // Serial.print("Largest Free Block: ");
                    // Serial.println(largestBlock);
                    // Serial.printf("Fragmentation: %d", fragmentation);
                    // Serial.println("%");
                }
            }

            // Log to file with timestamp
            if(log) {        
                log_time = (GET_CURRENT_TIME_IN_MILLISECONDS - starting_time)/1000.0f; 
                if(has_base()){
                    String memory_log_file = base_ptr->get_memory_log_file();
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
                }else{
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("❌ Unable to log: base_ptr is null or not ready");
                }
            }
        }

        // fast log : just for measure and update : lowest ram and fragmentation
        void m_log(){
            m_log("", false, false);
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
        void t_log(const char* msg, size_t begin_anchor_index, size_t end_anchor_idex, const char* unit = "ms" , bool print = true){
            float ratio = 1;  // default to ms 
            if(strcmp(unit, "s") == 0 || strcmp(unit, "second") == 0) ratio = 1000.0f;
            else if(strcmp(unit, "us") == 0 || strcmp(unit, "microsecond") == 0) ratio = 0.001f;

            if(time_anchors.size() == 0) return; // no anchors set
            if(begin_anchor_index >= time_anchors.size() || end_anchor_idex >= time_anchors.size()) return; // invalid index
            if(end_anchor_idex <= begin_anchor_index) {
                std::swap(begin_anchor_index, end_anchor_idex);
            }

            long unsigned begin_time = time_anchors[begin_anchor_index].anchor_time;
            long unsigned end_time = time_anchors[end_anchor_idex].anchor_time;
            float elapsed = (end_time - begin_time)/ratio;
            if(print){
                if constexpr(RF_DEBUG_LEVEL >= 1) {         
                    if(msg && strlen(msg) > 0){
                        Serial.print("⏱️  ");
                        Serial.print(msg);
                        Serial.print(": ");
                    } else {
                        Serial.print("⏱️  unknown event: ");
                    }
                    Serial.print(elapsed);
                    Serial.println(unit);
                }
            }
            // Log to file with timestamp      
            if(has_base()){
                String time_log_file = base_ptr->get_time_log_file(); 
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_APPEND);
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
                }
            }else{
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("❌ Unable to log: base_ptr is null or not ready");
            }
            time_anchors[end_anchor_idex].anchor_time = GET_CURRENT_TIME_IN_MILLISECONDS; // reset end anchor to current time
        }
    
        /**
         * @brief for duration measurement from an anchor to now
         * @param msg name of the event
         * @param begin_anchor_index index of the begin anchor
         * @param unit time unit, "ms" (default), "s", "us" 
         * @param print whether to print to Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will create a new anchor at the current time
         */
        void t_log(const char* msg, size_t begin_anchor_index, const char* unit = "ms" , bool print = true){
            time_anchor end_anchor;
            end_anchor.anchor_time = GET_CURRENT_TIME_IN_MILLISECONDS;
            end_anchor.index = time_anchors.size();
            time_anchors.push_back(end_anchor);
            t_log(msg, begin_anchor_index, end_anchor.index, unit, print);
        }

        /**
         * @brief log time from starting point to now
         * @param msg name of the event
         * @param print whether to print to Serial, will be disabled if RF_DEBUG_LEVEL <= 1
         * @note : this action will NOT create a new anchor
         */
        void t_log(const char* msg, bool print = true){
            long unsigned current_time = GET_CURRENT_TIME_IN_MILLISECONDS - starting_time;
            if(print){
                if constexpr(RF_DEBUG_LEVEL > 1) {         
                    if(msg && strlen(msg) > 0){
                        Serial.print("⏱️  ");
                        Serial.print(msg);
                        Serial.print(": ");
                    } else {
                        Serial.print("⏱️  unknown event: ");
                    }
                    Serial.print(current_time);       // timeline always in ms
                    Serial.println("ms");
                }
            }
            // Log to file with timestamp

            if(has_base()){
                String time_log_file = base_ptr->get_time_log_file();     
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_APPEND);
                if (logFile) {
                    if(msg && strlen(msg) > 0){
                        logFile.printf("%s,\t%.1f,\t_,\tms\n", msg, current_time/1000.0f); // time always in s
                    } else {
                        logFile.printf("unknown event,\t%.1f,\t_,\tms\n", current_time/1000.0f); // time always in s
                    }
                    logFile.close();
                }else{
                    if constexpr(RF_DEBUG_LEVEL > 0)
                    Serial.printf("❌ Failed to open time log file: %s\n", time_log_file.c_str());
                }
            }else{
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("❌ Unable to log: base_ptr is null or not ready");
            }
        }
    };

} // namespace mcu