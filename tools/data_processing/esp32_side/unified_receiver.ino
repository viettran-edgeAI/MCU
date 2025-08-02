/*
 * Unified Data Receiver for ESP32
 * 
 * This sketch receives a complete dataset (categorizer, parameters, and binary data)
 * from a PC in a single, coordinated session and saves the files to SPIFFS.
 * 
 * It is designed to work with the 'unified_transfer.py' script.
 */

#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"
#include "Rf_file_manager.h"

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

const uint16_t CHUNK_SIZE = 256; // Further reduced for USB CDC compatibility
const uint32_t CHUNK_DELAY = 50; // Increased delay for USB CDC stability
const uint32_t SERIAL_TIMEOUT_MS = 30000; // Extended timeout for large files
const uint32_t HEADER_WAIT_MS = 100; // Wait time for header assembly

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
char receivedBaseName[64];
char receivedFileName[64];
uint32_t receivedFileSize = 0;
uint32_t bytesWritten = 0;

// --- LED Configuration ---
const uint8_t LED_PIN = 2; // Built-in LED on many ESP32 boards

// --- Function Declarations ---
void setLed(bool on);
void blinkLed(int count, int duration);
bool read_header();
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

void setup() {
    pinMode(LED_PIN, OUTPUT);
    setLed(false);

    Serial.begin(115200);
    Serial.setTimeout(SERIAL_TIMEOUT_MS);

    // manageSPIFFSFiles();

    if (!SPIFFS.begin(true)) {
        currentState = State::ERROR_STATE;
        return;
    }

    blinkLed(3, 100); // Signal ready
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

    // Wait for basename length
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
    Serial.readBytes(receivedFileName, filename_len);
    receivedFileName[filename_len] = '\0';

    // Wait for file size (4 bytes)
    while (Serial.available() < sizeof(receivedFileSize)) {
        delay(1);
        yield();
    }
    Serial.readBytes((uint8_t*)&receivedFileSize, sizeof(receivedFileSize));
    bytesWritten = 0;

    if (currentFile) {
        currentFile.close();
    }
    currentFile = SPIFFS.open(receivedFileName, FILE_WRITE);
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
    // We now expect a stream of CHUNK commands.
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

    uint8_t buffer[CHUNK_SIZE];
    size_t bytesToRead = min((size_t)CHUNK_SIZE, (size_t)(receivedFileSize - bytesWritten));
    
    // Wait for complete chunk data
    uint32_t start_time = millis();
    while (Serial.available() < bytesToRead) {
        if (millis() - start_time > 5000) { // 5 second timeout for chunk data
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
            currentState = State::ERROR_STATE;
            Serial.print(RESP_ERROR);
            Serial.flush();
            return;
        }
        
        bytesWritten += bytesRead;
        
        // Flush file buffer more frequently for USB CDC
        if (bytesWritten % (CHUNK_SIZE * 2) == 0) {
            currentFile.flush();
        }
    }
    
    // Longer delay for stability with USB CDC
    delay(CHUNK_DELAY);
    yield(); // Allow other tasks to run

    Serial.print(RESP_ACK);
    Serial.flush(); // Ensure ACK is sent immediately

    if (bytesWritten >= receivedFileSize) {
        currentFile.flush(); // Final flush
        currentFile.close();
        setLed(false);
        blinkLed(2, 50); // Quick blink for success
        currentState = State::WAITING_FOR_COMMAND;
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

