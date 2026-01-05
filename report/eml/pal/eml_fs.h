#pragma once
/**
 * @file eml_fs.h
 * @brief Platform Abstraction Layer - Filesystem Interface
 * 
 * Declares platform-agnostic filesystem operations.
 * Each platform must implement these functions.
 */

#include <cstdint>
#include <cstddef>

namespace eml {
namespace pal {

/**
 * @brief File handle type (opaque pointer)
 */
struct EmlFileHandle;

/**
 * @brief Storage type enumeration for runtime selection
 */
enum class EmlStorageType : uint8_t {
    AUTO,           // Platform default selection
    INTERNAL_FLASH, // Internal flash (LittleFS/SPIFFS/etc.)
    INTERNAL_FAT,   // Internal flash with FAT filesystem
    SD_SPI,         // SD card over SPI
    SD_MMC_1BIT,    // SD card via MMC interface (1-bit mode)
    SD_MMC_4BIT,    // SD card via MMC interface (4-bit mode)
    HOST_FS,        // Host filesystem (POSIX/Windows)
};

/**
 * @brief File open modes
 */
enum class EmlFileMode : uint8_t {
    READ,       // Open for reading
    WRITE,      // Open for writing (truncate if exists)
    APPEND,     // Open for appending
    READ_WRITE  // Open for both reading and writing
};

/**
 * @brief File seek origin
 */
enum class EmlSeekOrigin : uint8_t {
    BEGIN,   // Seek from beginning
    CURRENT, // Seek from current position
    END      // Seek from end
};

/**
 * @brief Initialize the filesystem subsystem
 * @param type Storage type to use (AUTO = platform default)
 * @return true if initialization successful
 */
bool eml_fs_init(EmlStorageType type = EmlStorageType::AUTO);

/**
 * @brief Unmount/deinitialize the filesystem
 */
void eml_fs_deinit();

/**
 * @brief Get the name of the active storage backend
 * @return Human-readable name of active storage
 */
const char* eml_fs_storage_name();

/**
 * @brief Get the currently active storage type
 * @return Active storage type enum value
 */
EmlStorageType eml_fs_storage_type();

/**
 * @brief Check if a file or directory exists
 * @param path Path to check
 * @return true if path exists
 */
bool eml_fs_exists(const char* path);

/**
 * @brief Open a file
 * @param path File path
 * @param mode File open mode
 * @return File handle, or nullptr on failure
 */
EmlFileHandle* eml_fs_open(const char* path, EmlFileMode mode);

/**
 * @brief Close a file
 * @param file File handle to close
 */
void eml_fs_close(EmlFileHandle* file);

/**
 * @brief Read data from a file
 * @param file File handle
 * @param buffer Buffer to read into
 * @param size Maximum bytes to read
 * @return Number of bytes actually read
 */
size_t eml_fs_read(EmlFileHandle* file, void* buffer, size_t size);

/**
 * @brief Write data to a file
 * @param file File handle
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes actually written
 */
size_t eml_fs_write(EmlFileHandle* file, const void* buffer, size_t size);

/**
 * @brief Seek to a position in a file
 * @param file File handle
 * @param offset Offset to seek to
 * @param origin Seek origin
 * @return true if seek was successful
 */
bool eml_fs_seek(EmlFileHandle* file, int64_t offset, EmlSeekOrigin origin);

/**
 * @brief Get current position in a file
 * @param file File handle
 * @return Current position, or -1 on error
 */
int64_t eml_fs_tell(EmlFileHandle* file);

/**
 * @brief Get file size
 * @param file File handle
 * @return File size in bytes, or -1 on error
 */
int64_t eml_fs_size(EmlFileHandle* file);

/**
 * @brief Flush file buffers
 * @param file File handle
 */
void eml_fs_flush(EmlFileHandle* file);

/**
 * @brief Delete a file
 * @param path Path to file
 * @return true if deletion successful
 */
bool eml_fs_remove(const char* path);

/**
 * @brief Rename/move a file
 * @param old_path Current file path
 * @param new_path New file path
 * @return true if rename successful
 */
bool eml_fs_rename(const char* old_path, const char* new_path);

/**
 * @brief Create a directory
 * @param path Directory path
 * @return true if directory created or already exists
 */
bool eml_fs_mkdir(const char* path);

/**
 * @brief Remove an empty directory
 * @param path Directory path
 * @return true if removal successful
 */
bool eml_fs_rmdir(const char* path);

/**
 * @brief Get total storage size in bytes
 * @return Total storage capacity
 */
uint64_t eml_fs_total_bytes();

/**
 * @brief Get used storage in bytes
 * @return Used storage
 */
uint64_t eml_fs_used_bytes();

/**
 * @brief Get maximum dataset size supported by current storage
 * @return Maximum dataset size in bytes
 */
size_t eml_fs_max_dataset_bytes();

/**
 * @brief Get maximum inference log size supported by current storage
 * @return Maximum log size in bytes
 */
size_t eml_fs_max_infer_log_bytes();

/**
 * @brief Check if current storage is SD-based
 * @return true if SD card is active backend
 */
bool eml_fs_is_sd_based();

/**
 * @brief Check if current storage is internal flash
 * @return true if internal flash is active backend
 */
bool eml_fs_is_flash();

} // namespace pal
} // namespace eml
