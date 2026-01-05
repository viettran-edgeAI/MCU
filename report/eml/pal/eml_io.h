#pragma once
/**
 * @file eml_io.h
 * @brief Platform Abstraction Layer - I/O Interface
 * 
 * Declares platform-agnostic logging and serial I/O functions.
 * Each platform must implement these functions.
 */

#include <cstdint>
#include <cstddef>

namespace eml {
namespace pal {

/**
 * @brief Debug level enumeration for logging granularity
 */
enum class EmlDebugLevel : uint8_t {
    SILENT   = 0,  // No messages
    FOREST   = 1,  // Major events (start, end, forest-level)
    COMPONENT = 2, // Component-level + warnings
    DETAILED = 3   // All memory, timing, detailed info
};

/**
 * @brief Initialize the I/O subsystem
 * @param baud_rate Serial baud rate (ignored on some platforms)
 * @return true if initialization successful
 */
bool eml_io_init(uint32_t baud_rate = 115200);

/**
 * @brief Print a formatted message (printf-style)
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void eml_printf(const char* format, ...);

/**
 * @brief Print a string followed by newline
 * @param msg Message to print
 */
void eml_println(const char* msg);

/**
 * @brief Print a string without newline
 * @param msg Message to print
 */
void eml_print(const char* msg);

/**
 * @brief Check if input data is available
 * @return Number of bytes available to read
 */
int eml_input_available();

/**
 * @brief Read a single byte from input
 * @return Byte read, or -1 if no data
 */
int eml_input_read();

/**
 * @brief Read a line of input until delimiter
 * @param buffer Buffer to store the line
 * @param max_len Maximum buffer length
 * @param delimiter Character to stop reading at
 * @return Number of characters read
 */
size_t eml_input_read_line(char* buffer, size_t max_len, char delimiter = '\n');

/**
 * @brief Flush the output buffer
 */
void eml_io_flush();

/**
 * @brief Get the current debug level
 * @return Current debug level
 */
EmlDebugLevel eml_get_debug_level();

/**
 * @brief Set the debug level
 * @param level New debug level
 */
void eml_set_debug_level(EmlDebugLevel level);

} // namespace pal
} // namespace eml

// Convenience macros for debug output
#ifndef EML_DEBUG_LEVEL
    #define EML_DEBUG_LEVEL 1
#endif

#define eml_debug(level, ...) \
    do { \
        if (EML_DEBUG_LEVEL > (level)) { \
            eml::pal::eml_printf(__VA_ARGS__); \
            eml::pal::eml_println(""); \
        } \
    } while(0)

#define eml_debug_2(level, msg1, obj1, msg2, obj2) \
    do { \
        if (EML_DEBUG_LEVEL > (level)) { \
            eml::pal::eml_printf("%s%s %s%s\n", msg1, obj1, msg2, obj2); \
        } \
    } while(0)
