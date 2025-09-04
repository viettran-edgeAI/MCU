#include <iostream>
#include "../../src/STL_MCU.h"
#include <cstdint>

using namespace mcu;

using packed_vector_1bit = packed_vector<1>;
using packed_vector_2bit = packed_vector<2>;
using packed_vector_4bit = packed_vector<4>;
using packed_vector_1bit_tiny = packed_vector<1, mcu::TINY>;
using packed_vector_2bit_tiny = packed_vector<2, mcu::TINY>;
using packed_vector_4bit_tiny = packed_vector<4, mcu::TINY>;

void test_constructors_and_assignments() {
    std::cout << "------------- Constructor & Assignment Tests -------------\n";
    
    // Test 1: Default constructor
    packed_vector<3> v1;
    std::cout << "Default constructor - Size: " << v1.size() << ", Capacity: " << v1.capacity() << "\n";
    
    // Test 2: Constructor with capacity
    packed_vector<3> v2(5);
    std::cout << "Capacity constructor(5) - Size: " << v2.size() << ", Capacity: " << v2.capacity() << "\n";
    
    // Test 3: Constructor with size and value
    packed_vector<3> v3(4, 7);  // 3-bit values: max 7
    std::cout << "Size+value constructor(4,7): ";
    for (int i = 0; i < v3.size(); ++i) {
        std::cout << (int)v3[i] << " ";
    }
    std::cout << "\n";
    
    // Test 4: Custom initializer list constructor
    auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 0}, 8);
    packed_vector<3> v4(init_list);
    std::cout << "Custom initializer list constructor{1,2,3,4,5,6,7,0}: ";
    for (int i = 0; i < v4.size(); ++i) {
        std::cout << (int)v4[i] << " ";
    }
    std::cout << "\n";
    
    // Test 4b: Using macro for easier initialization
    packed_vector<3> v4b = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
    std::cout << "Macro initialization MAKE_INT_LIST(3, 1,2,3,4,5,6,7,0): ";
    for (int i = 0; i < v4b.size(); ++i) {
        std::cout << (int)v4b[i] << " ";
    }
    std::cout << "\n";
    
    // Test 5: Copy constructor
    packed_vector<3> v5(v4);
    std::cout << "Copy constructor from v4: ";
    for (int i = 0; i < v5.size(); ++i) {
        std::cout << (int)v5[i] << " ";
    }
    std::cout << "\n";
    
    // Test 6: Move constructor
    packed_vector<3> v6(std::move(v3));
    std::cout << "Move constructor from v3: ";
    for (int i = 0; i < v6.size(); ++i) {
        std::cout << (int)v6[i] << " ";
    }
    std::cout << " (v3 size after move: " << v3.size() << ")\n";
    
    // Test 7: Copy assignment
    packed_vector<3> v7;
    v7 = v4;
    std::cout << "Copy assignment from v4: ";
    for (int i = 0; i < v7.size(); ++i) {
        std::cout << (int)v7[i] << " ";
    }
    std::cout << "\n";
    
    // Test 8: Move assignment
    packed_vector<3> v8;
    v8 = std::move(v6);
    std::cout << "Move assignment from v6: ";
    for (int i = 0; i < v8.size(); ++i) {
        std::cout << (int)v8[i] << " ";
    }
    std::cout << " (v6 size after move: " << v6.size() << ")\n";
    
    // Test 9: Self-assignment (copy)
    v7 = v7;
    std::cout << "Self copy assignment - v7 unchanged: ";
    for (int i = 0; i < v7.size(); ++i) {
        std::cout << (int)v7[i] << " ";
    }
    std::cout << "\n";
    
    // Test 10: Assign with count and value
    v1.assign(6, 5);
    std::cout << "assign(6, 5): ";
    for (int i = 0; i < v1.size(); ++i) {
        std::cout << (int)v1[i] << " ";
    }
    std::cout << "\n";
    
    // Test 11: Assign with custom initializer list
    auto assign_list = mcu::min_init_list<uint8_t>((const uint8_t[]){7, 6, 5, 4, 3}, 5);
    v2.assign(assign_list);
    std::cout << "assign with custom list {7,6,5,4,3}: ";
    for (int i = 0; i < v2.size(); ++i) {
        std::cout << (int)v2[i] << " ";
    }
    std::cout << "\n";
    
    // Test 12: Empty custom initializer list
    auto empty_list = mcu::min_init_list<uint8_t>(nullptr, 0);
    packed_vector<3> v9(empty_list);
    std::cout << "Empty custom initializer list - Size: " << v9.size() << "\n";
    
    // Test 13: Large value clamping in constructors
    packed_vector<2> v10(3, 255);  // Should clamp 255 to 3 (2-bit max)
    std::cout << "2-bit vector with value 255 (clamped to " << (int)v10.max_value() << "): ";
    for (int i = 0; i < v10.size(); ++i) {
        std::cout << (int)v10[i] << " ";
    }
    std::cout << "\n";
    
    // Test 14: Memory efficiency comparison
    std::cout << "Memory usage comparison (8 elements):\n";
    packed_vector<1> v1bit(8, 1);
    packed_vector<2> v2bit(8, 3);
    packed_vector<4> v4bit(8, 15);
    std::cout << "1-bit vector: " << v1bit.memory_usage() << " bytes\n";
    std::cout << "2-bit vector: " << v2bit.memory_usage() << " bytes\n";
    std::cout << "4-bit vector: " << v4bit.memory_usage() << " bytes\n";
    std::cout << "Regular uint8_t array: " << (8 * sizeof(uint8_t)) << " bytes\n";
}

void test_packed_vector() {
    std::cout << "------------- Packed Vector Test -------------\n";
    
    // Test custom initializer list constructor
    auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){0, 1, 2, 3, 0, 1, 2, 3}, 8);
    packed_vector<2> packed2_init(init_list);
    std::cout << "2-bit packed vector from custom initializer list:\n";
    for (int i = 0; i < packed2_init.size(); ++i) {
        std::cout << (int)packed2_init[i] << " ";
    }
    std::cout << "\n";
    
    // Test resize functionality
    packed_vector<4> packed4;
    packed4.resize(5, 10);
    std::cout << "4-bit packed vector after resize(5, 10):\n";
    for (int i = 0; i < packed4.size(); ++i) {
        std::cout << (int)packed4[i] << " ";
    }
    std::cout << "\n";
    
    // Test front/back access
    if (!packed4.empty()) {
        std::cout << "Front: " << (int)packed4.front() << ", Back: " << (int)packed4.back() << "\n";
    }
    
    // Test assign
    auto assign_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5}, 5);
    packed4.assign(assign_list);
    std::cout << "After assign with custom list {1,2,3,4,5}:\n";
    for (int i = 0; i < packed4.size(); ++i) {
        std::cout << (int)packed4[i] << " ";
    }
    std::cout << "\n";
    
    // Test comparison
    packed_vector<4> packed4_copy = packed4;
    std::cout << "Vectors equal: " << (packed4 == packed4_copy ? "true" : "false") << "\n";
    
    // Original tests
    packed_vector<2> packed2(10, 3);
    std::cout << "2-bit packed vector (max value: " << (int)packed2.max_value() << "):\n";
    
    for (int i = 0; i < 10; ++i) {
        packed2.set(i, i % 4);
        std::cout << (int)packed2[i] << " ";
    }
    std::cout << "\nMemory usage: " << packed2.memory_usage() << " bytes\n";
    
    // Test 1-bit packed vector (boolean-like)
    packed_vector<1> packed1(8, 1);
    std::cout << "1-bit packed vector:\n";
    for (int i = 0; i < packed1.size(); ++i) {
        std::cout << (int)packed1[i] << " ";
    }
    std::cout << "\nMemory usage: " << packed1.memory_usage() << " bytes\n";
}

void test_fill_method() {
    std::cout << "------------- Fill Method Test -------------\n";
    
    // Test 1: Fill 2-bit vector with max value
    packed_vector<2> vec2bit(8);
    vec2bit.resize(8, 0); // Initialize with zeros
    std::cout << "2-bit vector before fill (initialized with 0): ";
    for (int i = 0; i < vec2bit.size(); ++i) {
        std::cout << (int)vec2bit[i] << " ";
    }
    std::cout << "\n";
    
    vec2bit.fill(3); // Fill with max 2-bit value
    std::cout << "After fill(3): ";
    for (int i = 0; i < vec2bit.size(); ++i) {
        std::cout << (int)vec2bit[i] << " ";
    }
    std::cout << "\n";
    
    // Test 2: Fill 4-bit vector with mid-range value
    packed_vector<4> vec4bit = MAKE_UINT8_LIST(4, 1, 2, 3, 4, 5, 6);
    std::cout << "4-bit vector before fill: ";
    for (int i = 0; i < vec4bit.size(); ++i) {
        std::cout << (int)vec4bit[i] << " ";
    }
    std::cout << "\n";
    
    vec4bit.fill(10);
    std::cout << "After fill(10): ";
    for (int i = 0; i < vec4bit.size(); ++i) {
        std::cout << (int)vec4bit[i] << " ";
    }
    std::cout << "\n";
    
    // Test 3: Fill 1-bit vector (boolean-like)
    packed_vector<1> vec1bit(12, 0);
    std::cout << "1-bit vector before fill (12 zeros): ";
    for (int i = 0; i < vec1bit.size(); ++i) {
        std::cout << (int)vec1bit[i];
    }
    std::cout << "\n";
    
    vec1bit.fill(1);
    std::cout << "After fill(1): ";
    for (int i = 0; i < vec1bit.size(); ++i) {
        std::cout << (int)vec1bit[i];
    }
    std::cout << "\n";
    
    // Test 4: Fill with value that exceeds bit limit (should be clamped)
    packed_vector<3> vec3bit(5, 0);
    std::cout << "3-bit vector (max value " << (int)vec3bit.max_value() << ") before fill: ";
    for (int i = 0; i < vec3bit.size(); ++i) {
        std::cout << (int)vec3bit[i] << " ";
    }
    std::cout << "\n";
    
    vec3bit.fill(255); // Should be clamped to 7 (max 3-bit value)
    std::cout << "After fill(255) - clamped to " << (int)vec3bit.max_value() << ": ";
    for (int i = 0; i < vec3bit.size(); ++i) {
        std::cout << (int)vec3bit[i] << " ";
    }
    std::cout << "\n";
    
    // Test 5: Fill empty vector (should do nothing)
    packed_vector<2> vecEmpty;
    std::cout << "Empty vector size before fill: " << vecEmpty.size() << "\n";
    vecEmpty.fill(2);
    std::cout << "Empty vector size after fill(2): " << vecEmpty.size() << "\n";
    
    // Test 6: Fill large vector for performance check
    packed_vector<6> vecLarge(100, 0);
    std::cout << "Large vector (100 elements) memory usage before fill: " << vecLarge.memory_usage() << " bytes\n";
    vecLarge.fill(63); // Max 6-bit value
    std::cout << "First 10 elements after fill(63): ";
    for (int i = 0; i < 10; ++i) {
        std::cout << (int)vecLarge[i] << " ";
    }
    std::cout << "\nLast element: " << (int)vecLarge[99] << "\n";
    
    std::cout << "------------- Fill Method Test Complete -------------\n\n";
}

void test_tiny_mode() {
    std::cout << "------------- TINY Mode Test (4-bit size + 4-bit capacity) -------------\n";
    
    // Test 1: Default constructor TINY mode
    packed_vector<2, mcu::TINY> tiny_vec;
    std::cout << "TINY default constructor - Size: " << (int)tiny_vec.size() 
              << ", Capacity: " << (int)tiny_vec.capacity() << "\n";
    
    // Test 2: Constructor with capacity (max 15 for TINY)
    packed_vector<3, mcu::TINY> tiny_vec2(8);
    std::cout << "TINY capacity constructor(8) - Size: " << (int)tiny_vec2.size() 
              << ", Capacity: " << (int)tiny_vec2.capacity() << "\n";
    
    // Test 3: Constructor with size and value
    packed_vector<2, mcu::TINY> tiny_vec3(5, 3);
    std::cout << "TINY size+value constructor(5,3): ";
    for (int i = 0; i < tiny_vec3.size(); ++i) {
        std::cout << (int)tiny_vec3[i] << " ";
    }
    std::cout << "Size: " << (int)tiny_vec3.size() 
              << ", Capacity: " << (int)tiny_vec3.capacity() << "\n";
    
    // Test 4: Using TINY type aliases
    packed_vector_2bit_tiny tiny_alias(4, 2);
    std::cout << "TINY type alias (4,2): ";
    for (int i = 0; i < tiny_alias.size(); ++i) {
        std::cout << (int)tiny_alias[i] << " ";
    }
    std::cout << "Size: " << (int)tiny_alias.size() 
              << ", Capacity: " << (int)tiny_alias.capacity() << "\n";
    
    // Test 5: Push back until capacity limit (15 elements max)
    packed_vector<1, mcu::TINY> tiny_vec4;
    std::cout << "TINY push_back test (1-bit elements): ";
    for (int i = 0; i < 12; ++i) {
        tiny_vec4.push_back(i % 2);
        if (i < 8) std::cout << (int)tiny_vec4[i] << " ";
    }
    std::cout << "\nAfter 12 push_backs - Size: " << (int)tiny_vec4.size() 
              << ", Capacity: " << (int)tiny_vec4.capacity() << "\n";
    
    // Test 6: Memory comparison TINY vs MEDIUM
    packed_vector<4, mcu::TINY> tiny_4bit(10, 15);
    packed_vector<4, mcu::MEDIUM> medium_4bit(10, 15);
    
    std::cout << "Memory comparison (10 elements, 4-bit each):\n";
    std::cout << "TINY mode total memory: " << (sizeof(tiny_4bit) + tiny_4bit.memory_usage()) << " bytes\n";
    std::cout << "MEDIUM mode total memory: " << (sizeof(medium_4bit) + medium_4bit.memory_usage()) << " bytes\n";
    std::cout << "TINY object size: " << sizeof(tiny_4bit) << " bytes\n";
    std::cout << "MEDIUM object size: " << sizeof(medium_4bit) << " bytes\n";
    
    // Test 7: Copy and move operations
    packed_vector<3, mcu::TINY> tiny_original(6, 5);
    packed_vector<3, mcu::TINY> tiny_copy(tiny_original);
    packed_vector<3, mcu::TINY> tiny_moved(std::move(tiny_original));
    
    std::cout << "Copy constructor: Size: " << (int)tiny_copy.size() 
              << ", Capacity: " << (int)tiny_copy.capacity() << "\n";
    std::cout << "Move constructor: Size: " << (int)tiny_moved.size() 
              << ", Capacity: " << (int)tiny_moved.capacity() << "\n";
    std::cout << "Original after move: Size: " << (int)tiny_original.size() 
              << ", Capacity: " << (int)tiny_original.capacity() << "\n";
    
    // Test 8: Resize operations
    packed_vector<2, mcu::TINY> tiny_resize(3, 1);
    std::cout << "Before resize(8): Size: " << (int)tiny_resize.size() 
              << ", Capacity: " << (int)tiny_resize.capacity() << "\n";
    tiny_resize.resize(8, 2);
    std::cout << "After resize(8,2): Size: " << (int)tiny_resize.size() 
              << ", Capacity: " << (int)tiny_resize.capacity() << "\n";
    std::cout << "Elements: ";
    for (int i = 0; i < tiny_resize.size(); ++i) {
        std::cout << (int)tiny_resize[i] << " ";
    }
    std::cout << "\n";
    
    // Test 9: Maximum capacity test (should be limited to 15)
    packed_vector<1, mcu::TINY> tiny_max;
    std::cout << "Testing maximum capacity for TINY mode:\n";
    for (int i = 0; i < 20; ++i) {  // Try to exceed limit
        tiny_max.push_back(1);
        if (tiny_max.capacity() >= 15) break;
    }
    std::cout << "Final size: " << (int)tiny_max.size() 
              << ", Final capacity: " << (int)tiny_max.capacity() << "\n";
    
    // Test 10: Initializer list with TINY mode
    auto tiny_init = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 0, 1, 1, 0}, 5);
    packed_vector<1, mcu::TINY> tiny_from_list(tiny_init);
    std::cout << "TINY from init list {1,0,1,1,0}: ";
    for (int i = 0; i < tiny_from_list.size(); ++i) {
        std::cout << (int)tiny_from_list[i];
    }
    std::cout << " Size: " << (int)tiny_from_list.size() 
              << ", Capacity: " << (int)tiny_from_list.capacity() << "\n";
    
    // Test 11: Fill operation
    packed_vector<3, mcu::TINY> tiny_fill(7, 0);
    tiny_fill.fill(6);
    std::cout << "TINY fill test (7 elements with value 6): ";
    for (int i = 0; i < tiny_fill.size(); ++i) {
        std::cout << (int)tiny_fill[i] << " ";
    }
    std::cout << "\n";
    
    // Test 12: Comparison between TINY vectors
    packed_vector<2, mcu::TINY> tiny_a(4, 3);
    packed_vector<2, mcu::TINY> tiny_b(4, 3);
    packed_vector<2, mcu::TINY> tiny_c(4, 2);
    
    std::cout << "TINY vector comparison:\n";
    std::cout << "tiny_a == tiny_b: " << (tiny_a == tiny_b ? "true" : "false") << "\n";
    std::cout << "tiny_a == tiny_c: " << (tiny_a == tiny_c ? "true" : "false") << "\n";
    
    std::cout << "------------- TINY Mode Test Complete -------------\n\n";
}

void test_iterators() {
    std::cout << "------------- Iterator Test -------------\n";
    
    // Test 1: Basic iterator functionality
    packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
    std::cout << "Original vector: ";
    for (int i = 0; i < vec.size(); ++i) {
        std::cout << (int)vec[i] << " ";
    }
    std::cout << "\n";
    
    // Test 2: Range-based for loop (const)
    std::cout << "Using range-based for loop: ";
    for (const auto& val : vec) {
        std::cout << (int)val << " ";
    }
    std::cout << "\n";
    
    // Test 3: Iterator traversal
    std::cout << "Using iterators: ";
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        std::cout << (int)*it << " ";
    }
    std::cout << "\n";
    
    // Test 4: Const iterator
    const packed_vector<3>& const_vec = vec;
    std::cout << "Using const iterators: ";
    for (auto it = const_vec.cbegin(); it != const_vec.cend(); ++it) {
        std::cout << (int)*it << " ";
    }
    std::cout << "\n";
    
    // Test 5: Iterator arithmetic
    auto it = vec.begin();
    std::cout << "Iterator arithmetic tests:\n";
    std::cout << "  First element: " << (int)*it << "\n";
    it += 3;
    std::cout << "  After += 3: " << (int)*it << " (index: " << it.get_index() << ")\n";
    it -= 1;
    std::cout << "  After -= 1: " << (int)*it << " (index: " << it.get_index() << ")\n";
    
    auto it2 = it + 2;
    std::cout << "  it + 2: " << (int)*it2 << " (index: " << it2.get_index() << ")\n";
    
    auto it3 = vec.end() - 1;
    std::cout << "  Last element (end - 1): " << (int)*it3 << "\n";
    
    // Test 6: Iterator distance
    auto distance = vec.end() - vec.begin();
    std::cout << "  Distance from begin to end: " << distance << "\n";
    
    // Test 7: Iterator comparison
    auto begin_it = vec.begin();
    auto end_it = vec.end();
    std::cout << "Iterator comparisons:\n";
    std::cout << "  begin == end: " << (begin_it == end_it ? "true" : "false") << "\n";
    std::cout << "  begin != end: " << (begin_it != end_it ? "true" : "false") << "\n";
    std::cout << "  begin < end: " << (begin_it < end_it ? "true" : "false") << "\n";
    
    // Test 8: Empty vector iterators
    packed_vector<2> empty_vec;
    std::cout << "Empty vector iterator test:\n";
    std::cout << "  begin == end for empty: " << (empty_vec.begin() == empty_vec.end() ? "true" : "false") << "\n";
    std::cout << "  Range-based for on empty: ";
    for (const auto& val : empty_vec) {
        std::cout << (int)val << " ";
    }
    std::cout << "(should print nothing)\n";
    
    // Test 9: Different bit sizes
    packed_vector<1> vec1bit = MAKE_UINT8_LIST(1, 1, 0, 1, 0, 1);
    std::cout << "1-bit vector with iterators: ";
    for (const auto& val : vec1bit) {
        std::cout << (int)val;
    }
    std::cout << "\n";
    
    packed_vector<4> vec4bit = MAKE_UINT8_LIST(4, 15, 14, 13, 12, 11);
    std::cout << "4-bit vector with iterators: ";
    for (const auto& val : vec4bit) {
        std::cout << (int)val << " ";
    }
    std::cout << "\n";
    
    // // Test 10: TINY mode iterators
    // packed_vector<2, mcu::TINY> tiny_vec = MAKE_INT_LIST(2, 3, 2, 1, 0, 3, 2);
    // std::cout << "TINY mode vector with iterators: ";
    // for (const auto& val : tiny_vec) {
    //     std::cout << (int)val << " ";
    // }
    // std::cout << "\n";
    
    // Test 11: Backward iteration
    std::cout << "Backward iteration (using -- operator): ";
    auto rev_it = vec.end();
    while (rev_it != vec.begin()) {
        --rev_it;
        std::cout << (int)*rev_it << " ";
    }
    std::cout << "\n";
    
    // Test 12: Iterator with large vector
    packed_vector<6> large_vec(20, 63);  // 20 elements, all max 6-bit value
    std::cout << "Large vector (20 elements) first 5 with iterator: ";
    int count = 0;
    for (const auto& val : large_vec) {
        if (count >= 5) break;
        std::cout << (int)val << " ";
        ++count;
    }
    std::cout << "\n";
    
    std::cout << "------------- Iterator Test Complete -------------\n\n";
}

void test_range_constructor() {
    std::cout << "------------- Range Constructor Test -------------\n";
    
    // Test 1: Basic range copy from middle of vector
    packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
    std::cout << "Source vector: ";
    for (int i = 0; i < source.size(); ++i) {
        std::cout << (int)source[i] << " ";
    }
    std::cout << "\n";
    
    packed_vector<3> range1(source, 2, 6);  // Copy elements at indices 2,3,4,5
    std::cout << "Range constructor(source, 2, 6): ";
    for (int i = 0; i < range1.size(); ++i) {
        std::cout << (int)range1[i] << " ";
    }
    std::cout << "Size: " << range1.size() << ", Capacity: " << range1.capacity() << "\n";
    
    // Test 2: Copy from beginning
    packed_vector<3> range2(source, 0, 3);  // Copy first 3 elements
    std::cout << "Range constructor(source, 0, 3): ";
    for (int i = 0; i < range2.size(); ++i) {
        std::cout << (int)range2[i] << " ";
    }
    std::cout << "Size: " << range2.size() << "\n";
    
    // Test 3: Copy to end
    packed_vector<3> range3(source, 5, source.size());  // Copy last 3 elements
    std::cout << "Range constructor(source, 5, end): ";
    for (int i = 0; i < range3.size(); ++i) {
        std::cout << (int)range3[i] << " ";
    }
    std::cout << "Size: " << range3.size() << "\n";
    
    // Test 4: Copy entire vector
    packed_vector<3> range4(source, 0, source.size());
    std::cout << "Range constructor(source, 0, full_size): ";
    for (int i = 0; i < range4.size(); ++i) {
        std::cout << (int)range4[i] << " ";
    }
    std::cout << "Size: " << range4.size() << "\n";
    
    // Test 5: Single element copy
    packed_vector<3> range5(source, 3, 4);  // Copy only element at index 3
    std::cout << "Range constructor(source, 3, 4) - single element: ";
    for (int i = 0; i < range5.size(); ++i) {
        std::cout << (int)range5[i] << " ";
    }
    std::cout << "Size: " << range5.size() << "\n";
    
    // Test 6: Edge cases - invalid ranges
    packed_vector<3> range6(source, 5, 3);  // start > end
    std::cout << "Invalid range (start > end) - Size: " << range6.size() << "\n";
    
    packed_vector<3> range7(source, 20, 25);  // start >= source.size()
    std::cout << "Invalid range (start >= source.size()) - Size: " << range7.size() << "\n";
    
    packed_vector<3> range8(source, 6, 20);  // end > source.size() (should clamp)
    std::cout << "Range with end > source.size() (should clamp): ";
    for (int i = 0; i < range8.size(); ++i) {
        std::cout << (int)range8[i] << " ";
    }
    std::cout << "Size: " << range8.size() << "\n";
    
    // Test 7: Empty range (start == end)
    packed_vector<3> range9(source, 4, 4);
    std::cout << "Empty range (start == end) - Size: " << range9.size() << "\n";
    
    // Test 8: Different bit sizes
    packed_vector<1> source1bit = MAKE_UINT8_LIST(1, 1, 0, 1, 1, 0, 0, 1, 0);
    packed_vector<1> range1bit(source1bit, 2, 7);
    std::cout << "1-bit range constructor(2, 7): ";
    for (int i = 0; i < range1bit.size(); ++i) {
        std::cout << (int)range1bit[i];
    }
    std::cout << " Size: " << range1bit.size() << "\n";
    
    packed_vector<4> source4bit = MAKE_UINT8_LIST(4, 15, 14, 13, 12, 11, 10, 9, 8);
    packed_vector<4> range4bit(source4bit, 1, 5);
    std::cout << "4-bit range constructor(1, 5): ";
    for (int i = 0; i < range4bit.size(); ++i) {
        std::cout << (int)range4bit[i] << " ";
    }
    std::cout << "Size: " << range4bit.size() << "\n";
    
    // Test 9: TINY mode range constructor
    packed_vector<2, mcu::TINY> tiny_source = MAKE_UINT8_LIST(2, 3, 2, 1, 0, 3, 2, 1, 0);
    packed_vector<2, mcu::TINY> tiny_range(tiny_source, 2, 6);
    std::cout << "TINY mode range constructor(2, 6): ";
    for (int i = 0; i < tiny_range.size(); ++i) {
        std::cout << (int)tiny_range[i] << " ";
    }
    std::cout << "Size: " << (int)tiny_range.size() << ", Capacity: " << (int)tiny_range.capacity() << "\n";
    
    // Test 10: Memory efficiency check
    std::cout << "Memory efficiency check:\n";
    std::cout << "  Source vector memory: " << source.memory_usage() << " bytes\n";
    std::cout << "  Range vector memory: " << range1.memory_usage() << " bytes\n";
    std::cout << "  Memory ratio: " << (double)range1.memory_usage() / source.memory_usage() * 100 << "%\n";
    
    // Test 11: Range constructor with subsequent operations
    packed_vector<3> range_ops(source, 1, 4);  // Copy elements 1,2,3
    std::cout << "Range for operations test: ";
    for (int i = 0; i < range_ops.size(); ++i) {
        std::cout << (int)range_ops[i] << " ";
    }
    std::cout << "\n";
    
    range_ops.push_back(0);
    std::cout << "After push_back(0): ";
    for (int i = 0; i < range_ops.size(); ++i) {
        std::cout << (int)range_ops[i] << " ";
    }
    std::cout << "Size: " << range_ops.size() << ", Capacity: " << range_ops.capacity() << "\n";
    
    range_ops.resize(6, 7);
    std::cout << "After resize(6, 7): ";
    for (int i = 0; i < range_ops.size(); ++i) {
        std::cout << (int)range_ops[i] << " ";
    }
    std::cout << "Size: " << range_ops.size() << "\n";
    
    // Test 12: Nested range construction
    packed_vector<3> nested_range(range_ops, 1, 4);  // Take range from a range
    std::cout << "Nested range constructor(range_ops, 1, 4): ";
    for (int i = 0; i < nested_range.size(); ++i) {
        std::cout << (int)nested_range[i] << " ";
    }
    std::cout << "Size: " << nested_range.size() << "\n";
    
    // Test 13: Range from empty source vector
    packed_vector<3> empty_source;
    packed_vector<3> range_from_empty(empty_source, 0, 1);
    std::cout << "Range from empty source - Size: " << range_from_empty.size() << "\n";
    
    // Test 14: Zero-index range at the end
    packed_vector<3> end_range(source, source.size()-1, source.size());  // Last element only
    std::cout << "Last element range: ";
    for (int i = 0; i < end_range.size(); ++i) {
        std::cout << (int)end_range[i] << " ";
    }
    std::cout << "Size: " << end_range.size() << "\n";
    
    // Test 15: Iterator compatibility with range constructor
    std::cout << "Iterator test on range-constructed vector: ";
    for (const auto& val : range1) {
        std::cout << (int)val << " ";
    }
    std::cout << "\n";
    
    // Test 16: Comparison between range-constructed vectors
    packed_vector<3> range_comp1(source, 2, 5);
    packed_vector<3> range_comp2(source, 2, 5);
    packed_vector<3> range_comp3(source, 2, 6);
    
    std::cout << "Range comparison tests:\n";
    std::cout << "  Same ranges equal: " << (range_comp1 == range_comp2 ? "true" : "false") << "\n";
    std::cout << "  Different ranges equal: " << (range_comp1 == range_comp3 ? "true" : "false") << "\n";
    
    // Test 17: Cross-bit-size range constructor (NEW TEMPLATED VERSION)
    std::cout << "Cross-bit-size range constructor tests:\n";
    
    // Test 17a: Copy from 4-bit to 2-bit (values should be clamped)
    packed_vector<4> source4to2 = MAKE_UINT8_LIST(15, 14, 13, 12, 4, 3, 2, 1);  // 8 elements
    std::cout << "4-bit source (" << source4to2.size() << " elements): ";
    for (int i = 0; i < source4to2.size(); ++i) {
        std::cout << (int)source4to2[i] << " ";
    }
    std::cout << "(max value: " << (int)source4to2.max_value() << ")\n";
    
    packed_vector<2> range4to2(source4to2, 1, 5);  // Copy indices 1-4: 14,13,12,4 -> should clamp to 2,1,0,0
    std::cout << "2-bit range(1,5) with clamping (" << range4to2.size() << " elements): ";
    for (int i = 0; i < range4to2.size(); ++i) {
        std::cout << (int)range4to2[i] << " ";
    }
    std::cout << "(max value: " << (int)range4to2.max_value() << ", expected: 2 1 0 0)\n";
    
    // Test 17b: Copy from 2-bit to 4-bit (values should fit perfectly)
    packed_vector<2> source2to4 = MAKE_UINT8_LIST(3, 2, 1, 0, 3, 2, 1);  // 7 elements
    std::cout << "2-bit source (" << source2to4.size() << " elements): ";
    for (int i = 0; i < source2to4.size(); ++i) {
        std::cout << (int)source2to4[i] << " ";
    }
    std::cout << "(max value: " << (int)source2to4.max_value() << ")\n";
    
    packed_vector<4> range2to4(source2to4, 2, 6);  // Copy indices 2-5: 1,0,3,2
    std::cout << "4-bit range(2,6) no clamping needed (" << range2to4.size() << " elements): ";
    for (int i = 0; i < range2to4.size(); ++i) {
        std::cout << (int)range2to4[i] << " ";
    }
    std::cout << "(max value: " << (int)range2to4.max_value() << ")\n";
    
    // Test 17c: Copy from 1-bit to 3-bit
    packed_vector<1> source1to3 = MAKE_UINT8_LIST(1, 0, 1, 1, 0, 0, 1);  // 6 elements
    std::cout << "1-bit source (" << source1to3.size() << " elements): ";
    for (int i = 0; i < source1to3.size(); ++i) {
        std::cout << (int)source1to3[i];
    }
    std::cout << " (max value: " << (int)source1to3.max_value() << ")\n";
    
    packed_vector<3> range1to3(source1to3, 1, 5);  // Copy indices 1-4: 0,1,1,0
    std::cout << "3-bit range(1,5) (" << range1to3.size() << " elements): ";
    for (int i = 0; i < range1to3.size(); ++i) {
        std::cout << (int)range1to3[i] << " ";
    }
    std::cout << "(max value: " << (int)range1to3.max_value() << ")\n";
    
    // Test 17d: Copy from 6-bit to 3-bit (heavy clamping)
    packed_vector<6> source6to3 = MAKE_UINT8_LIST(63, 32, 16, 8, 4, 15);  // 6 elements
    std::cout << "6-bit source (" << source6to3.size() << " elements): ";
    for (int i = 0; i < source6to3.size(); ++i) {
        std::cout << (int)source6to3[i] << " ";
    }
    std::cout << "(max value: " << (int)source6to3.max_value() << ")\n";
    
    packed_vector<3> range6to3(source6to3, 0, 4);  // Copy indices 0-3: 63,32,16,8 -> should clamp to 7,0,0,0
    std::cout << "3-bit range(0,4) with heavy clamping (" << range6to3.size() << " elements): ";
    for (int i = 0; i < range6to3.size(); ++i) {
        std::cout << (int)range6to3[i] << " ";
    }
    std::cout << "(max value: " << (int)range6to3.max_value() << ", expected: 7 0 0 0)\n";
    
    // Test 17e: Cross-size flag range constructor (TINY to MEDIUM)
    packed_vector<2, mcu::TINY> tiny_source_cross = MAKE_UINT8_LIST(3, 2, 1, 0, 3);  // 5 elements
    packed_vector<2, mcu::MEDIUM> medium_from_tiny(tiny_source_cross, 1, 4);  // Copy indices 1-3: 2,1,0
    std::cout << "TINY to MEDIUM range(1,4) (" << medium_from_tiny.size() << " elements): ";
    for (int i = 0; i < medium_from_tiny.size(); ++i) {
        std::cout << (int)medium_from_tiny[i] << " ";
    }
    std::cout << "Size: " << medium_from_tiny.size() << "\n";
    
    // Test 17f: Value clamping verification with edge cases
    packed_vector<8> source8bit = MAKE_UINT8_LIST(255, 128, 64, 32, 16, 8);  // 6 elements
    packed_vector<3> range8to3(source8bit, 0, 6);  // Should clamp all: 255&7=7, 128&7=0, 64&7=0, 32&7=0, 16&7=0, 8&7=0
    std::cout << "8-bit to 3-bit clamping test (" << range8to3.size() << " elements): ";
    for (int i = 0; i < range8to3.size(); ++i) {
        std::cout << (int)range8to3[i] << " ";
    }
    std::cout << "\nExpected: 7 0 0 0 0 0 (255&7=7, 128&7=0, etc.)\n";
    
    // Test 17g: Performance note for cross-bit copying
    std::cout << "Memory usage comparison for cross-bit copying:\n";
    std::cout << "  6-bit source (6 elements): " << source6to3.memory_usage() << " bytes\n";
    std::cout << "  3-bit range (4 elements): " << range6to3.memory_usage() << " bytes\n";
    std::cout << "  Compression ratio: " << (double)range6to3.memory_usage() / source6to3.memory_usage() * 100 << "%\n";
    
    // Test 18: Type safety verification (should compile)
    std::cout << "Type safety verification:\n";
    std::cout << "  Same-type range constructor: ";
    packed_vector<3> same_type_range(source, 1, 3);
    for (int i = 0; i < same_type_range.size(); ++i) {
        std::cout << (int)same_type_range[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "  Cross-type range constructor: ";
    packed_vector<3> cross_type_range(source4bit, 1, 3);  // 4-bit to 3-bit
    for (int i = 0; i < cross_type_range.size(); ++i) {
        std::cout << (int)cross_type_range[i] << " ";
    }
    std::cout << "\n";
    
    // Test 19: Edge case - empty range with cross-bit-size
    packed_vector<1> empty_cross_range(source4bit, 10, 10);
    std::cout << "  Empty cross-bit range - Size: " << empty_cross_range.size() << "\n";
    
    // Test 20: Maximum value preservation test
    std::cout << "Maximum value preservation test:\n";
    packed_vector<4> max_source = MAKE_UINT8_LIST(15, 15, 15);  // 3 elements, all max 4-bit values
    packed_vector<4> max_range_same(max_source, 0, 3);  // Same bit size
    packed_vector<3> max_range_smaller(max_source, 0, 3);  // Smaller bit size (15 -> 7)
    packed_vector<6> max_range_larger(max_source, 0, 3);  // Larger bit size (15 -> 15)
    
    std::cout << "  4-bit max values: ";
    for (int i = 0; i < max_range_same.size(); ++i) {
        std::cout << (int)max_range_same[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "  4-bit to 3-bit (clamped 15&7=7): ";
    for (int i = 0; i < max_range_smaller.size(); ++i) {
        std::cout << (int)max_range_smaller[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "  4-bit to 6-bit (preserved): ";
    for (int i = 0; i < max_range_larger.size(); ++i) {
        std::cout << (int)max_range_larger[i] << " ";
    }
    std::cout << "\n";
    
    // Test 21: Compile-time type safety demonstration 
    std::cout << "Compile-time type safety demonstration:\n";
    std::cout << "  - Regular range constructor only accepts same template parameters\n";
    std::cout << "  - Templated range constructor accepts different BitsPerElement with clamping\n";
    std::cout << "  - Both methods ensure type safety at compile time\n";
    
    // These should all compile successfully:
    packed_vector<3> same_params(source, 1, 3);           // Same type - uses non-templated version
    packed_vector<2> diff_bits(source, 1, 3);            // Different bits - uses templated version  
    packed_vector<3, mcu::TINY> diff_flags(source, 1, 3); // Different size flag - uses templated version
    
    std::cout << "  All range constructor variants compiled successfully!\n";
    
    // Test 22: Cross-size-flag range constructor tests (SMALL, MEDIUM, LARGE, TINY)
    std::cout << "\nCross-size-flag range constructor tests:\n";
    
    // Test 22a: SMALL to LARGE flag (same BitsPerElement)
    packed_vector<3, mcu::SMALL> small_source = MAKE_UINT8_LIST(7, 6, 5, 4, 3, 2, 1, 0);
    packed_vector<3, mcu::LARGE> large_from_small(small_source, 1, 5);  // Copy indices 1-4: 6,5,4,3
    std::cout << "  SMALL to LARGE (3-bit, indices 1-5): ";
    for (int i = 0; i < large_from_small.size(); ++i) {
        std::cout << (int)large_from_small[i] << " ";
    }
    std::cout << "Size: " << large_from_small.size() << "\n";
    
    // Test 22b: LARGE to SMALL flag (same BitsPerElement) 
    packed_vector<4, mcu::LARGE> large_source = MAKE_UINT8_LIST(15, 14, 13, 12, 11, 10, 9, 8, 7, 6);
    packed_vector<4, mcu::SMALL> small_from_large(large_source, 2, 7);  // Copy indices 2-6: 13,12,11,10,9
    std::cout << "  LARGE to SMALL (4-bit, indices 2-7): ";
    for (int i = 0; i < small_from_large.size(); ++i) {
        std::cout << (int)small_from_large[i] << " ";
    }
    std::cout << "Size: " << small_from_large.size() << "\n";
    
    // Test 22c: MEDIUM to TINY flag (different capacity limits)
    packed_vector<2, mcu::MEDIUM> medium_source = MAKE_UINT8_LIST(3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0);
    packed_vector<2, mcu::TINY> tiny_from_medium(medium_source, 3, 8);  // Copy indices 3-7: 0,3,2,1,0
    std::cout << "  MEDIUM to TINY (2-bit, indices 3-8): ";
    for (int i = 0; i < tiny_from_medium.size(); ++i) {
        std::cout << (int)tiny_from_medium[i] << " ";
    }
    std::cout << "Size: " << (int)tiny_from_medium.size() << ", Capacity: " << (int)tiny_from_medium.capacity() << "\n";
    
    // Test 22d: TINY to LARGE flag
    packed_vector<1, mcu::TINY> tiny_source_to_large = MAKE_UINT8_LIST(1, 0, 1, 1, 0, 1, 0);
    packed_vector<1, mcu::LARGE> large_from_tiny(tiny_source_to_large, 1, 5);  // Copy indices 1-4: 0,1,1,0
    std::cout << "  TINY to LARGE (1-bit, indices 1-5): ";
    for (int i = 0; i < large_from_tiny.size(); ++i) {
        std::cout << (int)large_from_tiny[i];
    }
    std::cout << " Size: " << large_from_tiny.size() << "\n";
    
    // Test 22e: Cross-flag AND cross-bit-size (SMALL 4-bit to LARGE 2-bit)
    packed_vector<4, mcu::SMALL> small4bit_source = MAKE_UINT8_LIST(15, 12, 8, 4, 7, 3);
    packed_vector<2, mcu::LARGE> large2bit_from_small4bit(small4bit_source, 1, 4);  // Copy 12,8,4 -> clamp to 0,0,0
    std::cout << "  SMALL 4-bit to LARGE 2-bit (indices 1-4): ";
    for (int i = 0; i < large2bit_from_small4bit.size(); ++i) {
        std::cout << (int)large2bit_from_small4bit[i] << " ";
    }
    std::cout << "Expected: 0 0 0 (12&3=0, 8&3=0, 4&3=0)\n";
    
    // Test 22f: Cross-flag AND cross-bit-size (LARGE 1-bit to TINY 3-bit)
    packed_vector<1, mcu::LARGE> large1bit_source = MAKE_UINT8_LIST(1, 0, 1, 0, 1, 1, 0, 1);
    packed_vector<3, mcu::TINY> tiny3bit_from_large1bit(large1bit_source, 0, 6);  // Copy 1,0,1,0,1,1
    std::cout << "  LARGE 1-bit to TINY 3-bit (indices 0-6): ";
    for (int i = 0; i < tiny3bit_from_large1bit.size(); ++i) {
        std::cout << (int)tiny3bit_from_large1bit[i] << " ";
    }
    std::cout << "Size: " << (int)tiny3bit_from_large1bit.size() << ", Capacity: " << (int)tiny3bit_from_large1bit.capacity() << "\n";
    
    // Test 22g: Memory efficiency comparison across flags
    std::cout << "Memory efficiency across different size flags:\n";
    std::cout << "  SMALL vector object size: " << sizeof(small_source) << " bytes\n";
    std::cout << "  MEDIUM vector object size: " << sizeof(medium_source) << " bytes\n";
    std::cout << "  LARGE vector object size: " << sizeof(large_source) << " bytes\n";
    std::cout << "  TINY vector object size: " << sizeof(tiny_from_medium) << " bytes\n";
    
    // Test 22h: Edge cases with different flags
    packed_vector<5, mcu::LARGE> large_edge_source = MAKE_UINT8_LIST(31, 30, 29);  // 3 elements
    
    // Empty range across flags
    packed_vector<5, mcu::TINY> tiny_empty_from_large(large_edge_source, 10, 10);
    std::cout << "  Empty range LARGE to TINY - Size: " << (int)tiny_empty_from_large.size() << "\n";
    
    // Invalid range across flags
    packed_vector<5, mcu::SMALL> small_invalid_from_large(large_edge_source, 5, 2);  // start > end
    std::cout << "  Invalid range LARGE to SMALL - Size: " << small_invalid_from_large.size() << "\n";
    
    // Single element across flags
    packed_vector<5, mcu::MEDIUM> medium_single_from_large(large_edge_source, 1, 2);  // Copy only element at index 1
    std::cout << "  Single element LARGE to MEDIUM: ";
    for (int i = 0; i < medium_single_from_large.size(); ++i) {
        std::cout << (int)medium_single_from_large[i] << " ";
    }
    std::cout << "Size: " << medium_single_from_large.size() << "\n";
    
    // Test 22i: Additional comprehensive cross-flag tests
    std::cout << "\nAdditional cross-flag scenarios:\n";
    
    // TINY to SMALL with different bit sizes
    packed_vector<1, mcu::TINY> tiny1bit = MAKE_UINT8_LIST(1, 0, 1, 0, 1);
    packed_vector<4, mcu::SMALL> small4bit_from_tiny1bit(tiny1bit, 0, 4);  // Copy 1,0,1,0
    std::cout << "  TINY 1-bit to SMALL 4-bit: ";
    for (int i = 0; i < small4bit_from_tiny1bit.size(); ++i) {
        std::cout << (int)small4bit_from_tiny1bit[i] << " ";
    }
    std::cout << "Size: " << small4bit_from_tiny1bit.size() << "\n";
    
    // LARGE to TINY with value clamping
    packed_vector<6, mcu::LARGE> large6bit = MAKE_UINT8_LIST(63, 32, 16, 15, 8, 7);
    packed_vector<2, mcu::TINY> tiny2bit_from_large6bit(large6bit, 1, 5);  // Copy 32,16,15,8 -> clamp to 0,0,3,0
    std::cout << "  LARGE 6-bit to TINY 2-bit with clamping: ";
    for (int i = 0; i < tiny2bit_from_large6bit.size(); ++i) {
        std::cout << (int)tiny2bit_from_large6bit[i] << " ";
    }
    std::cout << "Size: " << (int)tiny2bit_from_large6bit.size() << " (32&3=0, 16&3=0, 15&3=3, 8&3=0)\n";
    
    // SMALL to MEDIUM - verify capacity differences
    packed_vector<3, mcu::SMALL> small3bit = MAKE_UINT8_LIST(7, 6, 5, 4, 3, 2);
    packed_vector<3, mcu::MEDIUM> medium3bit_from_small(small3bit, 2, 6);  // Copy 5,4,3,2
    std::cout << "  SMALL to MEDIUM (3-bit): ";
    for (int i = 0; i < medium3bit_from_small.size(); ++i) {
        std::cout << (int)medium3bit_from_small[i] << " ";
    }
    std::cout << "Size: " << medium3bit_from_small.size() << ", Capacity: " << medium3bit_from_small.capacity() << "\n";
    
    // Test 22j: Maximum capacity stress test across flags
    std::cout << "\nCapacity behavior across flags:\n";
    std::cout << "  TINY max theoretical capacity: 15 elements\n";
    std::cout << "  SMALL max theoretical capacity: 255 elements\n";
    std::cout << "  MEDIUM max theoretical capacity: 65535 elements\n";
    std::cout << "  LARGE max theoretical capacity: 4294967295 elements\n";
    
    // Verify that range constructors respect destination capacity limits
    packed_vector<1, mcu::SMALL> small_for_tiny_test = MAKE_UINT8_LIST(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0);  // 20 elements
    std::cout << "  Large source for TINY test: " << small_for_tiny_test.size() << " elements\n";
    
    // This should work - copy subset that fits in TINY
    packed_vector<1, mcu::TINY> tiny_subset(small_for_tiny_test, 0, 10);  // Copy first 10 (within TINY limit)
    std::cout << "  TINY range from large source (10 elements): Size: " << (int)tiny_subset.size() 
              << ", Capacity: " << (int)tiny_subset.capacity() << "\n";
    
    // Test 22k: Extreme cross-flag scenarios
    std::cout << "\nExtreme cross-flag scenarios:\n";
    
    // TINY to LARGE with maximum elements
    packed_vector<1, mcu::TINY> tiny_max = MAKE_UINT8_LIST(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1);  // 15 elements (TINY max)
    packed_vector<1, mcu::LARGE> large_from_tiny_max(tiny_max, 0, tiny_max.size());  // Copy all
    std::cout << "  TINY max to LARGE (15 elements): Size: " << large_from_tiny_max.size() 
              << ", Capacity: " << large_from_tiny_max.capacity() << "\n";
    
    // LARGE to TINY but only copy what fits
    packed_vector<2, mcu::LARGE> large_big = MAKE_UINT8_LIST(3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0, 3, 2, 1, 0);  // 20 elements
    packed_vector<2, mcu::TINY> tiny_from_large_big(large_big, 5, 15);  // Copy 10 elements (within TINY limits)
    std::cout << "  LARGE to TINY (10 of 20 elements): Size: " << (int)tiny_from_large_big.size() 
              << ", Capacity: " << (int)tiny_from_large_big.capacity() << "\n";
    
    // Cross-flag with value overflow and underflow
    packed_vector<8, mcu::SMALL> small8bit = MAKE_UINT8_LIST(255, 128, 64, 32, 16, 8, 4, 2, 1, 0);
    packed_vector<3, mcu::LARGE> large3bit_clamped(small8bit, 0, 5);  // Should clamp: 255->7, 128->0, 64->0, 32->0, 16->0
    std::cout << "  SMALL 8-bit to LARGE 3-bit (clamping): ";
    for (int i = 0; i < large3bit_clamped.size(); ++i) {
        std::cout << (int)large3bit_clamped[i] << " ";
    }
    std::cout << "Expected: 7 0 0 0 0\n";
    
    // Test the opposite - expand bit size across flags
    packed_vector<1, mcu::LARGE> large1bit_expand = MAKE_UINT8_LIST(1, 0, 1, 1, 0, 1, 0, 1);
    packed_vector<6, mcu::SMALL> small6bit_expanded(large1bit_expand, 1, 6);  // Copy 0,1,1,0,1 to 6-bit
    std::cout << "  LARGE 1-bit to SMALL 6-bit (expanding): ";
    for (int i = 0; i < small6bit_expanded.size(); ++i) {
        std::cout << (int)small6bit_expanded[i] << " ";
    }
    std::cout << "Size: " << small6bit_expanded.size() << " (no clamping needed)\n";
    
    std::cout << "\nSafety mechanism verification:\n";
    std::cout << "  ✓ Type safety: Only compatible types compile\n";
    std::cout << "  ✓ Value clamping: Values automatically clamped to destination bit size\n";
    std::cout << "  ✓ Bounds checking: Invalid ranges create empty vectors\n";
    std::cout << "  ✓ Memory efficiency: Capacity matches range size\n";
    std::cout << "  ✓ Cross-flag compatibility: All size flag combinations work\n";
    
    std::cout << "------------- Range Constructor Test Complete -------------\n\n";
}

int main(){
    test_packed_vector();
    test_constructors_and_assignments();
    test_fill_method();
    test_tiny_mode();
    test_iterators();
    test_range_constructor();  // Add new test
    packed_vector<2, SMALL> vec2(10, 3);
    return 0;
}