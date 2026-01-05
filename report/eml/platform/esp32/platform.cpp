/**
 * @file platform.cpp
 * @brief ESP32 Platform - Platform Info Implementation
 * 
 * Implements eml_platform.h interface for ESP32.
 */

#include "../../pal/eml_platform.h"
#include "../../pal/eml_io.h"
#include "eml_esp32.h"
#include <Arduino.h>
#include <esp_system.h>

#if EML_ESP32_PSRAM_AVAILABLE
#include <esp_psram.h>
#endif

namespace eml {
namespace pal {

bool eml_platform_init() {
    // Platform-specific initialization if needed
    return true;
}

EmlPlatformInfo eml_platform_info() {
    EmlPlatformInfo info{};
    
    info.name = esp32::variant_name();
    info.variant = esp32::variant_name();
    info.cpu_freq_mhz = ESP.getCpuFreqMHz();
    info.flash_size = ESP.getFlashChipSize();
    info.ram_size = ESP.getHeapSize();
    
#if EML_ESP32_PSRAM_AVAILABLE
    if (esp_psram_is_initialized()) {
        info.external_ram_size = esp_psram_get_size();
    } else {
        info.external_ram_size = 0;
    }
#else
    info.external_ram_size = 0;
#endif
    
    // Build capabilities flags
    EmlPlatformCaps caps = EmlPlatformCaps::NONE;
    
    if (esp32::supports_psram()) {
        caps = caps | EmlPlatformCaps::HAS_PSRAM;
    }
    if (esp32::supports_sdmmc()) {
        caps = caps | EmlPlatformCaps::HAS_SD_MMC;
    }
    caps = caps | EmlPlatformCaps::HAS_SD_SPI;  // All ESP32 support SPI
    
    if (esp32::has_native_usb()) {
        caps = caps | EmlPlatformCaps::HAS_USB_CDC;
    }
    
    caps = caps | EmlPlatformCaps::HAS_WIFI;
    caps = caps | EmlPlatformCaps::HAS_BLE;
    caps = caps | EmlPlatformCaps::HAS_FPU;
    caps = caps | EmlPlatformCaps::HAS_FATFS;
    
    info.capabilities = caps;
    
    return info;
}

const char* eml_platform_name() {
    return esp32::variant_name();
}

const char* eml_platform_root_path() {
    return "/";
}

size_t eml_platform_default_chunk_size() {
    return esp32::default_chunk_size();
}

size_t eml_platform_rx_buffer_size() {
    return esp32::usb_rx_buffer_size();
}

bool eml_platform_has_capability(EmlPlatformCaps cap) {
    EmlPlatformInfo info = eml_platform_info();
    return has_cap(info.capabilities, cap);
}

[[noreturn]] void eml_platform_restart() {
    ESP.restart();
    // Should never reach here
    while (true) { delay(1000); }
}

uint64_t eml_platform_uptime_seconds() {
    return millis() / 1000;
}

void eml_platform_print_info() {
    EmlPlatformInfo info = eml_platform_info();
    
    eml_println("\n=== EML Platform Configuration ===");
    eml_printf("Platform: %s\n", info.name);
    eml_printf("CPU Freq: %u MHz\n", info.cpu_freq_mhz);
    eml_printf("Flash: %u bytes\n", info.flash_size);
    eml_printf("Internal RAM: %u bytes\n", info.ram_size);
    
    if (info.external_ram_size > 0) {
        eml_printf("External RAM (PSRAM): %u bytes\n", info.external_ram_size);
    }
    
    eml_printf("Default chunk size: %zu bytes\n", eml_platform_default_chunk_size());
    eml_printf("PSRAM enabled: %s\n", eml_platform_has_capability(EmlPlatformCaps::HAS_PSRAM) ? "yes" : "no");
    eml_printf("SD_MMC available: %s\n", eml_platform_has_capability(EmlPlatformCaps::HAS_SD_MMC) ? "yes" : "no");
    eml_printf("FATFS available: %s\n", eml_platform_has_capability(EmlPlatformCaps::HAS_FATFS) ? "yes" : "no");
    eml_println("================================\n");
}

} // namespace pal
} // namespace eml
