#include<iostream>
#include<vector>
#include<algorithm>
#include<functional>
#include <vector>
#include "STL_MCU.h"

using namespace mcu;

int main(){
    b_vector<int> v;
    v.resize(10,0);
    for(int i=0;i<10;i++) v[i] = i;
    std::cout << "size: " << v.size() << ", capacity: " << v.capacity() << std::endl;


}