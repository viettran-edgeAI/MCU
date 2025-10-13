#include <iostream>
#include "../../../src/STL_MCU.h"
// #include "packed_vector.h"
#include <cstdint>

using namespace mcu;

using packed_vector_1bit = packed_vector<1>;
using packed_vector_2bit = packed_vector<2>;
using packed_vector_4bit = packed_vector<4>;
using packed_vector_1bit_tiny = packed_vector<1, mcu::TINY>;
using packed_vector_2bit_tiny = packed_vector<2, mcu::TINY>;
using packed_vector_4bit_tiny = packed_vector<4, mcu::TINY>;

// Test counter
int total_tests = 0;
int passed_tests = 0;

#define TEST(name) { \
    total_tests++; \
    bool test_passed = true; \
    std::string test_name = name; \
    try {

#define EXPECT(condition) \
    if (!(condition)) { \
        test_passed = false; \
    }

#define END_TEST \
    } catch (...) { \
        test_passed = false; \
    } \
    if (test_passed) { \
        std::cout << "  [PASS] " << test_name << "\n"; \
        passed_tests++; \
    } else { \
        std::cout << "  [FAIL] " << test_name << "\n"; \
    } \
}

void test_constructors_and_assignments() {
    std::cout << "------------- Constructor & Assignment Tests -------------\n";
    
    TEST("Default constructor") {
        packed_vector<3> v1;
        EXPECT(v1.size() == 0);
        EXPECT(v1.capacity() >= 0);
    } END_TEST

    TEST("Constructor with capacity") {
        packed_vector<3> v2(5);
        EXPECT(v2.size() == 0);
        EXPECT(v2.capacity() >= 5);
    } END_TEST

    TEST("Constructor with size and value") {
        packed_vector<3> v3(4, 7);
        EXPECT(v3.size() == 4);
        EXPECT(v3[0] == 7 && v3[1] == 7 && v3[2] == 7 && v3[3] == 7);
    } END_TEST

    TEST("Custom initializer list constructor") {
        auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 0}, 8);
        packed_vector<3> v4(init_list);
        EXPECT(v4.size() == 8);
        EXPECT(v4[0] == 1 && v4[1] == 2 && v4[2] == 3);
    } END_TEST

    TEST("Macro initialization") {
        packed_vector<3> v4b = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        EXPECT(v4b.size() == 8);
        EXPECT(v4b[0] == 1 && v4b[1] == 2 && v4b[2] == 3);
    } END_TEST

    TEST("Copy constructor") {
        auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 0}, 8);
        packed_vector<3> v4(init_list);
        packed_vector<3> v5(v4);
        EXPECT(v5.size() == v4.size());
        EXPECT(v5[0] == v4[0] && v5[1] == v4[1]);
    } END_TEST

    TEST("Move constructor") {
        packed_vector<3> v3(4, 7);
        size_t original_size = v3.size();
        packed_vector<3> v6(std::move(v3));
        EXPECT(v6.size() == original_size);
        EXPECT(v3.size() == 0);
    } END_TEST

    TEST("Copy assignment") {
        auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 0}, 8);
        packed_vector<3> v4(init_list);
        packed_vector<3> v7;
        v7 = v4;
        EXPECT(v7.size() == v4.size());
        EXPECT(v7[0] == v4[0]);
    } END_TEST

    TEST("Move assignment") {
        packed_vector<3> v6(4, 7);
        size_t original_size = v6.size();
        packed_vector<3> v8;
        v8 = std::move(v6);
        EXPECT(v8.size() == original_size);
        EXPECT(v6.size() == 0);
    } END_TEST

    TEST("Self-assignment (copy)") {
        auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5, 6, 7, 0}, 8);
        packed_vector<3> v7(init_list);
        size_t original_size = v7.size();
        v7 = v7;
        EXPECT(v7.size() == original_size);
    } END_TEST

    TEST("Assign with count and value") {
        packed_vector<3> v1;
        v1.assign(6, 5);
        EXPECT(v1.size() == 6);
        EXPECT(v1[0] == 5 && v1[5] == 5);
    } END_TEST

    TEST("Assign with custom initializer list") {
        auto assign_list = mcu::min_init_list<uint8_t>((const uint8_t[]){7, 6, 5, 4, 3}, 5);
        packed_vector<3> v2(5);
        v2.assign(assign_list);
        EXPECT(v2.size() == 5);
        EXPECT(v2[0] == 7 && v2[4] == 3);
    } END_TEST

    TEST("Empty custom initializer list") {
        auto empty_list = mcu::min_init_list<uint8_t>(nullptr, 0);
        packed_vector<3> v9(empty_list);
        EXPECT(v9.size() == 0);
    } END_TEST

    TEST("Large value clamping in constructors") {
        packed_vector<2> v10(3, 255);  // Should clamp 255 to 3 (2-bit max)
        EXPECT(v10.size() == 3);
        EXPECT(v10[0] == v10.max_value());
    } END_TEST

    TEST("Memory efficiency comparison") {
        packed_vector<1> v1bit(8, 1);
        packed_vector<2> v2bit(8, 3);
        packed_vector<4> v4bit(8, 15);
        EXPECT(v1bit.memory_usage() <= v2bit.memory_usage());
        EXPECT(v2bit.memory_usage() <= v4bit.memory_usage());
        EXPECT(v4bit.memory_usage() <= 8 * sizeof(uint8_t));
    } END_TEST
}

void test_packed_vector() {
    std::cout << "------------- Packed Vector Test -------------\n";
    
    TEST("Custom initializer list constructor") {
        auto init_list = mcu::min_init_list<uint8_t>((const uint8_t[]){0, 1, 2, 3, 0, 1, 2, 3}, 8);
        packed_vector<2> packed2_init(init_list);
        EXPECT(packed2_init.size() == 8);
        EXPECT(packed2_init[0] == 0 && packed2_init[1] == 1);
    } END_TEST

    TEST("Resize functionality") {
        packed_vector<4> packed4;
        packed4.resize(5, 10);
        EXPECT(packed4.size() == 5);
        EXPECT(packed4[0] == 10 && packed4[4] == 10);
    } END_TEST

    TEST("Front/back access") {
        packed_vector<4> packed4;
        packed4.resize(5, 10);
        EXPECT(!packed4.empty());
        EXPECT(packed4.front() == 10 && packed4.back() == 10);
    } END_TEST

    TEST("Assign functionality") {
        auto assign_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5}, 5);
        packed_vector<4> packed4;
        packed4.assign(assign_list);
        EXPECT(packed4.size() == 5);
        EXPECT(packed4[0] == 1 && packed4[4] == 5);
    } END_TEST

    TEST("Vector comparison") {
        auto assign_list = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 2, 3, 4, 5}, 5);
        packed_vector<4> packed4;
        packed4.assign(assign_list);
        packed_vector<4> packed4_copy = packed4;
        EXPECT(packed4 == packed4_copy);
    } END_TEST

    TEST("2-bit packed vector basic operations") {
        packed_vector<2> packed2(10, 3);
        EXPECT(packed2.max_value() == 3);
        for (int i = 0; i < 10; ++i) {
            packed2.set(i, i % 4);
        }
        EXPECT(packed2[0] == 0 && packed2[1] == 1);
        EXPECT(packed2.memory_usage() > 0);
    } END_TEST

    TEST("1-bit packed vector (boolean-like)") {
        packed_vector<1> packed1(8, 1);
        EXPECT(packed1.size() == 8);
        EXPECT(packed1[0] == 1);
        EXPECT(packed1.memory_usage() > 0);
    } END_TEST
}

void test_fill_method() {
    std::cout << "------------- Fill Method Test -------------\n";
    
    TEST("Fill 2-bit vector with max value") {
        packed_vector<2> vec2bit(8);
        vec2bit.resize(8, 0);
        vec2bit.fill(3);
        EXPECT(vec2bit.size() == 8);
        EXPECT(vec2bit[0] == 3 && vec2bit[7] == 3);
    } END_TEST

    TEST("Fill 4-bit vector with mid-range value") {
        packed_vector<4> vec4bit = MAKE_UINT8_LIST(4, 1, 2, 3, 4, 5, 6);
        vec4bit.fill(10);
        EXPECT(vec4bit.size() == 6);
        EXPECT(vec4bit[0] == 10 && vec4bit[5] == 10);
    } END_TEST

    TEST("Fill 1-bit vector (boolean-like)") {
        packed_vector<1> vec1bit(12, 0);
        vec1bit.fill(1);
        EXPECT(vec1bit.size() == 12);
        EXPECT(vec1bit[0] == 1 && vec1bit[11] == 1);
    } END_TEST

    TEST("Fill with value exceeding bit limit (clamping)") {
        packed_vector<3> vec3bit(5, 0);
        vec3bit.fill(255); // Should be clamped to 7 (max 3-bit value)
        EXPECT(vec3bit.size() == 5);
        EXPECT(vec3bit[0] == vec3bit.max_value());
    } END_TEST

    TEST("Fill empty vector") {
        packed_vector<2> vecEmpty;
        size_t original_size = vecEmpty.size();
        vecEmpty.fill(2);
        EXPECT(vecEmpty.size() == original_size); // Should remain empty
    } END_TEST

    TEST("Fill large vector") {
        packed_vector<6> vecLarge(100, 0);
        vecLarge.fill(63); // Max 6-bit value
        EXPECT(vecLarge.size() == 100);
        EXPECT(vecLarge[0] == 63 && vecLarge[99] == 63);
    } END_TEST
}

void test_tiny_mode() {
    std::cout << "------------- TINY Mode Test -------------\n";
    
    TEST("TINY default constructor") {
        packed_vector<2, mcu::TINY> tiny_vec;
        EXPECT(tiny_vec.size() == 0);
        EXPECT(tiny_vec.capacity() >= 0);
    } END_TEST

    TEST("TINY constructor with capacity") {
        packed_vector<3, mcu::TINY> tiny_vec2(8);
        EXPECT(tiny_vec2.size() == 0);
        EXPECT(tiny_vec2.capacity() >= 8);
    } END_TEST

    TEST("TINY constructor with size and value") {
        packed_vector<2, mcu::TINY> tiny_vec3(5, 3);
        EXPECT(tiny_vec3.size() == 5);
        EXPECT(tiny_vec3[0] == 3 && tiny_vec3[4] == 3);
    } END_TEST

    TEST("TINY type aliases") {
        packed_vector_2bit_tiny tiny_alias(4, 2);
        EXPECT(tiny_alias.size() == 4);
        EXPECT(tiny_alias[0] == 2 && tiny_alias[3] == 2);
    } END_TEST

    TEST("TINY push_back operations") {
        packed_vector<1, mcu::TINY> tiny_vec4;
        for (int i = 0; i < 12; ++i) {
            tiny_vec4.push_back(i % 2);
        }
        EXPECT(tiny_vec4.size() == 12);
    } END_TEST

    TEST("TINY vs MEDIUM memory comparison") {
        packed_vector<4, mcu::TINY> tiny_4bit(10, 15);
        packed_vector<4, mcu::MEDIUM> medium_4bit(10, 15);
        EXPECT(sizeof(tiny_4bit) <= sizeof(medium_4bit));
    } END_TEST

    TEST("TINY copy and move operations") {
        packed_vector<3, mcu::TINY> tiny_original(6, 5);
        packed_vector<3, mcu::TINY> tiny_copy(tiny_original);
        packed_vector<3, mcu::TINY> tiny_moved(std::move(tiny_original));
        EXPECT(tiny_copy.size() == 6);
        EXPECT(tiny_moved.size() == 6);
        EXPECT(tiny_original.size() == 0);
    } END_TEST

    TEST("TINY resize operations") {
        packed_vector<2, mcu::TINY> tiny_resize(3, 1);
        tiny_resize.resize(8, 2);
        EXPECT(tiny_resize.size() == 8);
        EXPECT(tiny_resize[2] == 1 && tiny_resize[7] == 2);
    } END_TEST

    TEST("TINY maximum capacity test") {
        packed_vector<1, mcu::TINY> tiny_max;
        for (int i = 0; i < 20; ++i) {
            tiny_max.push_back(1);
            if (tiny_max.capacity() >= 15) break;
        }
        EXPECT(tiny_max.capacity() <= 15);
    } END_TEST

    TEST("TINY initializer list") {
        auto tiny_init = mcu::min_init_list<uint8_t>((const uint8_t[]){1, 0, 1, 1, 0}, 5);
        packed_vector<1, mcu::TINY> tiny_from_list(tiny_init);
        EXPECT(tiny_from_list.size() == 5);
        EXPECT(tiny_from_list[0] == 1 && tiny_from_list[4] == 0);
    } END_TEST

    TEST("TINY fill operation") {
        packed_vector<3, mcu::TINY> tiny_fill(7, 0);
        tiny_fill.fill(6);
        EXPECT(tiny_fill.size() == 7);
        EXPECT(tiny_fill[0] == 6 && tiny_fill[6] == 6);
    } END_TEST

    TEST("TINY vector comparison") {
        packed_vector<2, mcu::TINY> tiny_a(4, 3);
        packed_vector<2, mcu::TINY> tiny_b(4, 3);
        packed_vector<2, mcu::TINY> tiny_c(4, 2);
        EXPECT(tiny_a == tiny_b);
        EXPECT(!(tiny_a == tiny_c));
    } END_TEST
}

void test_iterators() {
    std::cout << "------------- Iterator Test -------------\n";
    
    TEST("Basic iterator functionality") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        EXPECT(vec.size() == 8);
        EXPECT(vec[0] == 1 && vec[7] == 0);
    } END_TEST

    TEST("Range-based for loop") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        int count = 0;
        for (const auto& val : vec) {
            count++;
        }
        EXPECT(count == vec.size());
    } END_TEST

    TEST("Iterator traversal") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        int count = 0;
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            count++;
        }
        EXPECT(count == vec.size());
    } END_TEST

    TEST("Const iterator") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        const packed_vector<3>& const_vec = vec;
        int count = 0;
        for (auto it = const_vec.cbegin(); it != const_vec.cend(); ++it) {
            count++;
        }
        EXPECT(count == vec.size());
    } END_TEST

    TEST("Iterator arithmetic") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        auto it = vec.begin();
        it += 3;
        EXPECT(it.get_index() == 3);
        it -= 1;
        EXPECT(it.get_index() == 2);
        auto it2 = it + 2;
        EXPECT(it2.get_index() == 4);
    } END_TEST

    TEST("Iterator distance") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        auto distance = vec.end() - vec.begin();
        EXPECT(distance == vec.size());
    } END_TEST

    TEST("Iterator comparison") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        auto begin_it = vec.begin();
        auto end_it = vec.end();
        EXPECT(!(begin_it == end_it));
        EXPECT(begin_it != end_it);
        EXPECT(begin_it < end_it);
    } END_TEST

    TEST("Empty vector iterators") {
        packed_vector<2> empty_vec;
        EXPECT(empty_vec.begin() == empty_vec.end());
        int count = 0;
        for (const auto& val : empty_vec) {
            count++;
        }
        EXPECT(count == 0);
    } END_TEST

    TEST("Different bit sizes iterators") {
        packed_vector<1> vec1bit = MAKE_UINT8_LIST(1, 1, 0, 1, 0, 1);
        packed_vector<4> vec4bit = MAKE_UINT8_LIST(4, 15, 14, 13, 12, 11);
        EXPECT(vec1bit.size() == 6);
        EXPECT(vec4bit.size() == 5);
    } END_TEST

    TEST("Backward iteration") {
        packed_vector<3> vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
        auto rev_it = vec.end();
        int count = 0;
        while (rev_it != vec.begin()) {
            --rev_it;
            count++;
        }
        EXPECT(count == vec.size());
    } END_TEST

    TEST("Large vector iterators") {
        packed_vector<6> large_vec(20, 63);
        int count = 0;
        for (const auto& val : large_vec) {
            if (count >= 5) break;
            count++;
        }
        EXPECT(count == 5);
    } END_TEST
}

void test_range_constructor() {
    std::cout << "------------- Range Constructor Test -------------\n";
    
    TEST("Basic range copy from middle") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range1(source, 2, 6);  // Copy elements at indices 2,3,4,5
        EXPECT(range1.size() == 4);
        EXPECT(range1[0] == 2 && range1[3] == 5);
    } END_TEST

    TEST("Copy from beginning") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range2(source, 0, 3);  // Copy first 3 elements
        EXPECT(range2.size() == 3);
        EXPECT(range2[0] == 0 && range2[2] == 2);
    } END_TEST

    TEST("Copy to end") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range3(source, 5, source.size());  // Copy last 3 elements
        EXPECT(range3.size() == 3);
        EXPECT(range3[0] == 5 && range3[2] == 7);
    } END_TEST

    TEST("Copy entire vector") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range4(source, 0, source.size());
        EXPECT(range4.size() == source.size());
        EXPECT(range4[0] == source[0] && range4[7] == source[7]);
    } END_TEST

    TEST("Single element copy") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range5(source, 3, 4);  // Copy only element at index 3
        EXPECT(range5.size() == 1);
        EXPECT(range5[0] == 3);
    } END_TEST

    TEST("Invalid range (start > end)") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range6(source, 5, 3);  // start > end
        EXPECT(range6.size() == 0);
    } END_TEST

    TEST("Invalid range (start >= size)") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range7(source, 20, 25);  // start >= source.size()
        EXPECT(range7.size() == 0);
    } END_TEST

    TEST("Range with end > size (clamping)") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range8(source, 6, 20);  // end > source.size() (should clamp)
        EXPECT(range8.size() == 2);  // Should only copy elements 6 and 7
    } END_TEST

    TEST("Empty range (start == end)") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range9(source, 4, 4);
        EXPECT(range9.size() == 0);
    } END_TEST

    TEST("Different bit sizes range") {
        packed_vector<1> source1bit = MAKE_UINT8_LIST(1, 1, 0, 1, 1, 0, 0, 1, 0);
        packed_vector<1> range1bit(source1bit, 2, 7);
        EXPECT(range1bit.size() == 5);
        
        packed_vector<4> source4bit = MAKE_UINT8_LIST(4, 15, 14, 13, 12, 11, 10, 9, 8);
        packed_vector<4> range4bit(source4bit, 1, 5);
        EXPECT(range4bit.size() == 4);
    } END_TEST

    TEST("TINY mode range constructor") {
        packed_vector<2, mcu::TINY> tiny_source = MAKE_UINT8_LIST(2, 3, 2, 1, 0, 3, 2, 1, 0);
        packed_vector<2, mcu::TINY> tiny_range(tiny_source, 2, 6);
        EXPECT(tiny_range.size() == 4);
    } END_TEST

    TEST("Range constructor with operations") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range_ops(source, 1, 4);  // Copy elements 1,2,3
        range_ops.push_back(0);
        range_ops.resize(6, 7);
        EXPECT(range_ops.size() == 6);
    } END_TEST

    TEST("Cross-bit-size range constructor") {
        // 4-bit to 2-bit (values should be clamped)
        packed_vector<4> source4to2 = MAKE_UINT8_LIST(15, 14, 13, 12, 4, 3, 2, 1);
        packed_vector<2> range4to2(source4to2, 1, 5);  // Copy indices 1-4
        EXPECT(range4to2.size() == 4);
        
        // 2-bit to 4-bit (values should fit perfectly)
        packed_vector<2> source2to4 = MAKE_UINT8_LIST(3, 2, 1, 0, 3, 2, 1);
        packed_vector<4> range2to4(source2to4, 2, 6);  // Copy indices 2-5
        EXPECT(range2to4.size() == 4);
    } END_TEST

    TEST("Cross-size-flag range constructor") {
        // TINY to MEDIUM
        packed_vector<2, mcu::TINY> tiny_source_cross = MAKE_UINT8_LIST(3, 2, 1, 0, 3);
        packed_vector<2, mcu::MEDIUM> medium_from_tiny(tiny_source_cross, 1, 4);
        EXPECT(medium_from_tiny.size() == 3);
        
        // SMALL to LARGE
        packed_vector<3, mcu::SMALL> small_source = MAKE_UINT8_LIST(7, 6, 5, 4, 3, 2, 1, 0);
        packed_vector<3, mcu::LARGE> large_from_small(small_source, 1, 5);
        EXPECT(large_from_small.size() == 4);
    } END_TEST

    TEST("Range comparison") {
        packed_vector<3> source = MAKE_UINT8_LIST(3, 0, 1, 2, 3, 4, 5, 6, 7);
        packed_vector<3> range_comp1(source, 2, 5);
        packed_vector<3> range_comp2(source, 2, 5);
        packed_vector<3> range_comp3(source, 2, 6);
        EXPECT(range_comp1 == range_comp2);
        EXPECT(!(range_comp1 == range_comp3));
    } END_TEST
}

void test_dynamic_bits_per_value() {
    std::cout << "------------- Dynamic Bits Per Value Tests -------------\n";
    
    TEST("Default bpv equals template parameter") {
        packed_vector<3> v1;
        EXPECT(v1.get_bits_per_value() == 3);
    } END_TEST

    TEST("Add values with default bpv") {
        packed_vector<3> v1;
        v1.push_back(7);  // max value for 3 bits
        v1.push_back(5);
        v1.push_back(2);
        EXPECT(v1.size() == 3);
        EXPECT(v1[0] == 7 && v1[1] == 5 && v1[2] == 2);
    } END_TEST

    TEST("Change bpv clears data") {
        packed_vector<3> v1;
        v1.push_back(7);
        v1.push_back(5);
        v1.set_bits_per_value(2);
        EXPECT(v1.get_bits_per_value() == 2);
        EXPECT(v1.size() == 0);  // data cleared
    } END_TEST

    TEST("Add values with new bpv") {
        packed_vector<3> v1;
        v1.set_bits_per_value(2);
        v1.push_back(3);  // max value for 2 bits
        v1.push_back(2);
        v1.push_back(1);
        v1.push_back(0);
        EXPECT(v1.size() == 4);
        EXPECT(v1[0] == 3 && v1[3] == 0);
    } END_TEST

    TEST("Value clamping with dynamic bpv") {
        packed_vector<3> v1;
        v1.set_bits_per_value(2);
        v1.push_back(15);  // Will be clamped to 3 (0b11) for 2-bit
        EXPECT(v1.back() == 3);  // clamped
    } END_TEST

    TEST("Multiple bpv changes") {
        packed_vector<4> v2(5, 15);  // Start with 5 elements, value 15
        size_t original_size = v2.size();
        v2.set_bits_per_value(6);  // Change to 6 bits
        EXPECT(v2.get_bits_per_value() == 6);
        EXPECT(v2.size() == 0);  // data cleared
    } END_TEST

    TEST("TINY mode with dynamic bpv") {
        packed_vector<4, mcu::TINY> v3;
        EXPECT(v3.get_bits_per_value() == 4);
        v3.push_back(15);
        v3.push_back(10);
        v3.set_bits_per_value(3);
        v3.push_back(7);  // max for 3 bits
        v3.push_back(4);
        EXPECT(v3.size() == 2);  // only new values after bpv change
    } END_TEST

    TEST("Invalid bpv values ignored") {
        packed_vector<3> v4;
        v4.push_back(7);
        v4.set_bits_per_value(0);  // Invalid
        EXPECT(v4.get_bits_per_value() == 3);  // unchanged
        
        v4.set_bits_per_value(9);  // Invalid
        EXPECT(v4.get_bits_per_value() == 3);  // unchanged
    } END_TEST

    TEST("Memory efficiency with dynamic bpv") {
        packed_vector<8> v5(10, 255);  // 10 bytes
        size_t mem_8bit = v5.memory_usage();
        
        v5.set_bits_per_value(4);
        v5.resize(10, 15);
        size_t mem_4bit = v5.memory_usage();
        
        v5.set_bits_per_value(2);
        v5.resize(10, 3);
        size_t mem_2bit = v5.memory_usage();
        
        EXPECT(mem_2bit <= mem_4bit && mem_4bit <= mem_8bit);
    } END_TEST

    TEST("Fill with dynamic bpv") {
        packed_vector<5> v6(8, 0);
        v6.fill(31);  // max for 5 bits
        EXPECT(v6[0] == 31 && v6[7] == 31);
        
        v6.set_bits_per_value(3);
        v6.resize(8, 0);
        v6.fill(7);  // max for 3 bits
        EXPECT(v6[0] == 7 && v6[7] == 7);
    } END_TEST
}

int main(){
    test_packed_vector();
    test_constructors_and_assignments();
    test_fill_method();
    test_tiny_mode();
    test_iterators();
    test_range_constructor();
    test_dynamic_bits_per_value();
    
    // Print summary
    std::cout << "===============================================\n";
    std::cout << "TEST SUMMARY\n";
    std::cout << "===============================================\n";
    std::cout << "Total tests: " << total_tests << "\n";
    std::cout << "Passed tests: " << passed_tests << "\n";
    std::cout << "Failed tests: " << (total_tests - passed_tests) << "\n";
    std::cout << "Success rate: " << (passed_tests * 100 / total_tests) << "%\n";
    std::cout << "===============================================\n";
    
    return (passed_tests == total_tests) ? 0 : 1;
}