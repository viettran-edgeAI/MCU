/*
  STL_MCU Library - Sensor Data Logger Example
  
  This example demonstrates practical usage of STL_MCU containers
  for IoT sensor data collection and management:
  - Collecting sensor readings with timestamps
  - Storing data efficiently in containers
  - Processing and analyzing collected data
  - Saving/loading data to/from SPIFFS
  
  Hardware connections (optional):
  - DHT22 sensor on pin 4 (temperature/humidity)
  - LDR (Light sensor) on analog pin A0
  - Built-in LED for status indication
  
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <STL_MCU.h>
#include <ESP32_HOG.h>

// Sensor data structure
struct SensorReading {
  uint32_t timestamp;
  float temperature;
  float humidity;
  uint16_t lightLevel;
  
  SensorReading() : timestamp(0), temperature(0), humidity(0), lightLevel(0) {}
  SensorReading(uint32_t t, float temp, float hum, uint16_t light) 
    : timestamp(t), temperature(temp), humidity(hum), lightLevel(light) {}
};

// Global data storage
mcu::vector<SensorReading> sensorHistory;
mcu::unordered_map<uint8_t, float> currentReadings;
mcu::unordered_map<uint8_t, mcu::pair<float, float>> sensorStats; // min, max pairs

// Configuration
const int READINGS_BUFFER_SIZE = 100;
const int SENSOR_INTERVAL_MS = 5000;  // 5 seconds
const char* DATA_FILE = "/sensor_data.txt";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("STL_MCU Sensor Data Logger Example");
  Serial.println("==================================");
  
  // Initialize built-in LED
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Initialize SPIFFS
  if(mcu::initializeSPIFFS()) {
    Serial.println("✓ SPIFFS initialized");
  } else {
    Serial.println("✗ SPIFFS initialization failed");
  }
  
  // Initialize data structures
  initializeDataStructures();
  
  // Load previous data if available
  loadSensorData();
  
  Serial.println("System ready - Starting sensor monitoring...");
  Serial.println("Commands: 'stats', 'history', 'clear', 'save', 'memory'");
}

void loop() {
  static unsigned long lastReading = 0;
  static bool ledState = false;
  
  // Read sensors periodically
  if(millis() - lastReading >= SENSOR_INTERVAL_MS) {
    readSensors();
    updateStatistics();
    lastReading = millis();
    
    // Blink LED to show activity
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
  }
  
  // Process serial commands
  if(Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    processCommand(command);
  }
  
  delay(100);
}

void initializeDataStructures() {
  Serial.println("Initializing data structures...");
  
  // Reserve space for sensor history
  sensorHistory.reserve(READINGS_BUFFER_SIZE);
  
  // Initialize current readings map
  currentReadings[0] = 0.0;  // Temperature
  currentReadings[1] = 0.0;  // Humidity  
  currentReadings[2] = 0.0;  // Light level
  
  // Initialize statistics (min, max pairs)
  sensorStats[0] = mcu::make_pair(999.0f, -999.0f);  // Temperature
  sensorStats[1] = mcu::make_pair(999.0f, -999.0f);  // Humidity
  sensorStats[2] = mcu::make_pair(9999.0f, -9999.0f); // Light
  
  Serial.print("✓ Data structures initialized - Free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void readSensors() {
  uint32_t timestamp = millis();
  
  // Simulate sensor readings (replace with actual sensor code)
  float temperature = 20.0 + (random(-50, 150) / 10.0);  // 15-35°C range
  float humidity = 50.0 + (random(-200, 300) / 10.0);    // 30-80% range
  uint16_t lightLevel = random(0, 1024);                  // 0-1023 ADC range
  
  // For real sensors, uncomment and modify as needed:
  /*
  // DHT22 sensor reading
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  
  // LDR reading
  lightLevel = analogRead(A0);
  */
  
  // Update current readings
  currentReadings[0] = temperature;
  currentReadings[1] = humidity;
  currentReadings[2] = (float)lightLevel;
  
  // Create sensor reading record
  SensorReading reading(timestamp, temperature, humidity, lightLevel);
  
  // Add to history (maintain buffer size)
  if(sensorHistory.size() >= READINGS_BUFFER_SIZE) {
    // Remove oldest reading (simple FIFO)
    sensorHistory.erase(sensorHistory.begin());
  }
  sensorHistory.push_back(reading);
  
  // Print current reading
  Serial.print("Reading #");
  Serial.print(sensorHistory.size());
  Serial.print(": T=");
  Serial.print(temperature, 1);
  Serial.print("°C, H=");
  Serial.print(humidity, 1);
  Serial.print("%, L=");
  Serial.println(lightLevel);
}

void updateStatistics() {
  // Update min/max statistics
  for(auto it = currentReadings.begin(); it != currentReadings.end(); ++it) {
    uint8_t sensorId = it->first;
    float value = it->second;
    
    auto statsIt = sensorStats.find(sensorId);
    if(statsIt != sensorStats.end()) {
      // Update minimum
      if(value < statsIt->second.first) {
        statsIt->second.first = value;
      }
      // Update maximum
      if(value > statsIt->second.second) {
        statsIt->second.second = value;
      }
    }
  }
}

void processCommand(const String& command) {
  if(command == "stats") {
    printStatistics();
  } else if(command == "history") {
    printHistory();
  } else if(command == "clear") {
    clearData();
  } else if(command == "save") {
    saveSensorData();
  } else if(command == "memory") {
    printMemoryUsage();
  } else if(command == "help") {
    printHelp();
  } else {
    Serial.println("Unknown command. Type 'help' for available commands.");
  }
}

void printStatistics() {
  Serial.println("\n--- Sensor Statistics ---");
  
  const char* sensorNames[] = {"Temperature", "Humidity", "Light Level"};
  const char* units[] = {"°C", "%", ""};
  
  for(int i = 0; i < 3; i++) {
    auto it = sensorStats.find(i);
    if(it != sensorStats.end()) {
      Serial.print(sensorNames[i]);
      Serial.print(": Min=");
      Serial.print(it->second.first, 1);
      Serial.print(units[i]);
      Serial.print(", Max=");
      Serial.print(it->second.second, 1);
      Serial.print(units[i]);
      
      // Calculate current value
      auto currentIt = currentReadings.find(i);
      if(currentIt != currentReadings.end()) {
        Serial.print(", Current=");
        Serial.print(currentIt->second, 1);
        Serial.print(units[i]);
      }
      Serial.println();
    }
  }
  
  Serial.print("Total readings: ");
  Serial.println(sensorHistory.size());
}

void printHistory() {
  Serial.println("\n--- Sensor History (Last 10 readings) ---");
  Serial.println("Time\t\tTemp\tHumid\tLight");
  Serial.println("----\t\t----\t-----\t-----");
  
  size_t startIdx = (sensorHistory.size() > 10) ? sensorHistory.size() - 10 : 0;
  
  for(size_t i = startIdx; i < sensorHistory.size(); i++) {
    const SensorReading& reading = sensorHistory[i];
    
    Serial.print(reading.timestamp / 1000);
    Serial.print("s\t\t");
    Serial.print(reading.temperature, 1);
    Serial.print("\t");
    Serial.print(reading.humidity, 1);
    Serial.print("\t");
    Serial.println(reading.lightLevel);
  }
}

void clearData() {
  sensorHistory.clear();
  
  // Reset statistics
  sensorStats[0] = mcu::make_pair(999.0f, -999.0f);
  sensorStats[1] = mcu::make_pair(999.0f, -999.0f);
  sensorStats[2] = mcu::make_pair(9999.0f, -9999.0f);
  
  Serial.println("✓ All data cleared");
}

void saveSensorData() {
  Serial.println("Saving sensor data to SPIFFS...");
  
  File file = SPIFFS.open(DATA_FILE, FILE_WRITE);
  if(!file) {
    Serial.println("✗ Failed to open file for writing");
    return;
  }
  
  // Write header
  file.println("Timestamp,Temperature,Humidity,LightLevel");
  
  // Write all readings
  for(size_t i = 0; i < sensorHistory.size(); i++) {
    const SensorReading& reading = sensorHistory[i];
    
    file.print(reading.timestamp);
    file.print(",");
    file.print(reading.temperature, 2);
    file.print(",");
    file.print(reading.humidity, 2);
    file.print(",");
    file.println(reading.lightLevel);
  }
  
  file.close();
  
  Serial.print("✓ Saved ");
  Serial.print(sensorHistory.size());
  Serial.println(" readings to SPIFFS");
}

void loadSensorData() {
  Serial.println("Loading sensor data from SPIFFS...");
  
  File file = SPIFFS.open(DATA_FILE, FILE_READ);
  if(!file) {
    Serial.println("No previous data file found");
    return;
  }
  
  // Skip header line
  if(file.available()) {
    file.readStringUntil('\n');
  }
  
  int loadedCount = 0;
  while(file.available() && loadedCount < READINGS_BUFFER_SIZE) {
    String line = file.readStringUntil('\n');
    line.trim();
    
    if(line.length() > 0) {
      // Parse CSV line
      int commaIndex1 = line.indexOf(',');
      int commaIndex2 = line.indexOf(',', commaIndex1 + 1);
      int commaIndex3 = line.indexOf(',', commaIndex2 + 1);
      
      if(commaIndex1 > 0 && commaIndex2 > 0 && commaIndex3 > 0) {
        uint32_t timestamp = line.substring(0, commaIndex1).toInt();
        float temperature = line.substring(commaIndex1 + 1, commaIndex2).toFloat();
        float humidity = line.substring(commaIndex2 + 1, commaIndex3).toFloat();
        uint16_t lightLevel = line.substring(commaIndex3 + 1).toInt();
        
        SensorReading reading(timestamp, temperature, humidity, lightLevel);
        sensorHistory.push_back(reading);
        loadedCount++;
      }
    }
  }
  
  file.close();
  
  Serial.print("✓ Loaded ");
  Serial.print(loadedCount);
  Serial.println(" readings from SPIFFS");
}

void printMemoryUsage() {
  Serial.println("\n--- Memory Usage ---");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  
  Serial.print("Sensor history size: ");
  Serial.println(sensorHistory.size());
  Serial.print("Sensor history capacity: ");
  Serial.println(sensorHistory.capacity());
  
  // Estimate memory usage
  size_t historyMemory = sensorHistory.capacity() * sizeof(SensorReading);
  Serial.print("Estimated history memory: ");
  Serial.println(historyMemory);
  
  // Print SPIFFS info
  Serial.print("SPIFFS total: ");
  Serial.println(SPIFFS.totalBytes());
  Serial.print("SPIFFS used: ");
  Serial.println(SPIFFS.usedBytes());
}

void printHelp() {
  Serial.println("\n--- Available Commands ---");
  Serial.println("stats   - Show sensor statistics");
  Serial.println("history - Show recent sensor readings");
  Serial.println("clear   - Clear all stored data");
  Serial.println("save    - Save data to SPIFFS");
  Serial.println("memory  - Show memory usage");
  Serial.println("help    - Show this help message");
}
