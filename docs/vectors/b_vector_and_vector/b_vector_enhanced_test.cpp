#include "b_vector.cpp"
#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <vector>
#include <random>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <complex>

using namespace mcu;

// Test results tracking
struct TestResults {
    int passed = 0;
    int failed = 0;
    
    void pass(const std::string& test_name) {
        std::cout << "[PASS] " << test_name << std::endl;
        passed++;
    }
    
    void fail(const std::string& test_name, const std::string& error) {
        std::cout << "[FAIL] " << test_name << " - " << error << std::endl;
        failed++;
    }
    
    void summary() {
        std::cout << "\n=== TEST SUMMARY ===" << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Total:  " << (passed + failed) << std::endl;
        if (passed + failed > 0) {
            std::cout << "Success Rate: " << (100.0 * passed / (passed + failed)) << "%" << std::endl;
        }
    }
};

TestResults results;

// Enhanced utility macros
#define ASSERT_EQ(expected, actual, test_name) \
    do { \
        auto exp_val = (expected); \
        auto act_val = (actual); \
        if (exp_val == act_val) { \
            results.pass(test_name); \
        } else { \
            std::ostringstream oss; \
            oss << "Expected: " << exp_val << ", Got: " << act_val; \
            results.fail(test_name, oss.str()); \
        } \
    } while(0)

#define ASSERT_TRUE(condition, test_name) \
    do { \
        if (condition) { \
            results.pass(test_name); \
        } else { \
            results.fail(test_name, "Condition was false"); \
        } \
    } while(0)

#define ASSERT_FALSE(condition, test_name) \
    do { \
        if (!(condition)) { \
            results.pass(test_name); \
        } else { \
            results.fail(test_name, "Condition was true"); \
        } \
    } while(0)

#define ASSERT_THROWS(code, test_name) \
    do { \
        bool threw = false; \
        try { \
            code; \
        } catch (...) { \
            threw = true; \
        } \
        if (threw) { \
            results.pass(test_name); \
        } else { \
            results.fail(test_name, "Expected exception but none was thrown"); \
        } \
    } while(0)

// Test structures for different sizes
struct SmallStruct {
    char c;
    bool operator==(const SmallStruct& other) const { return c == other.c; }
    bool operator<(const SmallStruct& other) const { return c < other.c; }
};

struct MediumStruct {
    int a;
    short b;
    char c;
    bool operator==(const MediumStruct& other) const { 
        return a == other.a && b == other.b && c == other.c; 
    }
    bool operator<(const MediumStruct& other) const { return a < other.a; }
};

struct LargeStruct {
    double values[4];
    int id;
    char name[16];
    
    LargeStruct() : id(0) { 
        std::fill(std::begin(values), std::end(values), 0.0);
        std::memset(name, 0, sizeof(name));
    }
    
    LargeStruct(int i, double v, const char* n) : id(i) {
        std::fill(std::begin(values), std::end(values), v);
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }
    
    bool operator==(const LargeStruct& other) const { 
        return id == other.id && 
               std::equal(std::begin(values), std::end(values), std::begin(other.values)) &&
               std::strcmp(name, other.name) == 0;
    }
    bool operator<(const LargeStruct& other) const { return id < other.id; }
};

// Test SBO size calculation for different types
void test_sbo_size_calculation() {
    std::cout << "\n=== SBO SIZE CALCULATION TESTS ===" << std::endl;
    
    // Test 1-byte types
    {
        b_vector<char> v_char;
        b_vector<uint8_t> v_uint8;
        b_vector<int8_t> v_int8;
        b_vector<bool> v_bool;
        
        std::cout << "sizeof(char): " << sizeof(char) << " -> capacity: " << v_char.capacity() << std::endl;
        std::cout << "sizeof(uint8_t): " << sizeof(uint8_t) << " -> capacity: " << v_uint8.capacity() << std::endl;
        std::cout << "sizeof(int8_t): " << sizeof(int8_t) << " -> capacity: " << v_int8.capacity() << std::endl;
        std::cout << "sizeof(bool): " << sizeof(bool) << " -> capacity: " << v_bool.capacity() << std::endl;
        
        // For 1-byte types, expect 32 elements
        ASSERT_EQ(32, v_char.capacity(), "char SBO capacity");
        ASSERT_EQ(32, v_uint8.capacity(), "uint8_t SBO capacity");
        ASSERT_EQ(32, v_int8.capacity(), "int8_t SBO capacity");
        ASSERT_EQ(32, v_bool.capacity(), "bool SBO capacity");
    }
    
    // Test 2-byte types
    {
        b_vector<short> v_short;
        b_vector<uint16_t> v_uint16;
        b_vector<int16_t> v_int16;
        
        std::cout << "sizeof(short): " << sizeof(short) << " -> capacity: " << v_short.capacity() << std::endl;
        std::cout << "sizeof(uint16_t): " << sizeof(uint16_t) << " -> capacity: " << v_uint16.capacity() << std::endl;
        std::cout << "sizeof(int16_t): " << sizeof(int16_t) << " -> capacity: " << v_int16.capacity() << std::endl;
        
        // For 2-byte types, expect 16 elements
        ASSERT_EQ(16, v_short.capacity(), "short SBO capacity");
        ASSERT_EQ(16, v_uint16.capacity(), "uint16_t SBO capacity");
        ASSERT_EQ(16, v_int16.capacity(), "int16_t SBO capacity");
    }
    
    // Test 4-byte types
    {
        b_vector<int> v_int;
        b_vector<float> v_float;
        b_vector<uint32_t> v_uint32;
        
        std::cout << "sizeof(int): " << sizeof(int) << " -> capacity: " << v_int.capacity() << std::endl;
        std::cout << "sizeof(float): " << sizeof(float) << " -> capacity: " << v_float.capacity() << std::endl;
        std::cout << "sizeof(uint32_t): " << sizeof(uint32_t) << " -> capacity: " << v_uint32.capacity() << std::endl;
        
        // For 4-byte types, expect 8 elements
        ASSERT_EQ(8, v_int.capacity(), "int SBO capacity");
        ASSERT_EQ(8, v_float.capacity(), "float SBO capacity");
        ASSERT_EQ(8, v_uint32.capacity(), "uint32_t SBO capacity");
    }
    
    // Test 8-byte types
    {
        b_vector<double> v_double;
        b_vector<uint64_t> v_uint64;
        b_vector<long long> v_longlong;
        
        std::cout << "sizeof(double): " << sizeof(double) << " -> capacity: " << v_double.capacity() << std::endl;
        std::cout << "sizeof(uint64_t): " << sizeof(uint64_t) << " -> capacity: " << v_uint64.capacity() << std::endl;
        std::cout << "sizeof(long long): " << sizeof(long long) << " -> capacity: " << v_longlong.capacity() << std::endl;
        
        // For 8-byte types, expect 4 elements
        ASSERT_EQ(4, v_double.capacity(), "double SBO capacity");
        ASSERT_EQ(4, v_uint64.capacity(), "uint64_t SBO capacity");
        ASSERT_EQ(4, v_longlong.capacity(), "long long SBO capacity");
    }
    
    // Test custom struct types
    {
        b_vector<SmallStruct> v_small;
        b_vector<MediumStruct> v_medium;
        b_vector<LargeStruct> v_large;
        
        std::cout << "sizeof(SmallStruct): " << sizeof(SmallStruct) << " -> capacity: " << v_small.capacity() << std::endl;
        std::cout << "sizeof(MediumStruct): " << sizeof(MediumStruct) << " -> capacity: " << v_medium.capacity() << std::endl;
        std::cout << "sizeof(LargeStruct): " << sizeof(LargeStruct) << " -> capacity: " << v_large.capacity() << std::endl;
        
        // SmallStruct (1 byte) should get 32 elements
        ASSERT_EQ(32, v_small.capacity(), "SmallStruct SBO capacity");
        
        // MediumStruct (likely 8 bytes due to alignment) should get 4 elements
        if (sizeof(MediumStruct) <= 8) {
            ASSERT_EQ(4, v_medium.capacity(), "MediumStruct SBO capacity");
        } else {
            ASSERT_TRUE(v_medium.capacity() >= 1, "MediumStruct SBO capacity >= 1");
        }
        
        // LargeStruct (likely > 16 bytes) should get 1 element
        ASSERT_EQ(1, v_large.capacity(), "LargeStruct SBO capacity");
    }
    
    // Test explicit SBO size override
    {
        b_vector<int, 10> v_custom;
        std::cout << "sizeof(int) with explicit SBO=10: " << sizeof(int) << " -> capacity: " << v_custom.capacity() << std::endl;
        ASSERT_EQ(10, v_custom.capacity(), "explicit SBO size override");
    }
}

// Test different data types with comprehensive operations
void test_different_data_types() {
    std::cout << "\n=== DIFFERENT DATA TYPES TESTS ===" << std::endl;
    
    // Test with char
    {
        b_vector<char> v;
        const char test_str[] = "Hello, World!";
        for (char c : test_str) {
            if (c != '\0') v.push_back(c);
        }
        
        ASSERT_EQ(13, v.size(), "char vector size");
        ASSERT_EQ('H', v[0], "char vector first element");
        ASSERT_EQ('!', v.back(), "char vector last element");
        
        // Test sorting
        v.sort();
        ASSERT_EQ(' ', v[0], "char vector sorted first"); // space character
        ASSERT_EQ('r', v[v.size()-1], "char vector sorted last");
    }
    
    // Test with float
    {
        b_vector<float> v;
        float values[] = {3.14f, 2.71f, 1.41f, 1.73f, 0.57f};
        
        for (float f : values) {
            v.push_back(f);
        }
        
        ASSERT_EQ(5, v.size(), "float vector size");
        ASSERT_TRUE(std::abs(v[0] - 3.14f) < 0.001f, "float vector first element");
        ASSERT_TRUE(std::abs(v.back() - 0.57f) < 0.001f, "float vector last element");
        
        // Test resize with value
        v.resize(8, 9.99f);
        ASSERT_EQ(8, v.size(), "float vector resized size");
        ASSERT_TRUE(std::abs(v[7] - 9.99f) < 0.001f, "float vector resize value");
    }
    
    // Test with double
    {
        b_vector<double> v;
        double pi = 3.141592653589793;
        double e = 2.718281828459045;
        
        v.push_back(pi);
        v.push_back(e);
        
        ASSERT_EQ(2, v.size(), "double vector size");
        ASSERT_TRUE(std::abs(v[0] - pi) < 1e-10, "double vector pi value");
        ASSERT_TRUE(std::abs(v[1] - e) < 1e-10, "double vector e value");
        
        // Test operations that might trigger heap allocation
        for (int i = 0; i < 10; ++i) {
            v.push_back(i * 1.5);
        }
        ASSERT_EQ(12, v.size(), "double vector after heap transition");
        ASSERT_TRUE(std::abs(v[0] - pi) < 1e-10, "double vector pi preserved after heap");
    }
    
    // Test with uint64_t
    {
        b_vector<uint64_t> v;
        uint64_t large_vals[] = {
            0xFFFFFFFFFFFFFFFFULL,
            0x8000000000000000ULL,
            0x0000000000000001ULL,
            0x123456789ABCDEFULL
        };
        
        for (uint64_t val : large_vals) {
            v.push_back(val);
        }
        
        ASSERT_EQ(4, v.size(), "uint64_t vector size");
        ASSERT_EQ(0xFFFFFFFFFFFFFFFFULL, v[0], "uint64_t max value");
        ASSERT_EQ(0x123456789ABCDEFULL, v.back(), "uint64_t pattern value");
        
        // Test capacity - should be exactly 4 for 8-byte types
        ASSERT_EQ(4, v.capacity(), "uint64_t SBO capacity exactly full");
        
        // Adding one more should trigger heap allocation
        v.push_back(0x42ULL);
        ASSERT_EQ(5, v.size(), "uint64_t after heap transition size");
        ASSERT_TRUE(v.capacity() > 4, "uint64_t heap capacity");
    }
    
    // Test with custom small struct
    {
        b_vector<SmallStruct> v;
        for (char c = 'A'; c <= 'Z'; ++c) {
            SmallStruct s;
            s.c = c;
            v.push_back(s);
        }
        
        ASSERT_EQ(26, v.size(), "SmallStruct vector size");
        ASSERT_EQ('A', v[0].c, "SmallStruct first element");
        ASSERT_EQ('Z', v.back().c, "SmallStruct last element");
        
        // Should still be in SBO since capacity is 32
        ASSERT_EQ(32, v.capacity(), "SmallStruct still in SBO");
        
        // Test sorting
        std::reverse(v.begin(), v.end()); // Reverse to make it Z...A
        v.sort();
        ASSERT_EQ('A', v[0].c, "SmallStruct sorted first");
        ASSERT_EQ('Z', v[25].c, "SmallStruct sorted last");
    }
    
    // Test with large struct
    {
        b_vector<LargeStruct> v;
        LargeStruct ls1(1, 1.1, "first");
        LargeStruct ls2(2, 2.2, "second");
        
        v.push_back(ls1);
        ASSERT_EQ(1, v.size(), "LargeStruct vector size 1");
        ASSERT_EQ(1, v.capacity(), "LargeStruct SBO capacity");
        
        // Adding second element should trigger heap allocation
        v.push_back(ls2);
        ASSERT_EQ(2, v.size(), "LargeStruct vector size 2");
        ASSERT_TRUE(v.capacity() >= 2, "LargeStruct heap capacity");
        
        // Verify data integrity
        ASSERT_EQ(1, v[0].id, "LargeStruct first id");
        ASSERT_EQ(2, v[1].id, "LargeStruct second id");
        ASSERT_TRUE(std::strcmp(v[0].name, "first") == 0, "LargeStruct first name");
        ASSERT_TRUE(std::strcmp(v[1].name, "second") == 0, "LargeStruct second name");
    }
}

// Edge cases and boundary conditions
void test_edge_cases_and_boundaries() {
    std::cout << "\n=== EDGE CASES AND BOUNDARIES TESTS ===" << std::endl;
    
    // Test exactly filling SBO capacity
    {
        b_vector<int> v; // capacity = 8 for int
        for (int i = 0; i < 8; ++i) {
            v.push_back(i);
        }
        
        ASSERT_EQ(8, v.size(), "exactly fill SBO - size");
        ASSERT_EQ(8, v.capacity(), "exactly fill SBO - capacity");
        
        // Should still be using SBO
        // Add one more to trigger heap
        v.push_back(8);
        ASSERT_EQ(9, v.size(), "overflow SBO - size");
        ASSERT_TRUE(v.capacity() > 8, "overflow SBO - capacity increased");
        
        // Verify all elements are preserved
        for (int i = 0; i < 9; ++i) {
            ASSERT_EQ(i, v[i], "overflow SBO - element " + std::to_string(i) + " preserved");
        }
    }
    
    // Test with zero-sized vector operations
    {
        b_vector<int> v;
        
        // Test operations on empty vector
        ASSERT_TRUE(v.empty(), "empty vector - empty()");
        ASSERT_EQ(0, v.size(), "empty vector - size()");
        
        // Test bounds-safe access on empty vector
        int safe_val = v[0]; // Should return safe value
        ASSERT_EQ(0, safe_val, "empty vector safe access");
        
        // Test resize from empty
        v.resize(5, 42);
        ASSERT_EQ(5, v.size(), "resize from empty - size");
        ASSERT_EQ(42, v[0], "resize from empty - value");
        ASSERT_EQ(42, v[4], "resize from empty - last value");
        
        // Test clear
        v.clear();
        ASSERT_TRUE(v.empty(), "cleared vector - empty");
    }
    
    // Test rapid size changes
    {
        b_vector<short> v; // capacity = 16 for short
        
        // Grow to SBO limit
        for (int i = 0; i < 16; ++i) {
            v.push_back(static_cast<short>(i));
        }
        ASSERT_EQ(16, v.capacity(), "rapid growth - at SBO limit");
        
        // Grow beyond SBO
        for (int i = 16; i < 32; ++i) {
            v.push_back(static_cast<short>(i));
        }
        ASSERT_TRUE(v.capacity() >= 32, "rapid growth - beyond SBO");
        
        // Shrink back
        v.resize(8);
        ASSERT_EQ(8, v.size(), "rapid shrink - size");
        for (int i = 0; i < 8; ++i) {
            ASSERT_EQ(i, v[i], "rapid shrink - element " + std::to_string(i));
        }
    }
    
    // Test copy/move with different storage states
    {
        // Test copy from SBO to SBO
        b_vector<int> sbo_source;
        sbo_source.push_back(1);
        sbo_source.push_back(2);
        
        b_vector<int> sbo_copy(sbo_source);
        ASSERT_EQ(sbo_source.size(), sbo_copy.size(), "SBO to SBO copy - size");
        ASSERT_EQ(1, sbo_copy[0], "SBO to SBO copy - element 0");
        ASSERT_EQ(2, sbo_copy[1], "SBO to SBO copy - element 1");
        
        // Test copy from heap to SBO
        b_vector<int> heap_source(20, 99);
        b_vector<int> heap_copy(heap_source);
        ASSERT_EQ(20, heap_copy.size(), "heap to SBO copy - size");
        ASSERT_EQ(99, heap_copy[0], "heap to SBO copy - first element");
        ASSERT_EQ(99, heap_copy[19], "heap to SBO copy - last element");
        
        // Test move from heap
        size_t original_capacity = heap_source.capacity();
        b_vector<int> heap_moved(std::move(heap_source));
        ASSERT_EQ(20, heap_moved.size(), "heap move - size");
        ASSERT_EQ(0, heap_source.size(), "heap move - source size");
        ASSERT_EQ(99, heap_moved[0], "heap move - element preserved");
    }
    
    // Test extreme values
    {
        b_vector<int> v;
        
        // Test with max/min int values
        v.push_back(std::numeric_limits<int>::max());
        v.push_back(std::numeric_limits<int>::min());
        v.push_back(0);
        v.push_back(-1);
        v.push_back(1);
        
        ASSERT_EQ(std::numeric_limits<int>::max(), v[0], "extreme values - max int");
        ASSERT_EQ(std::numeric_limits<int>::min(), v[1], "extreme values - min int");
        ASSERT_EQ(0, v[2], "extreme values - zero");
        ASSERT_EQ(-1, v[3], "extreme values - negative one");
        ASSERT_EQ(1, v[4], "extreme values - positive one");
        
        // Test sorting with extreme values
        v.sort();
        ASSERT_EQ(std::numeric_limits<int>::min(), v[0], "extreme values sorted - min first");
        ASSERT_EQ(std::numeric_limits<int>::max(), v[4], "extreme values sorted - max last");
    }
    
    // Test memory allocation patterns
    {
        b_vector<double> v; // capacity = 4 for double
        
        size_t initial_memory = v.memory_usage();
        ASSERT_TRUE(initial_memory > 0, "initial memory usage");
        
        // Fill SBO
        for (int i = 0; i < 4; ++i) {
            v.push_back(i * 1.5);
        }
        size_t sbo_memory = v.memory_usage();
        ASSERT_TRUE(sbo_memory >= initial_memory, "SBO memory usage");
        
        // Trigger heap allocation
        v.push_back(4 * 1.5);
        size_t heap_memory = v.memory_usage();
        ASSERT_TRUE(heap_memory > sbo_memory, "heap memory usage increased");
        
        std::cout << "Memory usage: initial=" << initial_memory 
                  << ", SBO=" << sbo_memory 
                  << ", heap=" << heap_memory << " bytes" << std::endl;
    }
}

// Stress test with different data types
void stress_test_different_types() {
    std::cout << "\n=== STRESS TESTS WITH DIFFERENT TYPES ===" << std::endl;
    
    // Stress test with char (high SBO capacity)
    {
        b_vector<char> v;
        const int iterations = 1000;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> char_dis('A', 'Z');
        
        for (int i = 0; i < iterations; ++i) {
            char c = static_cast<char>(char_dis(gen));
            v.push_back(c);
            
            if (i % 100 == 0) {
                v.sort();
            }
            
            if (i % 200 == 0 && !v.empty()) {
                v.pop_back();
            }
        }
        
        ASSERT_TRUE(v.size() > 0, "char stress test - non-empty result");
        results.pass("char stress test completed");
    }
    
    // Stress test with double (low SBO capacity)
    {
        b_vector<double> v;
        const int iterations = 500;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> real_dis(0.0, 100.0);
        
        for (int i = 0; i < iterations; ++i) {
            double val = real_dis(gen);
            v.push_back(val);
            
            if (i % 50 == 0) {
                v.resize(v.size() / 2);
            }
            
            if (i % 75 == 0) {
                v.clear();
            }
        }
        
        results.pass("double stress test completed");
    }
    
    // Stress test with LargeStruct (minimal SBO capacity)
    {
        b_vector<LargeStruct> v;
        const int iterations = 100;
        
        for (int i = 0; i < iterations; ++i) {
            LargeStruct ls(i, i * 0.5, ("item" + std::to_string(i)).c_str());
            v.push_back(ls);
            
            if (i % 20 == 0 && v.size() > 5) {
                v.erase(v.size() / 2);
            }
            
            if (i % 30 == 0) {
                LargeStruct insert_ls(i + 1000, i * 1.5, "inserted");
                if (!v.empty()) {
                    v.insert(0, insert_ls);
                }
            }
        }
        
        ASSERT_TRUE(v.size() > 0, "LargeStruct stress test - non-empty result");
        
        // Verify some data integrity
        bool found_original = false;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i].id < 1000) {
                found_original = true;
                break;
            }
        }
        ASSERT_TRUE(found_original, "LargeStruct stress test - original data preserved");
        
        results.pass("LargeStruct stress test completed");
    }
}

// Performance comparison between different types
void performance_comparison() {
    std::cout << "\n=== PERFORMANCE COMPARISON ===" << std::endl;
    
    const size_t num_elements = 10000;
    
    // Benchmark char (high SBO capacity)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        b_vector<char> v;
        for (size_t i = 0; i < num_elements; ++i) {
            v.push_back(static_cast<char>('A' + (i % 26)));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "char (" << v.capacity() << " SBO): " << duration.count() << " μs for " << num_elements << " elements" << std::endl;
    }
    
    // Benchmark int (medium SBO capacity)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        b_vector<int> v;
        for (size_t i = 0; i < num_elements; ++i) {
            v.push_back(static_cast<int>(i));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "int (" << v.capacity() << " SBO): " << duration.count() << " μs for " << num_elements << " elements" << std::endl;
    }
    
    // Benchmark double (low SBO capacity)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        b_vector<double> v;
        for (size_t i = 0; i < num_elements; ++i) {
            v.push_back(static_cast<double>(i) * 0.1);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "double (" << v.capacity() << " SBO): " << duration.count() << " μs for " << num_elements << " elements" << std::endl;
    }
    
    // Benchmark LargeStruct (minimal SBO capacity)
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        b_vector<LargeStruct> v;
        for (size_t i = 0; i < 1000; ++i) { // Fewer elements for large structs
            LargeStruct ls(static_cast<int>(i), i * 0.1, "test");
            v.push_back(ls);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "LargeStruct (" << v.capacity() << " SBO): " << duration.count() << " μs for 1000 elements" << std::endl;
    }
}

// Test type safety and template specialization
void test_type_safety() {
    std::cout << "\n=== TYPE SAFETY TESTS ===" << std::endl;
    
    // Test that different instantiations are separate types
    {
        b_vector<int> v_int;
        b_vector<float> v_float;
        b_vector<char> v_char;
        
        // Each should have different capacities based on type size
        ASSERT_TRUE(v_char.capacity() > v_int.capacity(), "char capacity > int capacity");
        ASSERT_TRUE(v_int.capacity() >= v_float.capacity(), "int capacity >= float capacity");
        
        std::cout << "Type-based capacities: char=" << v_char.capacity() 
                  << ", int=" << v_int.capacity() 
                  << ", float=" << v_float.capacity() << std::endl;
    }
    
    // Test explicit template parameter override
    {
        b_vector<int, 16> v_custom;
        b_vector<int> v_auto;
        
        ASSERT_EQ(16, v_custom.capacity(), "explicit template parameter");
        ASSERT_EQ(8, v_auto.capacity(), "auto-calculated capacity");
        ASSERT_TRUE(v_custom.capacity() != v_auto.capacity(), "explicit override works");
    }
    
    // Test with pointer types
    {
        b_vector<int*> v_ptr;
        ASSERT_TRUE(v_ptr.capacity() > 0, "pointer type capacity");
        
        int values[] = {1, 2, 3, 4, 5};
        for (int i = 0; i < 5; ++i) {
            v_ptr.push_back(&values[i]);
        }
        
        ASSERT_EQ(5, v_ptr.size(), "pointer vector size");
        ASSERT_EQ(1, *(v_ptr[0]), "pointer vector first dereferenced value");
        ASSERT_EQ(5, *(v_ptr[4]), "pointer vector last dereferenced value");
    }
}

int main() {
    std::cout << "=== B_VECTOR ENHANCED TEST SUITE ===" << std::endl;
    std::cout << "Testing adaptive SBO sizes based on type size\n" << std::endl;
    
    try {
        test_sbo_size_calculation();
        test_different_data_types();
        test_edge_cases_and_boundaries();
        test_type_safety();
        stress_test_different_types();
        performance_comparison();
        
        results.summary();
        
        if (results.failed == 0) {
            std::cout << "\n🎉 ALL TESTS PASSED! Enhanced b_vector implementation is working correctly." << std::endl;
            return 0;
        } else {
            std::cout << "\n❌ Some tests failed. Please review the implementation." << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cout << "\n💥 Exception caught: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "\n💥 Unknown exception caught!" << std::endl;
        return 1;
    }
}
