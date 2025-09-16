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
    #if RF_DEBUG_LEVEL > 5
        #undef RF_DEBUG_LEVEL
        #define RF_DEBUG_LEVEL 5
    #endif
#endif

/*
 NOTE : RF_DEBUG_LEVEL
    0 : silent mode - no messages
    1 : forest messages (start, end, major events) 
    2 : logger messages (errors, warnings, memory usage, timing)
    3 : messages at components level
    4 : 
    5 : extremely detailed debug (not recommended for normal use)
*/

static constexpr uint16_t MAX_LABELS             = 255;         // maximum number of unique labels supported 
static constexpr uint16_t MAX_NUM_FEATURES       = 1024;       // maximum number of features
static constexpr uint16_t MAX_NUM_SAMPLES        = 65535;     // maximum number of samples in a dataset
static constexpr uint16_t MAX_NODES              = 2047;     // Maximum nodes per tree 
static constexpr size_t   MAX_DATASET_SIZE       = 150000;  // Max dataset file size - 150kB
constexpr static size_t   MAX_INFER_LOGFILE_SIZE = 2048;   // Max log file size in bytes 

using sampleID_set = mcu::ID_vector<uint16_t>;       // set of unique sample IDs


/*
 NOTE : Random file components (for each model)
    1. model_name_nml.bin       : base data (dataset)
    2. model_name_config.json   : model configuration file 
    3. model_name_ctg.bin       : categorizer (feature quantizer and label mapping)
    4. model_name_dp.csv        : information about dataset (num_features, num_labels...)
    5. model_name_forest.bin    : model file (all trees) in unified format
    6. model_name_tree_*.bin    : model files (tree files) in individual format. (Given from pc/used during training, optional)
    7. model_name_node_pred.bin : node predictor file 
    8. model_name_node_log.csv  : node splitting log file during training (for retraining node predictor)
    9. model_name_infer_log.bin : inference log file (predictions, actual labels, metrics over time)
    10. model_name_time_log.csv     : time log file (detailed timing of forest events)
    11. model_name_memory_log.csv   : memory log file (detailed memory usage of forest events)
*/

class RandomForest;
// Forward declaration for callback 
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
    class Rf_node_predictor;    // Helper class: predict and pre-allocate resources needed during tree building
    class Rf_categorizer;       // sample quantizer (categorize features and labels mapping)
    class Rf_logger;            // logging forest events timing, memory usage, messages, errors
    class Rf_random;            // random generator (for stability across platforms and runs)
    class Rf_matrix_score;      // confusion matrix and metrics calculator
    class Rf_tree_container;    // manages all trees at forest level

    // enum Rf_training_flags;      // flags for training process/score calculation (accuracy, precision, recall, f1_score)
    // enum Rf_training_score;      // score types for training process (oob, validation, k-fold)
    // ...

    /*
    ------------------------------------------------------------------------------------------------------------------
    -------------------------------------------------- RF_DATA ------------------------------------------------------
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
            if constexpr(RF_DEBUG_LEVEL > 2)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 3) Serial.println("❌ Failed to read binary header.");
                file.close();
                return;
            }
            size_ = numSamples;
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            file.close();
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("ℹ️ Rf_data initialized: %s with %d features (%d bits/sample, %d samples/chunk, %d samples total)\n", 
                          filename.c_str(), numFeatures, bitsPerSample, samplesEachChunk, size_);
        }

        // for temporary Rf_data (without saving to SPIFFS)
        void init(uint16_t numFeatures) {   
            filename = "";
            bitsPerSample = numFeatures * 2;
            updateSamplesEachChunk();
            if constexpr(RF_DEBUG_LEVEL > 2)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.println("❌ Rf_data not loaded. Call loadData() first.");
                return Rf_sample();
            }
            if(sampleIndex >= size_){
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
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
            if(isLoaded) {
                // clear existing data
                sampleChunks.clear();
                allLabels.clear();
                size_ = 0;
                isLoaded = false;
            }
            
            File file = SPIFFS.open(csvFilename.c_str(), FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.println("❌ Failed to open CSV file for reading.");
                return;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
                    if constexpr(RF_DEBUG_LEVEL > 3)
                        Serial.printf("❌ Feature count mismatch: initialized for %d features, CSV has %d\n", 
                                expectedFeatures, numFeatures);
                    file.close();
                    return;
                }
            }
            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("📊 Loading CSV: %s (expecting %d features per sample)\n", csvFilename.c_str(), numFeatures);
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
                    if constexpr(RF_DEBUG_LEVEL > 3)
                        Serial.printf("❌ Line %d: Expected %d fields, got %d\n", linesProcessed, numFeatures + 1, fieldIdx);
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
            if constexpr(RF_DEBUG_LEVEL > 2) Serial.println("✅ CSV data loaded and file removed.");
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
                if constexpr(RF_DEBUG_LEVEL > 3)
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
            if constexpr (RF_DEBUG_LEVEL > 2)
                Serial.println("🔄 Converting CSV to binary format...");
            loadCSVData(csvFilename, numFeatures);
            releaseData(false); // Save to binary and clear memory
            if constexpr (RF_DEBUG_LEVEL > 2)
                Serial.println("✅ CSV conversion complete.");
        }

        /**
         * @brief Save data to SPIFFS in binary format and clear from RAM.
         * @param reuse If true, keeps data in RAM after saving; if false, clears data from RAM.
         * @note: after first time rf_data created, it must be releaseData(false) to save data
         */
        void releaseData(bool reuse = true) {
            if(!isLoaded) return;
            
            if(!reuse){
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("💾 Saving data to SPIFFS and clearing from RAM...");
                // Remove any existing file
                if (SPIFFS.exists(filename.c_str())) {
                    SPIFFS.remove(filename.c_str());
                }

                File file = SPIFFS.open(filename.c_str(), FILE_WRITE);
                if (!file) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
                        Serial.println("❌ Failed to open binary file for writing.");
                    return;
                }
                if constexpr(RF_DEBUG_LEVEL > 2) 
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

            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("✅ Data saved: %s (%d samples, %d features, %d bytes)\n", 
                            filename.c_str(), size_, bitsPerSample / 2, memory_usage());
        }

        // Load data using sequential indices 
        void loadData(bool re_use = true) {
            if(isLoaded || filename.length() < 1) return;
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("📂 Loading data from: %s\n", filename.c_str());
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                        if constexpr(RF_DEBUG_LEVEL > 3)
                        Serial.printf("❌ Failed to read label for sample %u\n", (unsigned)processed);
                        file.close();
                        return;
                    }
                    allLabels.push_back(lbl);
                    uint8_t packed[packedFeatureBytes];
                    if (file.read(packed, packedFeatureBytes) != packedFeatureBytes) {
                        if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("💾 Single-load mode: removing SPIFFS file after loading.");
                SPIFFS.remove(filename.c_str()); // Remove file after loading in single mode
            }
            if constexpr(RF_DEBUG_LEVEL > 2)
            Serial.printf("✅ Data loaded fast: %s (using %d chunks)\n", filename.c_str(), sampleChunks.size());
        }

        /**
         * @brief Load specific samples from another Rf_data source by sample IDs.
         * @param source The source Rf_data to load samples from.
         * @param sample_IDs A sorted set of sample IDs to load from the source.
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: The state of the source data will be automatically restored, no need to reload.
         */
        void loadData(Rf_data& source, const sampleID_set& sample_IDs, bool save_ram = true) {
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("📂 Loading %d specific samples from source Rf_data: %s\n", 
                sample_IDs.size(), source.filename.c_str());
            if (source.filename.length() == 0) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.println("❌ Source Rf_data has no filename for SPIFFS loading.");
                return;
            }

            if (!SPIFFS.exists(source.filename.c_str())) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ Source file does not exist: %s\n", source.filename.c_str());
                return;
            }

            File file = SPIFFS.open(source.filename.c_str(), FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                    if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Sample ID %d exceeds file sample count %d\n", sampleIdx, numSamples);
                    continue;
                }
                
                // Calculate file position for this sample
                size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);
                size_t sampleFilePos = headerSize + (sampleIdx * sampleDataSize);
                
                // Seek to the sample position
                if (!file.seek(sampleFilePos)) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Failed to seek to sample %d position %d\n", sampleIdx, sampleFilePos);
                    continue;
                }
                
                Rf_sample s;
                
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Failed to read label for sample %d\n", sampleIdx);
                    continue;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("♻️ Restoring source Rf_data state after loading.");
                source.loadData(); // reload source if it was pre-loaded
            }
            if constexpr(RF_DEBUG_LEVEL > 2)
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
            if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("📂 Loading chunk %d from source Rf_data: %s\n", 
                chunkIndex, source.filename.c_str());
            if(chunkIndex >= source.total_chunks()) {
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ Invalid chunk range: start %d, end %d\n", startSample, endSample);
                return;
            }
            sampleID_set chunkIDs(startSample, endSample - 1);
            chunkIDs.fill();
            loadData(source, chunkIDs, save_ram);   
        }

        // add new sample to the end of the dataset
        bool add_new_sample(const Rf_sample& sample, Rf_config& config) {
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
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
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
            if (filename.length() > 0 && SPIFFS.exists(filename.c_str())) {
                SPIFFS.remove(filename.c_str());
                if constexpr(RF_DEBUG_LEVEL > 2)
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
                if (index == 255 || nodes.empty()) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
                         Serial.println("❌ No valid index specified or tree is empty for saving");
                    return;
                }

                if constexpr(RF_DEBUG_LEVEL > 2){
                    Serial.printf("🔄 Saving tree %d \n", index);
                }

                char filename[32];  // filename = "/"+ model_name + "tree_" + index + ".bin"
                snprintf(filename, sizeof(filename), "/%s_tree_%d.bin", model_name.c_str(), index);
                
                // Skip exists/remove check - just overwrite directly for performance
                File file = SPIFFS.open(filename, FILE_WRITE);
                if (!file) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
                            if constexpr(RF_DEBUG_LEVEL > 5)
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
            if constexpr(RF_DEBUG_LEVEL > 2){
                uint16_t countLeaf = countLeafNodes();
                Serial.printf("✅ Tree saved: %d (%d nodes, %d leaves, depth %d, %d bytes)\n", 
                              index, nodes.size(), countLeaf, getTreeDepth(), memory_usage());
            }

            nodes.clear(); // Clear nodes to free memory
            nodes.fit(); // Release excess memory
            isLoaded = false; // Mark as unloaded
        }

        // Load tree from SPIFFS into RAM for ESP32
        void loadTree(String model_name = "", bool re_use = false) {
            if (isLoaded) return;
            
            if (index == 255) {
                if constexpr(RF_DEBUG_LEVEL > 3) 
                    Serial.println("❌ No valid index specified for tree loading");
                return;
            }
            
            char path_to_use[32];
            snprintf(path_to_use, sizeof(path_to_use), "/%s_tree_%d.bin", model_name.c_str(), index);

            if constexpr(RF_DEBUG_LEVEL > 3){
                Serial.printf("🔄 Loading tree from: %s\n", path_to_use);
            }
            
            File file = SPIFFS.open(path_to_use, FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Failed to open tree file: %s\n", path_to_use);
                return;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x54524545) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Invalid tree file format: %s\n", path_to_use);
                file.close();
                return;
            }
            
            // Read number of nodes
            uint32_t nodeCount;
            if (file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Failed to read node count: %s\n", path_to_use);
                file.close();
                return;
            }
            
            if (nodeCount == 0 || nodeCount > 2047) { // 11-bit limit for child indices
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                    if constexpr(RF_DEBUG_LEVEL > 3)
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
            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("✅ Tree loaded: %s (%d nodes, %d bytes)\n", 
                        path_to_use, nodeCount, memory_usage());
            }
            if (!re_use) {
                if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("🗑️ Removing tree file after load: %s\n", path_to_use);    
                SPIFFS.remove(path_to_use); // Remove file after loading in single mode
            }
        }

        // predict single (normalized)sample - packed features
        uint8_t predictSample(const mcu::packed_vector<2>& packed_features) const {
            if (nodes.empty() || !isLoaded) return 0;
            
            uint16_t currentIndex = 0;  // Start from root
            
            while (currentIndex < nodes.size() && !nodes[currentIndex].getIsLeaf()) {
                uint16_t featureID = nodes[currentIndex].getFeatureID();
                // Bounds check for feature access
                if (featureID >= packed_features.size()) {
                    if constexpr(RF_DEBUG_LEVEL > 3)
                        Serial.println("❌ Feature ID out of bounds during prediction");
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
    ---------------------------------------------------- RF_CONFIG ---------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef enum Rf_training_flags : uint8_t{
        EARLY_STOP  = 0x00,        // early stop training if accuracy is not improving
        ACCURACY    = 0x01,          // calculate accuracy of the model
        PRECISION   = 0x02,          // calculate precision of the model
        RECALL      = 0x04,            // calculate recall of the model
        F1_SCORE    = 0x08          // calculate F1 score of the model
    }Rf_training_flags;


    typedef enum Rf_training_score : uint8_t {
        OOB_SCORE = 0x00,   // default 
        VALID_SCORE = 0x01,
        K_FOLD_SCORE = 0x02
    } Rf_training_score;

    // Configuration class for Random Forest parameters
    // handle 2 files: model_name_config.json (config file) and model_name_dp.csv (dp file)
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

        bool extend_base_data;
        bool enable_retrain;

        // Dataset parameters (set after loading data)
        uint16_t num_samples;
        uint16_t num_features;
        uint8_t num_labels;
        mcu::pair<uint8_t, uint8_t> min_split_range;
        mcu::pair<uint8_t, uint8_t> max_depth_range;  

        b_vector<uint16_t> samples_per_label; // index = label, value = count
        
        String filename;
        bool isLoaded;

    private:
        // get dpfile from config filename : "/model_name_config.json" -> "/model_name_dp.csv"
        String getDpFilename() {
            if (filename.length() < 1) return "";
            int slashIdx = filename.lastIndexOf('/');
            int dotIdx = filename.lastIndexOf("_config.json");
            String baseName = (slashIdx >= 0) ? filename.substring(slashIdx, dotIdx) : filename.substring(0, dotIdx);
            return baseName + "_dp.csv";
        }
    public:
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
            
            // Set defaults for new properties (not in initial config file)
            extend_base_data = true;
            enable_retrain = true;
            
            filename = "";
        }

        ~Rf_config() {
            releaseConfig();
            releaseDpFile();
        }

        void init(const String& fn){
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("🔧 Initializing Rf_config with file: %s\n", fn.c_str());
            filename = fn;
            isLoaded = false;
        }

        // Load configuration from JSON file in SPIFFS
        void loadConfig() {
            if (isLoaded) return;

            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3){
                    Serial.printf("❌ Failed to open config file: %s\n", filename.c_str());
                    Serial.println("Switching to default configuration.");
                }
                return;
            }

            String jsonString = file.readString();
            file.close();

            // Parse JSON manually (simple parsing for known structure)
            parseJSONConfig(jsonString);
            isLoaded = true;
            if constexpr(RF_DEBUG_LEVEL > 2) {
                Serial.printf("✅ Config loaded: %s\n", filename.c_str());
                Serial.printf("   Trees: %d, max_depth: %d, min_split: %d\n", num_trees, max_depth, min_split);
                Serial.printf("   Estimated RAM: %d bytes\n", estimatedRAM);
                Serial.printf("   extend_base_data: %s, enable_retrain: %s\n", 
                            extend_base_data ? "true" : "false",
                            enable_retrain ? "true" : "false");
            }

            loadDpFile(); // Load dataset parameters
            
            // Validate configuration after loading
            if constexpr(RF_DEBUG_LEVEL > 2) {
                if (validateSamplesPerLabel()) {
                    Serial.println("✅ samples_per_label data is consistent");
                } else {
                    Serial.println("⚠️ Warning: samples_per_label data inconsistency detected");
                }
            }
        }

        // read dataset parameters from /dataset_dp.csv and write to config
        void loadDpFile() {
            String path = getDpFilename();
            if (path.length() < 1 || !SPIFFS.exists(path.c_str())) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Dataset params file not found: %s\n", path.c_str());
                return;
            }
            // Read dataset parameters from /dataset_params.csv
            File file = SPIFFS.open(path.c_str(), "r");
            if (!file) {
                Serial.println("❌ Failed to open data_params file.");
                return;
            }

            // Skip header line
            file.readStringUntil('\n');

            // Initialize variables with defaults
            uint16_t numSamples = 0;
            uint16_t numFeatures = 0;
            uint8_t numLabels = 0;
            mcu::unordered_map<uint8_t, uint16_t> labelCounts; // label -> count
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
            // this->lowest_distribution = lowest_distribution / 100.0f; // Store as fraction

            // Calculate optimal parameters based on dataset size
            int baseline_minsplit_ratio = 100 * (num_samples / 500 + 1); 
            if (baseline_minsplit_ratio > 500) baseline_minsplit_ratio = 500; 
            uint8_t min_minSplit = max(2, (int)(num_samples / baseline_minsplit_ratio));
            int dynamic_max_split = min(min_minSplit + 6, (int)(log2(num_samples) / 4 + num_features / 25.0f));
            uint8_t max_minSplit = min(24, dynamic_max_split); // Cap at 24 to prevent overly simple trees.
            if (max_minSplit <= min_minSplit) max_minSplit = min_minSplit + 4; // Ensure a valid range.


            int base_maxDepth = max((int)log2(num_samples * 2.0f), (int)(log2(num_features) * 2.5f));
            uint8_t max_maxDepth = max(6, base_maxDepth);
            int dynamic_min_depth = max(4, (int)(log2(num_features) + 2));
            uint8_t min_maxDepth = min((int)max_maxDepth - 2, dynamic_min_depth); // Ensure a valid range.
            if (min_maxDepth >= max_maxDepth) min_maxDepth = max_maxDepth - 2;
            if (min_maxDepth < 4) min_maxDepth = 4;

            if(min_split == 0 || max_depth == 0) {
                min_split = (min_minSplit + max_minSplit) / 2 - 2;
                max_depth = (min_maxDepth + max_maxDepth) / 2 + 2;
                Serial.println("⚙️ Not found minSplit/maxDepth in .");
                Serial.printf("Setting minSplit to %u and maxDepth to %u based on dataset size.\n", 
                            min_split, max_depth);
            }

            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("⚙️ Setting minSplit range: %d to %d (current: %d)\n", 
                            min_minSplit, max_minSplit, min_split);
                Serial.printf("⚙️ Setting maxDepth range: %d to %d (current: %d)\n", 
                            min_maxDepth, max_maxDepth, max_depth);
            }

            min_split_range = make_pair(min_minSplit, max_minSplit);
            max_depth_range = make_pair(min_maxDepth, max_maxDepth);

            // at deployment, test_set is not used, merge it to train_set
            if (!ENABLE_TEST_DATA){
                train_ratio += test_ratio;
            }
        }

        // write back to dataset_params file
        void releaseDpFile() {
            String path = getDpFilename();
            if (path.length() < 1) return;
            File file = SPIFFS.open(path.c_str(), "w");
            if (!file) {
                Serial.println("❌ Failed to open data_params file for writing.");
                return;
            }
            
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("💾 Saving dataset parameters to: %s\n", path.c_str());
            
            // Write header
            file.println("parameter,value");
            
            // Write core dataset parameters
            file.println("quantization_coefficient,2");  // Fixed for 2-bit quantization
            file.println("max_feature_value,3");         // Fixed for 2-bit values (0-3)
            file.println("features_per_byte,4");          // Fixed for 2-bit packing (4 features per byte)
            
            file.printf("num_features,%u\n", num_features);
            file.printf("num_samples,%u\n", num_samples);
            file.printf("num_labels,%u\n", num_labels);
            
            // Write actual label counts from samples_per_label vector
            for (uint8_t i = 0; i < num_labels && i < samples_per_label.size(); i++) {
                file.printf("samples_label_%u,%u\n", i, samples_per_label[i]);
            }
            
            file.close();
            
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("✅ Dataset parameters saved successfully.");
        }
        // Save configuration to JSON file in SPIFFS  
        void releaseConfig() {
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("💾 Saving config to SPIFFS: %s\n", filename.c_str());
            // Read existing file to preserve timestamp and author

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
            if constexpr(RF_DEBUG_LEVEL > 2)
            Serial.printf("✅ Config saved to: %s\n", filename.c_str());
        }

        // Update timestamp in the JSON file
        void update_timestamp() {
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("⏱️ Updating timestamp in config file: %s\n", filename.c_str());
            if (!SPIFFS.exists(filename.c_str())) return;
            
            // Read existing file
            File readFile = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!readFile) return;
            
            String jsonContent = readFile.readString();
            readFile.close();
            
            // Get current timestamp
            String currentTime = String(GET_CURRENT_TIME_IN_MILLISECONDS); // Simple timestamp, can be improved
            
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
            
            if constexpr(RF_DEBUG_LEVEL > 2) {
                Serial.printf("   extend_base_data: %s, enable_retrain: %s\n", 
                            extend_base_data ? "true" : "false",
                            enable_retrain ? "true" : "false");
            }
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
    private:
        uint8_t flags; // flags indicating the status of member files
        String model_name;
    public:
        Rf_base() : flags(static_cast<Rf_base_flags>(0)), model_name("") {}
        Rf_base(const char* bn) : flags(static_cast<Rf_base_flags>(0)), model_name(bn) {
            init(bn);
        }

        void init(const char* model_name){
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("🔧 Initializing the base with model name: %s\n", model_name ? model_name : "NULL");
            if (!model_name || strlen(model_name) == 0) {
                Serial.println("❌ Base name is empty.");
                return;
            } else {
                this->model_name = String(model_name);
                
                // BaseFile will now always have the structure "/model_name_nml.bin"
                String model_dataset = "/" + this->model_name + "_nml.bin";
                
                // Check if binary base file exists
                if(SPIFFS.exists(model_dataset.c_str())) {
                    // setup flags to indicate base file exists
                    flags = static_cast<Rf_base_flags>(EXIST_BASE_DATA);
                    
                    // generate filenames for data_params and categorizer based on model_name
                    String dataParamsFile = "/" + this->model_name + "_dp.csv"; // data_params file
                    String categorizerFile = "/" + this->model_name + "_ctg.csv"; // categorizer file

                    //  check categorizer file
                    if(SPIFFS.exists(categorizerFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_CATEGORIZER);
                    } else {
                        if constexpr(RF_DEBUG_LEVEL > 2){
                            Serial.printf("❌ No categorizer file found : %s\n", categorizerFile.c_str());
                            Serial.println("-> Model still able to re_train or run inference, but cannot re_train with new data later.");
                        }
                    }
                    // check data_params file
                    if(SPIFFS.exists(dataParamsFile.c_str())) {
                        flags |= static_cast<Rf_base_flags>(EXIST_DATA_PARAMS);
                        // If data_params exists, able to training
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_TRAINING);
                    } else {
                        if constexpr(RF_DEBUG_LEVEL > 2){
                            Serial.printf("❌ No data_parameters file found: %s\n", dataParamsFile.c_str());
                            Serial.println("Re_training and inference are not available..\n");
                        }
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
                        if constexpr(RF_DEBUG_LEVEL > 2)
                        Serial.printf("✅ Found unified model file: %s\n", unifiedModelFile.c_str());
                    } else {
                        // Check for individual tree files starting from tree_0.bin
                        for(uint8_t i = 0; i < 100; i++) { // Max 100 trees check
                            String treeFile = String("/") + model_name + "_tree_" + String(i) + ".bin";
                            if (SPIFFS.exists(treeFile.c_str())) {
                                tree_count++;
                            } else {
                                if constexpr(RF_DEBUG_LEVEL > 3)
                                Serial.printf("❌ Missing tree file: %s\n", treeFile.c_str());
                                break; // Stop when we find a missing tree file
                            }
                        }
                        
                        if (tree_count > 0) {
                            all_trees_exist = true;
                            if constexpr(RF_DEBUG_LEVEL > 2)
                            Serial.printf("✅ Found %d individual tree files\n", tree_count);
                        }
                    }
                    
                    if(all_trees_exist && (flags & EXIST_CATEGORIZER)) {
                        flags |= static_cast<Rf_base_flags>(ABLE_TO_INFERENCE);
                        if (flags & UNIFIED_MODEL_FORMAT) {
                            if constexpr(RF_DEBUG_LEVEL > 2)
                            Serial.println("✅ Inference enabled (unified model format)");
                        } else {
                            if constexpr(RF_DEBUG_LEVEL > 2)
                            Serial.printf("✅ Inference enabled (%d individual tree files)\n", tree_count);
                        }
                    } 
                    
                } else {
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.printf("❌ Base dataset does not exist: %s\n", model_dataset.c_str());
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

        size_t memory_usage() const {
            size_t total = sizeof(Rf_base);
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
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("🔧 Initializing node predictor with file: %s\n", fn ? fn : "NULL");
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
                    if constexpr(RF_DEBUG_LEVEL > 2)
                    Serial.println("⚠️  Unexpected node predictor filename format, using fallback for log filename generation.");
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
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("🔍 Loading node predictor from file: %s\n", filename.c_str());
            if(is_trained) return true;
            if (!SPIFFS.exists(filename.c_str())) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ No predictor file found: %s !\n", filename.c_str());
                Serial.println("Switching to use default predictor.");
                return false;
            }
            
            File file = SPIFFS.open(filename.c_str(), FILE_READ);
            if (!file) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ Failed to open predictor file: %s\n", filename.c_str());
                return false;
            }
            
            // Read and verify magic number
            uint32_t magic;
            if (file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic) || magic != 0x4E4F4445) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ Invalid predictor file format: %s\n", filename.c_str());
                file.close();
                return false;
            }
            
            // Read training status (but don't use it to set is_trained - that's set after successful loading)
            bool file_is_trained;
            if (file.read((uint8_t*)&file_is_trained, sizeof(file_is_trained)) != sizeof(file_is_trained)) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.println("❌ Failed to read training status");
                file.close();
                return false;
            }
            
            // Read accuracy and peak_percent
            if (file.read((uint8_t*)&accuracy, sizeof(accuracy)) != sizeof(accuracy)) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.println("❌ Failed to read accuracy");
                file.close();
                return false;
            }
            
            if (file.read((uint8_t*)&peak_percent, sizeof(peak_percent)) != sizeof(peak_percent)) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.println("❌ Failed to read peak_percent");
                file.close();
                return false;
            }
            
            // Read number of coefficients
            uint8_t num_coefficients;
            if (file.read((uint8_t*)&num_coefficients, sizeof(num_coefficients)) != sizeof(num_coefficients) || num_coefficients != 3) {
                if constexpr(RF_DEBUG_LEVEL > 3)
                Serial.printf("❌ Invalid coefficient count: %d (expected 3)\n", num_coefficients);
                file.close();
                return false;
            }
            
            // Read coefficients
            if (file.read((uint8_t*)coefficients, sizeof(float) * 3) != sizeof(float) * 3) {
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("✅ Node_predictor loaded: %s (accuracy: %d%%, peak: %d%%)\n", 
                            filename.c_str(), accuracy, peak_percent);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
                }
            } else {
                if constexpr(RF_DEBUG_LEVEL > 2)
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

            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.printf("💾 Saving node predictor to file: %s\n", filename.c_str());
            
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
            
            if constexpr(RF_DEBUG_LEVEL > 2)
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
            if constexpr (RF_DEBUG_LEVEL > 2)
                Serial.println("🔂 Starting retraining of node predictor...");
            if(!can_retrain()) {
                if constexpr(RF_DEBUG_LEVEL > 2)
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
                if constexpr(RF_DEBUG_LEVEL > 3)
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
                // if constexpr(RF_DEBUG_LEVEL > 3)
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
            
            // if constexpr(RF_DEBUG_LEVEL > 3)
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

            if constexpr(RF_DEBUG_LEVEL > 2){
                Serial.printf("✅ Retraining complete! Accuracy: %d%%, Peak: %d%%\n", accuracy, peak_percent);
                Serial.printf("   Coefficients: bias=%.2f, split=%.2f, depth=%.2f\n", 
                            coefficients[0], coefficients[1], coefficients[2]);
                Serial.printf("   MAE: %.2f, MAPE: %.2f%%\n", mae, mape);
                Serial.printf("   Split effect: %.2f, Depth effect: %.2f\n", split_effect, depth_effect);
            }

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
    ------------------------------------------------ RF_LOGGER -------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    typedef struct time_anchor{
        long unsigned anchor_time;
        uint16_t index;
    };

    class Rf_logger {
    public:
        uint32_t freeHeap;
        uint32_t largestBlock;
        long unsigned starting_time;
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
            starting_time = GET_CURRENT_TIME_IN_MILLISECONDS;
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
                if constexpr(RF_DEBUG_LEVEL > 1) {         
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
            if(time_log_file.length() > 0) {        
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
            if(time_log_file.length() > 0) {        
                File logFile = SPIFFS.open(time_log_file.c_str(), FILE_APPEND);
                if (logFile) {
                    if(msg && strlen(msg) > 0){
                        logFile.printf("%s,\t%.1f,\t_,\tms\n", msg, current_time/1000.0f); // time always in s
                    } else {
                        logFile.printf("unknown event,\t%.1f,\t_,\tms\n", current_time/1000.0f); // time always in s
                    }
                    logFile.close();
                }else{
                    if constexpr(RF_DEBUG_LEVEL > 3)
                    Serial.printf("❌ Failed to open time log file: %s\n", time_log_file.c_str());
                }
            }
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
        mcu::b_vector<String, mcu::SMALL> labelMapping; // Optional label reverse mapping
        
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
                if constexpr(RF_DEBUG_LEVEL > 4)
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
        
        Rf_categorizer(const String& csvFilename) : filename(csvFilename), isLoaded(false) {}

        void init(const String& csvFilename) {
            if constexpr(RF_DEBUG_LEVEL > 2)
                Serial.println("🔧 Initializing categorizer with file: " + csvFilename);
            filename = csvFilename;
            isLoaded = false;
        }
        
        // Load categorizer data from CTG v2 format
        bool loadCategorizer(bool re_use = true) {
            if constexpr (RF_DEBUG_LEVEL > 2)
                Serial.println("📂 Loading categorizer from file: " + filename);
            if (!SPIFFS.exists(filename)) {
                Serial.println("❌ Categorizer file not found: " + filename);
                return false;
            }
            
            File file = SPIFFS.open(filename, "r");
            if (!file) {
                Serial.println("❌ Failed to open Categorizer file: " + filename);
                return false;
            }
            
            Serial.println("📂 Loading Categorizer from: " + filename);
            
            try {
                // Read header: Categorizer,numFeatures,groupsPerFeature,numLabels,numSharedPatterns,scaleFactor
                if (!file.available()) {
                    Serial.println("❌ Empty Categorizer file");
                    file.close();
                    return false;
                }
                
                String headerLine = file.readStringUntil('\n');
                headerLine.trim();
                auto headerParts = split(headerLine, ',');
                
                if (headerParts.size() != 6 || headerParts[0] != "CTG2") {
                    Serial.println("❌ Invalid Categorizer header format");
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
                
                Serial.println("✅ Categorizer loaded successfully!");
                Serial.println("   Memory usage: " + String(memory_usage()) + " bytes");
                
                // Clean up file if not reusing
                if (!re_use) {
                    SPIFFS.remove(filename);
                }
                
                return true;
                
            } catch (...) {
                Serial.println("❌ Error parsing Categorizer file");
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

        // categorize features array
        mcu::packed_vector<2> categorizeFeatures(const float* features, size_t featureCount = 0) const {
            if (featureCount == 0) {
                if constexpr (RF_DEBUG_LEVEL > 6)
                    Serial.printf("⚠️ Feature count not provided, assuming %d\n", numFeatures);
                featureCount = numFeatures;
            }
            mcu::packed_vector<2> result;
            result.reserve(numFeatures);
            for (uint16_t i = 0; i < numFeatures; ++i) {
                result.push_back(categorizeFeature(i, features[i]));
            }
            return result;
        }

        // overload for vector input
        template<typename T>
        struct is_supported_vector : std::false_type {};

        template<mcu::index_size_flag flag>
        struct is_supported_vector<mcu::vector<float, flag>> :std::true_type{};
        template<mcu::index_size_flag flag>
        struct is_supported_vector<mcu::vector<int, flag>> :std::true_type{};

        template<mcu::index_size_flag flag, size_t sboSize>
        struct is_supported_vector<mcu::b_vector<float, flag, sboSize>> :std::true_type{};
        template<mcu::index_size_flag flag, size_t sboSize>
        struct is_supported_vector<mcu::b_vector<int, flag, sboSize>> :std::true_type{};

        template<tyename T>
        mcu::packed_vector<2> categorizeFeatures(const T& features) const {
            static_assert(is_supported_vector<T>::value, "Unsupported vector type for categorizeFeatures");
            if (features.size() != numFeatures) {
                Serial.printf("❌ Feature count mismatch: expected %d, got %d\n", numFeatures, features.size());
                return mcu::packed_vector<2>();
            }
            return categorizeFeatures(features.data(), features.size());
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
            usage += filename.length();
            
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

    /*
    ------------------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------ TREE_CONTAINER ------------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------------------
    */

    class Rf_tree_container{
        private:
            String model_name;
            vector<Rf_tree, SMALL> trees;        // b_vector storing root nodes of trees (now manages SPIFFS filenames)
            b_vector<uint16_t> tree_depths;     // store depth of each tree
            b_vector<uint16_t> tree_nodes;     // store number of nodes of each tree
            b_vector<uint16_t> tree_leaves;   // store number of leaves of each tree
            Rf_base* base_ptr;               // Pointer to the base 
            Rf_logger* logger_ptr;          // Pointer to logger 
            b_vector<NodeToBuild> queue_nodes; // Queue for breadth-first tree building
            bool is_loaded = false;

        public:
            Rf_tree_container(){};
            Rf_tree_container(const String& model_name, uint8_t num_trees, uint8_t num_labels, Rf_base& base, Rf_logger& logger){
                init(model_name, num_trees, num_labels, base, logger);
            }

            void init(const String& model_name, uint8_t num_trees, uint8_t num_labels, Rf_base& base, Rf_logger& logger){
                this->model_name = model_name;
                this->base_ptr = &base;
                this->logger_ptr = &logger;
                trees.reserve(num_trees);
                is_loaded = false; // Initially in individual form
            }

            ~Rf_tree_container(){
                for(auto& tree : trees){
                    tree.purgeTree();   // completely remove tree - for development stage 
                }
                trees.clear();
                queue_nodes.clear();
                tree_depths.clear();
                tree_nodes.clear();
                tree_leaves.clear();
            }
            size_t total_nodes(){
                size_t total = 0;
                for(auto& n : tree_nodes) total += n;
                return total;
            }

            size_t total_leaves(){
                size_t total = 0;
                for(auto& n : tree_leaves) total += n;
                return total;
            }

            float avg_depth(){
                float total = 0;
                for(auto& d : tree_depths) total += d;
                return total / tree_depths.size();
            }

            float avg_nodes(){
                float total = 0;
                for(auto& n : tree_nodes) total += n;
                return total / tree_nodes.size();
            }

            float avg_leaves(){
                float total = 0;
                for(auto& n : tree_leaves) total += n;
                return total / tree_leaves.size();
            }
            void add_tree(Rf_tree&& tree){
                tree_depths.push_back(tree.getTreeDepth());
                tree_nodes.push_back(tree.countNodes());
                tree_leaves.push_back(tree.countLeafNodes());
                trees.push_back(std::move(tree));
            }

            Rf_tree& operator[](uint8_t index){
                return trees[index];
            }

            // Get the number of trees
            size_t size() const {
                return trees.size();
            }

            // Check if container is empty
            bool empty() const {
                return trees.empty();
            }

            // Get queue_nodes for tree building
            b_vector<NodeToBuild>& getQueueNodes() {
                return queue_nodes;
            }

            // Clear all trees and reset state
            void clearForest() {
                // Process trees one by one to avoid heap issues
                for (size_t i = 0; i < trees.size(); i++) {
                    trees[i].purgeTree(this->model_name);
                    // Force yield to allow garbage collection
                    yield();        
                    delay(10);
                }
                trees.clear();
                // Mark forest as not loaded to force re-load after rebuilds
                is_loaded = false;
                // Remove old forest file to ensure clean slate
                if(base_ptr) {
                    String oldForestFile = base_ptr->get_unifiedModelFile();
                    if(SPIFFS.exists(oldForestFile.c_str())) {
                        SPIFFS.remove(oldForestFile.c_str());
                        Serial.printf("🗑️ Removed old forest file: %s\n", oldForestFile.c_str());
                    }
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
            iterator end()   { return iterator(this, trees.size()); }

            // Forest loading functionality - moved from RandomForest class
            void loadForest(){
                // Safety check: Don't load if already loaded
                if(is_loaded) {
                    Serial.println("✅ Forest already loaded in memory");
                    return;
                }
                
                // Safety check: Ensure forest exists
                if(trees.empty()) {
                    Serial.println("❌ No trees in forest to load!");
                    return;
                }
                
                // Memory safety check
                size_t freeMemory = ESP.getFreeHeap();
                size_t minMemoryRequired = 20000; // 20KB minimum threshold
                if(freeMemory < minMemoryRequired) {
                    Serial.printf("❌ Insufficient memory to load forest (need %d bytes, have %d)\n", 
                                minMemoryRequired, freeMemory);
                    return;
                }
                
                unsigned long start = GET_CURRENT_TIME_IN_MILLISECONDS;
                
                // Try to load from unified forest file first (using base method for consistency)
                String unifiedFilename = base_ptr ? base_ptr->get_unifiedModelFile() : "";
                
                if(!unifiedFilename.isEmpty() && SPIFFS.exists(unifiedFilename.c_str())){ 
                    // Load from unified file (optimized format)
                    File file = SPIFFS.open(unifiedFilename.c_str(), FILE_READ);
                    if(!file) {
                        Serial.printf("❌ Failed to open unified forest file: %s\n", unifiedFilename.c_str());
                        return;
                    }
                    
                    // Read forest header with error checking
                    uint32_t magic;
                    if(file.read((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                        Serial.println("❌ Failed to read magic number");
                        file.close();
                        return;
                    }
                    
                    if(magic != 0x464F5253) { // "FORS" 
                        Serial.println("❌ Invalid forest file format");
                        file.close();
                        return;
                    }
                    
                    uint8_t treeCount;
                    if(file.read((uint8_t*)&treeCount, sizeof(treeCount)) != sizeof(treeCount)) {
                        Serial.println("❌ Failed to read tree count");
                        file.close();
                        return;
                    }
                    
                    Serial.printf("Loading %d trees from forest file...\n", treeCount);
                    
                    // Read all trees with comprehensive error checking
                    uint8_t successfullyLoaded = 0;
                    for(uint8_t i = 0; i < treeCount; i++) {
                        // Memory check during loading
                        if(ESP.getFreeHeap() < 10000) { // 10KB safety buffer
                            Serial.printf("❌ Insufficient memory during tree loading (have %d bytes)\n", ESP.getFreeHeap());
                            break;
                        }
                        
                        uint8_t treeIndex;
                        if(file.read((uint8_t*)&treeIndex, sizeof(treeIndex)) != sizeof(treeIndex)) {
                            Serial.printf("❌ Failed to read tree index for tree %d\n", i);
                            break;
                        }
                        
                        uint32_t nodeCount;
                        if(file.read((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                            Serial.printf("❌ Failed to read node count for tree %d\n", treeIndex);
                            break;
                        }
                        
                        // Validate node count
                        if(nodeCount == 0 || nodeCount > 2047) {
                            Serial.printf("❌ Invalid node count %d for tree %d\n", nodeCount, treeIndex);
                            // Skip this tree's data
                            file.seek(file.position() + nodeCount * sizeof(uint32_t));
                            continue;
                        }
                        
                        // Find the corresponding tree in trees vector
                        bool treeFound = false;
                        for(auto& tree : trees) {
                            if(tree.index == treeIndex) {
                                // Memory check before loading this tree
                                size_t requiredMemory = nodeCount * sizeof(Tree_node);
                                if(ESP.getFreeHeap() < requiredMemory + 5000) { // 5KB safety buffer per tree
                                    Serial.printf("❌ Insufficient memory to load tree %d (need %d bytes, have %d)\n", 
                                                treeIndex, requiredMemory, ESP.getFreeHeap());
                                    file.close();
                                    return;
                                }
                                
                                tree.nodes.clear();
                                tree.nodes.reserve(nodeCount);
                                
                                // Read all nodes for this tree with error checking
                                bool nodeReadSuccess = true;
                                for(uint32_t j = 0; j < nodeCount; j++) {
                                    Tree_node node;
                                    if(file.read((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                        Serial.printf("❌ Failed to read node %d for tree %d\n", j, treeIndex);
                                        nodeReadSuccess = false;
                                        break;
                                    }
                                    tree.nodes.push_back(node);
                                }
                                
                                if(nodeReadSuccess) {
                                    tree.nodes.fit();
                                    tree.isLoaded = true;
                                    successfullyLoaded++;
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
                            Serial.printf("⚠️ Tree %d not found in forest structure, skipping\n", treeIndex);
                            // Skip the node data for this tree
                            file.seek(file.position() + nodeCount * sizeof(uint32_t));
                        }
                    }
                    
                    file.close();
                    
                    if(successfullyLoaded == 0) {
                        Serial.println("❌ No trees successfully loaded from unified forest file!");
                        return;
                    }
                    
                    Serial.printf("✅ Loaded %d trees from unified format\n", successfullyLoaded);
                    
                } else {
                    // Fallback to individual tree files (legacy format)
                    Serial.println("📁 Unified format not found, using individual tree files...");
                    uint8_t successfullyLoaded = 0;
                    for (auto& tree : trees) {
                        if (!tree.isLoaded) {
                            // Memory check before loading each tree
                            if(ESP.getFreeHeap() < 15000) { // 15KB threshold per tree
                                Serial.printf("❌ Insufficient memory to load more trees (have %d bytes)\n", ESP.getFreeHeap());
                                break;
                            }
                            try {
                                tree.loadTree(this->model_name);
                                if(tree.isLoaded) successfullyLoaded++;
                            } catch (...) {
                                Serial.printf("❌ Exception loading tree %d\n", tree.index);
                                tree.isLoaded = false;
                            }
                        }
                    }
                    
                    if(successfullyLoaded == 0) {
                        Serial.println("❌ No trees successfully loaded from individual files!");
                        return;
                    }
                    
                    Serial.printf("✅ Loaded %d trees from individual format\n", successfullyLoaded);
                }
                
                // Verify trees are actually loaded
                uint8_t loadedTrees = 0;
                uint32_t totalNodes = 0;
                for(const auto& tree : trees) {
                    if(tree.isLoaded && !tree.nodes.empty()) {
                        loadedTrees++;
                        totalNodes += tree.nodes.size();
                    }
                }
                
                if(loadedTrees == 0) {
                    Serial.println("❌ No trees successfully loaded!");
                    is_loaded = false;
                    return;
                }
                
                is_loaded = true;
                unsigned long end = GET_CURRENT_TIME_IN_MILLISECONDS;
                String formatUsed = (base_ptr && SPIFFS.exists(base_ptr->get_unifiedModelFile().c_str())) ? "unified" : "individual";
                Serial.printf("✅ Forest loaded (%s format): %d/%d trees (%d nodes) in %lu ms \n", 
                            formatUsed.c_str(), loadedTrees, trees.size(), totalNodes, end - start);
            }

            // Release forest from RAM into SPIFFS (optimized single-file approach)
            void releaseForest(){
                if(!is_loaded) {
                    Serial.println("✅ Forest is not loaded in memory, nothing to release.");
                    return;
                }
                if(logger_ptr) logger_ptr->m_log("before release forest");
                
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
                    Serial.println("✅ No loaded trees to release");
                    is_loaded = false;
                    return;
                }
                
                // Check available SPIFFS space before writing
                size_t totalFS = SPIFFS.totalBytes();
                size_t usedFS = SPIFFS.usedBytes();
                size_t freeFS = totalFS - usedFS;
                size_t estimatedSize = totalNodes * sizeof(uint32_t) + 100; // nodes + headers
                
                if(freeFS < estimatedSize) {
                    Serial.printf("❌ Insufficient SPIFFS space (need ~%d bytes, have %d)\n", estimatedSize, freeFS);
                    return;
                }
                
                // Single file approach - write all trees to unified forest file
                String unifiedFilename = base_ptr ? base_ptr->get_unifiedModelFile() : "";
                if(unifiedFilename.isEmpty()) {
                    Serial.println("❌ Cannot release forest: no base reference for file management");
                    return;
                }
                
                unsigned long fileStart = GET_CURRENT_TIME_IN_MILLISECONDS;
                File file = SPIFFS.open(unifiedFilename.c_str(), FILE_WRITE);
                if (!file) {
                    Serial.printf("❌ Failed to create unified forest file: %s\n", unifiedFilename.c_str());
                    return;
                }
                
                // Write forest header
                uint32_t magic = 0x464F5253; // "FORS" in hex (forest)
                if(file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) {
                    Serial.println("❌ Failed to write magic number");
                    file.close();
                    SPIFFS.remove(unifiedFilename.c_str());
                    return;
                }
                
                if(file.write((uint8_t*)&loadedCount, sizeof(loadedCount)) != sizeof(loadedCount)) {
                    Serial.println("❌ Failed to write tree count");
                    file.close();
                    SPIFFS.remove(unifiedFilename.c_str());
                    return;
                }
                
                unsigned long writeStart = GET_CURRENT_TIME_IN_MILLISECONDS;
                size_t totalBytes = 0;
                
                // Write all trees in sequence with error checking
                uint8_t savedCount = 0;
                for(auto& tree : trees) {
                    if (tree.isLoaded && tree.index != 255 && !tree.nodes.empty()) {
                        // Write tree header
                        if(file.write((uint8_t*)&tree.index, sizeof(tree.index)) != sizeof(tree.index)) {
                            Serial.printf("❌ Failed to write tree index %d\n", tree.index);
                            break;
                        }
                        
                        uint32_t nodeCount = tree.nodes.size();
                        if(file.write((uint8_t*)&nodeCount, sizeof(nodeCount)) != sizeof(nodeCount)) {
                            Serial.printf("❌ Failed to write node count for tree %d\n", tree.index);
                            break;
                        }
                        
                        // Write all nodes with progress tracking
                        bool writeSuccess = true;
                        for (uint32_t i = 0; i < tree.nodes.size(); i++) {
                            const auto& node = tree.nodes[i];
                            if(file.write((uint8_t*)&node.packed_data, sizeof(node.packed_data)) != sizeof(node.packed_data)) {
                                Serial.printf("❌ Failed to write node %d for tree %d\n", i, tree.index);
                                writeSuccess = false;
                                break;
                            }
                            totalBytes += sizeof(node.packed_data);
                            
                            // Check for memory issues during write
                            if(ESP.getFreeHeap() < 5000) { // 5KB safety threshold
                                Serial.printf("⚠️ Low memory during write (tree %d, node %d)\n", tree.index, i);
                            }
                        }
                        
                        if(!writeSuccess) {
                            Serial.printf("❌ Failed to save tree %d completely\n", tree.index);
                            break;
                        }
                        
                        savedCount++;
                    }
                }
                file.close();
                
                // Verify file was written correctly
                if(savedCount != loadedCount) {
                    Serial.printf("❌ Save incomplete: %d/%d trees saved\n", savedCount, loadedCount);
                    SPIFFS.remove(unifiedFilename.c_str());
                    return;
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
                if(logger_ptr) logger_ptr->m_log("after release forest");
                
                unsigned long end = GET_CURRENT_TIME_IN_MILLISECONDS;
                Serial.printf("✅ Released %d trees to unified format (%d bytes) in %lu ms \n", 
                            clearedCount, totalBytes, end - fileStart);
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

        public:
        Rf_pending_data() {
            buffer.clear();
            actual_labels.clear();
            max_pending_samples = 100; // default max pending samples
            // max_wait_time disabled by default (very large value)
            max_wait_time = 2147483647; // ~24 days
        }

        // add pending sample to buffer, including the label predicted by the model
        void add_pending_sample(const Rf_sample& sample, const char* base_data_file = nullptr, Rf_config* config_ptr = nullptr, const char* infer_log_file = nullptr){
            buffer.push_back(sample);
            if(buffer.size() > max_pending_samples){
                // Auto-flush if parameters are provided
                if(base_data_file && config_ptr && infer_log_file) {
                    flush_pending_data(base_data_file, *config_ptr, infer_log_file);
                } else {
                    // Otherwise just clear buffers to prevent memory overflow
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
        void write_to_base_data(const char* base_data_file, Rf_config& config){
            if(buffer.empty()) return;
            
            // Count valid samples first
            uint16_t valid_samples_count = 0;
            for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < 255) { // Valid actual label provided
                    valid_samples_count++;
                }
            }
            
            if(valid_samples_count == 0) {
                return; // No valid samples to add
            }
            
            if(!SPIFFS.exists(base_data_file)) {
                Serial.printf("❌ Base data file does not exist: %s\n", base_data_file);
                return;
            }
            
            // Read existing file header
            File file = SPIFFS.open(base_data_file, FILE_READ);
            if(!file) {
                Serial.printf("❌ Failed to open base data file for reading: %s\n", base_data_file);
                return;
            }
            
            uint32_t existing_num_samples;
            uint16_t existing_num_features;
            
            if(file.read((uint8_t*)&existing_num_samples, sizeof(existing_num_samples)) != sizeof(existing_num_samples) ||
            file.read((uint8_t*)&existing_num_features, sizeof(existing_num_features)) != sizeof(existing_num_features)) {
                Serial.println("❌ Failed to read existing file header");
                file.close();
                return;
            }
            
            if(existing_num_features != config.num_features) {
                Serial.printf("❌ Feature count mismatch: expected %d, file has %d\n", config.num_features, existing_num_features);
                file.close();
                return;
            }
            
            // Calculate sizes
            const uint16_t packed_feature_bytes = (existing_num_features + 3) / 4; // 4 values per byte
            const size_t record_size = sizeof(uint8_t) + packed_feature_bytes; // label + packed features
            
            if(config.extend_base_data) {
                // Check limits before extending
                uint32_t new_sample_count = existing_num_samples + valid_samples_count;
                size_t header_size = 6; // 4 bytes for num_samples + 2 bytes for num_features
                size_t new_file_size = header_size + (new_sample_count * record_size);
                
                if(new_sample_count > MAX_NUM_SAMPLES) {
                    Serial.printf("⚠️ Warning: Reached maximum of num samples, disabling extension (%u vs %u)\n", 
                                new_sample_count, MAX_NUM_SAMPLES);
                    file.close();
                    config.extend_base_data = false;
                    return;
                }
                
                if(new_file_size > MAX_DATASET_SIZE) {
                    Serial.printf("⚠️ Warning: Reached maximum dataset size, disabling extension (%u vs %u bytes)\n", 
                                new_file_size, MAX_DATASET_SIZE);
                    file.close();
                    config.extend_base_data = false;
                    return;
                }
                
                // Simple case: append new samples to the end
                file.close();
                
                // Reopen in append mode
                file = SPIFFS.open(base_data_file, FILE_APPEND);
                if(!file) {
                    Serial.printf("❌ Failed to open base data file for appending: %s\n", base_data_file);
                    return;
                }
                
                // Write new valid samples
                for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                    if(actual_labels[i] < 255) {
                        // Write label
                        uint8_t label = actual_labels[i];
                        file.write(&label, sizeof(label));
                        
                        // Pack and write features (4 values per byte)
                        uint8_t packed_buffer[packed_feature_bytes];
                        for(uint16_t j = 0; j < packed_feature_bytes; j++) {
                            packed_buffer[j] = 0;
                        }
                        
                        for(size_t j = 0; j < buffer[i].features.size() && j < existing_num_features; j++) {
                            uint16_t byte_index = j / 4;
                            uint8_t bit_offset = (j % 4) * 2;
                            uint8_t feature_value = buffer[i].features[j] & 0x03;
                            packed_buffer[byte_index] |= (feature_value << bit_offset);
                        }
                        
                        file.write(packed_buffer, packed_feature_bytes);

                        config.samples_per_label[label]++; // Update sample count per label
                    }
                }
                
                file.close();
                
                // Update header with new sample count
                file = SPIFFS.open(base_data_file, FILE_WRITE);
                if(file) {
                    uint32_t new_num_samples = existing_num_samples + valid_samples_count;
                    file.write((uint8_t*)&new_num_samples, sizeof(new_num_samples));
                    file.write((uint8_t*)&existing_num_features, sizeof(existing_num_features));
                    file.close();
                    config.num_samples = new_num_samples;
                }
                
            } else {
                // Replace mode: remove old samples from beginning, add new ones at end
                if(valid_samples_count > existing_num_samples) {
                    Serial.println("❌ Cannot remove more samples than exist in dataset");
                    file.close();
                    return;
                }
                
                // Read all existing data (skip the first N samples)
                file.seek(6 + (valid_samples_count * record_size)); // Skip header + N records
                
                uint32_t remaining_samples = existing_num_samples - valid_samples_count;
                size_t remaining_data_size = remaining_samples * record_size;
                
                uint8_t* temp_buffer = (uint8_t*)malloc(remaining_data_size);
                if(!temp_buffer) {
                    Serial.println("❌ Not enough memory for replace operation");
                    file.close();
                    return;
                }
                
                size_t bytes_read = file.read(temp_buffer, remaining_data_size);
                file.close();
                
                if(bytes_read != remaining_data_size) {
                    Serial.println("❌ Failed to read existing data");
                    free(temp_buffer);
                    return;
                }
                
                // Rewrite entire file
                file = SPIFFS.open(base_data_file, FILE_WRITE);
                if(!file) {
                    Serial.printf("❌ Failed to open base data file for writing: %s\n", base_data_file);
                    free(temp_buffer);
                    return;
                }
                
                // Write header (same number of samples)
                uint32_t final_num_samples = existing_num_samples;
                file.write((uint8_t*)&final_num_samples, sizeof(final_num_samples));
                file.write((uint8_t*)&existing_num_features, sizeof(existing_num_features));
                
                // Write remaining old data
                file.write(temp_buffer, remaining_data_size);
                free(temp_buffer);
                
                // Write new valid samples
                for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                    if(actual_labels[i] < 255) {
                        // Write label
                        uint8_t label = actual_labels[i];
                        file.write(&label, sizeof(label));
                        
                        // Pack and write features
                        uint8_t packed_buffer[packed_feature_bytes];
                        for(uint16_t j = 0; j < packed_feature_bytes; j++) {
                            packed_buffer[j] = 0;
                        }
                        
                        for(size_t j = 0; j < buffer[i].features.size() && j < existing_num_features; j++) {
                            uint16_t byte_index = j / 4;
                            uint8_t bit_offset = (j % 4) * 2;
                            uint8_t feature_value = buffer[i].features[j] & 0x03;
                            packed_buffer[byte_index] |= (feature_value << bit_offset);
                        }
                        
                        file.write(packed_buffer, packed_feature_bytes);
                    }
                }
                
                file.close();
            }
        }

        // Write prediction which given actual label (0 < actual_label < 255) to the inference log file
        void write_to_infer_log(const char* infer_log_file, int num_labels){
            if(buffer.empty()) return;
            
            // Check if file exists to determine if header needs to be written
            bool file_exists = SPIFFS.exists(infer_log_file);
            File file = SPIFFS.open(infer_log_file, file_exists ? FILE_APPEND : FILE_WRITE);
            
            if(!file) {
                Serial.printf("❌ Failed to open inference log file: %s\n", infer_log_file);
                return;
            }
            
            // Write header if new file
            if(!file_exists) {
                // Magic number (4 bytes) + num_labels (1 byte) + reserved space for prediction counts per label
                uint32_t magic = 0x494E4652; // "INFR" in hex
                file.write((uint8_t*)&magic, 4);
                file.write((uint8_t*)&num_labels, 1);
                
                // Write initial prediction counts (2 bytes per label - total predictions for each label)
                for(int i = 0; i < num_labels; i++) {
                    uint16_t count = 0;
                    file.write((uint8_t*)&count, 2);
                }
            }
            
            // Collect prediction results for valid samples
            b_vector<uint8_t, SMALL> prediction_bits;
            b_vector<uint8_t, SMALL> label_counts_increment(num_labels, 0);
            
            for(uint16_t i = 0; i < buffer.size() && i < actual_labels.size(); i++) {
                if(actual_labels[i] < 255 && actual_labels[i] < num_labels) {
                    uint8_t predicted_label = buffer[i].label;
                    uint8_t actual_label = actual_labels[i];
                    
                    // Prediction is correct if predicted == actual
                    bool is_correct = (predicted_label == actual_label);
                    prediction_bits.push_back(is_correct ? 1 : 0);
                    label_counts_increment[actual_label]++;
                }
            }
            
            if(!prediction_bits.empty()) {
                // Pack prediction bits into bytes (8 predictions per byte)
                b_vector<uint8_t, SMALL> packed_predictions;
                for(size_t i = 0; i < prediction_bits.size(); i += 8) {
                    uint8_t packed_byte = 0;
                    for(int bit = 0; bit < 8 && (i + bit) < prediction_bits.size(); bit++) {
                        if(prediction_bits[i + bit]) {
                            packed_byte |= (1 << bit);
                        }
                    }
                    packed_predictions.push_back(packed_byte);
                }
                
                // Write packed prediction data
                file.write(packed_predictions.data(), packed_predictions.size());
                
                // Update prediction counts in header
                file.seek(5); // Skip magic (4) + num_labels (1)
                for(int i = 0; i < num_labels; i++) {
                    uint16_t current_count;
                    file.read((uint8_t*)&current_count, 2);
                    file.seek(file.position() - 2); // Go back to overwrite
                    current_count += label_counts_increment[i];
                    file.write((uint8_t*)&current_count, 2);
                }
            }
            
            file.close();
            
            // Trim file if it exceeds max size
            trim_log_file(infer_log_file);
        }

        // trim log file if it exceeds max size (MAX_INFER_LOGFILE_SIZE)
        void trim_log_file(const char* infer_log_file) {
            if(!SPIFFS.exists(infer_log_file)) return;
            
            File file = SPIFFS.open(infer_log_file, FILE_READ);
            if(!file) return;
            
            size_t file_size = file.size();
            file.close();
            
            if(file_size <= MAX_INFER_LOGFILE_SIZE) return;
            
            // File is too large, trim from the beginning (keep most recent data)
            file = SPIFFS.open(infer_log_file, FILE_READ);
            if(!file) return;
            
            // Calculate how much data to keep (keep last MAX_INFER_LOGFILE_SIZE/2 bytes to have room for growth)
            size_t keep_size = MAX_INFER_LOGFILE_SIZE / 2;
            size_t skip_size = file_size - keep_size;
            
            // Read header first to preserve it
            uint32_t magic;
            uint8_t num_labels;
            file.read((uint8_t*)&magic, 4);
            file.read((uint8_t*)&num_labels, 1);
            
            // Read prediction counts
            b_vector<uint16_t, SMALL> prediction_counts(num_labels);
            for(int i = 0; i < num_labels; i++) {
                file.read((uint8_t*)&prediction_counts[i], 2);
            }
            
            size_t header_size = 5 + (num_labels * 2); // magic + num_labels + counts
            
            // Skip to the position we want to keep from
            file.seek(skip_size);
            
            // Read remaining data
            size_t remaining_data_size = file_size - skip_size;
            b_vector<uint8_t, MEDIUM> remaining_data(remaining_data_size);
            file.read(remaining_data.data(), remaining_data_size);
            file.close();
            
            // Rewrite file with header and trimmed data
            file = SPIFFS.open(infer_log_file, FILE_WRITE);
            if(!file) return;
            
            // Write header
            file.write((uint8_t*)&magic, 4);
            file.write((uint8_t*)&num_labels, 1);
            for(int i = 0; i < num_labels; i++) {
                file.write((uint8_t*)&prediction_counts[i], 2);
            }
            
            // Write remaining data
            file.write(remaining_data.data(), remaining_data.size());
            file.close();
        }

        // Public method to flush pending data when buffer is full or on demand
        void flush_pending_data(const char* base_data_file, Rf_config& config, const char* infer_log_file) {
            if(buffer.empty()) return;
            
            write_to_base_data(base_data_file, config);
            write_to_infer_log(infer_log_file, config.num_labels);
            
            // Clear buffers after processing
            buffer.clear();
            actual_labels.clear();
        }

    }


} // namespace mcu