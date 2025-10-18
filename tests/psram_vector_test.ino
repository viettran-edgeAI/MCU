/**
 * @file psram_vector_test.ino
 * @brief PSRAM-aware vector stress test for STL_MCU library on ESP32
 *
 * This sketch exercises all vector classes (vector, b_vector, packed_vector, ID_vector)
 * through a complete sequence of operations to verify PSRAM allocation and deallocation
 * work correctly on ESP32 microcontroller.
 *
 * Requirements:
 * - Board: ESP32 (e.g., ESP32-WROOM-32)
 * - Libraries: STL_MCU (installed in Arduino/libraries)
 * - Build flags: -std=c++17
 */

#define RF_USE_PSRAM

#include "STL_MCU.h"

using namespace mcu;

// ============================================================================
// Test results tracking
// ============================================================================
struct TestResults {
    int passed = 0;
    int failed = 0;

    void pass(const char* test_name) {
        passed++;
        Serial.print("[PASS] ");
        Serial.println(test_name);
    }

    void fail(const char* test_name, const char* reason = nullptr) {
        failed++;
        Serial.print("[FAIL] ");
        Serial.print(test_name);
        if (reason) {
            Serial.print(" - ");
            Serial.print(reason);
        }
        Serial.println();
    }

    void summary() {
        Serial.println("\n========== TEST SUMMARY ==========");
        Serial.print("Passed: ");
        Serial.println(passed);
        Serial.print("Failed: ");
        Serial.println(failed);
        Serial.print("Total:  ");
        Serial.println(passed + failed);
        Serial.println("==================================\n");
    }
};

TestResults results;

// PSRAM tracking
size_t psram_baseline = 0;

// ============================================================================
// PSRAM utility functions
// ============================================================================
void print_psram_status(const char* label = nullptr) {
    size_t total = mem_alloc::get_total_psram();
    size_t free = mem_alloc::get_free_psram();
    size_t used = total > free ? (total - free) : 0;
    
    if (label) {
        Serial.print("  [");
        Serial.print(label);
        Serial.print("] ");
    } else {
        Serial.print("  PSRAM: ");
    }
    
    Serial.print("Used: ");
    Serial.print(used);
    Serial.print(" / ");
    Serial.print(total);
    Serial.print(" bytes (");
    Serial.print((used * 100) / (total > 0 ? total : 1));
    Serial.println("%)");
}

void print_psram_delta(const char* label = nullptr) {
    size_t total = mem_alloc::get_total_psram();
    size_t free = mem_alloc::get_free_psram();
    size_t current_used = total > free ? (total - free) : 0;
    int32_t delta = (int32_t)current_used - (int32_t)psram_baseline;
    
    if (label) {
        Serial.print("  [");
        Serial.print(label);
        Serial.print("] ");
    } else {
        Serial.print("  Δ PSRAM: ");
    }
    
    if (delta >= 0) {
        Serial.print("+");
    }
    Serial.print(delta);
    Serial.print(" bytes (now: ");
    Serial.print(current_used);
    Serial.println(" bytes used)");
    
    psram_baseline = current_used;
}

// ============================================================================
// Assertion helper for Arduino
// ============================================================================
void assert_true(bool condition, const char* test_name, const char* message = nullptr) {
    if (!condition) {
        results.fail(test_name, message);
        delay(100); // Give serial time to flush
    } else {
        results.pass(test_name);
    }
}

// ============================================================================
// Test Suite 1: vector<T>
// ============================================================================
void test_vector_basic_construction() {
    vector<int> v(4, 1);
    assert_true(v.size() == 4, "vector_basic_construction", "size should be 4");
    assert_true(v.capacity() >= 4, "vector_basic_construction", "capacity should be >= 4");
    assert_true(v.data() != nullptr, "vector_basic_construction", "data pointer should not be null");
}

void test_vector_fill_operation() {
    vector<int> v(4, 1);
    v.fill(5);
    assert_true(v.size() == v.capacity(), "vector_fill_operation", "size should equal capacity after fill");
    
    bool all_filled = true;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] != 5) {
            all_filled = false;
            break;
        }
    }
    assert_true(all_filled, "vector_fill_operation", "all elements should be 5");
}

void test_vector_resize_operations() {
    vector<int> v(4, 1);
    
    v.resize(10, 3);
    assert_true(v.size() == 10, "vector_resize_operations", "size should be 10 after resize");
    assert_true(v[9] == 3, "vector_resize_operations", "new elements should be initialized to 3");
    
    v.fit();
    assert_true(v.capacity() == v.size(), "vector_resize_operations", "capacity should equal size after fit");
}

void test_vector_push_back() {
    vector<int> v(4, 1);
    size_t prev_size = v.size();
    
    v.push_back(9);
    assert_true(v.size() == prev_size + 1, "vector_push_back", "size should increase by 1");
    assert_true(v.back() == 9, "vector_push_back", "last element should be 9");
    assert_true(v.data() != nullptr, "vector_push_back", "data pointer should still be valid");
}

void test_vector_clear() {
    vector<int> v(4, 1);
    v.clear();
    assert_true(v.size() == 0, "vector_clear", "size should be 0 after clear");
}

void test_vector_psram_backing() {
    vector<int> v(100, 5);
    bool psram_backed = mem_alloc::is_psram_ptr(v.data());
    Serial.print("  >> vector<int> backing: ");
    Serial.println(psram_backed ? "PSRAM" : "internal memory");
    print_psram_status("large vector");
}

void run_vector_tests() {
    Serial.println("\n=== Testing vector<T> ===");
    psram_baseline = mem_alloc::get_total_psram() - mem_alloc::get_free_psram();
    print_psram_status("start");
    
    test_vector_basic_construction();
    print_psram_delta("after basic_construction");
    
    test_vector_fill_operation();
    print_psram_delta("after fill_operation");
    
    test_vector_resize_operations();
    print_psram_delta("after resize_operations");
    
    test_vector_push_back();
    print_psram_delta("after push_back");
    
    test_vector_clear();
    print_psram_delta("after clear");
    
    test_vector_psram_backing();
    print_psram_delta("end");
}

// ============================================================================
// Test Suite 2: b_vector (buffered vector with SBO)
// ============================================================================
void test_b_vector_basic_construction() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    assert_true(bv.size() == 0, "b_vector_basic_construction", "initial size should be 0");
    assert_true(bv.capacity() == SBO, "b_vector_basic_construction", "initial capacity should be SBO");
}

void test_b_vector_push_back() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    
    bv.push_back(1);
    bv.push_back(2);
    assert_true(bv.size() == 2, "b_vector_push_back", "size should be 2 after two pushes");
    assert_true(bv.back() == 2, "b_vector_push_back", "back element should be 2");
}

void test_b_vector_reserve_and_grow() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    
    bv.reserve(12);
    assert_true(bv.capacity() >= 12, "b_vector_reserve_and_grow", "capacity should be >= 12");
}

void test_b_vector_fill_operation() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    bv.reserve(12);
    
    bv.fill(3);
    assert_true(bv.size() == bv.capacity(), "b_vector_fill_operation", "size should equal capacity after fill");
    
    bool all_filled = true;
    for (size_t i = 0; i < bv.size(); ++i) {
        if (bv[i] != 3) {
            all_filled = false;
            break;
        }
    }
    assert_true(all_filled, "b_vector_fill_operation", "all elements should be 3");
}

void test_b_vector_resize() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    
    bv.resize(18, 4);
    assert_true(bv.size() == 18, "b_vector_resize", "size should be 18 after resize");
    assert_true(bv.back() == 4, "b_vector_resize", "back element should be 4");
    
    bv.fit();
    assert_true(bv.capacity() == bv.size(), "b_vector_resize", "capacity should equal size after fit");
}

void test_b_vector_clear() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    bv.push_back(1);
    bv.push_back(2);
    
    bv.clear();
    assert_true(bv.size() == 0, "b_vector_clear", "size should be 0 after clear");
}

void test_b_vector_psram_backing() {
    constexpr size_t SBO = 4;
    b_vector<int, SBO> bv;
    bv.reserve(64);
    
    if (bv.capacity() > SBO) {
        bool psram_backed = mem_alloc::is_psram_ptr(bv.data());
        Serial.print("  >> b_vector<int,");
        Serial.print(SBO);
        Serial.print("> backing: ");
        Serial.println(psram_backed ? "PSRAM" : "internal memory");
    } else {
        Serial.print("  >> b_vector<int,");
        Serial.print(SBO);
        Serial.println("> backing: SBO buffer");
    }
}

void run_b_vector_tests() {
    Serial.println("\n=== Testing b_vector<T> ===");
    psram_baseline = mem_alloc::get_total_psram() - mem_alloc::get_free_psram();
    print_psram_status("start");
    
    test_b_vector_basic_construction();
    print_psram_delta("after basic_construction");
    
    test_b_vector_push_back();
    print_psram_delta("after push_back");
    
    test_b_vector_reserve_and_grow();
    print_psram_delta("after reserve_and_grow");
    
    test_b_vector_fill_operation();
    print_psram_delta("after fill_operation");
    
    test_b_vector_resize();
    print_psram_delta("after resize");
    
    test_b_vector_clear();
    print_psram_delta("after clear");
    
    test_b_vector_psram_backing();
    print_psram_delta("end");
}

// ============================================================================
// Test Suite 3: packed_vector
// ============================================================================
void test_packed_vector_construction() {
    packed_vector<4> pv(4, 5);
    assert_true(pv.size() == 4, "packed_vector_construction", "size should be 4");
    
    bool all_initialized = true;
    for (size_t i = 0; i < pv.size(); ++i) {
        if (pv[i] != 5) {
            all_initialized = false;
            break;
        }
    }
    assert_true(all_initialized, "packed_vector_construction", "all elements should be 5");
}

void test_packed_vector_fill() {
    packed_vector<4> pv(4, 5);
    
    pv.fill(7);
    assert_true(pv.size() == pv.capacity(), "packed_vector_fill", "size should equal capacity after fill");
    
    bool all_filled = true;
    for (size_t i = 0; i < pv.size(); ++i) {
        if (pv[i] != 7) {
            all_filled = false;
            break;
        }
    }
    assert_true(all_filled, "packed_vector_fill", "all elements should be 7");
}

void test_packed_vector_resize() {
    packed_vector<4> pv(4, 5);
    
    pv.resize(9, 3);
    assert_true(pv.size() == 9, "packed_vector_resize", "size should be 9");
    assert_true(pv.back() == (3 & pv.max_value()), "packed_vector_resize", "back should be clamped to max value");
    
    pv.fit();
    assert_true(pv.capacity() == pv.size(), "packed_vector_resize", "capacity should equal size after fit");
}

void test_packed_vector_push_back() {
    packed_vector<4> pv(4, 5);
    size_t pre_push = pv.size();
    
    pv.push_back(12);
    assert_true(pv.size() == pre_push + 1, "packed_vector_push_back", "size should increase by 1");
    assert_true(pv.back() == (12 & pv.max_value()), "packed_vector_push_back", "value should be clamped");
}

void test_packed_vector_clear() {
    packed_vector<4> pv(4, 5);
    pv.clear();
    assert_true(pv.size() == 0, "packed_vector_clear", "size should be 0 after clear");
}

void test_packed_vector_reserve() {
    packed_vector<4> pv(4, 5);
    pv.reserve(16);
    assert_true(pv.capacity() >= 16, "packed_vector_reserve", "capacity should be >= 16");
}

void test_packed_vector_memory_usage() {
    packed_vector<4> pv(4, 5);
    size_t mem = pv.memory_usage();
    Serial.print("  >> packed_vector<4> memory usage: ");
    Serial.print(mem);
    Serial.println(" bytes");
}

void run_packed_vector_tests() {
    Serial.println("\n=== Testing packed_vector ===");
    psram_baseline = mem_alloc::get_total_psram() - mem_alloc::get_free_psram();
    print_psram_status("start");
    
    test_packed_vector_construction();
    print_psram_delta("after construction");
    
    test_packed_vector_fill();
    print_psram_delta("after fill");
    
    test_packed_vector_resize();
    print_psram_delta("after resize");
    
    test_packed_vector_push_back();
    print_psram_delta("after push_back");
    
    test_packed_vector_clear();
    print_psram_delta("after clear");
    
    test_packed_vector_reserve();
    print_psram_delta("after reserve");
    
    test_packed_vector_memory_usage();
    print_psram_delta("end");
}

// ============================================================================
// Test Suite 4: ID_vector
// ============================================================================
void test_id_vector_construction() {
    ID_vector<uint16_t, 2> ids;
    assert_true(ids.size() == 0, "id_vector_construction", "initial size should be 0");
}

void test_id_vector_push_and_count() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(5);
    ids.push_back(10);
    
    assert_true(ids.size() == 3, "id_vector_push_and_count", "size should be 3");
    assert_true(ids.count(5) == 2, "id_vector_push_and_count", "count of 5 should be 2");
    assert_true(ids.contains(10), "id_vector_push_and_count", "should contain 10");
}

void test_id_vector_range_operations() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(10);
    
    ids.reserve(80);
    ids.push_back(75);
    assert_true(ids.contains(75), "id_vector_range_operations", "should contain 75 after reserve");
    
    ids.set_maxID(200);
    assert_true(ids.contains(75), "id_vector_range_operations", "should still contain 75 after set_maxID");
}

void test_id_vector_set_range() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(10);
    
    ids.set_ID_range(0, 220);
    assert_true(ids.contains(5), "id_vector_set_range", "should still contain 5 after set_ID_range");
}

void test_id_vector_pop_operations() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(75);
    
    ids.pop_back();
    assert_true(!ids.contains(75), "id_vector_pop_operations", "should not contain 75 after pop_back");
}

void test_id_vector_erase() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(5);
    ids.erase(5);
    
    assert_true(ids.count(5) == 1, "id_vector_erase", "count of 5 should be 1 after one erase");
}

void test_id_vector_iteration() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(10);
    ids.push_back(15);
    
    size_t traversal_size = ids.size();
    size_t iterated = 0;
    
    for (auto value : ids) {
        (void)value; // Use value to avoid compiler warning
        iterated++;
    }
    
    assert_true(iterated == traversal_size, "id_vector_iteration", "iterated count should match size");
}

void test_id_vector_front_back() {
    ID_vector<uint16_t, 2> ids;
    
    ids.push_back(5);
    ids.push_back(50);
    ids.push_back(100);
    
    assert_true(ids.front() < ids.back(), "id_vector_front_back", "front should be less than back");
    assert_true(ids[0] == ids.front(), "id_vector_front_back", "first element should equal front");
}

void run_id_vector_tests() {
    Serial.println("\n=== Testing ID_vector ===");
    psram_baseline = mem_alloc::get_total_psram() - mem_alloc::get_free_psram();
    print_psram_status("start");
    
    test_id_vector_construction();
    print_psram_delta("after construction");
    
    test_id_vector_push_and_count();
    print_psram_delta("after push_and_count");
    
    test_id_vector_range_operations();
    print_psram_delta("after range_operations");
    
    test_id_vector_set_range();
    print_psram_delta("after set_range");
    
    test_id_vector_pop_operations();
    print_psram_delta("after pop_operations");
    
    test_id_vector_erase();
    print_psram_delta("after erase");
    
    test_id_vector_iteration();
    print_psram_delta("after iteration");
    
    test_id_vector_front_back();
    print_psram_delta("end");
}

// ============================================================================
// Arduino Setup and Loop
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000); // Give serial time to initialize
    
    Serial.println("\n\n");
    Serial.println("╔════════════════════════════════════════════╗");
    Serial.println("║  STL_MCU PSRAM Vector Stress Test Suite    ║");
    Serial.println("║  Board: ESP32                              ║");
    Serial.println("╚════════════════════════════════════════════╝");
    
    // Display system info
    Serial.print("Total PSRAM: ");
    Serial.print(mem_alloc::get_total_psram());
    Serial.println(" bytes");
    Serial.print("Free PSRAM:  ");
    Serial.print(mem_alloc::get_free_psram());
    Serial.println(" bytes");
    print_psram_status("initial");
    
    // Run all test suites
    run_vector_tests();
    Serial.println();
    
    run_b_vector_tests();
    Serial.println();
    
    run_packed_vector_tests();
    Serial.println();
    
    run_id_vector_tests();
    Serial.println();
    
    // Print final PSRAM status
    print_psram_status("FINAL");
    
    // Print summary
    results.summary();
    
    if (results.failed == 0) {
        Serial.println("✓ ALL TESTS PASSED!");
    } else {
        Serial.print("✗ ");
        Serial.print(results.failed);
        Serial.println(" TEST(S) FAILED!");
    }
    
    Serial.println("\nSerial monitor will stop printing.");
}

void loop() {
    // Test complete, nothing to do
    delay(1000);
}
