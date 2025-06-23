/*
  STL_MCU Library - Installation Test
  
  This example tests all major components of the STL_MCU library
  to verify proper installation and functionality.
  
  Upload this sketch to test your STL_MCU library installation.
  All tests should pass for a successful installation.
  
  Author: Viettran <tranvaviet@gmail.com>
  License: MIT
*/

#include <STL_MCU.h>
#include <ESP32_HOG.h>

// Test counters
int testsPassed = 0;
int testsTotal = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("STL_MCU Library Installation Test");
  Serial.println("=================================");
  Serial.print("ESP32 Free Heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.println();
  
  // Run all tests
  testVector();
  testUnorderedMap();
  testUnorderedSet();
  testPair();
  testChainedContainers();
  testStack();
  testHOGDescriptor();
  testCategorizer();
  testUtilityFunctions();
  
  // Print results
  Serial.println("\n" + String("=").substring(0, 40));
  Serial.println("TEST RESULTS");
  Serial.println(String("=").substring(0, 40));
  Serial.print("Tests passed: ");
  Serial.print(testsPassed);
  Serial.print(" / ");
  Serial.println(testsTotal);
  
  if(testsPassed == testsTotal) {
    Serial.println("🎉 ALL TESTS PASSED! STL_MCU is working correctly.");
  } else {
    Serial.println("❌ Some tests failed. Check your installation.");
  }
  
  Serial.print("Final free heap: ");
  Serial.println(ESP.getFreeHeap());
}

void loop() {
  // Blink LED to show test completion
  static bool ledState = false;
  static unsigned long lastBlink = 0;
  
  if(millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);
    lastBlink = millis();
  }
}

void runTest(const String& testName, bool condition) {
  testsTotal++;
  Serial.print("Testing ");
  Serial.print(testName);
  Serial.print("... ");
  
  if(condition) {
    Serial.println("✓ PASS");
    testsPassed++;
  } else {
    Serial.println("✗ FAIL");
  }
}

void testVector() {
  Serial.println("--- Vector Tests ---");
  
  mcu::vector<int> vec;
  runTest("vector creation", vec.size() == 0 && vec.empty());
  
  vec.push_back(10);
  vec.push_back(20);
  vec.push_back(30);
  runTest("vector push_back", vec.size() == 3 && vec[0] == 10 && vec[2] == 30);
  
  vec.pop_back();
  runTest("vector pop_back", vec.size() == 2 && vec.back() == 20);
  
  vec.reserve(100);
  runTest("vector reserve", vec.capacity() >= 100);
  
  vec.clear();
  runTest("vector clear", vec.size() == 0 && vec.empty());
  
  Serial.println();
}

void testUnorderedMap() {
  Serial.println("--- Unordered Map Tests ---");
  
  mcu::unordered_map<uint8_t, int> map;
  runTest("map creation", map.size() == 0 && map.empty());
  
  map[1] = 100;
  map[2] = 200;
  map[3] = 300;
  runTest("map insertion", map.size() == 3 && map[2] == 200);
  
  auto it = map.find(2);
  runTest("map find existing", it != map.end() && it->second == 200);
  
  auto it2 = map.find(99);
  runTest("map find non-existing", it2 == map.end());
  
  size_t erased = map.erase(1);
  runTest("map erase", erased == 1 && map.size() == 2);
  
  map.clear();
  runTest("map clear", map.size() == 0);
  
  Serial.println();
}

void testUnorderedSet() {
  Serial.println("--- Unordered Set Tests ---");
  
  mcu::unordered_set<uint8_t> set;
  runTest("set creation", set.size() == 0);
  
  auto result1 = set.insert(10);
  auto result2 = set.insert(20);
  auto result3 = set.insert(10); // Duplicate
  
  runTest("set insertion", set.size() == 2 && result1.second && result2.second && !result3.second);
  
  auto found = set.find(20);
  runTest("set find", found != set.end() && *found == 20);
  
  size_t erased = set.erase(10);
  runTest("set erase", erased == 1 && set.size() == 1);
  
  Serial.println();
}

void testPair() {
  Serial.println("--- Pair Tests ---");
  
  mcu::pair<int, float> p1(42, 3.14f);
  runTest("pair creation", p1.first == 42 && abs(p1.second - 3.14f) < 0.01f);
  
  auto p2 = mcu::make_pair(100, 2.5f);
  runTest("make_pair", p2.first == 100 && abs(p2.second - 2.5f) < 0.01f);
  
  mcu::pair<int, float> p3 = p1;
  runTest("pair copy", p3.first == p1.first && p3.second == p1.second);
  
  bool equal = (p1 == p3);
  bool notEqual = (p1 != p2);
  runTest("pair comparison", equal && notEqual);
  
  Serial.println();
}

void testChainedContainers() {
  Serial.println("--- Chained Container Tests ---");
  
  mcu::ChainedUnorderedMap<int> chainedMap;
  runTest("chained map creation", chainedMap.size() == 0);
  
  // Test with different key ranges
  chainedMap[1000] = 10;
  chainedMap[2000] = 20;
  chainedMap[3000] = 30;
  
  runTest("chained map insertion", chainedMap.size() == 3 && chainedMap[2000] == 20);
  
  auto found = chainedMap.find(1000);
  runTest("chained map find", found != chainedMap.end() && found->second == 10);
  
  size_t memBefore = chainedMap.memory_usage();
  chainedMap.fit();
  size_t memAfter = chainedMap.memory_usage();
  runTest("chained map fit", memAfter <= memBefore);
  
  mcu::ChainedUnorderedSet<uint16_t> chainedSet;
  chainedSet.insert(1000);
  chainedSet.insert(2000);
  chainedSet.insert(3000);
  
  runTest("chained set operations", chainedSet.size() == 3 && 
          chainedSet.find(2000) != chainedSet.end());
  
  Serial.println();
}

void testStack() {
  Serial.println("--- Stack Tests ---");
  
  mcu::Stack<int> stack;
  runTest("stack creation", stack.empty() && stack.size() == 0);
  
  stack.push(10);
  stack.push(20);
  stack.push(30);
  
  runTest("stack push", stack.size() == 3 && stack.top() == 30);
  
  int value = stack.top();
  stack.pop();
  runTest("stack pop", stack.size() == 2 && stack.top() == 20 && value == 30);
  
  Serial.println();
}

void testHOGDescriptor() {
  Serial.println("--- HOG Descriptor Tests ---");
  
  mcu::HOGDescriptorMCU::Params params;
  params.img_width = 8;
  params.img_height = 8;
  params.cell_size = 4;
  params.block_size = 2;
  params.block_stride = 1;
  params.nbins = 9;
  
  mcu::HOGDescriptorMCU hog(params);
  size_t expectedSize = hog.getFeatureSize();
  runTest("HOG descriptor creation", expectedSize > 0);
  
  // Create test image
  uint8_t testImage[64];
  for(int i = 0; i < 64; i++) {
    testImage[i] = (i * 4) % 256;
  }
  
  mcu::vector<float> features;
  hog.compute(testImage, features);
  
  runTest("HOG feature extraction", features.size() == expectedSize);
  
  // Check if features contain reasonable values
  bool validFeatures = true;
  for(size_t i = 0; i < features.size(); i++) {
    if(isnan(features[i]) || isinf(features[i])) {
      validFeatures = false;
      break;
    }
  }
  runTest("HOG feature validity", validFeatures);
  
  Serial.println();
}

void testCategorizer() {
  Serial.println("--- Categorizer Tests ---");
  
  mcu::Categorizer categorizer(3, 4);
  runTest("categorizer creation", categorizer.getNumFeatures() == 3 && 
          categorizer.getGroupsPerFeature() == 4);
  
  // Update feature ranges
  categorizer.updateFeatureRange(0, 10.0f);
  categorizer.updateFeatureRange(0, 20.0f);
  categorizer.updateFeatureRange(1, 5.0f);
  categorizer.updateFeatureRange(1, 15.0f);
  
  // Set up bins
  mcu::vector<float> bins;
  bins.push_back(0.0f);
  bins.push_back(10.0f);
  bins.push_back(20.0f);
  bins.push_back(30.0f);
  categorizer.setQuantileBinEdges(0, bins);
  
  uint8_t category = categorizer.categorizeFeature(0, 15.0f);
  runTest("categorizer feature categorization", category < 4);
  
  // Test sample categorization
  mcu::vector<float> sample;
  sample.push_back(15.0f);
  sample.push_back(10.0f);
  sample.push_back(25.0f);
  
  mcu::vector<uint8_t> categories = categorizer.categorizeSample(sample);
  runTest("categorizer sample categorization", categories.size() == 3);
  
  Serial.println();
}

void testUtilityFunctions() {
  Serial.println("--- Utility Function Tests ---");
  
  // Test SPIFFS initialization
  bool spiffsOk = mcu::initializeSPIFFS();
  runTest("SPIFFS initialization", spiffsOk);
  
  // Test memory info (shouldn't crash)
  Serial.print("Memory info test: ");
  mcu::printMemoryInfo();
  runTest("memory info function", true);
  
  Serial.println();
}
