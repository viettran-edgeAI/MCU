/*
 * Unified File Receiver for ESP32
 * 
 * This sketch receives all necessary model files from a PC and saves them 
 * to the file system with /model_name/filename structure.
 * 
 * Files received:
 * - {model_name}_nml.bin (normalized dataset)
 * - {model_name}_hogcfg.json (HOG configuration, optional)
 * - {model_name}_config.json (model configuration)
 * - {model_name}_forest.bin (unified forest file)
 * - {model_name}_npd.bin (node predictor model, optional)
 * - {model_name}_nlg.csv (training log, optional)
 * 
 * All files are saved to /{model_name}/ directory on the ESP32 filesystem.
 * 
 * Designed to work with 'unifier_transfer.py' script.
 * 
 * Usage: python3 unifier_transfer.py <model_name> <serial_port>
 */

#include "Rf_file_manager.h"  // Includes Rf_board_config.h internally

// --- Storage Configuration ---
const RfStorageType STORAGE_MODE = RfStorageType::FLASH;

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
 * Auto-synced with Rf_board_config.h via USER_CHUNK_SIZE
 */
const uint16_t CHUNK_SIZE = USER_CHUNK_SIZE;
const uint32_t CHUNK_DELAY = 20;  // Delay between chunk processing
const uint32_t SERIAL_TIMEOUT_MS = 30000;
const uint32_t HEADER_WAIT_MS = 100;

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
char receivedModelName[64];
char receivedFileName[128];
uint32_t receivedFileSize = 0;
uint32_t receivedFileCRC = 0;
uint32_t receivedChunkSize = 0;
uint32_t bytesWritten = 0;
uint32_t runningCRC = 0xFFFFFFFF;
uint16_t filesReceived = 0;

// --- LED Configuration ---
const uint8_t LED_PIN = 2;

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
    if (currentFile && String(currentFile.name()) == String(filename)) {
        currentFile.close();
        currentFile = File();
    }
    
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

void printStorageInfo() {
    size_t totalBytes = RF_TOTAL_BYTES();
    size_t usedBytes = RF_USED_BYTES();
    Serial.println("📊 File System Storage Info:");
    Serial.printf("   Total: %u bytes (%.1f KB)\n", totalBytes, totalBytes/1024.0);
    Serial.printf("   Used:  %u bytes (%.1f KB)\n", usedBytes, usedBytes/1024.0);
    Serial.printf("   Free:  %u bytes (%.1f KB)\n", totalBytes-usedBytes, (totalBytes-usedBytes)/1024.0);
}

void listReceivedFiles() {
    Serial.println("\n📁 Files in file system:");
    File root = RF_FS_OPEN("/", RF_FILE_READ);
    
    if (!root) {
        Serial.println("   ❌ Failed to open root directory");
        return;
    }
    
    int fileCount = 0;
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = file.name();
            // Remove leading slash if present
            if (fileName.startsWith("/")) {
                fileName = fileName.substring(1);
            }
            
            // Categorize files
            if (fileName.endsWith(".json") && fileName.indexOf("_config") >= 0) {
                Serial.printf("   📋 %s (%u bytes) - Model Config\n", fileName.c_str(), file.size());
            } else if (fileName.endsWith("_hogcfg.json")) {
                Serial.printf("   🖼️  %s (%u bytes) - HOG Config\n", fileName.c_str(), file.size());
            } else if (fileName.indexOf("npd") >= 0 && fileName.endsWith(".bin")) {
                Serial.printf("   🧮 %s (%u bytes) - Node Predictor\n", fileName.c_str(), file.size());
            } else if (fileName.indexOf("nlg") >= 0 && fileName.endsWith(".csv")) {
                Serial.printf("   📊 %s (%u bytes) - Training Log\n", fileName.c_str(), file.size());
            } else if (fileName.indexOf("_forest") >= 0 && fileName.endsWith(".bin")) {
                Serial.printf("   🌳 %s (%u bytes) - Unified Forest\n", fileName.c_str(), file.size());
            } else if (fileName.indexOf("_nml") >= 0 && fileName.endsWith(".bin")) {
                Serial.printf("   📊 %s (%u bytes) - Normalized Dataset\n", fileName.c_str(), file.size());
            } else if (fileName.endsWith(".bin")) {
                Serial.printf("   📄 %s (%u bytes)\n", fileName.c_str(), file.size());
            } else if (fileName.endsWith(".csv")) {
                Serial.printf("   📊 %s (%u bytes)\n", fileName.c_str(), file.size());
            } else {
                Serial.printf("   • %s (%u bytes)\n", fileName.c_str(), file.size());
            }
            fileCount++;
        }
        file = root.openNextFile();
    }
    
    if (fileCount == 0) {
        Serial.println("   (No files found)");
    } else {
        Serial.printf("   Total: %d files\n", fileCount);
    }
}

void setup() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    Serial.begin(115200);
    Serial.setTimeout(SERIAL_TIMEOUT_MS);
    
    // Print board info
    print_board_info();
    
    while (!Serial) {
        delay(10);
    }
    
    delay(1000);
    
    Serial.println("\n==================================================");
    Serial.println("🤖 ESP32 Unified File Receiver");
    Serial.println("    Ready to receive all model files");
    Serial.println("==================================================");

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
    Serial.println("   Use: python3 unifier_transfer.py <model_name> <serial_port>");
    
    blinkLed(3, 200);
    setLed(true);
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
            static bool resultsShown = false;
            if (!resultsShown) {
                Serial.println("\n🎉 Unified transfer completed!");
                Serial.printf("📈 Successfully received %d files\n", filesReceived);
                printStorageInfo();
                listReceivedFiles();
                Serial.println("\n✅ ESP32 is ready to use the model!");
                blinkLed(5, 150);
                setLed(false);
                resultsShown = true;
            }
            delay(1000);
            break;
        case State::ERROR_STATE:
            setLed(!digitalRead(LED_PIN));
            delay(200);
            break;
    }
    
    yield();
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

    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_START_SESSION) return;

    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t session_len = Serial.read();
    
    while (Serial.available() < session_len) {
        delay(1);
        yield();
    }
    Serial.readBytes(receivedSessionName, session_len);
    receivedSessionName[session_len] = '\0';

    // Extract model name from session name (remove "_unified" suffix)
    String sessionStr = String(receivedSessionName);
    int unifiedSuffixIdx = sessionStr.lastIndexOf("_unified");
    if (unifiedSuffixIdx > 0) {
        sessionStr = sessionStr.substring(0, unifiedSuffixIdx);
    }
    strncpy(receivedModelName, sessionStr.c_str(), sizeof(receivedModelName) - 1);
    receivedModelName[sizeof(receivedModelName) - 1] = '\0';

    Serial.printf("\n🚀 Starting unified transfer session: %s\n", receivedSessionName);
    Serial.printf("📁 Model name: %s\n", receivedModelName);
    
    // Create model directory
    char modelDir[80];
    snprintf(modelDir, sizeof(modelDir), "/%s", receivedModelName);
    if (!RF_FS_EXISTS(modelDir)) {
        if (RF_FS_MKDIR(modelDir)) {
            Serial.printf("✅ Created model directory: %s\n", modelDir);
        } else {
            Serial.printf("⚠️  Warning: Could not create directory %s\n", modelDir);
        }
    }
    
    filesReceived = 0;
    
    Serial.print(RESP_READY);
    Serial.flush();
    setLed(false);
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
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t filename_len = Serial.read();
    
    while (Serial.available() < filename_len) {
        delay(1);
        yield();
    }
    char baseFileName[64];
    Serial.readBytes(baseFileName, filename_len);
    baseFileName[filename_len] = '\0';

    // Construct full path: /model_name/filename
    snprintf(receivedFileName, sizeof(receivedFileName), "/%s/%s", receivedModelName, baseFileName);

    while (Serial.available() < 12) {
        delay(1);
        yield();
    }
    Serial.readBytes((uint8_t*)&receivedFileSize, sizeof(receivedFileSize));
    Serial.readBytes((uint8_t*)&receivedFileCRC, sizeof(receivedFileCRC));
    Serial.readBytes((uint8_t*)&receivedChunkSize, sizeof(receivedChunkSize));
    
    bytesWritten = 0;
    runningCRC = 0xFFFFFFFF;

    if (receivedChunkSize > (uint32_t)CHUNK_SIZE) {
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }

    // Determine file type
    String fileType = "📄 File";
    if (strstr(baseFileName, ".json") && strstr(baseFileName, "_config")) {
        fileType = "📋 Model Config";
    } else if (strstr(baseFileName, "_hogcfg.json")) {
        fileType = "🖼️  HOG Config";
    } else if (strstr(baseFileName, "npd") && strstr(baseFileName, ".bin")) {
        fileType = "🧮 Node Predictor";
    } else if (strstr(baseFileName, "nlg") && strstr(baseFileName, ".csv")) {
        fileType = "📊 Training Log";
    } else if (strstr(baseFileName, "_forest") && strstr(baseFileName, ".bin")) {
        fileType = "🌳 Unified Forest";
    } else if (strstr(baseFileName, "_nml") && strstr(baseFileName, ".bin")) {
        fileType = "📊 Normalized Dataset";
    } else if (strstr(baseFileName, ".csv")) {
        fileType = "📊 CSV Data";
    } else if (strstr(baseFileName, ".bin")) {
        fileType = "📄 Binary Data";
    }

    if (currentFile) {
        currentFile.close();
        currentFile = File();
    }
    
    if (!safeDeleteFile(receivedFileName)) {
        Serial.printf("❌ Failed to delete existing file: %s\n", receivedFileName);
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }
    
    Serial.printf("📥 Receiving %s: %s (%u bytes)\n", fileType.c_str(), baseFileName, receivedFileSize);
    
    currentFile = RF_FS_OPEN(receivedFileName, RF_FILE_WRITE);
    if (!currentFile) {
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        Serial.flush();
        return;
    }

    setLed(true);
    Serial.print(RESP_ACK);
    Serial.flush();
    currentState = State::RECEIVING_FILE_CHUNK;
}

void handleFileChunk() {
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;
    
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_FILE_CHUNK) {
        currentState = State::WAITING_FOR_COMMAND;
        return;
    }

    uint32_t offset = 0, chunk_len = 0, chunk_crc = 0;
    
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
                return;
            }
            delay(1);
            yield();
        }
    }

    uint32_t calc_crc = compute_crc32(buffer, chunk_len);

    if (calc_crc != chunk_crc) {
        Serial.print("NACK ");
        Serial.println(offset);
        Serial.flush();
        delay(2);
        return;
    }

    size_t written = currentFile.write(buffer, chunk_len);
    if (written != chunk_len) {
        Serial.printf("❌ Write error for %s\n", receivedFileName);
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        Serial.flush();
        return;
    }

    for (uint32_t i = 0; i < chunk_len; ++i) {
        uint8_t b = buffer[i];
        runningCRC ^= b;
        for (int j = 0; j < 8; ++j) {
            uint32_t mask = -(int)(runningCRC & 1);
            runningCRC = (runningCRC >> 1) ^ (0xEDB88320 & mask);
        }
    }
    
    bytesWritten += chunk_len;
    
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
        
        runningCRC ^= 0xFFFFFFFF;
        if (runningCRC == receivedFileCRC && bytesWritten == receivedFileSize) {
            filesReceived++;
            Serial.printf("✅ Saved: %s (%u bytes) - CRC verified\n", receivedFileName, receivedFileSize);
            setLed(false);
            blinkLed(2, 100);
            currentState = State::WAITING_FOR_COMMAND;
        } else {
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
