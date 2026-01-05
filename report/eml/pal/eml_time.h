#pragma once
/**
 * @file eml_time.h
 * @brief Platform Abstraction Layer - Time Interface
 * 
 * Declares platform-agnostic time and random number functions.
 * Each platform must implement these functions.
 */

#include <cstdint>
#include <cstddef>

namespace eml {
namespace pal {

/**
 * @brief Time unit enumeration
 */
enum class EmlTimeUnit : uint8_t {
    MILLISECONDS = 0,
    MICROSECONDS = 1,
    NANOSECONDS  = 2
};

/**
 * @brief Initialize the time subsystem
 * @return true if initialization successful
 */
bool eml_time_init();

/**
 * @brief Get current time since system start
 * @param unit Time unit for result
 * @return Time value in specified units
 */
uint64_t eml_time_now(EmlTimeUnit unit = EmlTimeUnit::MILLISECONDS);

/**
 * @brief Get current time in milliseconds
 * @return Milliseconds since system start
 */
uint64_t eml_millis();

/**
 * @brief Get current time in microseconds
 * @return Microseconds since system start
 */
uint64_t eml_micros();

/**
 * @brief Delay execution for specified duration
 * @param ms Milliseconds to delay
 */
void eml_delay_ms(uint32_t ms);

/**
 * @brief Delay execution for specified microseconds
 * @param us Microseconds to delay
 */
void eml_delay_us(uint32_t us);

/**
 * @brief Yield execution to other tasks/threads
 */
void eml_yield();

/**
 * @brief Get hardware entropy for random seeding
 * @return 64-bit entropy value from hardware sources
 */
uint64_t eml_random_entropy();

/**
 * @brief Get a hardware random number (if available)
 * @return 32-bit random value from hardware RNG
 */
uint32_t eml_hardware_random();

/**
 * @brief Get CPU cycle count (for timing/entropy)
 * @return CPU cycle counter value
 */
uint64_t eml_cpu_cycles();

} // namespace pal
} // namespace eml
