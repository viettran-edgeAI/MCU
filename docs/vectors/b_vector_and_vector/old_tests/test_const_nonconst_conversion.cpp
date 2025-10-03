#include "b_vector.cpp"
#include <iostream>
#include <cassert>

// Function that takes const b_vector
void test_const_parameter(const mcu::b_vector<int, 8>& vec) {
    std::cout << "Processing const b_vector<int, 8> with " << vec.size() << " elements\n";
}

// Function that takes non-const b_vector
void test_nonconst_parameter(mcu::b_vector<int, 8>& vec) {
    std::cout << "Processing non-const b_vector<int, 8> with " << vec.size() << " elements\n";
    // Could modify vec here if needed
}

// Function that returns const b_vector
const mcu::b_vector<int, 4> get_const_vector() {
    mcu::b_vector<int, 4> vec;
    vec.push_back(10);
    vec.push_back(20);
    return vec;
}

// Function that returns non-const b_vector  
mcu::b_vector<int, 4> get_nonconst_vector() {
    mcu::b_vector<int, 4> vec;
    vec.push_back(30);
    vec.push_back(40);
    return vec;
}

int main() {
    std::cout << "Testing const and non-const implicit conversions...\n\n";
    
    // Test 1: Non-const to different SBO size
    std::cout << "=== Test 1: Non-const source ===\n";
    mcu::b_vector<int, 4> nonconst_source;
    nonconst_source.push_back(1);
    nonconst_source.push_back(2);
    nonconst_source.push_back(3);
    
    // Copy constructor with non-const source
    mcu::b_vector<int, 8> dest1 = nonconst_source;  // Should use non-const overload
    assert(dest1.size() == 3);
    assert(dest1[0] == 1 && dest1[1] == 2 && dest1[2] == 3);
    std::cout << "✓ Non-const copy constructor works\n";
    
    // Copy assignment with non-const source
    mcu::b_vector<int, 12> dest2;
    dest2 = nonconst_source;  // Should use non-const overload
    assert(dest2.size() == 3);
    assert(dest2[0] == 1 && dest2[1] == 2 && dest2[2] == 3);
    std::cout << "✓ Non-const copy assignment works\n";
    
    // Test 2: Const source to different SBO size
    std::cout << "\n=== Test 2: Const source ===\n";
    const mcu::b_vector<int, 4> const_source = get_const_vector();
    
    // Copy constructor with const source
    mcu::b_vector<int, 8> dest3 = const_source;  // Should use const overload
    assert(dest3.size() == 2);
    assert(dest3[0] == 10 && dest3[1] == 20);
    std::cout << "✓ Const copy constructor works\n";
    
    // Copy assignment with const source
    mcu::b_vector<int, 6> dest4;
    dest4 = const_source;  // Should use const overload
    assert(dest4.size() == 2);
    assert(dest4[0] == 10 && dest4[1] == 20);
    std::cout << "✓ Const copy assignment works\n";
    
    // Test 3: Function parameter passing
    std::cout << "\n=== Test 3: Function parameters ===\n";
    
    // Pass non-const to function expecting different SBO size
    test_const_parameter(nonconst_source);      // implicit conversion (creates temporary)
    // test_nonconst_parameter(nonconst_source); // This creates temporary, can't bind to non-const ref
    
    // However, explicit conversion works for non-const refs:
    mcu::b_vector<int, 8> explicit_copy = nonconst_source;
    test_nonconst_parameter(explicit_copy);     // Now this works
    
    // Pass const to function expecting different SBO size  
    test_const_parameter(const_source);         // implicit conversion
    // test_nonconst_parameter(const_source);   // This should not compile (const -> non-const)
    
    std::cout << "✓ Function parameter conversions work\n";
    
    // Test 4: Temporary objects
    std::cout << "\n=== Test 4: Temporary objects ===\n";
    
    // Assignment from temporary (const)
    mcu::b_vector<int, 8> dest5 = get_const_vector();
    assert(dest5.size() == 2);
    assert(dest5[0] == 10 && dest5[1] == 20);
    std::cout << "✓ Assignment from const temporary works\n";
    
    // Assignment from temporary (non-const)  
    mcu::b_vector<int, 8> dest6 = get_nonconst_vector();
    assert(dest6.size() == 2);
    assert(dest6[0] == 30 && dest6[1] == 40);
    std::cout << "✓ Assignment from non-const temporary works\n";
    
    // Test 5: Mixed scenarios
    std::cout << "\n=== Test 5: Mixed scenarios ===\n";
    
    mcu::b_vector<int, 2> small_vec;
    small_vec.push_back(100);
    small_vec.push_back(200);
    small_vec.push_back(300);  // Forces heap usage
    
    const mcu::b_vector<int, 2>& const_ref = small_vec;
    mcu::b_vector<int, 2>& nonconst_ref = small_vec;
    
    // Both should work
    mcu::b_vector<int, 16> from_const_ref = const_ref;
    mcu::b_vector<int, 16> from_nonconst_ref = nonconst_ref;
    
    assert(from_const_ref.size() == 3);
    assert(from_nonconst_ref.size() == 3);
    std::cout << "✓ Reference conversions work\n";
    
    std::cout << "\n🎉 All const/non-const conversion tests passed!\n";
    std::cout << "Both const and non-const b_vector objects can be implicitly converted ✅\n";
    
    return 0;
}