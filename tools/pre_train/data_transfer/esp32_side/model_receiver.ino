/*
 * Pre-trained Model Receiver for ESP32
 * 
 * This sketch receives pre-trained random forest model files from a PC
 * and saves them to LittleFS with /model_name/filename structure.
 * 
 * Files received:
 * - {model_name}_config.json (model configuration)
 * - {model_name}_node_pred.bin (memory estimation model)
 * - {model_name}_node_log.csv (training history and statistics)
 * - {model_name}_forest.bin (unified forest file containing all decision trees)
 * 
 * The unified forest file replaces individual tree files for more efficient
 * storage and loading on the ESP32.
 * 
 * The node predictor enables accurate memory usage estimation for 
 * random forest configurations before model creation.
 * 
 * The tree log provides training history for analysis and debugging.
 * 
 * Designed to work with 'transfer_model.py' script.
 */

#include "Arduino.h"
#include "FS.h"
#include "LittleFS.h"

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

const uint16_t CHUNK_SIZE = 256;  // Must match PC side
const uint32_t CHUNK_DELAY = 20;  // Delay between chunk processing
const uint32_t SERIAL_TIMEOUT_MS = 30000;  // Extended timeout for large files
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
    if (LittleFS.exists(filename)) {
        for (int attempt = 0; attempt < 3; attempt++) {
            if (LittleFS.remove(filename)) {
                // Verify it's actually deleted
                if (!LittleFS.exists(filename)) {
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
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    Serial.println("📊 LittleFS Storage Info:");
    Serial.printf("   Total: %u bytes (%.1f KB)\n", totalBytes, totalBytes/1024.0);
    Serial.printf("   Used:  %u bytes (%.1f KB)\n", usedBytes, usedBytes/1024.0);
    Serial.printf("   Free:  %u bytes (%.1f KB)\n", totalBytes-usedBytes, (totalBytes-usedBytes)/1024.0);
}

void listReceivedFiles() {
    Serial.println("\n📁 Model files in LittleFS:");
    File root = LittleFS.open("/");
    if (!root) {
        Serial.println("   ❌ Failed to open root directory");
        return;
    }
    
    int fileCount = 0;
    int configFiles = 0;
    int forestFiles = 0;
    int predictorFiles = 0;
    int logFiles = 0;
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = file.name();
            if (fileName.endsWith(".json") && fileName.indexOf("_config") >= 0) {
                Serial.printf("   📋 %s (%u bytes) - Configuration\n", fileName.c_str(), file.size());
                configFiles++;
                fileCount++;
            } else if (fileName.indexOf("node_pred") >= 0 && fileName.endsWith(".bin")) {
                Serial.printf("   🧮 %s (%u bytes) - Node Predictor\n", fileName.c_str(), file.size());
                predictorFiles++;
                fileCount++;
            } else if (fileName.indexOf("node_log") >= 0 && fileName.endsWith(".csv")) {
                Serial.printf("   📊 %s (%u bytes) - Training Log\n", fileName.c_str(), file.size());
                logFiles++;
                fileCount++;
            } else if (fileName.indexOf("_forest") >= 0 && fileName.endsWith(".bin")) {
                Serial.printf("   🌳 %s (%u bytes) - Unified Forest\n", fileName.c_str(), file.size());
                forestFiles++;
                fileCount++;
            } else if (fileName.endsWith(".bin")) {
                Serial.printf("   📄 %s (%u bytes)\n", fileName.c_str(), file.size());
                fileCount++;
            } else if (fileName.endsWith(".csv")) {
                Serial.printf("   📊 %s (%u bytes) - CSV Data\n", fileName.c_str(), file.size());
                logFiles++;
                fileCount++;
            }
        }
        file = root.openNextFile();
    }
    
    if (fileCount == 0) {
        Serial.println("   (No model files found)");
    } else {
        Serial.printf("   Total: %d model files", fileCount);
        if (configFiles > 0 || predictorFiles > 0 || logFiles > 0 || forestFiles > 0) {
            Serial.printf(" (%d config, %d predictor, %d logs, %d forests)", configFiles, predictorFiles, logFiles, forestFiles);
        }
        Serial.println();
        
        // Check if we have a complete model
        if (configFiles > 0 && forestFiles > 0) {
            Serial.println("   ✅ Complete model ready for use!");
            if (predictorFiles > 0) {
                Serial.println("   🧮 Memory estimation available!");
            } else {
                Serial.println("   ⚠️  No node predictor - using default memory estimation");
            }
            if (logFiles > 0) {
                Serial.println("   📊 Training history available!");
            }
        } else {
            Serial.println("   ⚠️  Incomplete model (need config + unified forest)");
        }
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    Serial.begin(115200);
    Serial.setTimeout(SERIAL_TIMEOUT_MS);
    
    // Wait for serial connection
    while (!Serial) {
        delay(10);
    }
    
    delay(1000);  // Allow serial to stabilize
    
    Serial.println("\n==================================================");
    Serial.println("🤖 ESP32 Pre-trained Model Receiver");
    Serial.println("    Ready to receive Random Forest model files");
    Serial.println("=================================================");

    // Initialize LittleFS
    Serial.print("💾 Initializing LittleFS... ");
    if (!LittleFS.begin(true)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  LittleFS initialization failed. Cannot continue.");
        currentState = State::ERROR_STATE;
        return;
    }
    Serial.println("✅ OK");

    printStorageInfo();
    listReceivedFiles();
    
    Serial.println("\n🔌 Waiting for PC connection...");
    Serial.println("   Use: python3 transfer_model.py <serial_port>");
    
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
                Serial.println("\n🎉 Model transfer completed!");
                Serial.printf("📈 Successfully received %d files\n", filesReceived);
                printStorageInfo();
                listReceivedFiles();
                Serial.println("\n✅ ESP32 is ready to use the trained model!");
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

    // Extract model name from session name (remove "_transfer" suffix)
    // Session name format: "model_name_transfer"
    String sessionStr = String(receivedSessionName);
    int transferSuffixIdx = sessionStr.lastIndexOf("_transfer");
    if (transferSuffixIdx > 0) {
        sessionStr = sessionStr.substring(0, transferSuffixIdx);
    }
    strncpy(receivedModelName, sessionStr.c_str(), sizeof(receivedModelName) - 1);
    receivedModelName[sizeof(receivedModelName) - 1] = '\0';

    Serial.printf("\n🚀 Starting transfer session: %s\n", receivedSessionName);
    Serial.printf("📁 Model name: %s\n", receivedModelName);
    
    // Create model directory if it doesn't exist
    char modelDir[80];
    snprintf(modelDir, sizeof(modelDir), "/%s", receivedModelName);
    if (!LittleFS.exists(modelDir)) {
        if (LittleFS.mkdir(modelDir)) {
            Serial.printf("✅ Created model directory: %s\n", modelDir);
        } else {
            Serial.printf("⚠️  Warning: Could not create directory %s\n", modelDir);
        }
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
    char baseFileName[64];
    Serial.readBytes(baseFileName, filename_len);
    baseFileName[filename_len] = '\0';

    // Construct full path with model name: /model_name/filename
    snprintf(receivedFileName, sizeof(receivedFileName), "/%s/%s", receivedModelName, baseFileName);

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

    // Determine file type for better user feedback
    String fileType = "📄 File";
    if (strstr(receivedFileName, ".json") && strstr(receivedFileName, "_config")) {
        fileType = "📋 Configuration";
    } else if (strstr(receivedFileName, "node_pred") && strstr(receivedFileName, ".bin")) {
        fileType = "🧮 Node Predictor";
    } else if (strstr(receivedFileName, "node_log") && strstr(receivedFileName, ".csv")) {
        fileType = "📊 Training Log";
    } else if (strstr(receivedFileName, "_forest") && strstr(receivedFileName, ".bin")) {
        fileType = "🌳 Unified Forest";
    } else if (strstr(receivedFileName, ".csv")) {
        fileType = "📊 CSV Data";
    } else if (strstr(receivedFileName, ".bin")) {
        fileType = "📄 Binary Data";
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
    
    Serial.printf("📥 Receiving %s: %s (%u bytes)\n", fileType.c_str(), baseFileName, receivedFileSize);
    
    currentFile = LittleFS.open(receivedFileName, FILE_WRITE);
    if (!currentFile) {
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

    // Read chunk payload
    uint8_t buffer[CHUNK_SIZE];
    start_time = millis();
    while (Serial.available() < chunk_len) {
        if (millis() - start_time > 5000) {
            Serial.print("NACK ");
            Serial.println(offset);
            Serial.flush();
            return; // Sender will retry
        }
        delay(1);
        yield();
    }
    
    size_t bytesRead = Serial.readBytes(buffer, chunk_len);
    if (bytesRead != chunk_len) {
        Serial.print("NACK ");
        Serial.println(offset);
        Serial.flush();
        return; // Sender will retry
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
            LittleFS.remove(receivedFileName);
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
