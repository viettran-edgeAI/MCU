/**
 * @file time.cpp
 * @brief POSIX Platform - Time Implementation
 * 
 * Implements eml_time.h interface for POSIX systems.
 */

#include "../../pal/eml_time.h"
#include "eml_posix.h"
#include <chrono>
#include <thread>
#include <ctime>
#include <cstdlib>
#include <fstream>

namespace eml {
namespace pal {

// Track start time for millis/micros
static std::chrono::steady_clock::time_point g_start_time;
static bool g_time_initialized = false;

bool eml_time_init() {
    if (!g_time_initialized) {
        g_start_time = std::chrono::steady_clock::now();
        g_time_initialized = true;
    }
    return true;
}

uint64_t eml_time_now(EmlTimeUnit unit) {
    if (!g_time_initialized) {
        eml_time_init();
    }
    
    auto now = std::chrono::steady_clock::now();
    auto duration = now - g_start_time;
    
    switch (unit) {
        case EmlTimeUnit::MICROSECONDS:
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
        case EmlTimeUnit::NANOSECONDS:
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
        case EmlTimeUnit::MILLISECONDS:
        default:
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }
}

uint64_t eml_millis() {
    return eml_time_now(EmlTimeUnit::MILLISECONDS);
}

uint64_t eml_micros() {
    return eml_time_now(EmlTimeUnit::MICROSECONDS);
}

void eml_delay_ms(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void eml_delay_us(uint32_t us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void eml_yield() {
    std::this_thread::yield();
}

uint64_t eml_random_entropy() {
    // Combine multiple entropy sources for POSIX
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t time_val = static_cast<uint64_t>(duration.count());
    
    auto steady_now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t mono_val = static_cast<uint64_t>(steady_now.count());
    
    // Use current stack address as additional entropy
    volatile int stack_var;
    uint64_t addr_val = reinterpret_cast<uint64_t>(&stack_var);
    
    // Try to read from /dev/urandom on Linux/macOS
    uint64_t urandom_val = 0;
#if defined(__linux__) || defined(__APPLE__)
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(&urandom_val), sizeof(urandom_val));
    }
#endif
    
    // Mix all entropy sources
    return time_val ^ (mono_val << 1) ^ (addr_val >> 3) ^ urandom_val;
}

uint32_t eml_hardware_random() {
    // Use /dev/urandom if available, otherwise fall back to rand()
    uint32_t val = 0;
    
#if defined(__linux__) || defined(__APPLE__)
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom) {
        urandom.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }
#endif
    
    // Fallback to standard library random
    static bool seeded = false;
    if (!seeded) {
        srand(static_cast<unsigned int>(time(nullptr)));
        seeded = true;
    }
    
    val = static_cast<uint32_t>(rand()) ^ (static_cast<uint32_t>(rand()) << 16);
    return val;
}

uint64_t eml_cpu_cycles() {
#if defined(__x86_64__) || defined(__i386__)
    // Use RDTSC on x86
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__aarch64__)
    // ARM64: Use CNTVCT_EL0
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    // Fallback: use high-resolution clock
    return static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
#endif
}

} // namespace pal
} // namespace eml
