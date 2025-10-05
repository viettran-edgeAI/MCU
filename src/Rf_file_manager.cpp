#include "Rf_file_manager.h"

bool cloneFile(const String& src, const String& dest) {
    if(LittleFS.exists(src) == false) {
        Serial.print("❌ Source file does not exist: ");
        Serial.println(src);
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
        Serial.printf("🔄 Auto-generated destination: %s\n", actualDest.c_str());
    }

    File sourceFile = LittleFS.open(src, FILE_READ);
    if (!sourceFile) {
        Serial.print("❌ Failed to open source file: ");
        Serial.println(src);
        return false;
    }

    File destFile = LittleFS.open(actualDest, FILE_WRITE);
    if (!destFile) {
        Serial.print("❌ Failed to create destination file: ");
        Serial.println(actualDest);
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

    Serial.print("✅ File cloned from ");
    Serial.print(src);
    Serial.print(" ➝ ");
    Serial.println(actualDest);
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
    if (!LittleFS.exists(oldPath)) {
        Serial.print("❌ Source file does not exist: ");
        Serial.println(oldPath);
        return false;
    }

    // Check if destination already exists
    if (LittleFS.exists(newPath)) {
        Serial.print("❌ Destination file already exists: ");
        Serial.println(newPath);
        return false;
    }

    // Perform the rename operation
    if (LittleFS.rename(oldPath, newPath)) {
        Serial.print("✅ File renamed from ");
        Serial.print(oldPath);
        Serial.print(" ➝ ");
        Serial.println(newPath);
        return true;
    } else {
        Serial.print("❌ Failed to rename file from ");
        Serial.print(oldPath);
        Serial.print(" to ");
        Serial.println(newPath);
        return false;
    }
}

// Overload for const char* compatibility
bool renameFile(const char* oldPath, const char* newPath) {
    return renameFile(String(oldPath), String(newPath));
}

void printFile(String filename) {
    File file = LittleFS.open(filename.c_str(), FILE_READ);
    if (!file) {
        Serial.print("❌ Failed to open ");
        Serial.println(filename);
        return;
    }

    Serial.print("📄 ");
    Serial.println(filename);

    // Get file extension to determine file type
    String filenameLower = filename;
    filenameLower.toLowerCase();
    bool isCSV = filenameLower.endsWith(".csv");
    bool isTextFile = isCSV || filenameLower.endsWith(".txt") || 
                      filenameLower.endsWith(".log") || filenameLower.endsWith(".json");

    if (!isTextFile) {
        // Handle binary files - just show basic info
        size_t fileSize = file.size();
        Serial.printf("📊 Binary file size: %d bytes\n", fileSize);
        Serial.println("⚠️ Binary content not displayed");
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

        Serial.println(line);
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

    Serial.println("📊 Summary:");
    Serial.printf("🧾 Lines: %u\n", rowCount);
    if (isCSV) {
        Serial.printf("📐 Columns: %u\n", columnCount);
    }
}


void manage_files() {
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed!");
        return;
    }

    String currentDir = "/";  // Start at root

    while (true) {
        Serial.println("\n====== 📂 LittleFS File Manager ======");
        Serial.printf("📍 Current Directory: %s\n", currentDir.c_str());
        
        File dir = LittleFS.open(currentDir);
        if (!dir || !dir.isDirectory()) {
            Serial.println("❌ Failed to open directory!");
            currentDir = "/";  // Reset to root
            continue;
        }

        String fileList[50];   // List of file paths
        String folderList[50]; // List of folder paths
        int fileCount = 0;
        int folderCount = 0;

        Serial.printf("📦 LittleFS Free Space: %d / %d bytes available\n", 
                     LittleFS.totalBytes() - LittleFS.usedBytes(), LittleFS.totalBytes());

        // List directories first, then files
        File entry = dir.openNextFile();
        while (entry && (fileCount + folderCount) < 50) {
            String name = String(entry.name());
            
            // Ensure path starts with /
            if (!name.startsWith("/")) {
                name = "/" + name;
            }
            
            // Get just the name (last component of path)
            int lastSlash = name.lastIndexOf('/');
            String displayName = (lastSlash >= 0) ? name.substring(lastSlash + 1) : name;
            
            // Skip empty names
            if (displayName.length() == 0) {
                entry.close();
                entry = dir.openNextFile();
                continue;
            }
            
            if (entry.isDirectory()) {
                folderList[folderCount] = name;
                Serial.printf("📁 %2d: %s/\n", folderCount + 1, displayName.c_str());
                folderCount++;
            } else {
                fileList[fileCount] = name;
                size_t fileSize = entry.size();
                Serial.printf("📄 %2d: %-30s (%d bytes)\n", fileCount + 1, displayName.c_str(), fileSize);
                fileCount++;
            }
            
            entry.close();
            entry = dir.openNextFile();
        }
        dir.close();

        if (fileCount == 0 && folderCount == 0) {
            Serial.println("⚠️ Directory is empty.");
        }

        Serial.println("\n📋 Operations:");
        if (currentDir != "/") {
            Serial.println("..: ⬆️  Go to parent directory");
        }
        Serial.println("g: 📂 Go into folder (1-" + String(folderCount) + ")");
        Serial.println("a: 📄 Print file content");
        Serial.println("b: 📋 Clone file");
        Serial.println("c: ✏️  Rename file");
        Serial.println("d: 🗑️  Delete file/folder");
        Serial.println("e: ➕ Add new file");
        Serial.println("Type operation letter, or 'exit' to quit:");

        Serial.println("Type operation letter, or 'exit' to quit:");

        String operation = "";
        while (operation.length() == 0) {
            if (Serial.available()) {
                operation = Serial.readStringUntil('\n');
                operation.trim();
                operation.toLowerCase();
            }
            delay(10);
        }

        if (operation.equals("exit")) {
            Serial.println("🔚 Exiting file manager.");
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
                Serial.printf("⬆️ Moving to parent: %s\n", currentDir.c_str());
            } else {
                Serial.println("⚠️ Already at root directory.");
            }
            continue;
        }

        // Go into folder
        if (operation.equals("g")) {
            if (folderCount == 0) {
                Serial.println("⚠️ No folders in current directory.");
                continue;
            }
            
            Serial.printf("Enter folder number (1-%d): ", folderCount);
            String input = "";
            while (input.length() == 0) {
                if (Serial.available()) {
                    input = Serial.readStringUntil('\n');
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
                
                Serial.printf("📂 Entering folder: %s\n", currentDir.c_str());
            } else {
                Serial.println("⚠️ Invalid folder number.");
            }
            continue;
        }

        if (operation.equals("a")) {
            // Print file operation - isolated space
            Serial.println("\n========== 📄 PRINT FILE MODE ==========");
            while (true) {
                Serial.println("\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    Serial.printf("%2d: %s\n", i + 1, fileList[i].c_str());
                }
                Serial.println("\nEnter file number to print, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (Serial.available()) {
                        input = Serial.readStringUntil('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    Serial.println("🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    printFile(fileList[index - 1]);
                } else {
                    Serial.println("⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("b")) {
            // Clone file operation - isolated space
            Serial.println("\n========== 📋 CLONE FILE MODE ==========");
            while (true) {
                Serial.println("\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    Serial.printf("%2d: %s\n", i + 1, fileList[i].c_str());
                }
                Serial.println("\nEnter source file number to clone, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (Serial.available()) {
                        input = Serial.readStringUntil('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    Serial.println("🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    Serial.println("Enter destination path (or press Enter for auto-name):");
                    String dest = "";
                    unsigned long timeout = millis() + 10000; // 10 second timeout
                    while (millis() < timeout) {
                        if (Serial.available()) {
                            dest = Serial.readStringUntil('\n');
                            dest.trim();
                            break;
                        }
                        delay(10);
                    }
                    cloneFile(fileList[index - 1], dest);
                    delay(100); // Short delay to avoid flooding the output
                } else {
                    Serial.println("⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("c")) {
            // Rename file operation - isolated space
            Serial.println("\n========== ✏️ RENAME FILE MODE ==========");
            while (true) {
                Serial.println("\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    Serial.printf("%2d: %s\n", i + 1, fileList[i].c_str());
                }
                Serial.println("\nEnter file number to rename, or 'end' to return to main menu:");
                
                String input = "";
                while (input.length() == 0) {
                    if (Serial.available()) {
                        input = Serial.readStringUntil('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    Serial.println("🔙 Returning to main menu...");
                    break;
                }
                
                int index = input.toInt();
                if (index >= 1 && index <= fileCount) {
                    Serial.println("Enter new file path:");
                    String newPath = "";
                    while (newPath.length() == 0) {
                        if (Serial.available()) {
                            newPath = Serial.readStringUntil('\n');
                            newPath.trim();
                        }
                        delay(10);
                    }
                    if (newPath.length() > 0) {
                        if (!newPath.startsWith("/")) {
                            newPath = "/" + newPath;
                        }
                        if (renameFile(fileList[index - 1], newPath)) {
                            Serial.println("✅ File renamed successfully! You can rename more files or type 'end' to exit.");
                            // Update the specific file in the list for immediate reflection
                            fileList[index - 1] = newPath;
                        }
                    }
                } else {
                    Serial.println("⚠️ Invalid file number.");
                }
            }
        }
        else if (operation.equals("d")) {
            // Delete file/folder operation - isolated space with multiple item support
            Serial.println("\n========== 🗑️ DELETE MODE ==========");
            while (true) {
                // Refresh file and folder list each time to show current state
                File refreshDir = LittleFS.open(currentDir);
                if (!refreshDir || !refreshDir.isDirectory()) {
                    Serial.println("❌ Failed to refresh directory!");
                    break;
                }
                
                String refreshFileList[50];
                String refreshFolderList[50];
                int refreshFileCount = 0;
                int refreshFolderCount = 0;
                
                File refreshEntry = refreshDir.openNextFile();
                while (refreshEntry && (refreshFileCount + refreshFolderCount) < 50) {
                    String name = String(refreshEntry.name());
                    
                    // Ensure path starts with /
                    if (!name.startsWith("/")) {
                        name = "/" + name;
                    }
                    
                    int lastSlash = name.lastIndexOf('/');
                    String displayName = (lastSlash >= 0) ? name.substring(lastSlash + 1) : name;
                    
                    if (displayName.length() == 0) {
                        refreshEntry.close();
                        refreshEntry = refreshDir.openNextFile();
                        continue;
                    }
                    
                    if (refreshEntry.isDirectory()) {
                        refreshFolderList[refreshFolderCount] = name;
                        refreshFolderCount++;
                    } else {
                        refreshFileList[refreshFileCount] = name;
                        refreshFileCount++;
                    }
                    
                    refreshEntry.close();
                    refreshEntry = refreshDir.openNextFile();
                }
                refreshDir.close();
                
                Serial.println("\n📂 Available folders:");
                if (refreshFolderCount == 0) {
                    Serial.println("  (none)");
                } else {
                    for (int i = 0; i < refreshFolderCount; i++) {
                        int lastSlash = refreshFolderList[i].lastIndexOf('/');
                        String displayName = (lastSlash >= 0) ? refreshFolderList[i].substring(lastSlash + 1) : refreshFolderList[i];
                        Serial.printf("  F%d: %s/\n", i + 1, displayName.c_str());
                    }
                }
                
                Serial.println("\n� Available files:");
                if (refreshFileCount == 0) {
                    Serial.println("  (none)");
                } else {
                    for (int i = 0; i < refreshFileCount; i++) {
                        int lastSlash = refreshFileList[i].lastIndexOf('/');
                        String displayName = (lastSlash >= 0) ? refreshFileList[i].substring(lastSlash + 1) : refreshFileList[i];
                        Serial.printf("  %d: %s\n", i + 1, displayName.c_str());
                    }
                }
                
                if (refreshFileCount == 0 && refreshFolderCount == 0) {
                    Serial.println("⚠️ No files or folders to delete. Returning to main menu...");
                    break;
                }
                
                Serial.println("\nEnter item to delete:");
                Serial.println("  - File number (e.g., '3')");
                Serial.println("  - Folder number with 'F' prefix (e.g., 'F1')");
                Serial.println("  - 'all' to delete everything");
                Serial.println("  - 'end' to return:");
                
                String input = "";
                while (input.length() == 0) {
                    if (Serial.available()) {
                        input = Serial.readStringUntil('\n');
                        input.trim();
                    }
                    delay(10);
                }
                
                if (input.equalsIgnoreCase("end")) {
                    Serial.println("🔙 Returning to main menu...");
                    break;
                }
                
                if (input.equalsIgnoreCase("all")) {
                    Serial.println("⚠️ WARNING: This will delete ALL files and folders in current directory!");
                    Serial.println("Type 'CONFIRM' to proceed or anything else to cancel:");
                    String confirm = "";
                    while (confirm.length() == 0) {
                        if (Serial.available()) {
                            confirm = Serial.readStringUntil('\n');
                            confirm.trim();
                        }
                        delay(10);
                    }
                    if (confirm.equals("CONFIRM")) {
                        Serial.println("🗑️ Deleting all items...");
                        
                        // Delete all files first
                        for (int i = 0; i < refreshFileCount; i++) {
                            if (LittleFS.remove(refreshFileList[i])) {
                                Serial.printf("✅ Deleted file: %s\n", refreshFileList[i].c_str());
                            } else {
                                Serial.printf("❌ Failed to delete file: %s\n", refreshFileList[i].c_str());
                            }
                            delay(50);
                        }
                        
                        // Then delete all folders
                        for (int i = 0; i < refreshFolderCount; i++) {
                            if (LittleFS.rmdir(refreshFolderList[i])) {
                                Serial.printf("✅ Deleted folder: %s\n", refreshFolderList[i].c_str());
                            } else {
                                Serial.printf("❌ Failed to delete folder (may not be empty): %s\n", refreshFolderList[i].c_str());
                            }
                            delay(50);
                        }
                        
                        Serial.println("✅ Cleanup complete!");
                    } else {
                        Serial.println("❎ Delete all operation canceled.");
                    }
                    continue;
                }
                
                // Check if it's a folder (starts with 'F' or 'f')
                bool isFolder = false;
                int itemIndex = 0;
                
                if (input.length() > 0 && (input.charAt(0) == 'F' || input.charAt(0) == 'f')) {
                    isFolder = true;
                    itemIndex = input.substring(1).toInt();
                } else {
                    itemIndex = input.toInt();
                }
                
                if (isFolder) {
                    if (itemIndex >= 1 && itemIndex <= refreshFolderCount) {
                        String folderPath = refreshFolderList[itemIndex - 1];
                        Serial.printf("Delete folder '%s'? Type 'OK' to confirm: ", folderPath.c_str());
                        
                        String confirm = "";
                        while (confirm.length() == 0) {
                            if (Serial.available()) {
                                confirm = Serial.readStringUntil('\n');
                                confirm.trim();
                            }
                            delay(10);
                        }
                        
                        if (confirm.equalsIgnoreCase("OK")) {
                            if (LittleFS.rmdir(folderPath)) {
                                Serial.printf("✅ Deleted folder: %s\n", folderPath.c_str());
                            } else {
                                Serial.printf("❌ Failed to delete folder (may not be empty): %s\n", folderPath.c_str());
                                Serial.println("💡 Tip: Delete all files inside the folder first.");
                            }
                        } else {
                            Serial.println("❎ Deletion canceled.");
                        }
                    } else {
                        Serial.println("⚠️ Invalid folder number.");
                    }
                } else {
                    if (itemIndex >= 1 && itemIndex <= refreshFileCount) {
                        String filePath = refreshFileList[itemIndex - 1];
                        Serial.printf("Delete file '%s'? Type 'OK' to confirm: ", filePath.c_str());
                        
                        String confirm = "";
                        while (confirm.length() == 0) {
                            if (Serial.available()) {
                                confirm = Serial.readStringUntil('\n');
                                confirm.trim();
                            }
                            delay(10);
                        }
                        
                        if (confirm.equalsIgnoreCase("OK")) {
                            if (LittleFS.remove(filePath)) {
                                Serial.printf("✅ Deleted file: %s\n", filePath.c_str());
                            } else {
                                Serial.printf("❌ Failed to delete file: %s\n", filePath.c_str());
                            }
                        } else {
                            Serial.println("❎ Deletion canceled.");
                        }
                    } else {
                        Serial.println("⚠️ Invalid file number.");
                    }
                }
            }
        }
        else if (operation.equals("e")) {
            // Add new file operation - isolated space
            Serial.println("\n========== ➕ ADD NEW FILE MODE ==========");
            Serial.println("You can create .csv, .txt, .log, .json. Provide full path (e.g., /digit_data.csv).");
            String newFile = reception_data(0, true);
            if (newFile.length() > 0) {
                Serial.printf("✅ File created: %s\n", newFile.c_str());
            }
            Serial.println("🔙 Returning to main menu...");
        }
        else {
            Serial.println("⚠️ Invalid operation. Use a, b, c, d, e, or 'exit'.");
        }
    }
}

void deleteAllLittleFSFiles() {
    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed!");
        return;
    }

    Serial.println("🚮 Scanning and deleting all files from LittleFS...");

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    int deleted = 0, failed = 0;

    while (file) {
        String path = file.name();
        file.close(); // must close before delete

        if (LittleFS.remove(path)) {
            Serial.printf("✅ Deleted: %s\n", path.c_str());
            deleted++;
        } else {
            Serial.printf("❌ Failed:  %s\n", path.c_str());
            failed++;
        }

        delay(500); // minimal delay for safety
        file = root.openNextFile();
    }

    Serial.printf("🧹 Cleanup complete. Deleted: %d, Failed: %d\n", deleted, failed);
}

String reception_data(int exact_columns, bool print_file) {
    // Clear any residual input
    while (Serial.available()) {
        Serial.read();
    }
    delay(100); // Wait for any previous input to clear
    while (Serial.available()) {
        Serial.read();
    }

    Serial.println("Enter full file path with extension (e.g., /digit_data.csv or digit_data.txt):");
    Serial.flush();
    String fullPath = "";

    // Read and normalize path
    while (fullPath.length() == 0) {
        if (Serial.available()) {
            fullPath = Serial.readStringUntil('\n');
            fullPath.trim();
        }
        delay(10);
    }

    // Auto-prefix leading '/'
    if (!fullPath.startsWith("/")) {
        fullPath = "/" + fullPath;
        Serial.printf("ℹ️  Auto-prefixed leading '/': %s\n", fullPath.c_str());
    }

    // Ensure an extension exists; if not, default to .csv for backward compatibility
    int lastDot = fullPath.lastIndexOf('.');
    if (lastDot <= 0 || lastDot == (int)fullPath.length() - 1) {
        fullPath += ".csv";
        Serial.printf("ℹ️  No valid extension provided. Defaulting to .csv → %s\n", fullPath.c_str());
    }

    // Determine file type
    String lower = fullPath; lower.toLowerCase();
    bool isCSV = lower.endsWith(".csv");
    bool isTextFile = isCSV || lower.endsWith(".txt") || lower.endsWith(".log") || lower.endsWith(".json");

    Serial.printf("📁 Will save to: %s\n", fullPath.c_str());

    File file = LittleFS.open(fullPath, FILE_WRITE);
    if (!file) {
        Serial.println("❌ Failed to open file for writing");
        return fullPath;
    }

    if (isCSV) {
        Serial.println("📥 Enter CSV rows (separated by space or newline). Type END to finish.");
    } else if (isTextFile) {
        Serial.println("📥 Enter text lines. Press Enter for new line. Type END on its own line to finish.");
    } else {
        Serial.println("⚠️ Non-text extension detected. Input will be stored as plain text lines. Type END to finish.");
    }

    String buffer = "";
    int total_rows = 0;

    while (true) {
        if (Serial.available()) {
            String input = Serial.readStringUntil('\n');
            input.trim();

            if (input.equalsIgnoreCase("END")) {
                Serial.println("🔚 END received, closing file.");
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
                        Serial.printf("✅ Saved (%d elements): %s\n", count > 234 ? 234 : count, row.c_str());
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

    Serial.printf("📄 Total lines written: %d\n", total_rows);

    return fullPath; // Return the full path of the created file
}

void cleanMalformedRows(const String& filename, int exact_columns) {
    File file = LittleFS.open(filename, FILE_READ);
    if (!file) {
        Serial.print("❌ Failed to open ");
        Serial.println(filename);
        return;
    }

    String tempName = filename + ".tmp";
    File temp = LittleFS.open(tempName, FILE_WRITE);
    if (!temp) {
        Serial.println("❌ Failed to open temp file for writing");
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

    LittleFS.remove(filename);
    LittleFS.rename(tempName, filename);

    Serial.printf("✅ Cleaned %s: %u rows kept, %u rows removed (not exactly %u elements).\n",
                  filename.c_str(), kept, removed, exact_columns);
}
