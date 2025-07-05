/*
 * Simple Dataset Parameters Receiver for ESP32 with Dynamic Filename
 * 
 * This program receives filename first, then CSV data from PC via serial 
 * and saves it to SPIFFS with the received filename.
 * 
 * Protocol:
 * 1. PC sends: "FILENAME:dataset_params.csv"
 * 2. PC sends data lines
 * 3. PC sends: "END"
 * 
 * Usage:
 * 1. Upload this sketch to ESP32
 * 2. Run: python3 simple_transfer.py dataset_params.csv
 * 3. Check LED status for transfer progress
 * 
 * LED Status:
 * - 3 quick blinks: System ready
 * - Solid ON: Receiving data
 * - Solid OFF: File saved successfully
 * - Fast BLINK: Error occurred
 */

#include "Arduino.h"
#include "FS.h"
#include "SPIFFS.h"

// Configuration - optimized for embedded systems
const uint8_t LED_PIN = 2;
const uint32_t SERIAL_BAUDRATE = 115200;
const uint8_t MAX_FILENAME_LENGTH = 64;  // Conservative filename limit
const uint16_t MAX_LINE_LENGTH = 200;    // Max CSV line length
const uint16_t SERIAL_TIMEOUT_MS = 30000; // 30 second timeout

// State management - memory efficient
enum ReceiveState {
    WAITING_FILENAME,
    RECEIVING_DATA,
    COMPLETED,
    ERROR_STATE
};

// Global variables - minimal memory footprint
ReceiveState currentState = WAITING_FILENAME;
char receivedFilename[MAX_FILENAME_LENGTH];
File outputFile;
uint16_t linesReceived = 0;

void setup() {
    // Initialize hardware
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    
    Serial.begin(SERIAL_BAUDRATE);
    while (!Serial) {
        delay(10);
    }
    
    // Initialize SPIFFS with error checking
    if (!SPIFFS.begin(true)) {
        currentState = ERROR_STATE;
        return;
    }
    
    // System ready indication (3 quick blinks)
    for (uint8_t i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }
    
    // Clear filename buffer
    memset(receivedFilename, 0, MAX_FILENAME_LENGTH);
    
    Serial.setTimeout(SERIAL_TIMEOUT_MS);
}

void loop() {
    switch (currentState) {
        case WAITING_FILENAME:
            handleFilename();
            break;
            
        case RECEIVING_DATA:
            handleDataReception();
            break;
            
        case COMPLETED:
            // Success - LED OFF, file saved
            digitalWrite(LED_PIN, LOW);
            delay(1000);
            break;
            
        case ERROR_STATE:
            // Error indication - fast blink
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            delay(100);
            break;
    }
}

void handleFilename() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        
        if (line.startsWith("FILENAME:")) {
            // Extract filename from "FILENAME:dataset_params.csv"
            String filename = line.substring(9); // Skip "FILENAME:"
            filename.trim();
            
            // Validate filename length
            if (filename.length() > 0 && filename.length() < MAX_FILENAME_LENGTH - 1) {
                // Copy to buffer (add "/" prefix for SPIFFS)
                snprintf(receivedFilename, MAX_FILENAME_LENGTH, "/%s", filename.c_str());
                
                // Open file for writing
                outputFile = SPIFFS.open(receivedFilename, "w");
                if (outputFile) {
                    currentState = RECEIVING_DATA;
                    digitalWrite(LED_PIN, HIGH); // LED ON = receiving data
                } else {
                    currentState = ERROR_STATE;
                }
            } else {
                currentState = ERROR_STATE;
            }
        }
    }
}

void handleDataReception() {
    if (Serial.available()) {
        String line = Serial.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0) {
            // Check for end marker
            if (line == "END") {
                // Close file and complete
                if (outputFile) {
                    outputFile.close();
                }
                currentState = COMPLETED;
                return;
            }
            
            // Validate line length for memory safety
            if (line.length() > MAX_LINE_LENGTH) {
                closeFileAndError();
                return;
            }
            
            // Write line to file
            if (outputFile) {
                outputFile.println(line);
                outputFile.flush(); // Ensure immediate write
                linesReceived++;
            } else {
                currentState = ERROR_STATE;
            }
        }
    }
}

void closeFileAndError() {
    if (outputFile) {
        outputFile.close();
    }
    currentState = ERROR_STATE;
}

