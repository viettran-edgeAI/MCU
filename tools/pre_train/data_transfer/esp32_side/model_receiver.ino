/*
 * Pre-trained Model Receiver for ESP32
 * 
 * This sketch receives pre-trained random forest model files from a PC
 * and saves them to SPIFFS with their original filenames.
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
#include "SPIFFS.h"

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
const uint32_t CHUNK_DELAY = 50;  // Delay between chunk processing
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
char receivedFileName[64];
uint32_t receivedFileSize = 0;
uint32_t bytesWritten = 0;
uint16_t filesReceived = 0;

// --- LED Configuration ---
const uint8_t LED_PIN = 2;  // Built-in LED on most ESP32 boards

// --- Function Declarations ---
void setLed(bool on);
void blinkLed(int count, int duration);
bool read_header();
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

void printStorageInfo() {
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.println("📊 SPIFFS Storage Info:");
    Serial.printf("   Total: %u bytes (%.1f KB)\n", totalBytes, totalBytes/1024.0);
    Serial.printf("   Used:  %u bytes (%.1f KB)\n", usedBytes, usedBytes/1024.0);
    Serial.printf("   Free:  %u bytes (%.1f KB)\n", totalBytes-usedBytes, (totalBytes-usedBytes)/1024.0);
}

void listReceivedFiles() {
    Serial.println("\n📁 Model files in SPIFFS:");
    File root = SPIFFS.open("/");
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

    // Initialize SPIFFS
    Serial.print("💾 Initializing SPIFFS... ");
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ FAILED!");
        Serial.println("⚠️  SPIFFS initialization failed. Cannot continue.");
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

    Serial.printf("\n🚀 Starting transfer session: %s\n", receivedSessionName);
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

    // Wait for file size (4 bytes)
    while (Serial.available() < sizeof(receivedFileSize)) {
        delay(1);
        yield();
    }
    Serial.readBytes((uint8_t*)&receivedFileSize, sizeof(receivedFileSize));
    bytesWritten = 0;

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
    }
    
    // Create file path with leading slash for SPIFFS - preserve original filename
    String filePath = "/" + String(receivedFileName);
    
    // Delete existing file if it exists to ensure clean overwrite
    if (SPIFFS.exists(filePath)) {
        Serial.printf("🗑️  Deleting existing file: %s\n", receivedFileName);
        if (!SPIFFS.remove(filePath)) {
            Serial.printf("⚠️  Warning: Failed to delete existing file: %s\n", receivedFileName);
        }
    }
    
    Serial.printf("📥 Receiving %s: %s (%u bytes)\n", fileType.c_str(), receivedFileName, receivedFileSize);
    
    currentFile = SPIFFS.open(filePath, FILE_WRITE);
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

    uint8_t buffer[CHUNK_SIZE];
    size_t bytesToRead = min((size_t)CHUNK_SIZE, (size_t)(receivedFileSize - bytesWritten));
    
    // Wait for complete chunk data
    uint32_t start_time = millis();
    while (Serial.available() < bytesToRead) {
        if (millis() - start_time > 5000) {  // 5 second timeout
            Serial.print(RESP_ERROR);
            Serial.flush();
            currentState = State::ERROR_STATE;
            return;
        }
        delay(1);
        yield();
    }
    
    size_t bytesRead = Serial.readBytes(buffer, bytesToRead);

    if (bytesRead > 0) {
        size_t written = currentFile.write(buffer, bytesRead);
        if (written != bytesRead) {
            Serial.printf("❌ Write error for %s\n", receivedFileName);
            currentState = State::ERROR_STATE;
            Serial.print(RESP_ERROR);
            Serial.flush();
            return;
        }
        
        bytesWritten += bytesRead;
        
        // Periodic flush for stability
        if (bytesWritten % (CHUNK_SIZE * 4) == 0) {
            currentFile.flush();
        }
    }
    
    delay(CHUNK_DELAY);
    yield();

    Serial.print(RESP_ACK);
    Serial.flush();

    if (bytesWritten >= receivedFileSize) {
        currentFile.flush();
        currentFile.close();
        filesReceived++;
        Serial.printf("✅ Saved: %s (%u bytes)\n", receivedFileName, receivedFileSize);
        setLed(false);
        blinkLed(2, 100);  // Quick success blink
        currentState = State::WAITING_FOR_COMMAND;
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
