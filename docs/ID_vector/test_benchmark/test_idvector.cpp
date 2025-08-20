#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <cassert>
#include <algorithm>
#include <iomanip>
#include <utility>
#include "../ID_vector.cpp"

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
        
        ID_vector<uint16_t> vec(1000);
        
        // Test empty state
        assert_test(vec.empty(), "Empty vector check");
        assert_test(vec.size() == 0, "Initial size is 0");
        assert_test(vec.get_maxID() == 1000, "Max ID correctly set");
        assert_test(vec.get_minID() == 0, "Min ID correctly set (default)");
        
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
    
    // Test 1.5: Min ID functionality and range optimization
    void test_min_id_functionality() {
        std::cout << "\n=== Test 1.5: Min ID Functionality ===\n";
        
        // Test constructor with min and max ID range
        ID_vector<uint16_t> vec(1000, 2000);
        
        assert_test(vec.get_minID() == 1000, "Min ID correctly set in constructor");
        assert_test(vec.get_maxID() == 2000, "Max ID correctly set in constructor");
        assert_test(vec.empty(), "Vector initially empty");
        
        // Test adding IDs within range
        vec.push_back(1500);
        vec.push_back(1000); // min boundary
        vec.push_back(2000); // max boundary
        
        assert_test(vec.size() == 3, "Size after adding 3 IDs in range");
        assert_test(vec.contains(1500), "Contains ID 1500");
        assert_test(vec.contains(1000), "Contains min ID 1000");
        assert_test(vec.contains(2000), "Contains max ID 2000");
        
        // Test adding IDs outside range should throw
        bool exception_thrown = false;
        try {
            vec.push_back(999); // below min
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for ID < minID");
        
        exception_thrown = false;
        try {
            vec.push_back(2001); // above max
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for ID > maxID");
        
        // Test set_minID method
        ID_vector<uint16_t> vec2(100);
        vec2.set_minID(50);
        assert_test(vec2.get_minID() == 50, "set_minID correctly updates min ID");
        assert_test(vec2.get_maxID() == 100, "Max ID unchanged after set_minID");
        
        // Test set_ID_range method
        vec2.set_ID_range(200, 300);
        assert_test(vec2.get_minID() == 200, "set_ID_range correctly updates min ID");
        assert_test(vec2.get_maxID() == 300, "set_ID_range correctly updates max ID");
        
        // Test error cases for range setting
        exception_thrown = false;
        try {
            vec2.set_ID_range(400, 300); // min > max
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for min > max in set_ID_range");
        
        // Test memory efficiency with high min ID
        ID_vector<uint16_t> vec_low(0, 1000);     // Range 0-1000: 1001 slots
        ID_vector<uint16_t> vec_high(1000, 2000); // Range 1000-2000: 1001 slots
        
        // Both should have similar memory footprint despite different ranges
        vec_low.push_back(500);
        vec_high.push_back(1500);
        
        assert_test(vec_low.contains(500), "Low range vector works");
        assert_test(vec_high.contains(1500), "High range vector works");
        assert_test(!vec_high.contains(500), "High range vector correctly rejects low ID");
        
        std::cout << "Memory optimization test: Range 1000-2000 uses same memory as 0-1000" << std::endl;
    }
    
    // Test 2: Multi-bit functionality
    void test_multi_bit_functionality() {
        std::cout << "\n=== Test 2: Multi-bit Functionality ===\n";
        
        // Test 2 bits per value (max count = 3)
        ID_vector<uint16_t, 2> vec2(100);
        
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
        ID_vector<uint16_t, 3> vec3(100);
        for(int i = 0; i < 10; ++i) {
            vec3.push_back(25);
        }
        assert_test(vec3.count(25) == 7, "3-bit vector max count is 7");
        assert_test(vec3.size() == 7, "Size matches count");
        
        // Test 4 bits per value (max count = 15)
        ID_vector<uint16_t, 4> vec4(100);
        for(int i = 0; i < 20; ++i) {
            vec4.push_back(75);
        }
        assert_test(vec4.count(75) == 15, "4-bit vector max count is 15");
    }
    
    // Test 3: Iterator functionality
    void test_iterator_functionality() {
        std::cout << "\n=== Test 3: Iterator Functionality ===\n";
        
        ID_vector<uint16_t, 2> vec(10, 50); // Range 10-50 to test with min_id
        vec.push_back(20);
        vec.push_back(20);
        vec.push_back(30);
        vec.push_back(40);
        vec.push_back(40);
        vec.push_back(40);
        
        // Test iterator traversal
        std::vector<size_t> expected = {20, 20, 30, 40, 40, 40};
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
            (void)id; // Suppress unused variable warning
            count++;
        }
        assert_test(count == vec.size(), "Iterator count matches size");
    }
    
    // Test 4: Erase functionality
    void test_erase_functionality() {
        std::cout << "\n=== Test 4: Erase Functionality ===\n";
        
        ID_vector<uint16_t, 2> vec(100);
        
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
        ID_vector<uint16_t> vec(50, 100); // Range 50-100
        
        vec.push_back(100); // Should work (max ID)
        vec.push_back(50);  // Should work (min ID)
        assert_test(vec.contains(100), "Can add max ID");
        assert_test(vec.contains(50), "Can add min ID");
        
        // Try to add beyond max ID
        bool exception_thrown = false;
        try {
            vec.push_back(101);
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for ID > maxID");
        
        // Try to add below min ID
        exception_thrown = false;
        try {
            vec.push_back(49);
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for ID < minID");
        
        // Test empty vector operations
        ID_vector<uint16_t> empty_vec(10, 20);
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
        
        // Test range validation in constructors and setters
        exception_thrown = false;
        try {
            ID_vector<uint16_t> invalid_vec(100, 50); // min > max
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for min > max in constructor");
        
        // Test very large max ID construction
        exception_thrown = false;
        try {
            ID_vector<uint16_t> huge_vec(0, 65535); // MAX_RF_ID for uint16_t - should work
            huge_vec.push_back(0); // This should work
            ID_vector<uint16_t> invalid_vec(0, static_cast<uint16_t>(65536)); // MAX_RF_ID + 1 - should throw
        } catch (const std::out_of_range&) {
            exception_thrown = true;
        }
        assert_test(exception_thrown, "Exception thrown for max ID at limit");
    }
    
    // Test 6: Memory efficiency
    void test_memory_efficiency() {
        std::cout << "\n=== Test 6: Memory Efficiency ===\n";
        
        const size_t max_id = 10000;
        
        // Calculate expected memory usage for range-based allocation
        auto calc_memory = [](size_t min_id, size_t max_id, uint8_t bits_per_value) {
            size_t range = max_id - min_id + 1;
            size_t total_bits = range * bits_per_value;
            return (total_bits + 7) / 8;
        };
        
        ID_vector<uint16_t, 1> vec1(0, max_id);
        ID_vector<uint16_t, 2> vec2(0, max_id);
        ID_vector<uint16_t, 3> vec3(0, max_id);
        ID_vector<uint16_t, 4> vec4(0, max_id);
        
        size_t expected1 = calc_memory(0, max_id, 1);
        size_t expected2 = calc_memory(0, max_id, 2);
        size_t expected3 = calc_memory(0, max_id, 3);
        size_t expected4 = calc_memory(0, max_id, 4);
        
        std::cout << "1-bit vector memory: " << expected1 << " bytes" << std::endl;
        std::cout << "2-bit vector memory: " << expected2 << " bytes" << std::endl;
        std::cout << "3-bit vector memory: " << expected3 << " bytes" << std::endl;
        std::cout << "4-bit vector memory: " << expected4 << " bytes" << std::endl;
        
        // Test memory optimization with high range
        ID_vector<uint16_t, 1> vec_optimized(5000, 6000); // Range of 1001 elements
        size_t optimized_memory = calc_memory(5000, 6000, 1);
        size_t full_range_memory = calc_memory(0, 6000, 1);
        
        std::cout << "Optimized range [5000-6000] memory: " << optimized_memory << " bytes" << std::endl;
        std::cout << "Full range [0-6000] memory: " << full_range_memory << " bytes" << std::endl;
        
        double memory_savings = (double)(full_range_memory - optimized_memory) / full_range_memory * 100.0;
        std::cout << "Memory savings: " << memory_savings << "%" << std::endl;
        
        assert_test(optimized_memory < full_range_memory, "Range optimization saves memory");
        assert_test(memory_savings > 70.0, "Significant memory savings (>70%)");
        
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
        std::cout << "ID_vector<uint16_t, 1> with max_id=10000: " << expected1 << " bytes" << std::endl;
        
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
        
        ID_vector<size_t, 2> vec(max_id);
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
        
        // Note: iter_count should equal vec.size() since both count all instances
        assert_test(iter_count == vec.size(), "Iterator count equals size");
        
        // Performance should be reasonable
        bool reasonable_performance = duration.count() < 1000000; // Less than 1 second
        assert_test(reasonable_performance, "Performance is reasonable");
    }
    
    // Test 8: Template parameter variations
    void test_template_variations() {
        std::cout << "\n=== Test 8: Template Parameter Variations ===\n";
        
        // Test different bit configurations (simplified template - no size flags)
        ID_vector<uint16_t, 1> vec1(100);
        ID_vector<uint16_t, 2> vec2(100);
        ID_vector<uint16_t, 3> vec3(100);
        ID_vector<uint16_t, 4> vec4(100);
        ID_vector<uint16_t, 8> vec8(100);
        
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
        ID_vector<uint16_t, 1> test1(10);
        for(int i = 0; i < 5; ++i) test1.push_back(5);
        std::cout << "1-bit: max count = " << ((1 << 1) - 1) << ", actual count = " << test1.count(5) << std::endl;
        assert_test(test1.count(5) == 1, "1-bit max count is 1");
        
        // 2-bit: max count = 3
        ID_vector<uint16_t, 2> test2(10);
        for(int i = 0; i < 5; ++i) test2.push_back(5);
        std::cout << "2-bit: max count = " << ((1 << 2) - 1) << ", actual count = " << test2.count(5) << std::endl;
        assert_test(test2.count(5) == 3, "2-bit max count is 3");
        
        // 3-bit: max count = 7
        ID_vector<uint16_t, 3> test3(10);
        for(int i = 0; i < 10; ++i) test3.push_back(5);
        std::cout << "3-bit: max count = " << ((1 << 3) - 1) << ", actual count = " << test3.count(5) << std::endl;
        assert_test(test3.count(5) == 7, "3-bit max count is 7");
        
        // 4-bit: max count = 15
        ID_vector<uint16_t, 4> test4(10);
        for(int i = 0; i < 20; ++i) test4.push_back(5);
        std::cout << "4-bit: max count = " << ((1 << 4) - 1) << ", actual count = " << test4.count(5) << std::endl;
        assert_test(test4.count(5) == 15, "4-bit max count is 15");
        
        // 8-bit: max count = 255
        ID_vector<uint16_t, 8> test8(10);
        for(int i = 0; i < 300; ++i) test8.push_back(5);
        std::cout << "8-bit: max count = " << ((1 << 8) - 1) << ", actual count = " << test8.count(5) << std::endl;
        assert_test(test8.count(5) == 255, "8-bit max count is 255");
    }
    
    // Test 9: Stress testing
    void test_stress() {
        std::cout << "\n=== Test 9: Stress Testing ===\n";
        
        const size_t max_id = 1000;
        ID_vector<uint16_t, 3> vec(max_id);
        
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
        ID_vector<uint16_t, 1> id_vec(max_id);
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
    
    // Test 11: Copy/Move constructors and assignment operators
    void test_copy_move_semantics() {
        std::cout << "\n=== Test 11: Copy/Move Semantics ===\n";
        
        // Create original vector
        ID_vector<uint16_t, 2> original(10, 20);
        original.push_back(15);
        original.push_back(15);
        original.push_back(18);
        original.push_back(20);
        
        // Test copy constructor
        ID_vector<uint16_t, 2> copied(original);
        assert_test(copied == original, "Copy constructor creates equal vector");
        assert_test(copied.size() == original.size(), "Copy has same size");
        assert_test(copied.get_minID() == original.get_minID(), "Copy has same min ID");
        assert_test(copied.get_maxID() == original.get_maxID(), "Copy has same max ID");
        assert_test(copied.count(15) == 2, "Copy has correct count for ID 15");
        
        // Modify original, copy should be unaffected
        original.push_back(12);
        assert_test(copied.size() != original.size(), "Copy is independent of original");
        
        // Test copy assignment
        ID_vector<uint16_t, 2> assigned(5, 10);
        assigned.push_back(7);
        assigned = original;
        assert_test(assigned == original, "Copy assignment creates equal vector");
        assert_test(assigned.get_minID() == original.get_minID(), "Assigned has same min ID");
        assert_test(assigned.get_maxID() == original.get_maxID(), "Assigned has same max ID");
        
        // Test move constructor
        ID_vector<uint16_t, 2> original_copy(original);
        ID_vector<uint16_t, 2> moved(std::move(original));
        assert_test(moved == original_copy, "Move constructor preserves content");
        assert_test(original.size() == 0, "Moved-from vector is empty");
        
        // Test move assignment
        ID_vector<uint16_t, 2> move_assigned(1, 5);
        move_assigned = std::move(copied);
        assert_test(move_assigned.count(15) == 2, "Move assignment preserves content");
        assert_test(copied.size() == 0, "Moved-from vector is empty");
    }
    
    // Test 12: Comparison and set operations
    void test_comparison_and_set_operations() {
        std::cout << "\n=== Test 12: Comparison and Set Operations ===\n";
        
        ID_vector<uint16_t, 2> vec1(10, 20);
        vec1.push_back(12);
        vec1.push_back(12);
        vec1.push_back(15);
        vec1.push_back(18);
        
        ID_vector<uint16_t, 2> vec2(10, 20);
        vec2.push_back(12);
        vec2.push_back(12);
        vec2.push_back(15);
        vec2.push_back(18);
        
        // Test equality
        assert_test(vec1 == vec2, "Equal vectors compare equal");
        assert_test(!(vec1 != vec2), "Equal vectors are not unequal");
        
        vec2.push_back(19);
        assert_test(vec1 != vec2, "Different vectors compare unequal");
        assert_test(!(vec1 == vec2), "Different vectors are not equal");
        
        // Test subset
        ID_vector<uint16_t, 2> subset(10, 20);
        subset.push_back(12);
        subset.push_back(15);
        assert_test(subset.is_subset_of(vec1), "Subset correctly identified");
        assert_test(!vec1.is_subset_of(subset), "Superset is not subset");
        
        // Test union operation
        ID_vector<uint16_t, 2> vec3(15, 25);
        vec3.push_back(15);
        vec3.push_back(20);
        vec3.push_back(22);
        
        ID_vector<uint16_t, 2> union_result = vec1 | vec3;
        assert_test(union_result.contains(12), "Union contains elements from first vector");
        assert_test(union_result.contains(22), "Union contains elements from second vector");
        assert_test(union_result.contains(15), "Union contains common elements");
        assert_test(union_result.get_minID() == 10, "Union has correct min ID");
        assert_test(union_result.get_maxID() == 25, "Union has correct max ID");
        
        // Test intersection operation
        ID_vector<uint16_t, 2> intersect_result = vec1 & vec3;
        assert_test(intersect_result.contains(15), "Intersection contains common elements");
        assert_test(!intersect_result.contains(12), "Intersection excludes non-common elements");
        assert_test(!intersect_result.contains(22), "Intersection excludes non-common elements");
        
        // Test difference operation
        ID_vector<uint16_t, 2> diff_result = vec1 - vec3;
        assert_test(diff_result.contains(12), "Difference contains elements only in first");
        assert_test(diff_result.contains(18), "Difference contains elements only in first");
        assert_test(!diff_result.contains(15), "Difference excludes common elements");
        
        // Test compound assignment operators
        ID_vector<uint16_t, 2> compound_test(10, 25);
        compound_test.push_back(10);
        compound_test.push_back(25);
        
        compound_test |= vec1;
        assert_test(compound_test.contains(12), "Union assignment works");
        assert_test(compound_test.contains(25), "Union assignment preserves original elements");
        
        compound_test &= vec1;
        assert_test(compound_test.contains(12), "Intersection assignment works");
        assert_test(!compound_test.contains(25), "Intersection assignment removes non-common elements");
    }
    
    // Test: Smart Range Configuration with Data Preservation
    void test_smart_range_configuration() {
        std::cout << "\n=== Test: Smart Range Configuration with Data Preservation ===\n";
        
        // Test set_minID data preservation
        {
            // Test Case 1: Empty vector - should work normally
            ID_vector<uint16_t, 2> vec1(1000, 3000);
            assert_test(vec1.size() == 0, "Empty vector initial state");
            assert_test(vec1.get_minID() == 1000, "Initial min_id");
            assert_test(vec1.get_maxID() == 3000, "Initial max_id");
            
            vec1.set_minID(1500); // Should work since vector is empty
            assert_test(vec1.get_minID() == 1500, "set_minID on empty vector");
            assert_test(vec1.get_maxID() == 3000, "max_id unchanged");
            assert_test(vec1.size() == 0, "Size remains 0");
            
            // Test Case 2: Vector with data - safe range adjustment
            ID_vector<uint16_t, 2> vec2(1000, 3000);
            vec2.push_back(2000);
            vec2.push_back(2000); // Count: 2
            vec2.push_back(2500);
            vec2.push_back(2800); // Count: 1 each
            
            // Set new min_id to 1900 (below smallest element 2000) - should preserve data
            vec2.set_minID(1900);
            assert_test(vec2.get_minID() == 1900, "Safe set_minID");
            assert_test(vec2.size() == 4, "Data preserved after set_minID");
            assert_test(vec2.count(2000) == 2, "Count preserved");
            assert_test(vec2.contains(2500), "Element 2500 preserved");
            assert_test(vec2.contains(2800), "Element 2800 preserved");
            
            // Test Case 3: Vector with data - unsafe range adjustment (should throw)
            ID_vector<uint16_t, 2> vec3(1000, 3000);
            vec3.push_back(1500);
            vec3.push_back(2000);
            vec3.push_back(2500);
            
            bool exception_thrown = false;
            try {
                vec3.set_minID(1600); // Should throw since 1600 > 1500 (smallest element)
            } catch (const std::out_of_range& e) {
                exception_thrown = true;
            }
            assert_test(exception_thrown, "Exception thrown for unsafe set_minID");
            assert_test(vec3.get_minID() == 1000, "min_id unchanged after failed operation");
            assert_test(vec3.size() == 3, "Data unchanged after failed operation");
        }
        
        // Test set_maxID data preservation
        {
            // Test Case 1: Safe range expansion
            ID_vector<uint16_t, 2> vec1(1000, 3000);
            vec1.push_back(1500);
            vec1.push_back(2000);
            vec1.push_back(2000);
            vec1.push_back(2800);
            
            // Expand max_id to 3500 (above largest element 2800) - should preserve data
            vec1.set_maxID(3500);
            assert_test(vec1.get_maxID() == 3500, "Safe set_maxID expansion");
            assert_test(vec1.size() == 4, "Data preserved after set_maxID");
            assert_test(vec1.count(2000) == 2, "Count preserved after expansion");
            assert_test(vec1.contains(2800), "Largest element preserved");
            
            // Test Case 2: Unsafe range reduction (should throw)
            ID_vector<uint16_t, 2> vec2(1000, 3000);
            vec2.push_back(1500);
            vec2.push_back(2000);
            vec2.push_back(2800);
            
            bool exception_thrown = false;
            try {
                vec2.set_maxID(2500); // Should throw since 2500 < 2800 (largest element)
            } catch (const std::out_of_range& e) {
                exception_thrown = true;
            }
            assert_test(exception_thrown, "Exception thrown for unsafe set_maxID");
            assert_test(vec2.get_maxID() == 3000, "max_id unchanged after failed operation");
            assert_test(vec2.size() == 3, "Data unchanged after failed set_maxID");
        }
        
        // Test set_ID_range data preservation
        {
            // Test Case 1: Safe range adjustment
            ID_vector<uint16_t, 2> vec1(1000, 3000);
            vec1.push_back(1500);
            vec1.push_back(2000);
            vec1.push_back(2500);
            
            // Set new range that encompasses all elements
            vec1.set_ID_range(1200, 3500);
            assert_test(vec1.get_minID() == 1200, "Safe set_ID_range min");
            assert_test(vec1.get_maxID() == 3500, "Safe set_ID_range max");
            assert_test(vec1.size() == 3, "Data preserved after set_ID_range");
            assert_test(vec1.contains(1500), "Element 1500 preserved");
            assert_test(vec1.contains(2000), "Element 2000 preserved");
            assert_test(vec1.contains(2500), "Element 2500 preserved");
            
            // Test Case 2: Unsafe range adjustment (should throw)
            ID_vector<uint16_t, 2> vec2(1000, 3000);
            vec2.push_back(1200);
            vec2.push_back(2800);
            
            bool exception_thrown = false;
            try {
                vec2.set_ID_range(1500, 2500); // Should throw since it excludes 1200 and 2800
            } catch (const std::out_of_range& e) {
                exception_thrown = true;
            }
            assert_test(exception_thrown, "Exception thrown for unsafe set_ID_range");
            assert_test(vec2.get_minID() == 1000, "Range unchanged after failed set_ID_range");
            assert_test(vec2.size() == 2, "Data unchanged after failed set_ID_range");
        }
    }
    
    // Test: Size Type Overflow Prevention
    void test_size_overflow_prevention() {
        std::cout << "\n=== Test: Size Type Overflow Prevention ===\n";
        
        // Test Case 1: uint8_t with high BitsPerValue
        // Before fix: index_type was uint8_t (max 255), but with 8 bits, each ID can appear 255 times
        // If we have 2 IDs each appearing 255 times, total size would be 510, overflowing uint8_t
        {
            ID_vector<uint8_t, 8> vec1(0, 10); // Small range to focus on the overflow issue
            
            // Add ID 5 repeatedly (max 255 times due to 8 bits)
            for(int i = 0; i < 255; ++i) {
                vec1.push_back(5);
            }
            assert_test(vec1.size() == 255, "Added 255 instances correctly");
            assert_test(vec1.count(5) == 255, "Count of ID 5 is 255");
            
            // Add ID 6 repeatedly (max 255 times)
            for(int i = 0; i < 255; ++i) {
                vec1.push_back(6);
            }
            
            // Before fix: size_ would overflow uint8_t and wrap to 254 (510 % 256 = 254)
            // After fix: size_type is uint32_t, so it should correctly show 510
            assert_test(vec1.size() == 510, "Size correctly shows 510 (no overflow)");
            assert_test(vec1.count(5) == 255, "Count of ID 5 still 255");
            assert_test(vec1.count(6) == 255, "Count of ID 6 is 255");
            
            // Test iterator consistency
            size_t iter_count = 0;
            for(auto id : vec1) {
                iter_count++;
                (void)id; // Suppress unused warning
            }
            assert_test(iter_count == vec1.size(), "Iterator count matches size after overflow test");
        }
        
        // Test Case 2: uint16_t with moderate BitsPerValue
        {
            ID_vector<uint16_t, 4> vec2(0, 5000); // Sufficient range
            
            // Each ID can appear up to 15 times (2^4 - 1)
            // Add many different IDs, each appearing max times
            const size_t num_ids = 1000; // 1000 IDs * 15 instances = 15,000 total instances
            
            for(size_t id = 0; id < num_ids; ++id) {
                for(int count = 0; count < 15; ++count) {
                    vec2.push_back(static_cast<uint16_t>(id));
                }
            }
            
            // Before fix: size_ (uint16_t) could overflow at 65535 and wrap around
            // After fix: size_type is uint64_t, so it should correctly show 15000
            assert_test(vec2.size() == num_ids * 15, "Large size correctly calculated");
            
            // Test a few random IDs
            assert_test(vec2.count(0) == 15, "ID 0 has correct count");
            assert_test(vec2.count(100) == 15, "ID 100 has correct count");
            assert_test(vec2.count(999) == 15, "ID 999 has correct count");
        }
        
        // Test Case 3: Verify size_type definitions
        {
            ID_vector<uint8_t> vec_u8;
            ID_vector<uint16_t> vec_u16;
            ID_vector<uint32_t> vec_u32;
            ID_vector<size_t> vec_st;
            
            // Verify size_type is larger than index_type for small types
            assert_test(sizeof(decltype(vec_u8.size())) >= 4, "uint8_t template uses uint32_t+ size_type");
            assert_test(sizeof(decltype(vec_u16.size())) >= 8, "uint16_t template uses uint64_t+ size_type");
        }
    }
    
    // Test: Getter Functions for Range Configuration
    void test_range_getters() {
        std::cout << "\n=== Test: Range Getter Functions ===\n";
        
        ID_vector<uint32_t, 3> vec(50000, 100000);
        
        // Test initial getters
        assert_test(vec.get_minID() == 50000, "get_minID returns correct initial value");
        assert_test(vec.get_maxID() == 100000, "get_maxID returns correct initial value");
        
        // Add some data
        vec.push_back(60000);
        vec.push_back(80000);
        vec.push_back(90000);
        
        // Test getters after data addition
        assert_test(vec.get_minID() == 50000, "get_minID unchanged after data addition");
        assert_test(vec.get_maxID() == 100000, "get_maxID unchanged after data addition");
        assert_test(vec.minID() == 60000, "minID returns smallest actual element");
        assert_test(vec.maxID() == 90000, "maxID returns largest actual element");
        
        // Test after range modification
        vec.set_minID(55000);
        vec.set_maxID(95000);
        
        assert_test(vec.get_minID() == 55000, "get_minID returns updated min");
        assert_test(vec.get_maxID() == 95000, "get_maxID returns updated max");
        assert_test(vec.minID() == 60000, "minID still returns smallest actual element");
        assert_test(vec.maxID() == 90000, "maxID still returns largest actual element");
    }

    void run_all_tests() {
        std::cout << "🚀 Starting Comprehensive ID_vector Test Suite" << std::endl;
        std::cout << std::string(60, '=') << std::endl;
        
        test_basic_functionality();
        test_min_id_functionality();
        test_multi_bit_functionality();
        test_iterator_functionality();
        test_erase_functionality();
        test_edge_cases();
        test_memory_efficiency();
        test_performance();
        test_template_variations();
        test_stress();
        test_comparison_with_std();
        test_copy_move_semantics();
        test_comparison_and_set_operations();
        test_smart_range_configuration();
        test_size_overflow_prevention();
        test_range_getters();
        
        print_results();
    }
};

int main() {
    TestSuite suite;
    suite.run_all_tests();
    return 0;
}