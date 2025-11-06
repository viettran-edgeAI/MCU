#include <iostream>
// #include "../../../src/STL_MCU.h"
#include "packed_vector.h"
#include <cstdint>

using namespace mcu;

struct Tree_node {
    uint32_t packed_data = 0;

    inline uint16_t getFeatureID() const { return getBits(0, 8); }
    inline uint8_t getLabel() const { return static_cast<uint8_t>(getBits(8, 5)); }
    inline uint8_t getThresholdSlot() const { return static_cast<uint8_t>(getBits(13, 2)); }
    inline bool getIsLeaf() const { return getBits(15, 1) != 0; }
    inline uint16_t getLeftChildIndex() const { return getBits(16, 8); }
    inline uint16_t getRightChildIndex() const { return static_cast<uint16_t>(getLeftChildIndex() + 1); }

    inline void setFeatureID(uint16_t v) { setBits(0, 8, v); }
    inline void setLabel(uint8_t v) { setBits(8, 5, v); }
    inline void setThresholdSlot(uint8_t v) { setBits(13, 2, v); }
    inline void setIsLeaf(bool v) { setBits(15, 1, v ? 1u : 0u); }
    inline void setLeftChildIndex(uint16_t v) { setBits(16, 8, v); }

private:
    inline uint32_t getBits(uint8_t pos, uint8_t len) const {
        return (packed_data >> pos) & ((1u << len) - 1u);
    }

    inline void setBits(uint8_t pos, uint8_t len, uint32_t val) {
        const uint32_t mask = ((1u << len) - 1u) << pos;
        packed_data = (packed_data & ~mask) | ((val << pos) & mask);
    }
};

using packed_vector_1bit = packed_vector<1>;
using packed_vector_2bit = packed_vector<2>;
using packed_vector_4bit = packed_vector<4>;

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

void test_wide_bit_operations() {
    std::cout << "------------- Wide Bit Operations Test -------------\n";

    TEST("16-bit storage and retrieval") {
        packed_vector<16> vec16;
        vec16.push_back(65535);
        vec16.push_back(12345);
        EXPECT(vec16.size() == 2);
        EXPECT(vec16[0] == 65535 && vec16[1] == 12345);
    } END_TEST

    TEST("Initializer list with 12-bit values") {
        auto init_list = MAKE_UINT16_LIST(1023, 2047, 4095, 2048, 0);
        packed_vector<12> vec12(init_list);
        EXPECT(vec12.size() == 5);
        EXPECT(vec12[0] == 1023 && vec12[2] == 4095);
    } END_TEST

    TEST("Clamping values beyond 16-bit range") {
        packed_vector<16> vec16;
        vec16.push_back(70000);  // exceeds 16-bit max
        EXPECT(vec16.size() == 1);
        EXPECT(vec16[0] == (70000 & 0xFFFF));
    } END_TEST

    TEST("Runtime bpv expansion above 8 bits") {
        packed_vector<16> vecRuntime;
        vecRuntime.set_bits_per_value(12);
        vecRuntime.push_back(4095);
        vecRuntime.push_back(5000);  // clamps to 4095 with 12-bit limit
    EXPECT(vecRuntime.size() == 2);
    EXPECT(vecRuntime[0] == 4095 && vecRuntime[1] == (5000 & ((1 << 12) - 1)));
    } END_TEST

    TEST("Memory usage scales with wider bits") {
        packed_vector<16> wide_vec(10, 0xFFFF);
        packed_vector<4> narrow_vec(10, 0xF);
        EXPECT(wide_vec.memory_usage() >= narrow_vec.memory_usage());
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

void test_custom_type_support() {
    std::cout << "------------- Custom Type Support Test -------------\n";

    TEST("Tree_node storage and retrieval") {
        mcu::packed_vector<24, Tree_node> nodes;
        Tree_node root;
        root.setFeatureID(42);
        root.setLabel(17);
        root.setThresholdSlot(1);
        root.setIsLeaf(false);
        root.setLeftChildIndex(7);

        nodes.push_back(root);

        const Tree_node retrieved = nodes[0];
        EXPECT(retrieved.getFeatureID() == 42);
        EXPECT(retrieved.getLabel() == 17);
        EXPECT(retrieved.getThresholdSlot() == 1);
        EXPECT(!retrieved.getIsLeaf());
        EXPECT(retrieved.getLeftChildIndex() == 7);
        EXPECT(retrieved.getRightChildIndex() == 8);
    } END_TEST

    TEST("Tree_node bit clamping") {
        mcu::packed_vector<24, Tree_node> nodes(2, Tree_node{});
        Tree_node noisy;
        noisy.packed_data = 0xFFFFFFFFu; // deliberately exceed 24 bits
        nodes.set(1, noisy);

        const Tree_node stored = nodes.get(1);
        EXPECT((stored.packed_data & 0xFFFFFFu) == stored.packed_data);
        EXPECT(stored.packed_data == 0xFFFFFFu);
    EXPECT((mcu::packed_vector<24, Tree_node>::max_bits_value() == 0xFFFFFFu));
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

    TEST("Invalid bpv values ignored") {
        packed_vector<12> v4;
        v4.push_back(11);
        v4.set_bits_per_value(0);  // invalid
        EXPECT(v4.get_bits_per_value() == 12);

        v4.set_bits_per_value(10);  // valid reduction
        EXPECT(v4.get_bits_per_value() == 10);

    v4.set_bits_per_value(40);  // exceeds supported range, ignored
    EXPECT(v4.get_bits_per_value() == 10);
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

void test_runtime_bpv_memory_safety() {
    std::cout << "------------- Runtime BPV Memory Safety Tests -------------\n";

    TEST("Range constructor retains runtime bpv") {
        packed_vector<4> source;
        source.set_bits_per_value(3);
        for (uint8_t i = 0; i < 10; ++i) {
            source.push_back(i & 0x7);
        }
        packed_vector<4> slice(source, 2, 9);
        EXPECT(slice.size() == 7);
        EXPECT(slice.get_bits_per_value() == source.get_bits_per_value());
        EXPECT(slice[0] == 2 && slice[6] == 0);  // index 8 in source is 8 & 0x7 = 0
        EXPECT(slice.capacity() >= slice.size());
    } END_TEST

    TEST("Cross-type range clamps runtime bpv to destination") {
        packed_vector<4> source;
        source.set_bits_per_value(3);
        for (uint8_t i = 0; i < 6; ++i) {
            source.push_back(static_cast<uint8_t>(i + 4));
        }
        packed_vector<2> dest(source, 0, source.size());
        uint8_t expected_bpv = source.get_bits_per_value();
        if (expected_bpv > dest.bits_per_element()) {
            expected_bpv = dest.bits_per_element();
        }
        EXPECT(dest.size() == source.size());
        EXPECT(dest.get_bits_per_value() == expected_bpv);
        // Last value in source is (5+4) & 0x7 = 1, then clamped to 2-bit: 1 & 0x3 = 1
        EXPECT(dest.back() == 1);
    } END_TEST

    TEST("Reserve after runtime bpv change preserves values") {
        packed_vector<5> vec;
        vec.set_bits_per_value(3);
        for (uint8_t i = 0; i < 6; ++i) {
            vec.push_back(i & 0x7);
        }
        auto original_bits = vec.get_bits_per_value();
        auto original_values_ok = (vec[0] == 0 && vec[5] == 5);
        vec.reserve(vec.capacity() + 8);
        EXPECT(vec.get_bits_per_value() == original_bits);
        EXPECT(original_values_ok);
        EXPECT(vec.size() == 6);
        EXPECT(vec[5] == 5);
    } END_TEST

    TEST("Fit after runtime bpv change shrinks capacity") {
        packed_vector<6> vec;
        vec.set_bits_per_value(4);
        for (uint8_t i = 0; i < 12; ++i) {
            vec.push_back(static_cast<uint8_t>(i & 0xF));
        }
        vec.pop_back();
        auto expected_size = vec.size();
        vec.fit();
        EXPECT(vec.get_bits_per_value() == 4);
        EXPECT(vec.size() == expected_size);
        EXPECT(vec.capacity() == (expected_size == 0 ? 1 : expected_size));
        EXPECT(vec.back() == ((expected_size == 0) ? 0 : static_cast<uint8_t>((expected_size - 1) & 0xF)));
    } END_TEST

    TEST("Move assignment retains runtime bpv metadata") {
        packed_vector<5> source;
        source.set_bits_per_value(3);
        source.push_back(5);
        uint8_t runtime_bits = source.get_bits_per_value();
        packed_vector<5> dest;
        dest = std::move(source);
        EXPECT(dest.get_bits_per_value() == runtime_bits);
        EXPECT(dest.size() == 1);
        EXPECT(dest[0] == 5);
        EXPECT(source.size() == 0);
    } END_TEST
}

int main(){
    test_packed_vector();
    test_constructors_and_assignments();
    test_fill_method();
    test_wide_bit_operations();
    test_iterators();
    test_range_constructor();
    test_dynamic_bits_per_value();
    test_runtime_bpv_memory_safety();
    test_custom_type_support();
    
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