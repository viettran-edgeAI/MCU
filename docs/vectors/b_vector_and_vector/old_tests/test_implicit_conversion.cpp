#include "b_vector.cpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing b_vector implicit conversions between different SBO sizes...\n";
    
    // Test 1: Copy constructor - small to large SBO
    mcu::b_vector<int, 4> small_vec;
    small_vec.push_back(1);
    small_vec.push_back(2);
    small_vec.push_back(3);
    
    mcu::b_vector<int, 8> large_vec1 = small_vec;  // Implicit copy constructor
    assert(large_vec1.size() == 3);
    assert(large_vec1[0] == 1 && large_vec1[1] == 2 && large_vec1[2] == 3);
    std::cout << "✓ Copy constructor (small to large SBO) works\n";
    
    // Test 2: Copy constructor - large to small SBO (fits in small buffer)
    mcu::b_vector<int, 2> tiny_vec = small_vec;  // Should still work since data fits
    assert(tiny_vec.size() == 3);
    assert(tiny_vec[0] == 1 && tiny_vec[1] == 2 && tiny_vec[2] == 3);
    std::cout << "✓ Copy constructor (large to small SBO, data fits) works\n";
    
    // Test 3: Copy assignment - different SBO sizes
    mcu::b_vector<int, 6> medium_vec;
    medium_vec = small_vec;  // Implicit copy assignment
    assert(medium_vec.size() == 3);
    assert(medium_vec[0] == 1 && medium_vec[1] == 2 && medium_vec[2] == 3);
    std::cout << "✓ Copy assignment between different SBO sizes works\n";
    
    // Test 4: Move constructor
    mcu::b_vector<int, 4> source_vec;
    source_vec.push_back(10);
    source_vec.push_back(20);
    
    mcu::b_vector<int, 8> dest_vec = std::move(source_vec);  // Implicit move constructor
    assert(dest_vec.size() == 2);
    assert(dest_vec[0] == 10 && dest_vec[1] == 20);
    std::cout << "✓ Move constructor between different SBO sizes works\n";
    
    // Test 5: Move assignment
    mcu::b_vector<int, 4> another_source;
    another_source.push_back(100);
    another_source.push_back(200);
    another_source.push_back(300);
    
    mcu::b_vector<int, 12> another_dest;
    another_dest = std::move(another_source);  // Implicit move assignment
    assert(another_dest.size() == 3);
    assert(another_dest[0] == 100 && another_dest[1] == 200 && another_dest[2] == 300);
    std::cout << "✓ Move assignment between different SBO sizes works\n";
    
    // Test 6: Large data forcing heap usage
    mcu::b_vector<int, 2> small_sbo;
    for (int i = 0; i < 10; ++i) {
        small_sbo.push_back(i);
    }
    
    mcu::b_vector<int, 16> large_sbo = small_sbo;  // Copy from heap to SBO-capable
    assert(large_sbo.size() == 10);
    for (int i = 0; i < 10; ++i) {
        assert(large_sbo[i] == i);
    }
    std::cout << "✓ Copy from heap-using vector to SBO-capable vector works\n";
    
    std::cout << "\nAll implicit conversion tests passed! ✅\n";
    
    return 0;
}