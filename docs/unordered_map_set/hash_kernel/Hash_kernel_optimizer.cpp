#include <cstdint>
#include <iostream>
#include <unordered_set_s>
#include <unordered_map_s>
#include <vector>
#include <random>
#include <ctime>
#include <chrono>
#include <thread>

// #include "HashKernel_optimazer.h"

using namespace std;

int djb2Hash(uint8_t key,int hash) {
    return hash*33 + key;
}
uint8_t hashFunction(uint16_t TABLE_SIZE, size_t key, int hash) {
    // return (uint8_t)(djb2Hash(key, hash) % TABLE_SIZE);  // use normal hash func
    return (hash + key) % TABLE_SIZE;              // minimal 
    // return (key * 157) % TABLE_SIZE;         // use golden ration 
}
uint8_t linearShifting(uint16_t TABLE_SIZE,uint8_t index, uint8_t step) {
    return (index + step) % TABLE_SIZE;
}

// tim uoc chung lon nhat
uint8_t gcd(uint16_t a, uint8_t b) {
    while (b != 0) {
        uint8_t temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

uint8_t calStep(uint16_t a) {
    if(a<=10) return 1;
    if(a>10 && a<=20) {
      if(a==14 || a==18) return 5;
      return a/2 + a%2 -1;
    }
    uint8_t b = a /10 - 1;   
    while (b % 10 == 0 || gcd(a, b) > 1) {
        b = b - 1;
    }
    return b;
}
void print_bag(unordered_set_s<uint8_t> bag){
    cout << "bag: ";
    for(auto it = bag.begin(); it != bag.end(); ++it){
        cout << (int)*it << " ";
    }
    cout << endl;
}

void print_hashers(vector<uint16_t> hashers){
    cout << "hashers: ";
    for(auto it = hashers.begin(); it != hashers.end(); ++it){
        cout << (int)*it << ",";
    }
    cout << endl;
}

// find hashers with minimum collisions and erase that hashers
vector<uint16_t> get_best_hashers(unordered_map_s<int,vector<uint16_t>> hashers_container){
    vector<uint16_t> best_hashers;
    int min_collision = 99999;
    for(auto it = hashers_container.begin(); it != hashers_container.end(); ++it){
        if(it->first < min_collision){
            min_collision = it->first;
            best_hashers = it->second;
        }
    }
    // erase best_hashers from hashers_container
    for(auto it = hashers_container.begin(); it != hashers_container.end(); ++it){
        if(it->first == min_collision){
            hashers_container.erase(it);
            break;
        }
    }
    return best_hashers;
}

// takeout best_hashers from hashers_container and testing it. if it failed, take the next best_hashers
// if it pass, return best_hashers
// if all hashers failed, return empty vector
vector<uint16_t> final_testing(unordered_map_s<int,vector<uint16_t>> hashers_container, int iterators_check = 1000){
    cout << "Final checking for best_hashers" << std::flush;
    auto start_check = chrono::high_resolution_clock::now();

    while(hashers_container.size() > 0){
        vector<uint16_t> best_hashers = get_best_hashers(hashers_container);
        int total_errors = 0;
        for(int i= 0; i< iterators_check; i++){
            for(uint16_t TABLE_SIZE = 1; TABLE_SIZE <= 255; TABLE_SIZE++){
                int check[TABLE_SIZE];
                for (int i = 0; i < TABLE_SIZE; i++) {
                    check[i] = -1;
                }
                unordered_set_s<size_t> bag;
                int hasher = best_hashers[TABLE_SIZE-1];
                size_t step = calStep(TABLE_SIZE);

                while(true){
                    if(bag.size() == TABLE_SIZE) break;
                    size_t value = rand()%2000000000;
                    if(bag.insert(value).second){
                        int attempt = 0;
                        size_t index = hashFunction(TABLE_SIZE, value, hasher);
                        while(check[index] != -1){
                            index = linearShifting(TABLE_SIZE, index, step);
                            if(attempt++ >= TABLE_SIZE){
                                total_errors++;
                                break;
                            }
                        }
                        check[index] = value;
                    }
                }
                for(int i=0; i<TABLE_SIZE; i++){
                    if(bag.find(check[i]) == bag.end()){
                        cout << "Fake pass detected" << endl;
                        total_errors++;
                        break;
                    }
                }
            }
            auto now = chrono::high_resolution_clock::now();
            if(now - start_check > chrono::milliseconds(500)){
                cout << "." << std::flush;
                start_check = now;
            }
        }
        cout << endl;
        if(total_errors > 0){
            cout << "-------------- FAILED ! ----------------" << endl;
            cout << "Total errors: " << total_errors << endl;
            cout << "Number of testing_iterators: " << iterators_check << endl;
            cout << "Switch to next best_hashers" << endl;
        }else{
            cout << "<--------------- PASS ! ---------------------->" << endl;
            cout << "- Number of testing_iterators: " << iterators_check << endl;
            return best_hashers;
        }
    }
    return vector<uint16_t>();
    cout << "All hashers failed!" << endl;
}               



int main(){
    int num_iterators = 4; 
    unordered_map_s<int,vector<uint16_t>> hashers_container;
    vector<uint16_t> best_hashers;
    int min_collisions = 99999;
    int max_collisions = 0;
    int hashing_time = 0;
    int average_time = 0;   

    // find the TABLE_SIZE with the largest total collisions / TABLE_SIZE ratio
    unordered_map_s<int, float> collision_density;
    for(int i=0; i< 255; i++){  // TABLE_SIZE 1-255
        collision_density[i] = 0.0;
    }
    // number of calculations at each fill levels of elements in TABLE_SIZE (10%, 20%...)
    long long int fill_levels[10] = {0};
    
    auto start_algorimth = chrono::high_resolution_clock::now();
    int loop_count = 0;
    while(loop_count++ < num_iterators){
        vector<uint16_t> hashers;
        int total_collisions = 0;
        int loop = 0;
        srand(time(0));
        auto start = chrono::high_resolution_clock::now();
        for(uint16_t TABLE_SIZE = 1; TABLE_SIZE <= 255; TABLE_SIZE++){
            int fill_units = TABLE_SIZE / 10;
            uint8_t step = calStep(TABLE_SIZE);
            int min_collision = 99999;
            int best_hasher = -1;
            int check[TABLE_SIZE];
            unordered_set_s<uint8_t> bag;
            for(int hash = 1; hash <= 255; hash++){
                int current_size = 0;
                // reset check and bag
                for(int i=0; i<TABLE_SIZE; i++){
                    check[i] = -1;
                }
                bag.clear();
                bool hash_complete = true;
                int total_collision = 1;
                while(true){
                    if(bag.size() == TABLE_SIZE) break;
                    uint8_t value = rand()%256;     // 0-255
                    if(bag.insert(value).second){
                        current_size++;
                        int attempt = 0;
                        uint8_t index = hashFunction(TABLE_SIZE, value, hash);
                        while(check[index] != -1){
                            total_collision++;
                            index = linearShifting(TABLE_SIZE, index, step);
                            if(attempt++ > TABLE_SIZE){
                                hash_complete = false;
                                // cout << " failed";
                                break;
                            }
                        }
                        check[index] = value;
                    }// else cout << "duplicate value: " << (int)value << endl;
                    if(fill_units >=2){
                        int level = current_size / fill_units;
                        if(level < 10){
                            fill_levels[level]+=total_collision;
                        }
                    }
                }
                // print_bag(bag);
                // cout << "hash: " << hash << " collision: " << total_collision << endl;
                if(total_collision > 0){
                    collision_density[TABLE_SIZE-1] += total_collision;
                }
                if(total_collision < min_collision && hash_complete){
                    // re_check for sure all values in bag are in check[] too
                    bool re_check = true;
                    for(int i=0; i<TABLE_SIZE; i++){
                        if(bag.find(check[i]) == bag.end()){
                            cout << "Fake pass detected" << endl;
                            re_check = false;
                            break;
                        }
                    }
                    if(re_check){
                        best_hasher  = hash;
                        min_collision =  total_collision;
                    }
                }
            }
            if(best_hasher == -1){
                cout << "All hasher failed at TABLE_SIZE " << TABLE_SIZE << endl;
            }else{
                hashers.push_back(best_hasher);
                // cout << "TABLE_SIZE: " << TABLE_SIZE << " hash: " << best_hasher << " collision: " << min_collision << endl;
                // cout << best_hasher << "," << std::flush;
                total_collisions += min_collision;
            }
        }
        hashers_container.insert(make_pair(total_collisions, hashers));
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
        cout << "\nTotal time : " << duration.count() << " milliseconds" << endl << std::flush;
        cout << "Total collisions: " << total_collisions << endl << std::flush;
        cout << "-------------- loop " << loop_count << " ----------------------" << endl << std::flush;
        if(total_collisions < min_collisions){
            min_collisions = total_collisions;
            best_hashers = hashers;
            hashing_time = duration.count();
        }
        if(total_collisions > max_collisions){
            max_collisions = total_collisions;
        }
        average_time += duration.count();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    cout << "------------------- RESULT -------------------" << endl;
    cout << "==> Best hashers: " << endl;
    print_hashers(best_hashers);
    cout << "Min_collisions: " << min_collisions << endl;
    cout << "Max_collisions: " << max_collisions << endl;
    cout << "Hashing time: " << hashing_time << endl;
    cout << "Average time: " << average_time/num_iterators << endl;
    auto end_algorimth = chrono::high_resolution_clock::now();
    auto duration_total = chrono::duration_cast<chrono::seconds>(end_algorimth - start_algorimth);    
    cout << "Total time : " << duration_total.count() << " seconds" << endl;
    // cout << "------------------- DONE !-------------------" << endl;

    // re_check best_hashers by re_insert multiple repetitions
    vector<uint16_t> final_hashers = final_testing(hashers_container, 2000);
    cout << "-> Final hashers: " << endl;
    print_hashers(final_hashers);

    end_algorimth = chrono::high_resolution_clock::now();
    duration_total = chrono::duration_cast<chrono::seconds>(end_algorimth - start_algorimth);    

    cout << "-> Total time : " << duration_total.count() << " seconds" << endl;

    // printout density of each TABLE_SIZE from 21->255
    // look for TABLE_SIZEs with anomalous collision density and ignore it in hash_kernel
    cout << "-------------- COLLISION_DENSITY REPORT-------------------" << endl;
    for(int i=1; i<255; i = i+2){
        cout << "TABLE_SIZE: " << i+1 << " - " << (collision_density[i] / (num_iterators * 255) / (i+1) * 100) << " %" << endl;
    }
    cout << "---------------- CACULATIONS EACH LEVEL ----------------------" << endl;
    cout << "- " << "10% :" << (fill_levels[0] * 100.0 / fill_levels[9]) << " %" << endl;
    for (int i = 1; i < 10; i++) {
        double percent = (fill_levels[i] - fill_levels[i - 1]) * 100.0 / fill_levels[9];
        cout << "- " << (i + 1) * 10 << "% :" << percent << " %" << endl;
    }
    return 0;
}