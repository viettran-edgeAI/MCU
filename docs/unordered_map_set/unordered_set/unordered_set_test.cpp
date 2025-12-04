#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include "unordered_set.h"
#include <cstring>
#include <random>
#include <chrono>
#include <thread>
#include <ctime> 
#include <iomanip> // For std::setprecision
// for memory usage measurement
#include <memory>
#include <atomic>



template<typename T>
void print_map(unordered_set<T>& mySet){
    std::cout << "------------- Print map -------------" << std::endl;
    std::cout << " Chain capacity: " << mySet.chainCap() << std::endl;
    for(int i=0; i< mySet.chainCap(); i++){
        std::cout << "setID: " << i;
        if(mySet.set_in_use(i)){
            std::cout << " - "<< mySet.chain[i]->size() << std::endl;
        }else{
            std::cout << " -> empty[]" << std::endl;
        }
    }
    std::cout << "-------------------------------------" << std::endl;
}

template<typename T>
bool fake_pass_detecter(unordered_set<T>& mySet, std::unordered_set<T>& stdSet) {
    if(mySet.size() != stdSet.size()) {
        std::cout << "Fake pass detected !(size different)" << std::endl;
        std::cout << "mySet size: " << mySet.size() << std::endl;
        std::cout << "stdSet size: " << stdSet.size() << std::endl;
        return false;
    }
    // std::cout << "here 2" << std::endl;
    mySet.fit();
    
    for (auto it = stdSet.begin(); it != stdSet.end(); ++it) {
        T key = *it;
        if (mySet.find(key) == mySet.end()) {
            std::cout << "Fake pass detected !(key not found)" << std::endl;
            std::cout << "failed at std key: " << key << std::endl;
            return false;
        }
    }
    // std::cout << "here 3" << std::endl;
    for (auto it = mySet.begin(); it != mySet.end(); ++it) {
        T key = *it;
        if (stdSet.find(key) == stdSet.end()) {
            std::cout << "Fake pass detected !(ghost key)" << std::endl;
            std::cout << "failed at mySet key: " << key << std::endl;
            return false;
        }
    }
    // std::cout << "here 4" << std::endl;
    return true;
}

void iterator_test(unordered_set<int>& mySet, std::unordered_set<int>& stdSet, int num_iterators = 10) {  
    std::cout << "------------- Iterator test -------------" << std::endl;
    std::cout << "num_iterators: " << num_iterators << std::endl;
    mySet.clear();
    stdSet.clear();
    // mySet.set_fullness(0.9f);
    auto start = std::chrono::high_resolution_clock::now();
    int total_err = 0;
    while(num_iterators-- > 0) {
        int old_size = stdSet.size();
        int new_size = static_cast<int>(rand()%mySet.set_ability());
        if(new_size > old_size){
            while(stdSet.size() < new_size){
                int key = rand() % mySet.set_ability();
                mySet.insert(key);
                stdSet.insert(key);
            }
        }else{
            while(stdSet.size() > new_size){
                int key = rand() % mySet.set_ability();
                mySet.erase(key);
                stdSet.erase(key);
            }
        }
        for(auto it = mySet.begin(); it != mySet.end(); ++it) {
            int key = *it;
            if(stdSet.find(key) == stdSet.end()) {
                // std::cout << "Iterator test failed !"<< std::endl;
                total_err++;
                return;
            }
        }
    }
    mySet.clear();
    stdSet.clear();
    // mySet.set_fullness(0.9f);
    // compare iterators speed 
    while(mySet.size() < 50000){
        int key = rand() % mySet.set_ability();
        mySet.insert(key);
        stdSet.insert(key);
    }
    auto start_iter = std::chrono::high_resolution_clock::now();
    for(auto it = mySet.begin(); it != mySet.end(); ++it) {
        int key = *it;
        key++;
    }
    auto end_iter = std::chrono::high_resolution_clock::now();  
    auto my_duration_iter = std::chrono::duration_cast<std::chrono::microseconds>(end_iter - start_iter);
    auto start_std = std::chrono::high_resolution_clock::now();
    for(auto it = stdSet.begin(); it != stdSet.end(); ++it) {
        int key = *it;
        key++;
    }
    auto end_std = std::chrono::high_resolution_clock::now();
    auto std_duration_iter = std::chrono::duration_cast<std::chrono::microseconds>(end_std - start_std);
    if(total_err == 0){
        std::cout << "---> Passed!" << std::endl;
    }else{
        std::cout << "---> Failed !" << std::endl;
    }
    std::cout << "mySet iterator time: " << my_duration_iter.count() << " ms" << std::endl;
    std::cout << "stdSet iterator time: " << std_duration_iter.count() << " ms" << std::endl;
    std::cout << "Total errors: " << total_err << std::endl;
}

void random_test(unordered_set<int>& mySet, std::unordered_set<int>& stdSet, int num_iterrators = 40) {
    std::cout << "------------- Random test -------------" << std::endl;
    mySet.clear();
    stdSet.clear();

    int total_err = 0;
    int insert_err = 0;
    int erase_err = 0;
    int re_insert_err = 0;
    int re_erase_err = 0;

    if(!mySet.set_fullness(0.9f).first){
        std::cout << "Failed to set fullness!" << std::endl;
    }
    std::cout << "map ability: " << mySet.set_ability() << std::endl;
    // mySet.reserve(mySet.set_ability());
    auto start = std::chrono::high_resolution_clock::now();
    while(num_iterrators-- > 0) {
        mySet.fit();
        // std::cout <<"---> Iteration: " << num_iterrators << std::endl;
        int old_size = stdSet.size();
        int new_size = static_cast<int>(rand()% mySet.set_ability());
        // std::cout << "new size: " << new_size << std::endl;
        if(new_size > old_size){
            while(stdSet.size() < new_size){
                uint16_t key = rand() % mySet.set_ability();
                bool my_check = mySet.insert(key);
                bool std_check = stdSet.insert(key).second;
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
                // printMap(mySet);
            }
        }else{
            while(stdSet.size() > new_size){
                uint16_t key = rand() % mySet.set_ability();
                bool my_check = mySet.erase(key);
                bool std_check = stdSet.erase(key);
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
        // std::cout << "mySet size: " << mySet.size() << std::endl;
        // std::cout << "stdSet size: " << stdSet.size() << std::endl;
        // std::cout << "total errors: " << insert_err + erase_err + re_insert_err + re_erase_err << std::endl;
    }
    total_err = insert_err + erase_err + re_insert_err + re_erase_err;
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Random test report: " << std::endl;   
    if(total_err == 0){
        std::cout << "---> Passed!" << std::endl;
        if(!mySet.set_fullness(95).first){
            std::cout << "Failed to set fullness!" << std::endl;
        }
        fake_pass_detecter(mySet, stdSet);
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
    // printMap(mySet);
}

void sequential_test(unordered_set<int>& mySet, std::unordered_set<int>& stdSet, int num_iterators = 10) {
    std::cout << "------------- Sequentially test -------------" << std::endl;
    // clear mySet and stdSet
    mySet.clear();
    mySet.fit();
    stdSet.clear();

    std::unordered_set<int> err_bag;
    std::unordered_set<int> range_err_bag;

    std::cout << "num_iterators: " << num_iterators << std::endl;
    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    int total_err = 0;

    // alternately insert and erase a range of values ​​sequentially
    if(!mySet.set_fullness(0.8).first){
        std::cout << "Failed to set fullness!" << std::endl;
    }
    std::cout << "set ability: " << mySet.set_ability() << std::endl;
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        mySet.fit();
        // std::cout << "---> num_iterators: " << num_iterators << std::endl;  

        int start = rand()%mySet.set_ability();
        int end = rand()%mySet.set_ability();
        if(end < start) std::swap(start, end);
        // std::cout << "start: " << start << " end: " << end << std::endl;
        for(int i = start; i < end; i++){
            int key = i;
            if(num_iterators % 2 == 1){
                bool std_check = stdSet.insert(key).second;
                bool my_check = mySet.insert(key);
                if(std_check){
                    if(!my_check){
                        // std::cout << "insert failed auint16_t key: " << (int)key << std::endl;
                        insert_error++;
                        err_bag.insert(key);
                        uint8_t range = mySet.keyMappingIN(key).second;
                        range_err_bag.insert(range);
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-insert failed auint16_t key: " << (int)key << std::endl;
                        re_insert_error++;
                        err_bag.insert(key);
                        uint8_t range = mySet.keyMappingIN(key).second;
                        range_err_bag.insert(range);
                    }
                }
            }else{
                bool my_check = mySet.erase(key);
                bool std_check = stdSet.erase(key);
                if(std_check){
                    if(!my_check){
                        // std::cout << "erase failed auint16_t key: " << (int)key << std::endl;
                        erase_error++;
                        err_bag.insert(key);
                        uint8_t range = mySet.keyMappingIN(key).second;
                        range_err_bag.insert(range);
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-erase failed auint16_t key: " << (int)key << std::endl;
                        re_erase_error++;
                        err_bag.insert(key);
                        uint8_t range = mySet.keyMappingIN(key).second;
                        range_err_bag.insert(range);
                    }
                }
            }
        }
    //     std::cout << "mySet size: " << mySet.size() << std::endl;
    //     std::cout << "stdSet size: " << stdSet.size() << std::endl;
    //     std::cout << "total errors: " << insert_error + re_insert_error + erase_error + re_erase_error << std::endl;
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "Sequentially test report " << std::endl;
    total_err = insert_error + re_insert_error + erase_error + re_erase_error;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        // iterator_test(mySet, stdSet);
        fake_pass_detecter(mySet, stdSet);
    }else{
        std::cout << "---> failed" << std::endl;
        std::cout << "insert error: " << insert_error << std::endl;
        std::cout << "re_insert error: " << re_insert_error << std::endl;
        std::cout << "erase error: " << erase_error << std::endl;
        std::cout << "re_erase error: " << re_erase_error << std::endl;

        // std::cout << "err bag: ";
        // for(auto it = err_bag.begin(); it != err_bag.end(); ++it) {
        //     T key = *it;
        //     std::cout << key << " ";
        // }
        // std::cout << std::endl;
        // std::cout << "range err bag: ";
        // for(auto it = range_err_bag.begin(); it != range_err_bag.end(); ++it) {
        //     uint8_t key = *it;
        //     std::cout << (int)key << " ";
        // }
        // std::cout << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::cout << "total time: " << elapsed_seconds.count() << "s" << std::endl;
    //clear mySet and stdSet
}



void constructors_test(unordered_set<int>& mySet, std::unordered_set<int>& stdSet) {
    std::cout << "------------- Constructors test -------------" << std::endl;
    int total_err = 0;
    unordered_set<int> mySet1 = mySet;  // backup mySet
    // copy constructor
    unordered_set<int> mySet2(mySet);
    if(!fake_pass_detecter(mySet2,stdSet)){
        std::cout << "copy constructor failed!" << std::endl;
        total_err++;
    }

    // move constructor
    unordered_set<int> mySet3(std::move(mySet));
    if(!fake_pass_detecter(mySet3,stdSet)){
        std::cout << "move constructor failed!" << std::endl;
        total_err++;
    }
    mySet = mySet1; // restore mySet

    // copy assignment
    unordered_set<int> mySet4;
    mySet4 = mySet;
    if(!fake_pass_detecter(mySet4,stdSet)){
        std::cout << "copy assignment failed!" << std::endl;
        total_err++;
    }

    // move assignment
    unordered_set<int> mySet5;
    mySet5 = std::move(mySet);
    if(!fake_pass_detecter(mySet5,stdSet)){
        std::cout << "move assignment failed!" << std::endl;
        total_err++;
    }

    mySet = mySet1; // restore mySet
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
    }else{
        std::cout << "---> failed" << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
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
        unordered_set<uint16_t> mySet;
        mySet.set_fullness(level);
        
        FullnessResult result;
        result.fullness = level;
        result.memory_bytes = 0;
        
        // Store keys for lookup operations
        uint16_t keys[65535] = {0};
        uint16_t key_count = 0;
        
        // ----- INSERT PERFORMANCE -----
        auto start = std::chrono::high_resolution_clock::now();
        int count = num_iterators;
        key_count = 0;
        
        while (count-- > 0) {
            if (count % 10 == 0) mySet.clear(); // Periodically reset
            
            int start_index = rand() % (mySet.set_ability());
            int end_index = start_index + rand() % (mySet.set_ability());
            
            for (uint16_t i = start_index; i < end_index && i < 65535; i++) {
                if (mySet.insert(i) && key_count < 65535) {
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
                key = rand() % mySet.set_ability(); // Possibly non-existenuint16_t key
            }
            
            auto it = mySet.find(key);
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
                    mySet.insert(keys[i]);
                }
            }
            
            // Erase some keys
            int erases = rand() % key_count;
            for (int i = 0; i < erases; i++) {
                uint16_t key = keys[rand() % key_count];
                mySet.erase(key);
            }
        }
        end = std::chrono::high_resolution_clock::now();
        result.erase_time = std::chrono::duration<double>(end - start).count();
        std::cout << "Erase time: " << result.erase_time << "s" << std::endl;
        
        // Measure memory usage
        mySet.clear();
        int target_size = mySet.set_ability() * 0.8; // Fill to 80% of capacity
        while (mySet.size() < target_size) {
            uint16_t key = rand() % mySet.set_ability();
            uint16_t value = rand() % 256;
            mySet.insert(key);
        }
        result.memory_bytes = mySet.memory_usage();
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
    unordered_set<uint16_t> mySet;
    std::unordered_set<uint16_t> stdSet;
    mySet.set_fullness(0.8);
    mySet.clear();
    mySet.reserve(10000);
    // make some gaps between maps in chain
    for(int i = 0; i < 10000; i++){
        int value = rand() % 10000;
        mySet.insert(i);
        stdSet.insert(i);
    }
    for(int i = 3000; i < 7000; i++){
        mySet.erase(i);
        stdSet.erase(i);
    }
    mySet.fit();
    mySet.set_fullness(90);
    if(fake_pass_detecter(mySet, stdSet)){
        std::cout << "Fullness test 2 passed!" << std::endl;
    }else{
        std::cout << "Fullness test 2 failed!" << std::endl;
    }
}

int main(){
    srand(time(0));
    unordered_set<int> mySet;
    std::unordered_set<int> stdSet;
    int num_iterrators = 100;
    auto start = std::chrono::high_resolution_clock::now();

    // Test random insertions
    random_test(mySet, stdSet, num_iterrators);
    constructors_test(mySet, stdSet);
    iterator_test(mySet, stdSet, num_iterrators);
    sequential_test(mySet, stdSet, num_iterrators);
    iterator_test(mySet, stdSet, num_iterrators);
    // find_test(mySet, stdSet, num_iterrators);

    // first_benchmark(mySet, stdSet);
    // searching_benchmark(mySet, stdSet, num_iterrators);

    fullness_test();
    fullness_test2();


    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "=====> Total testing & benchmark time: " << elapsed_seconds.count() << "s" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    return 0;
}