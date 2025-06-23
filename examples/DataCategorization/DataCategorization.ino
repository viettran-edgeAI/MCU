/*
  STL_MCU Library - Data Categorization Example
  
  This example demonstrates the Categorizer class for feature preprocessing
  and categorization, which is useful for machine learning applications
  on microcontrollers.
  
  The Categorizer can:
  - Handle both discrete and continuous features
  - Quantize continuous features into bins
  - Save/load categorization parameters to/from files
  
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <STL_MCU.h>
#include <ESP32_HOG.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("STL_MCU Data Categorization Example");
  Serial.println("===================================");
  
  // Initialize SPIFFS for file operations
  if(mcu::initializeSPIFFS()) {
    Serial.println("SPIFFS initialized successfully");
  } else {
    Serial.println("Warning: SPIFFS initialization failed");
  }
  
  // Print initial memory info
  mcu::printMemoryInfo();
  
  // Test categorization functionality
  testCategorization();
  
  Serial.println("Categorization example completed!");
}

void loop() {
  // Nothing to do in loop
  delay(1000);
}

void testCategorization() {
  Serial.println("\n--- Testing Data Categorization ---");
  
  // Create sample dataset
  const int numFeatures = 4;
  const int numSamples = 10;
  const int groupsPerFeature = 3;
  
  // Create categorizer
  mcu::Categorizer categorizer(numFeatures, groupsPerFeature);
  
  // Generate sample data
  Serial.println("Generating sample dataset...");
  mcu::vector<mcu::vector<float>> dataset;
  generateSampleData(dataset, numSamples, numFeatures);
  
  // Print the dataset
  printDataset(dataset);
  
  // Train the categorizer (build feature ranges and quantization)
  Serial.println("\nTraining categorizer...");
  trainCategorizer(categorizer, dataset);
  
  // Test categorization on the dataset
  Serial.println("\nTesting categorization...");
  testCategorizationOnDataset(categorizer, dataset);
  
  // Test file operations
  testFileOperations(categorizer);
  
  // Print categorizer info
  categorizer.printInfo();
}

void generateSampleData(mcu::vector<mcu::vector<float>>& dataset, int numSamples, int numFeatures) {
  dataset.clear();
  dataset.reserve(numSamples);
  
  for(int i = 0; i < numSamples; i++) {
    mcu::vector<float> sample;
    sample.reserve(numFeatures);
    
    // Feature 0: Temperature (continuous, 20-40°C)
    sample.push_back(20.0f + (i * 2.0f));
    
    // Feature 1: Humidity (continuous, 30-80%)
    sample.push_back(30.0f + (i * 5.0f));
    
    // Feature 2: Light level (discrete: low=1, medium=2, high=3)
    sample.push_back((float)((i % 3) + 1));
    
    // Feature 3: Battery voltage (continuous, 3.0-4.2V)
    sample.push_back(3.0f + (i * 0.12f));
    
    dataset.push_back(sample);
  }
}

void printDataset(const mcu::vector<mcu::vector<float>>& dataset) {
  Serial.println("Sample Dataset:");
  Serial.println("ID\tTemp\tHumid\tLight\tBatt");
  Serial.println("--\t----\t-----\t-----\t----");
  
  for(size_t i = 0; i < dataset.size(); i++) {
    Serial.print(i);
    Serial.print("\t");
    
    for(size_t j = 0; j < dataset[i].size(); j++) {
      Serial.print(dataset[i][j], 1);
      Serial.print("\t");
    }
    Serial.println();
  }
}

void trainCategorizer(mcu::Categorizer& categorizer, const mcu::vector<mcu::vector<float>>& dataset) {
  // Update feature ranges for all samples
  for(size_t i = 0; i < dataset.size(); i++) {
    for(size_t j = 0; j < dataset[i].size(); j++) {
      categorizer.updateFeatureRange(j, dataset[i][j]);
    }
  }
  
  // Set up discrete feature (Light level)
  mcu::vector<float> lightLevels;
  lightLevels.push_back(1.0f); // Low
  lightLevels.push_back(2.0f); // Medium  
  lightLevels.push_back(3.0f); // High
  categorizer.setDiscreteFeature(2, lightLevels);
  
  // Set up quantile bin edges for continuous features
  // For simplicity, we'll use uniform bins based on min/max values
  
  // Temperature bins
  mcu::vector<float> tempBins;
  tempBins.push_back(20.0f);  // Low temp
  tempBins.push_back(30.0f);  // Medium temp
  tempBins.push_back(40.0f);  // High temp
  categorizer.setQuantileBinEdges(0, tempBins);
  
  // Humidity bins
  mcu::vector<float> humidBins;
  humidBins.push_back(30.0f);  // Low humidity
  humidBins.push_back(55.0f);  // Medium humidity
  humidBins.push_back(80.0f);  // High humidity
  categorizer.setQuantileBinEdges(1, humidBins);
  
  // Battery voltage bins
  mcu::vector<float> battBins;
  battBins.push_back(3.0f);   // Low battery
  battBins.push_back(3.6f);   // Medium battery
  battBins.push_back(4.2f);   // High battery
  categorizer.setQuantileBinEdges(3, battBins);
  
  Serial.println("Categorizer training completed");
}

void testCategorizationOnDataset(mcu::Categorizer& categorizer, const mcu::vector<mcu::vector<float>>& dataset) {
  Serial.println("Categorization Results:");
  Serial.println("ID\tTemp\tHumid\tLight\tBatt\t|\tCategorized");
  Serial.println("--\t----\t-----\t-----\t----\t|\t-----------");
  
  for(size_t i = 0; i < dataset.size(); i++) {
    // Print original values
    Serial.print(i);
    Serial.print("\t");
    
    for(size_t j = 0; j < dataset[i].size(); j++) {
      Serial.print(dataset[i][j], 1);
      Serial.print("\t");
    }
    Serial.print("|\t");
    
    // Categorize the sample
    mcu::vector<uint8_t> categorized = categorizer.categorizeSample(dataset[i]);
    
    // Print categorized values
    for(size_t j = 0; j < categorized.size(); j++) {
      Serial.print((int)categorized[j]);
      if(j < categorized.size() - 1) Serial.print(",");
    }
    Serial.println();
  }
}

void testFileOperations(mcu::Categorizer& categorizer) {
  Serial.println("\n--- Testing File Operations ---");
  
  const char* filename = "/categorizer_config.bin";
  
  // Save categorizer to file
  Serial.println("Saving categorizer to file...");
  if(categorizer.saveToBinary(filename)) {
    Serial.println("Categorizer saved successfully");
  } else {
    Serial.println("Failed to save categorizer");
    return;
  }
  
  // Create a new categorizer and load from file
  Serial.println("Loading categorizer from file...");
  mcu::Categorizer loadedCategorizer;
  
  if(loadedCategorizer.loadFromBinary(filename)) {
    Serial.println("Categorizer loaded successfully");
    
    // Compare configurations
    Serial.print("Original features: ");
    Serial.println(categorizer.getNumFeatures());
    Serial.print("Loaded features: ");
    Serial.println(loadedCategorizer.getNumFeatures());
    
    Serial.print("Original groups per feature: ");
    Serial.println(categorizer.getGroupsPerFeature());
    Serial.print("Loaded groups per feature: ");
    Serial.println(loadedCategorizer.getGroupsPerFeature());
    
    // Test categorization with loaded categorizer
    mcu::vector<float> testSample;
    testSample.push_back(25.0f);  // Temperature
    testSample.push_back(50.0f);  // Humidity
    testSample.push_back(2.0f);   // Light level
    testSample.push_back(3.7f);   // Battery voltage
    
    mcu::vector<uint8_t> result1 = categorizer.categorizeSample(testSample);
    mcu::vector<uint8_t> result2 = loadedCategorizer.categorizeSample(testSample);
    
    Serial.print("Original categorization: ");
    for(size_t i = 0; i < result1.size(); i++) {
      Serial.print((int)result1[i]);
      if(i < result1.size() - 1) Serial.print(",");
    }
    Serial.println();
    
    Serial.print("Loaded categorization: ");
    for(size_t i = 0; i < result2.size(); i++) {
      Serial.print((int)result2[i]);
      if(i < result2.size() - 1) Serial.print(",");
    }
    Serial.println();
    
    // Check if results match
    bool match = (result1.size() == result2.size());
    if(match) {
      for(size_t i = 0; i < result1.size(); i++) {
        if(result1[i] != result2[i]) {
          match = false;
          break;
        }
      }
    }
    
    if(match) {
      Serial.println("✓ File save/load test PASSED");
    } else {
      Serial.println("✗ File save/load test FAILED");
    }
    
  } else {
    Serial.println("Failed to load categorizer");
  }
}
