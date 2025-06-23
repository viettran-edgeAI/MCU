/*
  STL_MCU Library - HOG Feature Extraction Example (Updated)
  Demonstrates HOG feature extraction using the new hog_transform API.
  Input: grayscale square image as uint8_t array in a .txt file (see digit_0_0.txt)
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <Arduino.h>
#include <STL_MCU.h>
#include "hog_transform.h"

// Include a sample grayscale image (see digit_0_0.txt)
#include "digit_0_0.txt" // defines: digit_0_0_progmem[]

const char* imageFileName = "digit_0_0.txt";
const uint8_t* imageData = digit_0_0_progmem;
const uint8_t image_size = 32; // 32x32 image

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("STL_MCU HOG Feature Extraction Example (New API)");
  Serial.println("===============================================");

  // Initialize SPIFFS if you want to write CSV (optional)
  if (mcu::initializeSPIFFS()) {
    Serial.println("SPIFFS initialized successfully");
  } else {
    Serial.println("SPIFFS initialization failed, continuing without file operations");
  }

  // Print memory info
  mcu::printMemoryInfo();

  // Extract HOG features
  const uint8_t desired_features = 144; // Example: request 36 features
  Serial.print("Extracting HOG features from ");
  Serial.println(imageFileName);

  mcu::vector<float> features = processImageToCSV(
    imageData, image_size, desired_features, imageFileName
    // Optionally add ", \"/hog_features.csv\"" to save to CSV
  );

  Serial.print("Extracted features count: ");
  Serial.println(features.size());

  // Print first 10 features
  Serial.println("First 10 HOG features:");
  for (uint8_t i = 0; i < features.size() && i < 10; ++i) {
    Serial.print("Feature[");
    Serial.print(i);
    Serial.print("]: ");
    Serial.println(features[i], 4);
  }

  // Feature statistics
  if (features.size() > 0) {
    float minVal = features[0], maxVal = features[0], sum = 0;
    for (uint8_t i = 0; i < features.size(); ++i) {
      float v = features[i];
      if (v < minVal) minVal = v;
      if (v > maxVal) maxVal = v;
      sum += v;
    }
    float mean = sum / features.size();
    Serial.println("\nFeature Statistics:");
    Serial.print("Min value: "); Serial.println(minVal, 4);
    Serial.print("Max value: "); Serial.println(maxVal, 4);
    Serial.print("Mean value: "); Serial.println(mean, 4);
  }

  // Mock classification
  Serial.println("\n--- Mock Classification Example ---");
  classifyFeatures(features);

  Serial.println("HOG example completed!");
}

void loop() {
  // Monitor memory usage periodically
  static unsigned long lastMemoryCheck = 0;
  if(millis() - lastMemoryCheck > 5000) {
    Serial.println("\n--- Memory Status ---");
    mcu::printMemoryInfo();
    lastMemoryCheck = millis();
  }
  
  delay(100);
}

// Simple mock classification based on feature energy and sparsity
void classifyFeatures(const mcu::vector<float>& features) {
  if (features.size() == 0) {
    Serial.println("No features to classify");
    return;
  }

  float energy = 0;
  for (uint8_t i = 0; i < features.size(); ++i) {
    energy += features[i] * features[i];
  }
  Serial.print("Feature energy: ");
  Serial.println(energy, 4);

  if (energy > 5.0) {
    Serial.println("Classification: HIGH_TEXTURE");
  } else if (energy > 1.0) {
    Serial.println("Classification: MEDIUM_TEXTURE");
  } else {
    Serial.println("Classification: LOW_TEXTURE");
  }

  // Calculate sparsity (percentage of near-zero features)
  uint8_t nearZeroCount = 0;
  const float threshold = 0.01f;
  for (uint8_t i = 0; i < features.size(); ++i) {
    if (features[i] > -threshold && features[i] < threshold) {
      nearZeroCount++;
    }
  }
  float sparsity = (float)nearZeroCount / features.size() * 100;
  Serial.print("Feature sparsity: ");
  Serial.print(sparsity, 1);
  Serial.println("%");
}
