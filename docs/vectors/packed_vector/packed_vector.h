#pragma once

#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <cassert>
#include <utility>
#include <limits>

#include "../../../src/initializer_list.h"
#include "../../../src/hash_kernel.h"

namespace mcu {

    // Memory allocation helpers - automatically use PSRAM when enabled
    namespace mem_alloc {
        namespace detail {
            constexpr uint8_t FLAG_PSRAM = 0x1;
            constexpr size_t header_payload = sizeof(size_t) + sizeof(uint8_t);
            constexpr size_t header_padding = (alignof(std::max_align_t) - (header_payload % alignof(std::max_align_t))) % alignof(std::max_align_t);

            struct alignas(std::max_align_t) AllocationHeader {
                size_t count;
                uint8_t flags;
                std::array<uint8_t, header_padding> padding{};

                AllocationHeader(size_t c, uint8_t f) noexcept : count(c), flags(f) {}

                static constexpr size_t stride() noexcept { return sizeof(AllocationHeader); }
                bool uses_psram() const noexcept { return (flags & FLAG_PSRAM) != 0; }
            };
        }

        template<typename T>
        inline T* allocate(size_t count) {
            const size_t recorded_count = count;
            const size_t actual_count = (count == 0) ? 1 : count;
            if (actual_count == 0) {
                return nullptr;
            }

            constexpr size_t stride = detail::AllocationHeader::stride();
            const size_t alignment = std::max<size_t>(alignof(T), alignof(size_t));
            const size_t total_bytes = stride + actual_count * sizeof(T) + alignment + sizeof(size_t);

            uint8_t flags = 0;
            void* raw = nullptr;

            #if RF_PSRAM_AVAILABLE
                raw = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (raw) {
                    flags = detail::FLAG_PSRAM;
                }
            #endif

            if (!raw) {
                raw = std::malloc(total_bytes);
                if (!raw) {
                    return nullptr;
                }
            }

            auto* base = static_cast<uint8_t*>(raw);
            auto* header = new(raw) detail::AllocationHeader(recorded_count, flags);

            uint8_t* data_start = base + stride;
            uintptr_t aligned_addr = (reinterpret_cast<uintptr_t>(data_start) + alignment - 1) & ~(alignment - 1);
            size_t offset = aligned_addr - reinterpret_cast<uintptr_t>(data_start);
            while (offset < sizeof(size_t)) {
                aligned_addr += alignment;
                offset += alignment;
            }

            auto* aligned_data = reinterpret_cast<uint8_t*>(aligned_addr);
            auto* offset_slot = reinterpret_cast<size_t*>(aligned_data) - 1;
            *offset_slot = offset;

            auto* data = reinterpret_cast<T*>(aligned_data);

            if constexpr (!std::is_trivially_default_constructible_v<T>) {
                for (size_t i = 0; i < recorded_count; ++i) {
                    new (data + i) T();
                }
            }

            return data;
        }

        template<typename T>
        inline void deallocate(T* ptr) {
            if (!ptr) {
                return;
            }

            constexpr size_t stride = detail::AllocationHeader::stride();
            auto* data_bytes = reinterpret_cast<uint8_t*>(ptr);
            size_t offset = *(reinterpret_cast<const size_t*>(data_bytes) - 1);
            auto* raw = data_bytes - offset - stride;
            auto* header = reinterpret_cast<detail::AllocationHeader*>(raw);

            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = 0; i < header->count; ++i) {
                    ptr[i].~T();
                }
            }

            const bool uses_psram = header->uses_psram();
            header->~AllocationHeader();

            #if RF_PSRAM_AVAILABLE
                if (uses_psram) {
                    heap_caps_free(raw);
                    return;
                }
            #endif

            std::free(raw);
        }

        // Get allocation info for debugging
        inline bool is_psram_ptr(const void* ptr) {
            #if RF_PSRAM_AVAILABLE
                if (!ptr) {
                    return false;
                }
                constexpr size_t stride = detail::AllocationHeader::stride();
                auto* data_bytes = reinterpret_cast<const uint8_t*>(ptr);
                size_t offset = *(reinterpret_cast<const size_t*>(data_bytes) - 1);
                auto* raw = data_bytes - offset - stride;
                auto* header = reinterpret_cast<const detail::AllocationHeader*>(raw);
                return header->uses_psram();
            #else
                (void)ptr;
                return false;
            #endif
        }

        inline size_t get_free_psram() {
            #if RF_PSRAM_AVAILABLE
                return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            #else
                return 0;
            #endif
        }

        inline size_t get_total_psram() {
            #if RF_PSRAM_AVAILABLE
                return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
            #else
                return 0;
            #endif
        }
    }

    
    template<uint8_t BitsPerElement>
    class PackedArray {
        static_assert(BitsPerElement > 0 && BitsPerElement <= 32, "Invalid bit size");

        uint32_t* data = nullptr;
        uint8_t bpv_ = BitsPerElement;
        size_t capacity_words_ = 0;

    public:
        PackedArray() = default;

        explicit PackedArray(size_t capacity_words)
            : bpv_(BitsPerElement), capacity_words_(capacity_words) {
            if (capacity_words_ > 0) {
                data = mem_alloc::allocate<uint32_t>(capacity_words_);
                if (data) {
                    std::fill_n(data, capacity_words_, static_cast<uint32_t>(0));
                } else {
                    capacity_words_ = 0;
                }
            }
        }

        ~PackedArray() {
            mem_alloc::deallocate(data);
            data = nullptr;
            capacity_words_ = 0;
        }

        PackedArray(const PackedArray& other, size_t words)
            : bpv_(other.bpv_), capacity_words_(words) {
            if (capacity_words_ > 0) {
                data = mem_alloc::allocate<uint32_t>(capacity_words_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_words_, data);
                    } else {
                        std::fill_n(data, capacity_words_, static_cast<uint32_t>(0));
                    }
                } else {
                    capacity_words_ = 0;
                }
            }
        }

        PackedArray(PackedArray&& other) noexcept
            : data(other.data), bpv_(other.bpv_), capacity_words_(other.capacity_words_) {
            other.data = nullptr;
            other.capacity_words_ = 0;
        }

        void copy_from(const PackedArray& other, size_t words) {
            if (this == &other) {
                bpv_ = other.bpv_;
                capacity_words_ = words;
                return;
            }

            mem_alloc::deallocate(data);
            data = nullptr;
            capacity_words_ = words;

            if (capacity_words_ > 0) {
                data = mem_alloc::allocate<uint32_t>(capacity_words_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_words_, data);
                    } else {
                        std::fill_n(data, capacity_words_, static_cast<uint32_t>(0));
                    }
                } else {
                    capacity_words_ = 0;
                }
            }
            bpv_ = other.bpv_;
        }

        PackedArray& operator=(const PackedArray& other) {
            if (this != &other) {
                copy_from(other, other.capacity_words_);
            }
            return *this;
        }

        PackedArray& operator=(PackedArray&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(data);
                data = other.data;
                bpv_ = other.bpv_;
                capacity_words_ = other.capacity_words_;
                other.data = nullptr;
                other.capacity_words_ = 0;
            }
            return *this;
        }

        uint8_t get_bpv() const { return bpv_; }

        void set_bpv(uint8_t new_bpv) {
            if (new_bpv > 0 && new_bpv <= 32) {
                bpv_ = new_bpv;
            }
        }

        __attribute__((always_inline)) inline void set_unsafe(size_t index, uint32_t value) {
            if (!data) {
                return;
            }

            const uint8_t active_bpv = bpv_;
            const uint32_t mask = (active_bpv >= 32)
                ? static_cast<uint32_t>(std::numeric_limits<uint32_t>::max())
                : ((uint32_t{1} << active_bpv) - 1ull);
            const uint32_t clamped = static_cast<uint32_t>(value) & mask;

            const size_t bitPos = index * active_bpv;
            const size_t wordIdx = bitPos >> 5;
            if (wordIdx >= capacity_words_) {
                return;
            }

            const size_t bitOff = bitPos & 31;
            uint32_t* word = data + wordIdx;
            const size_t firstBits = std::min<size_t>(32 - bitOff, active_bpv);
            const uint32_t firstMask = (firstBits == 32)
                ? std::numeric_limits<uint32_t>::max()
                : ((uint32_t{1} << firstBits) - 1u);

            *word = (*word & ~(firstMask << bitOff)) | ((clamped & firstMask) << bitOff);

            if (firstBits < active_bpv) {
                const size_t secondBits = active_bpv - firstBits;
                if ((wordIdx + 1) >= capacity_words_) {
                    return;
                }
                uint32_t* nextWord = data + wordIdx + 1;
                const uint32_t secondMask = (secondBits == 32)
                    ? std::numeric_limits<uint32_t>::max()
                    : ((uint32_t{1} << secondBits) - 1u);
                const uint32_t secondPart = clamped >> firstBits;
                *nextWord = (*nextWord & ~secondMask) | (secondPart & secondMask);
            }
        }

        __attribute__((always_inline)) inline uint32_t get_unsafe(size_t index) const {
            if (!data) {
                return 0;
            }

            const uint8_t active_bpv = bpv_;
            const size_t bitPos = index * active_bpv;
            const size_t wordIdx = bitPos >> 5;
            if (wordIdx >= capacity_words_) {
                return 0;
            }

            const size_t bitOff = bitPos & 31;
            const uint32_t firstWord = data[wordIdx];
            const size_t firstBits = std::min<size_t>(32 - bitOff, active_bpv);
            const uint32_t firstMask = (firstBits == 32)
                ? std::numeric_limits<uint32_t>::max()
                : ((uint32_t{1} << firstBits) - 1u);
            uint32_t value = (firstWord >> bitOff) & firstMask;

            if (firstBits < active_bpv) {
                if ((wordIdx + 1) >= capacity_words_) {
                    return static_cast<uint32_t>(value);
                }
                const size_t secondBits = active_bpv - firstBits;
                const uint32_t secondWord = data[wordIdx + 1];
                const uint32_t secondMask = (secondBits == 32)
                    ? std::numeric_limits<uint32_t>::max()
                    : ((uint32_t{1} << secondBits) - 1u);
                value |= (secondWord & secondMask) << firstBits;
            }

            return static_cast<uint32_t>(value);
        }

        void copy_elements(const PackedArray& src, size_t element_count) {
            if (!data || !src.data) {
                return;
            }

            for (size_t i = 0; i < element_count; ++i) {
                set_unsafe(i, src.get_unsafe(i));
            }

            const size_t bits_used = element_count * bpv_;
            const size_t first_unused_word = (bits_used + 31) >> 5;
            for (size_t i = first_unused_word; i < capacity_words_; ++i) {
                data[i] = 0;
            }
        }

        void set(size_t index, uint32_t value) { set_unsafe(index, value); }
        uint32_t get(size_t index) const { return get_unsafe(index); }

        uint32_t* raw_data() { return data; }
        const uint32_t* raw_data() const { return data; }
        size_t words() const { return capacity_words_; }
    };


    template<uint8_t BitsPerElement>
    class packed_vector {
        static_assert(BitsPerElement > 0 && BitsPerElement <= 32, "Invalid bit size");

    public:
        using value_type = uint32_t;
        using size_type = size_t;

    private:
        PackedArray<BitsPerElement> packed_data;
        size_type size_ = 0;
        size_type capacity_ = 0;

        static constexpr size_type VECTOR_MAX_CAP =
            (std::numeric_limits<size_type>::max() / 2) > 0
                ? (std::numeric_limits<size_type>::max() / 2)
                : static_cast<size_type>(1);

        static constexpr value_type COMPILED_MAX =
            (BitsPerElement >= 32)
                ? std::numeric_limits<value_type>::max()
                : static_cast<value_type>((uint32_t{1} << BitsPerElement) - 1ull);

        static inline size_t calc_words_for_bpv(size_type capacity, uint8_t bpv) {
            const size_t bits = capacity * static_cast<size_t>(bpv);
            return (bits + 31u) >> 5;
        }

        static inline value_type runtime_max_value(uint8_t bpv) {
            if (bpv >= 32) {
                return std::numeric_limits<value_type>::max();
            }
            return static_cast<value_type>((uint32_t{1} << bpv) - 1ull);
        }

        value_type clamp_value(uint32_t value) const {
            const uint8_t bpv = packed_data.get_bpv();
            if (bpv >= 32) {
                return static_cast<value_type>(value & std::numeric_limits<value_type>::max());
            }
            const uint32_t mask = (uint32_t{1} << bpv) - 1ull;
            return static_cast<value_type>(value & mask);
        }

        template<typename T>
        struct init_view {
            const T* data;
            size_t count;
        };

        template<typename T>
        static init_view<T> normalize_init_list(mcu::min_init_list<T> init, uint8_t active_bpv) {
            init_view<T> view{init.begin(), static_cast<size_t>(init.size())};
            if (!view.data || view.count == 0) {
                view.count = 0;
                return view;
            }

            bool drop_header = false;
            if (static_cast<uint32_t>(view.data[0]) == active_bpv && view.count > 1) {
                for (size_t i = 1; i < view.count; ++i) {
                    if (static_cast<uint32_t>(view.data[i]) > active_bpv) {
                        drop_header = true;
                        break;
                    }
                }
            }

            if (drop_header) {
                ++view.data;
                --view.count;
            }

            if (view.count > VECTOR_MAX_CAP) {
                view.count = VECTOR_MAX_CAP;
            }

            return view;
        }

        template<typename SourceVector>
        void initialize_from_range(const SourceVector& source, size_t start_index, size_t end_index) {
            uint8_t source_bpv = source.get_bits_per_value();
            uint8_t active_bpv = (source_bpv == 0) ? BitsPerElement : source_bpv;
            if (active_bpv > BitsPerElement) {
                active_bpv = BitsPerElement;
            }

            const size_t source_size = source.size();
            if (start_index > end_index || start_index >= source_size) {
                capacity_ = 1;
                size_ = 0;
                packed_data = PackedArray<BitsPerElement>(calc_words_for_bpv(1, active_bpv));
                packed_data.set_bpv(active_bpv);
                return;
            }

            if (end_index > source_size) {
                end_index = source_size;
            }

            size_ = static_cast<size_type>(end_index - start_index);
            capacity_ = (size_ == 0) ? 1 : size_;

            packed_data = PackedArray<BitsPerElement>(calc_words_for_bpv(capacity_, active_bpv));
            packed_data.set_bpv(active_bpv);

            for (size_type i = 0; i < size_; ++i) {
                const auto value = static_cast<uint32_t>(source[start_index + i]);
                packed_data.set_unsafe(i, clamp_value(value));
            }
        }

        void ensure_capacity(size_type new_capacity) {
            if (new_capacity <= capacity_) {
                return;
            }

            if (new_capacity > VECTOR_MAX_CAP) {
                new_capacity = VECTOR_MAX_CAP;
            }

            const uint8_t active_bpv = packed_data.get_bpv();
            size_type adjusted = (new_capacity == 0) ? 1 : new_capacity;
            size_t words = calc_words_for_bpv(adjusted, active_bpv);
            if (words == 0) {
                words = 1;
            }

            PackedArray<BitsPerElement> new_data(words);
            new_data.set_bpv(active_bpv);
            new_data.copy_elements(packed_data, size_);
            packed_data = std::move(new_data);
            capacity_ = adjusted;
        }

        void init(uint8_t bpv) {
            if (bpv == 0 || bpv > 32) {
                return;
            }

            size_type target_capacity = (capacity_ == 0) ? 1 : capacity_;
            PackedArray<BitsPerElement> new_data(calc_words_for_bpv(target_capacity, bpv));
            new_data.set_bpv(bpv);
            packed_data = std::move(new_data);
            size_ = 0;
            capacity_ = target_capacity;
        }

    public:
        packed_vector()
            : packed_data(calc_words_for_bpv(1, BitsPerElement)), size_(0), capacity_(1) {}

        explicit packed_vector(size_type initialCapacity)
            : packed_data(calc_words_for_bpv((initialCapacity == 0) ? 1 : initialCapacity, BitsPerElement)),
              size_(0),
              capacity_((initialCapacity == 0) ? 1 : initialCapacity) {}

        packed_vector(size_type initialSize, value_type value)
            : packed_data(calc_words_for_bpv((initialSize == 0) ? 1 : initialSize, BitsPerElement)),
              size_(initialSize),
              capacity_((initialSize == 0) ? 1 : initialSize) {
            const value_type clamped = clamp_value(value);
            for (size_type i = 0; i < size_; ++i) {
                packed_data.set_unsafe(i, clamped);
            }
        }

        template<typename T>
        packed_vector(mcu::min_init_list<T> init)
            : packed_vector() {
            assign(init);
        }

        packed_vector(const packed_vector& other)
            : packed_data(other.packed_data, std::max<size_t>(size_t{1}, calc_words_for_bpv(other.capacity_, other.get_bits_per_value()))),
              size_(other.size_),
              capacity_(other.capacity_) {
            packed_data.set_bpv(other.get_bits_per_value());
        }

        packed_vector(packed_vector&& other) noexcept
            : packed_data(std::move(other.packed_data)),
              size_(other.size_),
              capacity_(other.capacity_) {
            other.size_ = 0;
            other.capacity_ = 0;
        }

        packed_vector& operator=(const packed_vector& other) {
            if (this != &other) {
                packed_data.copy_from(other.packed_data, std::max<size_t>(size_t{1}, calc_words_for_bpv(other.capacity_, other.get_bits_per_value())));
                packed_data.set_bpv(other.get_bits_per_value());
                size_ = other.size_;
                capacity_ = other.capacity_;
            }
            return *this;
        }

        packed_vector& operator=(packed_vector&& other) noexcept {
            if (this != &other) {
                packed_data = std::move(other.packed_data);
                size_ = other.size_;
                capacity_ = other.capacity_;
                other.size_ = 0;
                other.capacity_ = 0;
            }
            return *this;
        }

        packed_vector(const packed_vector& source, size_t start_index, size_t end_index) {
            initialize_from_range(source, start_index, end_index);
        }

        template<uint8_t SourceBitsPerElement>
        packed_vector(const packed_vector<SourceBitsPerElement>& source, size_t start_index, size_t end_index) {
            initialize_from_range(source, start_index, end_index);
        }

        size_type size() const { return size_; }
        size_type capacity() const { return capacity_; }
        bool empty() const { return size_ == 0; }

        value_type operator[](size_type index) const {
            if (size_ == 0) {
                return 0;
            }
            if (index >= size_) {
                return packed_data.get_unsafe(size_ - 1);
            }
            return packed_data.get_unsafe(index);
        }

        value_type at(size_type index) const {
            if (index >= size_) {
                throw std::out_of_range("packed_vector::at");
            }
            return packed_data.get_unsafe(index);
        }

        void set(size_type index, value_type value) {
            packed_data.set_unsafe(index, clamp_value(value));
        }

        void set_unsafe(size_type index, value_type value) {
            packed_data.set_unsafe(index, clamp_value(value));
        }

        value_type get(size_type index) const {
            return (index < size_) ? packed_data.get_unsafe(index) : 0;
        }

        value_type front() const {
            if (size_ == 0) {
                throw std::out_of_range("packed_vector::front");
            }
            return packed_data.get_unsafe(0);
        }

        value_type back() const {
            return (size_ > 0) ? packed_data.get_unsafe(size_ - 1) : 0;
        }

        void push_back(value_type value) {
            if (size_ == capacity_) {
                size_type new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;
                if (new_capacity > VECTOR_MAX_CAP) {
                    new_capacity = VECTOR_MAX_CAP;
                }
                ensure_capacity(new_capacity);
            }
            if (size_ < capacity_) {
                packed_data.set_unsafe(size_, clamp_value(value));
                ++size_;
            }
        }

        void pop_back() {
            if (size_ > 0) {
                --size_;
            }
        }

        void fill(value_type value) {
            if (size_ == 0) {
                return;
            }
            const value_type clamped = clamp_value(value);
            for (size_type i = 0; i < size_; ++i) {
                packed_data.set_unsafe(i, clamped);
            }
        }

        void resize(size_type newSize, value_type value = 0) {
            if (newSize > capacity_) {
                ensure_capacity(newSize);
            }
            if (newSize > size_) {
                const value_type clamped = clamp_value(value);
                for (size_type i = size_; i < newSize; ++i) {
                    packed_data.set_unsafe(i, clamped);
                }
            }
            size_ = newSize;
        }

        void reserve(size_type newCapacity) {
            ensure_capacity(newCapacity);
        }

        void assign(size_type count, value_type value) {
            clear();
            if (count == 0) {
                return;
            }
            ensure_capacity(count);
            const value_type clamped = clamp_value(value);
            for (size_type i = 0; i < count; ++i) {
                packed_data.set_unsafe(i, clamped);
            }
            size_ = count;
        }

        template<typename T>
        void assign(mcu::min_init_list<T> init) {
            auto view = normalize_init_list(init, packed_data.get_bpv());
            clear();
            if (view.count == 0) {
                return;
            }
            ensure_capacity(view.count);
            for (size_type i = 0; i < view.count; ++i) {
                packed_data.set_unsafe(i, clamp_value(static_cast<uint32_t>(view.data[i])));
            }
            size_ = view.count;
        }

        void clear() { size_ = 0; }

        static constexpr value_type max_value() { return COMPILED_MAX; }
        static constexpr uint8_t bits_per_element() { return BitsPerElement; }

        uint8_t get_bits_per_value() const { return packed_data.get_bpv(); }

        void set_bits_per_value(uint8_t bpv) {
            if (bpv == packed_data.get_bpv()) {
                return;
            }
            init(bpv);
        }

        void fit() {
            if (size_ < capacity_) {
                size_type target = (size_ == 0) ? 1 : size_;
                const uint8_t active_bpv = packed_data.get_bpv();
                PackedArray<BitsPerElement> new_data(calc_words_for_bpv(target, active_bpv));
                new_data.set_bpv(active_bpv);
                new_data.copy_elements(packed_data, size_);
                packed_data = std::move(new_data);
                capacity_ = target;
            }
        }

        size_t memory_usage() const {
            const size_t words = calc_words_for_bpv(capacity_, packed_data.get_bpv());
            return words * sizeof(uint32_t);
        }

        bool operator==(const packed_vector& other) const {
            if (size_ != other.size_) {
                return false;
            }
            for (size_type i = 0; i < size_; ++i) {
                if (packed_data.get_unsafe(i) != other.packed_data.get_unsafe(i)) {
                    return false;
                }
            }
            return true;
        }

        bool operator!=(const packed_vector& other) const { return !(*this == other); }

        class iterator {
        private:
            PackedArray<BitsPerElement>* data_ptr;
            size_type index;

        public:
            friend class const_iterator;

            iterator(PackedArray<BitsPerElement>* ptr, size_type idx)
                : data_ptr(ptr), index(idx) {}

            uint32_t operator*() const { return data_ptr->get_unsafe(index); }

            iterator& operator++() { ++index; return *this; }
            iterator operator++(int) { iterator tmp = *this; ++index; return tmp; }
            iterator& operator--() { --index; return *this; }
            iterator operator--(int) { iterator tmp = *this; --index; return tmp; }

            iterator operator+(size_type n) const { return iterator(data_ptr, index + n); }
            iterator operator-(size_type n) const { return iterator(data_ptr, index - n); }
            iterator& operator+=(size_type n) { index += n; return *this; }
            iterator& operator-=(size_type n) { index -= n; return *this; }

            bool operator==(const iterator& other) const { return index == other.index; }
            bool operator!=(const iterator& other) const { return index != other.index; }
            bool operator<(const iterator& other) const { return index < other.index; }
            bool operator>(const iterator& other) const { return index > other.index; }
            bool operator<=(const iterator& other) const { return index <= other.index; }
            bool operator>=(const iterator& other) const { return index >= other.index; }

            std::ptrdiff_t operator-(const iterator& other) const {
                return static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(other.index);
            }

            size_type get_index() const { return index; }
        };

        class const_iterator {
        private:
            const PackedArray<BitsPerElement>* data_ptr;
            size_type index;

        public:
            const_iterator(const PackedArray<BitsPerElement>* ptr, size_type idx)
                : data_ptr(ptr), index(idx) {}

            const_iterator(const iterator& it)
                : data_ptr(it.data_ptr), index(it.get_index()) {}

            uint32_t operator*() const { return data_ptr->get_unsafe(index); }

            const_iterator& operator++() { ++index; return *this; }
            const_iterator operator++(int) { const_iterator tmp = *this; ++index; return tmp; }
            const_iterator& operator--() { --index; return *this; }
            const_iterator operator--(int) { const_iterator tmp = *this; --index; return tmp; }

            const_iterator operator+(size_type n) const { return const_iterator(data_ptr, index + n); }
            const_iterator operator-(size_type n) const { return const_iterator(data_ptr, index - n); }
            const_iterator& operator+=(size_type n) { index += n; return *this; }
            const_iterator& operator-=(size_type n) { index -= n; return *this; }

            bool operator==(const const_iterator& other) const { return index == other.index; }
            bool operator!=(const const_iterator& other) const { return index != other.index; }
            bool operator<(const const_iterator& other) const { return index < other.index; }
            bool operator>(const const_iterator& other) const { return index > other.index; }
            bool operator<=(const const_iterator& other) const { return index <= other.index; }
            bool operator>=(const const_iterator& other) const { return index >= other.index; }

            std::ptrdiff_t operator-(const const_iterator& other) const {
                return static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(other.index);
            }

            size_type get_index() const { return index; }
        };

        iterator begin() { return iterator(&packed_data, 0); }
        iterator end() { return iterator(&packed_data, size_); }
        const_iterator begin() const { return const_iterator(&packed_data, 0); }
        const_iterator end() const { return const_iterator(&packed_data, size_); }
        const_iterator cbegin() const { return const_iterator(&packed_data, 0); }
        const_iterator cend() const { return const_iterator(&packed_data, size_); }

        const uint32_t* data() const { return packed_data.raw_data(); }
        uint32_t* data() { return packed_data.raw_data(); }
    };

} // namespace mcu
