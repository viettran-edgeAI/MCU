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

// Receive categorizer from Serial in CSV format
bool Rf_categorizer::receiveFromPySerial(HardwareSerial& serial, unsigned long timeout) {
    String csvData = "";
    unsigned long startTime = millis();
    
    Serial.println("Waiting for categorizer data...");
    
    // Read until timeout or end marker
    while (millis() - startTime < timeout) {
        if (serial.available()) {
            String line = serial.readStringUntil('\n');
            line.trim();
            
            if (line == "EOF_CATEGORIZER") {
                break;
            }
            
            csvData += line + "\n";
            startTime = millis(); // Reset timeout on data reception
        }
        delay(10);
    }
    
    if (csvData.length() == 0) {
        Serial.println("No data received");
        return false;
    }
    
    // Save CSV data to temporary file
    String tempCsvFile = "/temp_categorizer.csv";
    File file = SPIFFS.open(tempCsvFile, "w");
    if (!file) {
        Serial.println("Failed to create temp CSV file");
        return false;
    }
    
    file.print(csvData);
    file.close();
    
    Serial.println("CSV data received successfully");
    
    // Set filename for binary file
    filename = "/categorizer.bin";
    
    // Convert to binary format
    return convertToBin();
}

// Convert CSV format to binary format
bool Rf_categorizer::convertToBin() {
    String csvFile = "/temp_categorizer.csv";
    
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
    mcu::b_vector<String> headerParts = split(header);
    
    if (headerParts.size() != 2) {
        csvIn.close();
        Serial.println("Invalid CSV header format");
        return false;
    }
    
    numFeatures = headerParts[0].toInt();
    groupsPerFeature = headerParts[1].toInt();
    
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
        String line = csvIn.readStringUntil('\n');
        line.trim();
        mcu::b_vector<String> parts = split(line);
        
        if (parts.size() < 2) {
            csvIn.close();
            binOut.close();
            Serial.println("Invalid feature line format");
            return false;
        }
        
        uint8_t isDiscreteFlag = parts[0].toInt();
        uint16_t dataCount = parts[1].toInt();
        
        // Write feature metadata
        binOut.write(&isDiscreteFlag, sizeof(isDiscreteFlag));
        binOut.write((uint8_t*)&dataCount, sizeof(dataCount));
        
        // Write feature data
        for (uint16_t j = 0; j < dataCount && (j + 2) < parts.size(); j++) {
            float value = parts[j + 2].toFloat();
            binOut.write((uint8_t*)&value, sizeof(value));
        }
    }
    
    csvIn.close();
    binOut.close();
    
    // Clean up temporary CSV file
    SPIFFS.remove(csvFile);
    
    Serial.println("Conversion to binary completed");
    return true;
}

// Receive categorizer data using interactive CSV input
bool Rf_categorizer::receiveFromSerialMonitor(bool exact_columns, bool print_file) {
    Serial.println("=== Categorizer Data Input ===");
    Serial.println("Please enter categorizer data in CSV format.");
    Serial.println("Expected format:");
    Serial.println("Line 1: numFeatures,groupsPerFeature");
    Serial.println("Line 2+: isDiscrete,dataCount,value1,value2,...");
    Serial.println("=====================================");
    
    // Use the reception_data function to get CSV input
    String csvPath = reception_data(exact_columns, print_file);
    
    // Check if reception_data was successful
    if (csvPath.length() == 0) {
        Serial.println("❌ Failed to receive data");
        return false;
    }
    
    // Check if file exists
    if (!SPIFFS.exists(csvPath)) {
        Serial.println("❌ CSV file not found: " + csvPath);
        return false;
    }
    
    // Create temporary copy for processing
    String tempCsvFile = "/temp_categorizer.csv";
    if (!cloneCSVFile(csvPath, tempCsvFile)) {
        Serial.println("❌ Failed to copy CSV file for processing");
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
    
    // Set filename for binary output
    filename = "/" + baseName + "_categorizer.bin";
    
    Serial.println("📁 Binary file will be saved as: " + filename);
    
    // Convert to binary format
    bool success = convertToBin();
    
    if (success) {
        Serial.println("✅ Categorizer data successfully processed and saved!");
        printInfo();
    } else {
        Serial.println("❌ Failed to process categorizer data");
    }
    
    return success;
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
    if (!isLoaded || featureIdx >= numFeatures) {
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
    if (!isLoaded) {
        Serial.println("Categorizer not loaded");
        return result;
    }
    
    result.reserve(numFeatures);
    
    for (uint16_t i = 0; i < numFeatures && i < sample.size(); ++i) {
        result.push_back(categorizeFeature(i, sample[i]));
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
