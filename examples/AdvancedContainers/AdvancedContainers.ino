/*
  STL_MCU Library - Advanced Containers Example
  
  This example demonstrates advanced usage of STL containers including:
  - ChainedUnorderedMap for handling large key ranges
  - ChainedUnorderedSet for efficient set operations
  - Stack implementation
  - Memory management and optimization
  
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <STL_MCU.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("STL_MCU Advanced Containers Example");
  Serial.println("===================================");
  
  // Print initial memory status
  Serial.print("Free heap at start: ");
  Serial.println(ESP.getFreeHeap());
  
  // Test ChainedUnorderedMap
  testChainedUnorderedMap();
  
  // Test ChainedUnorderedSet  
  testChainedUnorderedSet();
  
  // Test Stack
  testStack();
  
  // Test memory management
  testMemoryManagement();
  
  Serial.println("Advanced containers example completed!");
}

void loop() {
  // Monitor memory usage
  static unsigned long lastCheck = 0;
  if(millis() - lastCheck > 5000) {
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    lastCheck = millis();
  }
  delay(100);
}

void testChainedUnorderedMap() {
  Serial.println("\n--- Testing ChainedUnorderedMap ---");
  
  // ChainedUnorderedMap can handle large key ranges efficiently
  mcu::ChainedUnorderedMap<int> sensorNetwork;
  
  // Simulate a sensor network with IDs from different ranges
  // Range 1: Temperature sensors (1000-1010)
  sensorNetwork[1001] = 25;  // 25°C
  sensorNetwork[1003] = 27;  // 27°C
  sensorNetwork[1007] = 23;  // 23°C
  
  // Range 2: Humidity sensors (2000-2010)
  sensorNetwork[2001] = 60;  // 60%
  sensorNetwork[2005] = 65;  // 65%
  
  // Range 3: Pressure sensors (3000-3010)
  sensorNetwork[3002] = 1013; // 1013 hPa
  sensorNetwork[3008] = 1015; // 1015 hPa
  
  Serial.print("ChainedUnorderedMap size: ");
  Serial.println(sensorNetwork.size());
  
  Serial.print("Memory usage: ");
  Serial.println(sensorNetwork.memory_usage());
  
  // Iterate through all sensors
  Serial.println("Sensor readings:");
  for(auto it = sensorNetwork.begin(); it != sensorNetwork.end(); ++it) {
    Serial.print("Sensor ");
    Serial.print(it->first);
    Serial.print(": ");
    Serial.println(it->second);
  }
  
  // Test find operation
  auto found = sensorNetwork.find(2001);
  if(found != sensorNetwork.end()) {
    Serial.print("Found humidity sensor 2001 with value: ");
    Serial.println(found->second);
  }
  
  // Test erase operation
  sensorNetwork.erase(1007);
  Serial.print("After erasing sensor 1007, size: ");
  Serial.println(sensorNetwork.size());
  
  // Test memory optimization
  size_t memoryBefore = sensorNetwork.memory_usage();
  size_t freedMemory = sensorNetwork.fit();
  Serial.print("Memory freed by fit(): ");
  Serial.println(freedMemory);
  Serial.print("Memory usage after fit(): ");
  Serial.println(sensorNetwork.memory_usage());
}

void testChainedUnorderedSet() {
  Serial.println("\n--- Testing ChainedUnorderedSet ---");
  
  // ChainedUnorderedSet for tracking active device IDs across ranges
  mcu::ChainedUnorderedSet<uint16_t> activeDevices;
  
  // Add devices from different ID ranges
  // IoT devices (10000-19999)
  activeDevices.insert(10001);
  activeDevices.insert(10050);
  activeDevices.insert(10100);
  
  // Mobile devices (20000-29999)
  activeDevices.insert(20001);
  activeDevices.insert(20025);
  
  // Server devices (30000-39999)
  activeDevices.insert(30001);
  activeDevices.insert(30010);
  activeDevices.insert(30020);
  
  Serial.print("Active devices count: ");
  Serial.println(activeDevices.size());
  
  // List all active devices
  Serial.println("Active device IDs:");
  for(auto it = activeDevices.begin(); it != activeDevices.end(); ++it) {
    Serial.print("Device: ");
    Serial.println(*it);
  }
  
  // Test membership
  uint16_t testDeviceId = 20001;
  if(activeDevices.find(testDeviceId) != activeDevices.end()) {
    Serial.print("Device ");
    Serial.print(testDeviceId);
    Serial.println(" is active");
  }
  
  testDeviceId = 99999;
  if(activeDevices.find(testDeviceId) == activeDevices.end()) {
    Serial.print("Device ");
    Serial.print(testDeviceId);
    Serial.println(" is not active");
  }
  
  // Remove some devices
  activeDevices.erase(10050);
  activeDevices.erase(30010);
  
  Serial.print("After removing 2 devices, count: ");
  Serial.println(activeDevices.size());
}

void testStack() {
  Serial.println("\n--- Testing Stack ---");
  
  // Create stack for processing commands
  mcu::Stack<String> commandStack;
  
  // Push some commands
  commandStack.push("INIT_SENSORS");
  commandStack.push("READ_TEMPERATURE");
  commandStack.push("READ_HUMIDITY");
  commandStack.push("SEND_DATA");
  commandStack.push("SLEEP_MODE");
  
  Serial.print("Stack size: ");
  Serial.println(commandStack.size());
  
  Serial.println("Processing commands (LIFO order):");
  while(!commandStack.empty()) {
    String command = commandStack.top();
    commandStack.pop();
    
    Serial.print("Executing: ");
    Serial.println(command);
    
    // Simulate command execution time
    delay(100);
  }
  
  Serial.println("All commands processed");
  
  // Test stack with numbers for calculation
  mcu::Stack<float> calculatorStack;
  
  // Simulate postfix expression: 5 3 + 2 * (which is (5+3)*2 = 16)
  calculatorStack.push(5.0);
  calculatorStack.push(3.0);
  
  // Addition operation
  float b = calculatorStack.top(); calculatorStack.pop();
  float a = calculatorStack.top(); calculatorStack.pop();
  calculatorStack.push(a + b);
  
  calculatorStack.push(2.0);
  
  // Multiplication operation
  b = calculatorStack.top(); calculatorStack.pop();
  a = calculatorStack.top(); calculatorStack.pop();
  calculatorStack.push(a * b);
  
  Serial.print("Calculator result: ");
  Serial.println(calculatorStack.top());
}

void testMemoryManagement() {
  Serial.println("\n--- Testing Memory Management ---");
  
  Serial.print("Free heap before test: ");
  Serial.println(ESP.getFreeHeap());
  
  {
    // Create containers in local scope
    mcu::vector<int> largeVector;
    mcu::unordered_map<uint8_t, String> stringMap;
    
    // Fill with data
    largeVector.reserve(1000);
    for(int i = 0; i < 500; i++) {
      largeVector.push_back(i * i);
    }
    
    for(int i = 0; i < 50; i++) {
      stringMap[i] = "Sensor_" + String(i);
    }
    
    Serial.print("Free heap with containers: ");
    Serial.println(ESP.getFreeHeap());
    
    Serial.print("Vector size: ");
    Serial.println(largeVector.size());
    Serial.print("Vector capacity: ");
    Serial.println(largeVector.capacity());
    
    Serial.print("Map size: ");
    Serial.println(stringMap.size());
  } // Containers go out of scope here
  
  Serial.print("Free heap after cleanup: ");
  Serial.println(ESP.getFreeHeap());
  
  // Test container reuse
  mcu::vector<float> reusableVector;
  
  for(int cycle = 0; cycle < 3; cycle++) {
    Serial.print("Cycle ");
    Serial.print(cycle + 1);
    Serial.print(" - ");
    
    // Add data
    for(int i = 0; i < 100; i++) {
      reusableVector.push_back(random(0, 1000) / 10.0);
    }
    
    Serial.print("Size: ");
    Serial.print(reusableVector.size());
    
    // Clear for next cycle
    reusableVector.clear();
    
    Serial.print(", After clear: ");
    Serial.println(reusableVector.size());
  }
  
  // Test shrink_to_fit equivalent
  reusableVector.reserve(1000);
  Serial.print("Reserved capacity: ");
  Serial.println(reusableVector.capacity());
  
  // Add only a few elements
  for(int i = 0; i < 10; i++) {
    reusableVector.push_back(i);
  }
  
  Serial.print("Used size: ");
  Serial.println(reusableVector.size());
  
  // In STL_MCU, you can create a new vector to reduce capacity
  mcu::vector<float> optimizedVector(reusableVector);
  Serial.print("Optimized capacity: ");
  Serial.println(optimizedVector.capacity());
}
