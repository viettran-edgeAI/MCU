#pragma once
/**
 * @file eml.h
 * @brief Edge Machine Learning (EML) Framework - Main Entry Header
 * 
 * This is the main entry point for the EML framework. Include this header
 * to use the framework with automatic platform detection and configuration.
 * 
 * Features:
 * - Automatic platform detection (ESP32, POSIX, Windows)
 * - Platform Abstraction Layer (PAL) for portable code
 * - Tree-based ML models (Random Forest, Decision Trees, XGBoost)
 * - Quantization and embedding for microcontrollers
 * - On-device retraining support
 * 
 * Usage:
 *   // Optionally define platform before including (auto-detected if not)
 *   // #define EML_PLATFORM_ESP32
 *   
 *   #include <eml.h>
 *   
 *   void setup() {
 *       eml::init();  // Initialize all EML subsystems
 *       // Use EML framework...
 *   }
 * 
 * Build Configurations:
 *   EML_STATIC_MODEL  - Inference-only mode (excludes training code)
 *   EML_DEV_STAGE     - Development mode (enables test data, extra validation)
 *   EML_DEBUG_LEVEL   - Debug verbosity (0=silent, 1=info, 2=debug, 3=verbose)
 *   EML_USE_PSRAM     - Enable PSRAM usage on supported platforms
 */

// =============================================================================
// VERSION INFORMATION
// =============================================================================

#define EML_VERSION_MAJOR 1
#define EML_VERSION_MINOR 0
#define EML_VERSION_PATCH 0
#define EML_VERSION_STRING "1.0.0"

// =============================================================================
// CORE CONFIGURATION
// =============================================================================

#include "core/eml_config.h"

// =============================================================================
// PLATFORM ABSTRACTION LAYER
// =============================================================================

#include "pal/eml_io.h"
#include "pal/eml_fs.h"
#include "pal/eml_memory.h"
#include "pal/eml_time.h"
#include "pal/eml_platform.h"

// =============================================================================
// EML NAMESPACE AND INITIALIZATION
// =============================================================================

namespace eml {

/**
 * @brief Initialize all EML subsystems
 * 
 * Call this once at startup to initialize:
 * - Platform subsystem
 * - Time subsystem
 * - Memory subsystem
 * - I/O subsystem
 * - Filesystem (optional)
 * 
 * @param init_fs Whether to initialize filesystem (default: true)
 * @param baud_rate Serial baud rate for I/O (default: 115200)
 * @return true if all subsystems initialized successfully
 */
inline bool init(bool init_fs = true, uint32_t baud_rate = 115200) {
    bool success = true;
    
    // Initialize I/O first (for debug output)
    success &= pal::eml_io_init(baud_rate);
    
    // Initialize other subsystems
    success &= pal::eml_platform_init();
    success &= pal::eml_time_init();
    success &= pal::eml_memory_init();
    
    // Optionally initialize filesystem
    if (init_fs) {
        success &= pal::eml_fs_init();
    }
    
    if (success) {
        pal::eml_println("✅ EML framework initialized");
    } else {
        pal::eml_println("⚠️ EML initialization completed with warnings");
    }
    
    return success;
}

/**
 * @brief Shutdown EML subsystems
 */
inline void shutdown() {
    pal::eml_fs_deinit();
}

/**
 * @brief Get EML version string
 */
inline const char* version() {
    return EML_VERSION_STRING;
}

/**
 * @brief Print EML framework information
 */
inline void print_info() {
    pal::eml_println("\n=== Edge Machine Learning (EML) Framework ===");
    pal::eml_printf("Version: %s\n", EML_VERSION_STRING);
    pal::eml_printf("Platform: %s\n", EML_PLATFORM_NAME_STR);
    
#if EML_INCLUDE_TRAINING
    pal::eml_println("Mode: Full (training + inference)");
#else
    pal::eml_println("Mode: Static (inference only)");
#endif

#if EML_DEV_STAGE
    pal::eml_println("Stage: Development");
#else
    pal::eml_println("Stage: Production");
#endif

    pal::eml_printf("Debug level: %d\n", EML_DEBUG_LEVEL);
    pal::eml_println("============================================\n");
    
    pal::eml_platform_print_info();
}

// =============================================================================
// CONVENIENCE FUNCTIONS (PAL wrappers)
// =============================================================================

// Time functions
inline uint64_t millis() { return pal::eml_millis(); }
inline uint64_t micros() { return pal::eml_micros(); }
inline void delay(uint32_t ms) { pal::eml_delay_ms(ms); }
inline void delay_us(uint32_t us) { pal::eml_delay_us(us); }
inline void yield() { pal::eml_yield(); }

// Memory functions
inline pal::EmlMemoryStatus memory_status() { return pal::eml_memory_status(); }
inline size_t free_heap() { return pal::eml_free_heap(); }
inline bool has_psram() { return pal::eml_has_external_memory(); }

// Platform functions
inline const char* platform_name() { return pal::eml_platform_name(); }
inline void restart() { pal::eml_platform_restart(); }

} // namespace eml

// =============================================================================
// BACKWARD COMPATIBILITY
// =============================================================================

// For code that uses the old mcu::rf_time_now() function
namespace mcu {
    using TimeUnit = eml::pal::EmlTimeUnit;
    
    inline long unsigned rf_time_now(TimeUnit unit) {
        return static_cast<long unsigned>(eml::pal::eml_time_now(unit));
    }
    
    inline size_t rf_max_dataset_size() {
        return eml::pal::eml_fs_max_dataset_bytes();
    }
    
    // Wrapper for old eml_memory_status() that returned pair<size_t, size_t>
    inline std::pair<size_t, size_t> eml_memory_status() {
        auto status = eml::pal::eml_memory_status();
        return std::make_pair(status.free_heap, status.largest_block);
    }
}
