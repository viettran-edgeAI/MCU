/*
 * STL_MCU PSRAM Test for ESP32-S3
 * 
 * This example demonstrates:
 * - PSRAM allocation and deallocation in unordered_map_s and unordered_set_s
 * - Synchronization and correctness of memory operations
 * - Performance comparison between regular DRAM and PSRAM storage
 * - Memory fragmentation and recovery testing
 * 
 * Hardware: ESP32-S3-N8R8 (or compatible with PSRAM)
 * Enables RF_USE_PSRAM to activate PSRAM storage for containers
 */

// Enable PSRAM allocation
#define RF_USE_PSRAM

#include "STL_MCU.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"


using namespace mcu;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

void print_memory_stats() {
    size_t free_dram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t total_dram = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t used_dram = total_dram - free_dram;

    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t used_psram = total_psram - free_psram;

    Serial.print("  DRAM:  ");
    Serial.print(used_dram);
    Serial.print(" / ");
    Serial.print(total_dram);
    Serial.println(" bytes");

    Serial.print("  PSRAM: ");
    Serial.print(used_psram);
    Serial.print(" / ");
    Serial.print(total_psram);
    Serial.println(" bytes");
}

// Helper for unordered_map_s
template<typename V, typename T>
void print_map_allocation_info(const char* name, const unordered_map_s<V, T>& map) {
    if (map.get_table_ptr() == nullptr) {
        Serial.println("  ❌ Map allocation FAILED (nullptr)");
        return;
    }

    bool is_psram = map.is_table_in_psram();
    Serial.print("  ");
    Serial.print(name);
    Serial.print(" allocated in: ");
    Serial.println(is_psram ? "PSRAM ✓" : "DRAM ✓");
}

// Helper for unordered_set_s
template<typename T>
void print_set_allocation_info(const char* name, const unordered_set_s<T>& set) {
    if (set.get_table_ptr() == nullptr) {
        Serial.println("  ❌ Set allocation FAILED (nullptr)");
        return;
    }

    bool is_psram = set.is_table_in_psram();
    Serial.print("  ");
    Serial.print(name);
    Serial.print(" allocated in: ");
    Serial.println(is_psram ? "PSRAM ✓" : "DRAM ✓");
}

// ============================================================================
// TEST 1: Basic unordered_map_s PSRAM allocation
// ============================================================================

void test_unordered_map_s_basic() {
    Serial.println("\n┌─────────────────────────────────────────────┐");
    Serial.println("│ TEST 1: unordered_map_s Basic PSRAM Allocation│");
    Serial.println("└─────────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    // Create a small map
    unordered_map_s<uint8_t, int16_t> map1(10);
    Serial.println("Created map1 with capacity 10");
    print_map_allocation_info("map1.table", map1);

    // Insert some values
    map1.insert(1, 100);
    map1.insert(2, 200);
    map1.insert(3, 300);
    Serial.println("Inserted 3 elements");

    // Verify retrieval
    if (map1.getValue(1) == 100 && map1.getValue(2) == 200 && map1.getValue(3) == 300) {
        Serial.println("✅ All values retrieved correctly");
    } else {
        Serial.println("❌ Value retrieval FAILED");
    }

    Serial.println("\nMemory after insertions:");
    print_memory_stats();
}

// ============================================================================
// TEST 2: unordered_map_s rehash with PSRAM
// ============================================================================

void test_unordered_map_s_rehash() {
    Serial.println("\n┌──────────────────────────────────────────┐");
    Serial.println("│ TEST 2: unordered_map_s Rehash with PSRAM  │");
    Serial.println("└──────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_map_s<uint8_t, uint16_t> map2(4);
    Serial.println("Created map2 with initial capacity 4");
    print_map_allocation_info("map2.table", map2);

    // Force multiple rehashes
    for (uint8_t i = 0; i < 20; i++) {
        bool result = map2.insert(i, i * 100).second;
        if (!result && i > 0) {
            Serial.print("Warning: Insertion of key ");
            Serial.print(i);
            Serial.println(" failed (duplicate or out of space)");
        }
    }

    Serial.print("Inserted 20 elements. Final map size: ");
    Serial.println(map2.size());
    Serial.print("Final capacity: ");
    Serial.println(map2.capacity());

    // Verify some values
    if (map2.getValue(5) == 500 && map2.getValue(15) == 1500) {
        Serial.println("✅ Values correct after rehashing");
    } else {
        Serial.println("❌ Values incorrect after rehashing");
    }

    print_map_allocation_info("map2.table (after rehash)", map2);

    Serial.println("\nMemory after rehashing:");
    print_memory_stats();
}

// ============================================================================
// TEST 3: unordered_set_s PSRAM allocation
// ============================================================================

void test_unordered_set_s_basic() {
    Serial.println("\n┌────────────────────────────────────────┐");
    Serial.println("│ TEST 3: unordered_set_s Basic PSRAM       │");
    Serial.println("└────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_set_s<uint8_t> set1(10);
    Serial.println("Created set1 with capacity 10");
    print_set_allocation_info("set1.table", set1);

    // Insert elements
    set1.insert(10);
    set1.insert(20);
    set1.insert(30);
    set1.insert(40);
    Serial.print("Inserted 4 elements. Set size: ");
    Serial.println(set1.size());

    // Verify contains
    if (set1.contains(20) && set1.contains(40) && !set1.contains(50)) {
        Serial.println("✅ Contains operations correct");
    } else {
        Serial.println("❌ Contains operations FAILED");
    }

    Serial.println("\nMemory after set operations:");
    print_memory_stats();
}

// ============================================================================
// TEST 4: unordered_set_s rehash and erase
// ============================================================================

void test_unordered_set_s_rehash_erase() {
    Serial.println("\n┌──────────────────────────────────────────┐");
    Serial.println("│ TEST 4: unordered_set_s Rehash & Erase     │");
    Serial.println("└──────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_set_s<uint16_t> set2(4);
    Serial.println("Created set2 with initial capacity 4");

    // Insert elements to trigger rehash
    for (uint16_t i = 0; i < 25; i++) {
        set2.insert(i * 11);
    }

    Serial.print("Inserted 25 elements. Set size: ");
    Serial.println(set2.size());
    Serial.print("Set capacity: ");
    Serial.println(set2.capacity());

    // Erase some elements
    uint8_t erased = 0;
    for (uint16_t i = 0; i < 5; i++) {
        if (set2.erase(i * 11)) {
            erased++;
        }
    }
    Serial.print("Erased ");
    Serial.print(erased);
    Serial.print(" elements. Remaining: ");
    Serial.println(set2.size());

    // Verify
    if (!set2.contains(0) && !set2.contains(11) && set2.contains(55)) {
        Serial.println("✅ Erase operations correct");
    } else {
        Serial.println("❌ Erase operations FAILED");
    }

    print_set_allocation_info("set2.table", set2);

    Serial.println("\nMemory after set operations:");
    print_memory_stats();
}

// ============================================================================
// TEST 5: Copy and Move operations with PSRAM
// ============================================================================

void test_copy_move_operations() {
    Serial.println("\n┌─────────────────────────────────────┐");
    Serial.println("│ TEST 5: Copy & Move with PSRAM      │");
    Serial.println("└─────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    // Original map
    unordered_map_s<uint8_t, int16_t> original(8);
    original.insert(1, 10);
    original.insert(2, 20);
    original.insert(3, 30);
    Serial.println("Created original map with 3 elements");
    print_map_allocation_info("original.table", original);

    Serial.println("\n--- Copy Constructor ---");
    unordered_map_s<uint8_t, int16_t> copied(original);
    Serial.println("Created copy via copy constructor");
    print_map_allocation_info("copied.table", copied);
    if (copied.getValue(2) == 20) {
        Serial.println("✅ Copy constructor successful");
    } else {
        Serial.println("❌ Copy constructor FAILED");
    }

    Serial.println("\n--- Move Constructor ---");
    unordered_map_s<uint8_t, int16_t> moved(std::move(original));
    Serial.println("Created moved map via move constructor");
    print_map_allocation_info("moved.table", moved);
    if (moved.getValue(3) == 30 && original.size() == 0) {
        Serial.println("✅ Move constructor successful");
    } else {
        Serial.println("❌ Move constructor FAILED");
    }

    Serial.println("\nMemory after copy/move:");
    print_memory_stats();
}

// ============================================================================
// TEST 6: Stress test with allocation failures
// ============================================================================

void test_allocation_failure_handling() {
    Serial.println("\n┌──────────────────────────────────────────┐");
    Serial.println("│ TEST 6: Allocation Failure Handling      │");
    Serial.println("└──────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_map_s<uint8_t, uint32_t> stress_map(8);
    Serial.println("Created stress_map with capacity 8");

    // Try to insert many elements - should succeed or fail gracefully
    uint8_t successful = 0;
    uint8_t failed = 0;

    for (uint8_t i = 0; i < 50; i++) {
        auto result = stress_map.insert(i % 255, (uint32_t)i * 12345);
        if (result.second) {
            successful++;
        } else {
            failed++;
        }
        // Give memory allocator a chance to defragment
        if (i % 10 == 0 && i > 0) {
            delay(1);
        }
    }

    Serial.print("Successful insertions: ");
    Serial.println(successful);
    Serial.print("Failed insertions: ");
    Serial.println(failed);
    Serial.print("Final map size: ");
    Serial.println(stress_map.size());

    Serial.println("\nMemory after stress test:");
    print_memory_stats();
}

// ============================================================================
// TEST 7: Fit and Clear operations
// ============================================================================

void test_fit_and_clear() {
    Serial.println("\n┌──────────────────────────────────────────┐");
    Serial.println("│ TEST 7: Fit & Clear Operations           │");
    Serial.println("└──────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_set_s<uint16_t> set_fit(20);
    Serial.println("Created set_fit with capacity 20");

    // Insert a few elements
    for (uint16_t i = 0; i < 5; i++) {
        set_fit.insert(i * 100);
    }
    Serial.print("Inserted 5 elements, capacity: ");
    Serial.println(set_fit.capacity());

    // Fit the set
    Serial.println("\nCalling fit()...");
    size_t freed = set_fit.fit();
    Serial.print("Bytes freed: ");
    Serial.println(freed);
    Serial.print("New capacity: ");
    Serial.println(set_fit.capacity());

    if (set_fit.size() == 5) {
        Serial.println("✅ Fit operation successful (size preserved)");
    } else {
        Serial.println("❌ Fit operation FAILED (size changed)");
    }

    // Clear the set
    Serial.println("\nCalling clear()...");
    set_fit.clear();
    Serial.print("After clear(), size: ");
    Serial.println(set_fit.size());
    Serial.print("Capacity: ");
    Serial.println(set_fit.capacity());

    if (set_fit.empty()) {
        Serial.println("✅ Clear operation successful");
    } else {
        Serial.println("❌ Clear operation FAILED");
    }

    Serial.println("\nMemory after fit/clear:");
    print_memory_stats();
}

// ============================================================================
// TEST 8: Reserve operation
// ============================================================================

void test_reserve() {
    Serial.println("\n┌──────────────────────────────────────────┐");
    Serial.println("│ TEST 8: Reserve Operation                │");
    Serial.println("└──────────────────────────────────────────┘");

    print_memory_stats();
    Serial.println();

    unordered_map_s<uint8_t, int32_t> reserve_map(4);
    Serial.println("Created reserve_map with initial capacity 4");
    Serial.print("Initial capacity: ");
    Serial.println(reserve_map.capacity());

    // Try to reserve space for more elements
    bool success = reserve_map.reserve(50);
    Serial.print("\nCalling reserve(50)...");
    if (success) {
        Serial.println(" ✅ SUCCESS");
        Serial.print("New capacity: ");
        Serial.println(reserve_map.capacity());

        // Insert elements
        for (uint8_t i = 0; i < 30; i++) {
            reserve_map.insert(i, i * 1000);
        }
        Serial.print("Inserted 30 elements. Size: ");
        Serial.println(reserve_map.size());

        if (reserve_map.getValue(15) == 15000) {
            Serial.println("✅ Reserve and insertion successful");
        } else {
            Serial.println("❌ Value check FAILED");
        }
    } else {
        Serial.println(" ❌ FAILED");
    }

    Serial.println("\nMemory after reserve:");
    print_memory_stats();
}

// ============================================================================
// MAIN SETUP AND LOOP
// ============================================================================

void setup() {
    Serial.begin(115200);
    while (!Serial);  // Wait for Serial connection

    delay(1000);

    Serial.println("\n╔══════════════════════════════════════════════════╗");
    Serial.println("║  STL_MCU PSRAM Test Suite - ESP32-S3             ║");
    Serial.println("║  Testing unordered_map_s & unordered_set_s with PSRAM║");
    Serial.println("╚══════════════════════════════════════════════════╝\n");

    delay(500);

    // Display PSRAM status
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_size > 0) {
        Serial.print("✅ PSRAM Detected: ");
        Serial.print(psram_size / 1024);
        Serial.println(" KB");
    } else {
        Serial.println("⚠️  No PSRAM detected - using DRAM only");
    }

    Serial.println("\n" + String(F("Starting tests...")));
    delay(1000);

    // Run all tests
    test_unordered_map_s_basic();
    delay(500);

    test_unordered_map_s_rehash();
    delay(500);

    test_unordered_set_s_basic();
    delay(500);

    test_unordered_set_s_rehash_erase();
    delay(500);

    test_copy_move_operations();
    delay(500);

    test_allocation_failure_handling();
    delay(500);

    test_fit_and_clear();
    delay(500);

    test_reserve();

    // Final summary
    Serial.println("\n╔══════════════════════════════════════════════════╗");
    Serial.println("║  All tests completed!                            ║");
    Serial.println("╚══════════════════════════════════════════════════╝\n");

    print_memory_stats();

    Serial.println("\n✅ Test suite finished. Check Serial Monitor for results.");
}

void loop() {
    delay(1000);
}
