/*
  STL_MCU Library - Basic Containers Example
  
  This example demonstrates the usage of basic STL containers optimized for ESP32:
  - mcu::vector
  - mcu::unordered_map
  - mcu::unordered_set
  - mcu::pair
  
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <STL_MCU.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("STL_MCU Basic Containers Example");
  Serial.println("================================");
  
  // Test mcu::vector
  testVector();
  
  // Test mcu::unordered_map
  testUnorderedMap();
  
  // Test mcu::unordered_set
  testUnorderedSet();
  
  // Test mcu::pair
  testPair();
  
  Serial.println("All tests completed successfully!");
}

void loop() {
  // Nothing to do in loop
  delay(1000);
}

void testVector() {
  Serial.println("\n--- Testing mcu::vector ---");
  
  // Create a vector of integers
  mcu::vector<int> numbers;
  
  // Add some numbers
  for(int i = 1; i <= 10; i++) {
    numbers.push_back(i * i);
  }
  
  Serial.print("Vector size: ");
  Serial.println(numbers.size());
  
  Serial.print("Vector contents: ");
  for(size_t i = 0; i < numbers.size(); i++) {
    Serial.print(numbers[i]);
    if(i < numbers.size() - 1) Serial.print(", ");
  }
  Serial.println();
  
  // Test capacity management
  Serial.print("Capacity: ");
  Serial.println(numbers.capacity());
  
  numbers.reserve(20);
  Serial.print("After reserve(20), capacity: ");
  Serial.println(numbers.capacity());
}

void testUnorderedMap() {
  Serial.println("\n--- Testing mcu::unordered_map ---");
  
  // Create a map from string keys to integer values
  mcu::unordered_map<uint8_t, int> tempReadings;
  
  // Add some temperature readings
  tempReadings[1] = 25;  // Sensor 1: 25°C
  tempReadings[2] = 30;  // Sensor 2: 30°C
  tempReadings[3] = 22;  // Sensor 3: 22°C
  
  Serial.print("Map size: ");
  Serial.println(tempReadings.size());
  
  // Access values
  Serial.println("Temperature readings:");
  for(auto it = tempReadings.begin(); it != tempReadings.end(); ++it) {
    Serial.print("Sensor ");
    Serial.print(it->first);
    Serial.print(": ");
    Serial.print(it->second);
    Serial.println("°C");
  }
  
  // Test find
  auto found = tempReadings.find(2);
  if(found != tempReadings.end()) {
    Serial.print("Found sensor 2 with temperature: ");
    Serial.println(found->second);
  }
}

void testUnorderedSet() {
  Serial.println("\n--- Testing mcu::unordered_set ---");
  
  // Create a set of unique IDs
  mcu::unordered_set<uint8_t> deviceIds;
  
  // Add some device IDs
  deviceIds.insert(10);
  deviceIds.insert(20);
  deviceIds.insert(30);
  deviceIds.insert(20); // Duplicate - won't be added
  
  Serial.print("Set size: ");
  Serial.println(deviceIds.size());
  
  Serial.print("Device IDs: ");
  for(auto it = deviceIds.begin(); it != deviceIds.end(); ++it) {
    Serial.print(*it);
    Serial.print(" ");
  }
  Serial.println();
  
  // Test contains
  if(deviceIds.find(20) != deviceIds.end()) {
    Serial.println("Device ID 20 is registered");
  }
  
  if(deviceIds.find(99) == deviceIds.end()) {
    Serial.println("Device ID 99 is not registered");
  }
}

void testPair() {
  Serial.println("\n--- Testing mcu::pair ---");
  
  // Create pairs for coordinate system
  mcu::pair<float, float> coordinates = mcu::make_pair(12.5f, 25.3f);
  
  Serial.print("Coordinates: (");
  Serial.print(coordinates.first);
  Serial.print(", ");
  Serial.print(coordinates.second);
  Serial.println(")");
  
  // Create pair for sensor data
  auto sensorData = mcu::make_pair(42, 3.14f);
  Serial.print("Sensor data: ID=");
  Serial.print(sensorData.first);
  Serial.print(", Value=");
  Serial.println(sensorData.second);
}
