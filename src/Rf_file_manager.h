#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <FS.h>

#define base_data_file           "/base_data.bin"
#define memory_log_file          "/rf_memory_log.csv"
// #define rf_ctg_file             "/rf_categorizer.bin"
// #define inference_log           "/rf_inference_log.bin"
// #define rf_config_file          "/rf_esp32_config.json"
// #define node_predictor_log      "/rf_tree_log.csv"
// #define node_predictor_file     "/node_predictor.bin"


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
 * @brief Renames a file in SPIFFS storage
 * 
 * Changes the name of an existing file from oldPath to newPath. Both paths
 * must be valid SPIFFS paths. The operation will fail if the source file
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
 * @brief Interactive SPIFFS file management interface with isolated operation modes
 * 
 * Provides a comprehensive command-line interface for managing files in SPIFFS.
 * Each operation runs in its own isolated space with dedicated menus and loops.
 * Users can perform multiple operations within each mode before returning to main menu.
 * 
 * Features:
 * - a: Print file content with repeated file selection
 * - b: Clone files with source selection and destination input
 * - c: Rename files with file selection and new name input
 * - d: Delete individual files or all files ('all' option)
 * - e: Create new CSV files using interactive data input
 * - Isolated operation spaces that maintain state until 'end'
 * - Real-time file list refresh after modifications
 * - Safe exit mechanisms and confirmation dialogs
 * 
 * @note Each operation mode maintains its own loop until user types 'end'.
 *       Type 'exit' in main menu to quit the file manager completely.
 *       Delete mode supports 'all' command to delete all files with confirmation.
 */
void manageSPIFFSFiles();
/**
 * @brief Prints the contents of any text file with format-aware summary statistics
 * 
 * Displays the complete contents of text files (CSV, TXT, LOG, JSON) line by line,
 * then provides a summary. For CSV files, shows row and column count. For other
 * text files, shows line count. Binary files display only size information.
 * 
 * @param filename Path to the file (e.g., "/data.csv", "/config.txt", "/image.jpg")
 * 
 * @note File must exist in SPIFFS. Empty lines are skipped during counting.
 *       Binary files (JPG, BIN, etc.) show only basic file information.
 */
void printFile(String filename);

/**
 * @brief Deletes all files from SPIFFS storage
 * 
 * Performs a complete cleanup of SPIFFS by scanning and deleting every file.
 * Provides detailed feedback showing which files were successfully deleted
 * and which failed. Includes safety delays between operations.
 * 
 * @warning This operation is irreversible. All data will be lost.
 * 
 * @note Prints a summary of deleted vs failed files at completion
 */
void deleteAllSPIFFSFiles();

/**
 * @brief Interactive CSV data input interface
 * 
 * Allows users to create CSV files by entering data through Serial interface.
 * Data can be entered as space-separated or newline-separated values.
 * Automatically limits rows to 234 elements maximum for memory efficiency.
 * 
 * Features:
 * - Interactive filename input (extension added automatically)
 * - Real-time data validation and row limiting
 * - Live feedback on saved data
 * - Automatic file display upon completion
 * 
 * @note Type "END" to finish data entry. Files are saved with .csv extension.
 */
String reception_data(int exact_columns = 0, bool print_file = true);

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


