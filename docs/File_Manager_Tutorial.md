# LittleFS File Manager Tutorial for ESP32

## Table of Contents
1. [Introduction](#introduction)
2. [Setup and Installation](#setup-and-installation)
3. [Quick Start Guide](#quick-start-guide)
4. [Interactive File Manager](#interactive-file-manager)
5. [Programmatic File Operations](#programmatic-file-operations)
6. [Advanced Usage](#advanced-usage)
7. [Important Notes and Best Practices](#important-notes-and-best-practices)
8. [Troubleshooting](#troubleshooting)

---

## Introduction

The **LittleFS File Manager** is a comprehensive file management library for ESP32 microcontrollers that provides both interactive terminal-based and programmatic interfaces for managing files and folders in LittleFS storage.

### Key Features
- ✅ Interactive terminal-based file browser with folder navigation
- ✅ Support for multiple file formats (CSV, TXT, JSON, LOG, BIN, JPG, etc.)
- ✅ Batch operations (delete multiple files at once)
- ✅ Smart path normalization (auto-adds `/` if missing)
- ✅ Format-aware file handling (text vs binary)
- ✅ Data validation for CSV files
- ✅ Memory-efficient design for embedded systems

---

## Setup and Installation

### Hardware Requirements
- ESP32 development board
- USB cable for Serial connection

### Software Requirements
```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include "Rf_file_manager.h"
```

### Basic Initialization
```cpp
void setup() {
    Serial.begin(115200);
    
    // Initialize LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed!");
        return;
    }
    
    Serial.println("✅ LittleFS Initialized");
}
```

---

## Quick Start Guide

### Example 1: Interactive File Manager
```cpp
void loop() {
    // Launch the interactive file manager
    manage_files();
    
    // After user exits, wait before restarting
    delay(5000);
}
```

### Example 2: Programmatic File Operations
```cpp
void setup() {
    Serial.begin(115200);
    LittleFS.begin(true);
    
    // Clone a file
    cloneFile("/data.csv", "/data_backup.csv");
    
    // Rename a file
    renameFile("/old_name.txt", "/new_name.txt");
    
    // Print file contents
    printFile("/config.json");
    
    // Delete all files (use with caution!)
    // deleteAllFiles();
}
```

---

## Interactive File Manager

### Launching the File Manager
Call `manage_files()` from your code to enter interactive mode:

```cpp
void loop() {
    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'm') {  // Press 'm' to open file manager
            manage_files();
        }
    }
}
```

### Main Menu Operations

#### **Directory Navigation**

1. **View Current Directory**
   - The manager starts at root (`/`) and displays the current location
   - Files shown as: `📄 1: filename.csv (123 bytes)`
   - Folders shown as: `📁 1: foldername/`

2. **Enter a Folder** (Operation: `g`)
   ```
   g
   Enter folder number (1-5): 2
   📂 Entering folder: /data/
   ```

3. **Go to Parent Directory** (Command: `..`)
   ```
   ..
   ⬆️ Moving to parent: /
   ```

#### **File Operations**

##### **a: Print File Content**
```
a
Enter file number to print, or 'end' to return: 1
📄 /data/config.txt
debug=true
mode=production
📊 Summary:
🧾 Lines: 2
```

**Supported formats:**
- **Text files** (CSV, TXT, LOG, JSON): Full content displayed
- **Binary files** (BIN, JPG): Size info only
- **CSV files**: Shows row and column count

##### **b: Clone File**
```
b
Enter source file number to clone, or 'end': 1
Enter destination filename or path (or press Enter for auto-name):
```

**Options:**
1. **Auto-naming** (press Enter):
   ```
   🔄 Auto-generated destination: /data_cpy.csv
   ✅ File cloned from /data.csv ➝ /data_cpy.csv
   ```

2. **Relative path** (just filename):
   ```
   backup.csv
   ℹ️ Resolved to: /current/dir/backup.csv
   ✅ File cloned from /data.csv ➝ /current/dir/backup.csv
   ```

3. **Absolute path** (starts with `/`):
   ```
   /backups/data_2025.csv
   ✅ File cloned from /data.csv ➝ /backups/data_2025.csv
   ```

##### **c: Rename File**
```
c
Enter file number to rename, or 'end': 1
Enter new filename or path: processed_data.csv
ℹ️ Resolved to: /current/dir/processed_data.csv
✅ File renamed from /data.csv ➝ /processed_data.csv
```

**Smart path handling:**
- Enter `newname.csv` → Auto-resolves to current directory
- Enter `/other/folder/file.csv` → Uses absolute path

##### **d: Delete Files/Folders**

**Single deletion:**
```
d
Enter item(s) to delete:
  - Single file: '3'
  - Single folder: 'F1'
  - Multiple items: '1 3 5 F2' or '1,3,5,F2'
  - 'all' to delete everything
  - 'end' to return:
3
Type 'OK' to confirm deletion: OK
✅ Deleted file: /old_data.csv
```

**Multiple deletion (NEW!):**
```
d
Enter item(s) to delete: 1 3 5 F2

📋 Items to delete (4):
  1: data1.csv
  3: data3.csv
  5: log.txt
  F2: temp/

Type 'OK' to confirm deletion: OK
🗑️ Deleting items...
✅ Deleted file: /data1.csv
✅ Deleted file: /data3.csv
✅ Deleted file: /log.txt
✅ Deleted folder: /temp/
📊 Summary: 4 deleted, 0 failed
```

**Delete all:**
```
d
Enter item(s) to delete: all
⚠️ WARNING: This will delete ALL files and folders in current directory!
Type 'CONFIRM' to proceed: CONFIRM
🗑️ Deleting all items...
✅ Cleanup complete!
```

##### **e: Add New File**
```
e
📍 Current directory: /data/
You can create .csv, .txt, .log, .json.
Enter filename or full path: sensor_data.csv
ℹ️ Resolved to: /data/sensor_data.csv
📥 Enter CSV rows (separated by space or newline). Type END to finish.
```

**CSV input example:**
```
1.2,3.4,5.6,7.8
2.3,4.5,6.7,8.9
END
🔚 END received, closing file.
✅ Saved (4 elements): 1.2,3.4,5.6,7.8
✅ Saved (4 elements): 2.3,4.5,6.7,8.9
📄 Total lines written: 2
```

**Text file input example:**
```
Enter filename or full path: notes.txt
ℹ️ Resolved to: /notes.txt
📥 Enter text lines. Press Enter for new line. Type END to finish.
This is line 1
This is line 2
END
📄 Total lines written: 2
```

---

## Programmatic File Operations

### 1. Clone Files

#### Basic Usage
```cpp
// Auto-generate destination name
cloneFile("/data.csv");
// Creates: /data_cpy.csv

// Specify destination
cloneFile("/data.csv", "/backup.csv");

// Clone to different directory
cloneFile("/data.csv", "/backups/data_2025.csv");
```

#### Format Support
```cpp
// Text files (line-by-line)
cloneFile("/config.txt", "/config_backup.txt");
cloneFile("/data.csv", "/data_backup.csv");

// Binary files (byte-by-byte)
cloneFile("/image.jpg", "/image_backup.jpg");
cloneFile("/model.bin", "/model_backup.bin");
```

### 2. Rename Files

```cpp
// Simple rename
renameFile("/old_name.csv", "/new_name.csv");

// Move to different directory
renameFile("/temp/data.csv", "/archive/data_old.csv");

// Change extension
renameFile("/data.txt", "/data.csv");
```

**⚠️ Important:** Destination file must not already exist!

### 3. Print Files

```cpp
// Print any text file
printFile("/config.txt");
printFile("/data.csv");
printFile("/logs/debug.log");

// Binary files show size only
printFile("/model.bin");  // Shows: Binary file size: 12345 bytes
```

### 4. Interactive Data Input

```cpp
// Basic CSV creation (no validation)
String filepath = reception_data();
// User enters filename, then data, then "END"

// CSV with column validation
String filepath = reception_data(4, true);
// Ensures all rows have exactly 4 columns

// Create file in specific directory
String filepath = reception_data(0, true, "/data/");
// File will be created in /data/ folder

// Create without auto-printing
String filepath = reception_data(0, false, "/");
```

### 5. Clean Malformed CSV Data

```cpp
// Remove rows that don't have exactly 10 columns
cleanMalformedRows("/dataset.csv", 10);

// Clean before processing
String filepath = reception_data(5, false);  // Create with 5 columns
cleanMalformedRows(filepath, 5);  // Clean any malformed rows
printFile(filepath);  // Display cleaned data
```

### 6. Delete All Files

```cpp
// ⚠️ DANGER: This deletes EVERYTHING!
deleteAllFiles();

// Use with confirmation
Serial.println("Type YES to delete all files:");
while (!Serial.available()) delay(10);
String confirm = Serial.readStringUntil('\n');
if (confirm.equals("YES")) {
    deleteAllFiles();
}
```

---

## Advanced Usage

### Example 1: Automated Data Pipeline
```cpp
void setup() {
    Serial.begin(115200);
    LittleFS.begin(true);
    
    // Step 1: Collect data
    Serial.println("📊 Creating dataset...");
    String dataPath = reception_data(10, false, "/data/");
    
    // Step 2: Clean data
    Serial.println("🧹 Cleaning malformed rows...");
    cleanMalformedRows(dataPath, 10);
    
    // Step 3: Create backup
    Serial.println("💾 Creating backup...");
    cloneFile(dataPath, "/backups/data_backup.csv");
    
    // Step 4: Process data (your ML code here)
    Serial.println("🤖 Processing data...");
    // ... your code ...
    
    Serial.println("✅ Pipeline complete!");
}
```

### Example 2: File Browser Menu
```cpp
void loop() {
    Serial.println("\n=== File Operations Menu ===");
    Serial.println("1. View files");
    Serial.println("2. Create CSV");
    Serial.println("3. Backup all files");
    Serial.println("4. Clean storage");
    
    while (!Serial.available()) delay(10);
    char choice = Serial.read();
    
    switch(choice) {
        case '1':
            manage_files();
            break;
        case '2':
            reception_data(0, true, "/");
            break;
        case '3':
            backupAllFiles();
            break;
        case '4':
            deleteAllFiles();
            break;
    }
}

void backupAllFiles() {
    // Custom backup logic
    cloneFile("/model.bin", "/model_backup.bin");
    cloneFile("/config.txt", "/config_backup.txt");
    Serial.println("✅ Backup complete!");
}
```

### Example 3: Smart Path Resolution
```cpp
void createFileInCurrentFolder(String filename, String folder) {
    // The library handles path normalization automatically
    String fullPath = reception_data(0, true, folder);
    
    // If user enters "data.csv", it becomes "/folder/data.csv"
    // If user enters "/other/data.csv", it stays "/other/data.csv"
    
    Serial.printf("File created at: %s\n", fullPath.c_str());
}
```

---

## Important Notes and Best Practices

### 🔴 Critical Warnings

1. **Stack Overflow Protection**
   - The library uses **memory-efficient streaming** to avoid stack overflow
   - Large arrays are processed in multiple passes, not stored on stack
   - Safe for ESP32's limited stack size (~8KB per task)

2. **File Paths**
   - ✅ Always use forward slashes: `/folder/file.csv`
   - ✅ Paths are case-sensitive: `/Data.csv` ≠ `/data.csv`
   - ✅ The library auto-adds leading `/` if missing

3. **LittleFS Initialization**
   ```cpp
   if (!LittleFS.begin(true)) {  // true = format if mount fails
       Serial.println("Failed to mount LittleFS!");
       return;
   }
   ```

4. **Folder Deletion**
   - ⚠️ Folders must be **empty** before deletion
   - Delete all files inside first, then delete the folder
   - Use "all" command to delete everything at once

### 📝 Best Practices

#### 1. **Path Handling**
```cpp
// ✅ GOOD: Let the library normalize paths
String path = reception_data(0, true, "/data/");
// User enters: "file.csv" → Becomes: "/data/file.csv"

// ✅ GOOD: Use absolute paths when needed
cloneFile("/data/file.csv", "/backups/file.csv");

// ❌ AVOID: Manual path concatenation
String path = folder + filename;  // May miss slashes
```

#### 2. **Error Checking**
```cpp
// ✅ GOOD: Check operation results
if (cloneFile("/data.csv", "/backup.csv")) {
    Serial.println("✅ Backup successful");
} else {
    Serial.println("❌ Backup failed - check if file exists");
}

// ✅ GOOD: Verify file existence
if (!LittleFS.exists("/data.csv")) {
    Serial.println("⚠️ File not found, creating new one...");
    reception_data(0, false, "/");
}
```

#### 3. **Memory Management**
```cpp
// ✅ GOOD: Process files one at a time
cloneFile("/file1.csv");
cloneFile("/file2.csv");
cloneFile("/file3.csv");

// ❌ AVOID: Creating large arrays
String files[50];  // Too much stack memory!
```

#### 4. **CSV Data Validation**
```cpp
// ✅ GOOD: Validate column count
String path = reception_data(10, false);  // Expect 10 columns
cleanMalformedRows(path, 10);  // Remove invalid rows
printFile(path);  // Verify cleaned data

// ✅ GOOD: Limit row elements (max 234)
// The library automatically enforces this limit
```

#### 5. **Interactive Mode Best Practices**
```cpp
// ✅ GOOD: Clear buffer before reading
while (Serial.available()) Serial.read();
delay(100);

// ✅ GOOD: Use isolated operation spaces
// Each operation (a, b, c, d, e) stays in its own loop
// Type 'end' to exit operation, 'exit' to quit manager

// ✅ GOOD: Confirm destructive operations
// Delete requires 'OK' confirmation
// Delete all requires 'CONFIRM' confirmation
```

### ⚠️ Common Pitfalls

1. **Timeout Issues** ❌ **FIXED!**
   - Old version: Clone had 10-second timeout
   - **New version:** No timeout - waits for user input
   
2. **Multiple Deletions**
   ```cpp
   // ✅ NEW: Delete multiple items at once
   // Input: 1 3 5 F2
   // Or: 1,3,5,F2
   // Or: 1, 3, 5, F2 (mixed spacing works!)
   ```

3. **File Extensions**
   ```cpp
   // ✅ GOOD: Include extension
   reception_data(0, true, "/");  // User enters: "data.csv"
   
   // ⚠️ Auto-default: If no extension provided
   // User enters: "data" → Becomes: "data.csv"
   ```

4. **Binary vs Text Files**
   ```cpp
   // The library auto-detects format based on extension:
   // Text: .csv, .txt, .log, .json (line-by-line)
   // Binary: .bin, .jpg, .png, etc. (byte-by-byte)
   ```

### 🎯 Performance Tips

1. **Batch Operations**
   - Use multiple deletion: `1 3 5 7` instead of deleting one-by-one
   - Clone files in current directory (faster than cross-directory)

2. **Storage Management**
   ```cpp
   // Check available space
   Serial.printf("Free: %d / %d bytes\n", 
       LittleFS.totalBytes() - LittleFS.usedBytes(),
       LittleFS.totalBytes());
   ```

3. **File Size Limits**
   - CSV rows: Max 234 elements per row (233 commas)
   - Total file size: Depends on ESP32 partition (~1-2 MB typical)

---

## Troubleshooting

### Problem: "Stack protection fault" or Guru Meditation Error

**Cause:** Stack overflow from large arrays

**Solution:** ✅ **Already fixed** in latest version!
- Library now uses streaming approach
- No large arrays on stack
- Safe for all ESP32 models

---

### Problem: "Failed to open file"

**Possible causes:**
1. File doesn't exist
2. Path is incorrect (missing `/` or wrong directory)
3. LittleFS not initialized

**Solution:**
```cpp
// Check if file exists
if (!rf_exists("/data.csv")) {  // Use runtime function instead
    Serial.println("File not found!");
}

// Verify storage is mounted
if (!RF_FS_BEGIN()) {  // Use macro instead (with optional storage mode parameter)
    Serial.println("Storage not mounted!");
}

// Better approach - specify storage at start:
#include "Rf_file_manager.h"
void setup() {
    const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("Storage not mounted!");
    }
}

// Check path format
String path = "/data.csv";  // ✅ Correct
String path = "data.csv";   // ❌ May fail, let library normalize it
```

---

### Problem: "Failed to delete folder (may not be empty)"

**Cause:** Folder contains files

**Solution:**
```cpp
// Option 1: Delete files first, then folder
// Enter 'd' mode, type: 1 2 3 F1
// This deletes files 1,2,3 then folder F1

// Option 2: Use 'all' command
// Enter 'd' mode, type: all
// Type: CONFIRM
```

---

### Problem: CSV data not saving correctly

**Possible causes:**
1. Rows have different column counts
2. Malformed data with extra/missing commas

**Solution:**
```cpp
// Use column validation
String path = reception_data(5, false);  // Expect 5 columns
cleanMalformedRows(path, 5);  // Remove bad rows
printFile(path);  // Verify result
```

---

### Problem: Serial input not responding

**Solution:**
```cpp
// Clear serial buffer
while (Serial.available()) {
    Serial.read();
}
delay(100);

// Check baud rate matches
Serial.begin(115200);  // Both ESP32 and Serial Monitor must match

// Try sending line endings: "NL" or "CR+NL"
```

---

### Problem: File shows wrong size or corrupted

**Possible causes:**
1. File not closed properly
2. Binary file treated as text (or vice versa)

**Solution:**
```cpp
// Ensure files are closed
File f = LittleFS.open("/data.bin", FILE_WRITE);
f.write(data, size);
f.close();  // ✅ Always close!

// Use correct clone method (library handles this automatically)
cloneFile("/image.jpg", "/image_copy.jpg");  // Auto-detects binary
```

---

## Summary

The LittleFS File Manager provides a complete solution for file management on ESP32:

### ✅ Key Advantages
- **User-friendly:** Interactive terminal interface
- **Flexible:** Programmatic and interactive modes
- **Safe:** Confirmation dialogs for destructive operations
- **Smart:** Auto path normalization and format detection
- **Efficient:** Memory-optimized for embedded systems
- **Powerful:** Batch operations, folder navigation, multi-format support

### 🎯 Use Cases
- Machine learning dataset management
- Configuration file handling
- Data logging and archival
- Image/binary file storage
- Sensor data collection
- Model deployment and backup

### 📚 Further Resources
- See `Rf_file_manager.h` for complete API reference
- Check examples in `/examples` folder
- ESP32 LittleFS documentation: https://docs.espressif.com/

---

**Version:** 2.0 (October 2025)
**Compatibility:** ESP32 (all variants)
**License:** See LICENSE file

For issues or questions, please refer to the repository documentation.
