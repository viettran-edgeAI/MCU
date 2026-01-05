#pragma once

#include "eml/core/containers/stl_mcu/STL_MCU.h"
#include "eml/pal/eml_time.h"
#include "eml/pal/eml_memory.h"
#include "eml/pal/eml_fs.h"
#include "eml/core/eml_debug.h"
#include <cstdlib>
#include <cstring>
#include <limits>

#ifdef EML_DEV_STAGE
    #define ENABLE_TEST_DATA 1
#else
    #define ENABLE_TEST_DATA 0
#endif

// EML_STATIC_MODEL: If defined, training code is excluded (inference only)

namespace eml {

    using rf_label_type    = uint8_t;  // type for label related operations
    using rf_sample_type   = uint32_t; // type for sample related operations
    using rf_node_type     = size_t; // type for tree node related operations
    using sampleID_set     = ID_vector<rf_sample_type>;       // set of unique sample IDs

    static constexpr uint8_t         RF_MAX_LABEL_LENGTH    = 32;     // max label length
    static constexpr uint8_t         RF_PATH_BUFFER         = 64;     // buffer for file_path(limit to 2 level of file)
    static constexpr uint8_t         RF_MAX_TREES           = 100;    // maximum number of trees in a forest
    static constexpr rf_label_type   RF_MAX_LABELS          = 255;    // maximum number of unique labels supported 
    static constexpr uint16_t        RF_MAX_FEATURES        = 1023;   // maximum number of features (can exceed this limit)
    static constexpr uint32_t        RF_MAX_NODES           = 262144; // Maximum nodes per tree (18 bits)
    static constexpr rf_sample_type  RF_MAX_SAMPLES         = 1048576;// maximum number of samples in a dataset (20 bits)
    
    // enum for time units
    typedef enum TimeUnit : uint8_t {
        MILLISECONDS = 0,
        MICROSECONDS = 1,
        NANOSECONDS  = 2
    };

    long unsigned inline rf_time_now(TimeUnit unit) {
        switch (unit) {
            case TimeUnit::MICROSECONDS:
                return static_cast<long>(pal::eml_micros());
            case TimeUnit::NANOSECONDS:
                return static_cast<long>(pal::eml_micros()) * 1000L; // approximate
            case TimeUnit::MILLISECONDS:
            default:
                return static_cast<long>(pal::eml_millis());
        }
    }

    // define error label base on rf_label_type
    template<typename T>
    struct Rf_err_label {
        static constexpr T value = static_cast<T>(~static_cast<T>(0));
    };

    static constexpr rf_label_type RF_ERROR_LABEL = Rf_err_label<rf_label_type>::value; 

    // func to check memory status (free heap size, largest free block)
    static pair<size_t, size_t> eml_memory_status(){
        auto status = pal::eml_memory_status();
        return make_pair(status.free_heap, status.largest_block);
    }

    // Macro for file existence check
    #define RF_FS_EXISTS(path) eml::pal::eml_fs_exists(path)

} // namespace eml
