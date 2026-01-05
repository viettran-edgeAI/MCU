#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/core/ml/common/eml_common_defs.h"

namespace eml {

    class Rf_quantizer {
    private:
        uint16_t numFeatures = 0;
        uint16_t groupsPerFeature = 0;
        rf_label_type numLabels = 0;
        uint8_t quantization_coefficient = 2; // Bits per feature value (1-8)
        bool isLoaded = false;
        float outlierZThreshold = 3.0f; // Z-score threshold for outlier detection
        bool removeOutliers = true; // Whether to apply outlier filtering before quantization
        const Rf_base* base_ptr = nullptr;
        
    #ifndef EML_STATIC_MODEL
        // Statistics for outlier filtering (computed during training)
        vector<float> featureMeans;
        vector<float> featureStdDevs;
    #endif

        // Per-feature quantization rules (QTZ4)
        vector<uint8_t> featureTypes;                // FeatureType per feature
        vector<float> featureMins;                   // Per-feature min
        vector<float> featureMaxs;                   // Per-feature max
        vector<int64_t> featureBaselinesScaled;      // Per-feature baseline offsets (scaled)
        vector<uint64_t> featureScales;              // Per-feature scaling factors

        // Concatenated storage for per-feature payloads
        vector<uint16_t> allEdgesScaled;             // Scaled edges for FT_CU
        vector<uint32_t> edgeOffsets;                // Per-feature offset into allEdgesScaled
        vector<uint8_t> edgeCounts;                  // Per-feature number of edges

        vector<float> allDiscreteValuesF;            // Discrete values for FT_DC
        vector<uint32_t> dcOffsets;                  // Per-feature offset into allDiscreteValuesF
        vector<uint8_t> dcCounts;                    // Per-feature number of discrete values
        vector<uint16_t> labelOffsets;               // Offsets into labelStorage for each normalized label
        mutable vector<uint8_t> labelLengths;                // Cached label lengths for faster copy (uint8_t sufficient for max 32 char labels)
        vector<char> labelStorage;                   // Contiguous storage for label strings (null terminated)
            
        bool has_base() const {
            return base_ptr != nullptr && base_ptr->ready_to_use();
        }
        static inline int64_t scaleToInt64(double value, uint64_t scale) {
            long double scaled = static_cast<long double>(value) * static_cast<long double>(scale);
            const long double maxVal = static_cast<long double>(std::numeric_limits<int64_t>::max());
            const long double minVal = static_cast<long double>(std::numeric_limits<int64_t>::min());
            if (scaled >= maxVal) {
                return std::numeric_limits<int64_t>::max();
            }
            if (scaled <= minVal) {
                return std::numeric_limits<int64_t>::min();
            }
            if (scaled >= 0.0L) {
                scaled += 0.5L;
            } else {
                scaled -= 0.5L;
            }
            return static_cast<int64_t>(scaled);
        }

        bool storeLabel(rf_label_type id, const char* label) {
            if (label == nullptr || id >= RF_MAX_LABELS) {
                return false;
            }

            if (id >= labelOffsets.size()) {
                labelOffsets.resize(id + 1, UINT16_MAX);
            }
            if (id >= labelLengths.size()) {
                labelLengths.resize(id + 1, 0);
            }
            
            // Update numLabels to reflect the highest label ID encountered
            if (id >= numLabels) {
                numLabels = id + 1;
            }

            size_t len = strlen(label);
            if (len > 0xFFFF) {
                len = 0xFFFF;
            }

            if (labelStorage.size() + len + 1 > 0xFFFF) {
                eml_debug(0, "❌ Label storage overflow");
                return false;
            }

            uint16_t offset = static_cast<uint16_t>(labelStorage.size());
            labelOffsets[id] = offset;
            labelLengths[id] = static_cast<uint8_t>(len);
            for (size_t i = 0; i < len; ++i) {
                labelStorage.push_back(label[i]);
            }
            labelStorage.push_back('\0');
            return true;
        }
        
        // Optimized feature categorization - hot path, force inline
        static inline uint8_t clampU8(int32_t v, uint8_t lo, uint8_t hi) {
            if (v < static_cast<int32_t>(lo)) return lo;
            if (v > static_cast<int32_t>(hi)) return hi;
            return static_cast<uint8_t>(v);
        }

        // Quantize with drift signal: return value may be <0 or >=groupsPerFeature to indicate out-of-range.
        __attribute__((always_inline)) inline int16_t quantizeValueSignal(uint16_t featureIdx, float value) const {
            // Apply outlier filtering if enabled and statistics are available
#ifndef EML_STATIC_MODEL
            if (removeOutliers && featureIdx < featureMeans.size() && featureIdx < featureStdDevs.size()) {
                const float mean = featureMeans[featureIdx];
                const float stdDev = featureStdDevs[featureIdx];
                
                // Only apply if we have enough standard deviation to avoid division by near-zero
                if (stdDev > 1e-6f) {
                    const float zScore = (value - mean) / stdDev;
                    
                    if (zScore > this->outlierZThreshold) {
                        value = mean + outlierZThreshold * stdDev; // Upper bound
                    } else if (zScore < -outlierZThreshold) {
                        value = mean - outlierZThreshold* stdDev; // Lower bound
                    }
                }
            }
#endif

            const uint8_t type = (featureIdx < featureTypes.size()) ? featureTypes[featureIdx] : static_cast<uint8_t>(FT_DF);
            const float fmin = (featureIdx < featureMins.size()) ? featureMins[featureIdx] : 0.0f;
            const float fmax = (featureIdx < featureMaxs.size()) ? featureMaxs[featureIdx] : 0.0f;

            const bool under = value < fmin;
            const bool over = value > fmax;
            
            // FT_DF is most common, check first
            if (__builtin_expect(type == FT_DF, 1)) {
                // Full discrete range: clamp to 0..groupsPerFeature-1
                int intValue = static_cast<int>(value);
                const int32_t clamped = (intValue < 0) ? 0 : ((intValue >= groupsPerFeature) ? (groupsPerFeature - 1) : intValue);
                // DF drift (rare): signal if outside min/max (should be 0..gpf-1)
                if (under) return -1;
                if (over) return static_cast<int16_t>(groupsPerFeature);
                return static_cast<int16_t>(clamped);
            }

            if (type == FT_CU) {
                const int64_t baselineScaled = (featureIdx < featureBaselinesScaled.size()) ? featureBaselinesScaled[featureIdx] : 0;
                const uint64_t scale = (featureIdx < featureScales.size()) ? featureScales[featureIdx] : 1ULL;
                int64_t adjusted = scaleToInt64(static_cast<double>(value), scale) - baselineScaled;
                if (adjusted <= 0) {
                    // underflow signal
                    return under ? static_cast<int16_t>(-1) : 0;
                }
                uint32_t scaledValue = static_cast<uint32_t>(adjusted <= std::numeric_limits<uint32_t>::max() ? adjusted : std::numeric_limits<uint32_t>::max());
                const uint32_t off = (featureIdx < edgeOffsets.size()) ? edgeOffsets[featureIdx] : 0;
                const uint8_t cnt = (featureIdx < edgeCounts.size()) ? edgeCounts[featureIdx] : 0;
                const uint16_t* edges = (off < allEdgesScaled.size()) ? &allEdgesScaled[off] : nullptr;

                for (uint8_t bin = 0; bin < cnt; ++bin) {
                    if (edges && scaledValue < edges[bin]) {
                        return static_cast<int16_t>(bin);
                    }
                }
                const int16_t inRangeBin = static_cast<int16_t>(cnt);
                // If value exceeds (min,max), return a larger drift code instead of rounding/clamping.
                if (over) {
                    float span = fmax - fmin;
                    float bw = (groupsPerFeature > 0) ? (span / static_cast<float>(groupsPerFeature)) : 0.0f;
                    if (bw > 1e-9f) {
                        int16_t extra = static_cast<int16_t>(std::floor((value - fmax) / bw) + 1.0f);
                        return static_cast<int16_t>(groupsPerFeature - 1) + extra;
                    }
                    return static_cast<int16_t>(groupsPerFeature);
                }
                if (under) {
                    float span = fmax - fmin;
                    float bw = (groupsPerFeature > 0) ? (span / static_cast<float>(groupsPerFeature)) : 0.0f;
                    if (bw > 1e-9f) {
                        int16_t extra = static_cast<int16_t>(std::floor((fmin - value) / bw) + 1.0f);
                        return static_cast<int16_t>(-extra);
                    }
                    return static_cast<int16_t>(-1);
                }
                return inRangeBin;
            }

            // FT_DC: Discrete custom values
            const uint32_t off = (featureIdx < dcOffsets.size()) ? dcOffsets[featureIdx] : 0;
            const uint8_t cnt = (featureIdx < dcCounts.size()) ? dcCounts[featureIdx] : 0;
            if (cnt == 0 || off >= allDiscreteValuesF.size()) {
                return under ? static_cast<int16_t>(-1) : (over ? static_cast<int16_t>(groupsPerFeature) : 0);
            }
            const float* vals = &allDiscreteValuesF[off];

            // Exact/near match first
            for (uint8_t i = 0; i < cnt; ++i) {
                if (std::fabs(vals[i] - value) <= 1e-6f) {
                    return static_cast<int16_t>(i);
                }
            }
            // Drift in discrete: unknown category => drift signal
            if (under) return static_cast<int16_t>(-1);
            if (over) return static_cast<int16_t>(groupsPerFeature);
            return static_cast<int16_t>(groupsPerFeature);
        }
      
    public:
        Rf_quantizer() = default;
        
        Rf_quantizer(Rf_base* base, Rf_config& config) {
            init(base, config);
        }
        ~Rf_quantizer() {
            base_ptr = nullptr;
            isLoaded = false;
            featureTypes.clear();
            featureMins.clear();
            featureMaxs.clear();
            featureBaselinesScaled.clear();
            featureScales.clear();
            allEdgesScaled.clear();
            edgeOffsets.clear();
            edgeCounts.clear();
            allDiscreteValuesF.clear();
            dcOffsets.clear();
            dcCounts.clear();
            labelOffsets.clear();
            labelLengths.clear();
            labelStorage.clear();
        }

        uint8_t getQuantizationCoefficient() const {
            return quantization_coefficient;
        }

        rf_label_type getNumLabels() const {
            return numLabels;
        }

        void init(Rf_base* base, Rf_config& config) {
            base_ptr = base;
            isLoaded = false;

            uint16_t num_features = config.num_features;
            uint8_t num_labels = config.num_labels;

            // Reserve memory for member vectors
            featureTypes.reserve(num_features);
            featureMins.reserve(num_features);
            featureMaxs.reserve(num_features);
            featureBaselinesScaled.reserve(num_features);
            featureScales.reserve(num_features);
            edgeOffsets.reserve(num_features);
            edgeCounts.reserve(num_features);
            dcOffsets.reserve(num_features);
            dcCounts.reserve(num_features);
            labelOffsets.reserve(num_labels);
            labelStorage.reserve(num_labels * 8); // Heuristic: 8 chars per label

#ifndef EML_STATIC_MODEL
            featureMeans.reserve(num_features);
            featureStdDevs.reserve(num_features);
#endif
            // Initialize labelLengths vector
            labelLengths.reserve(num_labels);
        }        // Load quantizer data from binary format
        
        bool loadQuantizer() {
            if (isLoaded) {
                return true;
            }
            if (!has_base()) {
                eml_debug(0, "❌ Load Quantizer failed: data pointer not ready");
                return false;
            }

            char file_path[RF_PATH_BUFFER];
            base_ptr->get_qtz_path(file_path);
            if (!RF_FS_EXISTS(file_path)) {
                eml_debug(0, "❌ Quantizer binary file not found: ", file_path);
                return false;
            }

            File file = RF_FS_OPEN(file_path, "r");
            if (!file) {
                eml_debug(0, "❌ Failed to open Quantizer binary file: ", file_path);
                return false;
            }

            auto resetData = [&]() {
                numFeatures = 0;
                groupsPerFeature = 0;
                numLabels = 0;
                quantization_coefficient = 2;
                removeOutliers = true;
                isLoaded = false;
                featureTypes.clear();
                featureMins.clear();
                featureMaxs.clear();
                featureBaselinesScaled.clear();
                featureScales.clear();
                allEdgesScaled.clear();
                edgeOffsets.clear();
                edgeCounts.clear();
                allDiscreteValuesF.clear();
                dcOffsets.clear();
                dcCounts.clear();
                labelOffsets.clear();
                labelLengths.clear();
                labelStorage.clear();
#ifndef EML_STATIC_MODEL
                featureMeans.clear();
                featureStdDevs.clear();
#endif
            };

            resetData();

            // Read magic number
            char magic[4];
            if (file.readBytes(magic, 4) != 4 || memcmp(magic, "QTZ4", 4) != 0) {
                eml_debug(0, "❌ Invalid quantizer binary magic number");
                file.close();
                return false;
            }

            // Read basic parameters
            if (file.readBytes(reinterpret_cast<char*>(&numFeatures), sizeof(uint16_t)) != sizeof(uint16_t)) {
                eml_debug(0, "❌ Failed to read numFeatures");
                file.close();
                resetData();
                return false;
            }

            if (file.readBytes(reinterpret_cast<char*>(&groupsPerFeature), sizeof(uint16_t)) != sizeof(uint16_t)) {
                eml_debug(0, "❌ Failed to read groupsPerFeature");
                file.close();
                resetData();
                return false;
            }

            uint8_t labelCount;
            if (file.readBytes(reinterpret_cast<char*>(&labelCount), sizeof(uint8_t)) != sizeof(uint8_t)) {
                eml_debug(0, "❌ Failed to read label count");
                file.close();
                resetData();
                return false;
            }
            // numLabels will be updated by storeLabel calls
            numLabels = 0; 

            // Read outlier filtering flag
            uint8_t outlierFlag;
            if (file.readBytes(reinterpret_cast<char*>(&outlierFlag), sizeof(uint8_t)) != sizeof(uint8_t)) {
                eml_debug(0, "❌ Failed to read outlier flag");
                file.close();
                resetData();
                return false;
            }
            removeOutliers = (outlierFlag != 0);

            // Read outlier statistics if enabled
            if (removeOutliers) {
#ifndef EML_STATIC_MODEL
                featureMeans.reserve(numFeatures);
                featureStdDevs.reserve(numFeatures);
#endif
                for (uint16_t i = 0; i < numFeatures; ++i) {
                    float mean, stdDev;
                    if (file.readBytes(reinterpret_cast<char*>(&mean), sizeof(float)) != sizeof(float) ||
                        file.readBytes(reinterpret_cast<char*>(&stdDev), sizeof(float)) != sizeof(float)) {
                        eml_debug(0, "❌ Failed to read outlier statistics");
                        file.close();
                        resetData();
                        return false;
                    }
#ifndef EML_STATIC_MODEL
                    featureMeans.push_back(mean);
                    featureStdDevs.push_back(stdDev);
#endif
                }
            }

            // Calculate quantization_coefficient from groupsPerFeature
            if (groupsPerFeature == 0) {
                eml_debug(0, "❌ Invalid groupsPerFeature value");
                file.close();
                resetData();
                return false;
            }
            
            quantization_coefficient = 0;
            uint16_t temp = groupsPerFeature;
            while (temp > 1) {
                temp >>= 1;
                quantization_coefficient++;
            }
            if (quantization_coefficient < 1) quantization_coefficient = 1;
            if (quantization_coefficient > 8) quantization_coefficient = 8;

            // Reserve memory
            featureTypes.reserve(numFeatures);
            featureMins.reserve(numFeatures);
            featureMaxs.reserve(numFeatures);
            featureBaselinesScaled.reserve(numFeatures);
            featureScales.reserve(numFeatures);
            edgeOffsets.reserve(numFeatures);
            edgeCounts.reserve(numFeatures);
            dcOffsets.reserve(numFeatures);
            dcCounts.reserve(numFeatures);
            labelOffsets.resize(labelCount, UINT16_MAX);
            labelLengths.resize(labelCount, 0);
            labelStorage.reserve(labelCount * 8);

            // Read label mappings
            for (uint8_t i = 0; i < labelCount; ++i) {
                uint8_t labelId;
                if (file.readBytes(reinterpret_cast<char*>(&labelId), sizeof(uint8_t)) != sizeof(uint8_t)) {
                    eml_debug(0, "❌ Failed to read label ID");
                    file.close();
                    resetData();
                    return false;
                }

                uint8_t labelLen;
                if (file.readBytes(reinterpret_cast<char*>(&labelLen), sizeof(uint8_t)) != sizeof(uint8_t)) {
                    eml_debug(0, "❌ Failed to read label length");
                    file.close();
                    resetData();
                    return false;
                }

                char labelBuffer[256];
                if (labelLen > 0) {
                    if (file.readBytes(labelBuffer, labelLen) != labelLen) {
                        eml_debug(0, "❌ Failed to read label text");
                        file.close();
                        resetData();
                        return false;
                    }
                    labelBuffer[labelLen] = '\0';
                    if (!storeLabel(labelId, labelBuffer)) {
                        eml_debug(0, "❌ Failed to store label");
                        file.close();
                        resetData();
                        return false;
                    }
                }
            }

            // Read feature definitions
            for (uint16_t i = 0; i < numFeatures; ++i) {
                uint8_t typeU8;
                if (file.readBytes(reinterpret_cast<char*>(&typeU8), sizeof(uint8_t)) != sizeof(uint8_t)) {
                    eml_debug(0, "❌ Failed to read feature type");
                    file.close();
                    resetData();
                    return false;
                }

                float minValue;
                float maxValue;
                if (file.readBytes(reinterpret_cast<char*>(&minValue), sizeof(float)) != sizeof(float) ||
                    file.readBytes(reinterpret_cast<char*>(&maxValue), sizeof(float)) != sizeof(float)) {
                    eml_debug(0, "❌ Failed to read feature min/max");
                    file.close();
                    resetData();
                    return false;
                }

                int64_t baselineValueScaled;
                if (file.readBytes(reinterpret_cast<char*>(&baselineValueScaled), sizeof(int64_t)) != sizeof(int64_t)) {
                    eml_debug(0, "❌ Failed to read baseline");
                    file.close();
                    resetData();
                    return false;
                }

                uint64_t scaleValue;
                if (file.readBytes(reinterpret_cast<char*>(&scaleValue), sizeof(uint64_t)) != sizeof(uint64_t)) {
                    eml_debug(0, "❌ Failed to read scale");
                    file.close();
                    resetData();
                    return false;
                }
                if (scaleValue == 0) scaleValue = 1;

                FeatureType type = static_cast<FeatureType>(typeU8);

                featureTypes.push_back(typeU8);
                featureMins.push_back(minValue);
                featureMaxs.push_back(maxValue);
                featureBaselinesScaled.push_back(baselineValueScaled);
                featureScales.push_back(scaleValue);

                switch (type) {
                    case FT_DF:
                        edgeOffsets.push_back(0);
                        edgeCounts.push_back(0);
                        dcOffsets.push_back(0);
                        dcCounts.push_back(0);
                        break;

                    case FT_DC:
                        {
                            uint8_t count;
                            if (file.readBytes(reinterpret_cast<char*>(&count), sizeof(uint8_t)) != sizeof(uint8_t)) {
                                eml_debug(0, "❌ Failed to read DC count");
                                file.close();
                                resetData();
                                return false;
                            }

                            uint32_t offset = static_cast<uint32_t>(allDiscreteValuesF.size());
                            for (uint8_t j = 0; j < count; ++j) {
                                float val;
                                if (file.readBytes(reinterpret_cast<char*>(&val), sizeof(float)) != sizeof(float)) {
                                    eml_debug(0, "❌ Failed to read DC value");
                                    file.close();
                                    resetData();
                                    return false;
                                }
                                allDiscreteValuesF.push_back(val);
                            }
                            dcOffsets.push_back(offset);
                            dcCounts.push_back(count);
                            edgeOffsets.push_back(0);
                            edgeCounts.push_back(0);
                        }
                        break;

                    case FT_CU:
                        {
                            uint8_t edgeCount;
                            if (file.readBytes(reinterpret_cast<char*>(&edgeCount), sizeof(uint8_t)) != sizeof(uint8_t)) {
                                eml_debug(0, "❌ Failed to read CU edge count");
                                file.close();
                                resetData();
                                return false;
                            }

                            uint32_t offset = static_cast<uint32_t>(allEdgesScaled.size());
                            for (uint8_t j = 0; j < edgeCount; ++j) {
                                uint16_t edge;
                                if (file.readBytes(reinterpret_cast<char*>(&edge), sizeof(uint16_t)) != sizeof(uint16_t)) {
                                    eml_debug(0, "❌ Failed to read CU edge");
                                    file.close();
                                    resetData();
                                    return false;
                                }
                                allEdgesScaled.push_back(edge);
                            }

                            edgeOffsets.push_back(offset);
                            edgeCounts.push_back(edgeCount);
                            dcOffsets.push_back(0);
                            dcCounts.push_back(0);
                        }
                        break;

                    default:
                        eml_debug(0, "❌ Unknown feature type");
                        file.close();
                        resetData();
                        return false;
                }

            }

            file.close();
            isLoaded = true;
            eml_debug(1, "✅ Quantizer binary loaded successfully! : ", file_path);
            eml_debug_2(2, "📊 Features: ", numFeatures, ", Groups: ", groupsPerFeature);
            eml_debug_2(2, "   Labels: ", numLabels, ", Outlier filtering: ", removeOutliers ? "enabled" : "disabled");
            return true;
        }

        // Release loaded data from memory
        void releaseQuantizer(bool re_use = true) {
            if (!isLoaded) {
                return;
            }
            
            // Clear all data structures
            featureTypes.clear();
            featureMins.clear();
            featureMaxs.clear();
            featureBaselinesScaled.clear();
            featureScales.clear();
            allEdgesScaled.clear();
            edgeOffsets.clear();
            edgeCounts.clear();
            allDiscreteValuesF.clear();
            dcOffsets.clear();
            dcCounts.clear();
            labelOffsets.clear();
            labelLengths.clear();
            labelStorage.clear();

            featureTypes.fit();
            featureMins.fit();
            featureMaxs.fit();
            featureBaselinesScaled.fit();
            featureScales.fit();
            allEdgesScaled.fit();
            edgeOffsets.fit();
            edgeCounts.fit();
            allDiscreteValuesF.fit();
            dcOffsets.fit();
            dcCounts.fit();
            labelOffsets.fit();
            labelLengths.fit();
            labelStorage.fit();
            
#ifndef EML_STATIC_MODEL
            featureMeans.clear();
            featureStdDevs.clear();
            featureMeans.fit();
            featureStdDevs.fit();
#endif
            isLoaded = false;

            eml_debug(2, "🧹 Quantizer data released from memory");
        }

        // Core categorization function: write directly to pre-allocated buffer.
        // Drift signaling: values outside (min,max) return <0 or >=groupsPerFeature from quantizeValueSignal().
        // For inference, we still clamp stored bins to [0..groupsPerFeature-1].
        __attribute__((always_inline)) inline bool quantizeFeatures(
            const float* features,
            packed_vector<8>& output,
            uint16_t* drift_feature = nullptr,
            float* drift_value = nullptr
        ) const {
            bool drift = false;
            const uint16_t gpf = groupsPerFeature;
            const uint8_t hi = (gpf > 0) ? static_cast<uint8_t>(gpf - 1) : 0;
            for (uint16_t i = 0; i < numFeatures; ++i) {
                const float v = features[i];
                const int16_t q = quantizeValueSignal(i, v);
                if (!drift && (q < 0 || q >= static_cast<int16_t>(gpf))) {
                    drift = true;
                    if (drift_feature) {
                        *drift_feature = i;
                    }
                    if (drift_value) {
                        *drift_value = v;
                    }
                }
                const uint8_t stored = (gpf == 0) ? 0 : clampU8(static_cast<int32_t>(q), 0, hi);
                output.set(i, stored);
            }
            return drift;
        }

        // Backward-compatible overload (no drift info requested)
        __attribute__((always_inline)) inline void quantizeFeatures(const float* features, packed_vector<8>& output) const {
            (void)quantizeFeatures(features, output, nullptr, nullptr);
        }

        // Expand quantizer ranges based on recorded drift samples, update continuous edges (bins widen),
        // and create a mapping filter oldBin->newBin for each feature.
        // The filter should be applied to existing quantized data the next time Eml_data is loaded into RAM.
        bool apply_concept_drift_update(const vector<Rf_drift_sample>& drift_samples, Rf_quantizer_update_filter& out_filter) {
            if (!isLoaded || numFeatures == 0 || groupsPerFeature == 0) {
                return false;
            }
            if (drift_samples.empty()) {
                return false;
            }

            out_filter.init(numFeatures, groupsPerFeature);

            // Compute new min/max per feature.
            vector<float> newMins = featureMins;
            vector<float> newMaxs = featureMaxs;
            for (const auto& ds : drift_samples) {
                if (ds.feature_index >= numFeatures) {
                    continue;
                }
                float& mn = newMins[ds.feature_index];
                float& mx = newMaxs[ds.feature_index];
                if (ds.value < mn) mn = ds.value;
                if (ds.value > mx) mx = ds.value;
            }

            const uint16_t gpf = groupsPerFeature;
            const uint16_t bins = gpf;

            for (uint16_t f = 0; f < numFeatures; ++f) {
                const float oldMin = featureMins[f];
                const float oldMax = featureMaxs[f];
                const float nm = newMins[f];
                const float nx = newMaxs[f];
                const bool changed = (nm < oldMin) || (nx > oldMax);
                if (!changed) {
                    continue;
                }

                const uint8_t ftype = featureTypes[f];
                if (ftype != static_cast<uint8_t>(FT_CU)) {
                    // For non-continuous features, keep mapping identity but still expand stored min/max.
                    featureMins[f] = nm;
                    featureMaxs[f] = nx;
                    continue;
                }

                const uint32_t off = edgeOffsets[f];
                const uint8_t ecount = edgeCounts[f];
                if (off + ecount > allEdgesScaled.size()) {
                    continue;
                }
                const uint64_t oldScale = featureScales[f] ? featureScales[f] : 1ULL;

                // Old edge positions in float.
                vector<float> oldEdges;
                oldEdges.reserve(ecount);
                for (uint8_t i = 0; i < ecount; ++i) {
                    oldEdges.push_back(oldMin + static_cast<float>(allEdgesScaled[off + i]) / static_cast<float>(oldScale));
                }

                // New edge positions keep the same fractional positions within range (bins only widen when range expands).
                vector<float> newEdges;
                newEdges.reserve(ecount);
                const float oldRange = oldMax - oldMin;
                const float newRange = nx - nm;
                if (oldRange > 1e-9f && newRange > 1e-9f) {
                    for (uint8_t i = 0; i < ecount; ++i) {
                        float frac = (oldEdges[i] - oldMin) / oldRange;
                        if (frac < 0.0f) frac = 0.0f;
                        if (frac > 1.0f) frac = 1.0f;
                        newEdges.push_back(nm + frac * newRange);
                    }
                } else if (newRange > 1e-9f && bins > 1) {
                    // Degenerate old range, fall back to uniform edges.
                    for (uint8_t i = 0; i < ecount; ++i) {
                        float frac = static_cast<float>(i + 1) / static_cast<float>(bins);
                        newEdges.push_back(nm + frac * newRange);
                    }
                } else {
                    // Still degenerate, keep edges at min.
                    for (uint8_t i = 0; i < ecount; ++i) {
                        newEdges.push_back(nm);
                    }
                }

                // Build mapping oldBin -> newBin based on >50% overlap.
                auto bin_bounds = [&](const float mn, const float mx, const vector<float>& edges, uint16_t bin, float& lo, float& hi) {
                    if (bin == 0) {
                        lo = mn;
                        hi = edges.empty() ? mx : edges[0];
                        return;
                    }
                    const uint16_t last = static_cast<uint16_t>(edges.size());
                    if (bin >= last) {
                        lo = edges.empty() ? mn : edges[last - 1];
                        hi = mx;
                        return;
                    }
                    lo = edges[bin - 1];
                    hi = edges[bin];
                };

                for (uint16_t oldBin = 0; oldBin < bins; ++oldBin) {
                    float oLo, oHi;
                    bin_bounds(oldMin, oldMax, oldEdges, oldBin, oLo, oHi);
                    float oWidth = oHi - oLo;
                    if (oWidth <= 0.0f) {
                        // Degenerate old bin: map via midpoint.
                        float mid = oLo;
                        uint8_t bestNew = 0;
                        for (uint16_t newBin = 0; newBin < bins; ++newBin) {
                            float nLo, nHi;
                            bin_bounds(nm, nx, newEdges, newBin, nLo, nHi);
                            if (mid >= nLo && mid <= nHi) {
                                bestNew = static_cast<uint8_t>(newBin);
                                break;
                            }
                        }
                        out_filter.setMapping(f, static_cast<uint8_t>(oldBin), bestNew);
                        continue;
                    }

                    float bestOverlap = -1.0f;
                    uint8_t bestNew = 0;
                    for (uint16_t newBin = 0; newBin < bins; ++newBin) {
                        float nLo, nHi;
                        bin_bounds(nm, nx, newEdges, newBin, nLo, nHi);
                        float overlap = std::min(oHi, nHi) - std::max(oLo, nLo);
                        if (overlap < 0.0f) overlap = 0.0f;
                        if (overlap > bestOverlap) {
                            bestOverlap = overlap;
                            bestNew = static_cast<uint8_t>(newBin);
                        }
                    }

                    if (bestOverlap / oWidth <= 0.5f) {
                        // Spec says only map when overlap > 50%.
                        // If not, still map to the best-overlap bin to keep values stable.
                    }
                    out_filter.setMapping(f, static_cast<uint8_t>(oldBin), bestNew);
                }

                // Update stored feature range & scaled edges.
                featureMins[f] = nm;
                featureMaxs[f] = nx;

                uint64_t newScale = 1ULL;
                if (newRange > 1e-9f) {
                    long double rawScale = static_cast<long double>(std::numeric_limits<uint16_t>::max()) / static_cast<long double>(newRange);
                    if (rawScale < 1.0L) rawScale = 1.0L;
                    if (rawScale > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
                        rawScale = static_cast<long double>(std::numeric_limits<uint64_t>::max());
                    }
                    newScale = static_cast<uint64_t>(rawScale);
                    if (newScale == 0) newScale = 1ULL;
                }
                featureScales[f] = newScale;
                featureBaselinesScaled[f] = scaleToInt64(static_cast<double>(nm), newScale);

                for (uint8_t i = 0; i < ecount; ++i) {
                    double diff = static_cast<double>(newEdges[i]) - static_cast<double>(nm);
                    if (diff < 0.0) diff = 0.0;
                    long double scaled = static_cast<long double>(diff) * static_cast<long double>(newScale);
                    if (scaled < 0.0L) scaled = 0.0L;
                    if (scaled > static_cast<long double>(std::numeric_limits<uint16_t>::max())) {
                        scaled = static_cast<long double>(std::numeric_limits<uint16_t>::max());
                    }
                    allEdgesScaled[off + i] = static_cast<uint16_t>(scaled + 0.5L);
                }
            }

            return true;
        }

        // Shrink continuous feature ranges if edge bins are unused in the currently loaded dataset.
        // This is intended for FIFO datasets where older samples are discarded and extreme bins may become empty.
        // Policy: shrink at most 2 bins at the low end and/or 2 bins at the high end, only if ALL samples miss them.
        // Emits an update filter (oldBin -> newBin) and can be applied in-place to the loaded data.
        bool apply_fifo_bin_shrink(Eml_data& loaded_train_data, Rf_quantizer_update_filter& out_filter, uint8_t maxBinsToShrink = 2) {
            if (!isLoaded || numFeatures == 0 || groupsPerFeature == 0) {
                return false;
            }
            if (!loaded_train_data.isLoaded || loaded_train_data.size() == 0) {
                return false;
            }
            const uint16_t dataFeatures = loaded_train_data.total_features();
            if (dataFeatures != numFeatures) {
                return false;
            }

            const uint16_t gpf = groupsPerFeature;
            const uint8_t ecount_expected = (gpf > 0) ? static_cast<uint8_t>(gpf - 1) : 0;
            bool changed_any = false;

            Rf_quantizer_update_filter tempFilter;
            tempFilter.init(numFeatures, gpf);

            vector<uint32_t> counts;
            counts.resize(gpf, 0);

            for (uint16_t f = 0; f < numFeatures; ++f) {
                const uint8_t ftype = (f < featureTypes.size()) ? featureTypes[f] : static_cast<uint8_t>(FT_DF);
                if (ftype != static_cast<uint8_t>(FT_CU)) {
                    continue;
                }

                // Histogram current quantized bins for this feature.
                std::fill(counts.begin(), counts.end(), 0);
                const rf_sample_type n = loaded_train_data.size();
                for (rf_sample_type si = 0; si < n; ++si) {
                    const uint16_t v = loaded_train_data.getFeature(si, f);
                    if (v < gpf) {
                        counts[v]++;
                    }
                }

                uint8_t lowShift = 0;
                while (lowShift < maxBinsToShrink && lowShift < gpf && counts[lowShift] == 0) {
                    lowShift++;
                }
                uint8_t highDrop = 0;
                while (highDrop < maxBinsToShrink && highDrop < gpf && counts[gpf - 1 - highDrop] == 0) {
                    highDrop++;
                }

                // Avoid degeneracy: must leave at least one non-collapsed bin.
                if (lowShift == 0 && highDrop == 0) {
                    continue;
                }
                if (static_cast<uint16_t>(lowShift) + static_cast<uint16_t>(highDrop) >= gpf) {
                    continue;
                }

                const float oldMin = featureMins[f];
                const float oldMax = featureMaxs[f];
                const uint32_t off = edgeOffsets[f];
                const uint8_t ecount = edgeCounts[f];
                if (ecount != ecount_expected || off + ecount > allEdgesScaled.size()) {
                    continue;
                }
                const uint64_t oldScale = featureScales[f] ? featureScales[f] : 1ULL;

                // Decode old edges to absolute float positions.
                vector<float> oldEdges;
                oldEdges.reserve(ecount);
                for (uint8_t i = 0; i < ecount; ++i) {
                    oldEdges.push_back(oldMin + static_cast<float>(allEdgesScaled[off + i]) / static_cast<float>(oldScale));
                }

                float newMin = oldMin;
                float newMax = oldMax;

                if (lowShift > 0) {
                    const uint8_t edgeIdx = static_cast<uint8_t>(lowShift - 1);
                    if (edgeIdx < oldEdges.size()) {
                        newMin = oldEdges[edgeIdx];
                    }
                }
                if (highDrop > 0) {
                    // Drop highest bins by moving max down to the upper boundary of the last kept bin.
                    const uint16_t keptHighestBin = static_cast<uint16_t>(gpf - 1 - highDrop);
                    if (keptHighestBin < oldEdges.size()) {
                        newMax = oldEdges[static_cast<uint8_t>(keptHighestBin)];
                    }
                }

                if (!(newMax > newMin + 1e-9f)) {
                    continue;
                }

                // Build mapping oldBin -> newBin (shift down by lowShift; clamp into last kept bin).
                const uint8_t lastKept = static_cast<uint8_t>(gpf - 1 - highDrop);
                for (uint16_t oldBin = 0; oldBin < gpf; ++oldBin) {
                    int32_t nb = static_cast<int32_t>(oldBin) - static_cast<int32_t>(lowShift);
                    if (nb < 0) nb = 0;
                    if (nb > static_cast<int32_t>(lastKept)) nb = static_cast<int32_t>(lastKept);
                    tempFilter.setMapping(f, static_cast<uint8_t>(oldBin), static_cast<uint8_t>(nb));
                }

                // Construct new absolute edges: shift left by lowShift (preserve existing boundaries),
                // and collapse any removed high-edge boundaries to newMax.
                vector<float> newEdgesAbs;
                newEdgesAbs.reserve(ecount);
                const int32_t lastKeptEdgeIdx = static_cast<int32_t>(ecount) - static_cast<int32_t>(highDrop) - 1;
                for (uint8_t ei = 0; ei < ecount; ++ei) {
                    int32_t src = static_cast<int32_t>(ei) + static_cast<int32_t>(lowShift);
                    if (src >= 0 && src <= lastKeptEdgeIdx && src < static_cast<int32_t>(oldEdges.size())) {
                        newEdgesAbs.push_back(oldEdges[static_cast<size_t>(src)]);
                    } else {
                        newEdgesAbs.push_back(newMax);
                    }
                }

                // Update stored feature range & scaled edges.
                featureMins[f] = newMin;
                featureMaxs[f] = newMax;

                const float newRange = newMax - newMin;
                uint64_t newScale = 1ULL;
                if (newRange > 1e-9f) {
                    long double rawScale = static_cast<long double>(std::numeric_limits<uint16_t>::max()) / static_cast<long double>(newRange);
                    if (rawScale < 1.0L) rawScale = 1.0L;
                    if (rawScale > static_cast<long double>(std::numeric_limits<uint64_t>::max())) {
                        rawScale = static_cast<long double>(std::numeric_limits<uint64_t>::max());
                    }
                    newScale = static_cast<uint64_t>(rawScale);
                    if (newScale == 0) newScale = 1ULL;
                }
                featureScales[f] = newScale;
                featureBaselinesScaled[f] = scaleToInt64(static_cast<double>(newMin), newScale);

                for (uint8_t i = 0; i < ecount; ++i) {
                    double diff = static_cast<double>(newEdgesAbs[i]) - static_cast<double>(newMin);
                    if (diff < 0.0) diff = 0.0;
                    long double scaled = static_cast<long double>(diff) * static_cast<long double>(newScale);
                    if (scaled < 0.0L) scaled = 0.0L;
                    if (scaled > static_cast<long double>(std::numeric_limits<uint16_t>::max())) {
                        scaled = static_cast<long double>(std::numeric_limits<uint16_t>::max());
                    }
                    allEdgesScaled[off + i] = static_cast<uint16_t>(scaled + 0.5L);
                }

                changed_any = true;
            }

            if (!changed_any) {
                tempFilter.clear();
                return false;
            }

            // Apply remapping to the currently loaded dataset so training uses the updated bins immediately.
            (void)loaded_train_data.apply_update_filter_inplace(tempFilter);

            // Store filter for next time this dataset is loaded from storage.
            out_filter = tempFilter;
            return true;
        }
        
        size_t memory_usage() const {
            size_t usage = 0;
            
            // Basic members
            usage += sizeof(numFeatures) + sizeof(groupsPerFeature) + sizeof(numLabels) + 
            sizeof(quantization_coefficient) + sizeof(isLoaded);
            usage += 4;
            
            // Core data structures
            usage += featureTypes.memory_usage();
            usage += featureMins.memory_usage();
            usage += featureMaxs.memory_usage();
            usage += featureBaselinesScaled.memory_usage();
            usage += featureScales.memory_usage();
            usage += allEdgesScaled.memory_usage();
            usage += edgeOffsets.memory_usage();
            usage += edgeCounts.memory_usage();
            usage += allDiscreteValuesF.memory_usage();
            usage += dcOffsets.memory_usage();
            usage += dcCounts.memory_usage();
            usage += labelOffsets.memory_usage();
            usage += labelLengths.memory_usage();
            usage += labelStorage.memory_usage();
#ifndef EML_STATIC_MODEL
            usage += featureMeans.memory_usage();
            usage += featureStdDevs.memory_usage();
#endif
            
            return usage;
        }
        
        // Getters}

} // namespace eml
