/**
 * @file fs.cpp
 * @brief ESP32 Platform - Filesystem Implementation
 * 
 * Implements eml_fs.h interface for ESP32 + Arduino.
 * Supports LittleFS, FFat, SD_MMC, and SD SPI backends.
 */

#include "../../pal/eml_fs.h"
#include "../../pal/eml_io.h"
#include "eml_esp32.h"
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#if EML_ESP32_HAS_FATFS
#include <FFat.h>
#endif

#if RF_HAS_SDMMC
#include <SD_MMC.h>
#endif

// Default SD SPI pins
#ifndef EML_SD_CS_PIN
    #define EML_SD_CS_PIN 5
#endif
#ifndef EML_SD_MOSI_PIN
    #define EML_SD_MOSI_PIN 23
#endif
#ifndef EML_SD_MISO_PIN
    #define EML_SD_MISO_PIN 19
#endif
#ifndef EML_SD_SCK_PIN
    #define EML_SD_SCK_PIN 18
#endif

// SD_MMC configuration
#ifndef EML_SDMMC_MOUNTPOINT
    #define EML_SDMMC_MOUNTPOINT "/sdcard"
#endif
#ifndef EML_SDMMC_FORMAT_IF_FAIL
    #define EML_SDMMC_FORMAT_IF_FAIL false
#endif
#ifndef EML_FATFS_FORMAT_IF_FAIL
    #define EML_FATFS_FORMAT_IF_FAIL true
#endif

namespace eml {
namespace pal {

// File handle wrapper
struct EmlFileHandle {
    File file;
    bool valid;
    
    EmlFileHandle() : valid(false) {}
    explicit EmlFileHandle(File f) : file(f), valid(true) {}
};

// Active storage tracking
static EmlStorageType g_active_storage = EmlStorageType::AUTO;

// Get the active FS object
static fs::FS* get_active_fs() {
    switch (g_active_storage) {
#if EML_ESP32_HAS_FATFS
        case EmlStorageType::INTERNAL_FAT:
            return &FFat;
#endif
#if RF_HAS_SDMMC
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
            return &SD_MMC;
#endif
        case EmlStorageType::SD_SPI:
            return &SD;
        case EmlStorageType::INTERNAL_FLASH:
        default:
            return &LittleFS;
    }
}

bool eml_fs_init(EmlStorageType type) {
    EmlStorageType selected = type;
    
    // Resolve AUTO to platform default
    if (selected == EmlStorageType::AUTO) {
        selected = EmlStorageType::INTERNAL_FLASH;
    }
    
    switch (selected) {
#if EML_ESP32_HAS_FATFS
        case EmlStorageType::INTERNAL_FAT:
        {
            if (!FFat.begin(EML_FATFS_FORMAT_IF_FAIL)) {
                eml_println("❌ FATFS mount failed");
                // Fallback to LittleFS
                if (!LittleFS.begin(true)) {
                    return false;
                }
                g_active_storage = EmlStorageType::INTERNAL_FLASH;
                return true;
            }
            g_active_storage = EmlStorageType::INTERNAL_FAT;
            eml_println("✅ FATFS initialized");
            return true;
        }
#endif

        case EmlStorageType::SD_SPI:
        {
            SPI.begin(EML_SD_SCK_PIN, EML_SD_MISO_PIN, EML_SD_MOSI_PIN, EML_SD_CS_PIN);
            if (!SD.begin(EML_SD_CS_PIN)) {
                eml_println("❌ SD Card mount failed");
                // Fallback to LittleFS
                if (!LittleFS.begin(true)) {
                    return false;
                }
                g_active_storage = EmlStorageType::INTERNAL_FLASH;
                return true;
            }
            g_active_storage = EmlStorageType::SD_SPI;
            eml_println("✅ SD Card initialized");
            return true;
        }

#if RF_HAS_SDMMC
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
        {
            bool use1bit = (selected == EmlStorageType::SD_MMC_1BIT);
            if (!SD_MMC.begin(EML_SDMMC_MOUNTPOINT, use1bit, EML_SDMMC_FORMAT_IF_FAIL)) {
                eml_println("❌ SD_MMC mount failed");
                // Fallback to LittleFS
                if (!LittleFS.begin(true)) {
                    return false;
                }
                g_active_storage = EmlStorageType::INTERNAL_FLASH;
                return true;
            }
            g_active_storage = selected;
            eml_println("✅ SD_MMC initialized");
            return true;
        }
#endif

        case EmlStorageType::INTERNAL_FLASH:
        default:
        {
            if (!LittleFS.begin(true)) {
                eml_println("❌ LittleFS mount failed");
                return false;
            }
            g_active_storage = EmlStorageType::INTERNAL_FLASH;
            eml_println("✅ LittleFS initialized");
            return true;
        }
    }
}

void eml_fs_deinit() {
    switch (g_active_storage) {
#if EML_ESP32_HAS_FATFS
        case EmlStorageType::INTERNAL_FAT:
            FFat.end();
            break;
#endif
#if RF_HAS_SDMMC
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
            SD_MMC.end();
            break;
#endif
        case EmlStorageType::SD_SPI:
            SD.end();
            break;
        case EmlStorageType::INTERNAL_FLASH:
        default:
            LittleFS.end();
            break;
    }
}

const char* eml_fs_storage_name() {
    switch (g_active_storage) {
        case EmlStorageType::INTERNAL_FAT: return "FATFS";
        case EmlStorageType::SD_SPI: return "SD SPI";
        case EmlStorageType::SD_MMC_1BIT: return "SD_MMC (1-bit)";
        case EmlStorageType::SD_MMC_4BIT: return "SD_MMC (4-bit)";
        case EmlStorageType::INTERNAL_FLASH:
        default:
            return "LittleFS";
    }
}

EmlStorageType eml_fs_storage_type() {
    return g_active_storage;
}

bool eml_fs_exists(const char* path) {
    fs::FS* fs = get_active_fs();
    return fs->exists(path);
}

EmlFileHandle* eml_fs_open(const char* path, EmlFileMode mode) {
    fs::FS* fs = get_active_fs();
    
    const char* fs_mode;
    switch (mode) {
        case EmlFileMode::WRITE:
            fs_mode = FILE_WRITE;
            break;
        case EmlFileMode::APPEND:
            fs_mode = FILE_APPEND;
            break;
        case EmlFileMode::READ_WRITE:
            fs_mode = FILE_WRITE;  // Arduino FS doesn't have true R/W mode
            break;
        case EmlFileMode::READ:
        default:
            fs_mode = FILE_READ;
            break;
    }
    
    File f = fs->open(path, fs_mode, (mode != EmlFileMode::READ));
    if (!f) {
        return nullptr;
    }
    
    EmlFileHandle* handle = new EmlFileHandle(f);
    return handle;
}

void eml_fs_close(EmlFileHandle* file) {
    if (!file) return;
    if (file->valid) {
        file->file.close();
    }
    delete file;
}

size_t eml_fs_read(EmlFileHandle* file, void* buffer, size_t size) {
    if (!file || !file->valid || !buffer) return 0;
    return file->file.read(static_cast<uint8_t*>(buffer), size);
}

size_t eml_fs_write(EmlFileHandle* file, const void* buffer, size_t size) {
    if (!file || !file->valid || !buffer) return 0;
    return file->file.write(static_cast<const uint8_t*>(buffer), size);
}

bool eml_fs_seek(EmlFileHandle* file, int64_t offset, EmlSeekOrigin origin) {
    if (!file || !file->valid) return false;
    
    SeekMode mode;
    switch (origin) {
        case EmlSeekOrigin::CURRENT:
            mode = SeekCur;
            break;
        case EmlSeekOrigin::END:
            mode = SeekEnd;
            break;
        case EmlSeekOrigin::BEGIN:
        default:
            mode = SeekSet;
            break;
    }
    
    return file->file.seek(static_cast<uint32_t>(offset), mode);
}

int64_t eml_fs_tell(EmlFileHandle* file) {
    if (!file || !file->valid) return -1;
    return file->file.position();
}

int64_t eml_fs_size(EmlFileHandle* file) {
    if (!file || !file->valid) return -1;
    return file->file.size();
}

void eml_fs_flush(EmlFileHandle* file) {
    if (!file || !file->valid) return;
    file->file.flush();
}

bool eml_fs_remove(const char* path) {
    fs::FS* fs = get_active_fs();
    return fs->remove(path);
}

bool eml_fs_rename(const char* old_path, const char* new_path) {
    fs::FS* fs = get_active_fs();
    return fs->rename(old_path, new_path);
}

bool eml_fs_mkdir(const char* path) {
    fs::FS* fs = get_active_fs();
    return fs->mkdir(path);
}

bool eml_fs_rmdir(const char* path) {
    fs::FS* fs = get_active_fs();
    return fs->rmdir(path);
}

uint64_t eml_fs_total_bytes() {
    switch (g_active_storage) {
#if EML_ESP32_HAS_FATFS
        case EmlStorageType::INTERNAL_FAT:
            return FFat.totalBytes();
#endif
#if RF_HAS_SDMMC
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
            return SD_MMC.totalBytes();
#endif
        case EmlStorageType::SD_SPI:
            return SD.totalBytes();
        case EmlStorageType::INTERNAL_FLASH:
        default:
            return LittleFS.totalBytes();
    }
}

uint64_t eml_fs_used_bytes() {
    switch (g_active_storage) {
#if EML_ESP32_HAS_FATFS
        case EmlStorageType::INTERNAL_FAT:
            return FFat.usedBytes();
#endif
#if RF_HAS_SDMMC
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
            return SD_MMC.usedBytes();
#endif
        case EmlStorageType::SD_SPI:
            return SD.usedBytes();
        case EmlStorageType::INTERNAL_FLASH:
        default:
            return LittleFS.usedBytes();
    }
}

size_t eml_fs_max_dataset_bytes() {
    if (eml_fs_is_sd_based()) {
        return esp32::sd_max_dataset_bytes();
    }
    return esp32::flash_max_dataset_bytes();
}

size_t eml_fs_max_infer_log_bytes() {
    if (eml_fs_is_sd_based()) {
        return esp32::sd_max_infer_log_bytes();
    }
    return esp32::flash_max_infer_log_bytes();
}

bool eml_fs_is_sd_based() {
    switch (g_active_storage) {
        case EmlStorageType::SD_SPI:
        case EmlStorageType::SD_MMC_1BIT:
        case EmlStorageType::SD_MMC_4BIT:
            return true;
        default:
            return false;
    }
}

bool eml_fs_is_flash() {
    return g_active_storage == EmlStorageType::INTERNAL_FLASH ||
           g_active_storage == EmlStorageType::INTERNAL_FAT;
}

} // namespace pal
} // namespace eml
