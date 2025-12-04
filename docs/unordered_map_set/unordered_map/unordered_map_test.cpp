#include <iostream>
#include <unordered_map>
// #include "unordered_map.h"
#include "../../../src/STL_MCU.h"
#include <cstring>
#include <random>
#include <chrono>
#include <thread>
#include <ctime> 
#include <iomanip> // For std::setprecision
// for memory usage measurement
#include <memory>
#include <atomic>

using namespace mcu;


void printunordered_map(unordered_map<uint16_t, uint16_t>& myMap) {
    std::cout << "===> Chained Unordered Map: ";
    for (auto it = myMap.begin(); it != myMap.end(); ++it) {
        uint16_t key = it->first;
        uint16_t value = it->second;
        std::cout << "(" << (int)key << ", " << value << ") ";
    }
    std::cout << std::endl;
    std::cout <<"myMap size: " << myMap.size() << std::endl;
}
// printout all map and element in chain (for debug)
void printMap(unordered_map<uint16_t, uint16_t>& myMap) {
    std::cout << "----- chain ------: " << std::endl;
    // std::cout << "chain cap_ :  " << static_cast<int>(myMap.chainCap()) << std::endl;
    //prinout used map by using rangeMap
    std::cout << "rangeMap: ";
    // for(auto map : myMap.rangeMap){
    //     std::cout << "mapID: " << (int)map.second << " - Range: " << (int)map.first << std::endl;
    // }
    // for (uint8_t i = 0; i < myMap.chainCap(); i++) {
    //     // printout all map
    //     if (myMap.chain[i] != nullptr) {
    //         std::cout << "Map " << static_cast<int>(i) << "     ";
    //         // for (auto it = myMap.chain[i]->begin(); it != myMap.chain[i]->end(); ++it) {
    //         //     std::cout << "(" << static_cast<int>(it->first) << ", " << static_cast<int>(it->second) << ") ";
    //         // }
    //         // printout range and size of current map
    //         auto range = myMap.rangeMap[i];
    //         std::cout << "-Range: "<<static_cast<int>(range) <<"     ";
    //         std::cout << "-Size: " << myMap.chain[i]->size() <<" ";
    //         std::cout << std::endl;
    //     }else{
    //         std::cout << "Map " << static_cast<int>(i) << ": Empty" << std::endl;
    //     }
    // }
    // std::cout << "total map: " << myMap.chainCap() << std::endl;
}
void printStdMap(std::unordered_map<uint16_t, uint16_t>& stdMap) {
    std::cout << "===> Std Map: ";
    for (auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        uint16_t key = it->first;
        uint16_t value = it->second;
        std::cout << "(" << (int)key << ", " << value << ") ";
    }
    std::cout << std::endl;
    std::cout <<"std map size: " << stdMap.size() << std::endl;
}

bool fake_pass_detecter(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap) {
    if(myMap.size() != stdMap.size()) {
        std::cout << "Fake pass detected !(size different)" << std::endl;
        return false;
    }
    // std::cout << "here 2" << std::endl;
    myMap.fit();
    
    for (auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        uint16_t key = it->first;
        uint16_t value = it->second;
        if (myMap.find(key) == myMap.end()) {
            std::cout << "Fake pass detected !(key not found)" << std::endl;
            return false;
        }
    }
    // std::cout << "here 3" << std::endl;
    for (auto it = myMap.begin(); it != myMap.end(); ++it) {
        uint16_t key = it->first;
        uint16_t value = it->second;
        if (stdMap.find(key) == stdMap.end()) {
            std::cout << "Fake pass detected !(ghost key)" << std::endl;
            return false;
        }
    }
    // std::cout << "here 4" << std::endl;
    return true;
}

void iterator_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 10) {  
    std::cout << "------------- Iterator test -------------" << std::endl;
    std::cout << "num_iterators: " << num_iterators << std::endl;
    myMap.clear();
    stdMap.clear();
    // myMap.set_fullness(0.9f);
    auto start = std::chrono::high_resolution_clock::now();
    int total_err = 0;
    while(num_iterators-- > 0) {
        int old_size = stdMap.size();
        int new_size = static_cast<int>(rand()%myMap.map_ability());
        if(new_size > old_size){
            while(stdMap.size() < new_size){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 5000;
                myMap.insert(key, value);
                stdMap.insert({key, value});
            }
        }else{
            while(stdMap.size() > new_size){
                uint16_t key = rand() % myMap.map_ability();
                myMap.erase(key);
                stdMap.erase(key);
            }
        }
        for(auto it = myMap.begin(); it != myMap.end(); ++it) {
            uint16_t key = it->first;
            uint16_t value = it->second;

            auto stdIt = stdMap.find(key);
            if(stdMap.find(key) == stdMap.end()) {
                // std::cout << "Iterator test failed !"<< std::endl;
                total_err++;
                return;
            }
            if(stdIt->second != value) {
                // std::cout << "Iterator test failed !"<< std::endl;
                total_err++;
                return;
            }
        }
        for(auto it = stdMap.begin(); it != stdMap.end(); ++it) {
            uint16_t key = it->first;
            uint16_t value = it->second;

            auto myIt = myMap.find(key);
            if(myMap.find(key) == myMap.end()) {
                // std::cout << "Iterator test failed !"<< std::endl;
                total_err++;
                return;
            }
            if(myIt->second != value) {
                // std::cout << "Iterator test failed !"<< std::endl;
                total_err++;
                return;
            }
        }
    }
    myMap.clear();
    stdMap.clear();
    myMap.set_fullness(0.9f);
    // compare iterators speed 
    while(myMap.size() < 50000){
        uint16_t key = rand() % myMap.map_ability();
        uint16_t value = rand() % 5000;
        myMap.insert(key, value);
        stdMap.insert({key, value});
    }
    auto start_iter = std::chrono::high_resolution_clock::now();
    for(auto it = myMap.begin(); it != myMap.end(); ++it) {
        uint16_t key = it->first;
        key++;
    }
    auto end_iter = std::chrono::high_resolution_clock::now();  
    auto my_duration_iter = std::chrono::duration_cast<std::chrono::microseconds>(end_iter - start_iter);
    auto start_std = std::chrono::high_resolution_clock::now();
    for(auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        uint16_t key = it->first;
        key++;
    }
    auto end_std = std::chrono::high_resolution_clock::now();
    auto std_duration_iter = std::chrono::duration_cast<std::chrono::microseconds>(end_std - start_std);
    if(total_err == 0){
        std::cout << "---> Passed!" << std::endl;
    }else{
        std::cout << "---> Failed !" << std::endl;
    }
    std::cout << "myMap iterator time: " << my_duration_iter.count() << " ms" << std::endl;
    std::cout << "stdMap iterator time: " << std_duration_iter.count() << " ms" << std::endl;
    std::cout << "Total errors: " << total_err << std::endl;
}


void random_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterrators = 4) {
    std::cout << "------------- Random test -------------" << std::endl;
    myMap.clear();
    stdMap.clear();

    int total_err = 0;
    int insert_err = 0;
    int erase_err = 0;
    int re_insert_err = 0;
    int re_erase_err = 0;

    if(!myMap.set_fullness(0.9f).first){
        std::cout << "Failed to set fullness!" << std::endl;
    }
    std::cout << "map ability: " << myMap.map_ability() << std::endl;
    // myMap.reserve(myMap.map_ability());
    auto start = std::chrono::high_resolution_clock::now();
    while(num_iterrators-- > 0) {
        myMap.fit();
        // std::cout <<"Iteration: " << num_iterrators << std::endl;
        int old_size = stdMap.size();
        int new_size = static_cast<int>(rand()%myMap.map_ability());
        // std::cout << "New size: " << new_size << std::endl;
        // std::cout << "Old size: " << old_size << std::endl;
        if(new_size > old_size){
            while(stdMap.size() < new_size){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 5000;
                bool my_check = myMap.insert(key, value);
                bool std_check = stdMap.insert({key, value}).second;
                // std::cout << "here 0" << std::endl;
                // std::cout << "key: " << key << std::endl;
                if(std_check){
                    if(!my_check){
                        // std::cout << "Insert error: " << key << std::endl;
                        insert_err++;
                    }
                }else{
                    if(my_check){
                        //std::cout << "Re-insert error: " << key << std::endl;
                        re_insert_err++;
                    }
                }
                // printMap(myMap);
            }
        }else{
            while(stdMap.size() > new_size){
                uint16_t key = rand() % myMap.map_ability();
                bool my_check = myMap.erase(key);
                bool std_check = stdMap.erase(key);
                // std::cout << "here 1" << std::endl;
                if(std_check){
                    if(!my_check){
                        //std::cout << "Erase error: " << key << std::endl;
                        erase_err++;
                    }
                }else{
                    if(my_check){
                        //std::cout << "Re-erase error: " << key << std::endl;
                        re_erase_err++;
                    }
                }
            }
        }
        // std::cout << "here 2" << std::endl;
    }
    total_err = insert_err + erase_err + re_insert_err + re_erase_err;
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Random test report: " << std::endl;   
    if(total_err == 0){
        std::cout << "---> Passed!" << std::endl;
        if(!myMap.set_fullness(95).first){
            std::cout << "Failed to set fullness!" << std::endl;
        }
        fake_pass_detecter(myMap, stdMap);
    }else{
        std::cout << "---> Failed !" << std::endl;
        std::cout << "Insert errors: " << insert_err << std::endl;
        std::cout << "Re-insert errors: " << re_insert_err << std::endl;
        std::cout << "Erase errors: " << erase_err << std::endl;
        std::cout << "Re-erase errors: " << re_erase_err << std::endl;
    }
    std::cout << "Total errors: " << total_err << std::endl;
    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    // printMap(myMap);
}

void sequential_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 10) {
    std::cout << "------------- Sequentially test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();

    std::cout << "num_iterators: " << num_iterators << std::endl;
    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    int total_err = 0;

    // alternately insert and erase a range of values ​​sequentially
    // if(!myMap.set_fullness(0.8).first){
    //     std::cout << "Failed to set fullness!" << std::endl;
    // }
    std::cout << "map ability: " << myMap.map_ability() << std::endl;
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        // myMap.fit();
        // std::cout << "num_iterators: " << num_iterators << std::endl;  

        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            uint16_t key = i;
            uint8_t value = rand() % 256;
            if(num_iterators % 2 == 1){
                bool std_check = stdMap.insert({key, value}).second;
                bool my_check = myMap.insert(key, value);
                if(std_check){
                    if(!my_check){
                        // std::cout << "insert failed at key: " << (int)key << std::endl;
                        insert_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-insert failed at key: " << (int)key << std::endl;
                        re_insert_error++;
                    }
                }
            }else{
                bool my_check = myMap.erase(key);
                bool std_check = stdMap.erase(key);
                if(std_check){
                    if(!my_check){
                        // std::cout << "erase failed at key: " << (int)key << std::endl;
                        erase_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-erase failed at key: " << (int)key << std::endl;
                        re_erase_error++;
                    }
                }
            }
        }
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "Sequentially test report " << std::endl;
    total_err = insert_error + re_insert_error + erase_error + re_erase_error;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        // iterator_test(myMap, stdMap);
        fake_pass_detecter(myMap, stdMap);
    }else{
        std::cout << "---> failed" << std::endl;
        std::cout << "insert error: " << insert_error << std::endl;
        std::cout << "re_insert error: " << re_insert_error << std::endl;
        std::cout << "erase error: " << erase_error << std::endl;
        std::cout << "re_erase error: " << re_erase_error << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
    //clear myMap and stdMap
}
// test for find() method
void find_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 10) {
    std::cout << "------------- Find test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();
    int total_err = 0;
    int find_error = 0;
    int re_find_error = 0;
    auto start_check = std::chrono::high_resolution_clock::now();
    unsigned long int find_time = 0;
    // std::cout << "max key: " << myMap.max_key() << std::endl;
    // std::cout << "map ability: " << myMap.map_ability() << std::endl;
    while(num_iterators-- > 0){
        int oldSize = myMap.size();
        int newSize = static_cast<int>(rand() % myMap.map_ability());
        // std::cout << "num_iterators: " << num_iterators << std::endl;
        if(newSize > oldSize){
            while(stdMap.size() < newSize){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 256;
                myMap.insert(key, value);
                stdMap.insert({key, value});
            }
        }else{
            while(stdMap.size() > newSize){
                uint16_t key = rand() % myMap.map_ability();
                myMap.erase(key);
                stdMap.erase(key);
            }  
        }
        // std::cout << "finding..." << std::endl;
        // check for find() method
        auto start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < myMap.map_ability(); i++){
            uint16_t key = i;
            auto it = stdMap.find(key);
            if(it != stdMap.end()){
                if(myMap.find(key) == myMap.end()){
                    // std::cout << "find failed at key: " << (int)key << std::endl;
                    find_error++;
                }
            }else{
                if(myMap.find(key) != myMap.end()){
                    // std::cout << "re-find failed at key: " << (int)key << std::endl;
                    re_find_error++;
                }
            }
        }
        // std::cout << "finding done!" << std::endl;
        auto end_find = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_find = end_find - start_find;

        // convert to us(microseconds):
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        find_time += elapsed_us;
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    total_err = find_error + re_find_error;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
    }
    else{
        std::cout << "---> failed" << std::endl;
        std::cout << "find error: " << find_error << std::endl;
        std::cout << "re_find error: " << re_find_error << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
    std::cout << "find time: " << static_cast<double>(find_time)/1000000 << "s" << std::endl;
}

// test for at() method
void at_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t,uint16_t>& stdMap, int num_iterators = 10) {
    std::cout << "------------- At test -------------" << std::endl;
    myMap.clear();
    stdMap.clear();
    size_t total_time = 0;

    int err_found = 0;
    int err_missing = 0;
    while (num_iterators-- > 0) {
        // grow or shrink both maps to a random target size
        int target = rand() % myMap.map_ability();
        while (stdMap.size() < static_cast<size_t>(target)) {
            uint16_t k = rand() % myMap.map_ability();
            uint16_t v = rand() % 5000;
            myMap.insert(k, v);
            // myMap[k] = v;
            stdMap.insert({k, v});
        }
        while (stdMap.size() > static_cast<size_t>(target)) {
            uint16_t k = rand() % myMap.map_ability();
            myMap.erase(k);
            stdMap.erase(k);
        }

        // validate at() on existing keys
        auto start = std::chrono::high_resolution_clock::now();
        for (auto &p : stdMap) {
            uint16_t k = p.first, v = p.second;
            try {
                if (myMap.at(k) != v) err_found++;
            } catch (...) {
                err_found++;
            }
        }
        // validate exception on missing keys
        for (uint16_t k = 0; k < myMap.map_ability(); ++k) {
            if (stdMap.find(k) == stdMap.end()) {
                try {
                    myMap.at(k);
                    err_missing++;
                } catch (const std::out_of_range&) {
                    // expected
                }
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        total_time += duration.count();
    }

    int total_err = err_found + err_missing;
    if (total_err == 0) {
        std::cout << "---> Passed!" << std::endl;
    } else {
        std::cout << "---> Failed!" << std::endl;
        std::cout << "Found errors: " << err_found << std::endl;
        std::cout << "Missing errors: " << err_missing << std::endl;
    }
    std::cout << "Total time: " << static_cast<double>(total_time) / 1000 << "ms" << std::endl;
    std::cout << "Total errors: " << total_err << std::endl;
}




void first_benchmark(unordered_map<uint16_t, uint16_t>& myMap,std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterrators = 10) {
    std::cout << "------------- First benchmark -------------" << std::endl;
    std::cout <<"---> random benchmark: " << std::endl;
    myMap.clear();
    stdMap.clear();
    // myMap.set_fullness(80);
    int iteration = num_iterrators;
    auto start = std::chrono::high_resolution_clock::now();
    // myMap.reserve(myMap.map_ability());
    while(iteration-- > 0) {
        // std::cout <<"Iteration: " << iteration << std::endl;
        int old_size = myMap.size();
        int new_size = static_cast<int>(rand()%myMap.map_ability());
        // std::cout << "New size: " << new_size << std::endl;
        if(new_size > old_size){
            while(myMap.size() < new_size){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 50000;
                // myMap.insert(key, value);
                myMap[key] = value;
            }
        }else{
            while(myMap.size() > new_size){
                uint16_t key = rand() % myMap.map_ability();
                myMap.erase(key);
            }
        }
        // fake_pass_detecter(myMap, stdMap);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "First benchmark report: " << std::endl;
    std::cout << "myMap Time: " << duration.count() << " ms" << std::endl;
    // benchmark for std::unordered_map
    start = std::chrono::high_resolution_clock::now();
    iteration = num_iterrators;
    while(iteration-- > 0) {
        // std::cout <<"Iteration: " << iteration << std::endl;
        int old_size = stdMap.size();
        int new_size = static_cast<int>(rand()%myMap.map_ability());
        if(new_size > old_size){
            while(stdMap.size() < new_size){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 50000;
                stdMap.insert({key, value});
            }
        }else{
            while(stdMap.size() > new_size){
                uint16_t key = rand() % myMap.map_ability();
                stdMap.erase(key);
            }
        }
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "StdMap Time : " << duration.count() << " ms" << std::endl;
    std::cout << "-------> sequentially benchmark: " << std::endl;
    myMap.clear();
    stdMap.clear();
    // myMap.reserve(myMap.map_ability());
    iteration = num_iterrators;
    start = std::chrono::high_resolution_clock::now();
    while(iteration-- > 0) {
        // std::cout <<"Iteration: " << iteration << std::endl;
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            uint16_t key = i;
            uint16_t value = rand() % 256;
            if(iteration % 2 == 1){
                myMap.insert(key, value);
            }else{
                myMap.erase(key);
            }
        }
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "myMap Time : " << duration.count() << " ms" << std::endl;;
    // benchmark for std::unordered_map
    start = std::chrono::high_resolution_clock::now();
    iteration = num_iterrators;
    while(iteration-- > 0) {
        // std::cout <<"Iteration: " << iteration << std::endl;
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            uint16_t key = i;
            uint8_t value = rand() % 256;
            if(iteration % 2 == 1){
                stdMap.insert({key, value});
            }else{
                stdMap.erase(key);
            }
        }
    }
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "StdMap Time : " << duration.count() << " ms" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
}

// one counter for *all* allocations
static std::atomic<size_t> g_bytes_allocated{0};

template<typename T>
struct CountingAllocator {
  using value_type = T;

  CountingAllocator() = default;
  template<typename U>
  constexpr CountingAllocator(const CountingAllocator<U>&) noexcept {}

  T* allocate(size_t n) {
    size_t bytes = n * sizeof(T);
    g_bytes_allocated += bytes;
    return static_cast<T*>(::operator new(bytes));
  }

  void deallocate(T* p, size_t n) noexcept {
    size_t bytes = n * sizeof(T);
    g_bytes_allocated -= bytes;
    ::operator delete(p);
  }
};

// compare memory usage between my custom unordered_map_s and regular std::unordered_map
void memory_usage_comparison(unordered_map<uint16_t, uint16_t>& myMap,std::unordered_map<uint16_t, uint16_t>& /*unused*/) {
    std::cout << "------------- Memory usage compare -------------\n";

    // reset the single global counter
    g_bytes_allocated = 0;

    // make a std::unordered_map that uses our allocator
    using SM = std::unordered_map<
    uint16_t,uint16_t,
    std::hash<uint16_t>,
    std::equal_to<uint16_t>,
    CountingAllocator<std::pair<const uint8_t,uint8_t>>>;
    SM cmap;

    // bump both maps equally
    myMap.clear();
    cmap.clear();
    // myMap.set_fullness(80);
    while(myMap.size() < myMap.map_ability()){
        uint16_t k = rand() % myMap.map_ability();
        uint16_t v = rand() % myMap.map_ability();
        myMap.insert(k, v);
        cmap.insert({k, v});
    }
    std::cout << "myMap total bytes:  " << myMap.memory_usage() << "\n";
    std::cout << "fit() saved : " << myMap.fit() << " bytes\n";

    size_t std_bytes = g_bytes_allocated.load();


    std::cout << "After fit() - myMap total bytes:  " << myMap.memory_usage()  << "\n";
    std::cout << "stdMap heap bytes:  " << std_bytes << "\n";
    std::cout << "fullness: " << myMap.get_fullness() << "\n";
    std::cout << "myMap ability: " << myMap.map_ability() << "\n";
}


// Part 1: Benchmark for finding elements that EXIST in the map
void searching_benchmark_hits(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 40) {
    myMap.clear();
    stdMap.clear();
    
    // Pre-populate with known elements - use stack allocation for embedded
    const uint16_t TEST_SIZE = 200; // Fixed size for embedded predictability
    uint16_t existing_keys[TEST_SIZE];
    uint16_t key_count = 0;
    
    // Fill both maps with same data
    while(key_count < TEST_SIZE && myMap.size() < myMap.map_ability() * 0.7) {
        uint16_t key = (key_count * 13 + 7) % myMap.map_ability(); // Deterministic pattern
        uint16_t value = key ^ 0xAAAA; // Simple value generation
        
        if(myMap.insert(key, value)) {
            if(stdMap.insert({key, value}).second) {
                existing_keys[key_count] = key;
                key_count++;
            }
        }
    }
    
    uint32_t my_hit_time = 0;
    uint32_t std_hit_time = 0;
    uint16_t checksum = 0; // Prevent optimization
    
    // Benchmark HITS (elements that exist)
    for(int iter = 0; iter < num_iterators; iter++) {
        
        // Test std::unordered_map hits
        auto start = std::chrono::high_resolution_clock::now();
        for(uint16_t i = 0; i < key_count; i++) {
            auto it = stdMap.find(existing_keys[i]);
            if(it != stdMap.end()) {
                checksum += it->second; // Use result
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        std_hit_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // Test unordered_map hits
        start = std::chrono::high_resolution_clock::now();
        for(uint16_t i = 0; i < key_count; i++) {
            auto it = myMap.find(existing_keys[i]);
            if(it != myMap.end()) {
                checksum += it->second; // Use result
            }
        }
        end = std::chrono::high_resolution_clock::now();
        my_hit_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
    
    // Results
    std::cout << "=== HITS BENCHMARK (Elements in map) ===" << std::endl;
    std::cout << "Tested " << key_count << " existing keys, " << num_iterators << " iterations" << std::endl;
    std::cout << "myMap hits time: " << static_cast<double>(my_hit_time)/1000000 << "s" << std::endl;
    std::cout << "stdMap hits time: " << static_cast<double>(std_hit_time)/1000000 << "s" << std::endl;
    std::cout << "Hit ratio (myMap/stdMap): " << static_cast<double>(my_hit_time)/std_hit_time << "x" << std::endl;
    std::cout << "Checksum: " << checksum << " (prevents optimization)" << std::endl;
    std::cout << "Map fullness: " << myMap.get_fullness() << std::endl;
}

// Part 2: Benchmark for finding elements that DON'T EXIST in the map  
void searching_benchmark_misses(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 40) {
    // Keep existing data from previous test or populate if empty
    if(myMap.empty()) {
        const uint16_t FILL_SIZE = 150;
        for(uint16_t i = 0; i < FILL_SIZE; i++) {
            uint16_t key = (i * 17 + 3) % myMap.map_ability();
            uint16_t value = key ^ 0x5555;
            myMap.insert(key, value);
            stdMap.insert({key, value});
        }
    }
    
    // Generate non-existent keys - use deterministic pattern for embedded
    const uint16_t MISS_SIZE = 200;
    uint16_t missing_keys[MISS_SIZE];
    uint16_t miss_count = 0;
    
    // Generate keys that definitely don't exist
    for(uint16_t i = 0; i < myMap.map_ability() && miss_count < MISS_SIZE; i++) {
        uint16_t test_key = (i * 23 + 11) % myMap.map_ability(); // Different pattern
        if(myMap.find(test_key) == myMap.end()) {
            missing_keys[miss_count] = test_key;
            miss_count++;
        }
    }
    
    uint32_t my_miss_time = 0;
    uint32_t std_miss_time = 0;
    uint16_t miss_checksum = 0; // Prevent optimization
    
    // Benchmark MISSES (elements that don't exist)
    for(int iter = 0; iter < num_iterators; iter++) {
        
        // Test std::unordered_map misses
        auto start = std::chrono::high_resolution_clock::now();
        for(uint16_t i = 0; i < miss_count; i++) {
            auto it = stdMap.find(missing_keys[i]);
            miss_checksum += (it == stdMap.end()) ? 1 : 0; // Count misses
        }
        auto end = std::chrono::high_resolution_clock::now();
        std_miss_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        // Test unordered_map misses
        start = std::chrono::high_resolution_clock::now();
        for(uint16_t i = 0; i < miss_count; i++) {
            auto it = myMap.find(missing_keys[i]);
            miss_checksum += (it == myMap.end()) ? 1 : 0; // Count misses
        }
        end = std::chrono::high_resolution_clock::now();
        my_miss_time += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
    
    // Results
    std::cout << "=== MISSES BENCHMARK (Elements not in map) ===" << std::endl;
    std::cout << "Tested " << miss_count << " non-existent keys, " << num_iterators << " iterations" << std::endl;
    std::cout << "myMap miss time: " << static_cast<double>(my_miss_time)/1000000 << "s" << std::endl;
    std::cout << "stdMap miss time: " << static_cast<double>(std_miss_time)/1000000 << "s" << std::endl;
    std::cout << "Miss ratio (myMap/stdMap): " << static_cast<double>(my_miss_time)/std_miss_time << "x" << std::endl;
    std::cout << "Miss checksum: " << miss_checksum << " (should be " << miss_count * num_iterators * 2 << ")" << std::endl;
    std::cout << "Map fullness: " << myMap.get_fullness() << std::endl;
}

// Combined function to run both benchmarks
void searching_benchmark_split(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 40) {
    std::cout << "========== SPLIT SEARCHING BENCHMARK ==========" << std::endl;
    
    searching_benchmark_hits(myMap, stdMap, num_iterators);
    std::cout << std::endl;
    searching_benchmark_misses(myMap, stdMap, num_iterators);
    
    std::cout << "===============================================" << std::endl;
    
    // Clean up
    myMap.clear();
    stdMap.clear();
}

// searching benchmark with the trio : [] operator, find(), at()
void searching_benchmark2(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap, int num_iterators = 40) {
    std::cout << "------------- Searching benchmark -------------" << std::endl;
    myMap.clear();
    stdMap.clear();
    int total_iterators = num_iterators;

    auto start_check = std::chrono::high_resolution_clock::now();
    
    unsigned long int my_find_time = 0;
    unsigned long int std_find_time = 0;
    
    unsigned long int my_at_time = 0;
    unsigned long int std_at_time = 0;

    unsigned long int my_operator_time = 0;
    unsigned long int std_operator_time = 0;
   
    // operator[] benchmark
    while(total_iterators-- > 0){
        // std::cout << "num_iterators: " << num_iterators << std::endl;
        int oldSize = myMap.size();
        int newSize = static_cast<int>(rand() % myMap.map_ability());
        uint16_t track[newSize];
        uint16_t cursor = 0;
        if(newSize > oldSize){
            while(stdMap.size() < newSize){
                uint16_t key = rand() % myMap.map_ability();
                uint16_t value = rand() % 40000;
                myMap.insert(key, value);
                if(stdMap.insert({key, value}).second){
                    track[cursor++] = key;  
                } 
            }
        }else{
            while(stdMap.size() > newSize){
                uint16_t key = rand() % myMap.map_ability();
                stdMap.erase(key);
                myMap.erase(key);
            }  
        };

        // check for [] operator
        auto start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            uint16_t value = stdMap[key];
        }
        auto end_find  = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_find = end_find - start_find;
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        std_operator_time += elapsed_us;

        start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            uint16_t value = myMap[key];
        }
        end_find   = std::chrono::high_resolution_clock::now();
        elapsed_find = end_find - start_find;
        elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        my_operator_time += elapsed_us;
        // check for find() method
        start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            auto it = stdMap.find(key);
            if(it != stdMap.end()){
                uint16_t value = it->second;
            }
        }
        end_find   = std::chrono::high_resolution_clock::now();
        elapsed_find = end_find - start_find;
        elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        std_find_time += elapsed_us;
        start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            auto it = myMap.find(key);
            if(it != myMap.end()){
                uint16_t value = it->second;
            }
        }
        end_find   = std::chrono::high_resolution_clock::now();
        elapsed_find = end_find - start_find;
        elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        my_find_time += elapsed_us;
        // check for at() method
        start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            uint16_t value = stdMap.at(key);
        }
        end_find   = std::chrono::high_resolution_clock::now();
        elapsed_find = end_find - start_find;
        elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        std_at_time += elapsed_us;
        start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < cursor; i++){
            uint16_t key = track[i];
            uint16_t value = myMap.at(key);
        }
        end_find   = std::chrono::high_resolution_clock::now();
        elapsed_find = end_find - start_find;
        elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        my_at_time += elapsed_us;
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    std::cout << " - MyMap:" << std::endl;
    std::cout << "   - [] operator time: " << static_cast<double>(my_operator_time)/1000000 << "s" << std::endl;
    std::cout << "   - find() time: " << static_cast<double>(my_find_time)/1000000 << "s" << std::endl;
    std::cout << "   - at() time: " << static_cast<double>(my_at_time)/1000000 << "s" << std::endl;
    std::cout << " - StdMap:" << std::endl;
    std::cout << "   - [] operator time: " << static_cast<double>(std_operator_time)/1000000 << "s" << std::endl;
    std::cout << "   - find() time: " << static_cast<double>(std_find_time)/1000000 << "s" << std::endl;
    std::cout << "   - at() time: " << static_cast<double>(std_at_time)/1000000 << "s" << std::endl;
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
    std::cout << "fullness: " << myMap.get_fullness() << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();
}   
// fullness test
void fullness_test(std::vector<float> fullness_levels = {0.7f, 0.8f, 0.9f, 1.0f}, int num_iterators = 40) {
    std::cout << "------------- Robust Fullness Test -------------" << std::endl;
    
    // Structure to store test results for comparison
    struct FullnessResult {
        float fullness;
        double insert_time;
        double find_time;
        double erase_time;
        size_t memory_bytes;
    };
    std::vector<FullnessResult> results;

    for (float level : fullness_levels) {
        std::cout << "\nTesting fullness: " << level << std::endl;
        unordered_map<uint16_t, uint16_t> myMap;
        myMap.set_fullness(level);
        
        FullnessResult result;
        result.fullness = level;
        result.memory_bytes = 0;
        
        // Store keys for lookup operations
        uint16_t keys[65535] = {0};
        int key_count = 0;
        
        // ----- INSERT PERFORMANCE -----
        auto start = std::chrono::high_resolution_clock::now();
        int count = num_iterators;
        key_count = 0;
        
        while (count-- > 0) {
            if (count % 10 == 0) myMap.clear(); // Periodically reset
            
            int start_index = rand() % (myMap.map_ability());
            int end_index = start_index + rand() % (myMap.map_ability());
            
            for (uint16_t i = start_index; i < end_index && i < 65535; i++) {
                uint16_t value = rand() % 50000;
                if (myMap.insert(i, value) && key_count < 65535) {
                    keys[key_count++] = i;
                }
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        result.insert_time = std::chrono::duration<double>(end - start).count();
        std::cout << "Insert time: " << result.insert_time << "s" << std::endl;
        
        // ----- FIND PERFORMANCE -----
        start = std::chrono::high_resolution_clock::now();
        count = num_iterators * 10; // More lookups than inserts
        
        while (count-- > 0) {
            // Mix of successful and unsuccessful lookups
            bool use_existing = (count % 3 != 0); // 2/3 lookups for existing keys
            uint16_t key;
            
            if (use_existing && key_count > 0) {
                key = keys[rand() % key_count]; // Existing key
            } else {
                key = rand() % myMap.map_ability(); // Possibly non-existent key
            }
            
            auto it = myMap.find(key);
            if (it != myMap.end()) {
                volatile uint16_t val = it->second; // Prevent optimization
            }
        }
        end = std::chrono::high_resolution_clock::now();
        result.find_time = std::chrono::duration<double>(end - start).count();
        std::cout << "Find time: " << result.find_time << "s" << std::endl;
        
        // ----- ERASE PERFORMANCE -----
        start = std::chrono::high_resolution_clock::now();
        count = num_iterators;
        
        while (count-- > 0) {
            if (count % 10 == 0) {
                // Refill the map periodically
                for (int i = 0; i < key_count; i++) {
                    myMap.insert(keys[i], rand() % 50000);
                }
            }
            
            // Erase some keys
            int erases = rand() % key_count;
            for (int i = 0; i < erases; i++) {
                uint16_t key = keys[rand() % key_count];
                myMap.erase(key);
            }
        }
        end = std::chrono::high_resolution_clock::now();
        result.erase_time = std::chrono::duration<double>(end - start).count();
        std::cout << "Erase time: " << result.erase_time << "s" << std::endl;
        
        // Measure memory usage
        myMap.clear();
        int target_size = myMap.map_ability() * 0.8; // Fill to 80% of capacity
        while (myMap.size() < target_size) {
            uint16_t key = rand() % myMap.map_ability();
            uint16_t value = rand() % 256;
            myMap.insert(key, value);
        }
        result.memory_bytes = myMap.memory_usage();
        std::cout << "Memory usage: " << result.memory_bytes << " bytes" << std::endl;
        
        results.push_back(result);
    }
    
    // Print comparative results
    std::cout << "\n------------- FULLNESS COMPARISON -------------" << std::endl;
    std::cout << "Fullness | Insert Time | Find Time | Erase Time | Memory (bytes)" << std::endl;
    
    // Use highest fullness as baseline (usually 1.0)
    const FullnessResult& baseline = results.back();
    
    for (const auto& r : results) {
        // Calculate percentages relative to baseline
        int insert_pct = static_cast<int>(r.insert_time / baseline.insert_time * 100);
        int find_pct = static_cast<int>(r.find_time / baseline.find_time * 100);
        int erase_pct = static_cast<int>(r.erase_time / baseline.erase_time * 100);
        int memory_pct = static_cast<int>(static_cast<float>(r.memory_bytes) / baseline.memory_bytes * 100);
        
        std::cout << std::fixed << std::setprecision(1) 
                  << r.fullness << " | " 
                  << r.insert_time << "s (" << insert_pct << "%) | "
                  << r.find_time << "s (" << find_pct << "%) | "
                  << r.erase_time << "s (" << erase_pct << "%) | "
                  << r.memory_bytes << " (" << memory_pct << "%)" 
                  << std::endl;
    }
    
    std::cout << "\nNote: Percentages show performance relative to baseline (highest fullness)" << std::endl;
    std::cout << "Lower percentages for timing indicate better performance" << std::endl;
}

// test for fit() and set_fullness() methods in case have some gaps in the map
void fullness_test2(){
    std::cout << "------------- Fullness Test 2 -------------" << std::endl;
    unordered_map<uint16_t, uint16_t> myMap;
    std::unordered_map<uint16_t, uint16_t> stdMap;
    // myMap.set_fullness(0.8);
    myMap.clear();
    myMap.reserve(10000);
    // make some gaps between maps in chain
    for(int i = 0; i < 10000; i++){
        int value = rand() % 10000;
        myMap.insert(i,value);
        stdMap.insert({i, value});
    }
    for(int i = 3000; i < 7000; i++){
        myMap.erase(i);
        stdMap.erase(i);
    }
    myMap.fit();
    // myMap.set_fullness(90);
    if(fake_pass_detecter(myMap, stdMap)){
        std::cout << "Fullness test 2 passed!" << std::endl;
    }else{
        std::cout << "Fullness test 2 failed!" << std::endl;
    }
}

// constructors and assignments test
void constructors_test(unordered_map<uint16_t, uint16_t>& myMap, std::unordered_map<uint16_t, uint16_t>& stdMap) {
    std::cout << "------------- Constructors test -------------" << std::endl;
    int total_err = 0;
    unordered_map<uint16_t, uint16_t> myMap1 = myMap;  // backup myMap
    // copy constructor
    unordered_map<uint16_t, uint16_t> myMap2(myMap);
    if(!fake_pass_detecter(myMap2,stdMap)){
        std::cout << "copy constructor failed!" << std::endl;
        total_err++;
    }

    // move constructor
    unordered_map<uint16_t, uint16_t> myMap3(std::move(myMap));
    if(!fake_pass_detecter(myMap3,stdMap)){
        std::cout << "move constructor failed!" << std::endl;
        total_err++;
    }
    myMap = myMap1; // restore myMap

    // copy assignment
    unordered_map<uint16_t, uint16_t> myMap4;
    myMap4 = myMap;
    if(!fake_pass_detecter(myMap4,stdMap)){
        std::cout << "copy assignment failed!" << std::endl;
        total_err++;
    }

    // move assignment
    unordered_map<uint16_t, uint16_t> myMap5;
    myMap5 = std::move(myMap);
    if(!fake_pass_detecter(myMap5,stdMap)){
        std::cout << "move assignment failed!" << std::endl;
        total_err++;
    }

    myMap = myMap1; // restore myMap
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
    }else{
        std::cout << "---> failed" << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
}


int main(){
    srand(time(0));
    unordered_map<uint16_t, uint16_t> myMap;
    std::unordered_map<uint16_t, uint16_t> stdMap;
    int num_iterrators = 100;
    auto start = std::chrono::high_resolution_clock::now();

    random_test(myMap, stdMap, num_iterrators);
    constructors_test(myMap, stdMap);
    sequential_test(myMap, stdMap, num_iterrators);
    constructors_test(myMap, stdMap);
    iterator_test(myMap, stdMap, num_iterrators);
    find_test(myMap, stdMap, num_iterrators);
    at_test(myMap, stdMap, num_iterrators);
    first_benchmark(myMap, stdMap);
    searching_benchmark_split(myMap, stdMap, num_iterrators);
    searching_benchmark2(myMap, stdMap, num_iterrators);
    fullness_test();
    memory_usage_comparison(myMap, stdMap);
    fullness_test2();

    // unordered_map<int, std::string> myMap2;
    // for(int i= 0; i < 10000; i++){
    //     myMap2.insert(i, "test" + std::to_string(i));
    // }
    // for(auto it = myMap2.begin(); it != myMap2.end(); ++it){
    //     std::cout << "Key: " << it->first << ", Value: " << it->second << std::endl;
    // }


    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "=====> Total testing & benchmark time: " << elapsed_seconds.count() << "s" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    return 0;
}