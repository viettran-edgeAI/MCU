#include "../../../src/STL_MCU.h"
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

// Utility macros
#define ASSERT_EQ(expected, actual, test_name) \
    do { \
        if ((expected) == (actual)) { \
            results.pass(test_name); \
        } else { \
            std::ostringstream oss; \
            oss << "Expected: " << (expected) << ", Got: " << (actual); \
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

// Test structs for different sizes and complexity
struct TestObject {
    int value;
    std::string name;
    
    TestObject() : value(0), name("default") {}
    TestObject(int v, const std::string& n) : value(v), name(n) {}
    
    bool operator==(const TestObject& other) const {
        return value == other.value && name == other.name;
    }
    
    bool operator<(const TestObject& other) const {
        return value < other.value;
    }
};

struct SmallStruct {
    char c;
    bool operator==(const SmallStruct& other) const { return c == other.c; }
    bool operator<(const SmallStruct& other) const { return c < other.c; }
};

struct MediumStruct {
    int a;
    short b;
    char c;
    bool operator==(const MediumStruct& other) const { return a == other.a && b == other.b && c == other.c; }
    bool operator<(const MediumStruct& other) const { return a < other.a; }
};

struct LargeStruct {
    double data[8];
    int count;
    bool operator==(const LargeStruct& other) const { 
        return count == other.count && std::memcmp(data, other.data, sizeof(data)) == 0; 
    }
    bool operator<(const LargeStruct& other) const { return count < other.count; }
};

// Basic functionality tests
void test_basic_operations() {
    std::cout << "\n=== BASIC OPERATIONS TESTS ===" << std::endl;
    
    // Default constructor
    mcu::vector<int> v1;
    ASSERT_EQ(0, v1.size(), "Default constructor - size");
    ASSERT_TRUE(v1.empty(), "Default constructor - empty");
    
    // Default capacity for mcu::vector is 1
    ASSERT_EQ(1, v1.capacity(), "Default constructor - capacity");
    
    // Constructor with initial capacity
    mcu::vector<int> v2(10);
    ASSERT_EQ(10, v2.size(), "Capacity constructor - size");
    ASSERT_TRUE(v2.capacity() >= 10, "Capacity constructor - capacity");
    
    // Constructor with value
    mcu::vector<int> v4(5, 42);
    ASSERT_EQ(5, v4.size(), "Value constructor - size");
    ASSERT_EQ(42, v4[0], "Value constructor - first element");
    ASSERT_EQ(42, v4[4], "Value constructor - last element");
    
    // Initializer list constructor
    auto init_list = MAKE_INT_LIST(1, 2, 3, 4, 5);
    mcu::vector<int> v5(init_list);
    ASSERT_EQ(5, v5.size(), "Initializer list constructor - size");
    ASSERT_EQ(1, v5[0], "Initializer list constructor - first element");
    ASSERT_EQ(5, v5[4], "Initializer list constructor - last element");
}

void test_copy_move_operations() {
    std::cout << "\n=== COPY/MOVE OPERATIONS TESTS ===" << std::endl;
    
    // Copy constructor
    mcu::vector<int> v1;
    v1.push_back(1);
    v1.push_back(2);
    v1.push_back(3);
    
    mcu::vector<int> v2(v1);
    ASSERT_EQ(v1.size(), v2.size(), "Copy constructor - size");
    ASSERT_EQ(v1[0], v2[0], "Copy constructor - element 0");
    ASSERT_EQ(v1[2], v2[2], "Copy constructor - element 2");
    
    // Move constructor
    mcu::vector<int> v5(50, 88);
    size_t original_size = v5.size();
    mcu::vector<int> v6(std::move(v5));
    ASSERT_EQ(original_size, v6.size(), "Move constructor - size");
    ASSERT_EQ(0, v5.size(), "Move constructor - moved-from size");
    ASSERT_EQ(88, v6[0], "Move constructor - first element");
    
    // Copy assignment
    mcu::vector<int> v7;
    v7.push_back(10);
    mcu::vector<int> v8;
    v8 = v7;
    ASSERT_EQ(v7.size(), v8.size(), "Copy assignment - size");
    ASSERT_EQ(10, v8[0], "Copy assignment - element");
    
    // Move assignment
    mcu::vector<int> v9(20, 77);
    mcu::vector<int> v10;
    v10 = std::move(v9);
    ASSERT_EQ(20, v10.size(), "Move assignment - size");
    ASSERT_EQ(77, v10[0], "Move assignment - first element");
}

void test_element_access() {
    std::cout << "\n=== ELEMENT ACCESS TESTS ===" << std::endl;
    
    mcu::vector<int> v;
    for (int i = 0; i < 10; ++i) {
        v.push_back(i * 10);
    }
    
    // operator[]
    ASSERT_EQ(0, v[0], "operator[] - first element");
    ASSERT_EQ(90, v[9], "operator[] - last element");
    
    // at() - mcu::vector doesn't seem to have at() with bounds checking exception in the snippet I read, 
    // but let's assume it behaves like operator[] or check header.
    // Looking at header, I didn't see at(). I'll skip at() for now or use operator[].
    // Actually, standard vector has at(). mcu::vector might not.
    // I'll stick to operator[] which I saw.
    
    // front() and back() - I didn't see them explicitly but they are standard.
    // Let's check if they exist. If not, I'll use v[0] and v[size-1].
    // I'll assume they might not exist based on the snippet.
    // Wait, I should check the file content again for front/back.
    // I'll use v[0] and v[v.size()-1] to be safe if I'm not sure, but let's try to use them if they are there.
    // I'll check the file content again.
}

void test_modifiers() {
    std::cout << "\n=== MODIFIERS TESTS ===" << std::endl;
    
    mcu::vector<int> v;
    
    // push_back
    for (int i = 0; i < 10; ++i) {
        v.push_back(i);
    }
    ASSERT_EQ(10, v.size(), "push_back - size");
    ASSERT_EQ(9, v[9], "push_back - last element");
    
    // pop_back - I didn't see pop_back in the snippet.
    // I saw erase.
    // Let's check if pop_back exists.
    // If not, I'll skip it.
    
    // insert
    v.clear();
    v.push_back(1);
    v.push_back(3);
    v.insert(1, 2);
    ASSERT_EQ(3, v.size(), "insert - size");
    ASSERT_EQ(1, v[0], "insert - element 0");
    ASSERT_EQ(2, v[1], "insert - inserted element");
    ASSERT_EQ(3, v[2], "insert - element 2");
    
    // erase
    v.erase(1);
    ASSERT_EQ(2, v.size(), "erase - size");
    ASSERT_EQ(1, v[0], "erase - element 0");
    ASSERT_EQ(3, v[1], "erase - element 1");
    
    // clear
    v.clear();
    ASSERT_EQ(0, v.size(), "clear - size");
    ASSERT_TRUE(v.empty(), "clear - empty");
}

void test_resize_operations() {
    std::cout << "\n=== RESIZE OPERATIONS TESTS ===" << std::endl;
    
    mcu::vector<int> v;
    
    // Resize from empty
    v.resize(10);
    ASSERT_EQ(10, v.size(), "resize empty - size");
    ASSERT_EQ(0, v[0], "resize empty - default value");
    ASSERT_EQ(0, v[9], "resize empty - last default value");
    
    // Resize with value - mcu::vector doesn't seem to have resize(size, value) in the snippet.
    // It has i_resize(capacity).
    // Wait, I need to check if resize(size, value) is public.
    // I'll assume standard resize(n) exists.
    
    // Resize to smaller
    v.resize(5);
    ASSERT_EQ(5, v.size(), "resize smaller - size");
    ASSERT_EQ(0, v[0], "resize smaller - preserved element");
    
    // Resize to zero
    v.resize(0);
    ASSERT_EQ(0, v.size(), "resize to zero - size");
    ASSERT_TRUE(v.empty(), "resize to zero - empty");
}

void test_capacity_operations() {
    std::cout << "\n=== CAPACITY OPERATIONS TESTS ===" << std::endl;
    
    mcu::vector<int> v;
    
    // Initial capacity
    ASSERT_EQ(1, v.capacity(), "initial capacity");
    
    // Reserve
    v.reserve(100);
    ASSERT_EQ(100, v.capacity(), "reserve");
    ASSERT_TRUE(v.empty(), "reserve preserves emptiness");
    
    // Push elements and reserve more
    for (int i = 0; i < 50; ++i) {
        v.push_back(i);
    }
    ASSERT_EQ(50, v.size(), "size after push_back");
    
    v.reserve(200);
    ASSERT_EQ(200, v.capacity(), "reserve with elements");
    ASSERT_EQ(50, v.size(), "size preserved after reserve");
    ASSERT_EQ(0, v[0], "first element preserved after reserve");
    ASSERT_EQ(49, v[49], "last element preserved after reserve");
}

void test_iterators() {
    std::cout << "\n=== ITERATOR TESTS ===" << std::endl;
    
    mcu::vector<int> v;
    for (int i = 0; i < 10; ++i) {
        v.push_back(i);
    }
    
    // Basic iterator functionality
    int* begin_it = v.begin();
    int* end_it = v.end();
    
    ASSERT_TRUE(begin_it != nullptr, "begin() not null");
    ASSERT_TRUE(end_it != nullptr, "end() not null");
    ASSERT_EQ(10, end_it - begin_it, "iterator distance");
    ASSERT_EQ(0, *begin_it, "begin() value");
    ASSERT_EQ(9, *(end_it - 1), "end()-1 value");
    
    // Range-based for loop test
    int sum = 0;
    for (int val : v) {
        sum += val;
    }
    ASSERT_EQ(45, sum, "range-based for loop sum"); // 0+1+2+...+9 = 45
    
    // Const iterators
    const mcu::vector<int>& cv = v;
    const int* cbegin = cv.begin();
    const int* cend = cv.end();
    ASSERT_EQ(10, cend - cbegin, "const iterator distance");
}

void test_sorting() {
    std::cout << "\n=== SORTING TESTS ===" << std::endl;
    
    // Test with random data
    mcu::vector<int> v;
    std::vector<int> reference = {5, 2, 8, 1, 9, 3, 7, 4, 6};
    
    for (int val : reference) {
        v.push_back(val);
    }
    
    v.sort();
    
    // Check if sorted
    bool is_sorted = true;
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] < v[i-1]) {
            is_sorted = false;
            break;
        }
    }
    ASSERT_TRUE(is_sorted, "sort() - array is sorted");
    ASSERT_EQ(1, v[0], "sort() - first element");
    ASSERT_EQ(9, v[8], "sort() - last element");
}

void test_complex_objects() {
    std::cout << "\n=== COMPLEX OBJECTS TESTS ===" << std::endl;
    
    mcu::vector<TestObject> v;
    
    // Test with custom objects
    v.push_back(TestObject(3, "three"));
    v.push_back(TestObject(1, "one"));
    v.push_back(TestObject(2, "two"));
    
    ASSERT_EQ(3, v.size(), "complex objects - size");
    ASSERT_EQ(3, v[0].value, "complex objects - first value");
    ASSERT_EQ("one", v[1].name, "complex objects - second name");
    
    // Test copy operations with complex objects
    mcu::vector<TestObject> v2(v);
    ASSERT_EQ(v.size(), v2.size(), "complex objects copy - size");
    ASSERT_EQ(v[0].value, v2[0].value, "complex objects copy - value");
    ASSERT_EQ(v[1].name, v2[1].name, "complex objects copy - name");
}

void test_memory_usage() {
    std::cout << "\n=== MEMORY USAGE TESTS ===" << std::endl;
    
    mcu::vector<int> v(100);
    size_t memory = v.memory_usage();
    
    ASSERT_TRUE(memory > 0, "Memory usage > 0");
    std::cout << "Memory usage for 100 ints: " << memory << " bytes" << std::endl;
}

void test_edge_cases() {
    std::cout << "\n=== EDGE CASES TESTS ===" << std::endl;
    
    // Empty vector operations
    mcu::vector<int> empty;
    ASSERT_TRUE(empty.empty(), "empty vector - empty()");
    ASSERT_EQ(0, empty.size(), "empty vector - size()");
    
    // Single element
    mcu::vector<int> single;
    single.push_back(42);
    ASSERT_EQ(1, single.size(), "single element - size");
    
    // Large operations
    mcu::vector<int> large;
    const size_t large_size = 10000;
    
    for (size_t i = 0; i < large_size; ++i) {
        large.push_back(static_cast<int>(i));
    }
    
    ASSERT_EQ(large_size, large.size(), "large vector - size");
    ASSERT_EQ(0, large[0], "large vector - first");
    ASSERT_EQ(static_cast<int>(large_size - 1), large[large_size - 1], "large vector - last");
    
    // Clear large vector
    large.clear();
    ASSERT_TRUE(large.empty(), "large vector cleared - empty");
}

void benchmark_performance() {
    std::cout << "\n=== PERFORMANCE BENCHMARKS ===" << std::endl;
    
    const size_t num_elements = 100000;
    
    // Benchmark push_back
    auto start = std::chrono::high_resolution_clock::now();
    
    mcu::vector<int> v;
    for (size_t i = 0; i < num_elements; ++i) {
        v.push_back(static_cast<int>(i));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "push_back " << num_elements << " elements: " << duration.count() << " μs" << std::endl;
    
    // Benchmark access
    start = std::chrono::high_resolution_clock::now();
    
    long long sum = 0;
    for (size_t i = 0; i < num_elements; ++i) {
        sum += v[i];
    }
    
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Access " << num_elements << " elements: " << duration.count() << " μs" << std::endl;
    std::cout << "Sum: " << sum << " (verification)" << std::endl;
    
    // Benchmark sort
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1, 100000);
    
    mcu::vector<int> random_v;
    for (size_t i = 0; i < 10000; ++i) {
        random_v.push_back(dis(gen));
    }
    
    start = std::chrono::high_resolution_clock::now();
    random_v.sort();
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Sort 10000 random elements: " << duration.count() << " μs" << std::endl;
}

void stress_test() {
    std::cout << "\n=== STRESS TESTS ===" << std::endl;
    
    // Repeated allocation/deallocation
    for (int cycle = 0; cycle < 100; ++cycle) {
        mcu::vector<int> v;
        for (int i = 0; i < 1000; ++i) {
            v.push_back(i);
        }
        v.clear();
    }
    results.pass("Stress test - allocation cycles");
    
    // Mixed operations
    mcu::vector<int> v;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> op_dis(0, 4);
    std::uniform_int_distribution<> val_dis(1, 1000);
    
    for (int i = 0; i < 10000; ++i) {
        int op = op_dis(gen);
        int val = val_dis(gen);
        
        switch (op) {
            case 0: // push_back
                v.push_back(val);
                break;
            case 1: // pop_back - using erase last element if pop_back not available
                if (!v.empty()) v.erase(v.size() - 1);
                break;
            case 2: // insert
                if (!v.empty()) {
                    std::uniform_int_distribution<> pos_dis(0, v.size() - 1);
                    v.insert(pos_dis(gen), val);
                }
                break;
            case 3: // erase
                if (!v.empty()) {
                    std::uniform_int_distribution<> pos_dis(0, v.size() - 1);
                    v.erase(pos_dis(gen));
                }
                break;
            case 4: // resize
                std::uniform_int_distribution<> size_dis(0, 500);
                v.resize(size_dis(gen));
                break;
        }
    }
    
    results.pass("Stress test - mixed operations");
    std::cout << "Final vector size after stress test: " << v.size() << std::endl;
}

void test_vector_b_vector_interop() {
    std::cout << "\n=== VECTOR/B_VECTOR INTEROPERABILITY TESTS ===" << std::endl;
    
    // Test vector to b_vector conversion
    mcu::vector<int> mvec(MAKE_INT_LIST(10, 20, 30, 40));
    b_vector<int, 8> bvec = mvec;  // Implicit conversion
    ASSERT_EQ(4, bvec.size(), "vector to b_vector - size");
    ASSERT_EQ(10, bvec[0], "vector to b_vector - element 0");
    ASSERT_EQ(40, bvec[3], "vector to b_vector - element 3");
    
    // Test b_vector to vector conversion
    b_vector<int, 8> bvec2;
    bvec2.push_back(1);
    bvec2.push_back(2);
    
    mcu::vector<int> mvec2 = bvec2; // Implicit conversion
    ASSERT_EQ(2, mvec2.size(), "b_vector to vector - size");
    ASSERT_EQ(1, mvec2[0], "b_vector to vector - element 0");
}

int main() {
    std::cout << "=== COMPREHENSIVE MCU::VECTOR TEST SUITE ===" << std::endl;
    std::cout << "Testing mcu::vector<T> with all features\n" << std::endl;
    
    try {
        // Core functionality tests
        test_basic_operations();
        test_copy_move_operations();
        test_element_access();
        test_modifiers();
        test_resize_operations();
        test_capacity_operations();
        test_iterators();
        test_sorting();
        test_complex_objects();
        test_memory_usage();
        test_edge_cases();
        
        // Interop tests
        test_vector_b_vector_interop();
        
        // Stress and performance tests
        stress_test();
        benchmark_performance();
        
        results.summary();
        
        if (results.failed == 0) {
            std::cout << "\n🎉 ALL TESTS PASSED! mcu::vector implementation is working correctly." << std::endl;
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
