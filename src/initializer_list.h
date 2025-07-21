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
    #define MAKE_FLOAT_LIST(...) \
        min_init_list<float>((const float[]){__VA_ARGS__}, \
        sizeof((float[]){__VA_ARGS__})/sizeof(float))
    #define MAKE_CHAR_LIST(...)  \
        min_init_list<const char*>((const char*[]){__VA_ARGS__},\
        sizeof((const char*[]){__VA_ARGS__})/sizeof(const char*))

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
        using type = typename std::conditional<
            sizeof(T) <= 1,
            uint16_t,
            size_t
        >::type;
    };
}