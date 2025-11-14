/*
 * HOG Configuration Receiver for ESP32
 * 
 * This sketch receives HOG configuration files from a PC
 * and saves them to the ESP32 filesystem.
 * 
 * Files received:
 * - {model_name}_hogcfg.json (HOG configuration for ESP32)
 * 
 * The HOG config file contains all necessary parameters for:
 * - Image preprocessing (resize, format conversion)
 * - HOG feature extraction (cell size, block size, bins, etc.)
 * - ESP32-specific settings (input format, camera resolution, etc.)
 * 
 * Designed to work with 'transfer_hog_config.py' script.
 * 
 * PERFORMANCE NOTES:
 * - Transfer speed depends on CHUNK_SIZE; larger chunks are faster
 *   but may cause USB CDC buffer overruns on ESP32-C3-like boards.
 * - This sketch auto-detects the board and sets CHUNK_SIZE conservatively
 *   for C3, but allows user override via USER_CHUNK_SIZE define.
 * - See board_config.h for board-specific recommendations.
 */

#include "Rf_file_manager.h"  // Includes Rf_board_config.h internally

// --- Storage Configuration ---
// Choose one of the following storage modes:
//   RfStorageType::FLASH      - Internal LittleFS (default, ~1.5MB)
//   RfStorageType::SD_MMC_1BIT - Built-in SD slot (1-bit mode, safe with camera sharing)
//   RfStorageType::SD_MMC_4BIT - Built-in SD slot (4-bit mode, requires dedicated SD bus)
//   RfStorageType::SD_SPI     - External SD card module (SPI interface, compatible with all ESP32 variants)
const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

// --- Protocol Constants ---
// Must match the Python sender script
const uint8_t CMD_HEADER[] = "ESP32_XFER";
const uint8_t CMD_START_SESSION = 0x01;
const uint8_t CMD_FILE_INFO = 0x02;
const uint8_t CMD_FILE_CHUNK = 0x03;
const uint8_t CMD_END_SESSION = 0x04;

const char* RESP_ACK = "ACK";
const char* RESP_READY = "READY";
const char* RESP_OK = "OK";
const char* RESP_ERROR = "ERROR";

/*
 * CHUNK_SIZE Configuration:
 * 
 * This is the maximum bytes per transfer chunk. The PC script must match this value.
 * 
 * Trade-off:
 *   - Larger chunks = faster transfers
 *   - Smaller chunks = more reliable (less USB CDC buffer saturation)
 * 
 * Board-specific guidance:
 *   - ESP32-C3/C6: 220 bytes (USB CDC buffer ~384 bytes; conservative margin)
 *   - ESP32-S3:    256 bytes (larger CDC buffer)
 *   - ESP32:       256 bytes (standard board)
 * 
 * If you need higher speed on larger boards, define USER_CHUNK_SIZE before
 * including this file, or edit board_config.h to adjust DEFAULT_CHUNK_SIZE.
 * 
 * Default is set via board_config.h based on detected board variant.
 */
const uint16_t CHUNK_SIZE = USER_CHUNK_SIZE;
const uint32_t CHUNK_DELAY = 20;  // Delay between chunk processing
const uint32_t SERIAL_TIMEOUT_MS = 30000;  // Extended timeout
const uint32_t HEADER_WAIT_MS = 100;  // Wait time for header assembly

// --- State Machine ---
enum class State {
    WAITING_FOR_SESSION,
    WAITING_FOR_COMMAND,
    RECEIVING_FILE_INFO,
    RECEIVING_FILE_CHUNK,
    SESSION_ENDED,
    ERROR_STATE
};

State currentState = State::WAITING_FOR_SESSION;

// --- Global Variables ---
File currentFile;
char receivedSessionName[64];
char receivedModelName[64];  // Extracted from session name
char receivedFileName[64];
uint32_t receivedFileSize = 0;
uint32_t receivedFileCRC = 0;
uint32_t receivedChunkSize = 0;
uint32_t bytesWritten = 0;
uint32_t runningCRC = 0xFFFFFFFF;
uint16_t filesReceived = 0;

// --- LED Configuration ---
const uint8_t LED_PIN = 2;  // Built-in LED on most ESP32 boards

// --- Function Declarations ---
void setLed(bool on);
void blinkLed(int count, int duration);
bool read_header();
uint32_t compute_crc32(const uint8_t* data, size_t len);
bool safeDeleteFile(const char* filename);
void handleStartSession();
void handleCommand();
void handleFileInfo();
void handleFileChunk(); 
void handleEndSession();
void printStorageInfo();
void listReceivedFiles();

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
    // Ensure no file handle is open for this file
    if (currentFile && String(currentFile.name()) == String(filename)) {
        currentFile.close();
        currentFile = File();
    }
    
    // Try to delete the file multiple times if needed
    if (RF_FS_EXISTS(filename)) {
        for (int attempt = 0; attempt < 3; attempt++) {
            if (RF_FS_REMOVE(filename)) {
                // Verify it's actually deleted
                if (!RF_FS_EXISTS(filename)) {
                    return true;
                }
            }
            delay(10);
        }
        return false; // Failed to delete after multiple attempts
    }
    return true; // File doesn't exist, nothing to delete
}

void printStorageInfo() {
    size_t totalBytes = RF_TOTAL_BYTES();
    size_t usedBytes = RF_USED_BYTES();
    Serial.println("📊 File System Storage Info:");
    Serial.printf("   Total: %u bytes (%.1f KB)\n", totalBytes, totalBytes/1024.0);
    Serial.printf("   Used:  %u bytes (%.1f KB)\n", usedBytes, usedBytes/1024.0);
    Serial.printf("   Free:  %u bytes (%.1f KB)\n", totalBytes-usedBytes, (totalBytes-usedBytes)/1024.0);
}

void listReceivedFiles() {
    Serial.println("\n📁 HOG config files in file system:");
    File root = RF_FS_OPEN("/", RF_FILE_READ);
    
    if (!root) {
        Serial.println("   ❌ Failed to open root directory");
        return;
    }
    
    int fileCount = 0;
    int hogConfigFiles = 0;
    
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            // Check inside each directory for HOG config files
            String dirName = file.name();
            File subFile = file.openNextFile();
            while (subFile) {
                if (!subFile.isDirectory()) {
                    String fileName = subFile.name();
                    if (fileName.endsWith(".json") && fileName.indexOf("_hogcfg") >= 0) {
                        String fullPath = String(dirName) + "/" + String(fileName);
                        Serial.printf("   📋 %s (%u bytes) - HOG Configuration\n", fullPath.c_str(), subFile.size());
                        hogConfigFiles++;
                        fileCount++;
                    }
                }
                subFile = file.openNextFile();
            }
        }
        file = root.openNextFile();
    }
    
    if (fileCount == 0) {
        Serial.println("   (No config files found)");
    } else {
        Serial.printf("   Total: %d files", fileCount);
        if (hogConfigFiles > 0) {
            Serial.printf(" (%d HOG configs)", hogConfigFiles);
        }
        Serial.println();
        
        if (hogConfigFiles > 0) {
            Serial.println("   ✅ HOG configuration ready for use!");
            Serial.println("\n   💡 Load config in your code with:");
            Serial.println("      hog.loadConfigFromFile(\"/model_name/model_name_hogcfg.json\");");
        }
    }
}

void setup() {
    /*
     * Initialize Serial, storage, and LED.
     * Print board configuration (chunk size, board type) for diagnostics.
     */
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    Serial.begin(115200);
    Serial.setTimeout(SERIAL_TIMEOUT_MS);
    
    // Print board info at startup
    print_board_info();
    
    // Wait for serial connection
    while (!Serial) {
        delay(10);
    }
    
    delay(1000);  // Allow serial to stabilize
    
    Serial.println("\n==================================================");
    Serial.println("🤖 ESP32 HOG Configuration Receiver");
    Serial.println("    Ready to receive HOG config files");
    Serial.println("==================================================");

    // Initialize file system with selected storage mode
    Serial.print("💾 Initializing file system... ");
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  File system initialization failed. Cannot continue.");
        currentState = State::ERROR_STATE;
        return;
    }
    Serial.println("✅ OK");

    printStorageInfo();
    listReceivedFiles();
    
    Serial.println("\n🔌 Waiting for PC connection...");
    Serial.println("   Use: python3 transfer_hog_config.py <model_name> <serial_port>");
    
    blinkLed(3, 200);  // Signal ready
    setLed(true);      // Steady light = waiting for connection
}

void loop() {
    switch (currentState) {
        case State::WAITING_FOR_SESSION:
            handleStartSession();
            break;
        case State::WAITING_FOR_COMMAND:
            handleCommand();
            break;
        case State::RECEIVING_FILE_CHUNK:
            handleFileChunk();
            break;
        case State::SESSION_ENDED:
            // Session completed, show results
            static bool resultsShown = false;
            if (!resultsShown) {
                Serial.println("\n🎉 HOG config transfer completed!");
                Serial.printf("📈 Successfully received %d files\n", filesReceived);
                printStorageInfo();
                listReceivedFiles();
                Serial.println("\n✅ ESP32 is ready to use HOG feature extraction!");
                blinkLed(5, 150);  // Celebration blink
                setLed(false);
                resultsShown = true;
            }
            delay(1000);  // Idle state
            break;
        case State::ERROR_STATE:
            // Fast blink to indicate error
            setLed(!digitalRead(LED_PIN));
            delay(200);
            break;
    }
    
    yield();  // Allow other tasks to run
}

// --- Protocol Handlers ---

bool read_header() {
    uint32_t start_time = millis();
    while (Serial.available() < sizeof(CMD_HEADER) - 1) {
        if (millis() - start_time > HEADER_WAIT_MS) {
            return false;
        }
        delay(1);
        yield();
    }
    
    uint8_t header_buffer[sizeof(CMD_HEADER) - 1];
    size_t bytes_read = Serial.readBytes(header_buffer, sizeof(header_buffer));
    if (bytes_read != sizeof(header_buffer) || memcmp(header_buffer, CMD_HEADER, sizeof(header_buffer)) != 0) {
        return false; 
    }
    return true;
}

void handleStartSession() {
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;

    // Wait for command byte
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_START_SESSION) return;

    // Wait for session name length
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t session_len = Serial.read();
    
    // Wait for complete session name
    while (Serial.available() < session_len) {
        delay(1);
        yield();
    }
    Serial.readBytes(receivedSessionName, session_len);
    receivedSessionName[session_len] = '\0';

    // Extract model name from session name (remove "_hog_config" suffix)
    // Session name format: "model_name_hog_config"
    String sessionStr = String(receivedSessionName);
    int configSuffixIdx = sessionStr.lastIndexOf("_hog_config");
    if (configSuffixIdx > 0) {
        sessionStr = sessionStr.substring(0, configSuffixIdx);
    }
    strncpy(receivedModelName, sessionStr.c_str(), sizeof(receivedModelName) - 1);
    receivedModelName[sizeof(receivedModelName) - 1] = '\0';

    Serial.printf("\n🚀 Starting transfer session: %s\n", receivedSessionName);
    Serial.printf("📁 Model name: %s\n", receivedModelName);
    
    // Create model directory if it doesn't exist
    char modelDir[80];
    snprintf(modelDir, sizeof(modelDir), "/%s", receivedModelName);
    Serial.printf("🔍 Checking directory: %s\n", modelDir);
    if (!RF_FS_EXISTS(modelDir)) {
        Serial.printf("📂 Creating directory: %s\n", modelDir);
        if (RF_FS_MKDIR(modelDir)) {
            Serial.printf("✅ Created model directory: %s\n", modelDir);
        } else {
            Serial.printf("❌ ERROR: Could not create directory %s\n", modelDir);
            Serial.print(RESP_ERROR);
            Serial.flush();
            currentState = State::ERROR_STATE;
            return;
        }
    } else {
        Serial.printf("✅ Directory already exists: %s\n", modelDir);
    }
    
    filesReceived = 0;
    
    Serial.print(RESP_READY);
    Serial.flush();
    setLed(false);  // Turn off waiting light
    currentState = State::WAITING_FOR_COMMAND;
}

void handleCommand() {
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;

    uint8_t command = Serial.read();
    switch (command) {
        case CMD_FILE_INFO:
            handleFileInfo();
            break;
        case CMD_END_SESSION:
            handleEndSession();
            break;
        default:
            break;
    }
}

void handleFileInfo() {
    // Wait for filename length
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t filename_len = Serial.read();
    
    // Wait for complete filename
    while (Serial.available() < filename_len) {
        delay(1);
        yield();
    }
    Serial.readBytes(receivedFileName, filename_len);
    receivedFileName[filename_len] = '\0';

    // Construct full path with model name: /model_name/filename
    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "/%s/%s", receivedModelName, receivedFileName);
    strncpy(receivedFileName, fullPath, sizeof(receivedFileName) - 1);
    receivedFileName[sizeof(receivedFileName) - 1] = '\0';

    // Wait for file size (4 bytes), file CRC (4 bytes), and chunk size (4 bytes)
    while (Serial.available() < 12) {
        delay(1);
        yield();
    }
    Serial.readBytes((uint8_t*)&receivedFileSize, sizeof(receivedFileSize));
    Serial.readBytes((uint8_t*)&receivedFileCRC, sizeof(receivedFileCRC));
    Serial.readBytes((uint8_t*)&receivedChunkSize, sizeof(receivedChunkSize));
    
    bytesWritten = 0;
    runningCRC = 0xFFFFFFFF;

    // Ensure chunk size doesn't exceed our buffer
    if (receivedChunkSize > (uint32_t)CHUNK_SIZE) {
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }

    // Close previous file if open
    if (currentFile) {
        currentFile.close();
        currentFile = File();
    }
    
    // Use safe delete function
    if (!safeDeleteFile(receivedFileName)) {
        Serial.printf("❌ Failed to delete existing file: %s\n", receivedFileName);
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }
    
    Serial.printf("📥 Receiving HOG Config: %s (%u bytes)\n", receivedFileName, receivedFileSize);
    
    currentFile = RF_FS_OPEN(receivedFileName, RF_FILE_WRITE);
    if (!currentFile) {
        Serial.printf("❌ Failed to open file for writing: %s\n", receivedFileName);
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        Serial.flush();
        return;
    }

    setLed(true);  // Turn LED on during transfer
    Serial.print(RESP_ACK);
    Serial.flush();
    currentState = State::RECEIVING_FILE_CHUNK;
}

void handleFileChunk() {
    // V2 Protocol: expect chunks with offset, length, and CRC
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;
    
    // Wait for command byte
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_FILE_CHUNK) {
        // If not a chunk, go back to command processing
        currentState = State::WAITING_FOR_COMMAND;
        return;
    }

    // Read chunk header: offset (4), chunk_len (4), chunk_crc (4)
    uint32_t offset = 0, chunk_len = 0, chunk_crc = 0;
    
    // Wait for complete header (12 bytes)
    uint32_t start_time = millis();
    while (Serial.available() < 12) {
        if (millis() - start_time > 5000) {
            Serial.print(RESP_ERROR);
            Serial.flush();
            currentState = State::ERROR_STATE;
            return;
        }
        delay(1);
        yield();
    }
    
    Serial.readBytes((uint8_t*)&offset, 4);
    Serial.readBytes((uint8_t*)&chunk_len, 4);
    Serial.readBytes((uint8_t*)&chunk_crc, 4);

    if (chunk_len > receivedChunkSize || chunk_len == 0) {
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }

    // Read chunk payload with streaming to handle small Serial buffers
    uint8_t buffer[CHUNK_SIZE];
    size_t bytesRead = 0;
    start_time = millis();
    while (bytesRead < chunk_len) {
        if (Serial.available()) {
            int value = Serial.read();
            if (value < 0) {
                continue;
            }
            buffer[bytesRead++] = static_cast<uint8_t>(value);
            start_time = millis();
        } else {
            if (millis() - start_time > 5000) {
                Serial.print("NACK ");
                Serial.print(offset);
                Serial.print(" streamed=");
                Serial.print(bytesRead);
                Serial.println(" timeout");
                Serial.flush();
                return; // Sender will retry
            }
            delay(1);
            yield();
        }
    }

    // Compute CRC32 of the received chunk
    uint32_t calc_crc = compute_crc32(buffer, chunk_len);

    if (calc_crc != chunk_crc) {
        Serial.print("NACK ");
        Serial.println(offset);
        Serial.flush();
        delay(2);
        return; // CRC mismatch, sender will retry
    }

    // CRC matched; write to file
    size_t written = currentFile.write(buffer, chunk_len);
    if (written != chunk_len) {
        Serial.printf("❌ Write error for %s\n", receivedFileName);
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        Serial.flush();
        return;
    }

    // Update running CRC for entire file
    for (uint32_t i = 0; i < chunk_len; ++i) {
        uint8_t b = buffer[i];
        runningCRC ^= b;
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = -(int)(runningCRC & 1);
            runningCRC = (runningCRC >> 1) ^ (0xEDB88320 & mask);
        }
    }
    
    bytesWritten += chunk_len;
    
    // Periodic flush for stability
    if (bytesWritten % (CHUNK_SIZE * 4) == 0) {
        currentFile.flush();
    }
    
    delay(CHUNK_DELAY);
    yield();

    Serial.print("ACK ");
    Serial.println(offset);
    Serial.flush();

    if (bytesWritten >= receivedFileSize) {
        currentFile.flush();
        currentFile.close();
        
        // Finalize and verify file CRC
        runningCRC ^= 0xFFFFFFFF;
        if (runningCRC == receivedFileCRC && bytesWritten == receivedFileSize) {
            filesReceived++;
            Serial.printf("✅ Saved: %s (%u bytes) - CRC verified\n", receivedFileName, receivedFileSize);
            setLed(false);
            blinkLed(2, 100);  // Quick success blink
            currentState = State::WAITING_FOR_COMMAND;
        } else {
            // CRC mismatch, remove file
            RF_FS_REMOVE(receivedFileName);
            Serial.printf("❌ CRC mismatch for %s - file removed\n", receivedFileName);
            currentState = State::ERROR_STATE;
            Serial.print(RESP_ERROR);
            Serial.flush();
        }
    }
}

void handleEndSession() {
    if (currentFile) {
        currentFile.close();
    }
    
    setLed(false);
    Serial.print(RESP_OK);
    Serial.flush();
    currentState = State::SESSION_ENDED;
}
