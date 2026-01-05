#pragma once
#include "eml/pal/eml_io.h"
#include "eml/core/eml_config.h"
#include <type_traits>

namespace eml {

// Helper for printing different types
template<typename T>
void eml_print_val(const T& val) {
    if constexpr (std::is_floating_point_v<T>) {
        pal::eml_printf("%.3f", val);
    } else if constexpr (std::is_integral_v<T>) {
        if constexpr (std::is_signed_v<T>) {
            pal::eml_printf("%lld", static_cast<long long>(val));
        } else {
            pal::eml_printf("%llu", static_cast<unsigned long long>(val));
        }
    } else {
        // Fallback
    }
}

// Overload for const char*
inline void eml_print_val(const char* val) {
    pal::eml_printf("%s", val);
}
inline void eml_print_val(char* val) {
    pal::eml_printf("%s", val);
}

// Variadic debug function
// eml_debug(level, msg, [val])

inline void eml_debug(int level, const char* msg) {
    if (level <= EML_DEBUG_LEVEL) {
        pal::eml_println(msg);
    }
}

template<typename T>
inline void eml_debug(int level, const char* msg, const T& val) {
    if (level <= EML_DEBUG_LEVEL) {
        pal::eml_print(msg);
        eml_print_val(val);
        pal::eml_println("");
    }
}

} // namespace eml
