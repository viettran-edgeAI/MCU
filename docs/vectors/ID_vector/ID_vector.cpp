
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <limits>
#include <cstring>
#include "../b_vector_and_vector/b_vector.cpp"
#include "../b_vector_and_vector/vector.cpp"
#include "../../../src/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>

using namespace mcu;

    template<typename>
    struct dependent_false : std::false_type {};

    template<typename T, typename Enable = void>
    struct packed_value_traits {
        static size_t to_bits(const T&) {
            static_assert(dependent_false<T>::value, "packed_value_traits specialization required for this type");
            return 0;
        }

        static T from_bits(size_t) {
            static_assert(dependent_false<T>::value, "packed_value_traits specialization required for this type");
            return T{};
        }
    };

    template<typename T>
    struct packed_value_traits<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
        static constexpr size_t to_bits(T value) noexcept {
            return static_cast<size_t>(value);
        }

        static constexpr T from_bits(size_t bits) noexcept {
            return static_cast<T>(bits);
        }
    };

    template<typename T>
    struct packed_value_traits<T, std::enable_if_t<!std::is_integral_v<T> && !std::is_enum_v<T> &&
                                                  std::is_trivially_copyable_v<T> &&
                                                  (sizeof(T) <= sizeof(size_t))>> {
        static size_t to_bits(const T& value) noexcept {
            size_t bits = 0;
            std::memcpy(&bits, &value, sizeof(T));
            return bits;
        }

        static T from_bits(size_t bits) noexcept {
            T value{};
            std::memcpy(&value, &bits, sizeof(T));
            return value;
        }
    };

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
        static_assert(BitsPerElement > 0, "Invalid bit size");

    public:
        using word_t = size_t;
        static constexpr size_t WORD_BITS = sizeof(word_t) * 8;
    private:
        word_t* data = nullptr;
        uint8_t bpv_ = BitsPerElement;
        size_t capacity_words_ = 0;

    public:
        PackedArray() = default;

        explicit PackedArray(size_t capacity_words)
            : bpv_(BitsPerElement), capacity_words_(capacity_words) {
            if (capacity_words_ > 0) {
                data = mem_alloc::allocate<word_t>(capacity_words_);
                if (data) {
                    std::fill_n(data, capacity_words_, static_cast<word_t>(0));
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
                data = mem_alloc::allocate<word_t>(capacity_words_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_words_, data);
                    } else {
                        std::fill_n(data, capacity_words_, static_cast<word_t>(0));
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
                data = mem_alloc::allocate<word_t>(capacity_words_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_words_, data);
                    } else {
                        std::fill_n(data, capacity_words_, static_cast<word_t>(0));
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
            if (new_bpv > 0) {
                bpv_ = new_bpv;
            }
        }

    __attribute__((always_inline)) inline void set_unsafe(size_t index, size_t value) {
            if (!data) {
                return;
            }

            const uint8_t active_bpv = bpv_;
            const size_t mask = (active_bpv >= WORD_BITS)
                ? static_cast<size_t>(std::numeric_limits<size_t>::max())
                : ((static_cast<size_t>(1) << active_bpv) - 1ull);
            const size_t clamped = static_cast<size_t>(value) & mask;

            const size_t bitPos = index * static_cast<size_t>(active_bpv);
            const size_t wordIdx = bitPos / WORD_BITS;
            if (wordIdx >= capacity_words_) {
                return;
            }

            size_t bitOff = bitPos % WORD_BITS;
            size_t remaining = active_bpv;
            size_t srcShift = 0;
            size_t wIndex = wordIdx;

            while (remaining > 0) {
                if (wIndex >= capacity_words_) return;
                size_t bitsInWord = std::min<size_t>(WORD_BITS - bitOff, remaining);
                const size_t maskPart = (bitsInWord == WORD_BITS) ? std::numeric_limits<size_t>::max() : ((static_cast<size_t>(1) << bitsInWord) - 1u);
                word_t& w = data[wIndex];
                w = (w & ~(maskPart << bitOff)) | (((clamped >> srcShift) & maskPart) << bitOff);
                remaining -= bitsInWord;
                srcShift += bitsInWord;
                bitOff = 0;
                ++wIndex;
            }
        }

    __attribute__((always_inline)) inline size_t get_unsafe(size_t index) const {
            if (!data) {
                return 0;
            }

            const uint8_t active_bpv = bpv_;
            const size_t bitPos = index * static_cast<size_t>(active_bpv);
            const size_t wordIdx = bitPos / WORD_BITS;
            if (wordIdx >= capacity_words_) {
                return 0;
            }

            size_t bitOff = bitPos % WORD_BITS;
            size_t remaining = active_bpv;
            size_t dstShift = 0;
            size_t value = 0;
            size_t wIdx = wordIdx;
            while (remaining > 0) {
                if (wIdx >= capacity_words_) return 0;
                size_t bitsInWord = std::min<size_t>(WORD_BITS - bitOff, remaining);
                const size_t maskPart = (bitsInWord == WORD_BITS) ? std::numeric_limits<size_t>::max() : ((static_cast<size_t>(1) << bitsInWord) - 1u);
                const size_t w = data[wIdx];
                size_t part = (w >> bitOff) & maskPart;
                value |= (part << dstShift);
                remaining -= bitsInWord;
                dstShift += bitsInWord;
                bitOff = 0;
                ++wIdx;
            }
            return value;
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

    void set(size_t index, size_t value) { set_unsafe(index, value); }
    size_t get(size_t index) const { return get_unsafe(index); }

    word_t* raw_data() { return data; }
    const word_t* raw_data() const { return data; }
        size_t words() const { return capacity_words_; }
    };


    template<uint8_t BitsPerElement, typename ValueType = size_t>
    class packed_vector {
    static_assert(BitsPerElement > 0, "Invalid bit size");

    public:
        using value_type = ValueType;
        using size_type = size_t;
        using traits_type = packed_value_traits<value_type>;
        using word_t = typename PackedArray<BitsPerElement>::word_t;

    private:
        PackedArray<BitsPerElement> packed_data;
        size_type size_ = 0;
        size_type capacity_ = 0;

        static constexpr size_type VECTOR_MAX_CAP =
            (std::numeric_limits<size_type>::max() / 2) > 0
                ? (std::numeric_limits<size_type>::max() / 2)
                : static_cast<size_type>(1);

        static constexpr size_t COMPILED_MAX_BITS =
            (BitsPerElement >= (int)PackedArray<BitsPerElement>::WORD_BITS)
                ? std::numeric_limits<size_t>::max()
                : ((static_cast<size_t>(1) << BitsPerElement) - 1u);

        static inline size_t calc_words_for_bpv(size_type capacity, uint8_t bpv) {
            const size_t bits = capacity * static_cast<size_t>(bpv);
            const size_t word_bits = PackedArray<BitsPerElement>::WORD_BITS;
            return (bits + word_bits - 1u) / word_bits;
        }

        static inline size_t runtime_mask(uint8_t bpv) {
            if (bpv >= PackedArray<BitsPerElement>::WORD_BITS) {
                return std::numeric_limits<size_t>::max();
            }
            return (static_cast<size_t>(1) << bpv) - 1u;
        }

        __attribute__((always_inline)) inline size_t mask_bits(size_t bits, uint8_t bpv) const {
            return bits & runtime_mask(bpv);
        }

        __attribute__((always_inline)) inline size_t mask_bits(size_t bits) const {
            return mask_bits(bits, packed_data.get_bpv());
        }

        __attribute__((always_inline)) inline size_t to_storage_bits(const value_type& value, uint8_t bpv) const {
            return mask_bits(traits_type::to_bits(value), bpv);
        }

        __attribute__((always_inline)) inline size_t to_storage_bits(const value_type& value) const {
            return to_storage_bits(value, packed_data.get_bpv());
        }

        __attribute__((always_inline)) inline value_type from_storage_bits(size_t bits, uint8_t bpv) const {
            return traits_type::from_bits(mask_bits(bits, bpv));
        }

        __attribute__((always_inline)) inline value_type from_storage_bits(size_t bits) const {
            return from_storage_bits(bits, packed_data.get_bpv());
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
            if (packed_value_traits<T>::to_bits(view.data[0]) == static_cast<size_t>(active_bpv) && view.count > 1) {
                for (size_t i = 1; i < view.count; ++i) {
                    if (packed_value_traits<T>::to_bits(view.data[i]) > static_cast<size_t>(active_bpv)) {
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

            // Optimized bulk copy for word-aligned ranges when bpv matches
            if (active_bpv == source_bpv && (start_index * active_bpv) % PackedArray<BitsPerElement>::WORD_BITS == 0) {
                // Fast path: word-aligned bulk copy
                const size_t start_bit = start_index * active_bpv;
                const size_t end_bit = end_index * active_bpv;
                const size_t start_word = start_bit / PackedArray<BitsPerElement>::WORD_BITS;
                const size_t num_bits = size_ * active_bpv;
                const size_t num_words = (num_bits + PackedArray<BitsPerElement>::WORD_BITS - 1) / PackedArray<BitsPerElement>::WORD_BITS;
                
                // Direct word copy from source packed data
                const word_t* src_words = source.get_packed_data_words();
                word_t* dst_words = packed_data.raw_data();
                
                if (src_words && dst_words && num_words > 0) {
                    // Use memcpy for bulk transfer
                    memcpy(dst_words, src_words + start_word, num_words * sizeof(word_t));
                    return;
                }
            }
            
            // Fallback: element-by-element copy
            for (size_type i = 0; i < size_; ++i) {
                using SourceValue = typename SourceVector::value_type;
                using SourceTraits = packed_value_traits<SourceValue>;
                const size_t source_bits = SourceTraits::to_bits(source[start_index + i]);
                const value_type converted = traits_type::from_bits(source_bits);
                packed_data.set_unsafe(i, mask_bits(traits_type::to_bits(converted), active_bpv));
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
            if (bpv == 0) {
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

        packed_vector(size_type initialSize, const value_type& value)
            : packed_data(calc_words_for_bpv((initialSize == 0) ? 1 : initialSize, BitsPerElement)),
              size_(initialSize),
              capacity_((initialSize == 0) ? 1 : initialSize) {
            const uint8_t active_bpv = packed_data.get_bpv();
            const size_t clamped = to_storage_bits(value, active_bpv);
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

    template<uint8_t SourceBitsPerElement, typename SourceValue>
    packed_vector(const packed_vector<SourceBitsPerElement, SourceValue>& source, size_t start_index, size_t end_index) {
            initialize_from_range(source, start_index, end_index);
        }

        size_type size() const { return size_; }
        size_type capacity() const { return capacity_; }
        bool empty() const { return size_ == 0; }

        value_type operator[](size_type index) const {
            if (size_ == 0) {
                return value_type{};
            }
            if (index >= size_) {
                return from_storage_bits(packed_data.get_unsafe(size_ - 1));
            }
            return from_storage_bits(packed_data.get_unsafe(index));
        }

        value_type at(size_type index) const {
            if (index >= size_) {
                throw std::out_of_range("packed_vector::at");
            }
            return from_storage_bits(packed_data.get_unsafe(index));
        }

        void set(size_type index, const value_type& value) {
            packed_data.set_unsafe(index, to_storage_bits(value));
        }

        void set_unsafe(size_type index, const value_type& value) {
            packed_data.set_unsafe(index, to_storage_bits(value));
        }

        value_type get(size_type index) const {
            return (index < size_) ? from_storage_bits(packed_data.get_unsafe(index)) : value_type{};
        }

        value_type front() const {
            if (size_ == 0) {
                throw std::out_of_range("packed_vector::front");
            }
            return from_storage_bits(packed_data.get_unsafe(0));
        }

        value_type back() const {
            return (size_ > 0) ? from_storage_bits(packed_data.get_unsafe(size_ - 1)) : value_type{};
        }

        void push_back(const value_type& value) {
            if (size_ == capacity_) {
                size_type new_capacity = (capacity_ == 0) ? 1 : capacity_ * 2;
                if (new_capacity > VECTOR_MAX_CAP) {
                    new_capacity = VECTOR_MAX_CAP;
                }
                ensure_capacity(new_capacity);
            }
            if (size_ < capacity_) {
                packed_data.set_unsafe(size_, to_storage_bits(value));
                ++size_;
            }
        }

        void pop_back() {
            if (size_ > 0) {
                --size_;
            }
        }

        void fill(const value_type& value) {
            if (size_ == 0) {
                return;
            }
            const uint8_t active_bpv = packed_data.get_bpv();
                const size_t clamped = to_storage_bits(value, active_bpv);
            for (size_type i = 0; i < size_; ++i) {
                packed_data.set_unsafe(i, clamped);
            }
        }

        void resize(size_type newSize, const value_type& value = value_type{}) {
            if (newSize > capacity_) {
                ensure_capacity(newSize);
            }
            if (newSize > size_) {
                const uint8_t active_bpv = packed_data.get_bpv();
                const size_t clamped = to_storage_bits(value, active_bpv);
                for (size_type i = size_; i < newSize; ++i) {
                    packed_data.set_unsafe(i, clamped);
                }
            }
            size_ = newSize;
        }

        void reserve(size_type newCapacity) {
            ensure_capacity(newCapacity);
        }

        void assign(size_type count, const value_type& value) {
            clear();
            if (count == 0) {
                return;
            }
            ensure_capacity(count);
            const uint8_t active_bpv = packed_data.get_bpv();
            const size_t clamped = to_storage_bits(value, active_bpv);
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
            const uint8_t active_bpv = packed_data.get_bpv();
            for (size_type i = 0; i < view.count; ++i) {
                const size_t bits = packed_value_traits<T>::to_bits(view.data[i]);
                const value_type converted = traits_type::from_bits(bits);
                packed_data.set_unsafe(i, mask_bits(traits_type::to_bits(converted), active_bpv));
            }
            size_ = view.count;
        }

        void clear() { size_ = 0; }

        static value_type max_value() { return packed_value_traits<value_type>::from_bits(COMPILED_MAX_BITS); }
        static constexpr uint8_t bits_per_element() { return BitsPerElement; }
    static constexpr size_t max_bits_value() { return COMPILED_MAX_BITS; }

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
            return words * sizeof(word_t);
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
            packed_vector* parent = nullptr;
            size_type index = 0;

        public:
            friend class const_iterator;

            iterator() = default;

            iterator(packed_vector* p, size_type idx)
                : parent(p), index(idx) {}

            value_type operator*() const { return parent->from_storage_bits(parent->packed_data.get_unsafe(index)); }

            iterator& operator++() { ++index; return *this; }
            iterator operator++(int) { iterator tmp = *this; ++index; return tmp; }
            iterator& operator--() { --index; return *this; }
            iterator operator--(int) { iterator tmp = *this; --index; return tmp; }

            iterator operator+(size_type n) const { return iterator(parent, index + n); }
            iterator operator-(size_type n) const { return iterator(parent, index - n); }
            iterator& operator+=(size_type n) { index += n; return *this; }
            iterator& operator-=(size_type n) { index -= n; return *this; }

            bool operator==(const iterator& other) const { return index == other.index && parent == other.parent; }
            bool operator!=(const iterator& other) const { return !(*this == other); }
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
            const packed_vector* parent = nullptr;
            size_type index = 0;

        public:
            const_iterator() = default;

            const_iterator(const packed_vector* p, size_type idx)
                : parent(p), index(idx) {}

            const_iterator(const iterator& it)
                : parent(it.parent), index(it.index) {}

            value_type operator*() const { return parent->from_storage_bits(parent->packed_data.get_unsafe(index)); }

            const_iterator& operator++() { ++index; return *this; }
            const_iterator operator++(int) { const_iterator tmp = *this; ++index; return tmp; }
            const_iterator& operator--() { --index; return *this; }
            const_iterator operator--(int) { const_iterator tmp = *this; --index; return tmp; }

            const_iterator operator+(size_type n) const { return const_iterator(parent, index + n); }
            const_iterator operator-(size_type n) const { return const_iterator(parent, index - n); }
            const_iterator& operator+=(size_type n) { index += n; return *this; }
            const_iterator& operator-=(size_type n) { index -= n; return *this; }

            bool operator==(const const_iterator& other) const { return index == other.index && parent == other.parent; }
            bool operator!=(const const_iterator& other) const { return !(*this == other); }
            bool operator<(const const_iterator& other) const { return index < other.index; }
            bool operator>(const const_iterator& other) const { return index > other.index; }
            bool operator<=(const const_iterator& other) const { return index <= other.index; }
            bool operator>=(const const_iterator& other) const { return index >= other.index; }

            std::ptrdiff_t operator-(const const_iterator& other) const {
                return static_cast<std::ptrdiff_t>(index) - static_cast<std::ptrdiff_t>(other.index);
            }

            size_type get_index() const { return index; }
        };

        iterator begin() { return iterator(this, 0); }
        iterator end() { return iterator(this, size_); }
        const_iterator begin() const { return const_iterator(this, 0); }
        const_iterator end() const { return const_iterator(this, size_); }
        const_iterator cbegin() const { return const_iterator(this, 0); }
        const_iterator cend() const { return const_iterator(this, size_); }

    const word_t* data() const { return packed_data.raw_data(); }
    word_t* data() { return packed_data.raw_data(); }
        
        // Helper methods for optimized range copying
    const word_t* get_packed_data_words() const { return packed_data.raw_data(); }
    };
    
    template <typename T,  uint8_t BitsPerValue = 1>
    class ID_vector{
        static_assert(BitsPerValue > 0 && BitsPerValue <= 32, "BitsPerValue must be between 1 and 32");
    public:
        using count_type = uint32_t; // type for storing count of each ID
        
        // Index type mapping based on T
        using index_type = typename conditional_t<
            is_same_t<T, uint8_t>::value, uint8_t,
            typename conditional_t<
                is_same_t<T, uint16_t>::value, uint16_t,
                typename conditional_t<
                    is_same_t<T, uint32_t>::value, size_t,
                    typename conditional_t<
                        is_same_t<T, size_t>::value, size_t,
                        size_t  // Default to size_t if T is not recognized
                    >::type
                >::type
            >::type
        >::type;
        
        // Size type that can handle total count considering BitsPerValue
        // When BitsPerValue > 1, total size can exceed index_type capacity
        using size_type = typename conditional_t<
            (sizeof(index_type) <= 1), uint32_t,
            typename conditional_t<
                (sizeof(index_type) == 2), uint64_t,
                size_t
            >::type
        >::type;
        
    private:
        PackedArray<BitsPerValue> id_array; // BitsPerValue bits per ID
        index_type max_id_ = 0; // maximum ID that can be stored
        index_type min_id_ = 0; // minimum ID that can be stored
        size_type size_ = 0; // total number of ID instances stored

        // MAX_RF_ID based on index_type capacity
        constexpr static index_type MAX_RF_ID = 
            is_same_t<index_type, uint8_t>::value ? 255 :
            is_same_t<index_type, uint16_t>::value ? 65535 :
            2147483647; // max for size_t (assuming 32-bit signed)
            
        constexpr static index_type DEFAULT_MAX_ID = 
            is_same_t<index_type, uint8_t>::value ? 63 :
            is_same_t<index_type, uint16_t>::value ? 255 :
            127; // default max ID
            
    constexpr static count_type MAX_COUNT = (static_cast<count_type>(1ULL) << BitsPerValue) - 1; // maximum count per ID

    static constexpr size_t bits_to_words(size_t bits){ const size_t word_bits = PackedArray<BitsPerValue>::WORD_BITS; return (bits + word_bits - 1) / word_bits; }

        void allocate_bits(){
            const size_t range = static_cast<size_t>(max_id_) - static_cast<size_t>(min_id_) + 1; // number of IDs in range
            const size_t total_bits = range * BitsPerValue; // multiply by bits per value
            const size_t words = bits_to_words(total_bits);
            id_array = PackedArray<BitsPerValue>(words);
        }

        // Convert external ID to internal array index
        index_type inline id_to_index(index_type id) const {
            return id - min_id_;
        }

        // Convert internal array index to external ID
        index_type inline index_to_id(index_type index) const {
            return index + min_id_;
        }

    public:
        // Set maximum ID that can be stored and allocate memory accordingly
        void set_maxID(index_type new_max_id) {
            if(new_max_id > MAX_RF_ID){
                throw std::out_of_range("Max RF ID exceeds limit");
            }
            if(new_max_id < min_id_){
                throw std::out_of_range("Max ID cannot be less than min ID");
            }
            
            // If vector is empty, just update max_id and allocate new memory
            if(size_ == 0) {
                max_id_ = new_max_id;
                allocate_bits();
                return;
            }
            
            // Vector has elements - check if we can safely preserve data
            index_type current_max_element = maxID(); // Get largest actual element
            
            if(new_max_id >= current_max_element) {
                // Safe case: new max_id is at or above the largest element
                // We can preserve all data by copying to new memory layout
                
                // Save current data
                index_type old_max_id = max_id_;
                const size_t old_words = id_array.words();
                PackedArray<BitsPerValue> old_array(old_words);
                old_array.copy_from(id_array, old_words);
                
                // Update max_id and allocate new memory
                max_id_ = new_max_id;
                allocate_bits();
                
                // Copy elements from old array to new array (indices remain the same)
                for(index_type old_id = min_id_; old_id <= old_max_id; ++old_id) {
                    index_type old_index = old_id - min_id_;
                    count_type element_count = old_array.get(old_index);
                    if(element_count > 0) {
                        index_type new_index = old_id - min_id_; // Same index in new array
                        id_array.set(new_index, element_count);
                    }
                }
                // size_ remains the same since we preserved all elements
                
            } else {
                // Potentially unsafe case: new max_id is below some existing elements
                // This would cause data loss, so we throw an exception
                throw std::out_of_range("Cannot set max_id below existing elements. Current largest element is " + std::to_string(current_max_element));
            }
        }
        
        // Set minimum ID that can be stored and allocate memory accordingly
        void set_minID(index_type new_min_id) {
            if(new_min_id > MAX_RF_ID){
                throw std::out_of_range("Min RF ID exceeds limit");
            }
            if(new_min_id > max_id_){
                throw std::out_of_range("Min ID cannot be greater than max ID");
            }
            
            // If vector is empty, just update min_id and allocate new memory
            if(size_ == 0) {
                min_id_ = new_min_id;
                allocate_bits();
                return;
            }
            
            // Vector has elements - check if we can safely cut off lower range
            index_type current_min_element = minID(); // Get smallest actual element
            
            if(new_min_id <= current_min_element) {
                // Safe case: new min_id is at or below the smallest element
                // We can preserve all data by copying to new memory layout
                
                // Save current data
                index_type old_min_id = min_id_;
                const size_t old_words = id_array.words();
                PackedArray<BitsPerValue> old_array(old_words);
                old_array.copy_from(id_array, old_words);
                
                // Update min_id and allocate new memory
                min_id_ = new_min_id;
                allocate_bits();
                
                // Copy elements from old array to new array with adjusted indices
                for(index_type old_id = current_min_element; old_id <= max_id_; ++old_id) {
                    index_type old_index = old_id - old_min_id;
                    count_type element_count = old_array.get(old_index);
                    if(element_count > 0) {
                        index_type new_index = old_id - min_id_;
                        id_array.set(new_index, element_count);
                    }
                }
                // size_ remains the same since we preserved all elements
                
            } else {
                // Potentially unsafe case: new min_id is above some existing elements
                // This would cause data loss, so we throw an exception
                throw std::out_of_range("Cannot set min_id above existing elements. Current smallest element is " + std::to_string(current_min_element));
            }
        }

        // Set both min and max ID range and allocate memory accordingly
        void set_ID_range(index_type new_min_id, index_type new_max_id) {
            if(new_min_id > MAX_RF_ID || new_max_id > MAX_RF_ID){
                throw std::out_of_range("RF ID exceeds limit");
            }
            if(new_min_id > new_max_id){
                throw std::out_of_range("Min ID cannot be greater than max ID");
            }
            
            // If vector is empty, just update range and allocate new memory
            if(size_ == 0) {
                min_id_ = new_min_id;
                max_id_ = new_max_id;
                allocate_bits();
                return;
            }
            
            // Vector has elements - check if we can safely preserve data
            index_type current_min_element = minID(); // Get smallest actual element
            index_type current_max_element = maxID(); // Get largest actual element
            
            if(new_min_id <= current_min_element && new_max_id >= current_max_element) {
                // Safe case: new range encompasses all existing elements
                // We can preserve all data by copying to new memory layout
                
                // Save current data
                index_type old_min_id = min_id_;
                index_type old_max_id = max_id_;
                const size_t old_words = id_array.words();
                PackedArray<BitsPerValue> old_array(old_words);
                old_array.copy_from(id_array, old_words);
                
                // Update range and allocate new memory
                min_id_ = new_min_id;
                max_id_ = new_max_id;
                allocate_bits();
                
                // Copy elements from old array to new array with adjusted indices
                for(index_type old_id = old_min_id; old_id <= old_max_id; ++old_id) {
                    index_type old_index = old_id - old_min_id;
                    count_type element_count = old_array.get(old_index);
                    if(element_count > 0) {
                        index_type new_index = old_id - min_id_;
                        id_array.set(new_index, element_count);
                    }
                }
                // size_ remains the same since we preserved all elements
                
            } else {
                // Potentially unsafe case: new range doesn't encompass all existing elements
                char error_msg[128];
                snprintf(error_msg, sizeof(error_msg), "Cannot set ID range that excludes existing elements. Current elements range: [%llu, %llu]", static_cast<unsigned long long>(current_min_element), static_cast<unsigned long long>(current_max_element));
                throw std::out_of_range(error_msg);
            }
        }

        // default constructor (default max ID 127, min ID 0 -> 128 bits -> 16 bytes)
        ID_vector(){
            set_maxID(DEFAULT_MAX_ID);
        }

        // constructor with max expected ID - calls set_maxID automatically
        explicit ID_vector(index_type max_id){
            set_maxID(max_id);
        }

        // constructor with min and max expected ID range
        ID_vector(index_type min_id, index_type max_id){
            set_ID_range(min_id, max_id);
        }

        // Copy constructor
        ID_vector(const ID_vector& other) 
            : id_array(), max_id_(other.max_id_), min_id_(other.min_id_), size_(other.size_) {
            const size_t words = other.id_array.words();
            id_array = PackedArray<BitsPerValue>(words);
            id_array.copy_from(other.id_array, words);
        }

        // constructor with b_vector of IDs (uint8_t, uint16_t, uint32_t, size_t)
        template<typename Y>
        ID_vector(const b_vector<Y>& ids,
                  typename std::enable_if<std::is_same<Y, uint8_t>::value || 
                                         std::is_same<Y, uint16_t>::value || 
                                         std::is_same<Y, uint32_t>::value ||
                                         std::is_same<Y, size_t>::value >::type* = nullptr) {
            if(ids.empty()){
                set_maxID(DEFAULT_MAX_ID);
                return;
            }
            ids.sort();
            index_type min_id = static_cast<size_t>(ids.front());
            index_type max_id = static_cast<size_t>(ids.back());
            set_ID_range(min_id, max_id);
            for(const Y& id : ids){
                push_back(static_cast<size_t>(id));
            }
        }
        // constructor with vector of IDs (uint8_t, uint16_t, uint32_t, size_t)
        template<typename Y>
        ID_vector(const vector<Y>& ids,
                  typename std::enable_if<std::is_same<Y, uint8_t>::value || 
                                         std::is_same<Y, uint16_t>::value || 
                                         std::is_same<Y, uint32_t>::value ||
                                         std::is_same<Y, size_t>::value >::type* = nullptr) {
            if(ids.empty()){
                set_maxID(DEFAULT_MAX_ID);
                return;
            }
            ids.sort();
            index_type min_id = static_cast<size_t>(ids.front());
            index_type max_id = static_cast<size_t>(ids.back());
            set_ID_range(min_id, max_id);
            for(const Y& id : ids){
                push_back(static_cast<size_t>(id));
            }
        }

        // Move constructor
        ID_vector(ID_vector&& other) noexcept 
            : id_array(std::move(other.id_array)), 
              max_id_(other.max_id_), min_id_(other.min_id_), size_(other.size_) {
            other.min_id_ = 0;
            other.max_id_ = 0;
            other.size_ = 0;
        }

        // Copy assignment operator
        ID_vector& operator=(const ID_vector& other) {
            if (this != &other) {
                min_id_ = other.min_id_;
                max_id_ = other.max_id_;
                size_ = other.size_;
                
                const size_t words = other.id_array.words();
                id_array = PackedArray<BitsPerValue>(words);
                id_array.copy_from(other.id_array, words);
            }
            return *this;
        }

        // Move assignment operator
        ID_vector& operator=(ID_vector&& other) noexcept {
            if (this != &other) {
                id_array = std::move(other.id_array);
                min_id_ = other.min_id_;
                max_id_ = other.max_id_;
                size_ = other.size_;
                
                other.min_id_ = 0;
                other.max_id_ = 0;
                other.size_ = 0;
            }
            return *this;
        }

        // Destructor (default is fine since PackedArray handles its own cleanup)
        ~ID_vector() = default;


        // check presence
        bool contains(index_type id) const {
            if(id < min_id_ || id > max_id_) return false;
            return id_array.get(id_to_index(id)) != 0;
        }

        // insert ID (order independent, data structure is inherently sorted)
        void push_back(index_type id){
            // Check if ID exceeds absolute maximum
            if(id > MAX_RF_ID){
                throw std::out_of_range("ID exceeds maximum allowed RF ID limit");
            }
            
            // Auto-expand range if necessary
            if(id > max_id_){
                set_maxID(id);
            } else if(id < min_id_){
                set_minID(id);
            }
            
            index_type index = id_to_index(id);
            count_type current_count = id_array.get(index);
            if(current_count < MAX_COUNT){
                id_array.set(index, current_count + 1);
                ++size_;
            } // if already at max count, ignore (do nothing)
        }

        // get count of specific ID
        count_type count(index_type id) const {
            if(id < min_id_ || id > max_id_) return 0;
            return id_array.get(id_to_index(id));
        }

        // remove one instance of specific ID (if exists)
        bool erase(index_type id){
            if(id < min_id_ || id > max_id_) return false;
            index_type index = id_to_index(id);
            count_type current_count = id_array.get(index);
            if(current_count > 0){
                id_array.set(index, current_count - 1);
                --size_;
                return true;
            }
            return false;
        }

        // largest ID in the vector (if empty, throws)
        index_type back() const {
            if(size_ == 0) throw std::out_of_range("ID_vector is empty");
            
            // Find the highest ID with count > 0
            // Use a safer loop to avoid unsigned underflow issues
            for(index_type id = max_id_; ; --id) {
                if(id_array.get(id_to_index(id)) > 0) {
                    return id;
                }
                if(id == min_id_) break; // Avoid underflow
            }
            throw std::out_of_range("ID_vector::back() internal error");
        }

        // pop largest ID (remove one instance)
        void pop_back(){
            if(size_ == 0) return; // empty
            
            // Find the highest ID with count > 0 and decrement
            // Use a safer loop to avoid unsigned underflow issues
            for(index_type id = max_id_; ; --id) {
                index_type index = id_to_index(id);
                count_type current_count = id_array.get(index);
                if(current_count > 0) {
                    id_array.set(index, current_count - 1);
                    --size_;
                    return;
                }
                if(id == min_id_) break; // Avoid underflow
            }
        }

        T front() const {
            if(size_ == 0) throw std::out_of_range("ID_vector is empty");
            
            // Find the lowest ID with count > 0
            for(T id = min_id_; id <= max_id_; ++id) {
                if(id_array.get(id_to_index(id)) > 0) {
                    return id;  
                }
            }
            throw std::out_of_range("ID_vector::front() internal error");
        }

        void pop_front() {
            if(size_ == 0) return; // empty

            // Find the lowest ID with count > 0 and decrement
            for(index_type id = min_id_; id <= max_id_; ++id) {
                index_type index = id_to_index(id);
                count_type current_count = id_array.get(index);
                if(current_count > 0) {
                    id_array.set(index, current_count - 1);
                    --size_;
                    return;
                }
            }
        }

        void reserve(index_type new_max_id){
            if(new_max_id >= MAX_RF_ID){
                throw std::out_of_range("Max RF ID exceeds limit");
            }
            if(new_max_id < min_id_){
                throw std::out_of_range("Max ID cannot be less than min ID");
            }
            if(new_max_id > max_id_){
                set_maxID(new_max_id);
            }
        }

        // get number of unique IDs stored (if bitspervalue=1, this is same as size())
        size_type unique_size() const {
            if(BitsPerValue == 1) return size_;
            size_type unique_count = 0;
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
            for(index_type i = 0; i < range; ++i){
                if(id_array.get(i) > 0) ++unique_count;
            }
            return unique_count;
        }

        // nth element (0-based) among all ID instances (in ascending order)
        // When an ID appears multiple times, it will be returned multiple times
        index_type operator[](size_type index) const {
            if(index >= size_) throw std::out_of_range("ID_vector::operator[] index out of range");
            
            size_type current_count = 0;
            for(index_type id = min_id_; id <= max_id_; ++id) {
                count_type id_count = id_array.get(id_to_index(id));
                if(id_count > 0) {
                    if(current_count + id_count > index) {
                        // The index falls within this ID's instances
                        return id;
                    }
                    current_count += id_count;
                }
            }
            throw std::out_of_range("ID_vector::operator[] internal error");
        }

        // iterator over all ID instances (ascending order with repetitions)
        class iterator {
            const ID_vector* vec = nullptr;
            index_type current_id = 0; // Current ID being processed
            count_type remaining_count = 0; // Remaining instances of current ID

            void find_first() {
                current_id = vec ? vec->min_id_ : 0;
                remaining_count = 0;
                
                if (!vec) {
                    current_id = 0;
                    remaining_count = 0;
                    return;
                }

                // Find the first ID with count > 0, starting from min_id
                while (current_id <= vec->max_id_) {
                    count_type id_count = vec->id_array.get(vec->id_to_index(current_id));
                    if (id_count > 0) {
                        remaining_count = id_count - 1; // -1 because we're returning this instance
                        return;
                    }
                    ++current_id;
                }

                // No IDs found
                current_id = vec->max_id_ + 1;
                remaining_count = 0;
            }

            void find_next() {
                if (!vec) {
                    current_id = 0;
                    remaining_count = 0;
                    return;
                }

                // If we still have instances of the current ID, just decrement
                if (remaining_count > 0) {
                    --remaining_count;
                    return;
                }

                // Find the next ID with count > 0
                ++current_id; // Move to next ID
                while (current_id <= vec->max_id_) {
                    count_type id_count = vec->id_array.get(vec->id_to_index(current_id));
                    if (id_count > 0) {
                        remaining_count = id_count - 1; // -1 because we're returning this instance
                        return;
                    }
                    ++current_id;
                }

                // No more IDs found
                current_id = vec->max_id_ + 1;
                remaining_count = 0;
            }

        public:
            using value_type = index_type;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;
            using pointer = const value_type*;
            using reference = const value_type&;

            iterator() : vec(nullptr), current_id(0), remaining_count(0) {}

            // Constructor for begin() and end()
            iterator(const ID_vector* v, bool is_end) : vec(v), current_id(0), remaining_count(0) {
                if (!v || v->size_ == 0 || is_end) {
                    current_id = v ? v->max_id_ + 1 : 0;
                    remaining_count = 0;
                } else {
                    find_first();
                }
            }

            reference operator*() const { return current_id; }

            iterator& operator++() {
                find_next();
                return *this;
            }

            iterator operator++(int) {
                iterator tmp = *this;
                find_next();
                return tmp;
            }

            bool operator==(const iterator& other) const {
                return vec == other.vec && current_id == other.current_id && remaining_count == other.remaining_count;
            }

            bool operator!=(const iterator& other) const {
                return !(*this == other);
            }
        };

        iterator begin() const { return iterator(this, false); }
        iterator end() const { return iterator(this, true); }

        // Comparison operators
        bool operator==(const ID_vector& other) const {
            if (this == &other) return true;
            if (min_id_ != other.min_id_ || max_id_ != other.max_id_ || size_ != other.size_) {
                return false;
            }
            
            // Compare element by element
            for (index_type id = min_id_; id <= max_id_; ++id) {
                if (count(id) != other.count(id)) {
                    return false;
                }
            }
            return true;
        }

        bool operator!=(const ID_vector& other) const {
            return !(*this == other);
        }

        // Subset comparison (this ⊆ other)
        bool is_subset_of(const ID_vector& other) const {
            if (min_id_ < other.min_id_ || max_id_ > other.max_id_) {
                return false; // Range not contained
            }
            
            for (index_type id = min_id_; id <= max_id_; ++id) {
                if (count(id) > other.count(id)) {
                    return false; // This has more instances than other
                }
            }
            return true;
        }

        // Set operations
        ID_vector operator|(const ID_vector& other) const { // Union
            index_type new_min = (min_id_ < other.min_id_) ? min_id_ : other.min_id_;
            index_type new_max = (max_id_ > other.max_id_) ? max_id_ : other.max_id_;
            ID_vector result(new_min, new_max);
            
            for (index_type id = new_min; id <= new_max; ++id) {
                count_type count1 = (id >= min_id_ && id <= max_id_) ? count(id) : 0;
                count_type count2 = (id >= other.min_id_ && id <= other.max_id_) ? other.count(id) : 0;
                count_type max_count = (count1 > count2) ? count1 : count2;
                
                for (count_type i = 0; i < max_count; ++i) {
                    result.push_back(id);
                }
            }
            return result;
        }

        ID_vector operator&(const ID_vector& other) const { // Intersection
            index_type new_min = (min_id_ > other.min_id_) ? min_id_ : other.min_id_;
            index_type new_max = (max_id_ < other.max_id_) ? max_id_ : other.max_id_;
            
            if (new_min > new_max) {
                return ID_vector(); // Empty intersection
            }
            
            ID_vector result(new_min, new_max);
            
            for (index_type id = new_min; id <= new_max; ++id) {
                count_type count1 = count(id);
                count_type count2 = other.count(id);
                count_type min_count = (count1 < count2) ? count1 : count2;
                
                for (count_type i = 0; i < min_count; ++i) {
                    result.push_back(id);
                }
            }
            return result;
        }

        // Compound assignment operators
        ID_vector& operator|=(const ID_vector& other) { // Union assignment
            *this = *this | other;
            return *this;
        }

        ID_vector& operator&=(const ID_vector& other) { // Intersection assignment
            *this = *this & other;
            return *this;
        }

        // Fill vector with all values in the current range [min_id_, max_id_]
        // For BitsPerValue > 1, fills with maximum count (MAX_COUNT) for each ID
        void fill() {
            if (max_id_ < min_id_) return; // Invalid range
            
            clear(); // Start fresh
            
            for (index_type id = min_id_; id <= max_id_; ++id) {
                // Fill with maximum possible count for each ID
                for (count_type i = 0; i < MAX_COUNT; ++i) {
                    push_back(id);
                }
            }
        }
        // remove all instances of specific ID (if exists)
        bool erase_all(index_type id){
            if(id < min_id_ || id > max_id_) return false;
            index_type index = id_to_index(id);
            count_type current_count = id_array.get(index);
            if(current_count > 0){
                id_array.set(index, 0);
                size_ -= current_count; // subtract all instances
                return true;
            }
            return false;
        }

        // Erase all instances of IDs in range [start, end] (inclusive)
        // Does NOT change the vector's min_id_/max_id_ range
        void erase_range(index_type start, index_type end) {
            if (start > end) return; // Invalid range
            
            // Only process IDs within our current range
            index_type actual_start = (start > min_id_) ? start : min_id_;
            index_type actual_end = (end < max_id_) ? end : max_id_;
            
            if (actual_start > actual_end) return; // No overlap
            
            for (index_type id = actual_start; id <= actual_end; ++id) {
                erase_all(id); // Remove all instances of this ID
            }
        }

        // Insert all IDs in range [start, end] (inclusive), one instance each
        // DOES allow expansion of the vector's min_id_/max_id_ range
        void insert_range(index_type start, index_type end) {
            if (start > end) return; // Invalid range
            
            for (index_type id = start; id <= end; ++id) {
                push_back(id); // This will auto-expand range if needed
            }
        }

        // Addition operator: adds one instance of each ID from other vector
        // Static assertion ensures compatible BitsPerValue
        template<uint8_t OtherBitsPerValue>
        ID_vector operator+(const ID_vector<T, OtherBitsPerValue>& other) const {
            static_assert(BitsPerValue == OtherBitsPerValue, 
                         "Cannot perform arithmetic operations on ID_vectors with different BitsPerValue");
            
            // Create result with expanded range to accommodate both vectors
            index_type new_min = (min_id_ < other.get_minID()) ? min_id_ : other.get_minID();
            index_type new_max = (max_id_ > other.get_maxID()) ? max_id_ : other.get_maxID();
            
            // Handle empty vectors
            if (size_ == 0 && other.size() == 0) {
                return ID_vector();
            } else if (size_ == 0) {
                new_min = other.get_minID();
                new_max = other.get_maxID();
            } else if (other.size() == 0) {
                new_min = min_id_;
                new_max = max_id_;
            }
            
            ID_vector result(new_min, new_max);
            
            // Copy this vector's elements
            for (index_type id = min_id_; id <= max_id_; ++id) {
                count_type my_count = count(id);
                for (count_type i = 0; i < my_count; ++i) {
                    result.push_back(id);
                }
            }
            
            // Add one instance of each ID from other vector
            for (index_type id = other.get_minID(); id <= other.get_maxID(); ++id) {
                if (other.count(id) > 0) {
                    result.push_back(id); // Add one instance
                }
            }
            
            return result;
        }

        // Subtraction operator: removes all instances of IDs present in other vector
        // Static assertion ensures compatible BitsPerValue
        template<uint8_t OtherBitsPerValue>
        ID_vector operator-(const ID_vector<T, OtherBitsPerValue>& other) const {
            static_assert(BitsPerValue == OtherBitsPerValue, 
                         "Cannot perform arithmetic operations on ID_vectors with different BitsPerValue");
            
            ID_vector result(min_id_, max_id_);
            
            for (index_type id = min_id_; id <= max_id_; ++id) {
                count_type my_count = count(id);
                if (my_count > 0) {
                    // If other vector contains this ID, remove all instances
                    bool other_has_id = (id >= other.get_minID() && id <= other.get_maxID() && other.count(id) > 0);
                    if (!other_has_id) {
                        // Keep all instances if other doesn't have this ID
                        for (count_type i = 0; i < my_count; ++i) {
                            result.push_back(id);
                        }
                    }
                    // If other has this ID, don't add any instances (remove all)
                }
            }
            
            return result;
        }

        // Addition assignment operator: adds one instance of each ID from other vector
        template<uint8_t OtherBitsPerValue>
        ID_vector& operator+=(const ID_vector<T, OtherBitsPerValue>& other) {
            static_assert(BitsPerValue == OtherBitsPerValue, 
                         "Cannot perform arithmetic operations on ID_vectors with different BitsPerValue");
            
            // Add one instance of each ID from other vector
            for (index_type id = other.get_minID(); id <= other.get_maxID(); ++id) {
                if (other.count(id) > 0) {
                    push_back(id); // Add one instance (auto-expands range if needed)
                }
            }
            return *this;
        }

        // Subtraction assignment operator: removes all instances of IDs present in other vector
        template<uint8_t OtherBitsPerValue>
        ID_vector& operator-=(const ID_vector<T, OtherBitsPerValue>& other) {
            static_assert(BitsPerValue == OtherBitsPerValue, 
                         "Cannot perform arithmetic operations on ID_vectors with different BitsPerValue");
            
            // Remove all instances of IDs present in other vector
            for (index_type id = other.get_minID(); id <= other.get_maxID(); ++id) {
                if (other.count(id) > 0) {
                    erase_all(id); // Remove all instances of this ID
                }
            }
            return *this;
        }

       // number of stored IDs
        size_type size() const { return size_; }
        bool empty() const { return size_ == 0; }

        void clear(){
            if(size_ == 0) return; // Already empty
            
            auto* data = id_array.raw_data();
            if(data != nullptr) {
                const size_t words = id_array.words();
                std::fill(data, data + words, 0u);
            }
            size_ = 0;
        }
        void fit() {    
            // Fit the ID_vector to the current range and size
            if(size_ == 0) {
                // Empty vector - nothing to fit
                return;
            }
            index_type new_min_id = minID();
            index_type new_max_id = maxID();
            if(new_min_id != min_id_ || new_max_id != max_id_) {
                set_ID_range(new_min_id, new_max_id);
            }
        }


        // Get current minimum ID that can be stored
        index_type get_minID() const {
            return min_id_;
        }

        // Get current maximum ID that can be stored  
        index_type get_maxID() const {
            return max_id_;
        }

        // get the smallest ID currently stored in the vector
        T minID(){
            if(size_ == 0) {
                throw std::out_of_range("ID_vector is empty");
            }
            // Find the lowest ID with count > 0
            for(T id = min_id_; id <= max_id_; ++id) {
                if(id_array.get(id_to_index(id)) > 0) {
                    return id;  
                }
            }
            throw std::out_of_range("ID_vector::minID() internal error");
        }

        // get the largest ID currently stored in the vector
        index_type maxID(){
            if(size_ == 0) {
                throw std::out_of_range("ID_vector is empty");
            }
            // Find the highest ID with count > 0
            // Use a safer loop to avoid unsigned underflow issues
            for(index_type id = max_id_; ; --id) {
                if(id_array.get(id_to_index(id)) > 0) {
                    return id;  
                }
                if(id == min_id_) break; // Avoid underflow
            }
            throw std::out_of_range("ID_vector::maxID() internal error");
        }

        size_t capacity() const {
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
            return range;
        }

        size_t memory_usage() const {
            const size_t range = static_cast<size_t>(max_id_) - static_cast<size_t>(min_id_) + 1;
            const size_t total_bits = range * BitsPerValue;
            const size_t words = bits_to_words(total_bits);
            using word_t = typename PackedArray<BitsPerValue>::word_t;
            const size_t bytes = words * sizeof(word_t);
            return sizeof(ID_vector) + bytes;
        }

        /**
         * @brief Get the current bits per value (runtime value).
         * @return Current bits per value setting.
         */
        uint8_t get_bits_per_value() const noexcept {
            return id_array.get_bpv();
        }

        /**
         * @brief Set the bits per value dynamically.
         * @param new_bpv New bits per value (must be 1-32).
         * @return true if successful, false if new_bpv is invalid or would cause data loss.
         * @note This reallocates the internal array and preserves existing data if possible.
         *       Will fail if any existing count value exceeds the new maximum count.
         */
        bool set_bits_per_value(uint8_t new_bpv) noexcept {
            // Validate new bits per value
            if (new_bpv == 0 || new_bpv > 32) {
                return false;
            }

            // Get current bits per value
            uint8_t current_bpv = id_array.get_bpv();
            
            // If same, no change needed
            if (new_bpv == current_bpv) {
                return true;
            }

            // Calculate new max count
            count_type new_max_count = (new_bpv >= 32) 
                ? std::numeric_limits<count_type>::max() 
                : ((static_cast<count_type>(1ULL) << new_bpv) - 1);

            // If reducing bits, check if any existing values would overflow
            if (new_bpv < current_bpv && size_ > 0) {
                index_type range = static_cast<index_type>((static_cast<size_t>(max_id_) - static_cast<size_t>(min_id_) + 1));
                for (index_type i = 0; i < range; ++i) {
                    count_type current_count = id_array.get(i);
                    if (current_count > new_max_count) {
                        // Would cause data loss
                        return false;
                    }
                }
            }

            // Save old data
            const size_t old_words = id_array.words();
            PackedArray<BitsPerValue> old_array(old_words);
            old_array.copy_from(id_array, old_words);

            // Calculate new word count and reallocate
            const size_t range = static_cast<size_t>(max_id_) - static_cast<size_t>(min_id_) + 1;
            const size_t new_total_bits = range * new_bpv;
            const size_t new_words = bits_to_words(new_total_bits);
            
            id_array = PackedArray<BitsPerValue>(new_words);
            id_array.set_bpv(new_bpv);

            // Copy elements from old array to new array with new bit width
            if (size_ > 0) {
                index_type element_range = static_cast<index_type>((static_cast<size_t>(max_id_) - static_cast<size_t>(min_id_) + 1));
                for (index_type i = 0; i < element_range; ++i) {
                    count_type count_val = old_array.get(i);
                    if (count_val > 0) {
                        id_array.set(i, count_val);
                    }
                }
            }

            return true;
        }
        
    };

    // 