#include <iostream>
#include <string>
#include <vector> // For comparison
#include <cassert>
#include <chrono>
#include <random>
#include <algorithm>

// Include our custom vector
#include "vector.cpp"
#include "b_vector.cpp"

using namespace std;
using namespace mcu;

// ANSI color codes for better output
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

class TestFramework {
public: // Make members public for access
    int passed = 0;
    int failed = 0;
    string current_test = "";

    void start_test(const string& test_name) {
        current_test = test_name;
        cout << CYAN << "Testing: " << test_name << RESET << endl;
    }

    template<typename T, typename U>
    void assert_equal(const string& description, T expected, U actual) {
        if (expected == actual) {
            cout << GREEN << "  ✓ " << description << RESET << endl;
            passed++;
        } else {
            cout << RED << "  ✗ " << description << " - Expected: " << expected 
                 << ", Got: " << actual << RESET << endl;
            failed++;
        }
    }

    void assert_true(const string& description, bool condition) {
        if (condition) {
            cout << GREEN << "  ✓ " << description << RESET << endl;
            passed++;
        } else {
            cout << RED << "  ✗ " << description << " - Expected true, got false" << RESET << endl;
            failed++;
        }
    }

    void print_summary() {
        cout << "\n" << WHITE << "========== TEST SUMMARY ==========" << RESET << endl;
        cout << GREEN << "Passed: " << passed << RESET << endl;
        cout << RED << "Failed: " << failed << RESET << endl;
        cout << "Total: " << (passed + failed) << endl;
        
        if (failed == 0) {
            cout << GREEN << "All tests passed! 🎉" << RESET << endl;
        } else {
            cout << RED << "Some tests failed! ❌" << RESET << endl;
        }
    }
};

// Test basic constructor and destructor
void test_constructors(TestFramework& tf) {
    tf.start_test("Constructors and Basic Operations");
    
    // Default constructor
    mcu::vector<int> v1;
    tf.assert_equal("Default constructor - size", (size_t)0, v1.size());
    tf.assert_equal("Default constructor - capacity", (size_t)1, v1.capacity());
    tf.assert_true("Default constructor - empty", v1.empty());
    
    // Constructor with size
    mcu::vector<int> v2(5);
    tf.assert_equal("Size constructor - size", (size_t)5, v2.size());
    tf.assert_equal("Size constructor - capacity", (size_t)5, v2.capacity());
    tf.assert_true("Size constructor - not empty", !v2.empty());
    
    // Constructor with size and value
    mcu::vector<int> v3(3, 42);
    tf.assert_equal("Size/value constructor - size", (size_t)3, v3.size());
    tf.assert_equal("Size/value constructor - first element", 42, v3[0]);
    tf.assert_equal("Size/value constructor - last element", 42, v3[2]);
    
    // Initializer list constructor
    mcu::vector<int> v4(MAKE_INT_LIST(1, 2, 3, 4, 5));
    tf.assert_equal("Initializer list constructor - size", (size_t)5, v4.size());
    tf.assert_equal("Initializer list constructor - first element", 1, v4[0]);
    tf.assert_equal("Initializer list constructor - last element", 5, v4[4]);
}

// Test copy and move semantics
void test_copy_move(TestFramework& tf) {
    tf.start_test("Copy and Move Semantics");
    
    mcu::vector<int> original(MAKE_INT_LIST(1, 2, 3, 4, 5));
    
    // Copy constructor
    mcu::vector<int> copied(original);
    tf.assert_equal("Copy constructor - size", original.size(), copied.size());
    tf.assert_equal("Copy constructor - content", original[2], copied[2]);
    
    // Copy assignment
    mcu::vector<int> assigned;
    assigned = original;
    tf.assert_equal("Copy assignment - size", original.size(), assigned.size());
    tf.assert_equal("Copy assignment - content", original[3], assigned[3]);
    
    // Move constructor
    mcu::vector<int> moved(std::move(original));
    tf.assert_equal("Move constructor - size", (size_t)5, moved.size());
    tf.assert_equal("Move constructor - content", 3, moved[2]);
    tf.assert_equal("Move constructor - original size", (size_t)0, original.size());
}

// Test element access
void test_element_access(TestFramework& tf) {
    tf.start_test("Element Access");
    
    mcu::vector<int> v(MAKE_INT_LIST(10, 20, 30, 40, 50));
    
    // operator[]
    tf.assert_equal("operator[] - valid index", 30, v[2]);
    tf.assert_equal("operator[] - first element", 10, v[0]);
    tf.assert_equal("operator[] - last element", 50, v[4]);
    
    // front() and back()
    tf.assert_equal("front()", 10, v.front());
    tf.assert_equal("back()", 50, v.back());
    
    // data()
    tf.assert_true("data() not null", v.data() != nullptr);
    tf.assert_equal("data() access", 10, *v.data());
    
    // Modify through operator[]
    v[2] = 99;
    tf.assert_equal("operator[] modification", 99, v[2]);
}

// Test capacity management
void test_capacity(TestFramework& tf) {
    tf.start_test("Capacity Management");
    
    mcu::vector<int> v;
    tf.assert_equal("Initial capacity", (size_t)1, v.capacity());
    
    // Reserve
    v.reserve(10);
    tf.assert_true("Reserve increases capacity", v.capacity() >= 10);
    tf.assert_equal("Reserve doesn't change size", (size_t)0, v.size());
    
    // Push elements to test auto-growth
    for (int i = 0; i < 25; i++) {
        v.push_back(i);
    }
    tf.assert_equal("Auto-growth - size", (size_t)25, v.size());
    tf.assert_true("Auto-growth - capacity", v.capacity() >= 25);
}

// Test modifiers
void test_modifiers(TestFramework& tf) {
    tf.start_test("Modifiers");
    
    mcu::vector<int> v;
    
    // push_back
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    tf.assert_equal("push_back - size", (size_t)3, v.size());
    tf.assert_equal("push_back - content", 2, v[1]);
    
    // pop_back
    v.pop_back();
    tf.assert_equal("pop_back - size", (size_t)2, v.size());
    tf.assert_equal("pop_back - last element", 2, v[1]);
    
    // insert at position
    v.insert(1, 99);
    tf.assert_equal("insert - size", (size_t)3, v.size());
    tf.assert_equal("insert - inserted element", 99, v[1]);
    tf.assert_equal("insert - shifted element", 2, v[2]);
    
    // erase
    v.erase(1);
    tf.assert_equal("erase - size", (size_t)2, v.size());
    tf.assert_equal("erase - remaining elements", 2, v[1]);
    
    // clear
    v.clear();
    tf.assert_equal("clear - size", (size_t)0, v.size());
    tf.assert_true("clear - empty", v.empty());
    
    // fill
    v.resize(5);
    v.fill(7);
    tf.assert_equal("fill - all elements same", 7, v[0]);
    tf.assert_equal("fill - all elements same", 7, v[4]);
}

// Test resize operations
void test_resize(TestFramework& tf) {
    tf.start_test("Resize Operations");
    
    mcu::vector<int> v(MAKE_INT_LIST(1, 2, 3));
    
    // Resize larger with default value
    v.resize(5);
    tf.assert_equal("resize larger - size", (size_t)5, v.size());
    tf.assert_equal("resize larger - old content", 2, v[1]);
    tf.assert_equal("resize larger - new content", 0, v[4]); // default int
    
    // Resize larger with specific value
    v.resize(7, 42);
    tf.assert_equal("resize with value - size", (size_t)7, v.size());
    tf.assert_equal("resize with value - new content", 42, v[6]);
    
    // Resize smaller
    v.resize(3);
    tf.assert_equal("resize smaller - size", (size_t)3, v.size());
    tf.assert_equal("resize smaller - remaining content", 2, v[1]);
}

// Test iterators
void test_iterators(TestFramework& tf) {
    tf.start_test("Iterators");
    
    mcu::vector<int> v(MAKE_INT_LIST(1, 2, 3, 4, 5));
    
    // begin() and end()
    tf.assert_true("begin() != end()", v.begin() != v.end());
    tf.assert_equal("begin() dereference", 1, *v.begin());
    tf.assert_equal("end() - 1 dereference", 5, *(v.end() - 1));
    
    // Range-based for loop simulation
    int sum = 0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    tf.assert_equal("Iterator sum", 15, sum);
}

// Test sorting functionality
void test_sorting(TestFramework& tf) {
    tf.start_test("Sorting");
    
    // Test with integers
    mcu::vector<int> v(MAKE_INT_LIST(5, 2, 8, 1, 9, 3));
    v.sort();
    
    bool is_sorted = true;
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i] < v[i-1]) {
            is_sorted = false;
            break;
        }
    }
    tf.assert_true("Integer sorting", is_sorted);
    tf.assert_equal("Sorted first element", 1, v[0]);
    tf.assert_equal("Sorted last element", 9, v[v.size()-1]);
    
    // Test with strings (if supported)
    mcu::vector<string> vs;
    vs.push_back("zebra");
    vs.push_back("apple");
    vs.push_back("banana");
    vs.sort();
    // Note: String sorting will use hash comparison
    tf.assert_equal("String vector size", (size_t)3, vs.size());
}

// Test edge cases
void test_edge_cases(TestFramework& tf) {
    tf.start_test("Edge Cases");
    
    mcu::vector<int> v;
    
    // Empty vector operations
    tf.assert_equal("Empty vector back()", 0, v.back()); // Should return default
    tf.assert_equal("Empty vector front()", 0, v.front()); // Should return default
    
    // Out of bounds access
    tf.assert_equal("Out of bounds access", 0, v[100]); // Should return default
    
    // Pop from empty
    v.pop_back(); // Should not crash
    tf.assert_equal("Pop from empty", (size_t)0, v.size());
    
    // Erase invalid position
    v.push_back(1);
    v.erase(100); // Should not crash
    tf.assert_equal("Erase invalid position", (size_t)1, v.size());
    
    // Large resize
    v.resize(1000);
    tf.assert_equal("Large resize", (size_t)1000, v.size());
    tf.assert_true("Large resize capacity", v.capacity() >= 1000);
}

// Test memory usage
void test_memory_usage(TestFramework& tf) {
    tf.start_test("Memory Usage");
    
    mcu::vector<int> v;
    size_t initial_memory = v.memory_usage();
    tf.assert_true("Memory usage > 0", initial_memory > 0);
    
    v.resize(100);
    size_t after_resize = v.memory_usage();
    tf.assert_true("Memory grows with size", after_resize > initial_memory);
}

// Performance test
void test_performance(TestFramework& tf) {
    tf.start_test("Performance Test");
    
    const size_t TEST_SIZE = 10000;
    
    auto start = chrono::high_resolution_clock::now();
    
    mcu::vector<int> v;
    for (size_t i = 0; i < TEST_SIZE; ++i) {
        v.push_back(i);
    }
    
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start);
    
    tf.assert_equal("Performance test size", TEST_SIZE, v.size());
    cout << YELLOW << "  ⏱️  Push " << TEST_SIZE << " elements took: " 
         << duration.count() << " microseconds" << RESET << endl;
    
    // Test random access performance
    start = chrono::high_resolution_clock::now();
    int sum = 0;
    for (size_t i = 0; i < TEST_SIZE; ++i) {
        sum += v[i];
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end - start);
    
    cout << YELLOW << "  ⏱️  Random access " << TEST_SIZE << " elements took: " 
         << duration.count() << " microseconds" << RESET << endl;
}

// Test with different data types
void test_different_types(TestFramework& tf) {
    tf.start_test("Different Data Types");
    
    // Test with double
    mcu::vector<double> vd;
    vd.push_back(3.14);
    vd.push_back(2.71);
    tf.assert_equal("Double vector size", (size_t)2, vd.size());
    tf.assert_true("Double comparison", abs(vd[0] - 3.14) < 0.001);
    
    // Test with string
    mcu::vector<string> vs;
    vs.push_back("Hello");
    vs.push_back("World");
    tf.assert_equal("String vector size", (size_t)2, vs.size());
    tf.assert_equal("String content", string("Hello"), vs[0]);
    
    // Test with char
    mcu::vector<char> vc;
    vc.push_back('A');
    vc.push_back('B');
    tf.assert_equal("Char vector size", (size_t)2, vc.size());
    tf.assert_equal("Char content", 'A', vc[0]);
}

// Stress test
void test_stress(TestFramework& tf) {
    tf.start_test("Stress Test");
    
    mcu::vector<int> v;
    const size_t STRESS_SIZE = 1000;
    
    // Random operations
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(1, 100);
    
    for (size_t i = 0; i < STRESS_SIZE; ++i) {
        int op = dis(gen) % 4;
        switch (op) {
            case 0: // push_back
                v.push_back(dis(gen));
                break;
            case 1: // pop_back
                if (!v.empty()) v.pop_back();
                break;
            case 2: // insert
                if (!v.empty()) v.insert(dis(gen) % v.size(), dis(gen));
                break;
            case 3: // erase
                if (!v.empty()) v.erase(dis(gen) % v.size());
                break;
        }
    }
    
    tf.assert_true("Stress test completed", true);
    cout << YELLOW << "  📊 Final vector size after stress test: " 
         << v.size() << RESET << endl;
}

// Test implicit conversions between vector and b_vector
void test_implicit_conversions(TestFramework& tf) {
    tf.start_test("Implicit Conversions between vector and b_vector");
    
    // Test 1: vector to b_vector conversion
    mcu::vector<int> v1(MAKE_INT_LIST(1, 2, 3, 4, 5));
    mcu::b_vector<int, 32> bv1 = v1; // Implicit conversion
    tf.assert_equal("vector to b_vector - size", v1.size(), bv1.size());
    tf.assert_equal("vector to b_vector - content", v1[2], bv1[2]);
    
    // Test 2: b_vector to vector conversion
    mcu::b_vector<int, 16> bv2(MAKE_INT_LIST(10, 20, 30));
    mcu::vector<int> v2 = bv2; // Implicit conversion
    tf.assert_equal("b_vector to vector - size", bv2.size(), v2.size());
    tf.assert_equal("b_vector to vector - content", bv2[1], v2[1]);
    
    // Test 3: Assignment conversions
    mcu::vector<int> v3;
    mcu::b_vector<int, 64> bv3(MAKE_INT_LIST(100, 200, 300, 400));
    v3 = bv3; // Assignment with implicit conversion
    tf.assert_equal("Assignment b_vector to vector - size", bv3.size(), v3.size());
    tf.assert_equal("Assignment b_vector to vector - content", bv3[3], v3[3]);
    
    mcu::b_vector<int, 32> bv4;
    mcu::vector<int> v4(MAKE_INT_LIST(50, 60, 70));
    bv4 = v4; // Assignment with implicit conversion
    tf.assert_equal("Assignment vector to b_vector - size", v4.size(), bv4.size());
    tf.assert_equal("Assignment vector to b_vector - content", v4[0], bv4[0]);
    
    // Test 4: Function parameter passing (implicit conversion)
    auto test_function_vector = [](const mcu::vector<int>& vec) -> int {
        return vec.size() + vec[0];
    };
    
    auto test_function_bvector = [](const mcu::b_vector<int, 32>& vec) -> int {
        return vec.size() + vec[0];
    };
    
    mcu::vector<int> v5(MAKE_INT_LIST(1, 2, 3));
    mcu::b_vector<int, 32> bv5(MAKE_INT_LIST(1, 2, 3));
    
    int result1 = test_function_vector(bv5); // b_vector passed to function expecting vector
    int result2 = test_function_bvector(v5); // vector passed to function expecting b_vector
    
    tf.assert_equal("Function param conversion - b_vector to vector", 4, result1);
    tf.assert_equal("Function param conversion - vector to b_vector", 4, result2);
    
    // Test 5: Large data conversion (heap vs SBO)
    mcu::vector<int> large_v;
    for (int i = 0; i < 100; ++i) {
        large_v.push_back(i);
    }
    
    mcu::b_vector<int, 32> large_bv = large_v; // Should use heap in b_vector
    tf.assert_equal("Large data conversion - size", large_v.size(), large_bv.size());
    tf.assert_equal("Large data conversion - first element", large_v[0], large_bv[0]);
    tf.assert_equal("Large data conversion - last element", large_v[99], large_bv[99]);
    
    // Test 6: Different b_vector sizes
    mcu::b_vector<int, 16> small_bv(MAKE_INT_LIST(1, 2, 3));
    mcu::b_vector<int, 64> large_bv2 = static_cast<mcu::vector<int>>(small_bv); // Explicit conversion through vector
    tf.assert_equal("Different SBO sizes - size", small_bv.size(), large_bv2.size());
    tf.assert_equal("Different SBO sizes - content", small_bv[1], large_bv2[1]);
}

// Test advanced edge cases
void test_advanced_edge_cases(TestFramework& tf) {
    tf.start_test("Advanced Edge Cases");
    
    // Test with zero capacity
    mcu::vector<int> v1(0);
    tf.assert_equal("Zero capacity constructor - size", (size_t)0, v1.size());
    tf.assert_true("Zero capacity constructor - capacity > 0", v1.capacity() > 0);
    
    // Test large allocations
    mcu::vector<int> v2;
    v2.reserve(1000000);
    tf.assert_true("Large reserve", v2.capacity() >= 1000000);
    tf.assert_equal("Large reserve - size unchanged", (size_t)0, v2.size());
    
    // Test maximum index access
    mcu::vector<int> v3(10, 42);
    tf.assert_equal("Max valid index", 42, v3[9]);
    tf.assert_equal("Beyond max index (safe)", 0, v3[1000]); // Should return default T()
    
    // Test resize to very large size
    mcu::vector<int> v4;
    v4.resize(10000);
    tf.assert_equal("Large resize - size", (size_t)10000, v4.size());
    tf.assert_equal("Large resize - default value", 0, v4[5000]);
    
    // Test shrink and regrow
    v4.resize(5);
    tf.assert_equal("Shrink after large resize", (size_t)5, v4.size());
    v4.resize(100, 99);
    tf.assert_equal("Regrow with value", (size_t)100, v4.size());
    tf.assert_equal("Regrow - new elements", 99, v4[50]);
    
    // Test multiple clears
    mcu::vector<int> v5(MAKE_INT_LIST(1, 2, 3));
    for (int i = 0; i < 5; ++i) {
        v5.clear();
        tf.assert_true("Multiple clear", v5.empty());
    }
    
    // Test insert at boundaries
    mcu::vector<int> v6(MAKE_INT_LIST(1, 3, 5));
    v6.insert(0, 0);  // Insert at beginning
    v6.insert(v6.size(), 6);  // Insert at end
    tf.assert_equal("Insert at boundaries - size", (size_t)5, v6.size());
    tf.assert_equal("Insert at beginning", 0, v6[0]);
    tf.assert_equal("Insert at end", 6, v6[4]);
    
    // Test erase at boundaries  
    v6.erase(0);  // Erase first
    v6.erase(v6.size() - 1);  // Erase last
    tf.assert_equal("Erase boundaries - size", (size_t)3, v6.size());
    tf.assert_equal("After boundary erase", 1, v6[0]);
}

// Test custom structs and complex types
void test_custom_types(TestFramework& tf) {
    tf.start_test("Custom Types and Structs");
    
    // Test with custom struct
    struct Point {
        int x, y;
        Point() : x(0), y(0) {}
        Point(int x_, int y_) : x(x_), y(y_) {}
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };
    
    mcu::vector<Point> points;
    points.push_back(Point(1, 2));
    points.push_back(Point(3, 4));
    points.push_back(Point(5, 6));
    
    tf.assert_equal("Custom struct - size", (size_t)3, points.size());
    tf.assert_true("Custom struct - content", points[1].x == 3 && points[1].y == 4);
    
    // Test resize with custom type
    points.resize(5, Point(10, 20));
    tf.assert_equal("Custom struct resize - size", (size_t)5, points.size());
    tf.assert_true("Custom struct resize - new element", points[4].x == 10 && points[4].y == 20);
    
    // Test with pair-like structure
    struct KeyValue {
        int key;
        std::string value;
        KeyValue() : key(0), value("") {}
        KeyValue(int k, const std::string& v) : key(k), value(v) {}
    };
    
    mcu::vector<KeyValue> kvs;
    kvs.push_back(KeyValue(1, "one"));
    kvs.push_back(KeyValue(2, "two"));
    
    tf.assert_equal("KeyValue struct - size", (size_t)2, kvs.size());
    tf.assert_equal("KeyValue struct - key", 2, kvs[1].key);
    tf.assert_equal("KeyValue struct - value", std::string("two"), kvs[1].value);
    
    // Test copy and move with complex types
    mcu::vector<KeyValue> kvs_copy = kvs;
    tf.assert_equal("Complex type copy - size", kvs.size(), kvs_copy.size());
    tf.assert_equal("Complex type copy - content", kvs[0].value, kvs_copy[0].value);
}

// Test different numeric types
void test_numeric_types(TestFramework& tf) {
    tf.start_test("Different Numeric Types");
    
    // Test with uint8_t
    mcu::vector<uint8_t> v_uint8;
    for (uint8_t i = 0; i < 10; ++i) {
        v_uint8.push_back(i * 25);
    }
    tf.assert_equal("uint8_t vector - size", (size_t)10, v_uint8.size());
    tf.assert_equal("uint8_t vector - content", (uint8_t)50, v_uint8[2]);
    
    // Test with int64_t
    mcu::vector<int64_t> v_int64;
    v_int64.push_back(1000000000000LL);
    v_int64.push_back(-1000000000000LL);
    tf.assert_equal("int64_t vector - size", (size_t)2, v_int64.size());
    tf.assert_equal("int64_t vector - positive", 1000000000000LL, v_int64[0]);
    tf.assert_equal("int64_t vector - negative", -1000000000000LL, v_int64[1]);
    
    // Test with float
    mcu::vector<float> v_float;
    v_float.push_back(3.14159f);
    v_float.push_back(-2.71828f);
    v_float.push_back(0.0f);
    tf.assert_equal("float vector - size", (size_t)3, v_float.size());
    tf.assert_true("float vector - positive", abs(v_float[0] - 3.14159f) < 0.00001f);
    tf.assert_true("float vector - negative", abs(v_float[1] - (-2.71828f)) < 0.00001f);
    tf.assert_equal("float vector - zero", 0.0f, v_float[2]);
    
    // Test with bool
    mcu::vector<bool> v_bool;
    v_bool.push_back(true);
    v_bool.push_back(false);
    v_bool.push_back(true);
    tf.assert_equal("bool vector - size", (size_t)3, v_bool.size());
    tf.assert_equal("bool vector - true", true, v_bool[0]);
    tf.assert_equal("bool vector - false", false, v_bool[1]);
    
    // Test sorting with different types
    mcu::vector<double> v_double;
    v_double.push_back(3.7);
    v_double.push_back(1.2);
    v_double.push_back(5.9);
    v_double.push_back(2.1);
    v_double.sort();
    tf.assert_true("double sort - ascending", v_double[0] <= v_double[1] && v_double[1] <= v_double[2]);
}

// Test memory stress scenarios
void test_memory_stress(TestFramework& tf) {
    tf.start_test("Memory Stress Tests");
    
    // Test rapid allocation/deallocation
    for (int cycle = 0; cycle < 10; ++cycle) {
        mcu::vector<int> v;
        for (int i = 0; i < 1000; ++i) {
            v.push_back(i);
        }
        v.clear();
        tf.assert_true("Rapid alloc/dealloc cycle", v.empty());
    }
    
    // Test fragmentation resistance
    std::vector<mcu::vector<int>> vectors;
    for (int i = 0; i < 20; ++i) {
        vectors.emplace_back();
        for (int j = 0; j < 100; ++j) {
            vectors.back().push_back(i * 100 + j);
        }
    }
    
    // Verify all vectors are intact
    bool all_correct = true;
    for (size_t i = 0; i < vectors.size(); ++i) {
        if (vectors[i].size() != 100 || vectors[i][50] != (int)(i * 100 + 50)) {
            all_correct = false;
            break;
        }
    }
    tf.assert_true("Fragmentation resistance", all_correct);
    
    // Test large element copying
    mcu::vector<std::string> v_strings;
    for (int i = 0; i < 100; ++i) {
        v_strings.push_back(std::string(100, 'A' + (i % 26)));
    }
    
    mcu::vector<std::string> v_strings_copy = v_strings;
    tf.assert_equal("Large element copy - size", v_strings.size(), v_strings_copy.size());
    tf.assert_equal("Large element copy - content", v_strings[50], v_strings_copy[50]);
    
    // Test fit operation
    mcu::vector<int> v_fit;
    v_fit.reserve(1000);
    for (int i = 0; i < 10; ++i) {
        v_fit.push_back(i);
    }
    size_t capacity_before = v_fit.capacity();
    v_fit.fit();
    tf.assert_true("Fit operation", v_fit.capacity() <= capacity_before);
    tf.assert_equal("Fit - size preserved", (size_t)10, v_fit.size());
}

// Test iterator edge cases
void test_iterator_edge_cases(TestFramework& tf) {
    tf.start_test("Iterator Edge Cases");
    
    // Test empty vector iterators
    mcu::vector<int> empty_v;
    tf.assert_true("Empty vector - begin == end", empty_v.begin() == empty_v.end());
    
    // Test single element iterators
    mcu::vector<int> single_v;
    single_v.push_back(42);
    tf.assert_equal("Single element - distance", (size_t)1, single_v.end() - single_v.begin());
    tf.assert_equal("Single element - dereference", 42, *single_v.begin());
    
    // Test iterator arithmetic
    mcu::vector<int> v(MAKE_INT_LIST(10, 20, 30, 40, 50));
    auto it = v.begin();
    tf.assert_equal("Iterator arithmetic - +2", 30, *(it + 2));
    tf.assert_equal("Iterator arithmetic - end-1", 50, *(v.end() - 1));
    
    // Test const iterators
    const mcu::vector<int>& const_v = v;
    auto const_it = const_v.begin();
    tf.assert_equal("Const iterator", 10, *const_it);
    tf.assert_equal("Const iterator - distance", (size_t)5, const_v.end() - const_v.begin());
    
    // Test iterator-based algorithms
    int sum = 0;
    for (auto it = v.begin(); it != v.end(); ++it) {
        sum += *it;
    }
    tf.assert_equal("Iterator-based sum", 150, sum);
    
    // Test range-based for loop simulation
    int product = 1;
    for (const auto& val : v) {
        if (val <= 30) product *= val;  // Only multiply first 3 elements
    }
    tf.assert_equal("Range-based iteration", 6000, product); // 10 * 20 * 30
}

// Test conversions between vector and b_vector with edge cases
void test_conversion_edge_cases(TestFramework& tf) {
    tf.start_test("Conversion Edge Cases");
    
    // Test empty vector conversions
    mcu::vector<int> empty_v;
    mcu::b_vector<int, 32> empty_bv = empty_v;
    tf.assert_equal("Empty vector to b_vector", (size_t)0, empty_bv.size());
    
    mcu::b_vector<int, 16> empty_bv2;
    mcu::vector<int> empty_v2 = empty_bv2;
    tf.assert_equal("Empty b_vector to vector", (size_t)0, empty_v2.size());
    
    // Test single element conversions
    mcu::vector<int> single_v;
    single_v.push_back(99);
    mcu::b_vector<int, 8> single_bv = single_v;
    tf.assert_equal("Single element conversion - size", (size_t)1, single_bv.size());
    tf.assert_equal("Single element conversion - value", 99, single_bv[0]);
    
    // Test conversion with exactly SBO size
    mcu::vector<int> exact_size_v;
    for (int i = 0; i < 16; ++i) {
        exact_size_v.push_back(i);
    }
    mcu::b_vector<int, 16> exact_size_bv = exact_size_v;
    tf.assert_equal("Exact SBO size conversion", (size_t)16, exact_size_bv.size());
    tf.assert_equal("Exact SBO size - content", 10, exact_size_bv[10]);
    
    // Test conversion exceeding SBO size
    mcu::vector<int> large_v;
    for (int i = 0; i < 100; ++i) {
        large_v.push_back(i * 2);
    }
    mcu::b_vector<int, 32> large_bv = large_v;
    tf.assert_equal("Large conversion - size", (size_t)100, large_bv.size());
    tf.assert_equal("Large conversion - content", 98, large_bv[49]);
    
    // Test multiple conversions (vector -> b_vector -> vector)
    mcu::vector<int> original(MAKE_INT_LIST(1, 2, 3, 4, 5));
    mcu::b_vector<int, 32> intermediate = original;
    mcu::vector<int> final = intermediate;
    tf.assert_equal("Multiple conversions - size", original.size(), final.size());
    tf.assert_equal("Multiple conversions - content", original[2], final[2]);
    
    // Test conversion with custom types
    struct TestStruct {
        int value;
        TestStruct() : value(0) {}
        TestStruct(int v) : value(v) {}
    };
    
    mcu::vector<TestStruct> struct_v;
    struct_v.push_back(TestStruct(10));
    struct_v.push_back(TestStruct(20));
    
    mcu::b_vector<TestStruct, 16> struct_bv = struct_v;
    tf.assert_equal("Custom type conversion - size", (size_t)2, struct_bv.size());
    tf.assert_equal("Custom type conversion - value", 20, struct_bv[1].value);
}

// Test boundary conditions and extreme cases
void test_boundary_conditions(TestFramework& tf) {
    tf.start_test("Boundary Conditions and Extreme Cases");
    
    // Test with maximum size_t values (be careful not to actually allocate)
    mcu::vector<uint8_t> v1;
    v1.reserve(0);  // Should handle gracefully
    tf.assert_true("Reserve 0 handled", v1.capacity() > 0);
    
    // Test with very small types
    mcu::vector<char> tiny_v;
    for (char c = 'A'; c <= 'Z'; ++c) {
        tiny_v.push_back(c);
    }
    tf.assert_equal("Tiny type vector - size", (size_t)26, tiny_v.size());
    tf.assert_equal("Tiny type vector - content", 'M', tiny_v[12]);
    
    // Test rapid size changes
    mcu::vector<int> dynamic_v;
    for (int cycle = 0; cycle < 5; ++cycle) {
        // Grow
        for (int i = 0; i < 100; ++i) {
            dynamic_v.push_back(cycle * 100 + i);
        }
        // Shrink
        for (int i = 0; i < 50; ++i) {
            if (!dynamic_v.empty()) dynamic_v.pop_back();
        }
    }
    tf.assert_equal("Rapid size changes - final size", (size_t)250, dynamic_v.size());
    
    // Test with zero-sized elements (empty struct)
    struct EmptyStruct {};
    mcu::vector<EmptyStruct> empty_struct_v;
    empty_struct_v.push_back(EmptyStruct{});
    empty_struct_v.push_back(EmptyStruct{});
    tf.assert_equal("Empty struct vector - size", (size_t)2, empty_struct_v.size());
    
    // Test insert and erase at every position
    mcu::vector<int> pos_test_v(MAKE_INT_LIST(0, 1, 2, 3, 4));
    pos_test_v.insert(2, 99);  // Insert in middle
    tf.assert_equal("Insert middle - size", (size_t)6, pos_test_v.size());
    tf.assert_equal("Insert middle - value", 99, pos_test_v[2]);
    tf.assert_equal("Insert middle - shifted", 2, pos_test_v[3]);
    
    // Test erase from middle
    pos_test_v.erase(2);
    tf.assert_equal("Erase middle - size", (size_t)5, pos_test_v.size());
    tf.assert_equal("Erase middle - value", 2, pos_test_v[2]);
    
    // Test multiple consecutive operations
    mcu::vector<std::string> string_ops_v;
    string_ops_v.push_back("first");
    string_ops_v.push_back("second");
    string_ops_v.push_back("third");
    string_ops_v.insert(1, "inserted");
    string_ops_v.erase(0);
    string_ops_v.push_back("fourth");
    tf.assert_equal("Multiple ops - size", (size_t)4, string_ops_v.size());
    tf.assert_equal("Multiple ops - content", std::string("inserted"), string_ops_v[0]);
    tf.assert_equal("Multiple ops - content", std::string("fourth"), string_ops_v[3]);
    
    // Test sort with duplicates
    mcu::vector<int> dup_v;
    int values[] = {5, 2, 8, 2, 1, 8, 3, 5, 1};
    for (int val : values) {
        dup_v.push_back(val);
    }
    dup_v.sort();
    tf.assert_equal("Sort with duplicates - size", (size_t)9, dup_v.size());
    tf.assert_equal("Sort with duplicates - first", 1, dup_v[0]);
    tf.assert_equal("Sort with duplicates - last", 8, dup_v[8]);
    
    // Verify sort stability for duplicates (should be in non-decreasing order)
    bool is_non_decreasing = true;
    for (size_t i = 1; i < dup_v.size(); ++i) {
        if (dup_v[i] < dup_v[i-1]) {
            is_non_decreasing = false;
            break;
        }
    }
    tf.assert_true("Sort with duplicates - non-decreasing", is_non_decreasing);
    
    // Test copy and assignment chain
    mcu::vector<int> chain1(MAKE_INT_LIST(1, 2, 3));
    mcu::vector<int> chain2 = chain1;
    mcu::vector<int> chain3;
    chain3 = chain2;
    mcu::vector<int> chain4(std::move(chain3));
    
    tf.assert_equal("Copy chain - final size", (size_t)3, chain4.size());
    tf.assert_equal("Copy chain - content", 2, chain4[1]);
    tf.assert_equal("Copy chain - moved from size", (size_t)0, chain3.size());
}

int main() {
    cout << MAGENTA << "╔══════════════════════════════════════╗" << endl;
    cout << "║        VECTOR CLASS TEST SUITE       ║" << endl;
    cout << "╚══════════════════════════════════════╝" << RESET << endl << endl;
    
    TestFramework tf;
    
    try {
        test_constructors(tf);
        test_copy_move(tf);
        test_element_access(tf);
        test_capacity(tf);
        test_modifiers(tf);
        test_resize(tf);
        test_iterators(tf);
        test_sorting(tf);
        test_edge_cases(tf);
        test_memory_usage(tf);
        test_different_types(tf);
        test_performance(tf);
        test_stress(tf);
        test_implicit_conversions(tf);
        test_advanced_edge_cases(tf);
        test_custom_types(tf);
        test_numeric_types(tf);
        test_memory_stress(tf);
        test_iterator_edge_cases(tf);
        test_conversion_edge_cases(tf);
        test_boundary_conditions(tf);
        
        cout << endl;
        tf.print_summary();
        
        return (tf.failed == 0) ? 0 : 1;
        
    } catch (const exception& e) {
        cout << RED << "Exception caught: " << e.what() << RESET << endl;
        return 1;
    } catch (...) {
        cout << RED << "Unknown exception caught!" << RESET << endl;
        return 1;
    }
}
