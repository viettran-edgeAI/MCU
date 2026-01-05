#pragma once
/**
 * @file eml_platform.h
 * @brief Platform Abstraction Layer - Platform Information Interface
 * 
 * Declares platform-agnostic system information functions.
 * Each platform must implement these functions.
 */

#include <cstdint>
#include <cstddef>

namespace eml {
namespace pal {

/**
 * @brief Platform capability flags
 */
enum class EmlPlatformCaps : uint32_t {
    NONE           = 0,
    HAS_PSRAM      = 1 << 0,   // External PSRAM available
    HAS_SD_MMC     = 1 << 1,   // SD_MMC interface available
    HAS_SD_SPI     = 1 << 2,   // SPI SD interface available
    HAS_USB_CDC    = 1 << 3,   // USB CDC (native USB serial)
    HAS_WIFI       = 1 << 4,   // WiFi available
    HAS_BLE        = 1 << 5,   // Bluetooth LE available
    HAS_CAMERA     = 1 << 6,   // Camera interface available
    HAS_FPU        = 1 << 7,   // Hardware floating point
    IS_64BIT       = 1 << 8,   // 64-bit processor
    HAS_FATFS      = 1 << 9,   // FAT filesystem support
};

inline EmlPlatformCaps operator|(EmlPlatformCaps a, EmlPlatformCaps b) {
    return static_cast<EmlPlatformCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline EmlPlatformCaps operator&(EmlPlatformCaps a, EmlPlatformCaps b) {
    return static_cast<EmlPlatformCaps>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool has_cap(EmlPlatformCaps caps, EmlPlatformCaps flag) {
    return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * @brief Platform information structure
 */
struct EmlPlatformInfo {
    const char* name;           // Platform name (e.g., "ESP32-S3", "Linux x86_64")
    const char* variant;        // Platform variant (e.g., "ESP32-CAM")
    uint32_t cpu_freq_mhz;      // CPU frequency in MHz
    uint32_t flash_size;        // Flash size in bytes
    uint32_t ram_size;          // Internal RAM size in bytes
    uint32_t external_ram_size; // External RAM size (0 if none)
    EmlPlatformCaps capabilities;
};

/**
 * @brief Initialize the platform subsystem
 * @return true if initialization successful
 */
bool eml_platform_init();

/**
 * @brief Get platform information
 * @return EmlPlatformInfo structure with platform details
 */
EmlPlatformInfo eml_platform_info();

/**
 * @brief Get platform name string
 * @return Human-readable platform name
 */
const char* eml_platform_name();

/**
 * @brief Get platform root path for model storage
 * @return Root path (e.g., "/" for embedded, "./models" for desktop)
 */
const char* eml_platform_root_path();

/**
 * @brief Get default chunk size for data transfers
 * @return Optimal chunk size in bytes
 */
size_t eml_platform_default_chunk_size();

/**
 * @brief Get USB/serial RX buffer size
 * @return Buffer size in bytes
 */
size_t eml_platform_rx_buffer_size();

/**
 * @brief Check if a specific capability is available
 * @param cap Capability to check
 * @return true if capability is available
 */
bool eml_platform_has_capability(EmlPlatformCaps cap);

/**
 * @brief Restart/reset the system
 */
[[noreturn]] void eml_platform_restart();

/**
 * @brief Get system uptime in seconds
 * @return Seconds since system start
 */
uint64_t eml_platform_uptime_seconds();

/**
 * @brief Print platform diagnostic information
 */
void eml_platform_print_info();

} // namespace pal
} // namespace eml
