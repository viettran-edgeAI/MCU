#pragma once

namespace mcu {
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
    // uint8_t 
    #define MAKE_UINT8_T_LIST(...)  \
        min_init_list<uint8_t>((const uint8_t[]){__VA_ARGS__}, \
        sizeof((uint8_t[]){__VA_ARGS__})/sizeof(uint8_t))
    #define MAKE_UINT16_T_LIST(...) \
        min_init_list<uint16_t>((const uint16_t[]){__VA_ARGS__}, \
        sizeof((uint16_t[]){__VA_ARGS__})/sizeof(uint16_t))
    #define MAKE_UINT32_T_LIST(...) \
        min_init_list<uint32_t>((const uint32_t[]){__VA_ARGS__}, \
        sizeof((uint32_t[]){__VA_ARGS__})/sizeof(uint32_t))
    #define MAKE_UINT64_T_LIST(...) \
        min_init_list<uint64_t>((const uint64_t[]){__VA_ARGS__}, \
        sizeof((uint64_t[]){__VA_ARGS__})/sizeof(uint64_t))
    #define MAKE_DOUBLE_LIST(...) \
        min_init_list<double>((const double[]){__VA_ARGS__}, \
        sizeof((double[]){__VA_ARGS__})/sizeof(double))
    #define MAKE_FLOAT_LIST(...) \
        min_init_list<float>((const float[]){__VA_ARGS__}, \
        sizeof((float[]){__VA_ARGS__})/sizeof(float))
    #define MAKE_CHAR_LIST(...)  \
        min_init_list<const char*>((const char*[]){__VA_ARGS__},\
        sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*))

    #define MAKE_STRING_LIST(...) \
        min_init_list<String>((const String[]){__VA_ARGS__}, \
        sizeof((String[]){__VA_ARGS__})/sizeof(String))    
    #define MKAE_SIZE_T_LIST(...) \
        min_init_list<size_t>((const size_t[]){__VA_ARGS__}, \
        sizeof((size_t[]){__VA_ARGS__})/sizeof(size_t))
    

    enum class index_size_flag{
        SMALL,
        MEDIUM,
        LARGE
    };

    template<index_size_flag Flag>
    struct vector_index_type;

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

    static constexpr index_size_flag SMALL  = index_size_flag::SMALL;
    static constexpr index_size_flag MEDIUM = index_size_flag::MEDIUM;
    static constexpr index_size_flag LARGE  = index_size_flag::LARGE;

    template<typename T>
    struct index_type {
        using type = typename std::conditional<
            sizeof(T) <= 1,
            uint16_t,
            size_t
        >::type;
    };
}
