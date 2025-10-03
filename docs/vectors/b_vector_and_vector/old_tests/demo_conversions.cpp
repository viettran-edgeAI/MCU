#include <iostream>
#include "vector.cpp"
#include "b_vector.cpp"

using namespace mcu;
using namespace std;

// Example functions that expect specific vector types
void process_vector(const vector<int>& v) {
    cout << "Processing vector with size: " << v.size() << endl;
    cout << "Elements: ";
    for (size_t i = 0; i < v.size(); ++i) {
        cout << v[i] << " ";
    }
    cout << endl;
}

void process_b_vector(const b_vector<int, 32>& bv) {
    cout << "Processing b_vector with size: " << bv.size() << endl;
    cout << "Elements: ";
    for (size_t i = 0; i < bv.size(); ++i) {
        cout << bv[i] << " ";
    }
    cout << endl;
}

int main() {
    cout << "=== Implicit Conversion Demo ===" << endl << endl;
    
    // Create a vector
    vector<int> v(MAKE_INT_LIST(1, 2, 3, 4, 5));
    cout << "1. Created vector with elements: 1, 2, 3, 4, 5" << endl;
    
    // Create a b_vector
    b_vector<int, 32> bv(MAKE_INT_LIST(10, 20, 30));
    cout << "2. Created b_vector with elements: 10, 20, 30" << endl << endl;
    
    cout << "3. Passing b_vector to function expecting vector:" << endl;
    process_vector(bv);  // Implicit conversion b_vector -> vector
    
    cout << endl << "4. Passing vector to function expecting b_vector:" << endl;
    process_b_vector(v);  // Implicit conversion vector -> b_vector
    
    cout << endl << "5. Assignment conversions:" << endl;
    
    // Assignment: vector = b_vector
    vector<int> v2;
    v2 = bv;  // Implicit conversion in assignment
    cout << "Assigned b_vector to vector. New vector size: " << v2.size() << endl;
    
    // Assignment: b_vector = vector
    b_vector<int, 64> bv2;
    bv2 = v;  // Implicit conversion in assignment
    cout << "Assigned vector to b_vector. New b_vector size: " << bv2.size() << endl;
    
    cout << endl << "6. Copy construction with conversion:" << endl;
    
    // Copy construction with implicit conversion
    vector<int> v3 = bv;  // b_vector -> vector
    b_vector<int, 16> bv3 = v;  // vector -> b_vector
    
    cout << "Created vector from b_vector. Size: " << v3.size() << endl;
    cout << "Created b_vector from vector. Size: " << bv3.size() << endl;
    
    cout << endl << "7. Testing with large data (heap allocation):" << endl;
    
    // Create large vector that will force b_vector to use heap
    vector<int> large_v;
    for (int i = 0; i < 50; ++i) {
        large_v.push_back(i * 2);
    }
    
    cout << "Created large vector with " << large_v.size() << " elements" << endl;
    
    // Convert to b_vector (will use heap since > SBO size)
    b_vector<int, 32> large_bv = large_v;
    cout << "Converted to b_vector. Size: " << large_bv.size() << endl;
    cout << "First few elements: " << large_bv[0] << ", " << large_bv[1] << ", " << large_bv[2] << endl;
    
    cout << endl << "=== All conversions work seamlessly! ===" << endl;
    
    return 0;
}
