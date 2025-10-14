#include "../../../src/STL_MCU.h"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace mcu;

// ANSI color codes for terminal output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"
#define BOLD "\033[1m"

int test_count = 0;
int passed_count = 0;

void print_test_header(const char* test_name) {
    std::cout << "\n" << BOLD << YELLOW << "Testing: " << test_name << RESET << std::endl;
}

void assert_test(bool condition, const char* test_desc) {
    test_count++;
    if (condition) {
        passed_count++;
        std::cout << GREEN << "  ✓ " << test_desc << RESET << std::endl;
    } else {
        std::cout << RED << "  ✗ " << test_desc << RESET << std::endl;
    }
}

// Test 1: fill() behavior - should fill entire capacity and update size
void test_fill() {
    print_test_header("fill() method");
    
    // Test packed_vector
    packed_vector<4> pv(10);  // capacity 10
    assert_test(pv.capacity() == 10, "packed_vector: initial capacity");
    assert_test(pv.size() == 0, "packed_vector: initial size is 0");
    
    pv.fill(7);
    assert_test(pv.size() == 10, "packed_vector: size equals capacity after fill");
    assert_test(pv[0] == 7, "packed_vector: first element is 7");
    assert_test(pv[9] == 7, "packed_vector: last element is 7");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv(10);
    assert_test(bv.capacity() >= 10, "b_vector: initial capacity at least 10 (may be higher due to SBO)");
    assert_test(bv.size() == 0, "b_vector: initial size is 0");
    
    bv.fill(7);
    assert_test(bv.size() == bv.capacity(), "b_vector: size equals capacity after fill");
    assert_test(bv[0] == 7, "b_vector: first element is 7");
    assert_test(bv[bv.size() - 1] == 7, "b_vector: last element is 7");
}

// Test 2: operator[] behavior - safe access with bounds checking
void test_operator_brackets() {
    print_test_header("operator[] safe access");
    
    // Test packed_vector
    packed_vector<4> pv;
    assert_test(pv[0] == 0, "packed_vector: empty vector returns 0");
    assert_test(pv[100] == 0, "packed_vector: out of bounds on empty returns 0");
    
    pv.push_back(5);
    pv.push_back(10);
    pv.push_back(15);
    
    assert_test(pv[0] == 5, "packed_vector: valid index 0");
    assert_test(pv[1] == 10, "packed_vector: valid index 1");
    assert_test(pv[2] == 15, "packed_vector: valid index 2");
    assert_test(pv[100] == 15, "packed_vector: out of bounds returns last element");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    assert_test(bv[0] == 0, "b_vector: empty vector returns default");
    assert_test(bv[100] == 0, "b_vector: out of bounds on empty returns default");
    
    bv.push_back(5);
    bv.push_back(10);
    bv.push_back(15);
    
    assert_test(bv[0] == 5, "b_vector: valid index 0");
    assert_test(bv[1] == 10, "b_vector: valid index 1");
    assert_test(bv[2] == 15, "b_vector: valid index 2");
    assert_test(bv[100] == 15, "b_vector: out of bounds returns last element");
}

// Test 3: reserve() behavior
void test_reserve() {
    print_test_header("reserve() method");
    
    // Test packed_vector
    packed_vector<4> pv;
    pv.push_back(1);
    pv.push_back(2);
    
    assert_test(pv.size() == 2, "packed_vector: size is 2");
    
    pv.reserve(20);
    assert_test(pv.capacity() >= 20, "packed_vector: capacity increased to at least 20");
    assert_test(pv.size() == 2, "packed_vector: size unchanged after reserve");
    assert_test(pv[0] == 1, "packed_vector: data preserved after reserve");
    assert_test(pv[1] == 2, "packed_vector: data preserved after reserve");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    bv.push_back(1);
    bv.push_back(2);
    
    assert_test(bv.size() == 2, "b_vector: size is 2");
    
    bv.reserve(20);
    assert_test(bv.capacity() >= 20, "b_vector: capacity increased to at least 20");
    assert_test(bv.size() == 2, "b_vector: size unchanged after reserve");
    assert_test(bv[0] == 1, "b_vector: data preserved after reserve");
    assert_test(bv[1] == 2, "b_vector: data preserved after reserve");
}

// Test 4: resize() behavior
void test_resize() {
    print_test_header("resize() method");
    
    // Test packed_vector - resize larger with value
    packed_vector<4> pv;
    pv.push_back(1);
    pv.push_back(2);
    
    pv.resize(5, 9);
    assert_test(pv.size() == 5, "packed_vector: size is 5 after resize");
    assert_test(pv[0] == 1, "packed_vector: original data preserved");
    assert_test(pv[1] == 2, "packed_vector: original data preserved");
    assert_test(pv[2] == 9, "packed_vector: new elements initialized to 9");
    assert_test(pv[4] == 9, "packed_vector: last new element is 9");
    
    // Resize smaller
    pv.resize(2);
    assert_test(pv.size() == 2, "packed_vector: size is 2 after shrink");
    assert_test(pv[0] == 1, "packed_vector: data preserved after shrink");
    
    // Test b_vector - resize larger with value
    b_vector<uint8_t, 0> bv;
    bv.push_back(1);
    bv.push_back(2);
    
    bv.resize(5, 9);
    assert_test(bv.size() == 5, "b_vector: size is 5 after resize");
    assert_test(bv[0] == 1, "b_vector: original data preserved");
    assert_test(bv[1] == 2, "b_vector: original data preserved");
    assert_test(bv[2] == 9, "b_vector: new elements initialized to 9");
    assert_test(bv[4] == 9, "b_vector: last new element is 9");
    
    // Resize smaller
    bv.resize(2);
    assert_test(bv.size() == 2, "b_vector: size is 2 after shrink");
    assert_test(bv[0] == 1, "b_vector: data preserved after shrink");
}

// Test 5: push_back and capacity growth
void test_push_back_growth() {
    print_test_header("push_back() and capacity growth");
    
    // Test packed_vector
    packed_vector<4> pv;
    for (int i = 0; i < 20; i++) {
        pv.push_back(i % 15);  // Keep within 4-bit range
    }
    
    assert_test(pv.size() == 20, "packed_vector: size is 20 after 20 push_backs");
    assert_test(pv.capacity() >= 20, "packed_vector: capacity grew appropriately");
    
    // Verify all values
    bool all_correct = true;
    for (int i = 0; i < 20; i++) {
        if (pv[i] != (i % 15)) all_correct = false;
    }
    assert_test(all_correct, "packed_vector: all 20 values correct");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    for (int i = 0; i < 20; i++) {
        bv.push_back(i % 15);
    }
    
    assert_test(bv.size() == 20, "b_vector: size is 20 after 20 push_backs");
    assert_test(bv.capacity() >= 20, "b_vector: capacity grew appropriately");
    
    // Verify all values
    all_correct = true;
    for (int i = 0; i < 20; i++) {
        if (bv[i] != (i % 15)) all_correct = false;
    }
    assert_test(all_correct, "b_vector: all 20 values correct");
}

// Test 6: empty() and clear()
void test_empty_clear() {
    print_test_header("empty() and clear() methods");
    
    // Test packed_vector
    packed_vector<4> pv;
    assert_test(pv.empty(), "packed_vector: empty on construction");
    
    pv.push_back(5);
    assert_test(!pv.empty(), "packed_vector: not empty after push_back");
    
    pv.clear();
    assert_test(pv.empty(), "packed_vector: empty after clear");
    assert_test(pv.capacity() > 0, "packed_vector: capacity preserved after clear");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    assert_test(bv.empty(), "b_vector: empty on construction");
    
    bv.push_back(5);
    assert_test(!bv.empty(), "b_vector: not empty after push_back");
    
    bv.clear();
    assert_test(bv.empty(), "b_vector: empty after clear");
    assert_test(bv.capacity() > 0, "b_vector: capacity preserved after clear");
}

// Test 7: front() and back()
void test_front_back() {
    print_test_header("front() and back() methods");
    
    // Test packed_vector
    packed_vector<4> pv;
    pv.push_back(3);
    pv.push_back(7);
    pv.push_back(11);
    
    assert_test(pv.front() == 3, "packed_vector: front() returns first element");
    assert_test(pv.back() == 11, "packed_vector: back() returns last element");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    bv.push_back(3);
    bv.push_back(7);
    bv.push_back(11);
    
    assert_test(bv.front() == 3, "b_vector: front() returns first element");
    assert_test(bv.back() == 11, "b_vector: back() returns last element");
}

// Test 8: pop_back()
void test_pop_back() {
    print_test_header("pop_back() method");
    
    // Test packed_vector
    packed_vector<4> pv;
    pv.push_back(1);
    pv.push_back(2);
    pv.push_back(3);
    
    assert_test(pv.size() == 3, "packed_vector: size is 3");
    pv.pop_back();
    assert_test(pv.size() == 2, "packed_vector: size is 2 after pop_back");
    assert_test(pv.back() == 2, "packed_vector: back() is now 2");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    bv.push_back(1);
    bv.push_back(2);
    bv.push_back(3);
    
    assert_test(bv.size() == 3, "b_vector: size is 3");
    bv.pop_back();
    assert_test(bv.size() == 2, "b_vector: size is 2 after pop_back");
    assert_test(bv.back() == 2, "b_vector: back() is now 2");
}

// Test 9: fit() method
void test_fit() {
    print_test_header("fit() method");
    
    // Test packed_vector
    packed_vector<4> pv;
    pv.reserve(50);
    pv.push_back(1);
    pv.push_back(2);
    
    assert_test(pv.capacity() >= 50, "packed_vector: capacity is at least 50");
    assert_test(pv.size() == 2, "packed_vector: size is 2");
    
    pv.fit();
    assert_test(pv.capacity() == 2, "packed_vector: capacity shrunk to size after fit");
    assert_test(pv.size() == 2, "packed_vector: size unchanged after fit");
    assert_test(pv[0] == 1 && pv[1] == 2, "packed_vector: data preserved after fit");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    bv.reserve(50);
    bv.push_back(1);
    bv.push_back(2);
    
    assert_test(bv.capacity() >= 50, "b_vector: capacity is at least 50");
    assert_test(bv.size() == 2, "b_vector: size is 2");
    
    bv.fit();
    assert_test(bv.capacity() == 2, "b_vector: capacity shrunk to size after fit");
    assert_test(bv.size() == 2, "b_vector: size unchanged after fit");
    assert_test(bv[0] == 1 && bv[1] == 2, "b_vector: data preserved after fit");
}

// Test 10: Iterator compatibility
void test_iterators() {
    print_test_header("Iterator functionality");
    
    // Test packed_vector
    packed_vector<4> pv;
    pv.push_back(2);
    pv.push_back(4);
    pv.push_back(6);
    
    int sum_pv = 0;
    for (auto val : pv) {
        sum_pv += val;
    }
    assert_test(sum_pv == 12, "packed_vector: iterator sum is correct");
    
    // Test b_vector
    b_vector<uint8_t, 0> bv;
    bv.push_back(2);
    bv.push_back(4);
    bv.push_back(6);
    
    int sum_bv = 0;
    for (auto val : bv) {
        sum_bv += val;
    }
    assert_test(sum_bv == 12, "b_vector: iterator sum is correct");
}

// Test 11: Value clamping in packed_vector
void test_value_clamping() {
    print_test_header("Value clamping (packed_vector specific)");
    
    packed_vector<4> pv;  // 4 bits, max value = 15
    
    pv.push_back(20);  // Should be clamped to 15
    assert_test(pv[0] <= 15, "packed_vector: value clamped to max_value");
    
    pv.fill(255);  // Should be clamped to 15
    assert_test(pv[0] <= 15, "packed_vector: fill() clamps to max_value");
}

int main() {
    std::cout << BOLD << "\n========================================" << std::endl;
    std::cout << "  packed_vector vs b_vector Parity Test" << std::endl;
    std::cout << "========================================\n" << RESET << std::endl;
    
    test_fill();
    test_operator_brackets();
    test_reserve();
    test_resize();
    test_push_back_growth();
    test_empty_clear();
    test_front_back();
    test_pop_back();
    test_fit();
    test_iterators();
    test_value_clamping();
    
    std::cout << "\n" << BOLD << "========================================" << std::endl;
    std::cout << "Results: " << GREEN << passed_count << "/" << test_count << " tests passed" << RESET << std::endl;
    std::cout << BOLD << "========================================\n" << RESET << std::endl;
    
    if (passed_count == test_count) {
        std::cout << GREEN << BOLD << "✓ All tests passed!" << RESET << std::endl;
        return 0;
    } else {
        std::cout << RED << BOLD << "✗ Some tests failed" << RESET << std::endl;
        return 1;
    }
}
