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

    #define MAKE_INT_LIST(...)   \
        min_init_list<int>((const int[]){__VA_ARGS__},  \
        sizeof((int[]){__VA_ARGS__})/sizeof(int))
    #define MAKE_FLOAT_LIST(...) \
        min_init_list<float>((const float[]){__VA_ARGS__}, \
        sizeof((float[]){__VA_ARGS__})/sizeof(float))
    #define MAKE_CHAR_LIST(...)  \
        min_init_list<const char*>((const char*[]){__VA_ARGS__},\
        sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*))
    // Define a simple initializer list for strings (standard c++ and arduino compatible)
    #if defined(ARDUINO)
    #define MAKE_STRING_LIST(...) \
        min_init_list<String>((const String[]){__VA_ARGS__}, \
        sizeof((String[]){__VA_ARGS__})/sizeof(String))
    #else
    #define MAKE_STRING_LIST(...) \
        min_init_list<std::string>((const std::string[]){__VA_ARGS__}, \
        sizeof((std::string[]){__VA_ARGS__})/sizeof(std::string))
    #endif

    #define MAKE_BOOL_LIST(...) \
        min_init_list<bool>((const bool[]){__VA_ARGS__}, \
        sizeof((bool[]){__VA_ARGS__})/sizeof(bool)) 
    #define MAKE_UINT8_LIST(...) \
        min_init_list<uint8_t>((const uint8_t[]){__VA_ARGS__}, \
        sizeof((uint8_t[]){__VA_ARGS__})/sizeof(uint8_t))           
    #define MAKE_UINT16_LIST(...) \
        min_init_list<uint16_t>((const uint16_t[]){__VA_ARGS__}, \
        sizeof((uint16_t[]){__VA_ARGS__})/sizeof(uint16_t))
    #define MAKE_UINT32_LIST(...) \
        min_init_list<uint32_t>((const uint32_t[]){__VA_ARGS__}, \
        sizeof((uint32_t[]){__VA_ARGS__})/sizeof(uint32_t))     
    #define MAKE_UINT64_LIST(...) \
        min_init_list<uint64_t>((const uint64_t[]){__VA_ARGS__}, \
        sizeof((uint64_t[]){__VA_ARGS__})/sizeof(uint64_t))     
    #define MAKE_SIZE_T_LIST(...) \
        min_init_list<size_t>((const size_t[]){__VA_ARGS__}, \
        sizeof((size_t[]){__VA_ARGS__})/sizeof(size_t))
    #define MAKE_DOUBLE_LIST(...) \
        min_init_list<double>((const double[]){__VA_ARGS__}, \
        sizeof((double[]){__VA_ARGS__})/sizeof(double))

    enum class index_size_flag{
        TINY,
        SMALL,
        MEDIUM,
        LARGE
    };

    template<index_size_flag Flag>
    struct vector_index_type;

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
        using type = uint32_t;
    };

    // #define SMALL_SIZE index_size_flag::SMALL
    // #define MEDIUM_SIZE index_size_flag::MEDIUM
    // #define LARGE_SIZE index_size_flag::LARGE

    static constexpr index_size_flag TINY   = index_size_flag::TINY;
    static constexpr index_size_flag SMALL  = index_size_flag::SMALL;
    static constexpr index_size_flag MEDIUM = index_size_flag::MEDIUM;
    static constexpr index_size_flag LARGE  = index_size_flag::LARGE;

    template<typename T>
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
}
