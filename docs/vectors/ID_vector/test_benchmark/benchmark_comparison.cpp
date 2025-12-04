#include <iostream>
#include <unordered_set_s>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <memory>
#include <algorithm>
#include <fstream>
#include "../ID_vector.cpp"

using namespace mcu;
using namespace std::chrono;

class BenchmarkSuite {
private:
    struct BenchmarkResult {
        std::string test_name;
        double id_vector_time_ns;
        double unordered_set_s_time_ns;
        double vector_time_ns;
        size_t id_vector_memory_bytes;
        size_t unordered_set_s_memory_bytes;
        size_t vector_memory_bytes;
        double speedup_vs_unordered_set_s;
        double speedup_vs_vector;
        double memory_ratio_vs_unordered_set_s;
        double memory_ratio_vs_vector;
    };
    
    std::vector<BenchmarkResult> results;
    
    // Estimate memory usage for std::vector
    template<typename T>
    size_t estimate_vector_memory(const std::vector<T>& vec) {
        return vec.capacity() * sizeof(T) + sizeof(std::vector<T>);
    }
    
    // Estimate memory usage for unordered_set_s
    template<typename T>
    size_t estimate_unordered_set_s_memory(const std::unordered_set<T>& set) {
        // Conservative estimate: 
        // - Each element: sizeof(T) + pointer overhead
        // - Hash table overhead: load factor ~0.75, so extra buckets
        // - Node allocation overhead
        size_t element_size = sizeof(T) + sizeof(void*) * 2; // value + 2 pointers (next, hash)
        size_t estimated_buckets = set.size() / 0.75; // account for load factor
        size_t bucket_overhead = estimated_buckets * sizeof(void*);
        return set.size() * element_size + bucket_overhead + sizeof(std::unordered_set<T>);
    }
    
    // Estimate memory usage for ID_vector
    template<typename T, uint8_t BitsPerValue>
    size_t estimate_id_vector_memory(const ID_vector<T, BitsPerValue>& vec) {
        T range = vec.get_maxID() - vec.get_minID() + 1;
        size_t total_bits = range * BitsPerValue;
        size_t data_bytes = (total_bits + 7) / 8;
        return data_bytes + sizeof(ID_vector<T, BitsPerValue>);
    }
    
public:
    template<typename T, uint8_t BitsPerValue>
    void benchmark_insertion(const std::string& test_name, T max_id, const std::vector<T>& test_data) {
        std::cout << "\n=== " << test_name << " ===\n";
        
        BenchmarkResult result;
        result.test_name = test_name;
        
        // Benchmark ID_vector insertion
        auto start = high_resolution_clock::now();
        
        ID_vector<T, BitsPerValue> id_vec(0, max_id);
        for (auto id : test_data) {
            if (id <= max_id) {
                id_vec.push_back(id);
            }
        }
        
        auto end = high_resolution_clock::now();
        result.id_vector_time_ns = duration_cast<nanoseconds>(end - start).count();
        result.id_vector_memory_bytes = estimate_id_vector_memory(id_vec);
        
        std::cout << "ID_vector<" << typeid(T).name() << ", " << (int)BitsPerValue << "> inserted " << id_vec.size() 
                  << " elements in " << result.id_vector_time_ns << " ns\n";
        std::cout << "ID_vector memory usage: " << result.id_vector_memory_bytes << " bytes\n";
        
        // Benchmark unordered_set_s insertion
        start = high_resolution_clock::now();
        
        std::unordered_set<T> uset;
        for (auto id : test_data) {
            if (id <= max_id) {
                uset.insert(id);
            }
        }
        
        end = high_resolution_clock::now();
        result.unordered_set_s_time_ns = duration_cast<nanoseconds>(end - start).count();
        result.unordered_set_s_memory_bytes = estimate_unordered_set_s_memory(uset);
        
        std::cout << "unordered_set_s inserted " << uset.size() 
                  << " elements in " << result.unordered_set_s_time_ns << " ns\n";
        std::cout << "unordered_set_s memory usage: " << result.unordered_set_s_memory_bytes << " bytes\n";
        
        // Benchmark std::vector insertion (sorted)
        start = high_resolution_clock::now();
        
        std::vector<T> vec;
        for (auto id : test_data) {
            if (id <= max_id) {
                auto it = std::lower_bound(vec.begin(), vec.end(), id);
                if (it == vec.end() || *it != id) {
                    vec.insert(it, id);
                }
            }
        }
        
        end = high_resolution_clock::now();
        result.vector_time_ns = duration_cast<nanoseconds>(end - start).count();
        result.vector_memory_bytes = estimate_vector_memory(vec);
        
        std::cout << "std::vector inserted " << vec.size() 
                  << " elements in " << result.vector_time_ns << " ns\n";
        std::cout << "std::vector memory usage: " << result.vector_memory_bytes << " bytes\n";
        
        // Calculate ratios
        result.speedup_vs_unordered_set_s = result.unordered_set_s_time_ns / std::max(1.0, result.id_vector_time_ns);
        result.speedup_vs_vector = result.vector_time_ns / std::max(1.0, result.id_vector_time_ns);
        result.memory_ratio_vs_unordered_set_s = (double)result.id_vector_memory_bytes / result.unordered_set_s_memory_bytes;
        result.memory_ratio_vs_vector = (double)result.id_vector_memory_bytes / result.vector_memory_bytes;
        
        std::cout << "Speedup vs unordered_set_s: " << std::fixed << std::setprecision(2) << result.speedup_vs_unordered_set_s << "x\n";
        std::cout << "Speedup vs vector: " << std::setprecision(2) << result.speedup_vs_vector << "x\n";
        std::cout << "Memory ratio vs unordered_set_s: " << std::setprecision(3) << result.memory_ratio_vs_unordered_set_s << "\n";
        std::cout << "Memory ratio vs vector: " << std::setprecision(3) << result.memory_ratio_vs_vector << "\n";
        
        results.push_back(result);
    }
    
    template<typename T, uint8_t BitsPerValue>
    void benchmark_lookup(const std::string& test_name, T max_id, 
                         const std::vector<T>& insert_data, 
                         const std::vector<T>& lookup_data) {
        std::cout << "\n=== " << test_name << " ===\n";
        
        // Prepare data structures
        ID_vector<T, BitsPerValue> id_vec(0, max_id);
        std::unordered_set<T> uset;
        std::vector<T> vec;
        
        for (auto id : insert_data) {
            if (id <= max_id) {
                id_vec.push_back(id);
                uset.insert(id);
                auto it = std::lower_bound(vec.begin(), vec.end(), id);
                if (it == vec.end() || *it != id) {
                    vec.insert(it, id);
                }
            }
        }
        
        BenchmarkResult result;
        result.test_name = test_name;
        result.id_vector_memory_bytes = estimate_id_vector_memory(id_vec);
        result.unordered_set_s_memory_bytes = estimate_unordered_set_s_memory(uset);
        result.vector_memory_bytes = estimate_vector_memory(vec);
        
        // Benchmark ID_vector lookup
        auto start = high_resolution_clock::now();
        
        size_t found_count_id_vec = 0;
        for (auto id : lookup_data) {
            if (id_vec.contains(id)) {
                found_count_id_vec++;
            }
        }
        
        auto end = high_resolution_clock::now();
        result.id_vector_time_ns = duration_cast<nanoseconds>(end - start).count();
        
        // Benchmark unordered_set_s lookup
        start = high_resolution_clock::now();
        
        size_t found_count_uset = 0;
        for (auto id : lookup_data) {
            if (uset.find(id) != uset.end()) {
                found_count_uset++;
            }
        }
        
        end = high_resolution_clock::now();
        result.unordered_set_s_time_ns = duration_cast<nanoseconds>(end - start).count();
        
        // Benchmark std::vector lookup
        start = high_resolution_clock::now();
        
        size_t found_count_vec = 0;
        for (auto id : lookup_data) {
            if (std::binary_search(vec.begin(), vec.end(), id)) {
                found_count_vec++;
            }
        }
        
        end = high_resolution_clock::now();
        result.vector_time_ns = duration_cast<nanoseconds>(end - start).count();
        
        std::cout << "ID_vector found " << found_count_id_vec << "/" << lookup_data.size() 
                  << " elements in " << result.id_vector_time_ns << " ns\n";
        std::cout << "unordered_set_s found " << found_count_uset << "/" << lookup_data.size() 
                  << " elements in " << result.unordered_set_s_time_ns << " ns\n";
        std::cout << "std::vector found " << found_count_vec << "/" << lookup_data.size() 
                  << " elements in " << result.vector_time_ns << " ns\n";
        
        // Calculate ratios
        result.speedup_vs_unordered_set_s = result.unordered_set_s_time_ns / std::max(1.0, result.id_vector_time_ns);
        result.speedup_vs_vector = result.vector_time_ns / std::max(1.0, result.id_vector_time_ns);
        result.memory_ratio_vs_unordered_set_s = (double)result.id_vector_memory_bytes / result.unordered_set_s_memory_bytes;
        result.memory_ratio_vs_vector = (double)result.id_vector_memory_bytes / result.vector_memory_bytes;
        
        std::cout << "Speedup vs unordered_set_s: " << std::fixed << std::setprecision(2) << result.speedup_vs_unordered_set_s << "x\n";
        std::cout << "Speedup vs vector: " << std::setprecision(2) << result.speedup_vs_vector << "x\n";
        std::cout << "Memory ratio vs unordered_set_s: " << std::setprecision(3) << result.memory_ratio_vs_unordered_set_s << "\n";
        std::cout << "Memory ratio vs vector: " << std::setprecision(3) << result.memory_ratio_vs_vector << "\n";
        
        results.push_back(result);
    }
    
    void benchmark_memory_scaling() {
        std::cout << "\n=== Memory Scaling Analysis ===\n";
        
        std::vector<size_t> max_ids = {1000, 5000, 10000, 50000, 65000}; // Adjusted to fit uint16_t
        std::vector<size_t> element_counts = {100, 500, 1000, 5000, 10000};
        
        std::cout << std::setw(10) << "Max ID" 
                  << std::setw(12) << "Elements"
                  << std::setw(15) << "ID_vec(1bit)"
                  << std::setw(15) << "ID_vec(2bit)"
                  << std::setw(15) << "unordered_set_s"
                  << std::setw(12) << "std::vector"
                  << std::setw(10) << "R1_vs_US"
                  << std::setw(10) << "R1_vs_V"
                  << std::setw(10) << "R2_vs_US"
                  << std::setw(10) << "R2_vs_V" << "\n";
        std::cout << std::string(130, '-') << "\n";
        
        for (size_t i = 0; i < max_ids.size(); ++i) {
            size_t max_id = max_ids[i];
            size_t elem_count = element_counts[i];
            
            try {
                // Create test data
                std::vector<size_t> test_data;
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<size_t> dis(0, max_id);
                
                for (size_t j = 0; j < elem_count; ++j) {
                    test_data.push_back(dis(gen));
                }
                
                // Test all data structures
                ID_vector<uint16_t, 1> vec1(0, static_cast<uint16_t>(max_id));
                ID_vector<uint16_t, 2> vec2(0, static_cast<uint16_t>(max_id));
                std::unordered_set<size_t> uset;
                std::vector<size_t> vec;
                
                for (auto id : test_data) {
                    if (id <= max_id && id <= 65535) { // Additional safety check
                        vec1.push_back(static_cast<uint16_t>(id));
                        vec2.push_back(static_cast<uint16_t>(id));
                        uset.insert(id);
                        auto it = std::lower_bound(vec.begin(), vec.end(), id);
                        if (it == vec.end() || *it != id) {
                            vec.insert(it, id);
                        }
                    }
                }
                
                size_t mem1 = estimate_id_vector_memory(vec1);
                size_t mem2 = estimate_id_vector_memory(vec2);
                size_t mem_uset = estimate_unordered_set_s_memory(uset);
                size_t mem_vec = estimate_vector_memory(vec);
                
                std::cout << std::setw(10) << max_id
                          << std::setw(12) << elem_count
                          << std::setw(15) << mem1
                          << std::setw(15) << mem2
                          << std::setw(15) << mem_uset
                          << std::setw(12) << mem_vec
                          << std::setw(10) << std::fixed << std::setprecision(2) << (double)mem1/mem_uset
                          << std::setw(10) << std::setprecision(2) << (double)mem1/mem_vec
                          << std::setw(10) << std::setprecision(2) << (double)mem2/mem_uset
                          << std::setw(10) << std::setprecision(2) << (double)mem2/mem_vec << "\n";
            } catch (const std::exception& e) {
                std::cout << "Error in memory scaling test: " << e.what() << "\n";
                continue;
            }
        }
    }
    
    void print_summary() {
        std::cout << "\n" << std::string(100, '=') << "\n";
        std::cout << "BENCHMARK SUMMARY\n";
        std::cout << std::string(100, '=') << "\n";
        
        if (results.empty()) {
            std::cout << "No benchmark results to display.\n";
            return;
        }
        
        std::cout << std::setw(25) << "Test Name"
                  << std::setw(12) << "Speed_vs_US"
                  << std::setw(12) << "Speed_vs_V"
                  << std::setw(12) << "Mem_vs_US"
                  << std::setw(12) << "Mem_vs_V"
                  << std::setw(15) << "ID_vec_Time(ns)"
                  << "\n";
        std::cout << std::string(100, '-') << "\n";
        
        double avg_speedup_us = 0.0, avg_speedup_v = 0.0;
        double avg_memory_us = 0.0, avg_memory_v = 0.0;
        
        for (const auto& result : results) {
            std::cout << std::setw(25) << result.test_name
                      << std::setw(12) << std::fixed << std::setprecision(1) << result.speedup_vs_unordered_set_s
                      << std::setw(12) << std::setprecision(1) << result.speedup_vs_vector
                      << std::setw(12) << std::setprecision(3) << result.memory_ratio_vs_unordered_set_s
                      << std::setw(12) << std::setprecision(3) << result.memory_ratio_vs_vector
                      << std::setw(15) << std::setprecision(0) << result.id_vector_time_ns
                      << "\n";
            
            avg_speedup_us += result.speedup_vs_unordered_set_s;
            avg_speedup_v += result.speedup_vs_vector;
            avg_memory_us += result.memory_ratio_vs_unordered_set_s;
            avg_memory_v += result.memory_ratio_vs_vector;
        }
        
        avg_speedup_us /= results.size();
        avg_speedup_v /= results.size();
        avg_memory_us /= results.size();
        avg_memory_v /= results.size();
        
        std::cout << std::string(100, '-') << "\n";
        std::cout << std::setw(25) << "AVERAGE"
                  << std::setw(12) << std::fixed << std::setprecision(1) << avg_speedup_us
                  << std::setw(12) << std::setprecision(1) << avg_speedup_v
                  << std::setw(12) << std::setprecision(3) << avg_memory_us
                  << std::setw(12) << std::setprecision(3) << avg_memory_v
                  << std::setw(15) << "-"
                  << "\n";
        
        std::cout << "\nKey Findings:\n";
        std::cout << "• ID_vector vs unordered_set_s: " << std::setprecision(1) << avg_speedup_us << "x faster, " 
                  << std::setprecision(1) << (avg_memory_us * 100) << "% memory\n";
        std::cout << "• ID_vector vs std::vector: " << std::setprecision(1) << avg_speedup_v << "x faster, " 
                  << std::setprecision(1) << (avg_memory_v * 100) << "% memory\n";
        
        // Save results to CSV file for visualization
        save_results_to_csv();
    }
    
    void save_results_to_csv() {
        std::ofstream file("benchmark_results.csv");
        if (!file.is_open()) {
            std::cerr << "Error: Could not open benchmark_results.csv for writing\n";
            return;
        }
        
        // Write header
        file << "Test_Name,ID_vector_Time_ns,unordered_set_s_Time_ns,vector_Time_ns,"
             << "ID_vector_Memory_bytes,unordered_set_s_Memory_bytes,vector_Memory_bytes,"
             << "Speedup_vs_unordered_set_s,Speedup_vs_vector,"
             << "Memory_Ratio_vs_unordered_set_s,Memory_Ratio_vs_vector\n";
        
        // Write data
        for (const auto& result : results) {
            file << result.test_name << ","
                 << result.id_vector_time_ns << ","
                 << result.unordered_set_s_time_ns << ","
                 << result.vector_time_ns << ","
                 << result.id_vector_memory_bytes << ","
                 << result.unordered_set_s_memory_bytes << ","
                 << result.vector_memory_bytes << ","
                 << result.speedup_vs_unordered_set_s << ","
                 << result.speedup_vs_vector << ","
                 << result.memory_ratio_vs_unordered_set_s << ","
                 << result.memory_ratio_vs_vector << "\n";
        }
        
        file.close();
        std::cout << "\nResults saved to benchmark_results.csv\n";
    }
    
    void run_comprehensive_benchmark() {
        std::cout << "🚀 Starting Comprehensive ID_vector vs unordered_set_s Benchmark\n";
        std::cout << std::string(80, '=') << "\n";
        
        // Test data generation
        std::random_device rd;
        std::mt19937 gen(42); // Fixed seed for reproducibility
        
        // Test 1: Small dataset, sparse IDs
        {
            std::vector<uint16_t> sparse_data;
            std::uniform_int_distribution<uint16_t> sparse_dis(0, 10000);
            for (int i = 0; i < 1000; ++i) {
                sparse_data.push_back(sparse_dis(gen));
            }
            benchmark_insertion<uint16_t, 1>("Small Sparse Dataset (BPV=1)", 10000, sparse_data);
            benchmark_lookup<uint16_t, 1>("Small Sparse Lookup (BPV=1)", 10000, sparse_data, sparse_data);
        }
        
        // Test 2: Dense dataset
        {
            std::vector<uint16_t> dense_data;
            for (uint16_t i = 0; i < 1000; ++i) {
                dense_data.push_back(i);
            }
            benchmark_insertion<uint16_t, 1>("Dense Dataset (BPV=1)", 1000, dense_data);
            benchmark_lookup<uint16_t, 1>("Dense Lookup (BPV=1)", 1000, dense_data, dense_data);
        }
        
        // Test 3: Large dataset with duplicates (BPV=2)
        {
            std::vector<uint16_t> dup_data;
            std::uniform_int_distribution<uint16_t> dup_dis(0, 5000);
            for (int i = 0; i < 10000; ++i) {
                dup_data.push_back(dup_dis(gen));
            }
            benchmark_insertion<uint16_t, 2>("Large Dataset with Duplicates (BPV=2)", 5000, dup_data);
            benchmark_lookup<uint16_t, 2>("Large Lookup with Duplicates (BPV=2)", 5000, dup_data, dup_data);
        }
        
        // Test 4: Very large sparse dataset
        {
            std::vector<uint32_t> huge_sparse;
            std::uniform_int_distribution<uint32_t> huge_dis(0, 100000); // Reduced for reasonable performance
            for (int i = 0; i < 5000; ++i) {
                huge_sparse.push_back(huge_dis(gen));
            }
            benchmark_insertion<uint32_t, 1>("Very Large Sparse (BPV=1)", 100000, huge_sparse);
            benchmark_lookup<uint32_t, 1>("Very Large Sparse Lookup (BPV=1)", 100000, huge_sparse, huge_sparse);
        }
        
        // Memory scaling analysis
        benchmark_memory_scaling();
        
        // Print summary
        print_summary();
    }
};

int main() {
    try {
        BenchmarkSuite suite;
        suite.run_comprehensive_benchmark();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
