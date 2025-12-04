/*
 * unordered_map/Set PSRAM Sequence Test
 * 
 * Tests PSRAM allocation/deallocation synchronization and safe API sequencing
 * for unordered_map and unordered_set after capacity operations.
 * 
 * Build & Upload:
 * - Select ESP32 board in Arduino IDE
 * - Optional: Define RF_USE_PSRAM before including STL_MCU.h to enable PSRAM
 * - Upload and open Serial Monitor at 115200 baud
 */

#define RF_USE_PSRAM  // Enable PSRAM allocation for this test

#include <STL_MCU.h>

using namespace mcu;

// Test counters for verbose output
int passCount = 0;
int failCount = 0;

void testAssert(bool condition, const char* message) {
    if (condition) {
        Serial.print("[PASS] ");
        Serial.println(message);
        passCount++;
    } else {
        Serial.print("[FAIL] ");
        Serial.println(message);
        failCount++;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("unordered_map/Set PSRAM Test");
    Serial.println("========================================\n");
    
    // Test unordered_map
    Serial.println("--- Testing unordered_map ---");
    testChainedMap();
    
    Serial.println();
    
    // Test unordered_set
    Serial.println("--- Testing unordered_set ---");
    testChainedSet();
    
    Serial.println("\n========================================");
    Serial.print("Results: ");
    Serial.print(passCount);
    Serial.print(" passed, ");
    Serial.print(failCount);
    Serial.println(" failed");
    Serial.println("========================================\n");
    
    if (failCount == 0) {
        Serial.println("All tests PASSED!");
    } else {
        Serial.println("Some tests FAILED!");
    }
}

void loop() {
    delay(10000);  // Idle loop
}

void testChainedMap() {
    unordered_map<uint16_t, uint16_t> cmap;
    
    testAssert(cmap.empty(), "unordered_map created empty");
    
    size_t freed = cmap.fit();
    testAssert(freed >= 0, "unordered_map fit() succeeded");
    
    testAssert(cmap.reserve(128), "unordered_map reserve(128) succeeded");
    
    auto mapFullness = cmap.set_fullness(0.75f);
    testAssert(mapFullness.first, "unordered_map set_fullness(0.75) succeeded");
    
    Serial.print("  Inserting 100 elements... ");
    for (uint16_t i = 0; i < 100; ++i) {
        bool inserted = cmap.insert(i, static_cast<uint16_t>(i * 2));
        if (!inserted) {
            Serial.println("FAILED at insert");
            failCount++;
            return;
        }
    }
    Serial.println("OK");
    
    testAssert(cmap.size() == 100, "unordered_map size() == 100 after inserts");
    
    Serial.print("  Finding 100 elements... ");
    bool findOk = true;
    for (uint16_t i = 0; i < 100; ++i) {
        auto it = cmap.find(i);
        if (it == cmap.end() || it->second != i * 2) {
            findOk = false;
            break;
        }
    }
    testAssert(findOk, "All 100 elements found with correct values");
    
    bool erased = cmap.erase(0);
    testAssert(erased, "unordered_map erase(0) succeeded");
    testAssert(cmap.size() == 99, "unordered_map size() == 99 after erase");
    
    cmap.clear();
    testAssert(cmap.empty(), "unordered_map empty after clear()");
    
    bool insertAfterClear = cmap.insert(500, 42);
    testAssert(insertAfterClear, "unordered_map insert after clear succeeded");
    
    cmap.clear();
    testAssert(cmap.empty(), "unordered_map empty after second clear()");
    
    Serial.println();
}

void testChainedSet() {
    unordered_set<uint16_t> cset;
    
    testAssert(cset.empty(), "unordered_set created empty");
    
    size_t freed = cset.fit();
    testAssert(freed >= 0, "unordered_set fit() succeeded");
    
    testAssert(cset.reserve(128), "unordered_set reserve(128) succeeded");
    
    auto setFullness = cset.set_fullness(0.8f);
    testAssert(setFullness.first, "unordered_set set_fullness(0.8) succeeded");
    
    Serial.print("  Inserting 50 elements... ");
    for (uint16_t i = 0; i < 50; ++i) {
        bool inserted = cset.insert(i);
        if (!inserted) {
            Serial.println("FAILED at insert");
            failCount++;
            return;
        }
    }
    Serial.println("OK");
    
    testAssert(cset.size() == 50, "unordered_set size() == 50 after inserts");
    
    Serial.print("  Finding 50 elements... ");
    bool findOk = true;
    for (uint16_t i = 0; i < 50; ++i) {
        auto it = cset.find(i);
        if (it == cset.end()) {
            findOk = false;
            break;
        }
    }
    testAssert(findOk, "All 50 elements found");
    
    Serial.print("  Erasing every 2nd element (0,2,4...)... ");
    size_t removed = 0;
    for (uint16_t i = 0; i < 50; i += 2) {
        if (cset.erase(i)) {
            ++removed;
        }
    }
    Serial.println("OK");
    
    testAssert(removed > 0, "unordered_set erased elements");
    
    cset.clear();
    testAssert(cset.empty(), "unordered_set empty after clear()");
    
    // Final chained operations sequence
    testAssert(cset.reserve(30), "unordered_set reserve(30) after clear");
    
    auto fullness2 = cset.set_fullness(0.9f);
    testAssert(fullness2.first, "unordered_set set_fullness(0.9) after clear");
    
    bool setInsert = cset.insert(15);
    testAssert(setInsert, "unordered_set insert after fullness change");
    
    bool setErased = cset.erase(15);
    testAssert(setErased, "unordered_set erase after insert");
    
    cset.clear();
    testAssert(cset.empty(), "unordered_set empty after final clear()");
    
    Serial.println();
}
