#include "Rf_categorizer.h"
#include "STL_MCU.h"

using namespace mcu;

Rf_categorizer categorizer;

void receiveCSVFile() {
    Serial.println("📡 ESP32 CSV Receiver Ready");
    Serial.println("Waiting for CSV data from PC...");
    Serial.flush();
    
    // Wait for start signal
    while (true) {
        if (Serial.available()) {
            String command = Serial.readStringUntil('\n');
            command.trim();
            
            if (command == "START_CSV") {
                Serial.println("READY");
                Serial.flush();
                break;
            }
        }
        delay(100);
    }
    
    // Get filename
    Serial.println("Waiting for filename...");
    String filename = "";
    unsigned long timeout = millis() + 10000; // 10 second timeout
    
    while (millis() < timeout) {
        if (Serial.available()) {
            filename = Serial.readStringUntil('\n');
            filename.trim();
            if (filename.length() > 0) {
                if (!filename.startsWith("/")) {
                    filename = "/" + filename;
                }
                Serial.println("Filename: " + filename);
                Serial.flush();
                break;
            }
        }
        delay(10);
    }
    
    if (filename.length() == 0) {
        Serial.println("ERROR: No filename received");
        return;
    }
    
    // Receive CSV data
    Serial.println("Ready to receive CSV data...");
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        Serial.println("ERROR: Cannot create file");
        return;
    }
    
    int lineCount = 0;
    timeout = millis() + 60000; // 60 second timeout for data
    
    while (millis() < timeout) {
        if (Serial.available()) {
            String line = Serial.readStringUntil('\n');
            line.trim();
            
            if (line == "EOF_CSV") {
                break; // End of data
            }
            
            if (line.length() > 0) {
                file.println(line);
                lineCount++;
                
                // Send progress feedback every 50 lines
                if (lineCount % 50 == 0) {
                    Serial.println("RECEIVED_" + String(lineCount));
                    Serial.flush();
                }
            }
            
            timeout = millis() + 60000; // Reset timeout on data received
        }
        delay(5);
    }
    
    file.close();
    
    if (millis() >= timeout) {
        Serial.println("ERROR: Timeout receiving data");
        SPIFFS.remove(filename);
        return;
    }
    
    Serial.println("SUCCESS: Received " + String(lineCount) + " lines");
    Serial.println("File saved: " + filename);
    Serial.flush();
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for stable connection
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS initialization failed!");
        return;
    }
    
    Serial.println("✅ SPIFFS initialized");
    
    // Receive CSV file
    receiveCSVFile();
    
    // Try to load the received categorizer
    if (categorizer.loadFromCSV("/categorizer.csv")) {
        Serial.println("✅ Categorizer loaded successfully!");
        categorizer.printInfo();
        
        // Test categorization with a sample
        Serial.println("\n🧪 Testing categorizer:");
        b_vector<float> testSample = MAKE_FLOAT_LIST(2, 
            0.02, 0.02, 0.0276, 0.0201, 0, 0, 0, 0.0107, 0.0291, 0.0228);
        
        if (testSample.size() <= categorizer.getNumFeatures()) {
            auto result = categorizer.categorizeSample(testSample);
            Serial.print("Sample: ");
            for (int i = 0; i < testSample.size(); i++) {
                Serial.print(String(testSample[i], 4) + " ");
            }
            Serial.println();
            Serial.print("Categorized: ");
            for (int i = 0; i < result.size(); i++) {
                Serial.print(String(result[i]) + " ");
            }
            Serial.println();
        }
        
    } else {
        Serial.println("❌ Failed to load categorizer");
    }
}

void loop() {
    // Empty - one-time execution
}
