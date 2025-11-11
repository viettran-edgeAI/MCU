#pragma once

#ifndef RF_BOARD_CONFIG_H
#define RF_BOARD_CONFIG_H

/*
 * Random Forest MCU - Board capability configuration
 * --------------------------------------------------
 * This header centralizes feature detection for the targets supported by the
 * STL_MCU library. It favours automatic detection for popular ESP32 variants
 * while remaining extensible to other MCU families.
 *
 * Customisation hooks:
 *   - Define RF_BOARD_SKIP_AUTODETECT before including this file to bypass the
 *     built-in detection logic and provide your own feature macros.
 *   - Override specific capabilities by defining macros such as
 *       RF_BOARD_NAME, RF_BOARD_SUPPORTS_PSRAM, RF_BOARD_SUPPORTS_SDMMC,
 *       RF_BOARD_DEFAULT_CHUNK, RF_BOARD_USB_RX_BUFFER, etc. prior to include.
 *   - Use RF_USE_PSRAM, RF_USE_SDCARD, RF_USE_SDSPI in your sketch or project
 *     to control PSRAM usage and storage backends per build without editing
 *     this file.
 *
 * The goal is to prevent impossible configurations (e.g. enabling PSRAM on a
 * C3) while leaving users in control of feature toggles that are valid for
 * their hardware.
 */

#include <stdint.h>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// -----------------------------------------------------------------------------
// Auto-detect common MCU families unless the user opts out
// -----------------------------------------------------------------------------
#if !defined(RF_BOARD_SKIP_AUTODETECT)
  #if defined(ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32H2)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32-H2"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 0
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 0
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 256
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 220
    #endif
    #ifndef RF_BOARD_IS_ESP32H2
      #define RF_BOARD_IS_ESP32H2 1
    #endif
  #elif defined(ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32C6)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32-C6"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 0
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 0
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 384
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 220
    #endif
    #ifndef RF_BOARD_IS_ESP32C6
      #define RF_BOARD_IS_ESP32C6 1
    #endif
  #elif defined(ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C3)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32-C3"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 0
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 0
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 384
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 220
    #endif
    #ifndef RF_BOARD_IS_ESP32C3
      #define RF_BOARD_IS_ESP32C3 1
    #endif
  #elif defined(ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S3)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32-S3"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 1
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 1
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 512
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 256
    #endif
    #ifndef RF_BOARD_IS_ESP32S3
      #define RF_BOARD_IS_ESP32S3 1
    #endif
  #elif defined(ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S2)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32-S2"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 1
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 1
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 512
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 256
    #endif
    #ifndef RF_BOARD_IS_ESP32S2
      #define RF_BOARD_IS_ESP32S2 1
    #endif
  #elif defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ARCH_ESP32)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "ESP32"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 1
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 1
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 0
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 512
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 256
    #endif
    #ifndef RF_BOARD_IS_ESP32
      #define RF_BOARD_IS_ESP32 1
    #endif
  #elif defined(ARDUINO_ARCH_STM32) || defined(STM32F4) || defined(STM32F7)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "STM32"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 0
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 1
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 512
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 256
    #endif
    #ifndef RF_BOARD_IS_STM32
      #define RF_BOARD_IS_STM32 1
    #endif
  #elif defined(ARDUINO_ARCH_RP2040)
    #ifndef RF_BOARD_NAME
      #define RF_BOARD_NAME "RP2040"
    #endif
    #ifndef RF_BOARD_SUPPORTS_PSRAM
      #define RF_BOARD_SUPPORTS_PSRAM 0
    #endif
    #ifndef RF_BOARD_SUPPORTS_SDMMC
      #define RF_BOARD_SUPPORTS_SDMMC 0
    #endif
    #ifndef RF_BOARD_HAS_NATIVE_USB
      #define RF_BOARD_HAS_NATIVE_USB 1
    #endif
    #ifndef RF_BOARD_USB_RX_BUFFER
      #define RF_BOARD_USB_RX_BUFFER 256
    #endif
    #ifndef RF_BOARD_DEFAULT_CHUNK
      #define RF_BOARD_DEFAULT_CHUNK 220
    #endif
    #ifndef RF_BOARD_IS_RP2040
      #define RF_BOARD_IS_RP2040 1
    #endif
  #endif
#endif // RF_BOARD_SKIP_AUTODETECT

// -----------------------------------------------------------------------------
// Ensure feature macros exist even if detection was skipped or not matched
// -----------------------------------------------------------------------------
#ifndef RF_BOARD_NAME
  #define RF_BOARD_NAME "Generic MCU"
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
#ifndef RF_BOARD_USB_RX_BUFFER
  #define RF_BOARD_USB_RX_BUFFER 256
#endif
#ifndef RF_BOARD_DEFAULT_CHUNK
  #define RF_BOARD_DEFAULT_CHUNK 220
#endif
#ifndef RF_BOARD_SMALL_USB_BUFFER
  #define RF_BOARD_SMALL_USB_BUFFER ((RF_BOARD_USB_RX_BUFFER) <= 384)
#endif

#ifndef RF_BOARD_IS_ESP32
  #define RF_BOARD_IS_ESP32 0
#endif
#ifndef RF_BOARD_IS_ESP32S2
  #define RF_BOARD_IS_ESP32S2 0
#endif
#ifndef RF_BOARD_IS_ESP32S3
  #define RF_BOARD_IS_ESP32S3 0
#endif
#ifndef RF_BOARD_IS_ESP32C3
  #define RF_BOARD_IS_ESP32C3 0
#endif
#ifndef RF_BOARD_IS_ESP32C6
  #define RF_BOARD_IS_ESP32C6 0
#endif
#ifndef RF_BOARD_IS_ESP32H2
  #define RF_BOARD_IS_ESP32H2 0
#endif
#ifndef RF_BOARD_IS_STM32
  #define RF_BOARD_IS_STM32 0
#endif
#ifndef RF_BOARD_IS_RP2040
  #define RF_BOARD_IS_RP2040 0
#endif
#ifndef RF_BOARD_IS_UNKNOWN
  #define RF_BOARD_IS_UNKNOWN 0
#endif

#if !RF_BOARD_IS_ESP32 && !RF_BOARD_IS_ESP32S2 && !RF_BOARD_IS_ESP32S3 && \
    !RF_BOARD_IS_ESP32C3 && !RF_BOARD_IS_ESP32C6 && !RF_BOARD_IS_ESP32H2 && \
    !RF_BOARD_IS_STM32 && !RF_BOARD_IS_RP2040
  #undef RF_BOARD_IS_UNKNOWN
  #define RF_BOARD_IS_UNKNOWN 1
#endif

// -----------------------------------------------------------------------------
// Build-time PSRAM availability
// -----------------------------------------------------------------------------
#ifndef RF_BOARD_BUILD_HAS_PSRAM
  #if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT) || \
      defined(SPIRAM_CACHE_WORKAROUND)
    #define RF_BOARD_BUILD_HAS_PSRAM 1
  #else
    #define RF_BOARD_BUILD_HAS_PSRAM 0
  #endif
#endif

#ifdef RF_PSRAM_AVAILABLE
  #undef RF_PSRAM_AVAILABLE
#endif

#if defined(RF_USE_PSRAM)
  #if !RF_BOARD_SUPPORTS_PSRAM
    #undef RF_USE_PSRAM
    #define RF_PSRAM_AVAILABLE 0
    #warning "RF_USE_PSRAM requested but board class reports no PSRAM support; disabling."
  #elif RF_BOARD_BUILD_HAS_PSRAM
    #define RF_PSRAM_AVAILABLE 1
  #else
    #define RF_PSRAM_AVAILABLE 0
    #warning "RF_USE_PSRAM requested but build is missing PSRAM support; falling back to DRAM."
  #endif
#else
  #define RF_PSRAM_AVAILABLE 0
#endif

// -----------------------------------------------------------------------------
// Storage helpers
// -----------------------------------------------------------------------------
#ifdef RF_HAS_SDMMC
  #undef RF_HAS_SDMMC
#endif
#define RF_HAS_SDMMC (RF_BOARD_SUPPORTS_SDMMC)

#if defined(RF_USE_SDCARD) && !defined(RF_USE_SDSPI) && !RF_BOARD_SUPPORTS_SDMMC
  #define RF_USE_SDSPI 1
  #warning "RF_USE_SDCARD selected; board lacks SD_MMC so enabling SPI fallback."
#endif

// -----------------------------------------------------------------------------
// USB transfer tuning defaults
// -----------------------------------------------------------------------------
#ifndef DEFAULT_CHUNK_SIZE
  #define DEFAULT_CHUNK_SIZE RF_BOARD_DEFAULT_CHUNK
#endif
#ifndef USER_CHUNK_SIZE
  #define USER_CHUNK_SIZE DEFAULT_CHUNK_SIZE
#endif

#define RF_BOARD_CDC_WARNING (RF_BOARD_SMALL_USB_BUFFER)

// -----------------------------------------------------------------------------
// Diagnostics helper
// -----------------------------------------------------------------------------
inline void print_board_info() {
#if defined(ARDUINO)
    Serial.println("\n=== RF Board Configuration ===");
    Serial.print("Board: ");
    Serial.println(RF_BOARD_NAME);
    Serial.print("USB CDC chunk: ");
    Serial.print(USER_CHUNK_SIZE);
    Serial.println(" bytes");
    Serial.print("PSRAM enabled: ");
    Serial.println(RF_PSRAM_AVAILABLE ? "yes" : "no");
    Serial.print("SD_MMC available: ");
    Serial.println(RF_HAS_SDMMC ? "yes" : "no");
    Serial.print("Storage preference: ");
    Serial.println(
        #ifdef RF_USE_SDCARD
            #ifdef RF_USE_SDSPI
                "SD (SPI)"
            #else
                "SD (SD_MMC)"
            #endif
        #else
            "Flash"
        #endif
    );
    if (RF_BOARD_CDC_WARNING) {
        Serial.println("Note: board has a compact USB CDC buffer. Keep chunks conservative or define USER_CHUNK_SIZE manually.");
    }
    Serial.println("==============================\n");
#else
    (void)0;
#endif
}

#endif // RF_BOARD_CONFIG_H
