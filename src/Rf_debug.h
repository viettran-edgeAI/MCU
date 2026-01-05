#pragma once
#include <Arduino.h>

#ifndef RF_DEBUG_LEVEL
    #define RF_DEBUG_LEVEL 1
#else
    #if RF_DEBUG_LEVEL > 3
        #undef RF_DEBUG_LEVEL
        #define RF_DEBUG_LEVEL 3
    #endif
#endif

/*
 RF_DEBUG_LEVEL :
    0 : silent mode - no messages
    1 : forest messages (start, end, major events) 
    2 : messages at components level + warnings
    3 : all memory and event timing messages & detailed info
 note: all errors messages (lead to failed process) will be enabled with RF_DEBUG_LEVEL >=1
*/

inline void rf_debug_print(const char* msg) {
    Serial.printf("%s\n", msg);
}
template<typename T>
inline void rf_debug_print(const char* msg, const T& obj) {
#if RF_DEBUG_LEVEL > 0
    Serial.printf("%s", msg);
    if constexpr (std::is_floating_point_v<T>) {
        Serial.println(obj, 3);  // 3 decimal places for floats/doubles
    } else {
        Serial.println(obj);
    }
#endif
}

template<typename T1, typename T2>
inline void rf_debug_print_2(const char* msg1, const T1& obj1, const char* msg2, const T2& obj2) {
#if RF_DEBUG_LEVEL > 0
    Serial.printf("%s", msg1);
    if constexpr (std::is_floating_point_v<T1>) {
        Serial.print(obj1, 3);
    } else {
        Serial.print(obj1);
    }
    Serial.printf(" %s", msg2);
    if constexpr (std::is_floating_point_v<T2>) {
        Serial.println(obj2, 3);
    } else {
        Serial.println(obj2);
    }
#endif
}

#define eml_debug(level, ...)                        \
    do{                                              \
        if constexpr (RF_DEBUG_LEVEL > (level)) {     \
            rf_debug_print(__VA_ARGS__);               \
        }                                               \
    }while(0)

#define eml_debug_2(level, msg1, obj1, msg2, obj2)          \
    do{                                                     \
        if constexpr (RF_DEBUG_LEVEL > (level)) {            \
            rf_debug_print_2(msg1, obj1, msg2, obj2);         \
        }                                                      \
    }while(0)
