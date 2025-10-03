#include "b_vector.cpp"
#include <iostream>

// Function that takes a b_vector with SBO size 8
void process_large_sbo(const mcu::b_vector<int, 8>& vec) {
    std::cout << "Processing b_vector<int, 8> with " << vec.size() << " elements: ";
    for (size_t i = 0; i < vec.size(); ++i) {
        std::cout << vec[i] << " ";
    }
    std::cout << std::endl;
}

// Function that takes a b_vector with SBO size 4
void process_medium_sbo(const mcu::b_vector<int, 4>& vec) {
    std::cout << "Processing b_vector<int, 4> with " << vec.size() << " elements: ";
    for (size_t i = 0; i < vec.size(); ++i) {
        std::cout << vec[i] << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "Demonstrating implicit conversions between b_vector with different SBO sizes\n";
    std::cout << "================================================================\n\n";
    
    // Create a b_vector with SBO size 2
    mcu::b_vector<int, 2> small_vec;
    small_vec.push_back(1);
    small_vec.push_back(2);
    small_vec.push_back(3);
    
    std::cout << "Original b_vector<int, 2>: ";
    for (size_t i = 0; i < small_vec.size(); ++i) {
        std::cout << small_vec[i] << " ";
    }
    std::cout << "\n\n";
    
    // Implicit conversion when passing to functions
    std::cout << "1. Implicit conversion when passing to functions:\n";
    process_medium_sbo(small_vec);  // b_vector<int, 2> → b_vector<int, 4>
    process_large_sbo(small_vec);   // b_vector<int, 2> → b_vector<int, 8>
    std::cout << "\n";
    
    // Assignment between different SBO sizes
    std::cout << "2. Assignment between different SBO sizes:\n";
    mcu::b_vector<int, 6> medium_vec = small_vec;  // Copy constructor
    std::cout << "After copy: medium_vec<6> = ";
    for (size_t i = 0; i < medium_vec.size(); ++i) {
        std::cout << medium_vec[i] << " ";
    }
    std::cout << "\n";
    
    mcu::b_vector<int, 10> large_vec;
    large_vec = small_vec;  // Copy assignment
    std::cout << "After assignment: large_vec<10> = ";
    for (size_t i = 0; i < large_vec.size(); ++i) {
        std::cout << large_vec[i] << " ";
    }
    std::cout << "\n\n";
    
    // Move semantics between different SBO sizes
    std::cout << "3. Move semantics between different SBO sizes:\n";
    mcu::b_vector<int, 3> temp_vec;
    temp_vec.push_back(10);
    temp_vec.push_back(20);
    temp_vec.push_back(30);
    
    mcu::b_vector<int, 8> moved_to = std::move(temp_vec);  // Move constructor
    std::cout << "After move construction: moved_to<8> = ";
    for (size_t i = 0; i < moved_to.size(); ++i) {
        std::cout << moved_to[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "\n✅ All implicit conversions work seamlessly!\n";
    
    return 0;
}