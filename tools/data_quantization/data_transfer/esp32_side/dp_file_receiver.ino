/*
 * ESP32 Dataset Parameters Receiver with V2 Protocol
 * Upload this sketch to ESP32, then use transfer_dp_file.py to send files.
 * Saves files to file system with model_name/filename structure and CRC verification.
 */

#include "Rf_file_manager.h"  // Includes Rf_board_config.h internally

// --- Storage Configuration ---
const RfStorageType STORAGE_MODE = RfStorageType::FLASH;

/*
 * Transfer timing and size configuration.
 * IMPORTANT: Keep these in sync with the PC sender script (transfer_dp_file.py)
 * 
 * BUFFER_CHUNK must match CHUNK_SIZE in the PC script.
 * ESP32-C3: 220 bytes (USB CDC buffer constraint)
 * Other boards: Can use 256+ bytes for higher speed
 */
const int BUFFER_CHUNK = USER_CHUNK_SIZE;
const int BUFFER_DELAY_MS = 20;
uint8_t buffer[BUFFER_CHUNK];

// LED for status indication
const uint8_t LED_PIN = 2;

// Helper functions
void setLed(bool on) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void blinkLed(int count, int duration) {
    for (int i = 0; i < count; i++) {
        setLed(true);
        delay(duration);
        setLed(false);
        delay(duration);
    }
}

uint32_t compute_crc32(const uint8_t* data, size_t len) {
    /*
     * CRC32 computation for chunk validation using the same polynomial
     * as the PC sender. Ensures data integrity across the USB link.
     */
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        crc ^= b;
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = -(int)(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return crc ^ 0xFFFFFFFF;
}

bool safeDeleteFile(const char* filename) {
    /*
     * Safely delete a file, retrying multiple times to handle
     * file system delays or locking issues.
     */
    if (RF_FS_EXISTS(filename)) {
        for (int attempt = 0; attempt < 3; attempt++) {
            if (RF_FS_REMOVE(filename)) {
                if (!RF_FS_EXISTS(filename)) {
                    return true;
                }
            }
            delay(10);
        }
        return false;
    }
    return true;
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);
    
    Serial.begin(115200);
    
    // Initialize file system with selected storage mode
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        setLed(true); // Error indication
        return;
    }
    
    blinkLed(3, 200); // Ready indication
}

void loop() {
    // Check for incoming transfer command
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();

        if (command == "TRANSFER_V2") {
            receiveFileV2();
        }
    }

    delay(50);
}

void receiveFileV2() {
    // Receive filename length
    uint32_t filename_length = 0;
    if (Serial.readBytes((uint8_t*)&filename_length, 4) != 4) {
        return;
    }

    // Receive filename
    char filename[256];
    if (filename_length >= sizeof(filename)) {
        return;
    }
    if (Serial.readBytes((uint8_t*)filename, filename_length) != filename_length) {
        return;
    }
    filename[filename_length] = '\0';

    // Extract model name from filename (everything before last underscore)
    String filenameStr = String(filename);
    int lastUnderscore = filenameStr.lastIndexOf('_');
    String modelName = (lastUnderscore > 0) ? filenameStr.substring(0, lastUnderscore) : "default_model";
    
    // Create model directory if it doesn't exist
    String modelDir = "/" + modelName;
    if (!RF_FS_EXISTS(modelDir.c_str())) {
        RF_FS_MKDIR(modelDir.c_str());
    }

    // Receive file size, expected file CRC, and chunk size
    uint32_t file_size = 0;
    uint32_t file_crc_expected = 0;
    uint32_t chunk_size = 0;
    if (Serial.readBytes((uint8_t*)&file_size, 4) != 4) return;
    if (Serial.readBytes((uint8_t*)&file_crc_expected, 4) != 4) return;
    if (Serial.readBytes((uint8_t*)&chunk_size, 4) != 4) return;

    // Ensure chunk size doesn't exceed our buffer
    if (chunk_size > (uint32_t)BUFFER_CHUNK) {
        return;
    }

    String filepath = modelDir + "/" + String(filename);
    
    // Safe delete old file
    if (!safeDeleteFile(filepath.c_str())) {
        return;
    }
    
    File file = RF_FS_OPEN(filepath, RF_FILE_WRITE);
    if (!file) {
        return;
    }

    // Send ready for V2
    Serial.println("READY_V2");
    setLed(true); // Transfer in progress

    uint32_t bytes_received = 0;
    uint32_t crc = 0xFFFFFFFF;

    while (bytes_received < file_size) {
        // Header: offset (4), chunk_len (4), chunk_crc (4)
        uint32_t offset = 0, clen = 0, ccrc = 0;
        if (Serial.readBytes((uint8_t*)&offset, 4) != 4) { file.close(); RF_FS_REMOVE(filepath); return; }
        if (Serial.readBytes((uint8_t*)&clen, 4) != 4)   { file.close(); RF_FS_REMOVE(filepath); return; }
        if (Serial.readBytes((uint8_t*)&ccrc, 4) != 4)   { file.close(); RF_FS_REMOVE(filepath); return; }

        if (clen > chunk_size || clen == 0) { file.close(); RF_FS_REMOVE(filepath); return; }
        
        // Read chunk payload with streaming to support small Serial buffers
        size_t got = 0;
        bool chunkComplete = true;
        unsigned long chunk_start = millis();
        while (got < clen) {
            if (Serial.available()) {
                int value = Serial.read();
                if (value < 0) {
                    continue;
                }
                buffer[got++] = static_cast<uint8_t>(value);
                chunk_start = millis();
            } else {
                if (millis() - chunk_start >= 5000) {
                    Serial.print("NACK ");
                    Serial.print(offset);
                    Serial.print(" streamed=");
                    Serial.print(got);
                    Serial.println(" timeout");
                    Serial.flush();
                    delay(2);
                    chunkComplete = false;
                    break;
                }
                delay(1);
            }
        }
        if (!chunkComplete) {
            continue; // Sender will retry
        }

        // Compute CRC32 of the received chunk
        uint32_t calc = compute_crc32(buffer, clen);

        if (calc != ccrc) {
            Serial.print("NACK ");
            Serial.println(offset);
            delay(2);
            continue; // Sender will retry
        }

        // CRC matched; write to file
        size_t written = file.write(buffer, clen);
        if (written != clen) { file.close(); RF_FS_REMOVE(filepath); return; }

        // Update running CRC for entire file
        for (uint32_t i = 0; i < clen; ++i) {
            uint8_t b = buffer[i];
            crc ^= b;
            for (int j = 0; j < 8; ++j) {
                uint32_t mask = -(int)(crc & 1);
                crc = (crc >> 1) ^ (0xEDB88320 & mask);
            }
        }

        bytes_received += clen;
        Serial.print("ACK ");
        Serial.println(offset);
        delay(BUFFER_DELAY_MS);
    }

    // Wait for TRANSFER_END line
    {
        unsigned long start = millis();
        while (millis() - start < 3000) {
            if (Serial.available()) {
                String endcmd = Serial.readStringUntil('\n');
                endcmd.trim();
                if (endcmd == "TRANSFER_END") break;
            }
            delay(1);
        }
    }

    file.close();
    setLed(false);

    // Finalize file CRC
    crc ^= 0xFFFFFFFF;
    if (crc == file_crc_expected && bytes_received == file_size) {
        blinkLed(2, 100); // Success indication
        Serial.println("TRANSFER_COMPLETE");
    } else {
        RF_FS_REMOVE(filepath);
    }
}
