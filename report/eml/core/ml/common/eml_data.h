#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Eml_data {
    private:

    #ifndef RS_PSRAM_AVAILABLE
        static constexpr size_t MAX_CHUNKS_SIZE = 8192; // max bytes per chunk (8kB)
    #else
        static constexpr size_t MAX_CHUNKS_SIZE = 32768; // max bytes per chunk (32kB)
    #endif  

        // Chunked packed storage - eliminates both heap overhead per sample and large contiguous allocations
        vector<packed_vector<8>> sampleChunks;  // Multiple chunks of packed features (up to 8 bits per value)
        packed_vector<8> allLabels;                  // Labels storage 
        uint16_t bitsPerSample;                    // Number of bits per sample (numFeatures * quantization_coefficient)
        rf_sample_type samplesEachChunk;                  // Maximum samples per chunk
        size_t size_;  
        uint8_t quantization_coefficient;              // Bits per feature value (1-8)
        char file_path[RF_PATH_BUFFER] = {0};          // dataset file_path 

        // Pending quantizer update mapping (concept drift): applied on next RAM load.
        Rf_quantizer_update_filter quantizer_update_filter;

        uint8_t num_labels_2_bpv(rf_label_type num_labels) {
            if (num_labels <= 2) return 1;
            else if (num_labels <= 4) return 2;
            else if (num_labels <= 16) return 4;
            else if (num_labels <= 256) return 8;
            else return 16; // up to 65536 labels
        }

    public:
        bool isLoaded;      

        Eml_data() : isLoaded(false), size_(0), bitsPerSample(0), samplesEachChunk(0), quantization_coefficient(2) {}

        Eml_data(const char* path, Rf_config& config){ 
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
            eml_debug_2(1, "ℹ️ Eml_data initialized (", samplesEachChunk, "samples/chunk): ", file_path);
            isLoaded = false;
            size_ = config.num_samples;
            sampleChunks.clear();
            allLabels.clear();
            quantizer_update_filter.clear();
            return isProperlyInitialized();
        }

        Rf_quantizer_update_filter& get_update_filter() { return quantizer_update_filter; }
        const Rf_quantizer_update_filter& get_update_filter() const { return quantizer_update_filter; }

        void clear_update_filter() { quantizer_update_filter.clear(); }

        // Apply a mapping filter to currently loaded (RAM) quantized samples.
        // This is used for immediate remapping after a quantizer update/shrink.
        bool apply_update_filter_inplace(const Rf_quantizer_update_filter& filter) {
            if (!isLoaded) {
                return false;
            }
            const uint16_t numFeatures = bitsPerSample / quantization_coefficient;
            if (!filter.active() || filter.numFeatures() != numFeatures || filter.groupsPerFeature() != (1u << quantization_coefficient)) {
                return false;
            }
            const uint16_t gpf = filter.groupsPerFeature();
            for (size_t ci = 0; ci < sampleChunks.size(); ++ci) {
                packed_vector<8>& chunk = sampleChunks[ci];
                const size_t chunkSize = chunk.size();
                for (size_t ei = 0; ei < chunkSize; ++ei) {
                    const uint16_t fidx = static_cast<uint16_t>(ei % numFeatures);
                    const uint8_t oldVal = static_cast<uint8_t>(chunk[ei]);
                    if (oldVal < gpf) {
                        const uint8_t newVal = filter.map(fidx, oldVal);
                        chunk.set_unsafe(ei, newVal);
                    }
                }
            }
            return true;
        }

        // Iterator class (returns Rf_sample by value for read-only querying)
        class iterator {
        private:
            Eml_data* data_;
            size_t index_;

        public:
            iterator(Eml_data* data, size_t index) : data_(data), index_(index) {}

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

        // Validate that the Eml_data has been properly initialized
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
                eml_debug(2, "❌ Eml_data not loaded. Call loadData() first.");
                return Rf_sample();
            }
            if(sampleIndex >= size_){
                eml_debug_2(2, "❌ Sample index out of bounds: ", sampleIndex, "size: ", size_);
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
                eml_debug(2, "❌ Store sample failed: Eml_data not properly initialized.");
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
                eml_debug(0, "❌ Failed to open CSV file for reading: ", csvfile_path);
                return false;
            }

            if(numFeatures == 0){       
                // Read header line to determine number of features
                String line = file.readStringUntil('\n');
                line.trim();
                if (line.length() == 0) {
                    eml_debug(0, "❌ CSV file is empty or missing header: ", csvfile_path);
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
                    eml_debug_2(0, "❌ Feature count mismatch: expected ", expectedFeatures, ", found ", numFeatures);   
                    file.close();
                    return false;
                }
            }
            
            rf_sample_type linesProcessed = 0;
            rf_sample_type emptyLines = 0;
            rf_sample_type validSamples = 0;
            rf_sample_type invalidSamples = 0;
            
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

                rf_label_type fieldIdx = 0;
                int start = 0;
                while (start < line.length()) {
                    int comma = line.indexOf(',', start);
                    if (comma < 0) comma = line.length();

                    String tok = line.substring(start, comma);
                    rf_label_type v = (rf_label_type)tok.toInt();

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
                    eml_debug_2(2, "❌ Invalid field count in line ", linesProcessed, ": expected ", numFeatures + 1);
                    invalidSamples++;
                    continue;
                }
                
                if (s.features.size() != numFeatures) {
                    eml_debug_2(2, "❌ Feature count mismatch in line ", linesProcessed, ": expected ", numFeatures);
                    invalidSamples++;
                    continue;
                }
                
                s.features.fit();

                // Store in chunked packed format
                storeSample(s, validSamples);
                validSamples++;
                
                if (validSamples >= RF_MAX_SAMPLES) {
                    eml_debug(1, "⚠️ Reached maximum sample limit");
                    break;
                }
            }
            size_ = validSamples;
            
            eml_debug(1, "📋 CSV Processing Results: ");
            eml_debug(1, "   Lines processed: ", linesProcessed);
            eml_debug(1, "   Empty lines: ", emptyLines);
            eml_debug(1, "   Valid samples: ", validSamples);
            eml_debug(1, "   Invalid samples: ", invalidSamples);
            eml_debug(1, "   Total samples in memory: ", size_);
            eml_debug(1, "   Chunks used: ", sampleChunks.size());
            
            allLabels.fit();
            for (auto& chunk : sampleChunks) {
                chunk.fit();
            }
            file.close();
            isLoaded = true;
            RF_FS_REMOVE(csvfile_path);
            eml_debug(1, "✅ CSV data loaded and file removed: ", csvfile_path);
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

        rf_sample_type samplesPerChunk() const {
            return samplesEachChunk;
        }

        size_t size() const {
            return size_;
        }

        void setFilePath(const char* path) {
            strncpy(this->file_path, path, RF_PATH_BUFFER);
            this->file_path[RF_PATH_BUFFER - 1] = '\0';
        }

        void getFilePath(char* buffer) const {
            if (buffer) {
                strncpy(buffer, this->file_path, RF_PATH_BUFFER);
            }
        }

        // Fast accessors for training-time hot paths (avoid reconstructing Rf_sample)
        inline uint16_t num_features() const { return bitsPerSample / quantization_coefficient; }

        inline rf_label_type getLabel(size_t sampleIndex) const {
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
                eml_debug(1, "❌ Cannot reserve space: Eml_data not properly initialized", file_path);
                return;
            }
            allLabels.reserve(numSamples);
            ensureChunkCapacity(numSamples);
            eml_debug_2(2, "📦 Reserved space for", numSamples, "samples, used chunks: ", sampleChunks.size());
        }

        bool convertCSVtoBinary(const char* csvfile_path, uint16_t numFeatures = 0) {
            eml_debug(1, "🔄 Converting CSV to binary format from: ", csvfile_path);
            if(!loadCSVData(csvfile_path, numFeatures)) return false;
            if(!releaseData(false)) return false; 
            eml_debug(1, "✅ CSV converted to binary and saved: ", file_path);
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
                eml_debug(1, "💾 Saving data to file system and clearing from RAM...");
                // Remove any existing file
                if (RF_FS_EXISTS(file_path)) {
                    RF_FS_REMOVE(file_path);
                }
                File file = RF_FS_OPEN(file_path, RF_FILE_WRITE);
                if (!file) {
                    eml_debug(0, "❌ Failed to open binary file for writing: ", file_path);
                    return false;
                }
                eml_debug(2, "📂 Saving data to: ", file_path);

                // Write binary header
                uint32_t numSamples = size_;
                uint16_t numFeatures = bitsPerSample / quantization_coefficient;
                
                file.write((uint8_t*)&numSamples, sizeof(numSamples));
                file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

                // Calculate packed bytes needed for features per sample
                uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
                uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte
                
                // Record size = label (1 byte) + packed features
                uint16_t recordSize = sizeof(rf_label_type) + packedFeatureBytes;
                
                // Use a heap-allocated write buffer to batch multiple samples (512 bytes to save heap)
                static constexpr size_t WRITE_BUFFER_SIZE = 512;
                uint8_t* writeBuffer = (uint8_t*)malloc(WRITE_BUFFER_SIZE);
                if (!writeBuffer) {
                    eml_debug(0, "❌ Failed to allocate write buffer");
                    file.close();
                    return false;
                }
                size_t bufferPos = 0;
                
                // Calculate how many complete samples fit in buffer
                rf_sample_type samplesPerBuffer = WRITE_BUFFER_SIZE / recordSize;
                if (samplesPerBuffer == 0) samplesPerBuffer = 1; // At least one sample per write
                
                for (rf_sample_type i = 0; i < size_; i++) {
                    // Reconstruct sample from chunked packed storage
                    Rf_sample s = getSample(i);
                    
                    // Write label to buffer
                    writeBuffer[bufferPos++] = s.label;
                    
                    // Initialize packed feature area to 0
                    memset(&writeBuffer[bufferPos], 0, packedFeatureBytes);
                    
                    // Pack features into buffer according to quantization_coefficient
                    for (size_t j = 0; j < s.features.size(); ++j) {
                        uint32_t bitPosition = static_cast<uint32_t>(j) * quantization_coefficient;
                        uint16_t byteIndex = bitPosition / 8;
                        uint8_t bitOffset = bitPosition % 8;
                        uint8_t feature_value = s.features[j] & ((1 << quantization_coefficient) - 1);
                        
                        if (bitOffset + quantization_coefficient <= 8) {
                            // Feature fits in single byte
                            writeBuffer[bufferPos + byteIndex] |= (feature_value << bitOffset);
                        } else {
                            // Feature spans two bytes
                            uint8_t bitsInFirstByte = 8 - bitOffset;
                            writeBuffer[bufferPos + byteIndex] |= (feature_value << bitOffset);
                            writeBuffer[bufferPos + byteIndex + 1] |= (feature_value >> bitsInFirstByte);
                        }
                    }
                    bufferPos += packedFeatureBytes;
                    
                    // Flush buffer when full or last sample
                    if (bufferPos + recordSize > WRITE_BUFFER_SIZE || i == size_ - 1) {
                        file.write(writeBuffer, bufferPos);
                        bufferPos = 0;
                    }
                }
                free(writeBuffer);
                file.close();

                // If we wrote the remapped data back to storage, any pending update filter is now obsolete.
                quantizer_update_filter.clear();
            }
            
            // Clear chunked memory
            sampleChunks.clear();
            sampleChunks.fit();
            allLabels.clear();
            allLabels.fit();
            isLoaded = false;
            eml_debug_2(1, "✅ Data saved(", size_, "samples) to: ", file_path); 
            return true;
        }

        // Load data using sequential indices from file system in binary format
        bool loadData(bool re_use = true) {
            if(isLoaded || !isProperlyInitialized()) return false;
            eml_debug(1, "📂 Loading data from: ", file_path);
            
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open data file: ", file_path);
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
                eml_debug(0, "❌ Failed to read data header: ", file_path);
                file.close();
                return false;
            }

            if(numFeatures * quantization_coefficient != bitsPerSample) {
                eml_debug_2(0, "❌ Feature count mismatch: expected ", bitsPerSample / quantization_coefficient, ",found ", numFeatures);
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
            const size_t MAX_BATCH_BYTES = 2048; // conservative for mcu
            uint8_t* ioBuf = mem_alloc::allocate<uint8_t>(MAX_BATCH_BYTES);
            if (!ioBuf) {
                eml_debug(1, "❌ Failed to allocate IO buffer");
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
                            eml_debug(0, "❌ Read batch failed: ", file_path);
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
                                eml_debug_2(0, "❌ Index out of bounds: elemIndex=", elemIndex, ", size=", sampleChunks[chunkIndex].size());
                            }
                            sampleChunks[chunkIndex].set_unsafe(elemIndex, fv);
                        }
                    }
                } else {
                    if (!fallback_yet) {
                        eml_debug(2, "⚠️ IO buffer allocation failed, falling back to per-sample read");
                        fallback_yet = true;
                    }
                    // Fallback: per-sample small buffer
                    batchSamples = 1;
                    uint8_t lbl;
                    if (file.read(&lbl, sizeof(lbl)) != sizeof(lbl)) {
                        eml_debug_2(0, "❌ Read label failed at sample: ", processed, ": ", file_path);
                        if (ioBuf) mem_alloc::deallocate(ioBuf);
                        file.close();
                        return false;
                    }
                    allLabels.push_back(lbl);
                    uint8_t packed[packedFeatureBytes] = {0};
                    if (file.read(packed, packedFeatureBytes) != packedFeatureBytes) {
                        eml_debug_2(0, "❌ Read features failed at sample: ", processed, ": ", file_path);
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

            // Apply mapping if a quantizer update was recorded.
            if (quantizer_update_filter.active() &&
                quantizer_update_filter.numFeatures() == numFeatures &&
                quantizer_update_filter.groupsPerFeature() == (1u << quantization_coefficient)) {
                eml_debug(1, "🔁 Applying quantizer update filter to loaded data");
                (void)apply_update_filter_inplace(quantizer_update_filter);
                // One-shot application completed.
                quantizer_update_filter.clear();
            }
            isLoaded = true;
            file.close();
            if(!re_use) {
                eml_debug(1, "♻️ Single-load mode: removing file after loading: ", file_path);
                RF_FS_REMOVE(file_path); // Remove file after loading in single mode
            }
            eml_debug_2(1, "✅ Data loaded(", sampleChunks.size(), "chunks): ", file_path);
            return true;
        }

        /**
         * @brief Load specific samples from another Eml_data source by sample IDs.
         * @param source The source Eml_data to load samples from.
         * @param sample_IDs A sorted set of sample IDs to load from the source.
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: The state of the source data will be automatically restored, no need to reload.
         */
        bool loadData(Eml_data& source, const sampleID_set& sample_IDs, bool save_ram = true) {
            // Only the source must exist on file system; destination can be an in-memory buffer
            if (!RF_FS_EXISTS(source.file_path)) {
                eml_debug(0, "❌ Source file does not exist: ", source.file_path);
                return false;
            }

            File file = RF_FS_OPEN(source.file_path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open source file: ", source.file_path);
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
                eml_debug(0, "❌ Failed to read source header: ", source.file_path);
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
            
            eml_debug_2(2, "📦 Loading ", numRequestedSamples, "samples from source: ", source.file_path);
            
            size_t addedSamples = 0;
            // Since sample_IDs are sorted in ascending order, we can read efficiently
            for(rf_sample_type sampleIdx : sample_IDs) {
                if(sampleIdx >= numSamples) {
                    eml_debug_2(2, "⚠️ Sample ID ", sampleIdx, "exceeds source sample count ", numSamples);
                    continue;
                }
                
                // Calculate file position for this sample
                size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);
                size_t sampleFilePos = headerSize + (sampleIdx * sampleDataSize);
                
                // Seek to the sample position
                if (!file.seek(sampleFilePos)) {
                    eml_debug_2(2, "⚠️ Failed to seek to sample ", sampleIdx, "position ", sampleFilePos);
                    continue;
                }
                
                Rf_sample s;
                
                // Read label
                if(file.read(&s.label, sizeof(s.label)) != sizeof(s.label)) {
                    eml_debug(2, "⚠️ Failed to read label for sample ", sampleIdx);
                    continue;
                }
                
                // Read packed features
                s.features.clear();
                s.features.reserve(numFeatures);
                
                uint8_t packedBuffer[packedFeatureBytes];
                if(file.read(packedBuffer, packedFeatureBytes) != packedFeatureBytes) {
                    eml_debug(2, "⚠️ Failed to read features for sample ", sampleIdx);
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
            eml_debug_2(2, "✅ Loaded ", addedSamples, "samples from source: ", source.file_path);
            return true;
        }
        
        /**
         * @brief Load a specific chunk of samples from another Eml_data source.
         * @param source The source Eml_data to load samples from.
         * @param chunkIndex The index of the chunk to load (0-based).
         * @param save_ram If true, release source data(if loaded) during process to avoid both datasets in RAM.
         * @note: this function will call loadData(source, chunkIDs) internally.
         */
        bool loadChunk(Eml_data& source, size_t chunkIndex, bool save_ram = true) {
            eml_debug_2(2, "📂 Loading chunk ", chunkIndex, "from source: ", source.file_path);
            if(chunkIndex >= source.total_chunks()) {
                eml_debug_2(2, "❌ Chunk index ", chunkIndex, "out of bounds : total chunks=", source.total_chunks());
                return false; 
            }
            bool pre_loaded = source.isLoaded;

            rf_sample_type startSample = chunkIndex * source.samplesEachChunk;
            rf_sample_type endSample = startSample + source.samplesEachChunk;
            if(endSample > source.size()) {
                endSample = source.size();
            }
            if(startSample >= endSample) {
                eml_debug_2(2, "❌ Invalid chunk range: start ", startSample, ", end ", endSample);
                return false;
            }
            sampleID_set chunkIDs(startSample, endSample - 1);
            chunkIDs.fill();
            loadData(source, chunkIDs, save_ram);   
            return true;
        }

        /**
         *@brief: copy assignment (but not copy file_path to avoid file system over-writing)
         *@note : Eml_data will be put into release state. loadData() to reload into RAM if needed.
        */
        Eml_data& operator=(const Eml_data& other) {
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
                                eml_debug(0, "❌ Failed to clone source file: ", other.file_path);
                            }
                        } else {
                            eml_debug(0, "❌ Source file has invalid header: ", other.file_path);
                        }
                    } else {
                        eml_debug(0, "❌ Cannot open source file: ", other.file_path);
                    }
                } else {
                    eml_debug(0, "❌ Source file does not exist: ", other.file_path);
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
                eml_debug(1, "🗑️ Deleted file: ", file_path);
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
        vector<rf_label_type> addNewData(const vector<Rf_sample>& samples, rf_sample_type max_samples = 0) {
            vector<rf_label_type> deletedLabels;

            if (!isProperlyInitialized()) {
                eml_debug(0, "❌ Eml_data not properly initialized. Cannot add new data.");
                return deletedLabels;
            }
            if (!RF_FS_EXISTS(file_path)) {
                eml_debug(0, "⚠️ File does not exist for adding new data: ", file_path);
                return deletedLabels;
            }
            if (samples.size() == 0) {
                eml_debug(1, "⚠️ No samples to add");
                return deletedLabels;
            }

            // Read current file header to get existing info
            File file = RF_FS_OPEN(file_path, RF_FILE_READ);
            if (!file) {
                eml_debug(0, "❌ Failed to open file for adding new data: ", file_path);
                return deletedLabels;
            }

            uint32_t currentNumSamples;
            uint16_t numFeatures;
            
            if (file.read((uint8_t*)&currentNumSamples, sizeof(currentNumSamples)) != sizeof(currentNumSamples) ||
                file.read((uint8_t*)&numFeatures, sizeof(numFeatures)) != sizeof(numFeatures)) {
                eml_debug(0, "❌ Failed to read file header: ", file_path);
                file.close();
                return deletedLabels;
            }
            file.close();

            // Validate feature count compatibility
            if (samples.size() > 0 && samples[0].features.size() != numFeatures) {
                eml_debug_2(0, "❌ Feature count mismatch: expected ", numFeatures, ", found ", samples[0].features.size());
                return deletedLabels;
            }

            // Calculate packed bytes needed for features
            uint32_t totalBits = static_cast<uint32_t>(numFeatures) * quantization_coefficient;
            uint16_t packedFeatureBytes = (totalBits + 7) / 8; // Round up to nearest byte
            size_t sampleDataSize = sizeof(uint8_t) + packedFeatureBytes; // label + packed features
            size_t headerSize = sizeof(uint32_t) + sizeof(uint16_t);

            uint32_t newNumSamples;
            size_t writePosition;
            
            // Append mode: add to existing samples
            newNumSamples = currentNumSamples + samples.size();
            
            // Apply max_samples limit if specified
            if (max_samples > 0 && newNumSamples > max_samples) {
                eml_debug_2(1, "📊 Applying max_samples limit: ", max_samples, " (current: ", currentNumSamples);
                // Calculate how many oldest samples to remove
                rf_sample_type samples_to_remove = newNumSamples - max_samples;
                newNumSamples = max_samples;
                
                // Read labels of samples that will be removed (oldest samples at the beginning)
                File readFile = RF_FS_OPEN(file_path, RF_FILE_READ);
                if (readFile) {
                    readFile.seek(headerSize); // Skip to data section
                    for (rf_sample_type i = 0; i < samples_to_remove && i < currentNumSamples; i++) {
                        uint8_t label;
                        if (readFile.read(&label, sizeof(label)) == sizeof(label)) {
                            deletedLabels.push_back(label);
                        }
                        // Skip the packed features to get to next sample
                        readFile.seek(readFile.position() + packedFeatureBytes);
                    }
                    readFile.close();
                }
                
                // Shift remaining samples to the beginning (remove oldest)
                // This is done by reading samples after the removed ones and writing them at the beginning
                if (samples_to_remove < currentNumSamples) {
                    rf_sample_type samples_to_keep = currentNumSamples - samples_to_remove;
                    b_vector<uint8_t> temp_buffer;
                    temp_buffer.reserve(sampleDataSize);
                    
                    File shiftFile = RF_FS_OPEN(file_path, "r+");
                    if (shiftFile) {
                        // Read and shift each sample
                        for (rf_sample_type i = 0; i < samples_to_keep; i++) {
                            size_t read_pos = headerSize + (samples_to_remove + i) * sampleDataSize;
                            size_t write_pos = headerSize + i * sampleDataSize;
                            
                            shiftFile.seek(read_pos);
                            temp_buffer.clear();
                            for (size_t b = 0; b < sampleDataSize; b++) {
                                int byte_val = shiftFile.read();
                                if (byte_val < 0) break;
                                temp_buffer.push_back(static_cast<uint8_t>(byte_val));
                            }
                            
                            if (temp_buffer.size() == sampleDataSize) {
                                shiftFile.seek(write_pos);
                                shiftFile.write(temp_buffer.data(), temp_buffer.size());
                            }
                        }
                        shiftFile.close();
                    }
                    
                    currentNumSamples = samples_to_keep;
                    eml_debug_2(1, "♻️  Removed ", samples_to_remove, " oldest samples, kept ", samples_to_keep);
                }
            }
            
            // Check RF_MAX_SAMPLES limit
            if (newNumSamples > RF_MAX_SAMPLES) {
                size_t maxAddable = RF_MAX_SAMPLES - currentNumSamples;
                eml_debug(2, "⚠️ Reaching maximum sample limit, limiting to ", maxAddable);
                newNumSamples = RF_MAX_SAMPLES;
            }
            
            size_t newFileSize = headerSize + (newNumSamples * sampleDataSize);
            const size_t datasetLimit = rf_max_dataset_size();
            if (newFileSize > datasetLimit) {
                size_t maxSamplesBySize = (datasetLimit - headerSize) / sampleDataSize;
                eml_debug(2, "⚠️ Limiting samples by file size to ", maxSamplesBySize);
                newNumSamples = maxSamplesBySize;
            }
            
            writePosition = headerSize + (currentNumSamples * sampleDataSize);

            // Calculate actual number of samples to write
            uint32_t samplesToWrite = (newNumSamples - currentNumSamples);

            eml_debug_2(1, "📝 Adding ", samplesToWrite, "samples to ", file_path);
            eml_debug_2(2, "📊 Dataset info: current=", currentNumSamples, ", new_total=", newNumSamples);

            // Open file for writing (r+ mode to update existing file)
            file = RF_FS_OPEN(file_path, "r+");
            if (!file) {
                eml_debug(0, "❌ Failed to open file for writing: ", file_path);
                return deletedLabels;
            }

            // Update header with new sample count
            file.seek(0);
            file.write((uint8_t*)&newNumSamples, sizeof(newNumSamples));
            file.write((uint8_t*)&numFeatures, sizeof(numFeatures));

            // Seek to write position
            if (!file.seek(writePosition)) {
                eml_debug_2(0, "❌ Failed seek to write position ", writePosition, ": ", file_path);
                file.close();
                return deletedLabels;
            }

            // Write samples directly to file
            uint32_t written = 0;
            for (uint32_t i = 0; i < samplesToWrite && i < samples.size(); ++i) {
                const Rf_sample& sample = samples[i];
                
                // Validate sample feature count
                if (sample.features.size() != numFeatures) {
                    eml_debug_2(2, "⚠️ Skipping sample ", i, " due to feature count mismatch: ", file_path);
                    continue;
                }

                // Write label
                if (file.write(&sample.label, sizeof(sample.label)) != sizeof(sample.label)) {
                    eml_debug_2(0, "❌ Write label failed at sample ", i, ": ", file_path);
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
                    eml_debug_2(0, "❌ Write features failed at sample ", i, ": ", file_path);
                    break;
                }
                
                written++;
            }

            file.close();

            // Update internal size if data is loaded in memory
            if (isLoaded) {
                size_ = newNumSamples;
                eml_debug(1, "ℹ️ Data is loaded in memory. Consider reloading for consistency.");
            }

            eml_debug_2(1, "✅ Successfully wrote ", written, "samples to: ", file_path);
            
            return deletedLabels;
        }

        size_t memory_usage() const {
            size_t total = sizeof(Eml_data);
            total += allLabels.capacity() * sizeof(rf_label_type);
            for (const auto& chunk : sampleChunks) {
                total += sizeof(packed_vector<8>);
                total += chunk.capacity() * sizeof(uint8_t); // stored in bytes regardless of bpv
            }
            return total;
        }
    };

} // namespace eml
