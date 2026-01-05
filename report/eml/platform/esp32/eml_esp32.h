#pragma once
/**
 * @file eml_esp32.h
 * @brief ESP32 Platform Extension Header
 * 
 * ESP32-specific definitions, configurations, and extensions.
 * This header is automatically included when EML_PLATFORM_ESP32 is defined.
 */

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

// ESP-IDF includes
#if defined(ESP_PLATFORM)
    #include "esp_system.h"
    #include "esp_heap_caps.h"
    #include "esp_random.h"
#endif

// =============================================================================
// ESP32 VARIANT DETECTION
// =============================================================================

namespace eml {
namespace esp32 {

// Detect specific ESP32 variant
enum class Esp32Variant : uint8_t {
    ESP32_CLASSIC,
    ESP32_S2,
    ESP32_S3,
    ESP32_C3,
    ESP32_C6,
    ESP32_H2,
    UNKNOWN
};

constexpr Esp32Variant detect_variant() {
#if defined(ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32H2)
    return Esp32Variant::ESP32_H2;
#elif defined(ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C6)
    return Esp32Variant::ESP32_C6;
#elif defined(ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    return Esp32Variant::ESP32_C3;
#elif defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    return Esp32Variant::ESP32_S3;
#elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2)
    return Esp32Variant::ESP32_S2;
#elif defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ARCH_ESP32)
    return Esp32Variant::ESP32_CLASSIC;
#else
    return Esp32Variant::UNKNOWN;
#endif
}

static constexpr Esp32Variant VARIANT = detect_variant();

// =============================================================================
// ESP32 CAPABILITY DETECTION
// =============================================================================

// PSRAM support varies by chip
constexpr bool supports_psram() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_CLASSIC:
        case Esp32Variant::ESP32_S2:
        case Esp32Variant::ESP32_S3:
            return true;
        default:
            return false;
    }
}

// SD_MMC support
constexpr bool supports_sdmmc() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_CLASSIC:
        case Esp32Variant::ESP32_S2:
        case Esp32Variant::ESP32_S3:
            return true;
        default:
            return false;
    }
}

// Native USB support
constexpr bool has_native_usb() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_S2:
        case Esp32Variant::ESP32_S3:
        case Esp32Variant::ESP32_C3:
        case Esp32Variant::ESP32_C6:
        case Esp32Variant::ESP32_H2:
            return true;
        default:
            return false;
    }
}

// Default chunk sizes per variant
constexpr size_t default_chunk_size() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_S3:
            return 512;
        case Esp32Variant::ESP32_S2:
            return 256;
        case Esp32Variant::ESP32_CLASSIC:
            return 512;
        default:
            return 220;
    }
}

// USB RX buffer size per variant
constexpr size_t usb_rx_buffer_size() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_S3:
        case Esp32Variant::ESP32_CLASSIC:
            return 512;
        case Esp32Variant::ESP32_S2:
            return 512;
        case Esp32Variant::ESP32_C3:
        case Esp32Variant::ESP32_C6:
            return 384;
        case Esp32Variant::ESP32_H2:
            return 256;
        default:
            return 256;
    }
}

// Get variant name string
inline const char* variant_name() {
    switch (VARIANT) {
        case Esp32Variant::ESP32_H2:     return "ESP32-H2";
        case Esp32Variant::ESP32_C6:     return "ESP32-C6";
        case Esp32Variant::ESP32_C3:     return "ESP32-C3";
        case Esp32Variant::ESP32_S3:     return "ESP32-S3";
        case Esp32Variant::ESP32_S2:     return "ESP32-S2";
        case Esp32Variant::ESP32_CLASSIC: return "ESP32";
        default:                          return "ESP32-Unknown";
    }
}

// =============================================================================
// PSRAM CONFIGURATION
// =============================================================================

// Build-time PSRAM detection
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT) || defined(SPIRAM_CACHE_WORKAROUND)
    #define EML_ESP32_BUILD_HAS_PSRAM 1
#else
    #define EML_ESP32_BUILD_HAS_PSRAM 0
#endif

// Final PSRAM availability
#ifdef EML_USE_PSRAM
    #if !supports_psram()
        #undef EML_USE_PSRAM
        #define EML_ESP32_PSRAM_AVAILABLE 0
        #warning "EML_USE_PSRAM requested but ESP32 variant does not support PSRAM"
    #elif EML_ESP32_BUILD_HAS_PSRAM
        #define EML_ESP32_PSRAM_AVAILABLE 1
    #else
        #define EML_ESP32_PSRAM_AVAILABLE 0
        #warning "EML_USE_PSRAM requested but build lacks PSRAM support"
    #endif
#else
    #define EML_ESP32_PSRAM_AVAILABLE 0
#endif

#ifndef EML_USE_PSRAM
    #define EML_USE_PSRAM 0
#endif

// =============================================================================
// STORAGE CONFIGURATION
// =============================================================================

// SD_MMC availability
#define EML_ESP32_HAS_SDMMC supports_sdmmc()

// FATFS availability (all ESP32 variants support this)
#define EML_ESP32_HAS_FATFS 1

// Default storage limits
constexpr size_t flash_max_dataset_bytes() {
    return 512 * 1024;  // 512KB for internal flash
}

constexpr size_t sd_max_dataset_bytes() {
    return 50 * 1024 * 1024;  // 50MB for SD card
}

constexpr size_t flash_max_infer_log_bytes() {
    return 64 * 1024;  // 64KB for inference logs
}

constexpr size_t sd_max_infer_log_bytes() {
    return 10 * 1024 * 1024;  // 10MB for SD card logs
}

} // namespace esp32
} // namespace eml

// =============================================================================
// BACKWARD COMPATIBILITY MACROS
// =============================================================================

// Map to old RF_BOARD_* macros for backward compatibility
#ifndef RF_BOARD_NAME
    #define RF_BOARD_NAME eml::esp32::variant_name()
#endif

#ifndef RF_BOARD_SUPPORTS_PSRAM
    #define RF_BOARD_SUPPORTS_PSRAM (eml::esp32::supports_psram() ? 1 : 0)
#endif

#ifndef RF_BOARD_SUPPORTS_SDMMC
    #define RF_BOARD_SUPPORTS_SDMMC (eml::esp32::supports_sdmmc() ? 1 : 0)
#endif

#ifndef RF_BOARD_HAS_NATIVE_USB
    #define RF_BOARD_HAS_NATIVE_USB (eml::esp32::has_native_usb() ? 1 : 0)
#endif

#ifndef RF_BOARD_DEFAULT_CHUNK
    #define RF_BOARD_DEFAULT_CHUNK eml::esp32::default_chunk_size()
#endif

#ifndef RF_BOARD_USB_RX_BUFFER
    #define RF_BOARD_USB_RX_BUFFER eml::esp32::usb_rx_buffer_size()
#endif

#ifndef RF_PSRAM_AVAILABLE
    #define RF_PSRAM_AVAILABLE EML_ESP32_PSRAM_AVAILABLE
#endif

#ifndef RF_USE_PSRAM
    #define RF_USE_PSRAM EML_USE_PSRAM
#endif

#ifndef RF_HAS_SDMMC
    #define RF_HAS_SDMMC (eml::esp32::supports_sdmmc() ? 1 : 0)
#endif

#ifndef RF_HAS_FATFS
    #define RF_HAS_FATFS 1
#endif
