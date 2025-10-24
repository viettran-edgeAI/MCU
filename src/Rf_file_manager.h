#pragma once

#include <Arduino.h>
#include <FS.h>

// Storage selection: Define RF_USE_SDCARD to use SD card, otherwise LittleFS
#ifdef RF_USE_SDCARD
    #include <SD.h>
    #include <SPI.h>
    
    // Default SD card pins for ESP32
    #ifndef SD_CS_PIN
        #define SD_CS_PIN 5
    #endif
    #ifndef SD_MOSI_PIN
        #define SD_MOSI_PIN 23
    #endif
    #ifndef SD_MISO_PIN
        #define SD_MISO_PIN 19
    #endif
    #ifndef SD_SCK_PIN
        #define SD_SCK_PIN 18
    #endif
    
    // File system macros for SD card
    #define RF_FS SD
    #define RF_FS_TYPE "SD Card"
    #define RF_FILE_READ FILE_READ
    #define RF_FILE_WRITE FILE_WRITE
#else
    #include <LittleFS.h>
    
    // File system macros for LittleFS (default)
    #define RF_FS LittleFS
    #define RF_FS_TYPE "LittleFS"
    #define RF_FILE_READ FILE_READ
    #define RF_FILE_WRITE FILE_WRITE
#endif

// Unified file operation macros
#define RF_FS_BEGIN() rf_storage_begin()
#define RF_FS_END() rf_storage_end()
#define RF_FS_EXISTS(path) RF_FS.exists(path)
#define RF_FS_OPEN(path, mode) RF_FS.open(path, mode)
#define RF_FS_REMOVE(path) RF_FS.remove(path)
#define RF_FS_RENAME(oldPath, newPath) RF_FS.rename(oldPath, newPath)
#define RF_FS_RMDIR(path) RF_FS.rmdir(path)
#define RF_FS_MKDIR(path) RF_FS.mkdir(path)
#define RF_TOTAL_BYTES() RF_FS.totalBytes()
#define RF_USED_BYTES() RF_FS.usedBytes()

// Unified input operation macros
// These allow easy switching between Serial (Arduino), stdin (PC), or other interfaces
#define RF_INPUT_AVAILABLE() Serial.available()
#define RF_INPUT_READ() Serial.read()
#define RF_INPUT_READ_LINE_UNTIL(delim) Serial.readStringUntil(delim)
#define RF_INPUT_FLUSH() Serial.flush()

/**
 * @brief Initialize the selected storage system (LittleFS or SD card)
 * 
 * Automatically initializes the storage system based on compile-time configuration.
 * For LittleFS: Uses begin(true) to format if mount fails.
 * For SD card: Initializes SPI and mounts SD card with configured pins.
 * 
 * @return true if initialization successful, false otherwise
 */
bool rf_storage_begin();

/**
 * @brief Unmount/end the storage system
 * 
 * Safely unmounts the storage system. For LittleFS, calls end().
 * For SD card, calls end() to release resources.
 */
void rf_storage_end();

/**
 * @brief Get the name of the currently active storage system
 * 
 * @return const char* "LittleFS" or "SD Card"
 */
inline const char* rf_storage_type() {
    return RF_FS_TYPE;
}

#ifndef RF_DEBUG_LEVEL
    #define RF_DEBUG_LEVEL 1
#else
    #if RF_DEBUG_LEVEL > 3
        #undef RF_DEBUG_LEVEL
        #define RF_DEBUG_LEVEL 3
    #endif
#endif

/*
 RF_DEBUG_LEVEL :
    0 : silent mode - no messages
    1 : forest messages (start, end, major events) 
    2 : messages at components level + warnings
    3 : all memory and event timing messages & detailed info
 note: all errors messages (lead to failed process) will be enabled with RF_DEBUG_LEVEL >=1
*/

#if RF_DEBUG_LEVEL > 0
    inline void rf_debug_print(const char* msg) {
        Serial.printf("%s\n", msg);
    }
    template<typename T>
    inline void rf_debug_print(const char* msg, const T& obj) {
        Serial.printf("%s", msg);
        if constexpr (std::is_floating_point_v<T>) {
            Serial.println(obj, 3);  // 3 decimal places for floats/doubles
        } else {
            Serial.println(obj);
        }
    }

    template<typename T1, typename T2>
    inline void rf_debug_print_2(const char* msg1, const T1& obj1, const char* msg2, const T2& obj2) {
        Serial.printf("%s", msg1);
        if constexpr (std::is_floating_point_v<T1>) {
            Serial.print(obj1, 3);
        } else {
            Serial.print(obj1);
        }
        Serial.printf(" %s", msg2);
        if constexpr (std::is_floating_point_v<T2>) {
            Serial.println(obj2, 3);
        } else {
            Serial.println(obj2);
        }
    }

    #define RF_DEBUG(level, ...)                        \
        do{                                              \
            if constexpr (RF_DEBUG_LEVEL > (level)) {     \
                rf_debug_print(__VA_ARGS__);               \
            }                                               \
        }while(0)

    #define RF_DEBUG_2(level, msg1, obj1, msg2, obj2)          \
        do{                                                     \
            if constexpr (RF_DEBUG_LEVEL > (level)) {            \
                rf_debug_print_2(msg1, obj1, msg2, obj2);         \
            }                                                      \
        }while(0)
#endif

/**
 * @brief Clones any file from source to destination with format-aware handling
 * 
 * This function creates an exact copy of any file type, automatically detecting
 * whether it's a text file (csv, txt, log, json) or binary file (jpg, bin, etc.)
 * and using the appropriate copying method. Text files preserve line endings,
 * while binary files are copied byte-for-byte.
 * 
 * If no destination is provided, automatically generates one by adding 'cpy' 
 * before the file extension (e.g., "/data.csv" → "/datacpy.csv").
 * 
 * @param src Source file path (must include leading slash, e.g., "/data.csv", "/image.jpg")
 * @param dest Destination file path (optional, auto-generated if empty)
 * @return true if cloning was successful, false otherwise
 * 
 * @note Supports all file formats: CSV, TXT, BIN, JPG, LOG, JSON, etc.
 */
bool cloneFile(const String& src, const String& dest = "");

/**
 * @brief Clones file with auto-generated destination name
 * 
 * @param src Source file path
 * @return true if cloning was successful, false otherwise
 */
bool cloneFile(const String& src);

/**
 * @brief Clones file with const char* parameters
 * 
 * @param src Source file path
 * @param dest Destination file path
 * @return true if cloning was successful, false otherwise
 */
bool cloneFile(const char* src, const char* dest);

/**
 * @brief Clones file with const char* source and auto-generated destination
 * 
 * @param src Source file path
 * @return true if cloning was successful, false otherwise
 */
bool cloneFile(const char* src);

/**
 * @brief Renames a file in LittleFS storage
 * 
 * Changes the name of an existing file from oldPath to newPath. Both paths
 * must be valid LittleFS paths. The operation will fail if the source file
 * doesn't exist or if the destination file already exists.
 * 
 * @param oldPath Current file path (must include leading slash, e.g., "/old_data.csv")
 * @param newPath New file path (must include leading slash, e.g., "/new_data.csv")
 * @return true if rename was successful, false otherwise
 * 
 * @note This is an atomic operation - either the file is renamed completely or not at all.
 *       The file content remains unchanged, only the filename/path changes.
 */
bool renameFile(const String& oldPath, const String& newPath);

/**
 * @brief Renames a file with const char* parameters
 * 
 * @param oldPath Current file path
 * @param newPath New file path
 * @return true if rename was successful, false otherwise
 */
bool renameFile(const char* oldPath, const char* newPath);

/**
 * @brief Interactive file manager with directory navigation for LittleFS
 * 
 * Provides a terminal-based interface for managing files and folders in LittleFS.
 * Supports directory navigation and hierarchical file organization.
 * 
 * Main Features:
 * - Lists files and folders in current directory (starts at root /)
 * - g: Navigate into folders
 * - ..: Navigate to parent directory
 * - a: Print/read file contents (CSV, TXT, JSON, LOG, BIN)
 * - b: Clone files (with auto-naming or custom destination)
 * - c: Rename files
 * - d: Delete files and folders (with confirmation)
 * - e: Create new CSV files using interactive data input
 * - Isolated operation spaces that maintain state until 'end'
 * - Real-time file/folder list refresh after modifications
 * - Safe exit mechanisms and confirmation dialogs
 * 
 * Directory Navigation:
 * - Displays folders with 📁 icon and trailing /
 * - Displays files with 📄 icon and size information
 * - Use 'g' to enter a folder by number
 * - Use '..' to go back to parent directory
 * 
 * @note Each operation mode maintains its own loop until user types 'end'.
 *       Type 'exit' in main menu to quit the file manager completely.
 *       Delete mode supports 'all' command to delete all files with confirmation.
 *       Folders can only be deleted if they are empty.
 */
void manage_files();

/**
 * @brief Prints the contents of any text file with format-aware summary statistics
 * 
 * Displays the complete contents of text files (CSV, TXT, LOG, JSON) line by line,
 * then provides a summary. For CSV files, shows row and column count. For other
 * text files, shows line count. Binary files display only size information.
 * 
 * @param filename Path to the file (e.g., "/data.csv", "/config.txt", "/image.jpg")
 * 
 * @note File must exist in LittleFS. Empty lines are skipped during counting.
 *       Binary files (JPG, BIN, etc.) show only basic file information.
 */
void printFile(String filename);

/**
 * @brief Deletes all files from LittleFS storage
 * 
 * Performs a complete cleanup of LittleFS by scanning and deleting every file.
 * Provides detailed feedback showing which files were successfully deleted
 * and which failed. Includes safety delays between operations.
 * 
 * @warning This operation is irreversible. All data will be lost.
 * 
 * @note Prints a summary of deleted vs failed files at completion
 */
void deleteAllLittleFSFiles();

// Backward compatibility alias
inline void deleteAllSPIFFSFiles() { deleteAllLittleFSFiles(); }

/**
 * @brief Interactive text data input interface (CSV/TXT/LOG/JSON)
 * 
 * Allows users to create files by entering data through the Serial interface.
 * Requires a full path with extension (e.g., "/digit_data.csv"). If the path does
 * not start with '/', it will be auto-prefixed. If no extension is provided,
 * ".csv" will be used by default for backward compatibility.
 * 
 * Behavior by format:
 * - CSV (.csv): You can enter rows separated by space or newline. Each row is
 *   limited to 234 elements (commas + 1). Optionally runs cleanMalformedRows
 *   if exact_columns > 0.
 * - TXT/LOG/JSON: Each entered line is written as-is (newline separated).
 * 
 * Features:
 * - Real-time data validation and row limiting for CSV
 * - Live feedback on saved data
 * - Automatic file display upon completion (configurable)
 * 
 * @note Type "END" on its own line to finish data entry.
 */
/**
 * @brief Interactive CSV data reception with row/column validation
 * 
 * @param exact_columns Expected column count (0 = no validation)
 * @param print_file Whether to print the file after creation
 * @param currentDir Current directory context (default="/")
 * @return String path of the created file
 */
String reception_data(int exact_columns = 0, bool print_file = true, String currentDir = "/");

/**
 * @brief Removes malformed rows from CSV files to ensure data consistency
 * 
 * Scans a CSV file and removes any rows that don't have exactly the specified
 * number of columns. Creates a temporary file during processing to ensure
 * data integrity. Useful for cleaning datasets before machine learning operations.
 * 
 * @param filename Path to the CSV file to clean
 * @param exact_columns Expected number of columns (default: 234 for MCU optimization)
 * 
 * @note Original file is replaced with cleaned version. Backup recommended before use.
 */
void cleanMalformedRows(const String& filename, int exact_columns);
/**
 * @brief Alias for printFile for backward compatibility
 * 
 * @param filename Path to the file to print
 */
inline void printCSVFile(const String& filename) {
    printFile(filename);
}


