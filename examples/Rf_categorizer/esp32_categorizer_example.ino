/*
 * ESP32 RfCategorizer Example
 * 
 * This example demonstrates the complete workflow:
 * 1. Receive categorizer data from Serial
 * 2. Convert to binary format
 * 3. Load/release from memory
 * 4. Categorize new samples
 */

#include "Rf_categorizer.h"

Rf_categorizer categorizer;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed!");
        return;
    }
    
    Serial.println("ESP32 RfCategorizer Ready");
    Serial.println("Commands:");
    Serial.println("1. 'receive' - Receive categorizer from Serial (EOF method)");
    Serial.println("2. 'input' - Interactive categorizer data input (CSV method)");
    Serial.println("3. 'load' - Load categorizer into RAM");
    Serial.println("4. 'release' - Release categorizer from RAM");
    Serial.println("5. 'test' - Test categorization with sample data");
    Serial.println("6. 'info' - Show categorizer information");
    Serial.println("7. 'categorize x1,x2,x3...' - Categorize custom sample");
}

void loop() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toLowerCase();
        
        if (command == "receive") {
            handleReceiveCommand();
        }
        else if (command == "input") {
            handleInputCommand();
        }
        else if (command == "load") {
            handleLoadCommand();
        }
        else if (command == "release") {
            handleReleaseCommand();
        }
        else if (command == "test") {
            handleTestCommand();
        }
        else if (command == "info") {
            handleInfoCommand();
        }
        else if (command.startsWith("categorize ")) {
            handleCategorizeCommand(command.substring(11));
        }
        else {
            Serial.println("Unknown command: " + command);
        }
    }
    
    delay(100);
}

void handleReceiveCommand() {
    Serial.println("Ready to receive categorizer data...");
    Serial.println("Send CSV data followed by 'EOF_CATEGORIZER'");
    
    if (categorizer.receiveFromPySerial(Serial, 60000)) { // 60 second timeout
        Serial.println("Categorizer received and converted successfully!");
        categorizer = Rf_categorizer("/categorizer.bin");
    } else {
        Serial.println("Failed to receive categorizer data");
    }
}

void handleInputCommand() {
    Serial.println("Starting interactive categorizer data input...");
    
    if (categorizer.receiveFromSerialMonitor(234, false)) { // 234 columns, don't print file
        Serial.println("Categorizer data input completed successfully!");
    } else {
        Serial.println("Failed to input categorizer data");
    }
}

void handleLoadCommand() {
    if (categorizer.loadCtg()) {
        Serial.println("Categorizer loaded successfully");
        Serial.println("Memory usage: " + String(categorizer.getMemoryUsage()) + " bytes");
    } else {
        Serial.println("Failed to load categorizer");
    }
}

void handleReleaseCommand() {
    categorizer.releaseCtg();
}

void handleTestCommand() {
    if (!categorizer.getIsLoaded()) {
        Serial.println("Categorizer not loaded. Use 'load' command first.");
        return;
    }
    
    // Example test sample (adjust based on your dataset)
    mcu::b_vector<float> testSample = {
        17.99f, 10.38f, 122.8f, 1001.0f, 0.1184f,
        0.2776f, 0.3001f, 0.1471f, 0.2419f, 0.07871f
    };
    
    // Resize to match number of features
    while (testSample.size() < categorizer.getNumFeatures()) {
        testSample.push_back(0.0f);
    }
    
    auto result = categorizer.categorizeSample(testSample);
    
    Serial.print("Test sample categorization: ");
    for (uint16_t i = 0; i < result.size(); i++) {
        Serial.print(String(result[i]));
        if (i < result.size() - 1) Serial.print(",");
    }
    Serial.println();
}

void handleInfoCommand() {
    categorizer.printInfo();
    
    // Additional system info
    Serial.println("=== System Info ===");
    Serial.println("Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("SPIFFS used: " + String(SPIFFS.usedBytes()) + " bytes");
    Serial.println("SPIFFS total: " + String(SPIFFS.totalBytes()) + " bytes");
    Serial.println("==================");
}

void handleCategorizeCommand(String sampleData) {
    if (!categorizer.getIsLoaded()) {
        Serial.println("Categorizer not loaded. Use 'load' command first.");
        return;
    }
    
    // Parse comma-separated values
    mcu::b_vector<float> sample;
    int start = 0;
    int end = sampleData.indexOf(',');
    
    while (end != -1) {
        sample.push_back(sampleData.substring(start, end).toFloat());
        start = end + 1;
        end = sampleData.indexOf(',', start);
    }
    sample.push_back(sampleData.substring(start).toFloat());
    
    if (sample.size() != categorizer.getNumFeatures()) {
        Serial.println("Sample size (" + String(sample.size()) + 
                      ") doesn't match expected features (" + 
                      String(categorizer.getNumFeatures()) + ")");
        return;
    }
    
    auto result = categorizer.categorizeSample(sample);
    
    Serial.print("Categorized sample: ");
    for (uint16_t i = 0; i < result.size(); i++) {
        Serial.print(String(result[i]));
        if (i < result.size() - 1) Serial.print(",");
    }
    Serial.println();
}

// Helper function to send categorizer data over Serial
// (This would be used on the PC side)
void sendCategorizerOverSerial() {
    // This is example code that would run on PC side
    // to send the CSV data to ESP32
    /*
    // Read categorizer_esp32.csv
    // Send each line followed by newline
    // End with "EOF_CATEGORIZER"
    
    Example transmission:
    2,4
    1,3,0.5,1.0,1.5
    0,3,10.2,15.7,20.1
    EOF_CATEGORIZER
    */
}
