
#include <iostream>
#include <stdexcept>
#include "../../../src/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <array>

using namespace mcu;

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

    template <typename T,  uint8_t BitsPerValue = 1>
    class ID_vector{
        static_assert(BitsPerValue > 0 && BitsPerValue <= 8, "BitsPerValue must be between 1 and 8");
    public:
        using count_type = uint8_t; // type for storing count of each ID
        
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
            536870912; // For size_t types
            
        constexpr static index_type DEFAULT_MAX_ID = 
            is_same_t<index_type, uint8_t>::value ? 63 :
            is_same_t<index_type, uint16_t>::value ? 255 :
            127; // default max ID
            
        constexpr static count_type MAX_COUNT = (1 << BitsPerValue) - 1; // maximum count per ID

        static constexpr size_t bits_to_words(size_t bits){ return (bits + 31) >> 5; }

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
                // This would cause data loss, so we throw an exception
                std::string error_msg = "Cannot set ID range that excludes existing elements. ";
                error_msg += "Current elements range: [" + std::to_string(current_min_element) + ", " + std::to_string(current_max_element) + "]";
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
            
            uint32_t* data = id_array.raw_data();
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
            const size_t bytes = words * sizeof(uint32_t);
            return sizeof(ID_vector) + bytes;
        }

        // takeout normalized vector of IDs (ascending order, no repetitions)
    };

    // 