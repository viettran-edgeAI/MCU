/**
 * ESP32 Dataset Receiver Sketch
 * 
 * This sketch receives binary dataset files via Serial communication
 * and saves them to the ESP32 filesystem (SPIFFS/LittleFS).
 * 
 * Protocol:
 * 1. Receives start command with filename and file size
 * 2. Receives data in chunks with acknowledgments
 * 3. Receives end command and finalizes the file
 * 
 * Usage:
 * 1. Upload this sketch to ESP32
 * 2. Close Serial Monitor in Arduino IDE
 * 3. Run: python3 transfer_to_esp32.py <file> <port> <esp32_path>
 * 
 * Compatible with: ESP32, ESP32-S2, ESP32-S3, ESP32-C3
 */

#include "FS.h"
#include "SPIFFS.h"
// Alternative: Use LittleFS instead of SPIFFS
// #include "LittleFS.h"
// #define SPIFFS LittleFS

// Protocol constants (must match Python script)
const char HEADER_MAGIC[] = "ESP32BIN";
const uint8_t CMD_START_TRANSFER = 0x01;
const uint8_t CMD_DATA_CHUNK = 0x02;
const uint8_t CMD_END_TRANSFER = 0x03;
const uint8_t CMD_ACK = 0xAA;
const uint8_t CMD_NACK = 0x55;

const int CHUNK_SIZE = 512;  // Must match Python script
const int SERIAL_TIMEOUT = 5000;  // 5 seconds
const int MAX_FILENAME_LENGTH = 64;

// Configuration
#define DEBUG_TRANSFER false  // Set to false to disable debug output during transfer

// Global variables
File currentFile;
String currentFilename;
uint32_t expectedFileSize = 0;
uint32_t receivedBytes = 0;
uint16_t expectedChunk = 0;
bool transferInProgress = false;

void setup() {
  Serial.begin(115200);
  
  // Wait for serial connection (optional for debugging)
  delay(1000);
  
  // Initialize filesystem
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS initialization failed!");
    while (1) delay(1000);
  }
  
  Serial.println("=== ESP32 Dataset Receiver ===");
  Serial.println("✅ Ready to receive binary files");
  Serial.println("📁 Filesystem: SPIFFS");
  
  // Print filesystem info
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  Serial.printf("💾 Storage: %d / %d bytes used (%.1f%%)\n", 
                usedBytes, totalBytes, (float)usedBytes / totalBytes * 100);
  
  Serial.println("🔌 Waiting for transfer commands...");
  Serial.println();
}

void loop() {
  handleSerialCommand();
  delay(10);  // Small delay to prevent excessive CPU usage
}

void sendAck() {
  Serial.write(CMD_ACK);
  Serial.flush();
}

void sendNack() {
  Serial.write(CMD_NACK);
  Serial.flush();
}

bool readExactBytes(uint8_t* buffer, int length, int timeout = SERIAL_TIMEOUT) {
  unsigned long startTime = millis();
  int bytesRead = 0;
  
  while (bytesRead < length && (millis() - startTime) < timeout) {
    if (Serial.available()) {
      buffer[bytesRead] = Serial.read();
      bytesRead++;
    }
    yield();  // Allow other tasks to run
  }
  
  return bytesRead == length;
}

void handleStartTransfer() {
  if (DEBUG_TRANSFER) Serial.println("📤 Receiving start command...");
  
  // Read filename (64 bytes, null-terminated)
  uint8_t filenameBuffer[MAX_FILENAME_LENGTH];
  if (!readExactBytes(filenameBuffer, MAX_FILENAME_LENGTH)) {
    if (DEBUG_TRANSFER) Serial.println("❌ Failed to read filename");
    sendNack();
    return;
  }
  
  // Read file size (4 bytes, little-endian)
  uint8_t sizeBuffer[4];
  if (!readExactBytes(sizeBuffer, 4)) {
    if (DEBUG_TRANSFER) Serial.println("❌ Failed to read file size");
    sendNack();
    return;
  }
  
  // Parse filename and file size
  currentFilename = String((char*)filenameBuffer);
  expectedFileSize = sizeBuffer[0] | (sizeBuffer[1] << 8) | (sizeBuffer[2] << 16) | (sizeBuffer[3] << 24);
  
  if (DEBUG_TRANSFER) {
    Serial.printf("📁 Filename: %s\n", currentFilename.c_str());
    Serial.printf("📊 File size: %d bytes\n", expectedFileSize);
  }
  
  // Validate filename
  if (currentFilename.length() == 0 || !currentFilename.startsWith("/")) {
    if (DEBUG_TRANSFER) Serial.println("❌ Invalid filename - must start with '/'");
    sendNack();
    return;
  }
  
  // Check available space
  size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
  if (expectedFileSize > freeBytes) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Insufficient space: need %d bytes, have %d bytes\n", expectedFileSize, freeBytes);
    sendNack();
    return;
  }
  
  // Remove existing file if it exists
  if (SPIFFS.exists(currentFilename)) {
    if (DEBUG_TRANSFER) Serial.println("🗑️  Removing existing file");
    SPIFFS.remove(currentFilename);
  }
  
  // Open file for writing
  currentFile = SPIFFS.open(currentFilename, FILE_WRITE);
  if (!currentFile) {
    if (DEBUG_TRANSFER) Serial.println("❌ Failed to create file");
    sendNack();
    return;
  }
  
  // Initialize transfer state
  receivedBytes = 0;
  expectedChunk = 0;
  transferInProgress = true;
  
  if (DEBUG_TRANSFER) Serial.println("✅ Ready to receive data");
  sendAck();
}

void handleDataChunk() {
  if (DEBUG_TRANSFER) Serial.println("📦 Receiving data chunk...");
  
  if (!transferInProgress) {
    if (DEBUG_TRANSFER) Serial.println("❌ No transfer in progress");
    sendNack();
    return;
  }
  
  // Read chunk index (2 bytes, little-endian)
  uint8_t indexBuffer[2];
  if (!readExactBytes(indexBuffer, 2)) {
    if (DEBUG_TRANSFER) Serial.println("❌ Failed to read chunk index");
    sendNack();
    return;
  }
  
  // Read chunk size (2 bytes, little-endian)
  uint8_t sizeBuffer[2];
  if (!readExactBytes(sizeBuffer, 2)) {
    if (DEBUG_TRANSFER) Serial.println("❌ Failed to read chunk size");
    sendNack();
    return;
  }
  
  uint16_t chunkIndex = indexBuffer[0] | (indexBuffer[1] << 8);
  uint16_t chunkSize = sizeBuffer[0] | (sizeBuffer[1] << 8);
  
  if (DEBUG_TRANSFER) Serial.printf("📦 Chunk info: index=%d, size=%d, expected=%d\n", chunkIndex, chunkSize, expectedChunk);
  
  // Validate chunk
  if (chunkIndex != expectedChunk) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Wrong chunk order: expected %d, got %d\n", expectedChunk, chunkIndex);
    sendNack();
    return;
  }
  
  if (chunkSize > CHUNK_SIZE) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Chunk too large: %d bytes (max %d)\n", chunkSize, CHUNK_SIZE);
    sendNack();
    return;
  }
  
  // Read chunk data
  uint8_t chunkBuffer[CHUNK_SIZE];
  if (!readExactBytes(chunkBuffer, chunkSize)) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Failed to read chunk data (%d bytes)\n", chunkSize);
    sendNack();
    return;
  }
  
  // Write to file
  size_t written = currentFile.write(chunkBuffer, chunkSize);
  if (written != chunkSize) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Write error: wrote %d of %d bytes\n", written, chunkSize);
    sendNack();
    return;
  }
  
  receivedBytes += chunkSize;
  expectedChunk++;
  
  // Progress indicator
  if (DEBUG_TRANSFER) {
    float progress = (float)receivedBytes / expectedFileSize * 100.0;
    Serial.printf("📦 Chunk %d: %d bytes (%.1f%% complete)\n", chunkIndex, chunkSize, progress);
  }
  
  sendAck();
}

void handleEndTransfer() {
  if (DEBUG_TRANSFER) Serial.println("📤 Receiving end command...");
  
  if (!transferInProgress) {
    if (DEBUG_TRANSFER) Serial.println("❌ No transfer in progress");
    sendNack();
    return;
  }
  
  // Close file
  currentFile.close();
  transferInProgress = false;
  
  // Verify file size
  if (receivedBytes != expectedFileSize) {
    if (DEBUG_TRANSFER) Serial.printf("❌ Size mismatch: expected %d, received %d\n", expectedFileSize, receivedBytes);
    SPIFFS.remove(currentFilename);  // Remove incomplete file
    sendNack();
    return;
  }
  
  // Verify file exists and has correct size
  if (!SPIFFS.exists(currentFilename)) {
    if (DEBUG_TRANSFER) Serial.println("❌ File not found after transfer");
    sendNack();
    return;
  }
  
  File verifyFile = SPIFFS.open(currentFilename, FILE_READ);
  if (!verifyFile) {
    if (DEBUG_TRANSFER) Serial.println("❌ Cannot open file for verification");
    sendNack();
    return;
  }
  
  size_t actualSize = verifyFile.size();
  verifyFile.close();
  
  if (actualSize != expectedFileSize) {
    if (DEBUG_TRANSFER) Serial.printf("❌ File size verification failed: expected %d, actual %d\n", expectedFileSize, actualSize);
    SPIFFS.remove(currentFilename);
    sendNack();
    return;
  }
  
  // Always show completion message
  Serial.println("🎉 Transfer completed successfully!");
  Serial.printf("📁 File saved: %s (%d bytes)\n", currentFilename.c_str(), receivedBytes);
  
  // Print updated filesystem info
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  Serial.printf("💾 Storage: %d / %d bytes used (%.1f%%)\n", 
                usedBytes, totalBytes, (float)usedBytes / totalBytes * 100);
  
  Serial.println("🔄 Ready for next transfer");
  Serial.println();
  
  sendAck();
}



