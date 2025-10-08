#pragma once

namespace mcu {
    // Add embedded-specific conditional implementation
    template<bool B, typename T, typename F>
    struct conditional_t {
        using type = T;
    };

    template<typename T, typename F>
    struct conditional_t<false, T, F> {
        using type = F;
    };

    template<typename T>
    struct min_init_list {
        const T* data_;
        unsigned size_;
        min_init_list(const T* d, unsigned s) : data_(d), size_(s) {}
        const T* begin() const { return data_; }
        const T* end() const { return data_ + size_; }
        unsigned size() const { return size_; }
    };

    // Generic macro that works with any type
    #define MAKE_LIST(type, ...) \
        min_init_list<type>((const type[]){__VA_ARGS__}, \
        sizeof((type[]){__VA_ARGS__})/sizeof(type))

    // Convenience macros for common types (kept for backward compatibility)
    #define MAKE_INT_LIST(...)     MAKE_LIST(int, __VA_ARGS__)
    #define MAKE_FLOAT_LIST(...)   MAKE_LIST(float, __VA_ARGS__)
    #define MAKE_CHAR_LIST(...)    MAKE_LIST(const char*, __VA_ARGS__)
    #define MAKE_BOOL_LIST(...)    MAKE_LIST(bool, __VA_ARGS__)
    #define MAKE_UINT8_LIST(...)   MAKE_LIST(uint8_t, __VA_ARGS__)
    #define MAKE_UINT16_LIST(...)  MAKE_LIST(uint16_t, __VA_ARGS__)
    #define MAKE_UINT32_LIST(...)  MAKE_LIST(uint32_t, __VA_ARGS__)
    #define MAKE_UINT64_LIST(...)  MAKE_LIST(uint64_t, __VA_ARGS__)
    #define MAKE_SIZE_T_LIST(...)  MAKE_LIST(size_t, __VA_ARGS__)
    #define MAKE_DOUBLE_LIST(...)  MAKE_LIST(double, __VA_ARGS__)
    
    // String handling (platform-specific)
    #if defined(ARDUINO)
    #define MAKE_STRING_LIST(...)  MAKE_LIST(String, __VA_ARGS__)
    #else
    #define MAKE_STRING_LIST(...)  MAKE_LIST(std::string, __VA_ARGS__)
    #endif

    enum class index_size_flag{
        TINY,
        SMALL,
        MEDIUM,
        LARGE
    };

    template<index_size_flag Flag>
    struct vector_index_type;       // for vector classes

    template<>
    struct vector_index_type<index_size_flag::TINY> {
        using type = uint8_t;
    };
    template<>
    struct vector_index_type<index_size_flag::SMALL> {
        using type = uint8_t;
    };
    template<>
    struct vector_index_type<index_size_flag::MEDIUM> {
        using type = uint16_t;
    };
    template<>
    struct vector_index_type<index_size_flag::LARGE> {
        using type = size_t;
    };

    // #define SMALL_SIZE index_size_flag::SMALL
    // #define MEDIUM_SIZE index_size_flag::MEDIUM
    // #define LARGE_SIZE index_size_flag::LARGE

    static constexpr index_size_flag TINY   = index_size_flag::TINY;
    static constexpr index_size_flag SMALL  = index_size_flag::SMALL;
    static constexpr index_size_flag MEDIUM = index_size_flag::MEDIUM;
    static constexpr index_size_flag LARGE  = index_size_flag::LARGE;

    template<typename T>        // for another class 
    struct index_type {
        using type = typename conditional_t<
            sizeof(T) <= 1,
            uint16_t,
            size_t
        >::type;
    };

    template<typename T, typename U>
    struct is_same_t {
        static constexpr bool value = false;
    };

    template<typename T>
    struct is_same_t<T, T> {
        static constexpr bool value = true;
    };

    template<typename U, typename = void>
    struct less_comparable : std::false_type {};

    template<typename U>
    struct less_comparable<U, std::void_t<decltype(std::declval<U>() < std::declval<U>())>>
        : std::integral_constant<bool,
            std::is_convertible<decltype(std::declval<U>() < std::declval<U>()), bool>::value> {};
}
