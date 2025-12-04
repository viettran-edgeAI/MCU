# ESP32 File Manager Tutorial - Multi-Storage Backend Support

## Table of Contents
1. [Introduction](#introduction)
2. [Storage Backends](#storage-backends)
3. [Setup and Installation](#setup-and-installation)
4. [Quick Start Guide](#quick-start-guide)
5. [Interactive File Manager](#interactive-file-manager)
6. [Programmatic File Operations](#programmatic-file-operations)
7. [Advanced Usage](#advanced-usage)
8. [Important Notes and Best Practices](#important-notes-and-best-practices)
9. [Troubleshooting](#troubleshooting)

---

## Introduction

The **ESP32 File Manager** is a comprehensive file management library for ESP32 microcontrollers that provides both interactive terminal-based and programmatic interfaces for managing files and folders across **multiple storage backends**.

### Key Features
- ✅ **Multi-storage support**: LittleFS, FATFS, SD_MMC (1-bit/4-bit), SD SPI
- ✅ Interactive terminal-based file browser with folder navigation
- ✅ Support for multiple file formats (CSV, TXT, JSON, LOG, BIN, JPG, etc.)
- ✅ Batch operations (delete multiple files at once)
- ✅ Smart path normalization (auto-adds `/` if missing)
- ✅ Format-aware file handling (text vs binary)
- ✅ Data validation for CSV files
- ✅ Memory-efficient design for embedded systems
- ✅ Automatic fallback to LittleFS if SD mount fails
- ✅ Runtime storage backend selection

---

## Storage Backends

The library supports **5 storage backends** that can be selected at runtime:

| Storage Type | Description | Use Case |
|--------------|-------------|----------|
| `RfStorageType::FLASH` / `LITTLEFS` | Internal flash with LittleFS | Default, wear-leveling, small files |
| `RfStorageType::FATFS` / `FAT` | Internal flash with FAT filesystem | PC-compatible file access |
| `RfStorageType::SD_MMC_1BIT` | SD card via MMC (1-bit mode) | ESP32-CAM, shared pins |
| `RfStorageType::SD_MMC_4BIT` | SD card via MMC (4-bit mode) | Maximum SD performance |
| `RfStorageType::SD_SPI` / `SD` | SD card via SPI bus | Universal SD support |

### Storage Selection Example
```cpp
#include "Rf_file_manager.h"

void setup() {
    Serial.begin(115200);
    
    // Option 1: Use default (LittleFS)
    rf_storage_begin();
    
    // Option 2: Explicitly select LittleFS
    rf_storage_begin(RfStorageType::FLASH);
    
    // Option 3: Use FATFS for PC-compatible file access
    rf_storage_begin(RfStorageType::FATFS);
    
    // Option 4: Use SD card (SPI mode)
    rf_storage_begin(RfStorageType::SD_SPI);
    
    // Option 5: Use SD_MMC (1-bit mode, good for ESP32-CAM)
    rf_storage_begin(RfStorageType::SD_MMC_1BIT);
    
    // Option 6: Use SD_MMC (4-bit mode, fastest)
    rf_storage_begin(RfStorageType::SD_MMC_4BIT);
    
    // Check which storage is active
    Serial.printf("Active storage: %s\n", rf_storage_type());
}
```

### Storage Backend Comparison

| Feature | LittleFS | FATFS | SD_MMC | SD_SPI |
|---------|----------|-------|--------|--------|
| Max Size | ~1-2 MB | ~1-2 MB | 32+ GB | 32+ GB |
| Wear Leveling | ✅ Yes | ❌ No | ❌ No | ❌ No |
| PC Compatible | ❌ No | ✅ Yes | ✅ Yes | ✅ Yes |
| Speed | Medium | Medium | Fast | Slower |
| External HW | None | None | SD slot | SD + SPI |

### FATFS Setup Requirements

FATFS requires a FAT partition in your ESP32's partition table. Add this to your `partitions.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x140000,
ffat,     data, fat,     0x150000,0x2B0000,
```

Then select this partition scheme in Arduino IDE or `platformio.ini`.

---

## Setup and Installation

### Hardware Requirements
- ESP32 development board (any variant: ESP32, S2, S3, C3, C6, H2)
- USB cable for Serial connection
- (Optional) SD card module for SD storage backends

### Software Requirements
```cpp
#include <Arduino.h>
#include "Rf_file_manager.h"  // Includes all necessary storage libraries
```

### Supported Storage Libraries (Auto-included)
- `LittleFS.h` - Always available
- `FFat.h` - Available on all ESP32 variants (requires FAT partition)
- `SD_MMC.h` - Available on ESP32, S2, S3 (boards with SDMMC peripheral)
- `SD.h` + `SPI.h` - Always available for SPI-based SD cards

### Basic Initialization
```cpp
void setup() {
    Serial.begin(115200);
    
    // Initialize with default storage (LittleFS)
    if (!rf_storage_begin()) {
        Serial.println("❌ Storage Mount Failed!");
        return;
    }
    
    Serial.printf("✅ %s Initialized\n", rf_storage_type());
}
```

### Initialize with Specific Storage Backend
```cpp
void setup() {
    Serial.begin(115200);
    
    // Choose your storage backend
    RfStorageType storage = RfStorageType::FATFS;  // or FLASH, SD_SPI, SD_MMC_1BIT, etc.
    
    if (!rf_storage_begin(storage)) {
        Serial.println("❌ Storage Mount Failed!");
        // Note: SD failures auto-fallback to LittleFS
        return;
    }
    
    // Verify active storage
    Serial.printf("✅ Active: %s\n", rf_storage_type());
    Serial.printf("📊 Total: %llu bytes, Used: %llu bytes\n", 
                  rf_total_bytes(), rf_used_bytes());
}
```

### SD Card Pin Configuration (SPI Mode)
```cpp
// Default pins (can be overridden before including Rf_file_manager.h)
#define SD_CS_PIN   5
#define SD_MOSI_PIN 23
#define SD_MISO_PIN 19
#define SD_SCK_PIN  18
```

---

## Quick Start Guide

### Example 1: Interactive File Manager
```cpp
void setup() {
    Serial.begin(115200);
    rf_storage_begin();  // Use default LittleFS
}

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
    rf_storage_begin(RfStorageType::FLASH);  // Explicitly use LittleFS
    
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

### Example 3: Using FATFS for PC Compatibility
```cpp
void setup() {
    Serial.begin(115200);
    
    // Use FATFS - files can be read on PC via USB
    if (rf_storage_begin(RfStorageType::FATFS)) {
        Serial.println("✅ FATFS ready - PC compatible!");
        
        File f = rf_open("/log.txt", "w");
        f.println("Hello from ESP32!");
        f.close();
    }
}
```

### Example 4: SD Card with Auto-Fallback
```cpp
void setup() {
    Serial.begin(115200);
    
    // Try SD card, falls back to LittleFS if no card inserted
    rf_storage_begin(RfStorageType::SD_SPI);
    
    Serial.printf("Using: %s\n", rf_storage_type());
    
    if (rf_storage_is_sd_based()) {
        Serial.println("📀 SD card active - large storage available");
    } else if (rf_storage_is_flash()) {
        Serial.println("💾 Using internal flash storage");
    }
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
    
    // Use SD card for large datasets, fallback to flash if unavailable
    rf_storage_begin(RfStorageType::SD_SPI);
    Serial.printf("📦 Storage: %s\n", rf_storage_type());
    
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

### Example 2: Multi-Storage File Browser
```cpp
void loop() {
    Serial.println("\n=== Storage Selection ===");
    Serial.println("1. Internal Flash (LittleFS)");
    Serial.println("2. Internal Flash (FATFS)");
    Serial.println("3. SD Card (SPI)");
    Serial.println("4. SD Card (MMC 1-bit)");
    
    while (!Serial.available()) delay(10);
    char choice = Serial.read();
    
    RfStorageType storage;
    switch(choice) {
        case '1': storage = RfStorageType::FLASH; break;
        case '2': storage = RfStorageType::FATFS; break;
        case '3': storage = RfStorageType::SD_SPI; break;
        case '4': storage = RfStorageType::SD_MMC_1BIT; break;
        default: return;
    }
    
    if (rf_storage_begin(storage)) {
        Serial.printf("✅ Using: %s\n", rf_storage_type());
        manage_files();  // Launch file manager
        rf_storage_end();  // Clean unmount
    }
}
```

### Example 3: Storage Info Dashboard
```cpp
void printStorageInfo() {
    Serial.println("\n=== Storage Information ===");
    Serial.printf("Backend: %s\n", rf_storage_type());
    Serial.printf("Total: %llu bytes\n", rf_total_bytes());
    Serial.printf("Used: %llu bytes\n", rf_used_bytes());
    Serial.printf("Free: %llu bytes\n", rf_total_bytes() - rf_used_bytes());
    
    if (rf_storage_is_sd_based()) {
        Serial.println("Type: External SD Card");
        Serial.printf("Max dataset: %zu bytes\n", rf_storage_max_dataset_bytes());
    } else if (rf_storage_is_fatfs()) {
        Serial.println("Type: Internal Flash (FAT)");
        Serial.println("PC Compatible: Yes");
    } else {
        Serial.println("Type: Internal Flash (LittleFS)");
        Serial.println("Wear Leveling: Yes");
    }
}
```

---

## Important Notes and Best Practices

### 🔴 Critical Warnings

1. **Storage Initialization**
   - Always call `rf_storage_begin()` before any file operations
   - SD failures automatically fall back to LittleFS
   - Check `rf_storage_type()` to verify which backend is active

2. **FATFS Partition Requirement**
   - FATFS requires a FAT partition in the ESP32 flash
   - Without proper partition table, FATFS will fail to mount
   - Use `RF_FATFS_FORMAT_IF_FAIL` to auto-format on first use

3. **Stack Overflow Protection**
   - The library uses **memory-efficient streaming** to avoid stack overflow
   - Large arrays are processed in multiple passes, not stored on stack
   - Safe for ESP32's limited stack size (~8KB per task)

4. **File Paths**
   - ✅ Always use forward slashes: `/folder/file.csv`
   - ✅ Paths are case-sensitive: `/Data.csv` ≠ `/data.csv`
   - ✅ The library auto-adds leading `/` if missing

5. **Folder Deletion**
   - ⚠️ Folders must be **empty** before deletion with `rf_rmdir()`
   - Use `deleteDirectoryRecursive()` for non-empty folders
   - Or use operation 'f' in interactive mode

### 📝 Best Practices

#### 1. **Storage Selection**
```cpp
// ✅ GOOD: Select storage at startup
void setup() {
    Serial.begin(115200);
    
    // Try SD first, auto-fallback to flash
    rf_storage_begin(RfStorageType::SD_SPI);
    
    // Verify what's actually being used
    if (rf_storage_is_sd_based()) {
        Serial.println("Using SD card - large files OK");
    } else {
        Serial.println("Using flash - keep files small");
    }
}

// ✅ GOOD: Clean shutdown
void cleanup() {
    rf_storage_end();  // Properly unmount
}
```

#### 2. **Path Handling**
```cpp
// ✅ GOOD: Let the library normalize paths
String path = reception_data(0, true, "/data/");
// User enters: "file.csv" → Becomes: "/data/file.csv"

// ✅ GOOD: Use absolute paths when needed
cloneFile("/data/file.csv", "/backups/file.csv");

// ❌ AVOID: Manual path concatenation
String path = folder + filename;  // May miss slashes
```

#### 3. **Error Checking**
```cpp
// ✅ GOOD: Check operation results
if (cloneFile("/data.csv", "/backup.csv")) {
    Serial.println("✅ Backup successful");
} else {
    Serial.println("❌ Backup failed - check if file exists");
}

// ✅ GOOD: Verify file existence
if (!rf_exists("/data.csv")) {
    Serial.println("⚠️ File not found, creating new one...");
    reception_data(0, false, "/");
}
```

#### 4. **Storage-Aware File Sizes**
```cpp
// ✅ GOOD: Check storage limits
size_t maxSize = rf_storage_max_dataset_bytes();
Serial.printf("Max dataset: %zu bytes\n", maxSize);

// Flash storage: ~150KB limit
// SD storage: 5-20MB depending on PSRAM

// ✅ GOOD: Check available space before large writes
uint64_t freeSpace = rf_total_bytes() - rf_used_bytes();
if (freeSpace < requiredSize) {
    Serial.println("Not enough space!");
}
```

#### 5. **Memory Management**
```cpp
// ✅ GOOD: Process files one at a time
cloneFile("/file1.csv");
cloneFile("/file2.csv");
cloneFile("/file3.csv");

// ❌ AVOID: Creating large arrays
String files[50];  // Too much stack memory!
```

#### 6. **CSV Data Validation**
```cpp
// ✅ GOOD: Validate column count
String path = reception_data(10, false);  // Expect 10 columns
cleanMalformedRows(path, 10);  // Remove invalid rows
printFile(path);  // Verify cleaned data

// ✅ GOOD: Limit row elements (max 234)
// The library automatically enforces this limit
```

#### 7. **Interactive Mode Best Practices**
```cpp
// ✅ GOOD: Clear buffer before reading
while (Serial.available()) Serial.read();
delay(100);

// ✅ GOOD: Use isolated operation spaces
// Each operation (a, b, c, d, e, f) stays in its own loop
// Type 'end' to exit operation, 'exit' to quit manager

// ✅ GOOD: Confirm destructive operations
// Delete requires 'OK' confirmation
// Delete all requires 'CONFIRM' confirmation
// Recursive delete requires 'DELETE' confirmation
```

### ⚠️ Common Pitfalls

1. **FATFS Without Partition**
   ```cpp
   // ❌ FAILS: No FAT partition configured
   rf_storage_begin(RfStorageType::FATFS);
   
   // ✅ FIX: Add FAT partition to partitions.csv or use different scheme
   ```

2. **SD Card Pin Conflicts**
   ```cpp
   // ⚠️ ESP32-CAM: SD_MMC shares pins with camera
   // Use 1-bit mode to free some pins
   rf_storage_begin(RfStorageType::SD_MMC_1BIT);
   ```

3. **Multiple Deletions**
   ```cpp
   // ✅ NEW: Delete multiple items at once
   // Input: 1 3 5 F2
   // Or: 1,3,5,F2
   // Or: 1, 3, 5, F2 (mixed spacing works!)
   ```

4. **File Extensions**
   ```cpp
   // ✅ GOOD: Include extension
   reception_data(0, true, "/");  // User enters: "data.csv"
   
   // ⚠️ Auto-default: If no extension provided
   // User enters: "data" → Becomes: "data.csv"
   ```

5. **Binary vs Text Files**
   ```cpp
   // The library auto-detects format based on extension:
   // Text: .csv, .txt, .log, .json (line-by-line)
   // Binary: .bin, .jpg, .png, etc. (byte-by-byte)
   ```

### 🎯 Performance Tips

1. **Storage Backend Selection**
   - **LittleFS**: Best for frequent small writes (wear leveling)
   - **FATFS**: Best for PC compatibility
   - **SD_MMC 4-bit**: Fastest for large files
   - **SD_SPI**: Most compatible, slower

2. **Batch Operations**
   - Use multiple deletion: `1 3 5 7` instead of deleting one-by-one
   - Clone files in current directory (faster than cross-directory)

3. **Storage Management**
   ```cpp
   // Check available space
   Serial.printf("Free: %llu / %llu bytes\n", 
       rf_total_bytes() - rf_used_bytes(),
       rf_total_bytes());
   ```

4. **File Size Limits**
   - CSV rows: Max 234 elements per row (233 commas)
   - LittleFS/FATFS: ~1-2 MB (depends on partition)
   - SD Card: Limited by card size (32GB+)

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
3. Storage not initialized

**Solution:**
```cpp
// Check if file exists
if (!rf_exists("/data.csv")) {
    Serial.println("File not found!");
}

// Verify storage is mounted
if (!rf_storage_begin()) {
    Serial.println("Storage not mounted!");
}

// Check active storage type
Serial.printf("Active storage: %s\n", rf_storage_type());

// Check path format
String path = "/data.csv";  // ✅ Correct
String path = "data.csv";   // ❌ May fail, let library normalize it
```

---

### Problem: FATFS mount fails

**Possible causes:**
1. No FAT partition in partition table
2. Partition not formatted

**Solution:**
```cpp
// Ensure your partition table includes a FAT partition
// In Arduino IDE: Tools > Partition Scheme > "Custom" or one with FFat

// The library auto-formats on first use if RF_FATFS_FORMAT_IF_FAIL is true (default)
// You can disable this before including the header:
#define RF_FATFS_FORMAT_IF_FAIL false
#include "Rf_file_manager.h"
```

---

### Problem: SD card not detected

**Possible causes:**
1. Wrong pins configured
2. Card not inserted or damaged
3. Wrong SD mode selected

**Solution:**
```cpp
// Check pin configuration for SPI mode
#define SD_CS_PIN   5    // Chip Select
#define SD_MOSI_PIN 23   // Master Out Slave In
#define SD_MISO_PIN 19   // Master In Slave Out
#define SD_SCK_PIN  18   // Clock

// For ESP32-CAM, use SD_MMC 1-bit mode (shares pins with camera)
rf_storage_begin(RfStorageType::SD_MMC_1BIT);

// The library auto-falls back to LittleFS if SD fails
if (rf_storage_is_flash()) {
    Serial.println("SD failed, using internal flash");
}
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

// Option 3: Use recursive delete (operation 'f')
// Enter 'f' mode, select folder, type: DELETE
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

The ESP32 File Manager provides a complete solution for file management on ESP32 with **flexible storage backend support**:

### ✅ Key Advantages
- **Multi-storage:** LittleFS, FATFS, SD_MMC, SD_SPI with runtime selection
- **User-friendly:** Interactive terminal interface
- **Flexible:** Programmatic and interactive modes
- **Safe:** Confirmation dialogs for destructive operations
- **Smart:** Auto path normalization and format detection
- **Efficient:** Memory-optimized for embedded systems
- **Resilient:** Auto-fallback to LittleFS if SD fails
- **Powerful:** Batch operations, folder navigation, multi-format support

### 🎯 Use Cases
- Machine learning dataset management
- Configuration file handling
- Data logging and archival
- Image/binary file storage
- Sensor data collection
- Model deployment and backup
- PC-compatible file access (via FATFS)
- Large dataset storage (via SD card)

### 📚 API Quick Reference

#### Storage Functions
```cpp
rf_storage_begin(RfStorageType type)  // Initialize storage
rf_storage_end()                       // Unmount storage
rf_storage_type()                      // Get active storage name
rf_current_storage()                   // Get active storage enum
rf_storage_is_sd_based()              // Check if using SD
rf_storage_is_flash()                 // Check if using LittleFS
rf_storage_is_fatfs()                 // Check if using FATFS
rf_total_bytes()                      // Get total storage size
rf_used_bytes()                       // Get used storage size
```

#### File Operations
```cpp
rf_exists(path)                       // Check if file exists
rf_open(path, mode)                   // Open file
rf_remove(path)                       // Delete file
rf_rename(oldPath, newPath)           // Rename/move file
rf_mkdir(path)                        // Create directory
rf_rmdir(path)                        // Remove empty directory
```

#### High-Level Functions
```cpp
manage_files()                        // Interactive file manager
cloneFile(src, dest)                  // Copy file
renameFile(old, new)                  // Rename file
printFile(path)                       // Display file contents
deleteAllFiles()                      // Delete everything
reception_data(cols, print, dir)      // Interactive file creation
cleanMalformedRows(path, cols)        // Clean CSV data
deleteDirectoryRecursive(path)        // Delete folder with contents
```

### 📚 Further Resources
- See `Rf_file_manager.h` for complete API reference
- See `Rf_board_config.h` for board-specific settings
- Check examples in `/examples` folder
- ESP32 documentation: https://docs.espressif.com/

---

**Version:** 3.0 (December 2025)  
**Compatibility:** ESP32 (all variants: ESP32, S2, S3, C3, C6, H2)  
**Storage Backends:** LittleFS, FATFS, SD_MMC (1-bit/4-bit), SD_SPI  
**License:** See LICENSE file

For issues or questions, please refer to the repository documentation.
