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

const uint16_t CHUNK_SIZE = 512; // Must match python script. Reduced for C3 stability.
const uint32_t SERIAL_TIMEOUT_MS = 10000; // 10-second timeout for reads

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

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        currentState = State::ERROR_STATE;
        return;
    }

    Serial.println("\nESP32 Unified Receiver Ready.");
    Serial.println("Waiting for transfer session to start...");
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
}

// --- Protocol Handlers ---

bool read_header() {
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

    uint8_t command = Serial.read();
    if (command != CMD_START_SESSION) return;

    uint8_t basename_len = Serial.read();
    Serial.readBytes(receivedBaseName, basename_len);
    receivedBaseName[basename_len] = '\0';

    Serial.printf("Received START_SESSION for base: %s\n", receivedBaseName);
    Serial.print(RESP_READY);
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
            Serial.printf("Unknown command: 0x%02X\n", command);
            break;
    }
}

void handleFileInfo() {
    uint8_t filename_len = Serial.read();
    Serial.readBytes(receivedFileName, filename_len);
    receivedFileName[filename_len] = '\0';

    Serial.readBytes((uint8_t*)&receivedFileSize, sizeof(receivedFileSize));
    bytesWritten = 0;

    Serial.printf("Receiving file: %s (%u bytes)\n", receivedFileName, receivedFileSize);

    if (currentFile) {
        currentFile.close();
    }
    currentFile = SPIFFS.open(receivedFileName, FILE_WRITE);
    if (!currentFile) {
        Serial.println("Failed to open file for writing");
        currentState = State::ERROR_STATE;
        Serial.print(RESP_ERROR);
        return;
    }

    setLed(true); // Turn LED on during transfer
    Serial.print(RESP_ACK);
    currentState = State::RECEIVING_FILE_CHUNK;
}

void handleFileChunk() {
    // This state is entered after a FILE_INFO command is successfully processed.
    // We now expect a stream of CHUNK commands.
    if (Serial.available() < sizeof(CMD_HEADER)) return;

    if (!read_header()) return;
    
    uint8_t command = Serial.read();
    if (command != CMD_FILE_CHUNK) {
        // If we get something other than a chunk, maybe it's a new command
        currentState = State::WAITING_FOR_COMMAND;
        // We need to put the command back in the stream or handle it here.
        // For simplicity, we'll just go back to waiting for a command.
        // This assumes the sender won't send malformed sequences.
        return;
    }

    uint8_t buffer[CHUNK_SIZE];
    size_t bytesToRead = min((size_t)CHUNK_SIZE, (size_t)(receivedFileSize - bytesWritten));
    size_t bytesRead = Serial.readBytes(buffer, bytesToRead);

    if (bytesRead > 0) {
        currentFile.write(buffer, bytesRead);
        bytesWritten += bytesRead;
        // Serial.printf("  ...wrote %u bytes (%u / %u)\n", bytesRead, bytesWritten, receivedFileSize);
    }
    delay(10); // Yield to other tasks, important for single-core chips

    Serial.print(RESP_ACK);

    if (bytesWritten >= receivedFileSize) {
        Serial.println("File transfer complete.");
        currentFile.close();
        setLed(false);
        blinkLed(2, 50); // Quick blink for success
        currentState = State::WAITING_FOR_COMMAND;
    }
}

void handleEndSession() {
    Serial.println("Received END_SESSION. All transfers complete.");
    if (currentFile) {
        currentFile.close();
    }
    setLed(false);
    blinkLed(5, 100); // Final success signal
    Serial.print(RESP_OK);
    currentState = State::SESSION_ENDED;
    listSPIFFSFiles();
}

void listSPIFFSFiles() {
    Serial.println("\n--- SPIFFS Contents ---");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file){
        Serial.printf("  - %s (%u bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }
    Serial.println("-----------------------");
}
