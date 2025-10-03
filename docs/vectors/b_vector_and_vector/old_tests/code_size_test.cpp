#include "b_vector.cpp"

// Simple test to verify code generation efficiency
int main() {
    // Test various combinations to ensure all templates are instantiated
    mcu::b_vector<int, 2> vec2;
    mcu::b_vector<int, 4> vec4;
    mcu::b_vector<int, 8> vec8;
    mcu::b_vector<int, 16> vec16;
    
    vec2.push_back(1);
    vec4.push_back(2);
    vec8.push_back(3);
    vec16.push_back(4);
    
    // Test all conversion combinations
    mcu::b_vector<int, 8> a = vec2;   // 2->8
    mcu::b_vector<int, 4> b = vec8;   // 8->4
    mcu::b_vector<int, 16> c = vec4;  // 4->16
    mcu::b_vector<int, 2> d = vec16;  // 16->2
    
    // Test assignments  
    a = vec4;  // 4->8
    b = vec2;  // 2->4
    c = vec8;  // 8->16
    d = vec4;  // 4->2
    
    // Test const versions
    const auto& const_vec2 = vec2;
    const auto& const_vec4 = vec4;
    
    mcu::b_vector<int, 8> e = const_vec2;  // const 2->8
    mcu::b_vector<int, 4> f = const_vec4;  // const 4->4
    
    e = const_vec2;  // const assignment 2->8
    f = const_vec4;  // const assignment 4->4
    
    return a.size() + b.size() + c.size() + d.size() + e.size() + f.size();
}