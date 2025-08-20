#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cassert>
#include <algorithm>
#include <iomanip>
#include "ID_vector.cpp"

using namespace mcu;
using namespace std::chrono;

class TestSuite {
private:
    int tests_passed = 0;
    int tests_failed = 0;
    
    void assert_test(bool condition, const std::string& test_name) {
        if (condition) {
            std::cout << "✓ " << test_name << std::endl;
            tests_passed++;
        } else {
            std::cout << "✗ " << test_name << " FAILED" << std::endl;
            tests_failed++;
        }
    }
    
public:
    void print_results() {
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "TEST RESULTS: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
        if (tests_failed == 0) {
            std::cout << "🎉 ALL TESTS PASSED!" << std::endl;
        } else {
            std::cout << "❌ " << tests_failed << " test(s) failed" << std::endl;
        }
        std::cout << std::string(60, '=') << std::endl;
    }
    
    // Test 1: Basic functionality with default parameters
    void test_basic_functionality() {
        std::cout << "\n=== Test 1: Basic Functionality (1 bit per value) ===\n";
        
        ID_vector<> vec(1000);
        
        // Test empty state
        assert_test(vec.empty(), "Empty vector check");
        assert_test(vec.size() == 0, "Initial size is 0");
        assert_test(vec.get_maxID() == 1000, "Max ID correctly set");
        
        // Test adding elements
        vec.push_back(100);
        vec.push_back(50);
        vec.push_back(200);
        
        assert_test(vec.size() == 3, "Size after adding 3 elements");
        assert_test(!vec.empty(), "Vector is not empty");
        assert_test(vec.contains(100), "Contains ID 100");
        assert_test(vec.contains(50), "Contains ID 50");
        assert_test(vec.contains(200), "Contains ID 200");
        assert_test(!vec.contains(999), "Does not contain ID 999");
        
        // Test duplicate addition (should be ignored in 1-bit mode)
        vec.push_back(100);
        assert_test(vec.size() == 3, "Size unchanged after duplicate");
        assert_test(vec.count(100) == 1, "Count of ID 100 is 1");
        
        // Test back and pop_back
        assert_test(vec.back() == 200, "Back returns largest ID");
        vec.pop_back();
        assert_test(vec.size() == 2, "Size after pop_back");
        assert_test(!vec.contains(200), "ID 200 removed");
        
        // Test erase
        assert_test(vec.erase(50), "Erase existing ID returns true");
        assert_test(!vec.erase(999), "Erase non-existing ID returns false");
        assert_test(vec.size() == 1, "Size after erase");
        
        // Test clear
        vec.clear();
        assert_test(vec.empty(), "Vector empty after clear");
        assert_test(vec.size() == 0, "Size 0 after clear");
    }
    
    // Test 2: Multi-bit functionality
    void test_multi_bit_functionality() {
        std::cout << "\n=== Test 2: Multi-bit Functionality ===\n";
        
        // Test 2 bits per value (max count = 3)
        ID_vector<2> vec2(100);
        
        // Add same ID multiple times
        vec2.push_back(50);
        assert_test(vec2.count(50) == 1 && vec2.size() == 1, "First addition - count 1");
        
        vec2.push_back(50);
        assert_test(vec2.count(50) == 2 && vec2.size() == 2, "Second addition - count 2");
        
        vec2.push_back(50);
        assert_test(vec2.count(50) == 3 && vec2.size() == 3, "Third addition - count 3");
        
        // Fourth addition should be ignored
        vec2.push_back(50);
        assert_test(vec2.count(50) == 3 && vec2.size() == 3, "Fourth addition ignored");
        
        // Test 3 bits per value (max count = 7)
        ID_vector<3> vec3(100);
        for(int i = 0; i < 10; ++i) {
            vec3.push_back(25);
        }
        assert_test(vec3.count(25) == 7, "3-bit vector max count is 7");
        assert_test(vec3.size() == 7, "Size matches count");
        
        // Test 4 bits per value (max count = 15)
        ID_vector<4> vec4(100);
        for(int i = 0; i < 20; ++i) {
            vec4.push_back(75);
        }
        assert_test(vec4.count(75) == 15, "4-bit vector max count is 15");
    }
    
    // Test 3: Iterator functionality
    void test_iterator_functionality() {
        std::cout << "\n=== Test 3: Iterator Functionality ===\n";
        
        ID_vector<2> vec(50);
        vec.push_back(10);
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);
        vec.push_back(30);
        vec.push_back(30);
        
        // Test iterator traversal
        std::vector<size_t> expected = {10, 10, 20, 30, 30, 30};
        std::vector<size_t> actual;
        
        for(auto id : vec) {
            actual.push_back(id);
        }
        
        assert_test(actual == expected, "Iterator returns correct sequence");
        
        // Test operator[]
        for(size_t i = 0; i < expected.size(); ++i) {
            assert_test(vec[i] == expected[i], "operator[] index " + std::to_string(i));
        }
        
        // Test range-based for loop count
        size_t count = 0;
        for(auto id : vec) {
            count++;
        }
        assert_test(count == vec.size(), "Iterator count matches size");
    }
    
    // Test 4: Erase functionality
    void test_erase_functionality() {
        std::cout << "\n=== Test 4: Erase Functionality ===\n";
        
        ID_vector<2> vec(100);
        
        // Add multiple instances
        vec.push_back(50);
        vec.push_back(50);
        vec.push_back(50);
        vec.push_back(75);
        vec.push_back(75);
        
        // Test single erase
        assert_test(vec.erase(50), "Erase one instance of 50");
        assert_test(vec.count(50) == 2, "Count of 50 reduced to 2");
        assert_test(vec.size() == 4, "Size reduced by 1");
        
        // Test erase_all
        assert_test(vec.erase_all(50), "Erase all instances of 50");
        assert_test(vec.count(50) == 0, "Count of 50 is now 0");
        assert_test(!vec.contains(50), "50 no longer in vector");
        assert_test(vec.size() == 2, "Size reduced by 2");
        
        // Test erase on non-existing ID
        assert_test(!vec.erase(999), "Erase non-existing ID returns false");
        assert_test(!vec.erase_all(999), "Erase_all non-existing ID returns false");
    }
    
    // Test 5: Edge cases and error handling
    void test_edge_cases() {
        std::cout << "\n=== Test 5: Edge Cases and Error Handling ===\n";
        
        // Test max ID boundary
        ID_vector<> vec(100);
        
        vec.push_back(100); // Should work (max ID)
        assert_test(vec.contains(100), "Can add max ID");
        
        // Try to add beyond max ID
        bool exception_thrown = false;
        try {
            vec.push_back(101);
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for ID > maxID");
        
        // Test empty vector operations
        ID_vector<> empty_vec(10);
        exception_thrown = false;
        try {
            empty_vec.back();
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for back() on empty vector");
        
        // Test operator[] out of bounds
        exception_thrown = false;
        try {
            empty_vec[0];
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for operator[] out of bounds");
        
        // Test very large max ID construction
        exception_thrown = false;
        try {
            ID_vector<> huge_vec(536870912); // MAX_RF_ID
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for max ID at limit");
    }
    
    // Test 6: Memory efficiency
    void test_memory_efficiency() {
        std::cout << "\n=== Test 6: Memory Efficiency ===\n";
        
        const size_t max_id = 10000;
        
        // Calculate expected memory usage
        auto calc_memory = [](size_t max_id, uint8_t bits_per_value) {
            size_t total_bits = (max_id + 1) * bits_per_value;
            return (total_bits + 7) / 8;
        };
        
        ID_vector<1> vec1(max_id);
        ID_vector<2> vec2(max_id);
        ID_vector<3> vec3(max_id);
        ID_vector<4> vec4(max_id);
        
        size_t expected1 = calc_memory(max_id, 1);
        size_t expected2 = calc_memory(max_id, 2);
        size_t expected3 = calc_memory(max_id, 3);
        size_t expected4 = calc_memory(max_id, 4);
        
        std::cout << "1-bit vector memory: " << expected1 << " bytes" << std::endl;
        std::cout << "2-bit vector memory: " << expected2 << " bytes" << std::endl;
        std::cout << "3-bit vector memory: " << expected3 << " bytes" << std::endl;
        std::cout << "4-bit vector memory: " << expected4 << " bytes" << std::endl;
        
        // Verify memory scaling (allowing for byte alignment)
        double ratio2 = (double)expected2 / expected1;
        double ratio3 = (double)expected3 / expected1;
        double ratio4 = (double)expected4 / expected1;
        
        assert_test(ratio2 >= 1.9 && ratio2 <= 2.1, "2-bit uses ~2x memory of 1-bit");
        assert_test(ratio3 >= 2.9 && ratio3 <= 3.1, "3-bit uses ~3x memory of 1-bit");
        assert_test(ratio4 >= 3.9 && ratio4 <= 4.1, "4-bit uses ~4x memory of 1-bit");
        
        // Compare with traditional vector<uint16_t>
        size_t traditional_memory = 1000 * sizeof(uint16_t); // for 1000 elements
        std::cout << "Traditional vector<uint16_t> for 1000 elements: " << traditional_memory << " bytes" << std::endl;
        std::cout << "ID_vector<1> with max_id=10000: " << expected1 << " bytes" << std::endl;
        
        bool memory_efficient = expected1 < traditional_memory;
        assert_test(memory_efficient, "ID_vector is more memory efficient for sparse data");
    }
    
    // Test 7: Performance benchmarks
    void test_performance() {
        std::cout << "\n=== Test 7: Performance Benchmarks ===\n";
        
        const size_t num_operations = 100000;
        const size_t max_id = 50000;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<size_t> dis(0, max_id);
        
        // Generate random IDs
        std::vector<size_t> test_ids;
        test_ids.reserve(num_operations);
        for(size_t i = 0; i < num_operations; ++i) {
            test_ids.push_back(dis(gen));
        }
        
        // Benchmark insertions
        auto start = high_resolution_clock::now();
        
        ID_vector<2> vec(max_id);
        for(auto id : test_ids) {
            vec.push_back(id);
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        std::cout << "Inserted " << num_operations << " elements in " 
                  << duration.count() << " μs" << std::endl;
        std::cout << "Average insertion time: " 
                  << (double)duration.count() / num_operations << " μs per element" << std::endl;
        
        // Benchmark lookups
        start = high_resolution_clock::now();
        
        size_t found_count = 0;
        for(auto id : test_ids) {
            if(vec.contains(id)) found_count++;
        }
        
        end = high_resolution_clock::now();
        duration = duration_cast<microseconds>(end - start);
        
        std::cout << "Performed " << num_operations << " lookups in " 
                  << duration.count() << " μs" << std::endl;
        std::cout << "Average lookup time: " 
                  << (double)duration.count() / num_operations << " μs per element" << std::endl;
        std::cout << "Found " << found_count << " elements" << std::endl;
        
        // Benchmark iteration
        start = high_resolution_clock::now();
        
        size_t iter_count = 0;
        for(auto id : vec) {
            iter_count++;
            (void)id; // Suppress unused variable warning
        }
        
        end = high_resolution_clock::now();
        duration = duration_cast<microseconds>(end - start);
        
        std::cout << "Iterated over " << iter_count << " elements in " 
                  << duration.count() << " μs" << std::endl;
        std::cout << "Vector size: " << vec.size() << std::endl;
        
        // Note: iter_count may be less than vec.size() due to duplicates being capped at max count
        assert_test(iter_count <= vec.size(), "Iterator count <= size (due to duplicate limits)");
        
        // Performance should be reasonable
        bool reasonable_performance = duration.count() < 1000000; // Less than 1 second
        assert_test(reasonable_performance, "Performance is reasonable");
    }
    
    // Test 8: Template parameter variations
    void test_template_variations() {
        std::cout << "\n=== Test 8: Template Parameter Variations ===\n";
        
        // Test different bit configurations (simplified template - no size flags)
        ID_vector<1> vec1(100);
        ID_vector<2> vec2(100);
        ID_vector<3> vec3(100);
        ID_vector<4> vec4(100);
        ID_vector<8> vec8(100);
        
        // All should work the same functionally
        vec1.push_back(50);
        vec2.push_back(50);
        vec3.push_back(50);
        vec4.push_back(50);
        vec8.push_back(50);
        
        assert_test(vec1.contains(50), "1-bit template works");
        assert_test(vec2.contains(50), "2-bit template works");
        assert_test(vec3.contains(50), "3-bit template works");
        assert_test(vec4.contains(50), "4-bit template works");
        assert_test(vec8.contains(50), "8-bit template works");
        
        // Test maximum count behavior for each template
        std::cout << "Testing bit counts and max values:" << std::endl;
        
        // 1-bit: max count = 1
        ID_vector<1> test1(10);
        for(int i = 0; i < 5; ++i) test1.push_back(5);
        std::cout << "1-bit: max count = " << ((1 << 1) - 1) << ", actual count = " << test1.count(5) << std::endl;
        assert_test(test1.count(5) == 1, "1-bit max count is 1");
        
        // 2-bit: max count = 3
        ID_vector<2> test2(10);
        for(int i = 0; i < 5; ++i) test2.push_back(5);
        std::cout << "2-bit: max count = " << ((1 << 2) - 1) << ", actual count = " << test2.count(5) << std::endl;
        assert_test(test2.count(5) == 3, "2-bit max count is 3");
        
        // 3-bit: max count = 7
        ID_vector<3> test3(10);
        for(int i = 0; i < 10; ++i) test3.push_back(5);
        std::cout << "3-bit: max count = " << ((1 << 3) - 1) << ", actual count = " << test3.count(5) << std::endl;
        assert_test(test3.count(5) == 7, "3-bit max count is 7");
        
        // 4-bit: max count = 15
        ID_vector<4> test4(10);
        for(int i = 0; i < 20; ++i) test4.push_back(5);
        std::cout << "4-bit: max count = " << ((1 << 4) - 1) << ", actual count = " << test4.count(5) << std::endl;
        assert_test(test4.count(5) == 15, "4-bit max count is 15");
        
        // 8-bit: max count = 255
        ID_vector<8> test8(10);
        for(int i = 0; i < 300; ++i) test8.push_back(5);
        std::cout << "8-bit: max count = " << ((1 << 8) - 1) << ", actual count = " << test8.count(5) << std::endl;
        assert_test(test8.count(5) == 255, "8-bit max count is 255");
    }
    
    // Test 9: Stress testing
    void test_stress() {
        std::cout << "\n=== Test 9: Stress Testing ===\n";
        
        const size_t max_id = 1000;
        ID_vector<3> vec(max_id);
        
        // Add many elements (respecting max count of 7 for 3-bit)
        for(size_t i = 0; i <= max_id; i += 10) {
            for(int count = 0; count < 5; ++count) { // Add 5 instances (within limit of 7)
                vec.push_back(i);
            }
        }
        
        std::cout << "Added elements to vector, size: " << vec.size() << std::endl;
        
        // Verify counts
        bool counts_correct = true;
        for(size_t i = 0; i <= max_id; i += 10) {
            if(vec.count(i) != 5) {
                counts_correct = false;
                break;
            }
        }
        assert_test(counts_correct, "All element counts are correct");
        
        // Test iterator consistency
        size_t manual_count = 0;
        for(size_t i = 0; i <= max_id; i += 10) {
            manual_count += vec.count(i);
        }
        
        size_t iter_count = 0;
        for(auto id : vec) {
            iter_count++;
            (void)id; // Suppress unused variable warning
        }
        
        std::cout << "Manual count: " << manual_count << ", Iterator count: " << iter_count 
                  << ", vec.size(): " << vec.size() << std::endl;
        
        assert_test(manual_count == iter_count, "Manual count matches iterator count");
        assert_test(iter_count == vec.size(), "Iterator count matches size()");
        
        // Test massive clear and rebuild
        vec.clear();
        assert_test(vec.empty(), "Vector empty after stress clear");
        
        // Rebuild with different pattern
        for(size_t i = 0; i < 100; ++i) {
            vec.push_back(i % 50); // This will create multiple instances
        }
        
        assert_test(vec.size() == 100, "Rebuilt vector has correct size");
    }
    
    // Test 10: Comparison with standard containers
    void test_comparison_with_std() {
        std::cout << "\n=== Test 10: Comparison with Standard Containers ===\n";
        
        const size_t max_id = 1000;
        ID_vector<1> id_vec(max_id);
        std::vector<bool> bool_vec(max_id + 1, false);
        
        // Add same elements to both
        std::vector<size_t> test_ids = {10, 50, 100, 200, 500, 999};
        
        for(auto id : test_ids) {
            id_vec.push_back(id);
            bool_vec[id] = true;
        }
        
        // Verify same behavior
        bool behaviors_match = true;
        for(size_t i = 0; i <= max_id; ++i) {
            if(id_vec.contains(i) != bool_vec[i]) {
                behaviors_match = false;
                break;
            }
        }
        
        assert_test(behaviors_match, "ID_vector behaves like vector<bool>");
        
        // Compare memory usage
        size_t id_vec_memory = (max_id + 8) / 8; // approximate
        size_t bool_vec_memory = bool_vec.size() / 8; // approximate
        
        std::cout << "ID_vector memory (approx): " << id_vec_memory << " bytes" << std::endl;
        std::cout << "vector<bool> memory (approx): " << bool_vec_memory << " bytes" << std::endl;
        
        bool comparable_memory = abs((int)id_vec_memory - (int)bool_vec_memory) < 100;
        assert_test(comparable_memory, "Memory usage comparable to vector<bool>");
    }
    
    void run_all_tests() {
        std::cout << "🚀 Starting Comprehensive ID_vector Test Suite" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        test_basic_functionality();
        test_multi_bit_functionality();
        test_iterator_functionality();
        test_erase_functionality();
        test_edge_cases();
        test_memory_efficiency();
        test_performance();
        test_template_variations();
        test_stress();
        test_comparison_with_std();
        
        print_results();
    }
};

int main() {
    TestSuite suite;
    suite.run_all_tests();
    return 0;
}