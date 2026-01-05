#pragma once
/**
 * @file eml_posix.h
 * @brief POSIX Platform Extension Header
 * 
 * POSIX-specific definitions for Linux, macOS, BSD systems.
 * This header is automatically included when EML_PLATFORM_POSIX is defined.
 */

#include <cstdint>
#include <cstddef>

// =============================================================================
// POSIX PLATFORM CONFIGURATION
// =============================================================================

namespace eml {
namespace posix {

/**
 * @brief Detect specific OS
 */
enum class PosixVariant : uint8_t {
    LINUX,
    MACOS,
    BSD,
    UNKNOWN
};

constexpr PosixVariant detect_variant() {
#if defined(__linux__)
    return PosixVariant::LINUX;
#elif defined(__APPLE__)
    return PosixVariant::MACOS;
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    return PosixVariant::BSD;
#else
    return PosixVariant::UNKNOWN;
#endif
}

static constexpr PosixVariant VARIANT = detect_variant();

// Detect 64-bit
constexpr bool is_64bit() {
    return sizeof(void*) == 8;
}

// Default chunk size for POSIX (larger than MCU)
constexpr size_t default_chunk_size() {
    return 4096;
}

// Default RX buffer size
constexpr size_t default_rx_buffer_size() {
    return 4096;
}

// Get variant name
inline const char* variant_name() {
    switch (VARIANT) {
        case PosixVariant::LINUX:   return "Linux";
        case PosixVariant::MACOS:   return "macOS";
        case PosixVariant::BSD:     return "BSD";
        default:                     return "POSIX";
    }
}

// Storage limits (essentially unlimited on desktop)
constexpr size_t max_dataset_bytes() {
    return 1024ULL * 1024 * 1024;  // 1GB
}

constexpr size_t max_infer_log_bytes() {
    return 100 * 1024 * 1024;  // 100MB
}

// Default root path for models
#ifndef EML_POSIX_ROOT_PATH
    #define EML_POSIX_ROOT_PATH "./eml_data"
#endif

} // namespace posix
} // namespace eml

// =============================================================================
// BACKWARD COMPATIBILITY MACROS
// =============================================================================

#ifndef RF_BOARD_NAME
    #define RF_BOARD_NAME eml::posix::variant_name()
#endif

#ifndef RF_BOARD_SUPPORTS_PSRAM
    #define RF_BOARD_SUPPORTS_PSRAM 0
#endif

#ifndef RF_BOARD_SUPPORTS_SDMMC
    #define RF_BOARD_SUPPORTS_SDMMC 0
#endif

#ifndef RF_BOARD_HAS_NATIVE_USB
    #define RF_BOARD_HAS_NATIVE_USB 0
#endif

#ifndef RF_BOARD_DEFAULT_CHUNK
    #define RF_BOARD_DEFAULT_CHUNK eml::posix::default_chunk_size()
#endif

#ifndef RF_BOARD_USB_RX_BUFFER
    #define RF_BOARD_USB_RX_BUFFER eml::posix::default_rx_buffer_size()
#endif

#ifndef RF_PSRAM_AVAILABLE
    #define RF_PSRAM_AVAILABLE 0
#endif

#ifndef RF_USE_PSRAM
    #define RF_USE_PSRAM 0
#endif

#ifndef RF_HAS_SDMMC
    #define RF_HAS_SDMMC 0
#endif

#ifndef RF_HAS_FATFS
    #define RF_HAS_FATFS 0
#endif
