#pragma once

#include <Arduino.h>
#include <SPIFFS.h>
#include <FS.h>

/**
 * @brief Clones a CSV file from source to destination with exact formatting preservation
 * 
 * This function creates an exact copy of a CSV file, preserving all formatting including
 * line endings and whitespace. Useful for creating backups or duplicates of data files.
 * 
 * @param src Source file path (must include leading slash, e.g., "/data.csv")
 * @param dest Destination file path (must include leading slash, e.g., "/data_copy.csv")
 * @return true if cloning was successful, false otherwise
 * 
 * @note Both files must be valid SPIFFS paths. The function will print the cloned file content.
 */
bool cloneCSVFile(const String& src, const String& dest);


/**
 * @brief Interactive SPIFFS file management interface
 * 
 * Provides a command-line interface for viewing and managing files stored in SPIFFS.
 * Users can view file listings with sizes, delete individual files, and monitor
 * available storage space. The interface runs in a loop until user types "END".
 * 
 * Features:
 * - Lists all files with sizes
 * - Shows free space available
 * - Interactive file deletion with confirmation
 * - Safe exit mechanism
 * 
 * @note Requires Serial communication for user interaction
 */
void manageSPIFFSFiles();
/**
 * @brief Prints the contents of a CSV file with summary statistics
 * 
 * Displays the complete contents of a CSV file line by line, then provides
 * a summary showing the total number of rows and columns detected.
 * Column count is determined by counting commas in the first row.
 * 
 * @param filename Path to the CSV file (e.g., "/data.csv")
 * 
 * @note File must exist in SPIFFS. Empty lines are skipped during counting.
 */
void printCSVFile(String filename);

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
String reception_data(bool exact_columns = 0, bool print_file = true);

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
void cleanMalformedRows(const String& filename, uint16_t exact_columns);

/**
 * @brief Alias for printCSVFile for backward compatibility
 * 
 * @param filename Path to the file to print
 */
inline void printFile(const String& filename) {
    printCSVFile(filename);
}

