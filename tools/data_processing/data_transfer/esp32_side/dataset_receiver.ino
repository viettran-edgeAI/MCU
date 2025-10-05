/*
 * ESP32 Binary File Receiver
 * Upload this sketch to ESP32, then use transfer_dataset.py to send files
 * Saves files to LittleFS with model_name/filename structure
 */

#include "LittleFS.h"

// Transfer timing and size configuration
// IMPORTANT: Keep these in sync with the PC sender script (transfer_dataset.py)
// - BUFFER_CHUNK should match CHUNK_SIZE
// - BUFFER_DELAY_MS should reflect CHUNK_DELAY (in milliseconds)
const int BUFFER_CHUNK = 256;
const int BUFFER_DELAY_MS = 20;  // 0.05s to match CHUNK_DELAY=0.05
uint8_t buffer[BUFFER_CHUNK];

void setup() {
  Serial.begin(115200);
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    return;
  }
}

void loop() {
  // Check for incoming transfer command
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "TRANSFER_START") {
      // Legacy mode (no CRC/ACK) remains available
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
  
  // Extract model name from filename (everything before last underscore)
  // e.g., "digit_data_nml.bin" -> model_name = "digit_data"
  String filenameStr = String(filename);
  int lastUnderscore = filenameStr.lastIndexOf('_');
  String modelName = (lastUnderscore > 0) ? filenameStr.substring(0, lastUnderscore) : "default_model";
  
  // Create model directory if it doesn't exist
  String modelDir = "/" + modelName;
  if (!LittleFS.exists(modelDir)) {
    LittleFS.mkdir(modelDir);
  }
  
  // Receive file size
  uint32_t file_size = 0;
  if (Serial.readBytes((uint8_t*)&file_size, 4) != 4) {
    return;
  }
  
  // Prepare file path with model name
  String filepath = modelDir + "/" + String(filename);
  
  // Open file for writing
  File file = LittleFS.open(filepath, FILE_WRITE);
  if (!file) {
    return;
  }
  
  // Send ready signal
  Serial.println("READY");
  
  // Receive file data
  uint32_t bytes_received = 0;
  
  while (bytes_received < file_size) {
  uint32_t bytes_to_read = min((uint32_t)BUFFER_CHUNK, file_size - bytes_received);
    
    // Wait for data
    unsigned long start_time = millis();
    while (Serial.available() < bytes_to_read && (millis() - start_time) < 5000) {
      delay(1);
    }
    
    if (Serial.available() < bytes_to_read) {
      file.close();
      LittleFS.remove(filepath);
      return;
    }
    
    // Read data chunk
  size_t actual_read = Serial.readBytes(buffer, bytes_to_read);
    
    // Write to file
    size_t written = file.write(buffer, actual_read);
    if (written != actual_read) {
      file.close();
      LittleFS.remove(filepath);
      return;
    }
    
    bytes_received += actual_read;
    
    // Small delay between buffer writes
  delay(BUFFER_DELAY_MS);
  }
  
  file.close();
  
  // Verify file was saved correctly
  File verify_file = LittleFS.open(filepath, FILE_READ);
  if (verify_file && verify_file.size() == file_size) {
    verify_file.close();
    Serial.println("TRANSFER_COMPLETE");
  } else {
    if (verify_file) verify_file.close();
    LittleFS.remove(filepath);
  }
}

// V2: Robust receiver with per-chunk CRC and ACK/NACK
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
  if (!LittleFS.exists(modelDir)) {
    LittleFS.mkdir(modelDir);
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
  File file = LittleFS.open(filepath, FILE_WRITE);
  if (!file) {
    return;
  }

  // Send ready for V2
  Serial.println("READY_V2");

  uint32_t bytes_received = 0;
  // Simple CRC32 calculation using ESP32's ROM function isn't directly available here.
  // We'll accumulate CRC manually via a software implementation.
  uint32_t crc = 0xFFFFFFFF;

  while (bytes_received < file_size) {
    // Header: offset (4), chunk_len (4), chunk_crc (4)
    uint32_t offset = 0, clen = 0, ccrc = 0;
    if (Serial.readBytes((uint8_t*)&offset, 4) != 4) { file.close(); LittleFS.remove(filepath); return; }
    if (Serial.readBytes((uint8_t*)&clen, 4) != 4)   { file.close(); LittleFS.remove(filepath); return; }
    if (Serial.readBytes((uint8_t*)&ccrc, 4) != 4)   { file.close(); LittleFS.remove(filepath); return; }

    if (clen > chunk_size || clen == 0) { file.close(); LittleFS.remove(filepath); return; }
    // Read chunk payload
    size_t got = Serial.readBytes(buffer, clen);
    if (got != clen) { file.close(); LittleFS.remove(filepath); return; }

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
    // For simplicity since we receive in order, append write is okay.
    size_t written = file.write(buffer, clen);
    if (written != clen) { file.close(); LittleFS.remove(filepath); return; }

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
    LittleFS.remove(filepath);
  }
}

