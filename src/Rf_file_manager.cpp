#include "Rf_file_manager.h"

bool cloneCSVFile(const String& src, const String& dest) {
    File sourceFile = SPIFFS.open(src, FILE_READ);
    if (!sourceFile) {
        Serial.print("❌ Failed to open source file: ");
        Serial.println(src);
        return false;
    }

    File destFile = SPIFFS.open(dest, FILE_WRITE);
    if (!destFile) {
        Serial.print("❌ Failed to create destination file: ");
        Serial.println(dest);
        sourceFile.close();
        return false;
    }

    while (sourceFile.available()) {
        String line = sourceFile.readStringUntil('\n');
        destFile.print(line);
        destFile.print('\n');  // manually add \n to preserve exact format
    }

    sourceFile.close();
    destFile.close();

    Serial.print("✅ File cloned exactly from ");
    Serial.print(src);
    Serial.print(" ➝ ");
    Serial.println(dest);
    printCSVFile(dest);
    return true;
}

void printCSVFile(String filename) {
    File file = SPIFFS.open(filename.c_str(), FILE_READ);
    if (!file) {
        Serial.print("❌ Failed to open ");
        Serial.println(filename);
        return;
    }

    Serial.print("📄 ");
    Serial.println(filename);

    uint16_t rowCount = 0;
    uint16_t columnCount = 0;
    bool columnCounted = false;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        Serial.println(line);
        ++rowCount;

        if (!columnCounted) {
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
    Serial.printf("🧾 Rows: %u\n", rowCount);
    Serial.printf("📐 Columns: %u\n", columnCount);
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

        Serial.println("\nType a file number to delete it, or type END to finish:");

        String input = "";
        while (input.length() == 0) {
            if (Serial.available()) {
                input = Serial.readStringUntil('\n');
                input.trim();
            }
            delay(10);
        }

        if (input.equalsIgnoreCase("END")) {
            Serial.println("🔚 Exiting file manager.");
            break;
        }

        int index = input.toInt();
        if (index >= 1 && index <= fileCount) {
            String fileToDelete = fileList[index - 1];
            if (!fileToDelete.startsWith("/")) {
                fileToDelete = "/" + fileToDelete;
            }

            Serial.printf("🗑️  Are you sure you want to delete '%s'?\n", fileToDelete.c_str());
            Serial.println("Type OK to confirm or NO to cancel:");

            String confirm = "";
            while (confirm.length() == 0) {
                if (Serial.available()) {
                    confirm = Serial.readStringUntil('\n');
                    confirm.trim();
                }
                delay(10);
            }

            if (confirm.equalsIgnoreCase("OK")) {
                if (!SPIFFS.exists(fileToDelete)) {
                    Serial.println("⚠️ File does not exist!");
                    continue;
                }

                if (SPIFFS.remove(fileToDelete)) {
                    Serial.printf("✅ Deleted: %s\n", fileToDelete.c_str());
                } else {
                    Serial.printf("❌ Failed to delete: %s\n", fileToDelete.c_str());
                }
            } else {
                Serial.println("❎ Deletion canceled.");
            }
        } else {
            Serial.println("⚠️ Invalid file number.");
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

String reception_data(bool exact_columns, bool print_file) {
    Serial.println("Enter base filename (no extension), e.g. animal_data:");
    String baseName = "";

    while (baseName.length() == 0) {
        if (Serial.available()) {
            baseName = Serial.readStringUntil('\n');
            baseName.trim();
        }
    }

    String fullPath = "/" + baseName + ".csv";
    Serial.printf("📁 Will save to: %s\n", fullPath.c_str());

    File file = SPIFFS.open(fullPath, FILE_WRITE);
    if (!file) {
        Serial.println("❌ Failed to open file for writing");
        return;
    }

    Serial.println("📥 Enter CSV lines (separated by space or newline). Type END to finish.");

    String buffer = "";

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
        }

        delay(10);
    }
    file.close();
    if(exact_columns > 0) {
        cleanMalformedRows(fullPath, exact_columns);
    } else {
        Serial.println("No exact column count specified, skipping cleanup.");
    }
    if(print_file) printCSVFile(fullPath);
    return fullPath; // Return the full path of the created file
}

void cleanMalformedRows(const String& filename, uint16_t exact_columns) {
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
