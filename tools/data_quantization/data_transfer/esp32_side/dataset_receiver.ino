/*
 * ESP32 Binary File Receiver
 * Upload this sketch to ESP32, then use transfer_dataset.py to send files.
 * Saves files to file system with model_name/filename structure.
 */

#include "Rf_file_manager.h"  // Includes Rf_board_config.h internally

// --- Storage Configuration ---
// Choose one of the following storage modes:
//   RfStorageType::FLASH      - Internal LittleFS (default, ~1.5MB)
//   RfStorageType::SD_MMC_1BIT - Built-in SD slot (1-bit mode)
//   RfStorageType::SD_MMC_4BIT - Built-in SD slot (4-bit mode)
//   RfStorageType::SD_SPI     - External SD card module (SPI interface, compatible with all ESP32 variants)

const RfStorageType STORAGE_MODE = RfStorageType::FLASH;


const int BUFFER_CHUNK = USER_CHUNK_SIZE;
const int BUFFER_DELAY_MS = 20;  // ms to match CHUNK_DELAY (0.02s) on PC
uint8_t buffer[BUFFER_CHUNK];

void setup() {
  Serial.begin(115200);
  
  // Print board configuration at startup
  print_board_info();
  
  // Initialize file system with selected storage mode
  if (!RF_FS_BEGIN(STORAGE_MODE)) {
    return;
  }
}

void loop() {
  // Check for incoming transfer command
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "TRANSFER_START") {
      receiveFile();
    } else if (command == "TRANSFER_V2") {
      receiveFileV2();
    }
  }

  delay(50);
}

void receiveFile() {
  // Receive filename length
  uint32_t filename_length = 0;
  if (Serial.readBytes((uint8_t*)&filename_length, 4) != 4) {
    return;
  }
  
  // Receive filename
  char filename[256];
  if (Serial.readBytes((uint8_t*)filename, filename_length) != filename_length) {
    return;
  }
  filename[filename_length] = '\0';
  
  String filenameStr = String(filename);
  int lastUnderscore = filenameStr.lastIndexOf('_');
  String modelName = (lastUnderscore > 0) ? filenameStr.substring(0, lastUnderscore) : "default_model";

  String modelDir = "/" + modelName;
  if (!RF_FS_EXISTS(modelDir.c_str())) {
    RF_FS_MKDIR(modelDir.c_str());
  }
  
  // Receive file size
  uint32_t file_size = 0;
  if (Serial.readBytes((uint8_t*)&file_size, 4) != 4) {
    return;
  }
  
  String filepath = modelDir + "/" + String(filename);
  File file = RF_FS_OPEN(filepath, RF_FILE_WRITE);
  if (!file) {
    return;
  }
  
  // Send ready signal
  Serial.println("READY");
  
  // Receive file data
  uint32_t bytes_received = 0;
  
  while (bytes_received < file_size) {
  uint32_t bytes_to_read = min((uint32_t)BUFFER_CHUNK, file_size - bytes_received);
    
    // Read data chunk with streaming to handle small buffers
    size_t actual_read = 0;
    unsigned long start_time = millis();
    while (actual_read < bytes_to_read) {
      if (Serial.available()) {
        int value = Serial.read();
        if (value < 0) {
          continue;
        }
        buffer[actual_read++] = static_cast<uint8_t>(value);
        start_time = millis();
      } else {
        if (millis() - start_time >= 5000) {
          file.close();
          RF_FS_REMOVE(filepath.c_str());
          return;
        }
        delay(1);
      }
    }
  
    size_t written = file.write(buffer, actual_read);
    if (written != actual_read) {
      file.close();
      RF_FS_REMOVE(filepath.c_str());
      return;
    }
    
    bytes_received += actual_read;
    
    // Small delay between buffer writes
  delay(BUFFER_DELAY_MS);
  }
  
  file.close();
  
  // Verify file was saved correctly
  File verify_file = RF_FS_OPEN(filepath, RF_FILE_READ);
  if (verify_file && verify_file.size() == file_size) {
    verify_file.close();
    Serial.println("TRANSFER_COMPLETE");
  } else {
    if (verify_file) verify_file.close();
    RF_FS_REMOVE(filepath.c_str());
  }
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
    // Can't accept; avoid buffer overflow
    return;
  }

  String filepath = modelDir + "/" + String(filename);
  File file = RF_FS_OPEN(filepath, RF_FILE_WRITE);
  if (!file) {
    return;
  }

  // Send ready for V2
  Serial.println("READY_V2");

  uint32_t bytes_received = 0;
  uint32_t crc = 0xFFFFFFFF;

  while (bytes_received < file_size) {
    // Header: offset (4), chunk_len (4), chunk_crc (4)
    uint32_t offset = 0, clen = 0, ccrc = 0;
    if (Serial.readBytes((uint8_t*)&offset, 4) != 4) { file.close(); RF_FS_REMOVE(filepath.c_str()); return; }
    if (Serial.readBytes((uint8_t*)&clen, 4) != 4)   { file.close(); RF_FS_REMOVE(filepath.c_str()); return; }
    if (Serial.readBytes((uint8_t*)&ccrc, 4) != 4)   { file.close(); RF_FS_REMOVE(filepath.c_str()); return; }

    if (clen > chunk_size || clen == 0) { file.close(); RF_FS_REMOVE(filepath.c_str()); return; }
    // Read chunk payload with streaming
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
      continue; // Sender will retry this chunk
    }

    // Compute CRC32 of the received chunk
    uint32_t calc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < clen; ++i) {
      uint8_t b = buffer[i];
      calc ^= b;
      for (int j = 0; j < 8; ++j) {
        uint32_t mask = -(int)(calc & 1);
        calc = (calc >> 1) ^ (0xEDB88320 & mask);
      }
    }
    calc ^= 0xFFFFFFFF;

    if (calc != ccrc) {
      Serial.print("NACK ");
      Serial.println(offset);
      delay(2);
      continue; // Sender will retry
    }

    // CRC matched; write to file at the correct position if needed
    size_t written = file.write(buffer, clen);
    if (written != clen) { file.close(); RF_FS_REMOVE(filepath.c_str()); return; }

    // Update running CRC for entire file (same polynomial)
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

  // Wait for TRANSFER_END line before finalizing
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

  // Finalize file CRC
  crc ^= 0xFFFFFFFF;
  if (crc == file_crc_expected && bytes_received == file_size) {
    Serial.println("TRANSFER_COMPLETE");
  } else {
    RF_FS_REMOVE(filepath.c_str());
  }
}

