#include <iostream>
#include <unordered_map>
#include "unordered_map.h"
#include <cstring>
#include <random>
#include <chrono>
#include <iomanip> // For std::setprecision
#include <thread>
#include <ctime> 

// for memory usage measurement
#include <memory>
#include <atomic>

/*
--------------------------------------------------------------------------------------------------------------------------
--------------------------------------------------- TESTING --------------------------------------------------------------
--------------------------------------------------------------------------------------------------------------------------
*/

void print_map(const unordered_map<uint8_t, uint8_t>& m){
    std::cout << "------------- Print map -------------\n";
    for (auto& kv : m) {
        std::cout << int(kv.first) << "|" << int(kv.second) << " - ";
    }
    std::cout << "\n--------------------------------------\n";
}

void print_std_map(const std::unordered_map<uint8_t, uint8_t>& m){
    std::cout << "------------- Print std map -------------\n";
    for (auto& kv : m) {
        std::cout << int(kv.first) << "|" << int(kv.second) << " - ";
    }
    std::cout << "\n------------------------------------------\n";
}

// check exact similarity between myMap and stdMap
bool fake_pass_detecter(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap) {
    if(myMap.size() != stdMap.size()) {
        std::cout << "Fake pass detected !" << std::endl;
        return false;
    }

    for (auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        uint8_t key = it->first;
        uint8_t value = it->second;
        if (myMap.find(key) == myMap.end()) {
            std::cout << "Fake pass detected !" << std::endl;
            return false;
        }
    }
    for (auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        uint8_t key = it->first;
        uint8_t value = it->second;
        uint8_t myValue = myMap[key];
        if (myValue != value) {
            std::cout << "Fake pass detected !" << std::endl;
            return false;
        }
    }
    return true;
}

// constructors and assignments test
void constructors_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap) {
    std::cout << "------------- Constructors test -------------" << std::endl;
    unordered_map<uint8_t, uint8_t> myMap1 = myMap;  // backup myMap
    // copy constructor
    unordered_map<uint8_t, uint8_t> myMap2(myMap);
    fake_pass_detecter(myMap2,stdMap);

    // move constructor
    unordered_map<uint8_t, uint8_t> myMap3(std::move(myMap));
    fake_pass_detecter(myMap3,stdMap);
    myMap = myMap1; // restore myMap

    // copy assignment
    unordered_map<uint8_t, uint8_t> myMap4;
    myMap4 = myMap;
    fake_pass_detecter(myMap4,stdMap);

    // move assignment
    unordered_map<uint8_t, uint8_t> myMap5;
    myMap5 = std::move(myMap);
    fake_pass_detecter(myMap5,stdMap);

    myMap = myMap1; // restore myMap

    std::cout << "--> done !" << std::endl;
}

// insert and delete elements randomly.
void random_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- Random test -------------" << std::endl;

    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    int total_err = 0;

    // std::mt19937 rng(0); // Fixed seed for reproducibility
    // std::uniform_int_distribution<uint8_t> dist(0, 255);
    std::cout << "num_iterators: " << num_iterators << std::endl;
    // myMap.set_fullness(0.8);
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        // myMap.set_fullness(0.95);
        // myMap.fit();
        // std::cout << "num_iterators: " << num_iterators << std::endl;
        int old_size = stdMap.size();
        int new_size = static_cast<int>(rand() % myMap.map_ability());
        if(new_size > old_size){
            while(stdMap.size() < new_size){
                uint8_t key = rand() % 256;
                uint8_t value = rand() % 256;
                bool std_check = stdMap.insert({key, value}).second;
                bool my_check = myMap.insert(key, value).second;
                if(std_check){
                    if(!my_check){
                        std::cout << "insert failed at key: " << (int)key << std::endl;
                        insert_error++;
                    }
                }else{
                    if(my_check){
                        std::cout << "re-insert failed at key: " << (int)key << std::endl;
                        re_insert_error++;
                    }
                }
            }
        }else{
            while(stdMap.size() > new_size){
                uint8_t key = rand() % 256;
                bool std_check = stdMap.erase(key);
                bool my_check = myMap.erase(key);
                if(std_check){
                    if(!my_check){
                        std::cout << "erase failed at key: " << (int)key << std::endl;
                        erase_error++;
                    }
                }else{
                    if(my_check){
                        std::cout << "re-erase failed at key: " << (int)key << std::endl;
                        re_erase_error++;
                    }
                }
            }  
        }
        // fake_pass_detecter(myMap, stdMap);
        // myMap.fit();
    }
    total_err = insert_error + re_insert_error + erase_error + re_erase_error;
    auto end_check = std::chrono::high_resolution_clock::now(); 
    std::cout << "Random test report " << std::endl;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        fake_pass_detecter(myMap, stdMap);
    }else{
        std::cout << "---> failed" << std::endl;
        std::cout << "insert error: " << insert_error << std::endl;
        std::cout << "re_insert error: " << re_insert_error << std::endl;
        std::cout << "erase error: " << erase_error << std::endl;
        std::cout << "re_erase error: " << re_erase_error << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
}

void random_test_2(unordered_map<int, int> & myMap, std::unordered_map<int, int>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- Random test 2 -------------" << std::endl;

    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    int total_err = 0;

    std::cout << "num_iterators: " << num_iterators << std::endl;
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        // myMap.set_fullness(0.95);
        // myMap.fit();
        // std::cout << "num_iterators: " << num_iterators << std::endl;
        int old_size = stdMap.size();
        int new_size = static_cast<int>(rand() % myMap.map_ability());
        // std::cout << "old_size: " << old_size << " new_size: " << new_size << std::endl;
        if(new_size > old_size){
            while(stdMap.size() < new_size){
                int key = rand();
                int value = rand();
                bool std_check = stdMap.insert({key, value}).second;
                bool my_check = myMap.insert(key, value).second;
                if(std_check){
                    if(!my_check){
                        // std::cout << "insert failed at key: " << key << std::endl;
                        insert_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-insert failed at key: " << key << std::endl;
                        re_insert_error++;
                    }
                }
            }
        }else{
            while(stdMap.size() > new_size){
                int key = stdMap.begin()->first; // Get a random key from stdMap
                bool std_check = stdMap.erase(key);
                bool my_check = myMap.erase(key);
                if(std_check){
                    if(!my_check){
                        // std::cout << "erase failed at key: " << key << std::endl;
                        erase_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-erase failed at key: " << key << std::endl;
                        re_erase_error++;
                    }
                }
            }  
        }
        // fake_pass_detecter(myMap, stdMap);
        // myMap.fit();
    }
    total_err = insert_error + re_insert_error + erase_error + re_erase_error;
    auto end_check = std::chrono::high_resolution_clock::now(); 
    std::cout << "Random test report " << std::endl;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        // fake_pass_detecter(myMap, stdMap);
    }else{
        std::cout << "---> failed" << std::endl;
        std::cout << "insert error: " << insert_error << std::endl;
        std::cout << "re_insert error: " << re_insert_error << std::endl;
        std::cout << "erase error: " << erase_error << std::endl;
        std::cout << "re_erase error: " << re_erase_error << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
}

void sequentially_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {    
    std::cout << "------------- Sequentially test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();

    std::mt19937 rng(0); // Fixed seed for reproducibility
    std::uniform_int_distribution<uint8_t> dist(0, 255);

    std::cout << "num_iterators: " << num_iterators << std::endl;
    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    int total_err = 0;

    // alternately insert and erase a range of values ​​sequentially
    // myMap.set_fullness(0.9);
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        // std::cout << "num_iterators: " << num_iterators << std::endl;  

        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            uint8_t key = i;
            uint8_t value = rand() % 256;
            if(num_iterators % 2 == 1){
                bool std_check = stdMap.insert({key, value}).second;
                bool my_check = myMap.insert(key, value).second;
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
}

// fullness test
void fullness_test(std::vector<float> fullness_levels = {0.7f, 0.8f, 0.9f, 1.0f}, int num_iterators = 10000) {
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
        unordered_map<uint8_t, uint8_t> myMap;
        myMap.set_fullness(level);
        
        FullnessResult result;
        result.fullness = level;
        result.memory_bytes = 0;
        
        // Store keys for lookup operations
        uint8_t keys[255] = {0};
        int key_count = 0;
        
        // ----- INSERT PERFORMANCE -----
        auto start = std::chrono::high_resolution_clock::now();
        int count = num_iterators;
        key_count = 0;
        
        while (count-- > 0) {
            if (count % 10 == 0) myMap.clear(); // Periodically reset
            
            int start_index = rand() % (myMap.map_ability() / 2);
            int end_index = start_index + rand() % (myMap.map_ability() / 2);
            
            for (uint8_t i = start_index; i < end_index && i < 255; i++) {
                uint8_t value = rand() % 256;
                if (myMap.insert(i, value).second && key_count < 255) {
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
            uint8_t key;
            
            if (use_existing && key_count > 0) {
                key = keys[rand() % key_count]; // Existing key
            } else {
                key = rand() % 256; // Possibly non-existent key
            }
            
            auto it = myMap.find(key);
            if (it != myMap.end()) {
                volatile uint8_t val = it->second; // Prevent optimization
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
                    myMap.insert(keys[i], rand() % 256);
                }
            }
            
            // Erase some keys
            int erases = rand() % key_count;
            for (int i = 0; i < erases; i++) {
                uint8_t key = keys[rand() % key_count];
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
            uint8_t key = rand() % 256;
            uint8_t value = rand() % 256;
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

// Add this line to main() to run the test
// fullness_test({0.7f, 0.8f, 0.9f, 1.0f}, 5000);


void operator_test2() {
    unordered_map<uint8_t, uint8_t> myMap;
    std::unordered_map<uint8_t, uint8_t> stdMap;
    
    // First, generate all our key-value pairs
    while(myMap.size() < 50) {
        uint8_t key = rand() % 256;
        uint8_t value = rand() % 256;
        myMap[key] = value;
        stdMap[key] = value;
    }

    print_map(myMap);
    print_std_map(stdMap);
    
}
void operator_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- operator[] test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();

    int total_err = 0;
    int for_test = 0;
    auto start_check = std::chrono::high_resolution_clock::now();
    unsigned long int find_time = 0;
    // myMap.reserve(myMap.map_ability());
    while(num_iterators-- > 0){
        // std::cout << "num_iterators: " << num_iterators << std::endl;
        int oldSize = myMap.size();
        int newSize = static_cast<int>(rand() % myMap.map_ability());
        if(newSize > oldSize){
            while(stdMap.size() < newSize){
                uint8_t key = rand() % 256;
                uint8_t value = rand() % 256;
                // std::cout << "here 0" << std::endl;
                myMap[key] = value;
                stdMap[key] = value;
                // std::cout << "here 1" << std::endl;
            }
        }else{
            while(stdMap.size() > newSize){
                uint8_t key = rand() % 256;
                stdMap.erase(key);
                myMap.erase(key);
            }  
        }
        //print_map(myMap);
        // fake_pass_detecter(myMap, stdMap);

        // check for [] operator
        auto start_find = std::chrono::high_resolution_clock::now();
        for(auto it = stdMap.begin(); it != stdMap.end(); ++it){
            uint8_t key = it->first;
            uint8_t value = it->second;
            uint8_t myValue = myMap[key];
            if(myValue != value){
                // std::cout << "[] operator failed at key: " << (int)key << std::endl;
                total_err++;
            }
        }
        auto end_find   = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_find = end_find - start_find;

        // convert to us(microseconds):
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        find_time += elapsed_us;
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        fake_pass_detecter(myMap, stdMap);
    }
    else{
        std::cout << "---> failed" << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
    std::cout << "[] operator time: " << static_cast<double>(find_time)/1000000 << "s" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();
}

// test for find() method
void find_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- Find test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();
    int total_err = 0;
    int find_error = 0;
    int re_find_error = 0;
    auto start_check = std::chrono::high_resolution_clock::now();
    unsigned long int find_time = 0;
    int total_iterators = num_iterators;
    // randomly insert and delete elements
    while(total_iterators-- > 0){
        int oldSize = myMap.size();
        int newSize = static_cast<int>(rand() % myMap.map_ability());
        // std::cout << "num_iterators: " << total iterators << std::endl;
        if(newSize > oldSize){
            while(stdMap.size() < newSize){
                uint8_t key = rand() % 256;
                uint8_t value = key;
                myMap.insert(key, value);
                // myMap[key] = value;
                stdMap.insert({key, value});
            }
        }else{
            while(stdMap.size() > newSize){
                uint8_t key = rand() % 256;
                myMap.erase(key);
                stdMap.erase(key);
            }  
        }
        // check for find() method
        auto start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < 256; i++){
            uint8_t key = i;
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
        // inside find_test(), replace:
        auto end_find   = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_find = end_find - start_find;

        // convert to us(microseconds):
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        find_time += elapsed_us;
    }
    std::cout << "random find time: " << static_cast<double>(find_time)/1000000 << "s" << std::endl;
    find_time = 0;

    // sequentially insert and delete elements
    total_iterators = num_iterators;
    while(total_iterators-- > 0){
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            uint8_t key = i;
            uint8_t value = key;
            if(num_iterators % 2 == 1){
                myMap.insert(key, value);
                stdMap.insert({key, value});
            }else{
                myMap.erase(key);
                stdMap.erase(key);
            }
        }
        // check for find() method
        auto start_find = std::chrono::high_resolution_clock::now();
        for(int i = 0; i < 256; i++){
            uint8_t key = i;
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
        auto end_find   = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed_find = end_find - start_find;
        // convert to us(microseconds):
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end_find - start_find).count();
        find_time += elapsed_us;
    }
    std::cout << "sequentially find time: " << static_cast<double>(find_time)/1000000 << "s" << std::endl;
    auto end_check = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
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
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
}

// test for performance and compare with std::unordered_map
void operation_benchmark(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int total_iterators = 10000) {
    std::cout << "------------- Performance benchmark -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();

    int num_iterators = total_iterators;
    std::cout << "num_iterators: " << num_iterators << std::endl;
    // myMap.set_fullness(100);
    // randomly insert and delete elements
    auto start_std = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        if(num_iterators % 2 == 1){
            for(int i = start; i < end; i++){
                stdMap.insert({i, i});
            }
        }else{
            for(int i = start; i < end; i++){
                stdMap.erase(i);
            }
        }
    }
    auto end_std = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_std = end_std - start_std;
    std::cout << "stdMap random time: " << elapsed_std.count() << "s" << std::endl;
    // benchmark for myMap
    auto start_my = std::chrono::high_resolution_clock::now();
    num_iterators = total_iterators;
    while(num_iterators-- > 0){
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        if(num_iterators % 2 == 1){
            for(int i = start; i < end; i++){
                myMap.insert(i, i);
            }
        }else{
            for(int i = start; i < end; i++){
                myMap.erase(i);
            }
        }
    }
    auto end_my = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_my = end_my - start_my;
    std::cout << "myMap random time: " << elapsed_my.count() << "s" << std::endl;
    myMap.clear();
    stdMap.clear();
    // sequentially insert and delete elements
    // std::cout << "myMap ablity: " << myMap.map_ability() << std::endl;  
    num_iterators = total_iterators;
    start_std = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        if(num_iterators % 2 == 1){
            for(int i = start; i < end; i++){
                stdMap.insert({i, i});
            }
        }else{
            for(int i = start; i < end; i++){
                stdMap.erase(i);
            }
        }
    }
    end_std = std::chrono::high_resolution_clock::now();
    elapsed_std = end_std - start_std;
    std::cout << "stdMap sequentially time: " << elapsed_std.count() << "s" << std::endl;
    // benchmark for myMap
    num_iterators = total_iterators;
    start_my = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        int start = rand()%myMap.map_ability();
        int end = rand()%myMap.map_ability();
        if(end < start) std::swap(start, end);
        if(num_iterators % 2 == 1){
            for(int i = start; i < end; i++){
                // myMap[i] = i;
                myMap.insert(i, i);
            }
        }else{
            for(int i = start; i < end; i++){
                myMap.erase(i);
            }
        }
    }
    end_my = std::chrono::high_resolution_clock::now();
    elapsed_my = end_my - start_my;
    std::cout << "myMap sequentially time: " << elapsed_my.count() << "s" << std::endl;
    std::cout << "fullness: " << myMap.get_fullness() << std::endl;
}

// searching benchmark with the trio : [] operator, find(), at()
void searching_benchmark(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- Searching benchmark -------------" << std::endl;
    myMap.clear();
    stdMap.clear();
    
    // Pre-populate with substantial dataset
    const int DATASET_SIZE = myMap.map_ability() * 0.8;
    uint8_t test_keys[DATASET_SIZE];
    uint8_t key_count = 0;
    
    // Fill both maps with same data
    for(int i = 0; i < DATASET_SIZE && key_count < DATASET_SIZE; i++) {
        uint8_t key = rand() % 256;
        uint8_t value = rand() % 256;
        if(myMap.insert(key, value).second) {
            stdMap.insert({key, value});
            test_keys[key_count++] = key;
        }
    }
    
    // Add non-existent keys for missed lookups
    uint8_t miss_keys[20];
    uint8_t miss_count = 0;
    for(int i = 0; i < 50 && miss_count < 20; i++) {
        uint8_t key = rand() % 256;
        if(myMap.find(key) == myMap.end()) {
            miss_keys[miss_count++] = key;
        }
    }
    
    unsigned long int my_find_time = 0;
    unsigned long int std_find_time = 0;
    unsigned long int my_at_time = 0;
    unsigned long int std_at_time = 0;
    unsigned long int my_operator_time = 0;
    unsigned long int std_operator_time = 0;
    
    // Prevent compiler optimizations with meaningful accumulation
    uint16_t checksum_std = 0;
    uint16_t checksum_my = 0;
    
    for(int iteration = 0; iteration < num_iterators; iteration++) {
        
        // === FIND() BENCHMARK ===
        auto start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            auto it = stdMap.find(test_keys[i]);
            if(it != stdMap.end()) {
                checksum_std += it->second;  // Simple addition prevents optimization
            }
        }
        for(uint8_t i = 0; i < miss_count; i++) {
            auto it = stdMap.find(miss_keys[i]);
            checksum_std += (it == stdMap.end()) ? 1 : 0;  // Conditional assignment
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        std_find_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            auto it = myMap.find(test_keys[i]);
            if(it != myMap.end()) {
                checksum_my += it->second;  // Simple addition prevents optimization
            }
        }
        for(uint8_t i = 0; i < miss_count; i++) {
            auto it = myMap.find(miss_keys[i]);
            checksum_my += (it == myMap.end()) ? 1 : 0;  // Conditional assignment
        }
        end_time = std::chrono::high_resolution_clock::now();
        my_find_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        // === OPERATOR[] BENCHMARK ===
        start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            checksum_std ^= stdMap[test_keys[i]];  // XOR assignment to use result
        }
        end_time = std::chrono::high_resolution_clock::now();
        std_operator_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            checksum_my ^= myMap[test_keys[i]];  // XOR assignment to use result
        }
        end_time = std::chrono::high_resolution_clock::now();
        my_operator_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        // === AT() BENCHMARK ===
        start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            try {
                checksum_std |= stdMap.at(test_keys[i]);  // OR assignment
            } catch(...) {
                checksum_std++;  // Simple increment on exception
            }
        }
        end_time = std::chrono::high_resolution_clock::now();
        std_at_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        
        start_time = std::chrono::high_resolution_clock::now();
        for(uint8_t i = 0; i < key_count; i++) {
            checksum_my |= myMap.at(test_keys[i]);  // OR assignment
        }
        end_time = std::chrono::high_resolution_clock::now();
        my_at_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    }
    
    // Use checksums to prevent dead code elimination
    if(checksum_std == checksum_my) {
        std::cout << "Checksums match - operations verified" << std::endl;
    } else {
        std::cout << "Checksum difference: " << (int)(checksum_std - checksum_my) << std::endl;
    }
    
    std::cout << " - MyMap:" << std::endl;
    std::cout << "   - [] operator time: " << static_cast<double>(my_operator_time)/1000000 << "s" << std::endl;
    std::cout << "   - find() time: " << static_cast<double>(my_find_time)/1000000 << "s" << std::endl;
    std::cout << "   - at() time: " << static_cast<double>(my_at_time)/1000000 << "s" << std::endl;
    std::cout << " - StdMap:" << std::endl;
    std::cout << "   - [] operator time: " << static_cast<double>(std_operator_time)/1000000 << "s" << std::endl;
    std::cout << "   - find() time: " << static_cast<double>(std_find_time)/1000000 << "s" << std::endl;
    std::cout << "   - at() time: " << static_cast<double>(std_at_time)/1000000 << "s" << std::endl;
    
    std::cout << "fullness: " << myMap.get_fullness() << std::endl;
}

void iterator_benchmark(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap, int num_iterators = 10000) {
    std::cout << "------------- Iterator benchmark -------------" << std::endl;
    myMap.clear();
    stdMap.clear();
    
    // Pre-populate with substantial dataset
    const int DATASET_SIZE = myMap.map_ability();
    for(int i = 0; i < DATASET_SIZE; i++) {
        uint8_t key = rand() % 256;
        uint8_t value = rand() % 256;
        myMap.insert(key, value);
        stdMap.insert({key, value});
    }
    
    unsigned long int my_iter_time = 0;
    unsigned long int std_iter_time = 0;
    
    // Prevent compiler optimizations with meaningful accumulation
    uint16_t checksum_std = 0;
    uint16_t checksum_my = 0;
    
    // === MY ITERATOR BENCHMARK ===
    auto start_time = std::chrono::high_resolution_clock::now();
    for(auto it = myMap.begin(); it != myMap.end(); ++it) {
        checksum_my += it->second;  // Simple addition prevents optimization
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    my_iter_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    // === STD ITERATOR BENCHMARK ===
    start_time = std::chrono::high_resolution_clock::now();
    for(auto it = stdMap.begin(); it != stdMap.end(); ++it) {
        checksum_std += it->second;  // Simple addition prevents optimization
    }
    end_time = std::chrono::high_resolution_clock::now();
    std_iter_time += std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    
    // Use checksums to prevent dead code elimination
    if(checksum_std == checksum_my) {
        std::cout << "Checksums match - iterators verified" << std::endl;
    } else {
        std::cout << "Checksum difference: " << (int)(checksum_std - checksum_my) << std::endl;
    }
    
    std::cout << " - MyMap:" << std::endl;
    std::cout << "   - Iterator time: " << static_cast<double>(my_iter_time)/1000000 << "s" << std::endl;
    std::cout << " - StdMap:" << std::endl;
    std::cout << "   - Iterator time: " << static_cast<double>(std_iter_time)/1000000 << "s" << std::endl;          
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

// compare memory usage between my custom unordered_map and regular std::unordered_map
void memory_usage_comparison(unordered_map<uint8_t, uint8_t>& myMap,std::unordered_map<uint8_t, uint8_t>& /*unused*/) {
    std::cout << "------------- Memory usage compare -------------\n";

    // reset the single global counter
    g_bytes_allocated = 0;

    // make a std::unordered_map that uses our allocator
    using SM = std::unordered_map<
    uint8_t,uint8_t,
    std::hash<uint8_t>,
    std::equal_to<uint8_t>,
    CountingAllocator<std::pair<const uint8_t,uint8_t>>>;
    SM cmap;

    // bump both maps equally
    myMap.clear();
    cmap.clear();
    // myMap.set_fullness(100);
    while(myMap.size() < myMap.map_ability()) {
        uint8_t k = rand() % 256;
        uint8_t v = rand() % 256;
        myMap.insert(k, v);
        cmap.insert({k, v});
    }

    size_t std_bytes = g_bytes_allocated.load();
    size_t my_bytes  = myMap.memory_usage();

    std::cout << "myMap total bytes:  " << my_bytes  << "\n";
    std::cout << "stdMap heap bytes:  " << std_bytes << "\n";
    std::cout << "fullness: " << myMap.get_fullness() << "\n";
    std::cout << "myMap ability: " << myMap.map_ability() << "\n";
}

// getValue() test
void getValue_test(unordered_map<uint8_t, uint8_t>& myMap, std::unordered_map<uint8_t, uint8_t>& stdMap) {
    std::cout << "------------- getValue() test -------------" << std::endl;
    // clear myMap and stdMap
    myMap.clear();
    stdMap.clear();

    int size = 120;
    while(myMap.size() < size) {
        uint8_t key = rand() % 256;
        uint8_t value = rand() % 256;
        myMap.insert(key, value);
        stdMap.insert({key, value});
    }

    // check for getValue() method
    for(auto it = stdMap.begin(); it != stdMap.end(); ++it){
        uint8_t key = it->first;
        int16_t value = it->second;
        int16_t myValue = myMap.getValue(key);
        if(myValue != value){
            std::cout << "getValue failed at key: " << (int)key << std::endl;
        }
    }
}

int main(){
    int num_iterators = 10000;
    unordered_map<uint8_t, uint8_t> myMap;
    std::unordered_map<uint8_t, uint8_t> stdMap;

    unordered_map<int, int> myMap2;
    std::unordered_map<int, int> stdMap2;
    srand(time(0));

    getValue_test(myMap, stdMap);
    random_test(myMap, stdMap, num_iterators);
    random_test_2(myMap2, stdMap2, 10000);
    sequentially_test(myMap, stdMap, num_iterators);
    constructors_test(myMap, stdMap);
    operator_test(myMap, stdMap, num_iterators);
    // operator_test2();
    find_test(myMap, stdMap, num_iterators);
    operation_benchmark(myMap, stdMap, num_iterators);
    searching_benchmark(myMap, stdMap, num_iterators);
    memory_usage_comparison(myMap, stdMap);

    fullness_test({0.7f, 0.8f, 0.9f, 1.0f}, 50000);

    unordered_map<const char*, int> myMap3;

    myMap3.insert("hello", 1);
    myMap3.insert("world", 2);
    myMap3.insert("test", 3);   

    for(auto it = myMap3.begin(); it != myMap3.end(); ++it){
        std::cout << "key: " << it->first << ", value: " << it->second << std::endl;
    }
    return 0;
}

// 