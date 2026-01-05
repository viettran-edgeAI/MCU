/**
 * @file io.cpp
 * @brief POSIX Platform - I/O Implementation
 * 
 * Implements eml_io.h interface for POSIX systems (Linux, macOS, BSD).
 */

#include "../../pal/eml_io.h"
#include "eml_posix.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <poll.h>

namespace eml {
namespace pal {

static EmlDebugLevel g_debug_level = EmlDebugLevel::FOREST;

bool eml_io_init(uint32_t baud_rate) {
    (void)baud_rate;  // Baud rate not applicable for POSIX stdio
    // stdin/stdout are already available
    return true;
}

void eml_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
}

void eml_println(const char* msg) {
    printf("%s\n", msg);
    fflush(stdout);
}

void eml_print(const char* msg) {
    printf("%s", msg);
    fflush(stdout);
}

int eml_input_available() {
    struct pollfd fds;
    fds.fd = STDIN_FILENO;
    fds.events = POLLIN;
    
    int ret = poll(&fds, 1, 0);  // Non-blocking check
    if (ret > 0 && (fds.revents & POLLIN)) {
        return 1;
    }
    return 0;
}

int eml_input_read() {
    if (eml_input_available()) {
        return getchar();
    }
    return -1;
}

size_t eml_input_read_line(char* buffer, size_t max_len, char delimiter) {
    if (!buffer || max_len == 0) return 0;
    
    size_t i = 0;
    int c;
    
    while (i < max_len - 1) {
        c = getchar();
        if (c == EOF || c == delimiter) {
            break;
        }
        buffer[i++] = static_cast<char>(c);
    }
    
    buffer[i] = '\0';
    return i;
}

void eml_io_flush() {
    fflush(stdout);
    fflush(stderr);
}

EmlDebugLevel eml_get_debug_level() {
    return g_debug_level;
}

void eml_set_debug_level(EmlDebugLevel level) {
    g_debug_level = level;
}

} // namespace pal
} // namespace eml
