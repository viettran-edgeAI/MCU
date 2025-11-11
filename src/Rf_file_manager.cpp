#include "Rf_file_manager.h"

// Track which storage system is actually active at runtime
static RfStorageType g_active_storage = RfStorageType::AUTO;

bool rf_storage_begin(RfStorageType type) {
    // Determine which storage to use
    RfStorageType selected = type;
    if (selected == RfStorageType::AUTO) {
        // Use compile-time configuration
#ifdef RF_USE_SDCARD
    #ifdef RF_USE_SDSPI
        selected = RfStorageType::SD_SPI;
    #else
        selected = RfStorageType::SD_MMC;
    #endif
#else
        selected = RfStorageType::LITTLEFS;
#endif
    }
    
    g_active_storage = selected;
    
    // Initialize based on selected storage type
    switch (selected) {
        case RfStorageType::SD_SPI:
            {
                // Initialize SPI with custom pins
                SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
                
                if (!SD.begin(SD_CS_PIN)) {
                    RF_DEBUG(0, "❌ SD Card Mount Failed!");
                    return false;
                }
                
                uint8_t cardType = SD.cardType();
                if (cardType == CARD_NONE) {
                    RF_DEBUG(0, "❌ No SD card attached!");
                    return false;
                }
                
                RF_DEBUG(1, "✅ SD Card initialized successfully");
                
                // Print card info at debug level 1+
                if (RF_DEBUG_LEVEL >= 1) {
                    const char* cardTypeStr = "UNKNOWN";
                    if (cardType == CARD_MMC) cardTypeStr = "MMC";
                    else if (cardType == CARD_SD) cardTypeStr = "SDSC";
                    else if (cardType == CARD_SDHC) cardTypeStr = "SDHC";
                    
                    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), "📊 SD Card Type: %s, Size: %llu MB", cardTypeStr, cardSize);
                    RF_DEBUG(0, "", buffer);
                }
                
                return true;
            }
            
        case RfStorageType::SD_MMC:
            {
#if RF_HAS_SDMMC
                // SD_MMC mode (default for RF_USE_SDCARD)
                bool mounted = SD_MMC.begin(RF_SDMMC_MOUNTPOINT, RF_SDMMC_MODE_1BIT, RF_SDMMC_FORMAT_IF_FAIL);
                if (!mounted) {
                    RF_DEBUG(0, RF_SDMMC_FORMAT_IF_FAIL ? "❌ SD_MMC mount failed (format attempted)." : "❌ SD_MMC Mount Failed!");
                    return false;
                }

                uint8_t cardType = SD_MMC.cardType();
                if (cardType == CARD_NONE) {
                    RF_DEBUG(0, "❌ No SD card attached!");
                    return false;
                }

                // RF_DEBUG(1, "✅ SD_MMC initialized successfully");

                if (RF_DEBUG_LEVEL >= 1) {
                    const char* cardTypeStr = "UNKNOWN";
                    if (cardType == CARD_MMC) cardTypeStr = "MMC";
                    else if (cardType == CARD_SD) cardTypeStr = "SDSC";
                    else if (cardType == CARD_SDHC) cardTypeStr = "SDHC";

                    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), "📊 SD_MMC Type: %s, Size: %llu MB", cardTypeStr, cardSize);
                    RF_DEBUG(0, "", buffer);

                    RF_DEBUG(0, RF_SDMMC_MODE_1BIT ? "ℹ️ SD_MMC running in 1-bit mode" : "ℹ️ SD_MMC running in 4-bit mode");
                }

                return true;
#else
                RF_DEBUG(0, "❌ SD_MMC not available on this platform");
                return false;
#endif
            }
            
        case RfStorageType::LITTLEFS:
        default:
            {
                if (!LittleFS.begin(true)) {
                    RF_DEBUG(0, "❌ LittleFS Mount Failed!");
                    return false;
                }
                // RF_DEBUG(1, "✅ LittleFS initialized successfully");
                return true;
            }
    }
}

void rf_storage_end() {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            SD.end();
            RF_DEBUG(1, "✅ SD Card unmounted");
            break;
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            SD_MMC.end();
            RF_DEBUG(1, "✅ SD_MMC unmounted");
#endif
            break;
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            LittleFS.end();
            RF_DEBUG(1, "✅ LittleFS unmounted");
            break;
    }
}

// Runtime file system operations
bool rf_mkdir(const char* path) {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.mkdir(path);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.mkdir(path);
#else
            return false;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.mkdir(path);
    }
}

bool rf_exists(const char* path) {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.exists(path);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.exists(path);
#else
            return false;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.exists(path);
    }
}

bool rf_remove(const char* path) {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.remove(path);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.remove(path);
#else
            return false;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.remove(path);
    }
}

bool rf_rename(const char* oldPath, const char* newPath) {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.rename(oldPath, newPath);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.rename(oldPath, newPath);
#else
            return false;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.rename(oldPath, newPath);
    }
}

bool rf_rmdir(const char* path) {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.rmdir(path);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.rmdir(path);
#else
            return false;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.rmdir(path);
    }
}

File rf_open(const char* path, const char* mode) {
    bool needsCreate = false;
    if (mode != nullptr) {
        for (const char* p = mode; *p != '\0'; ++p) {
            if (*p == 'w' || *p == 'a' || *p == '+') {
                needsCreate = true;
                break;
            }
        }
    }

    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.open(path, mode);
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.open(path, mode);
#else
            return File();
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            if (needsCreate) {
                return LittleFS.open(path, mode, true);
            }
            return LittleFS.open(path, mode);
    }
}

size_t rf_total_bytes() {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.totalBytes();
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.totalBytes();
#else
            return 0;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.totalBytes();
    }
}

size_t rf_used_bytes() {
    switch (g_active_storage) {
        case RfStorageType::SD_SPI:
            return SD.usedBytes();
        case RfStorageType::SD_MMC:
#if RF_HAS_SDMMC
            return SD_MMC.usedBytes();
#else
            return 0;
#endif
        case RfStorageType::LITTLEFS:
        case RfStorageType::AUTO:
        default:
            return LittleFS.usedBytes();
    }
}

// Helper function to normalize file paths
// Ensures path starts with '/' and is properly formatted
String normalizePath(const String& input, const String& currentDir) {
    String path = input;
    path.trim();
    
    // If empty, return as-is (for auto-generation cases)
    if (path.length() == 0) {
        return path;
    }
    
    // If already starts with '/', it's an absolute path
    if (path.startsWith("/")) {
        return path;
    }
    
    // Relative path - prepend current directory
    if (currentDir == "/") {
        return "/" + path;
    } else {
        // Ensure currentDir ends with '/'
        if (currentDir.endsWith("/")) {
            return currentDir + path;
        } else {
            return currentDir + "/" + path;
        }
    }
}

bool cloneFile(const String& src, const String& dest) {
    if(RF_FS_EXISTS(src) == false) {
        RF_DEBUG(0, "❌ Source file does not exist: ", src);
        return false;
    }

    String actualDest = dest;
    
    // Auto-generate destination if not provided
    if (dest.length() == 0) {
        int lastDot = src.lastIndexOf('.');
        if (lastDot > 0) {
            String baseName = src.substring(0, lastDot);
            String extension = src.substring(lastDot);
            actualDest = baseName + "_cpy" + extension;
        } else {
            actualDest = src + "_cpy";
        }
        RF_DEBUG(0, "🔄 Auto-generated destination: ", actualDest);
    }

    File sourceFile = RF_FS_OPEN(src, RF_FILE_READ);
    if (!sourceFile) {
        RF_DEBUG(0, "❌ Failed to open source file: ", src);
        return false;
    }

    File destFile = RF_FS_OPEN(actualDest, RF_FILE_WRITE);
    if (!destFile) {
        RF_DEBUG(0, "❌ Failed to create destination file: ", actualDest);
        sourceFile.close();
        return false;
    }

    // Get file extension to determine if it's a text or binary file
    String srcLower = src;
    srcLower.toLowerCase();
    bool isTextFile = srcLower.endsWith(".csv") || srcLower.endsWith(".txt") || 
                      srcLower.endsWith(".log") || srcLower.endsWith(".json");

    if (isTextFile) {
        // Handle text files line by line to preserve formatting
        while (sourceFile.available()) {
            String line = sourceFile.readStringUntil('\n');
            destFile.print(line);
            destFile.print('\n');
        }
    } else {
        // Handle binary files (jpg, bin, etc.) byte by byte
        uint8_t buffer[512]; // 512-byte buffer for efficiency
        while (sourceFile.available()) {
            size_t bytesRead = sourceFile.readBytes((char*)buffer, sizeof(buffer));
            destFile.write(buffer, bytesRead);
        }
    }

    sourceFile.close();
    destFile.close();

    RF_DEBUG_2(0, "✅ File cloned from ", src, "➝ ", actualDest);
    return true;
}

// Overloads for const char* compatibility
bool cloneFile(const char* src, const char* dest) {
    return cloneFile(String(src), String(dest));
}

bool cloneFile(const char* src) {
    return cloneFile(String(src), String(""));
}

bool cloneFile(const String& src) {
    return cloneFile(src, String(""));
}

bool renameFile(const String& oldPath, const String& newPath) {
    // Check if source file exists
    if (!RF_FS_EXISTS(oldPath)) {
        RF_DEBUG(0, "❌ Source file does not exist: ", oldPath);
        return false;
    }

    // Check if destination already exists
    if (RF_FS_EXISTS(newPath)) {
        RF_DEBUG(0, "❌ Destination file already exists: ", newPath);
        return false;
    }

    // Perform the rename operation
    if (RF_FS_RENAME(oldPath, newPath)) {
        RF_DEBUG_2(0, "✅ File renamed from ", oldPath, "➝ ", newPath);
        return true;
    } else {
        RF_DEBUG_2(0, "❌ Failed to rename file from ", oldPath, "to ", newPath);
        return false;
    }
}

// Overload for const char* compatibility
bool renameFile(const char* oldPath, const char* newPath) {
    return renameFile(String(oldPath), String(newPath));
}

void printFile(String filename) {
    File file = RF_FS_OPEN(filename.c_str(), RF_FILE_READ);
    if (!file) {
        RF_DEBUG(0, "❌ Failed to open file: ", filename);
        return;
    }
    RF_DEBUG(0, "📄 Printing file: ", filename);

    // Get file extension to determine file type
    String filenameLower = filename;
    filenameLower.toLowerCase();
    bool isCSV = filenameLower.endsWith(".csv");
    bool isTextFile = isCSV || filenameLower.endsWith(".txt") || 
                      filenameLower.endsWith(".log") || filenameLower.endsWith(".json");

    if (!isTextFile) {
        // Handle binary files - just show basic info
        size_t fileSize = file.size();;
        RF_DEBUG(0, "📊 Binary file size (bytes): ", fileSize);
        RF_DEBUG(0, "⚠️ Binary content not displayed");
        file.close();
        return;
    }

    uint16_t rowCount = 0;
    uint16_t columnCount = 0;
    bool columnCounted = false;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        RF_DEBUG(0, "", line);
        ++rowCount;

        // Only count columns for CSV files
        if (isCSV && !columnCounted) {
            // Count columns as number of commas + 1
            for (uint16_t i = 0; i < line.length(); ++i) {
                if (line.charAt(i) == ',') ++columnCount;
            }
            columnCount += 1;  // one more than the number of commas
            columnCounted = true;
        }
    }

    file.close();

    RF_DEBUG(0, "📊 Summary:");
    RF_DEBUG(0, "🧾 Lines: ", rowCount);

    if (isCSV) {
        RF_DEBUG(0, "📐 Columns: ", columnCount);
    }
}


void manage_files() {
    if (!RF_FS_BEGIN()) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "❌ %s Mount Failed!", RF_FS_TYPE);
        RF_DEBUG(0, "", buffer);
        return;
    }

    String currentDir = "/";  // Start at root

    while (true) {
        char header[64];
        snprintf(header, sizeof(header), "\n====== 📂 %s File Manager ======", RF_FS_TYPE);
        RF_DEBUG(0, "", header);
        RF_DEBUG(0, "📍 Current Directory: ", currentDir);
        
        File dir = RF_FS_OPEN(currentDir, RF_FILE_READ);
        if (!dir || !dir.isDirectory()) {
            RF_DEBUG(0, "❌ Failed to open directory: ", currentDir);
            currentDir = "/";  // Reset to root
            continue;
        }

    // NOTE: Use static storage to avoid large stack frames on ESP32 loopTask
        // Each String is a small object; 100 of them can consume several KB on stack.
        // Keeping them static prevents stack overflows seen with default 8KB loop stack.
        static String fileList[50];   // List of file paths
        static String folderList[50]; // List of folder paths
        int fileCount = 0;
        int folderCount = 0;

        RF_DEBUG_2(0, "📦 Free Space: ", 
                    RF_TOTAL_BYTES() - RF_USED_BYTES(), "/", RF_TOTAL_BYTES());

        // List directories first, then files
        File entry = dir.openNextFile();
        while (entry && (fileCount + folderCount) < 50) {
            String name = String(entry.name());
            
            // Construct full path
            String fullPath;
            
            // If entry.name() doesn't start with /, it's relative to currentDir
            if (!name.startsWith("/")) {
                if (currentDir == "/") {
                    fullPath = "/" + name;
                } else {
                    fullPath = currentDir + name;
                }
            } else {
                // Already absolute path
                fullPath = name;
            }
            
            // Get just the name (last component of path) for display
            int lastSlash = fullPath.lastIndexOf('/');
            String displayName = (lastSlash >= 0) ? fullPath.substring(lastSlash + 1) : fullPath;
            
            // Skip empty names
            if (displayName.length() == 0) {
                entry.close();
                entry = dir.openNextFile();
                continue;
            }
            
            if (entry.isDirectory()) {
                folderList[folderCount] = fullPath;
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "📁 %2d: %s/", folderCount + 1, displayName.c_str());
                RF_DEBUG(0, "", buffer);
                folderCount++;
            } else {
                fileList[fileCount] = fullPath;
                size_t fileSize = entry.size();
                char buffer[80];
                snprintf(buffer, sizeof(buffer), "📄 %2d: %-30s (%d bytes)", fileCount + 1, displayName.c_str(), fileSize);
                RF_DEBUG(0, "", buffer);
                fileCount++;
            }
            
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();

        if (fileCount == 0 && folderCount == 0) {
            RF_DEBUG(0, "⚠️ Directory is empty.");
        }

        RF_DEBUG(0, "\n📋 Operations:");
        if (currentDir != "/") {
            RF_DEBUG(0, "..: ⬆️  Go to parent directory");
        }
        RF_DEBUG(0, "g: 📂 Go into folder (1-", String(folderCount) + ")");
        RF_DEBUG(0, "a: 📄 Print file content");
        RF_DEBUG(0, "b: 📋 Clone file");
        RF_DEBUG(0, "c: ✏️  Rename file");
        RF_DEBUG(0, "d: 🗑️  Delete file/folder");
        RF_DEBUG(0, "e: ➕ Add new file");
        RF_DEBUG(0, "Type operation letter, or 'exit' to quit:");

        String operation = "";
        while (operation.length() == 0) {
            if (RF_INPUT_AVAILABLE()) {
                operation = RF_INPUT_READ_LINE_UNTIL('\n');
                operation.trim();
                operation.toLowerCase();
            }
            delay(10);
        }

        if (operation.equals("exit")) {
            RF_DEBUG(0, "🔚 Exiting file manager.");
            break;
        }

        // Navigate to parent directory
        if (operation.equals("..")) {
            if (currentDir != "/") {
                int lastSlash = currentDir.lastIndexOf('/', currentDir.length() - 2);
                if (lastSlash > 0) {
                    currentDir = currentDir.substring(0, lastSlash + 1);
                } else {
                    currentDir = "/";
                }
                char buffer[128];
                snprintf(buffer, sizeof(buffer), "⬆️ Moving to parent: %s", currentDir.c_str());
                RF_DEBUG(0, "", buffer);
            } else {
                RF_DEBUG(0, "⚠️ Already at root directory.");
            }
            continue;
        }

        // Go into folder
        if (operation.equals("g")) {
            if (folderCount == 0) {
                RF_DEBUG(0, "⚠️ No folders in current directory.");
                continue;
            }
            
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Enter folder number (1-%d): ", folderCount);
            RF_DEBUG(0, "", buffer);
            String input = "";
            while (input.length() == 0) {
                if (RF_INPUT_AVAILABLE()) {
                    input = RF_INPUT_READ_LINE_UNTIL('\n');
                    input.trim();
                }
                delay(10);
            }
            
            int index = input.toInt();
            if (index >= 1 && index <= folderCount) {
                currentDir = folderList[index - 1];
                
                // Ensure path starts with /
                if (!currentDir.startsWith("/")) {
                    currentDir = "/" + currentDir;
                }
                
                // Ensure path ends with /
                if (!currentDir.endsWith("/")) {
                    currentDir += "/";
                }
                
                char msg[128];
                snprintf(msg, sizeof(msg), "📂 Entering folder: %s", currentDir.c_str());
                RF_DEBUG(0, "", msg);
            } else {
                RF_DEBUG(0, "⚠️ Invalid folder number.");
            }
            continue;
        }

        if (operation.equals("a")) {
            // Print file operation - isolated space
            RF_DEBUG(0, "\n========== 📄 PRINT FILE MODE ==========");
            while (true) {
                RF_DEBUG(0, "\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    char buffer[80];
                    snprintf(buffer, sizeof(buffer), "%2d: %s", i + 1, fileList[i].c_str());
                    RF_DEBUG(0, "", buffer);
                }
                RF_DEBUG(0, "\nEnter file number to print, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (RF_INPUT_AVAILABLE()) {
                        input = RF_INPUT_READ_LINE_UNTIL('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    RF_DEBUG(0, "🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    printFile(fileList[index - 1]);
                } else {
                    RF_DEBUG(0, "⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("b")) {
            // Clone file operation - isolated space
            RF_DEBUG(0, "\n========== 📋 CLONE FILE MODE ==========");
            while (true) {
                RF_DEBUG(0, "\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    char buffer[80];
                    snprintf(buffer, sizeof(buffer), "%2d: %s", i + 1, fileList[i].c_str());
                    RF_DEBUG(0, "", buffer);
                }
                RF_DEBUG(0, "\nEnter source file number to clone, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (RF_INPUT_AVAILABLE()) {
                        input = RF_INPUT_READ_LINE_UNTIL('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    RF_DEBUG(0, "🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    RF_DEBUG(0, "Enter destination filename or path (or press Enter for auto-name):");
                    String dest = "";
                    while (dest.length() == 0) {
                        if (RF_INPUT_AVAILABLE()) {
                            dest = RF_INPUT_READ_LINE_UNTIL('\n');
                            dest.trim();
                            break;
                        }
                        delay(10);
                    }
                    // Normalize the destination path
                    if (dest.length() > 0) {
                        dest = normalizePath(dest, currentDir);
                    }
                    cloneFile(fileList[index - 1], dest);
                    delay(100); // Short delay to avoid flooding the output
                } else {
                    RF_DEBUG(0, "⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("c")) {
            // Rename file operation - isolated space
            RF_DEBUG(0, "\n========== ✏️ RENAME FILE MODE ==========");
            while (true) {
                RF_DEBUG(0, "\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    char buffer[80];
                    snprintf(buffer, sizeof(buffer), "%2d: %s", i + 1, fileList[i].c_str());
                    RF_DEBUG(0, "", buffer);
                }
                RF_DEBUG(0, "\nEnter file number to rename, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (RF_INPUT_AVAILABLE()) {
                        input = RF_INPUT_READ_LINE_UNTIL('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    RF_DEBUG(0, "🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    RF_DEBUG(0, "Enter new filename or path:");
                    String newPath = "";
                    while (newPath.length() == 0) {
                        if (RF_INPUT_AVAILABLE()) {
                            newPath = RF_INPUT_READ_LINE_UNTIL('\n');
                            newPath.trim();
                        }
                        delay(10);
                    }
                    if (newPath.length() > 0) {
                        // Normalize the path
                        newPath = normalizePath(newPath, currentDir);
                        if (renameFile(fileList[index - 1], newPath)) {
                            RF_DEBUG(0, "✅ File renamed successfully! You can rename more files or type 'end' to exit.");
                            // Update the specific file in the list for immediate reflection
                            fileList[index - 1] = newPath;
                        }
                    }
                } else {
                    RF_DEBUG(0, "⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("d")) {
            // Delete file/folder operation - isolated space with multiple item support
            RF_DEBUG(0, "\n========== 🗑️ DELETE MODE ==========");
            while (true) {
                // Refresh file and folder list each time to show current state
                File refreshDir = LittleFS.open(currentDir);
                if (!refreshDir || !refreshDir.isDirectory()) {
                    RF_DEBUG(0, "❌ Failed to refresh directory!");
                    break;
                }
                
                // Also make refresh lists static to avoid inner-scope stack spikes
                static String refreshFileList[50];
                static String refreshFolderList[50];
                int refreshFileCount = 0;
                int refreshFolderCount = 0;
                
                File refreshEntry = refreshDir.openNextFile();
                while (refreshEntry && (refreshFileCount + refreshFolderCount) < 50) {
                    String name = String(refreshEntry.name());
                    
                    // Construct full path
                    String fullPath;
                    
                    // If entry.name() doesn't start with /, it's relative to currentDir
                    if (!name.startsWith("/")) {
                        if (currentDir == "/") {
                            fullPath = "/" + name;
                        } else {
                            fullPath = currentDir + name;
                        }
                    } else {
                        // Already absolute path
                        fullPath = name;
                    }
                    
                    int lastSlash = fullPath.lastIndexOf('/');
                    String displayName = (lastSlash >= 0) ? fullPath.substring(lastSlash + 1) : fullPath;
                    
                    if (displayName.length() == 0) {
                        refreshEntry.close();
                        refreshEntry = refreshDir.openNextFile();
                        continue;
                    }
                    
                    if (refreshEntry.isDirectory()) {
                        refreshFolderList[refreshFolderCount] = fullPath;
                        refreshFolderCount++;
                    } else {
                        refreshFileList[refreshFileCount] = fullPath;
                        refreshFileCount++;
                    }
                    
                    refreshEntry.close();
                    refreshEntry = refreshDir.openNextFile();
                }
                refreshDir.close();
                
                RF_DEBUG(0, "\n📂 Available folders:");
                if (refreshFolderCount == 0) {
                    RF_DEBUG(0, "  (none)");
                } else {
                    for (int i = 0; i < refreshFolderCount; i++) {
                        int lastSlash = refreshFolderList[i].lastIndexOf('/');
                        String displayName = (lastSlash >= 0) ? refreshFolderList[i].substring(lastSlash + 1) : refreshFolderList[i];
                        char buffer[80];
                        snprintf(buffer, sizeof(buffer), "  F%d: %s/", i + 1, displayName.c_str());
                        RF_DEBUG(0, "", buffer);
                    }
                }
                
                RF_DEBUG(0, "\n📄 Available files:");
                if (refreshFileCount == 0) {
                    RF_DEBUG(0, "  (none)");
                } else {
                    for (int i = 0; i < refreshFileCount; i++) {
                        int lastSlash = refreshFileList[i].lastIndexOf('/');
                        String displayName = (lastSlash >= 0) ? refreshFileList[i].substring(lastSlash + 1) : refreshFileList[i];
                        char buffer[80];
                        snprintf(buffer, sizeof(buffer), "  %d: %s", i + 1, displayName.c_str());
                        RF_DEBUG(0, "", buffer);
                    }
                }
                
                if (refreshFileCount == 0 && refreshFolderCount == 0) {
                    RF_DEBUG(0, "⚠️ No files or folders to delete. Returning to main menu...");
                    break;
                }
                
                RF_DEBUG(0, "\nEnter item(s) to delete:");
                RF_DEBUG(0, "  - Single file: '3'");
                RF_DEBUG(0, "  - Single folder: 'F1'");
                RF_DEBUG(0, "  - Multiple items: '1 3 5 F2' or '1,3,5,F2'");
                RF_DEBUG(0, "  - 'all' to delete everything");
                RF_DEBUG(0, "  - 'end' to return:");
                
                String input = "";
                while (input.length() == 0) {
                    if (RF_INPUT_AVAILABLE()) {
                        input = RF_INPUT_READ_LINE_UNTIL('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    RF_DEBUG(0, "🔙 Returning to main menu...");
                    break;
                }
                
                if (input.equalsIgnoreCase("all")) {
                    RF_DEBUG(0, "⚠️ WARNING: This will delete ALL files and folders in current directory!");
                    RF_DEBUG(0, "Type 'CONFIRM' to proceed or anything else to cancel:");
                    String confirm = "";
                    while (confirm.length() == 0) {
                        if (RF_INPUT_AVAILABLE()) {
                            confirm = RF_INPUT_READ_LINE_UNTIL('\n');
                            confirm.trim();
                        }
                        delay(10);
                    }
                    if (confirm.equals("CONFIRM")) {
                        RF_DEBUG(0, "🗑️ Deleting all items...");
                        
                        // Delete all files first
                        for (int i = 0; i < refreshFileCount; i++) {
                            if (RF_FS_REMOVE(refreshFileList[i])) {
                                char buffer[128];
                                snprintf(buffer, sizeof(buffer), "✅ Deleted file: %s", refreshFileList[i].c_str());
                                RF_DEBUG(0, "", buffer);
                            } else {
                                char buffer[128];
                                snprintf(buffer, sizeof(buffer), "❌ Failed to delete file: %s", refreshFileList[i].c_str());
                                RF_DEBUG(0, "", buffer);
                            }
                            delay(50);
                        }
                        
                        // Then delete all folders
                        for (int i = 0; i < refreshFolderCount; i++) {
                            if (RF_FS_RMDIR(refreshFolderList[i])) {
                                char buffer[128];
                                snprintf(buffer, sizeof(buffer), "✅ Deleted folder: %s", refreshFolderList[i].c_str());
                                RF_DEBUG(0, "", buffer);
                            } else {
                                char buffer[128];
                                snprintf(buffer, sizeof(buffer), "❌ Failed to delete folder (may not be empty): %s", refreshFolderList[i].c_str());
                                RF_DEBUG(0, "", buffer);
                            }
                            delay(50);
                        }
                        
                        RF_DEBUG(0, "✅ Cleanup complete!");
                    } else {
                        RF_DEBUG(0, "❎ Delete all operation canceled.");
                    }
                    continue;
                }
                
                // Parse multiple items (space or comma separated)
                // Replace commas with spaces for uniform parsing
                input.replace(",", " ");
                
                // Process items one token at a time to minimize memory usage
                int startPos = 0;
                int itemCount = 0;
                
                // First pass: count and validate items
                String tempInput = input; // Make a copy for first pass
                int tempStart = 0;
                while (tempStart < tempInput.length()) {
                    // Skip whitespace
                    while (tempStart < tempInput.length() && tempInput.charAt(tempStart) == ' ') {
                        tempStart++;
                    }
                    if (tempStart >= tempInput.length()) break;
                    
                    // Find end of token
                    int tempEnd = tempStart;
                    while (tempEnd < tempInput.length() && tempInput.charAt(tempEnd) != ' ') {
                        tempEnd++;
                    }
                    
                    String token = tempInput.substring(tempStart, tempEnd);
                    token.trim();
                    
                    if (token.length() > 0) {
                        // Validate token
                        if (token.charAt(0) == 'F' || token.charAt(0) == 'f') {
                            int folderIdx = token.substring(1).toInt();
                            if (folderIdx >= 1 && folderIdx <= refreshFolderCount) {
                                itemCount++;
                            } else {
                                RF_DEBUG(0, "⚠️ Invalid folder number: ", token);
                            }
                        } else {
                            int fileIdx = token.toInt();
                            if (fileIdx >= 1 && fileIdx <= refreshFileCount) {
                                itemCount++;
                            } else {
                                RF_DEBUG(0, "⚠️ Invalid file number: ", token);
                            }
                        }
                    }
                    
                    tempStart = tempEnd + 1;
                }
                
                if (itemCount == 0) {
                    RF_DEBUG(0, "⚠️ No valid items to delete.");
                    continue;
                }
                
                // Second pass: show items and confirm
                char summaryBuffer[64];
                snprintf(summaryBuffer, sizeof(summaryBuffer), "\n📋 Items to delete (%d):", itemCount);
                RF_DEBUG(0, "", summaryBuffer);
                startPos = 0;
                while (startPos < input.length()) {
                    // Skip whitespace
                    while (startPos < input.length() && input.charAt(startPos) == ' ') {
                        startPos++;
                    }
                    if (startPos >= input.length()) break;
                    
                    // Find end of token
                    int endPos = startPos;
                    while (endPos < input.length() && input.charAt(endPos) != ' ') {
                        endPos++;
                    }
                    
                    String token = input.substring(startPos, endPos);
                    token.trim();
                    
                    if (token.length() > 0) {
                        // Check if it's a folder (starts with 'F' or 'f')
                        if (token.charAt(0) == 'F' || token.charAt(0) == 'f') {
                            int folderIdx = token.substring(1).toInt();
                            if (folderIdx >= 1 && folderIdx <= refreshFolderCount) {
                                int lastSlash = refreshFolderList[folderIdx - 1].lastIndexOf('/');
                                String displayName = (lastSlash >= 0) ? refreshFolderList[folderIdx - 1].substring(lastSlash + 1) : refreshFolderList[folderIdx - 1];
                                char buffer[80];
                                snprintf(buffer, sizeof(buffer), "  F%d: %s/", folderIdx, displayName.c_str());
                                RF_DEBUG(0, "", buffer);
                            }
                        } else {
                            int fileIdx = token.toInt();
                            if (fileIdx >= 1 && fileIdx <= refreshFileCount) {
                                int lastSlash = refreshFileList[fileIdx - 1].lastIndexOf('/');
                                String displayName = (lastSlash >= 0) ? refreshFileList[fileIdx - 1].substring(lastSlash + 1) : refreshFileList[fileIdx - 1];
                                char buffer[80];
                                snprintf(buffer, sizeof(buffer), "  %d: %s", fileIdx, displayName.c_str());
                                RF_DEBUG(0, "", buffer);
                            }
                        }
                    }
                    
                    startPos = endPos + 1;
                }
                
                RF_DEBUG(0, "\nType 'OK' to confirm deletion:");
                String confirm = "";
                while (confirm.length() == 0) {
                    if (RF_INPUT_AVAILABLE()) {
                        confirm = RF_INPUT_READ_LINE_UNTIL('\n');
                        confirm.trim();
                    }
                    delay(10);
                }
                
                if (confirm.equalsIgnoreCase("OK")) {
                    RF_DEBUG(0, "🗑️ Deleting items...");
                    int successCount = 0;
                    int failCount = 0;
                    
                    // Third pass: actually delete items
                    startPos = 0;
                    while (startPos < input.length()) {
                        // Skip whitespace
                        while (startPos < input.length() && input.charAt(startPos) == ' ') {
                            startPos++;
                        }
                        if (startPos >= input.length()) break;
                        
                        // Find end of token
                        int endPos = startPos;
                        while (endPos < input.length() && input.charAt(endPos) != ' ') {
                            endPos++;
                        }
                        
                        String token = input.substring(startPos, endPos);
                        token.trim();
                        
                        if (token.length() > 0) {
                            // Check if it's a folder (starts with 'F' or 'f')
                            if (token.charAt(0) == 'F' || token.charAt(0) == 'f') {
                                int folderIdx = token.substring(1).toInt();
                                if (folderIdx >= 1 && folderIdx <= refreshFolderCount) {
                                    if (RF_FS_RMDIR(refreshFolderList[folderIdx - 1])) {
                                        char buffer[128];
                                        snprintf(buffer, sizeof(buffer), "✅ Deleted folder: %s", refreshFolderList[folderIdx - 1].c_str());
                                        RF_DEBUG(0, "", buffer);
                                        successCount++;
                                    } else {
                                        char buffer[128];
                                        snprintf(buffer, sizeof(buffer), "❌ Failed to delete folder (may not be empty): %s", refreshFolderList[folderIdx - 1].c_str());
                                        RF_DEBUG(0, "", buffer);
                                        failCount++;
                                    }
                                }
                            } else {
                                int fileIdx = token.toInt();
                                if (fileIdx >= 1 && fileIdx <= refreshFileCount) {
                                    if (RF_FS_REMOVE(refreshFileList[fileIdx - 1])) {
                                        char buffer[128];
                                        snprintf(buffer, sizeof(buffer), "✅ Deleted file: %s", refreshFileList[fileIdx - 1].c_str());
                                        RF_DEBUG(0, "", buffer);
                                        successCount++;
                                    } else {
                                        char buffer[128];
                                        snprintf(buffer, sizeof(buffer), "❌ Failed to delete file: %s", refreshFileList[fileIdx - 1].c_str());
                                        RF_DEBUG(0, "", buffer);
                                        failCount++;
                                    }
                                }
                            }
                            delay(50);
                        }
                        
                        startPos = endPos + 1;
                    }
                    
                    char resultBuffer[80];
                    snprintf(resultBuffer, sizeof(resultBuffer), "📊 Summary: %d deleted, %d failed", successCount, failCount);
                    RF_DEBUG(0, "", resultBuffer);
                    if (failCount > 0) {
                        RF_DEBUG(0, "💡 Tip: Folders must be empty before deletion.");
                    }
                } else {
                    RF_DEBUG(0, "❎ Deletion canceled.");
                }
            }
        }
        else if (operation.equals("e")) {
            // Add new file operation - isolated space
            RF_DEBUG(0, "\n========== ➕ ADD NEW FILE MODE ==========");
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "📍 Current directory: %s", currentDir.c_str());
            RF_DEBUG(0, "", buffer);
            RF_DEBUG(0, "You can create .csv, .txt, .log, .json.");
            RF_DEBUG(0, "Enter filename or full path:");
            String newFile = reception_data(0, true, currentDir);
            if (newFile.length() > 0) {
                char msg[128];
                snprintf(msg, sizeof(msg), "✅ File created: %s", newFile.c_str());
                RF_DEBUG(0, "", msg);
            }
            RF_DEBUG(0, "🔙 Returning to main menu...");
        }
        else {
            RF_DEBUG(0, "⚠️ Invalid operation. Use a, b, c, d, e, or 'exit'.");
        }
    }
}

void deleteAllLittleFSFiles() {
    if (!RF_FS_BEGIN()) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "❌ %s Mount Failed!", RF_FS_TYPE);
        RF_DEBUG(0, "", buffer);
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "🚮 Scanning and deleting all files from %s...", RF_FS_TYPE);
    RF_DEBUG(0, "", msg);

    File root = RF_FS_OPEN("/", RF_FILE_READ);
    File file = root.openNextFile();
    int deleted = 0, failed = 0;

    while (file) {
        String path = file.name();
        file.close(); // must close before delete

        if (RF_FS_REMOVE(path)) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "✅ Deleted: %s", path.c_str());
            RF_DEBUG(0, "", buffer);
            deleted++;
        } else {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "❌ Failed:  %s", path.c_str());
            RF_DEBUG(0, "", buffer);
            failed++;
        }

        delay(500); // minimal delay for safety
        file = root.openNextFile();
    }

    char summaryBuffer[80];
    snprintf(summaryBuffer, sizeof(summaryBuffer), "🧹 Cleanup complete. Deleted: %d, Failed: %d", deleted, failed);
    RF_DEBUG(0, "", summaryBuffer);
}

String reception_data(int exact_columns, bool print_file, String currentDir) {
    // Clear any residual input
    while (RF_INPUT_AVAILABLE()) {
        RF_INPUT_READ();
    }
    delay(100); // Wait for any previous input to clear
    while (RF_INPUT_AVAILABLE()) {
        RF_INPUT_READ();
    }

    String fullPath = "";

    // Read filename/path from user
    while (fullPath.length() == 0) {
        if (RF_INPUT_AVAILABLE()) {
            fullPath = RF_INPUT_READ_LINE_UNTIL('\n');
            fullPath.trim();
        }
        delay(10);
    }

    // Normalize the path using common function
    fullPath = normalizePath(fullPath, currentDir);
    RF_DEBUG(0, "ℹ️  Resolved to: ", fullPath);

    // Ensure an extension exists; if not, default to .csv for backward compatibility
    int lastDot = fullPath.lastIndexOf('.');
    if (lastDot <= 0 || lastDot == (int)fullPath.length() - 1) {
        fullPath += ".csv";
        RF_DEBUG(0, "ℹ️  No valid extension provided. Defaulting to .csv → ", fullPath);
    }

    // Determine file type
    String lower = fullPath; lower.toLowerCase();
    bool isCSV = lower.endsWith(".csv");
    bool isTextFile = isCSV || lower.endsWith(".txt") || lower.endsWith(".log") || lower.endsWith(".json");

    RF_DEBUG(0, "📁 Will save to: ", fullPath);

    File file = RF_FS_OPEN(fullPath, RF_FILE_WRITE);
    if (!file) {
        RF_DEBUG(0, "❌ Failed to open file for writing: ", fullPath);
        return fullPath;
    }

    if (isCSV) {
        RF_DEBUG(0, "📥 Enter CSV rows (separated by space or newline). Type END to finish.");
    } else if (isTextFile) {
        RF_DEBUG(0, "📥 Enter text lines. Type END to finish.");
    } else {
        RF_DEBUG(0, "📥 Enter lines. Type END to finish.");
    }

    String buffer = "";
    int total_rows = 0;

    while (true) {
        if (RF_INPUT_AVAILABLE()) {
            String input = RF_INPUT_READ_LINE_UNTIL('\n');
            input.trim();

            if (input.equalsIgnoreCase("END")) {
                RF_DEBUG(0, "🔚 END received, closing file.");
                break;
            }

            if (isCSV) {
                // Backward-compatible: treat spaces as row separators too
                buffer += input + " ";
                buffer.trim();

                int start = 0;
                while (start < buffer.length()) {
                    int spaceIdx = buffer.indexOf(' ', start);
                    if (spaceIdx == -1) spaceIdx = buffer.length();

                    String row = buffer.substring(start, spaceIdx);
                    row.trim();

                    if (row.length() > 0) {
                        // Limit row to 234 elements (233 commas max after limit check logic)
                        uint16_t count = 1;
                        for (uint16_t i = 0; i < row.length(); ++i) {
                            if (row.charAt(i) == ',') ++count;
                            if (count > 234) {
                                row = row.substring(0, i); // Cut off at last allowed comma
                                break;
                            }
                        }

                        file.println(row);
                        RF_DEBUG_2(0, "✅ Saved (", count > 234 ? 234 : count, " elements): ", row);
                        total_rows++;
                    }

                    start = spaceIdx + 1;
                }

                buffer = ""; // reset buffer each input line since we handled all tokens
            } else {
                // General text formats: write line as-is
                file.println(input);
                total_rows++;
            }
        }
        delay(30);
    }
    file.close();

    // Only clean CSV files when requested
    if (isCSV && exact_columns > 0) {
        cleanMalformedRows(fullPath, exact_columns);
    }
    if (print_file) printFile(fullPath);

    RF_DEBUG(0, "📄 Total lines written: ", total_rows);

    return fullPath; // Return the full path of the created file
}

void cleanMalformedRows(const String& filename, int exact_columns) {
    File file = RF_FS_OPEN(filename, RF_FILE_READ);
    if (!file) {
        RF_DEBUG(0, "❌ Failed to open ", filename);
        return;
    }

    String tempName = filename + ".tmp";
    File temp = RF_FS_OPEN(tempName, RF_FILE_WRITE);
    if (!temp) {
        RF_DEBUG(0, "❌ Failed to open temp file for writing: ", tempName); 
        file.close();
        return;
    }

    uint16_t kept = 0, removed = 0;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        // Count number of commas to determine number of elements
        uint16_t count = 1;
        for (uint16_t i = 0; i < line.length(); ++i) {
            if (line.charAt(i) == ',') ++count;
        }

        if (count == exact_columns) {
            temp.println(line); // Keep valid line
            ++kept;
        } else {
            ++removed; // Skip invalid line
        }
    }

    file.close();
    temp.close();

    RF_FS_REMOVE(filename);
    RF_FS_RENAME(tempName, filename);

    char buffer[128];
    snprintf(buffer, sizeof(buffer), "✅ Cleaned %s: %u rows kept, %u rows removed (not exactly %u elements).",
             filename.c_str(), kept, removed, exact_columns);
    RF_DEBUG(0, "", buffer);
}
