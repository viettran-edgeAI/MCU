#include "Rf_file_manager.h"

bool cloneFile(const String& src, const String& dest) {
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

    File sourceFile = SPIFFS.open(src, FILE_READ);
    if (!sourceFile) {
        Serial.print("❌ Failed to open source file: ");
        Serial.println(src);
        return false;
    }

    File destFile = SPIFFS.open(actualDest, FILE_WRITE);
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
    if (!SPIFFS.exists(oldPath)) {
        Serial.print("❌ Source file does not exist: ");
        Serial.println(oldPath);
        return false;
    }

    // Check if destination already exists
    if (SPIFFS.exists(newPath)) {
        Serial.print("❌ Destination file already exists: ");
        Serial.println(newPath);
        return false;
    }

    // Perform the rename operation
    if (SPIFFS.rename(oldPath, newPath)) {
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
    File file = SPIFFS.open(filename.c_str(), FILE_READ);
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


void manageSPIFFSFiles() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS Mount Failed!");
        return;
    }

    while (true) {
        Serial.println("\n====== 📂 Files in SPIFFS ======");
        File root = SPIFFS.open("/");
        File file = root.openNextFile();

        String fileList[50];  // List of full paths
        int fileCount = 0;

        Serial.printf("📦 SPIFFS Free Space: %d / %d bytes available\n", 
                     SPIFFS.totalBytes() - SPIFFS.usedBytes(), SPIFFS.totalBytes());

        while (file && fileCount < 50) {
            String path = String(file.name());  // Full path, e.g. "/cancer_data.csv"
            size_t fileSize = file.size();
            if (!path.startsWith("/")) {
                path = "/" + path;
            }
            fileList[fileCount] = path;

            Serial.printf("%2d: %-20s (%d bytes)\n", fileCount + 1, path.c_str(), fileSize);

            file.close();
            file = root.openNextFile();
            fileCount++;
        }

        if (fileCount == 0) {
            Serial.println("⚠️ No files found.");
        }

        Serial.println("\n📋 Operations:");
        Serial.println("a: 📄 Print file content");
        Serial.println("b: 📋 Clone file");
        Serial.println("c: ✏️  Rename file");
        Serial.println("d: 🗑️  Delete file");
        Serial.println("e: ➕ Add new file");
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
            // Delete file operation - isolated space with multiple file support
            Serial.println("\n========== 🗑️ DELETE FILE MODE ==========");
            while (true) {
                // Refresh file list each time to show current state
                File refreshRoot = SPIFFS.open("/");
                File refreshFile = refreshRoot.openNextFile();
                fileCount = 0; // Reset count
                
                while (refreshFile && fileCount < 50) {
                    String refreshPath = String(refreshFile.name());
                    if (!refreshPath.startsWith("/")) {
                        refreshPath = "/" + refreshPath;
                    }
                    fileList[fileCount] = refreshPath;
                    refreshFile.close();
                    refreshFile = refreshRoot.openNextFile();
                    fileCount++;
                }
                
                Serial.println("\n📂 Available files:");
                for (int i = 0; i < fileCount; i++) {
                    Serial.printf("%2d: %s\n", i + 1, fileList[i].c_str());
                }
                
                if (fileCount == 0) {
                    Serial.println("⚠️ No files found. Returning to main menu...");
                    break;
                }
                
                Serial.println("\nEnter file number(s) to delete (e.g., '1,3,5' or '1 3 5'),");
                Serial.println("'all' to delete all files, or 'end' to return:");
                
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
                    Serial.println("⚠️ WARNING: This will delete ALL files!");
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
                        deleteAllSPIFFSFiles();
                        Serial.println("✅ All files deleted! You can continue or type 'end' to exit.");
                    } else {
                        Serial.println("❎ Delete all operation canceled.");
                    }
                    continue;
                }
                
                // Parse multiple file numbers
                int filesToDelete[20]; // Max 20 files for memory efficiency
                int deleteCount = 0;
                
                // Replace commas with spaces for uniform parsing
                input.replace(",", " ");
                
                int start = 0;
                while (start < input.length() && deleteCount < 20) {
                    int spaceIdx = input.indexOf(' ', start);
                    if (spaceIdx == -1) spaceIdx = input.length();
                    
                    String numStr = input.substring(start, spaceIdx);
                    numStr.trim();
                    
                    if (numStr.length() > 0) {
                        int fileNum = numStr.toInt();
                        if (fileNum >= 1 && fileNum <= fileCount) {
                            filesToDelete[deleteCount] = fileNum;
                            deleteCount++;
                        } else if (fileNum > 0) {
                            Serial.printf("⚠️ Invalid file number: %d\n", fileNum);
                        }
                    }
                    
                    start = spaceIdx + 1;
                }
                
                if (deleteCount == 0) {
                    Serial.println("⚠️ No valid file numbers entered.");
                    continue;
                }
                
                // Show files to be deleted and confirm
                Serial.printf("🗑️ Files to delete (%d):\n", deleteCount);
                for (int i = 0; i < deleteCount; i++) {
                    Serial.printf("  %d: %s\n", filesToDelete[i], fileList[filesToDelete[i] - 1].c_str());
                }
                Serial.println("Type 'OK' to confirm deletion or 'NO' to cancel:");

                String confirm = "";
                while (confirm.length() == 0) {
                    if (Serial.available()) {
                        confirm = Serial.readStringUntil('\n');
                        confirm.trim();
                    }
                    delay(10);
                }

                if (confirm.equalsIgnoreCase("OK")) {
                    int deleted = 0, failed = 0;
                    
                    for (int i = 0; i < deleteCount; i++) {
                        String fileToDelete = fileList[filesToDelete[i] - 1];
                        
                        if (!SPIFFS.exists(fileToDelete)) {
                            Serial.printf("⚠️ File does not exist: %s\n", fileToDelete.c_str());
                            failed++;
                            continue;
                        }

                        if (SPIFFS.remove(fileToDelete)) {
                            Serial.printf("✅ Deleted: %s\n", fileToDelete.c_str());
                            deleted++;
                        } else {
                            Serial.printf("❌ Failed to delete: %s\n", fileToDelete.c_str());
                            failed++;
                        }
                        delay(100); // Small delay for stability
                    }
                    
                    Serial.printf("📊 Deletion summary: %d deleted, %d failed\n", deleted, failed);
                    Serial.println("✅ Operation completed! You can delete more files or type 'end' to exit.");
                } else {
                    Serial.println("❎ Deletion canceled.");
                }
            }
        }
        else if (operation.equals("e")) {
            // Add new file operation - isolated space
            Serial.println("\n========== ➕ ADD NEW FILE MODE ==========");
            Serial.println("Creating new CSV file...");
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

void deleteAllSPIFFSFiles() {
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS Mount Failed!");
        return;
    }

    Serial.println("🚮 Scanning and deleting all files from SPIFFS...");

    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    int deleted = 0, failed = 0;

    while (file) {
        String path = file.name();
        file.close(); // must close before delete

        if (SPIFFS.remove(path)) {
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
    while (Serial.available()) {
        Serial.read();
    }
    delay(100); // Wait for any previous input to clear
    while (Serial.available()) {
        Serial.read();
    }

    Serial.println("Enter base filename (no extension), e.g. animal_data:");
    Serial.flush();
    String baseName = "";

    while (baseName.length() == 0) {
        if (Serial.available()) {
            baseName = Serial.readStringUntil('\n');
            baseName.trim();
        }
        delay(10);
    }

    String fullPath = "/" + baseName + ".csv";
    Serial.printf("📁 Will save to: %s\n", fullPath.c_str());

    File file = SPIFFS.open(fullPath, FILE_WRITE);
    if (!file) {
        Serial.println("❌ Failed to open file for writing");
        return baseName;
    }

    Serial.println("📥 Enter CSV lines (separated by space or newline). Type END to finish.");

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

            buffer += input + " ";
            buffer.trim();

            int start = 0;
            while (start < buffer.length()) {
                int spaceIdx = buffer.indexOf(' ', start);
                if (spaceIdx == -1) spaceIdx = buffer.length();

                String row = buffer.substring(start, spaceIdx);
                row.trim();

                if (row.length() > 0) {
                    // Limit row to 255 elements (254 commas max)
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
                }

                start = spaceIdx + 1;
            }

            buffer = "";
            total_rows++;
        }

        delay(10);
    }
    file.close();
    if(exact_columns > 0) {
        cleanMalformedRows(fullPath, exact_columns);
    } 
    if(print_file) printFile(fullPath);

    Serial.printf("📄 Total rows: %d", total_rows);

    return fullPath; // Return the full path of the created file
}

void cleanMalformedRows(const String& filename, int exact_columns) {
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
        Serial.print("❌ Failed to open ");
        Serial.println(filename);
        return;
    }

    String tempName = filename + ".tmp";
    File temp = SPIFFS.open(tempName, FILE_WRITE);
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

    SPIFFS.remove(filename);
    SPIFFS.rename(tempName, filename);

    Serial.printf("✅ Cleaned %s: %u rows kept, %u rows removed (not exactly %u elements).\n",
                  filename.c_str(), kept, removed, exact_columns);
}
