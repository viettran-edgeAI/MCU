/**
 * @file io.cpp
 * @brief ESP32 Platform - I/O Implementation
 * 
 * Implements eml_io.h interface for ESP32 + Arduino.
 */

#include "../../pal/eml_io.h"
#include "eml_esp32.h"
#include <Arduino.h>
#include <cstdarg>

namespace eml {
namespace pal {

static EmlDebugLevel g_debug_level = EmlDebugLevel::FOREST;

bool eml_io_init(uint32_t baud_rate) {
    Serial.begin(baud_rate);
    
    // Wait for serial to be ready (with timeout)
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 3000) {
        delay(10);
    }
    
    return true;
}

void eml_printf(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
}

void eml_println(const char* msg) {
    Serial.println(msg);
}

void eml_print(const char* msg) {
    Serial.print(msg);
}

int eml_input_available() {
    return Serial.available();
}

int eml_input_read() {
    return Serial.read();
}

size_t eml_input_read_line(char* buffer, size_t max_len, char delimiter) {
    if (!buffer || max_len == 0) return 0;
    
    String line = Serial.readStringUntil(delimiter);
    size_t len = line.length();
    if (len >= max_len) {
        len = max_len - 1;
    }
    memcpy(buffer, line.c_str(), len);
    buffer[len] = '\0';
    return len;
}

void eml_io_flush() {
    Serial.flush();
}

EmlDebugLevel eml_get_debug_level() {
    return g_debug_level;
}

void eml_set_debug_level(EmlDebugLevel level) {
    g_debug_level = level;
}

} // namespace pal
} // namespace eml
