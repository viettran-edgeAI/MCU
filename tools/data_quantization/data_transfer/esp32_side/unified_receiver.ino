/*
 * Unified Data Receiver for ESP32
 * 
 * This sketch receives a complete dataset (quantizer, parameters, and binary data)
 * from a PC in a single, coordinated session and saves the files to file system.
 * Files are saved to /model_name/file_path structure.
 * 
 * It is designed to work with the 'unified_transfer.py' script.
 * 
 * PERFORMANCE NOTES:
 * - Transfer speed depends on CHUNK_SIZE; larger chunks are faster but may cause
 *   USB CDC buffer overruns on boards like ESP32-C3.
 * - This sketch auto-detects the board and sets CHUNK_SIZE conservatively for C3,
 *   but allows user override via USER_CHUNK_SIZE define.
 * - See board_config.h for board-specific recommendations.
 */

// CRITICAL: Disable RF_DEBUG_LEVEL BEFORE including Rf_file_manager.h
// Debug messages to Serial will corrupt the binary protocol communication
#define RF_DEBUG_LEVEL 0

#include "Rf_file_manager.h"  // Includes Rf_board_config.h internally

// --- Storage Configuration ---
// Uses LittleFS for ESP32 boards; file creation is handled by Rf_file_manager
const RfStorageType STORAGE_MODE = RfStorageType::FLASH;

// --- Protocol Constants ---
// Must match the Python sender script (unified_transfer.py)
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

const uint32_t CHUNK_DELAY = 20;        // ms delay between chunks (ACK-driven, can be small)
const uint32_t SERIAL_TIMEOUT_MS = 30000; // Extended timeout for large files
const uint32_t HEADER_WAIT_MS = 100;    // Wait time for header assembly

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
char receivedBaseName[32];  // This is the model_name used throughout the project
char receivedFileName[64];
uint32_t receivedFileSize = 0;
uint32_t receivedFileCRC = 0;
uint32_t receivedChunkSize = 0;
uint32_t bytesWritten = 0;
uint32_t runningCRC = 0xFFFFFFFF;

// --- LED Configuration ---
const uint8_t LED_PIN = 2; // Built-in LED on many ESP32 boards

// --- Function Declarations ---
void setLed(bool on);
void blinkLed(int count, int duration);
bool read_header();
uint32_t compute_crc32(const uint8_t* data, size_t len);
bool safeDeleteFile(const char* filename);
void deleteOldDatasetFiles(const char* basename);
void handleStartSession();
void handleCommand();
void handleFileInfo();
void handleFileChunk();
void handleEndSession();

void setLed(bool on) { 
    digitalWrite(LED_PIN, on ? HIGH : LOW);
}

void blinkLed(int count, int duration) {
    for (int i = 0; i < count; i++) {
        setLed(true); delay(duration);
        setLed(false); delay(duration);
    }
}

uint32_t compute_crc32(const uint8_t* data, size_t len) {
    /*
     * CRC32 computation for chunk validation.
     * Both ESP32 and PC use this same polynomial (0xEDB88320) to ensure
     * consistency. Each chunk header includes its CRC; mismatch triggers NACK.
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
     * Safely delete a file by first closing any open handle, then removing.
     * Retries multiple times to handle file system delays.
     * Returns true on successful deletion, false otherwise.
     */
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

void deleteOldDatasetFiles(const char* basename) {
    /*
     * Clean up old dataset files for a given model name before receiving new data.
     * This prevents partial file uploads or mismatched versions from persisting.
     * basename is the model name; files are stored in /<basename>/ directory.
     */
    char filepath[128];
    
    // Delete quantizer file (categories/histogram info)
    snprintf(filepath, sizeof(filepath), "/%s/%s_ctg.csv", basename, basename);
    safeDeleteFile(filepath);
    
    // Delete parameters file (model hyperparameters, feature scaling)
    snprintf(filepath, sizeof(filepath), "/%s/%s_dp.csv", basename, basename);
    safeDeleteFile(filepath);
    
    // Delete binary dataset file (normalized/quantized training data)
    snprintf(filepath, sizeof(filepath), "/%s/%s_nml.bin", basename, basename);
    safeDeleteFile(filepath);
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

    // Print board info at startup for user reference
    print_board_info();

    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        currentState = State::ERROR_STATE;
        return;
    }

    blinkLed(3, 100); // Signal ready
}

void loop() {
    /*
     * Main state machine loop.
     * Orchestrates session start, file metadata exchange, chunk reception,
     * CRC validation, and session end.
     */
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
            // Do nothing, wait for reset
            break;
        case State::ERROR_STATE:
            // Fast blink to indicate error
            setLed(!digitalRead(LED_PIN));
            delay(100);
            break;
        // Other states are handled within the command loop
    }
    
    // Small yield to prevent blocking
    yield();
}

// --- Protocol Handlers ---

bool read_header() {
    // Wait for complete header with timeout
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

    if (!read_header()) return; // Wait for a valid header

    // Wait for command byte
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_START_SESSION) return;

    // Wait for basename length (basename IS the model name)
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t basename_len = Serial.read();
    
    // Wait for complete basename
    while (Serial.available() < basename_len) {
        delay(1);
        yield();
    }
    Serial.readBytes(receivedBaseName, basename_len);
    receivedBaseName[basename_len] = '\0';

    // Create model directory if it doesn't exist (basename is the model name)
    char modelDir[80];
    snprintf(modelDir, sizeof(modelDir), "/%s", receivedBaseName);
    if (!RF_FS_EXISTS(modelDir)) {
        RF_FS_MKDIR(modelDir);
    }

    // Delete any existing files with this basename before starting new transfer
    deleteOldDatasetFiles(receivedBaseName);

    Serial.print(RESP_READY);
    Serial.flush();
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

    // Construct full path with model name (basename is the model name)
    snprintf(receivedFileName, sizeof(receivedFileName), "/%s%s", receivedBaseName, baseFileName);

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

    // Close any currently open file
    if (currentFile) {
        currentFile.close();
        currentFile = File(); // Reset file handle
    }
    
    // Use safe delete function with multiple retry attempts
    if (!safeDeleteFile(receivedFileName)) {
        // Failed to delete existing file after multiple attempts
        Serial.print(RESP_ERROR);
        Serial.flush();
        currentState = State::ERROR_STATE;
        return;
    }
    
    // Create new file
    currentFile = RF_FS_OPEN(receivedFileName, RF_FILE_WRITE); // Force create
    if (!currentFile) {
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        Serial.flush();
        return;
    }

    setLed(true); // Turn LED on during transfer
    Serial.print(RESP_ACK);
    Serial.flush();
    currentState = State::RECEIVING_FILE_CHUNK;
}

void handleFileChunk() {
    // This state is entered after a FILE_INFO command is successfully processed.
    // We now expect a stream of CHUNK commands with V2 protocol (offset, len, CRC).
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;
    
    // Wait for command byte
    while (!Serial.available()) {
        delay(1);
        yield();
    }
    uint8_t command = Serial.read();
    if (command != CMD_FILE_CHUNK) {
        // If we get something other than a chunk, maybe it's a new command
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

    // Read chunk payload (handle small CDC buffers by streaming byte-by-byte)
    uint8_t buffer[CHUNK_SIZE];
    size_t bytesRead = 0;
    start_time = millis();
    while (bytesRead < chunk_len) {
        if (Serial.available()) {
            int byteVal = Serial.read();
            if (byteVal < 0) {
                continue; // read error, try again
            }
            buffer[bytesRead++] = static_cast<uint8_t>(byteVal);
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
        Serial.print(offset);
        Serial.print(" crc_calc=0x");
        Serial.print(calc_crc, HEX);
        Serial.print(" expected=0x");
        Serial.println(chunk_crc, HEX);
        Serial.flush();
        delay(2);
        return; // CRC mismatch, sender will retry
    }

    // CRC matched; write to file
    size_t written = currentFile.write(buffer, chunk_len);
    if (written != chunk_len) {
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
    
    // Flush file buffer periodically
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
            setLed(false);
            blinkLed(2, 50); // Quick blink for success
            currentState = State::WAITING_FOR_COMMAND;
        } else {
            // CRC mismatch, remove file
            RF_FS_REMOVE(receivedFileName);
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
    blinkLed(5, 100); // Final success signal
    Serial.print(RESP_OK);
    Serial.flush();
    currentState = State::SESSION_ENDED;
}

