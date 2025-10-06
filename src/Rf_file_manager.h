#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>

#define temp_base_data           "/base_data.bin"

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


