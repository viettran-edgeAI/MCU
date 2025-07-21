#include "Rf_categorizer.h"

// Default constructor
Rf_categorizer::Rf_categorizer() : numFeatures(0), groupsPerFeature(0), isLoaded(false) {}

// Constructor with filename
Rf_categorizer::Rf_categorizer(const String& binFilename) : filename(binFilename), isLoaded(false) {
    // Try to load basic info from binary file
    if (SPIFFS.exists(filename)) {
        File file = SPIFFS.open(filename, "r");
        if (file) {
            file.read((uint8_t*)&numFeatures, sizeof(numFeatures));
            file.read((uint8_t*)&groupsPerFeature, sizeof(groupsPerFeature));
            file.close();
        }
    }
}


// Helper function to split string
mcu::b_vector<String> Rf_categorizer::split(const String& line, char delimiter) {
    mcu::b_vector<String> result;
    int start = 0;
    int end = line.indexOf(delimiter);
    
    while (end != -1) {
        result.push_back(line.substring(start, end));
        start = end + 1;
        end = line.indexOf(delimiter, start);
    }
    result.push_back(line.substring(start));
    
    return result;
}

// Update the implementation to use Stream& instead of HardwareSerial&
bool Rf_categorizer::receiveFromPySerial(Stream& serial, unsigned long timeout) {
    unsigned long startTime = millis();
    
    // Clear any leftover data in serial buffer
    while (serial.available()) {
        serial.read();
    }
    delay(100);
    
    // Phase 1: Wait for "receive" command from Python script
    Serial.println("Waiting for 'receive' command from Python script...");
    Serial.flush();
    
    String commandBuffer = "";
    commandBuffer.reserve(32);
    
    while (millis() - startTime < timeout) {
        if (serial.available()) {
            char c = serial.read();
            
            if (c == '\n' || c == '\r') {
                commandBuffer.trim();
                commandBuffer.toLowerCase();
                
                if (commandBuffer.equals("receive")) {
                    Serial.println("READY"); // Match what Python expects
                    Serial.flush();
                    break;
                } else if (commandBuffer.length() > 0) {
                    Serial.println("Received: '" + commandBuffer + "' - expecting 'receive'");
                }
                commandBuffer = ""; // Reset for next command
            } else if (c >= 32 && c <= 126 && commandBuffer.length() < 30) { // Printable ASCII only
                commandBuffer += c;
            }
        }
        delay(10);
    }
    
    if (millis() - startTime >= timeout) {
        Serial.println("Timeout waiting for receive command");
        return false;
    }
    
    // Phase 2: Receive desired filename
    Serial.println("Waiting for filename...");
    Serial.flush();
    
    String receivedFilename = "";
    startTime = millis(); // Reset timeout for filename reception
    
    while (millis() - startTime < timeout) {
        if (serial.available()) {
            char c = serial.read();
            
            if (c == '\n' || c == '\r') {
                receivedFilename.trim();
                if (receivedFilename.length() > 0) {
                    // Ensure filename starts with '/' for SPIFFS
                    if (!receivedFilename.startsWith("/")) {
                        receivedFilename = "/" + receivedFilename;
                    }
                    filename = receivedFilename;
                    Serial.println("Received filename: " + filename);
                    break;
                }
            } else if (c >= 32 && c <= 126 && receivedFilename.length() < 60) { // Printable ASCII only
                receivedFilename += c;
            }
        }
        delay(10);
    }
    
    if (receivedFilename.length() == 0) {
        // Fallback to auto-generated filename if none received
        static uint16_t fileCounter = 0;
        char filenameBuffer[32];
        snprintf(filenameBuffer, sizeof(filenameBuffer), "/categorizer_%u.bin", fileCounter++);
        filename = filenameBuffer;
        Serial.println("No filename received, using: " + filename);
    }
    
    // Phase 3: Receive CSV data
    Serial.println("Waiting for categorizer data...");
    Serial.flush();
    
    mcu::b_vector<char> csvBuffer;
    csvBuffer.reserve(4096); // Larger buffer for bigger datasets
    const size_t MAX_CSV_SIZE = 8192; // Buffer overflow protection for ESP32
    startTime = millis(); // Reset timeout for data reception
    
    bool foundEOF = false;
    const char* eofMarker = "EOF_CATEGORIZER";
    const size_t eofLen = 15; // strlen("EOF_CATEGORIZER")
    size_t eofMatchPos = 0;
    
    while (millis() - startTime < timeout && !foundEOF) {
        if (serial.available()) {
            char c = serial.read();
            
            // Check buffer size limit
            if (csvBuffer.size() >= MAX_CSV_SIZE) {
                Serial.println("ERROR: CSV data too large, buffer overflow protection");
                return false;
            }
            
            csvBuffer.push_back(c);
            
            // Efficient EOF detection - character by character matching
            if (c == eofMarker[eofMatchPos]) {
                eofMatchPos++;
                if (eofMatchPos >= eofLen) {
                    // Remove EOF marker from buffer
                    csvBuffer.resize(csvBuffer.size() - eofLen);
                    foundEOF = true;
                    break;
                }
            } else {
                eofMatchPos = (c == eofMarker[0]) ? 1 : 0;
            }
            
            startTime = millis(); // Reset timeout on data reception
        }
        delay(1);
    }
    
    if (!foundEOF) {
        Serial.println("ERROR: Timeout or no EOF marker found");
        return false;
    }
    
    if (csvBuffer.empty()) {
        Serial.println("ERROR: No CSV data received");
        return false;
    }
    
    Serial.println("SUCCESS: CSV data received (" + String(csvBuffer.size()) + " bytes)");
    
    Serial.println("Binary file will be saved as: " + filename);
    
    // Phase 4: Save CSV to temporary file using memory-efficient approach
    String csvPath = "/temp_csv.csv";
    File csvFile = SPIFFS.open(csvPath, "w");
    if (!csvFile) {
        Serial.println("Failed to create temporary CSV file");
        return false;
    }
    
    // Write buffer to file in chunks to save RAM
    const uint16_t chunkSize = 128; // Smaller chunks for embedded systems
    for (uint16_t i = 0; i < csvBuffer.size(); i += chunkSize) {
        uint16_t writeSize = (i + chunkSize > csvBuffer.size()) ? 
                            (csvBuffer.size() - i) : chunkSize;
        
        // Check write success
        size_t written = csvFile.write(reinterpret_cast<const uint8_t*>(&csvBuffer[i]), writeSize);
        if (written != writeSize) {
            Serial.println("ERROR: Failed to write chunk to file");
            csvFile.close();
            return false;
        }
    }
    csvFile.close();
    
    // Clear buffer to free memory
    csvBuffer.clear();
    csvBuffer.fit();
    
    // Phase 5: Convert to binary format
    bool success = convertToBin(csvPath);
    
    // Clean up temporary CSV file
    SPIFFS.remove(csvPath);
    
    if (success) {
        Serial.println("SUCCESS: Categorizer converted successfully");
    } else {
        Serial.println("ERROR: Failed to convert CSV to binary format");
    }
    
    return success;
}

// Receive categorizer data using interactive CSV input
bool Rf_categorizer::receiveFromSerialMonitor(bool print_file) {
    Serial.println("=== Categorizer Data Input ===");
    Serial.println("Please enter categorizer data in CSV format.");
    
    // Use our custom CSV input method instead of reception_data
    String csvPath = reception_data(0,print_file);
    
    // Check if CSV input was successful
    if (csvPath.length() == 0) {
        Serial.println("❌ Failed to receive CSV data");
        return false;
    }
    
    // Extract base name from the full path for binary filename
    String baseName = csvPath;
    int lastSlash = baseName.lastIndexOf('/');
    if (lastSlash >= 0) {
        baseName = baseName.substring(lastSlash + 1);
    }
    int lastDot = baseName.lastIndexOf('.');
    if (lastDot >= 0) {
        baseName = baseName.substring(0, lastDot);
    }

    filename = "/" + baseName + "_ctg.bin";
    Serial.println("📁 Binary file will be saved as: " + filename);
    
    return convertToBin(csvPath);
}

// Convert CSV format to binary format
bool Rf_categorizer::convertToBin(const String& csvFile) {
    
    if (!SPIFFS.exists(csvFile)) {
        Serial.println("CSV file not found");
        return false;
    }
    
    File csvIn = SPIFFS.open(csvFile, "r");
    if (!csvIn) {
        Serial.println("Failed to open CSV file");
        return false;
    }
    
    // Read header line
    String header = csvIn.readStringUntil('\n');
    header.trim();
    mcu::b_vector<String> headerParts = split(header, ',');  // Add comma delimiter
    
    if (headerParts.size() != 2) {
        csvIn.close();
        Serial.println("Invalid CSV header format. Expected: numFeatures,groupsPerFeature");
        Serial.println("Got: " + header);
        return false;
    }
    
    numFeatures = headerParts[0].toInt();
    groupsPerFeature = headerParts[1].toInt();
    
    Serial.println("Processing " + String(numFeatures) + " features with " + String(groupsPerFeature) + " groups each");
    
    // Create binary file
    File binOut = SPIFFS.open(filename, "w");
    if (!binOut) {
        csvIn.close();
        Serial.println("Failed to create binary file");
        return false;
    }
    
    // Write header to binary
    binOut.write((uint8_t*)&numFeatures, sizeof(numFeatures));
    binOut.write((uint8_t*)&groupsPerFeature, sizeof(groupsPerFeature));
    
    // Process each feature
    for (uint16_t i = 0; i < numFeatures; i++) {
        if (!csvIn.available()) {
            csvIn.close();
            binOut.close();
            Serial.println("Unexpected end of file at feature " + String(i));
            SPIFFS.remove(csvFile);
            return false;
        }
        
        String line = csvIn.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) {
            csvIn.close();
            binOut.close();
            Serial.println("Empty line encountered at feature " + String(i));
            SPIFFS.remove(csvFile);
            return false;
        }
        
        mcu::b_vector<String> parts = split(line, ',');  // Add comma delimiter
        
        // Expected format: isDiscreteFlag,dataCount,value1,value2,value3,...
        if (parts.size() < 2) {
            csvIn.close();
            binOut.close();
            Serial.println("Invalid feature line format at feature " + String(i));
            Serial.println("Expected at least 2 values, got " + String(parts.size()));
            Serial.println("Line: " + line);
            SPIFFS.remove(csvFile);
            return false;
        }
        
        uint8_t isDiscreteFlag = parts[0].toInt();
        uint16_t dataCount = parts[1].toInt();
        
        // Validate that we have enough data values
        if (parts.size() < (2 + dataCount)) {
            csvIn.close();
            binOut.close();
            Serial.println("Insufficient data values at feature " + String(i));
            Serial.println("Expected " + String(dataCount) + " values, got " + String(parts.size() - 2));
            SPIFFS.remove(csvFile);
            return false;
        }
        
        // Write feature metadata
        binOut.write(&isDiscreteFlag, sizeof(isDiscreteFlag));
        binOut.write((uint8_t*)&dataCount, sizeof(dataCount));
        
        // Write feature data
        for (uint16_t j = 0; j < dataCount; j++) {
            float value = parts[j + 2].toFloat();
            binOut.write((uint8_t*)&value, sizeof(value));
        }
        
        // Debug output for first few features
        if (i < 3) {
            Serial.println("Feature " + String(i) + ": discrete=" + String(isDiscreteFlag) + 
                          ", count=" + String(dataCount) + ", first_value=" + parts[2]);
        }
    }
    
    csvIn.close();
    binOut.close();
    
    SPIFFS.remove(csvFile);
    
    Serial.println("Conversion to binary completed successfully");
    return true;
}

// Load categorizer from SPIFFS to RAM
bool Rf_categorizer::loadCtg() {
    if (isLoaded) {
        Serial.println("Categorizer already loaded");
        return true;
    }
    
    if (!SPIFFS.exists(filename)) {
        Serial.println("Binary file not found: " + filename);
        return false;
    }
    
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        Serial.println("Failed to open binary file");
        return false;
    }
    
    // Read header
    file.read((uint8_t*)&numFeatures, sizeof(numFeatures));
    file.read((uint8_t*)&groupsPerFeature, sizeof(groupsPerFeature));
    
    // Initialize vectors
    isDiscrete.clear();
    discreteValues.clear();
    quantileBinEdges.clear();
    
    isDiscrete.reserve(numFeatures);
    discreteValues.reserve(numFeatures);
    quantileBinEdges.reserve(numFeatures);
    
    // Read feature data
    for (uint16_t i = 0; i < numFeatures; i++) {
        uint8_t isDiscreteFlag;
        uint16_t dataCount;
        
        file.read(&isDiscreteFlag, sizeof(isDiscreteFlag));
        file.read((uint8_t*)&dataCount, sizeof(dataCount));
        
        isDiscrete.push_back(isDiscreteFlag == 1);
        
        mcu::b_vector<float> data;
        data.reserve(dataCount);
        
        for (uint16_t j = 0; j < dataCount; j++) {
            float value;
            file.read((uint8_t*)&value, sizeof(value));
            data.push_back(value);
        }
        
        if (isDiscrete[i]) {
            discreteValues.push_back(data);
            quantileBinEdges.push_back(mcu::b_vector<float>());
        } else {
            discreteValues.push_back(mcu::b_vector<float>());
            quantileBinEdges.push_back(data);
        }
    }
    
    file.close();
    isLoaded = true;
    
    Serial.println("Categorizer loaded into RAM");
    return true;
}

// Release categorizer from RAM
void Rf_categorizer::releaseCtg() {
    if (!isLoaded) {
        Serial.println("Categorizer not loaded");
        return;
    }
    
    isDiscrete.clear();
    discreteValues.clear();
    quantileBinEdges.clear();
    
    isLoaded = false;
    Serial.println("Categorizer released from RAM");
}

// Categorize a single feature value
uint8_t Rf_categorizer::categorizeFeature(uint16_t featureIdx, float value) const {
    if (featureIdx >= numFeatures) {
        return 0;
    }
    
    if (isDiscrete[featureIdx]) {
        // For discrete features, find matching value index
        const auto& values = discreteValues[featureIdx];
        for (uint8_t i = 0; i < values.size(); ++i) {
            if (abs(values[i] - value) < 1e-6f) { // Float comparison with tolerance
                return i;
            }
        }
        return 0; // Default if not found
    } else {
        // For continuous features, use quantile binning
        const auto& edges = quantileBinEdges[featureIdx];
        for (uint8_t bin = 0; bin < edges.size(); ++bin) {
            if (value < edges[bin]) {
                return bin;
            }
        }
        return edges.size(); // Last bin
    }
}

// Categorize an entire sample
mcu::b_vector<uint8_t> Rf_categorizer::categorizeSample(const mcu::b_vector<float>& sample) const {
    mcu::b_vector<uint8_t> result;
    bool preloaded = isLoaded;
    if (!preloaded) {
        if (!const_cast<Rf_categorizer*>(this)->loadCtg()) {
            Serial.println("Failed to load categorizer");
            return result; // Return empty result
        }
    }
    
    result.reserve(numFeatures);
    
    for (uint16_t i = 0; i < numFeatures && i < sample.size(); ++i) {
        result.push_back(categorizeFeature(i, sample[i]));
    }
    if(!preloaded){
        const_cast<Rf_categorizer*>(this)->releaseCtg(); // Release if we loaded it just for this operation
    }
    return result;
}

// Check if categorizer is valid
bool Rf_categorizer::isValid() const {
    return (numFeatures > 0 && groupsPerFeature > 0 && filename.length() > 0);
}

// Print categorizer information
void Rf_categorizer::printInfo() const {
    Serial.println("=== Rf_categorizer Info ===");
    Serial.println("Filename: " + filename);
    Serial.println("Features: " + String(numFeatures));
    Serial.println("Groups per feature: " + String(groupsPerFeature));
    Serial.println("Loaded: " + String(isLoaded ? "Yes" : "No"));
    
    if (isLoaded) {
        Serial.println("Memory usage: " + String(getMemoryUsage()) + " bytes");
        
        for (uint16_t i = 0; i < numFeatures && i < 5; i++) { // Show first 5 features
            Serial.print("Feature " + String(i) + ": ");
            if (isDiscrete[i]) {
                Serial.print("Discrete (" + String(discreteValues[i].size()) + " values)");
            } else {
                Serial.print("Continuous (" + String(quantileBinEdges[i].size()) + " edges)");
            }
            Serial.println();
        }
        
        if (numFeatures > 5) {
            Serial.println("... and " + String(numFeatures - 5) + " more features");
        }
    }
    Serial.println("========================");
}

// Calculate memory usage
size_t Rf_categorizer::getMemoryUsage() const {
    if (!isLoaded) return 0;
    
    size_t usage = sizeof(*this);
    usage += isDiscrete.cap() * sizeof(bool);
    
    for (const auto& vec : discreteValues) {
        usage += vec.cap() * sizeof(float);
    }
    
    for (const auto& vec : quantileBinEdges) {
        usage += vec.cap() * sizeof(float);
    }
    
    return usage;
}
