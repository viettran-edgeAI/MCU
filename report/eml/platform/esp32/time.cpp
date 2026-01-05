/**
 * @file time.cpp
 * @brief ESP32 Platform - Time Implementation
 * 
 * Implements eml_time.h interface for ESP32 + Arduino.
 */

#include "../../pal/eml_time.h"
#include "eml_esp32.h"
#include <Arduino.h>
#include <esp_random.h>

namespace eml {
namespace pal {

bool eml_time_init() {
    // Time subsystem is initialized by Arduino framework
    return true;
}

uint64_t eml_time_now(EmlTimeUnit unit) {
    switch (unit) {
        case EmlTimeUnit::MICROSECONDS:
            return static_cast<uint64_t>(micros());
        case EmlTimeUnit::NANOSECONDS:
            return static_cast<uint64_t>(micros()) * 1000ULL;
        case EmlTimeUnit::MILLISECONDS:
        default:
            return static_cast<uint64_t>(millis());
    }
}

uint64_t eml_millis() {
    return static_cast<uint64_t>(millis());
}

uint64_t eml_micros() {
    return static_cast<uint64_t>(micros());
}

void eml_delay_ms(uint32_t ms) {
    delay(ms);
}

void eml_delay_us(uint32_t us) {
    delayMicroseconds(us);
}

void eml_yield() {
    yield();
}

uint64_t eml_random_entropy() {
    // Combine multiple entropy sources
    uint64_t hw1 = esp_random();
    uint64_t hw2 = esp_random();
    uint64_t cycles = ESP.getCycleCount();
    uint64_t time_us = micros();
    
    // Mix entropy sources
    return (hw1 << 32) ^ hw2 ^ (cycles << 16) ^ time_us;
}

uint32_t eml_hardware_random() {
    return esp_random();
}

uint64_t eml_cpu_cycles() {
    return static_cast<uint64_t>(ESP.getCycleCount());
}

} // namespace pal
} // namespace eml
