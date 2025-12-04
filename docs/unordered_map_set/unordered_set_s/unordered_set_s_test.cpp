#include <iostream>
#include <unordered_set>
#include "unordered_set_s.h"
#include <cstring>
#include <random>
#include <chrono>
#include <thread>
#include <ctime> 

// for memory usage measurement
#include <memory>
#include <atomic>

// simple global counter for allocator
static std::atomic<size_t> g_bytes_allocated{0};

// counting allocator for std containers
template<typename U>
struct CountingAllocator {
    using value_type = U;
    CountingAllocator() = default;
    template<typename V> constexpr CountingAllocator(const CountingAllocator<V>&) noexcept {}
    U* allocate(size_t n) {
        size_t bytes = n * sizeof(U);
        g_bytes_allocated += bytes;
        return static_cast<U*>(::operator new(bytes));
    }
    void deallocate(U* p, size_t n) noexcept {
        g_bytes_allocated -= n * sizeof(U);
        ::operator delete(p);
    }
};

void print_set(const unordered_set_s<int>& mySet) {
    std::cout << "mySet: ";
    for (const auto& elem : mySet) {
        std::cout << (int)elem << " ";
    }
    std::cout << std::endl;
}

void print_set(const std::unordered_set<int>& stdSet) {
    std::cout << "stdSet: ";
    for (const auto& elem : stdSet) {
        std::cout << (int)elem << " ";
    }
    std::cout << std::endl;
}

bool fake_pass_detecter(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet) {
    // Check if all elements in mySet are in stdSet
    int total_err = 0;
    for (const auto& elem : mySet) {
        if (stdSet.find(elem) == stdSet.end()) {
            // std::cout << "Fake pass detected: " << (int)elem << " not found in stdSet" << std::endl;
            total_err++;
        }
    }
    // Check if all elements in stdSet are in mySet
    for (const auto& elem : stdSet) {
        if (mySet.find(elem) == mySet.end()) {
            // std::cout << "Fake pass detected: " << (int)elem << " not found in mySet" << std::endl;
            total_err++;
        }
    }
    if(total_err > 0) {
        std::cout << "fake pass detected! total errors: " << total_err << std::endl;
        print_set(mySet);
        print_set(stdSet);
        std::cout << "mySet size: " << (int)mySet.size() << std::endl;
        std::cout << "stdSet size: " << stdSet.size() << std::endl;
        return false;
    }
    return true;
}

void random_test(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet, int num_iterators = 10000) {   
    std::cout << "------------- Random test -------------" << std::endl;
    int total_err = 0;
    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;
    mySet.clear();
    stdSet.clear();
    std::cout << "num_iterators: " << num_iterators << std::endl;
    // mySet.set_fullness(90);
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        // mySet.set_fullness(0.9);
        int new_size = rand() % mySet.set_ability();  // 0-254
        if(new_size > mySet.size()){
            while(mySet.size() < new_size){
                int value = rand() % 2000;
                bool std_check = stdSet.insert(value).second;
                bool my_check = mySet.insert(value);
                if(std_check){
                    if(!my_check){
                        // td::cout << "insert failed " << std::endl;
                        insert_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-insert failed " << std::endl;
                        re_insert_error++;
                    }
                }
            }
        }else{
            while(mySet.size() > new_size){
                int value = rand() % 2000;
                bool std_check = stdSet.erase(value);
                bool my_check = mySet.erase(value);
                if(std_check){
                    if(!my_check){
                        // std::cout << "erase failed " << std::endl;
                        erase_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-erase failed " << std::endl;
                        re_insert_error++;
                    }
                }
            }
        }
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "Random test report " << std::endl;
    total_err = insert_error + re_insert_error + erase_error + re_erase_error;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        fake_pass_detecter(mySet, stdSet);
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
void sequential_test(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet, int num_iterators = 1000) {
    std::cout << "------------- Sequential test -------------" << std::endl;
    mySet.clear();
    stdSet.clear();
    int total_err = 0;
    int insert_error = 0;
    int re_insert_error = 0;
    int erase_error = 0;
    int re_erase_error = 0;

    std::cout << "num_iterators: " << num_iterators << std::endl;
    auto start_check = std::chrono::high_resolution_clock::now();
    while(num_iterators-- > 0){
        int start = rand() % mySet.set_ability();
        int end = rand() % mySet.set_ability();
        if(start > end){
            std::swap(start, end);
        }
        for(int i = start; i<end; i++){
            int value = i;
            if(num_iterators % 2 == 0){
                bool std_check = stdSet.insert(value).second;
                bool my_check = mySet.insert(value);
                if(std_check){
                    if(!my_check){
                        // std::cout << "insert failed " << std::endl;
                        insert_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-insert failed " << std::endl;
                        re_insert_error++;
                    }
                }
            }else{
                bool std_check = stdSet.erase(value);
                bool my_check = mySet.erase(value);
                if(std_check){
                    if(!my_check){
                        // std::cout << "erase failed " << std::endl;
                        erase_error++;
                    }
                }else{
                    if(my_check){
                        // std::cout << "re-erase failed " << std::endl;
                        re_erase_error++;
                    }
                }
            }
        }
    }
    auto end_check = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end_check - start_check;
    std::cout << "Sequential test report " << std::endl;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
        fake_pass_detecter(mySet, stdSet);
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

// test for find() method
void find_test(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet, int num_iterators = 10000) {
    std::cout << "------------- Find test -------------" << std::endl;
    mySet.clear(); stdSet.clear();
    int find_error = 0, re_find_error = 0;
    unsigned long find_time = 0;
    auto start_all = std::chrono::high_resolution_clock::now();

    while (num_iterators-- > 0) {
        int old_sz = mySet.size();
        int new_sz = rand() % mySet.set_ability();
        if (new_sz > old_sz) {
            while (mySet.size() < new_sz) {
                uint8_t v = rand() % 256;
                stdSet.insert(v);
                mySet.insert(v);
            }
        } else {
            while (mySet.size() > new_sz) {
                uint8_t v = rand() % 256;
                stdSet.erase(v);
                mySet.erase(v);
            }
        }
        auto t0 = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 256; ++i) {
            bool in_std = stdSet.find(i) != stdSet.end();
            bool in_my  = mySet.find(i)  != mySet.end();
            if (in_std && !in_my) ++find_error;
            if (!in_std && in_my) ++re_find_error;
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        find_time += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    }

    auto end_all = std::chrono::high_resolution_clock::now();
    int total_err = find_error + re_find_error;
    std::cout << "---> " << (total_err ? "failed" : "passed") << std::endl;
    if (total_err) {
        std::cout << "find error: " << find_error << ", re-find error: " << re_find_error << std::endl;
    }
    std::cout << "total errors: " << total_err << std::endl;
    std::cout << "total time: "
              << std::chrono::duration<double>(end_all - start_all).count() << "s"
              << ", find time: " << (find_time/1e6) << "s"
              << std::endl;
}

// performance benchmark
void benchmark_test(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet, int total_iterators = 10000) {
    std::cout << "------------- Performance benchmark -------------" << std::endl;
    mySet.clear(); stdSet.clear();
    int iter = total_iterators;
    auto t0 = std::chrono::high_resolution_clock::now();
    while (iter-- > 0) {
        int s = rand() % mySet.set_ability();
        int e = rand() % mySet.set_ability();
        if (e < s) std::swap(s, e);
        if (iter & 1) {
            for (int i = s; i < e; ++i) stdSet.insert(i);
        } else {
            for (int i = s; i < e; ++i) stdSet.erase(i);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "std::unordered_set elapsed time: "
              << std::chrono::duration<double>(t1 - t0).count() << "s" << std::endl;

    t0 = std::chrono::high_resolution_clock::now();
    iter = total_iterators;
    // mySet.set_fullness(0.8);
    while (iter-- > 0) {
        int s = rand() % mySet.set_ability();
        int e = rand() % mySet.set_ability();
        if (e < s) std::swap(s, e);
        if (iter & 1) {
            for (int i = s; i < e; ++i) mySet.insert(i);
        } else {
            for (int i = s; i < e; ++i) mySet.erase(i);
        }
    }
    t1 = std::chrono::high_resolution_clock::now();
    std::cout << "mySet elapsed time: "
              << std::chrono::duration<double>(t1 - t0).count() << "s"
              << ", fullness: " << mySet.get_fullness()
              << std::endl;
}

void constructor_test(unordered_set_s<int>& mySet, std::unordered_set<int>& stdSet) {
    std::cout << "------------- Constructor test -------------" << std::endl;
    int total_err = 0;
    unordered_set_s<int> mySet1 = mySet;
    // copy constructor
    unordered_set_s<int> mySet2(mySet);
    if(!fake_pass_detecter(mySet2, stdSet)){
        std::cout << "copy constructor failed" << std::endl;
        total_err++;
    }
    // std::cout << "here 0" << std::endl;
    // move constructor
    unordered_set_s<int> mySet3(std::move(mySet));
    if(!fake_pass_detecter(mySet3, stdSet)){
        std::cout << "move constructor failed" << std::endl;
        total_err++;
    }
    mySet = mySet1;
    // std::cout << "here 1" << std::endl;
    // copy assignment
    unordered_set_s<int> mySet4;
    mySet4 = mySet;
    if(!fake_pass_detecter(mySet4, stdSet)){
        std::cout << "copy assignment failed" << std::endl;
        total_err++;
    }
    // std::cout << "here 2" << std::endl;
    // move assignment
    unordered_set_s<int> mySet5;
    mySet5 = std::move(mySet);
    if(!fake_pass_detecter(mySet5, stdSet)){
        std::cout << "move assignment failed" << std::endl;
        total_err++;
    }
    // std::cout << "here 3" << std::endl;
    mySet = mySet1;
    if(total_err == 0){
        std::cout << "---> passed" << std::endl;
    }else{
        std::cout << "---> failed" << std::endl;
        std::cout << "total errors: " << total_err << std::endl;
    }
}

// memory usage comparison
void memory_usage_comparison(unordered_set_s<int>& mySet, std::unordered_set<int>& /*unused*/) {
    std::cout << "------------- Memory usage compare -------------" << std::endl;
    g_bytes_allocated = 0;
    using SU = std::unordered_set<
        uint8_t,
        std::hash<uint8_t>,
        std::equal_to<uint8_t>,
        CountingAllocator<uint8_t>
    >;
    SU cmap;
    mySet.clear(); cmap.clear();
    if(!mySet.set_fullness(0.92)) {
        std::cout << "failed to set fullness" << std::endl;
        return ;
    }
    std::cout << "fullness: " << mySet.get_fullness() << std::endl;
    // return;
    int count = 0;
    std::cout <<" mySet ability: " << mySet.set_ability() << std::endl;
    while (mySet.size() < mySet.set_ability()) {
        // std::cout << "size: " << mySet.size() << std::endl;
        uint8_t v = rand() % 256;
        mySet.insert(v);
        cmap.insert(v);
        if(count++ > 255) break;
    }
    size_t std_bytes = g_bytes_allocated.load();
    size_t my_bytes  = mySet.memory_usage();
    std::cout << "mySet total bytes:  " << my_bytes  << "\n"
              << "stdSet heap bytes:  " << std_bytes << "\n"
              << "fullness: " << mySet.get_fullness()
              << ", ability: " << mySet.set_ability()
              << std::endl;
}

int main(){
    srand(time(0));
    int num_iterators = 10000;
    unordered_set_s<int> mySet;
    std::unordered_set<int> stdSet;
    mySet.set_fullness(90);

    random_test(mySet, stdSet, num_iterators);
    constructor_test(mySet, stdSet);
    sequential_test(mySet, stdSet, num_iterators);
    constructor_test(mySet, stdSet);
    find_test(mySet, stdSet, num_iterators);
    benchmark_test(mySet, stdSet, num_iterators);
    memory_usage_comparison(mySet, stdSet);

    return 0;
}