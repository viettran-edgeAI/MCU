#include "packed_vector.h"
#include <iostream>

using namespace mcu;

int main(){
    auto init_list = MAKE_UINT16_LIST(1023, 2047, 4095, 2048, 0);
    std::cout << "Init list size: " << init_list.size() << std::endl;
    std::cout << "Init list values: ";
    for(size_t i = 0; i < init_list.size(); ++i) {
        std::cout << init_list.begin()[i] << " ";
    }
    std::cout << std::endl;

    packed_vector<12> vec12(init_list);
    std::cout << "Vec12 size: " << vec12.size() << std::endl;
    std::cout << "Vec12 capacity: " << vec12.capacity() << std::endl;
    for(size_t i = 0; i < vec12.size(); ++i) {
        std::cout << "vec12[" << i << "] = " << vec12[i] << std::endl;
    }
    return 0;
}
