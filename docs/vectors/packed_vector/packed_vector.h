#pragma once

#include <stdexcept>
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstring>
#include <cstdint>

#include "../../../src/initializer_list.h"
#include "../../../src/hash_kernel.h"

namespace mcu {
    
    template<uint8_t BitsPerElement>
    class PackedArray {
        static_assert(BitsPerElement > 0 && BitsPerElement <= 8, "Invalid bit size");
        uint8_t* data = nullptr;
        uint8_t bpv_ = BitsPerElement;  // Runtime bits per value

    public:
        // Default constructor - creates empty array
        PackedArray() : data(nullptr), bpv_(BitsPerElement) {}

        // Remove count - capacity is managed by packed_vector
        PackedArray(size_t capacity_bytes) : bpv_(BitsPerElement) {
            if(capacity_bytes > 0) {
                data = new uint8_t[capacity_bytes]();
            } else {
                data = nullptr;
            }
        }

        ~PackedArray() {
            delete[] data;
        }

        // Copy constructor - requires byte size
        PackedArray(const PackedArray& other, size_t bytes) : bpv_(other.bpv_) {
            data = new uint8_t[bytes];
            for (size_t i = 0; i < bytes; ++i) {
                data[i] = other.data[i];
            }
        }

        // Move constructor
        PackedArray(PackedArray&& other) noexcept : data(other.data), bpv_(other.bpv_) {
            other.data = nullptr;
        }

        // Copy from another PackedArray with specified byte size
        void copy_from(const PackedArray& other, size_t bytes) {
            delete[] data;
            data = new uint8_t[bytes];
            for (size_t i = 0; i < bytes; ++i) {
                data[i] = other.data[i];
            }
            bpv_ = other.bpv_;
        }

        // Copy assignment
        PackedArray& operator=(const PackedArray& other) {
            if (this != &other) {
                // This shouldn't be used directly - use copy_from with byte size
                delete[] data;
                data = nullptr;
            }
            return *this;
        }

        // Move assignment
        PackedArray& operator=(PackedArray&& other) noexcept {
            if (this != &other) {
                delete[] data;
                data = other.data;
                bpv_ = other.bpv_;
                other.data = nullptr;
            }
            return *this;
        }

        // Get bits per value
        uint8_t get_bpv() const { return bpv_; }
        
        // Set bits per value (must be called before using the array)
        void set_bpv(uint8_t new_bpv) {
            if (new_bpv > 0 && new_bpv <= 8) {
                bpv_ = new_bpv;
            }
        }

        // Fast bit manipulation without bounds checking
        __attribute__((always_inline)) inline void set_unsafe(size_t index, uint8_t value) {
            if(data == nullptr) return; // Safety check
            
            value &= (1 << bpv_) - 1;
            size_t bitPos = index * bpv_;
            size_t byteIdx = bitPos >> 3;  // Faster than /8
            size_t bitOff = bitPos & 7;    // Faster than %8
            
            if (bitOff + bpv_ <= 8) {
                uint8_t mask = ((1 << bpv_) - 1) << bitOff;
                data[byteIdx] = (data[byteIdx] & ~mask) | (value << bitOff);
            } else {
                uint8_t bitsInFirstByte = 8 - bitOff;
                uint8_t bitsInSecondByte = bpv_ - bitsInFirstByte;
                
                uint8_t mask1 = ((1 << bitsInFirstByte) - 1) << bitOff;
                uint8_t mask2 = (1 << bitsInSecondByte) - 1;
                
                data[byteIdx] = (data[byteIdx] & ~mask1) | ((value & ((1 << bitsInFirstByte) - 1)) << bitOff);
                data[byteIdx + 1] = (data[byteIdx + 1] & ~mask2) | (value >> bitsInFirstByte);
            }
        }

        __attribute__((always_inline)) inline uint8_t get_unsafe(size_t index) const {
            if(data == nullptr) return 0; // Safety check
            
            size_t bitPos = index * bpv_;
            size_t byteIdx = bitPos >> 3;  // Faster than /8
            size_t bitOff = bitPos & 7;    // Faster than %8
            
            if (bitOff + bpv_ <= 8) {
                return (data[byteIdx] >> bitOff) & ((1 << bpv_) - 1);
            } else {
                uint8_t bitsInFirstByte = 8 - bitOff;
                uint8_t bitsInSecondByte = bpv_ - bitsInFirstByte;
                
                uint8_t firstPart = (data[byteIdx] >> bitOff) & ((1 << bitsInFirstByte) - 1);
                uint8_t secondPart = (data[byteIdx + 1] & ((1 << bitsInSecondByte) - 1)) << bitsInFirstByte;
                
                return firstPart | secondPart;
            }
        }

        // Fast memory copy for resize operations
        void copy_elements(const PackedArray& src, size_t element_count) {
            if (element_count == 0) return;
            size_t bits = element_count * bpv_;
            size_t full_bytes = bits >> 3;  // Full bytes to copy
            size_t remaining_bits = bits & 7;  // Remaining bits
            
            // Copy full bytes
            for (size_t i = 0; i < full_bytes; ++i) {
                data[i] = src.data[i];
            }
            
            // Copy remaining bits if any
            if (remaining_bits > 0) {
                uint8_t mask = (1 << remaining_bits) - 1;
                data[full_bytes] = (data[full_bytes] & ~mask) | (src.data[full_bytes] & mask);
            }
        }

        // Compatibility methods for existing code
        void set(size_t index, uint8_t value) { set_unsafe(index, value); }
        uint8_t get(size_t index) const { return get_unsafe(index); }
        
        // Get raw data pointer for bulk operations
        uint8_t* raw_data() { return data; }
        const uint8_t* raw_data() const { return data; }
    };


    // Specialized packed_vector for packed elements
    template<uint8_t BitsPerElement, index_size_flag SizeFlag = index_size_flag::MEDIUM>
    class packed_vector {
    private:
        using vector_index_type = typename vector_index_type<SizeFlag>::type;
        PackedArray<BitsPerElement> packed_data;
        
        // For TINY: pack size (4 bits) and capacity (4 bits) into one uint8_t
        // For others: use separate variables
        static constexpr bool IS_TINY = (SizeFlag == index_size_flag::TINY);
        
        union {
            struct {
                vector_index_type size_;
                vector_index_type capacity_;
            } separate;
            uint8_t packed_size_capacity; // Lower 4 bits = size, upper 4 bits = capacity
        } storage;
        
        static constexpr int VECTOR_MAX_CAP = 
            (SizeFlag == index_size_flag::TINY) ? 15 :
            (std::is_same<vector_index_type, uint8_t>::value) ? 255 :
            (std::is_same<vector_index_type, uint16_t>::value) ? 65535 :
            2000000000;
        
        static constexpr uint8_t MAX_VALUE = (1 << BitsPerElement) - 1;
        
        // Helper to get runtime max value based on current bpv
        inline uint8_t get_max_value() const {
            return (1 << packed_data.get_bpv()) - 1;
        }

        struct init_view {
            const uint8_t* data;
            unsigned count;
        };

        static init_view normalize_init_list(mcu::min_init_list<uint8_t> init, uint8_t active_bpv) {
            init_view view{init.begin(), 0U};
            unsigned raw_size = init.size();

            const uint8_t* raw_data = init.begin();
            if (raw_size == 0U || raw_data == nullptr) {
                view.data = nullptr;
                view.count = 0U;
                return view;
            }

            bool drop_header = false;
            if (raw_data[0] == active_bpv && raw_size > 1U) {
                for (unsigned i = 1U; i < raw_size; ++i) {
                    if (raw_data[i] > active_bpv) {
                        drop_header = true;
                        break;
                    }
                }
            }

            if (drop_header) {
                raw_data += 1;
                raw_size -= 1U;
            }

            const unsigned max_cap = static_cast<unsigned>(VECTOR_MAX_CAP);
            if (raw_size > max_cap) {
                raw_size = max_cap;
            }

            view.data = (raw_size > 0U) ? raw_data : nullptr;
            view.count = raw_size;
            return view;
        }
        
        // Calculate bytes needed for given capacity
        static inline size_t calc_bytes_for_bpv(vector_index_type capacity, uint8_t bpv) {
            size_t bits = static_cast<size_t>(capacity) * static_cast<size_t>(bpv);
            return (bits + 7) >> 3;  // Faster than /8
        }

        inline size_t calc_bytes(vector_index_type capacity) const {
            return calc_bytes_for_bpv(capacity, packed_data.get_bpv());
        }
        
        // Initialize with new bits per value (private helper)
        void init(uint8_t bpv) {
            if (bpv == 0 || bpv > 8) return;  // Invalid bpv
            
            // Clean up existing data
            clear();
            
            // Set new bpv
            packed_data.set_bpv(bpv);
            
            // Reallocate with new bpv (minimum capacity of 1)
            vector_index_type current_capacity = get_capacity();
            if (current_capacity == 0) current_capacity = 1;
            
            PackedArray<BitsPerElement> new_data(calc_bytes(current_capacity));
            new_data.set_bpv(bpv);
            packed_data = std::move(new_data);
        }
        
        // Helper functions for TINY mode
        vector_index_type get_size() const {
            return IS_TINY ? (storage.packed_size_capacity & 0x0F) : storage.separate.size_;
        }
        
        vector_index_type get_capacity() const {
            return IS_TINY ? ((storage.packed_size_capacity >> 4) & 0x0F) : storage.separate.capacity_;
        }
        
        void set_size(vector_index_type new_size) {
            if (IS_TINY) {
                storage.packed_size_capacity = (storage.packed_size_capacity & 0xF0) | (new_size & 0x0F);
            } else {
                storage.separate.size_ = new_size;
            }
        }
        
        void set_capacity(vector_index_type new_capacity) {
            if (IS_TINY) {
                storage.packed_size_capacity = (storage.packed_size_capacity & 0x0F) | ((new_capacity & 0x0F) << 4);
            } else {
                storage.separate.capacity_ = new_capacity;
            }
        }
        
        void set_size_capacity(vector_index_type new_size, vector_index_type new_capacity) {
            if (IS_TINY) {
                storage.packed_size_capacity = ((new_capacity & 0x0F) << 4) | (new_size & 0x0F);
            } else {
                storage.separate.size_ = new_size;
                storage.separate.capacity_ = new_capacity;
            }
        }

    public:
        // Default constructor
        packed_vector() : packed_data(calc_bytes_for_bpv(1, BitsPerElement)) {
            set_size_capacity(0, 1);
        }
        
        // Constructor with initial capacity
        explicit packed_vector(vector_index_type initialCapacity) 
            : packed_data(calc_bytes_for_bpv((initialCapacity == 0) ? 1 : initialCapacity, BitsPerElement)) {
            set_size_capacity(0, (initialCapacity == 0) ? 1 : initialCapacity);
        }
        
        // Constructor with initial size and value
        explicit packed_vector(vector_index_type initialSize, uint8_t value) 
            : packed_data(calc_bytes_for_bpv((initialSize == 0) ? 1 : initialSize, BitsPerElement)) {
            set_size_capacity(initialSize, (initialSize == 0) ? 1 : initialSize);
            value &= get_max_value();
            for (vector_index_type i = 0; i < get_size(); ++i) {
                packed_data.set_unsafe(i, value);
            }
        }
        
        // Initializer list constructor using custom min_init_list
        packed_vector(mcu::min_init_list<uint8_t> init)
            : packed_vector() {
            assign(init);
        }
        
        // Copy constructor
        packed_vector(const packed_vector& other) 
            : packed_data(other.packed_data, other.calc_bytes(other.get_capacity())) {
            packed_data.set_bpv(other.get_bits_per_value());
            if (IS_TINY) {
                storage.packed_size_capacity = other.storage.packed_size_capacity;
            } else {
                storage.separate = other.storage.separate;
            }
        }
        
        // Move constructor
        packed_vector(packed_vector&& other) noexcept 
            : packed_data(std::move(other.packed_data)) {
            if (IS_TINY) {
                storage.packed_size_capacity = other.storage.packed_size_capacity;
                other.storage.packed_size_capacity = 0x10; // capacity=1, size=0
            } else {
                storage.separate = other.storage.separate;
                other.storage.separate.size_ = 0;
                other.storage.separate.capacity_ = 0;
            }
        }
        
        // Copy assignment
        packed_vector& operator=(const packed_vector& other) {
            if (this != &other) {
                packed_data.copy_from(other.packed_data, other.calc_bytes(other.get_capacity()));
                if (IS_TINY) {
                    storage.packed_size_capacity = other.storage.packed_size_capacity;
                } else {
                    storage.separate = other.storage.separate;
                }
            }
            return *this;
        }
        // Range constructor - copy from another packed_vector within specified range
        // SAME TYPE VERSION: Only accepts source with identical template parameters
        // Parameters:
        //   source: The source packed_vector to copy from (same BitsPerElement and SizeFlag)
        //   start_index: Starting index (inclusive) in the source vector
        //   end_index: Ending index (exclusive) in the source vector
        // Behavior:
        //   - Creates a new vector containing elements [start_index, end_index)
        //   - If start_index >= source.size() or start_index > end_index, creates empty vector
        //   - If end_index > source.size(), it's clamped to source.size()
        //   - Capacity is set to the range size (or 1 if empty)
        //   - Memory efficient: only allocates space needed for the range
        //   - Values are clamped using MAX_VALUE for safety
        // NOTE: Use size_t for indices to avoid truncation when destination index type is small.
        packed_vector(const packed_vector& source, size_t start_index, size_t end_index) {
            uint8_t source_bpv = source.get_bits_per_value();
            uint8_t active_bpv = (source_bpv == 0) ? BitsPerElement : source_bpv;
            if (active_bpv > BitsPerElement) {
                active_bpv = BitsPerElement;
            }

            if (start_index > end_index || start_index >= source.get_size()) {
                // Invalid range - create empty vector with capacity 1
                packed_data = PackedArray<BitsPerElement>(calc_bytes_for_bpv(1, active_bpv));
                packed_data.set_bpv(active_bpv);
                set_size_capacity(0, 1);
                return;
            }
            
            // Clamp end_index to source size
            if (end_index > static_cast<size_t>(source.get_size())) {
                end_index = static_cast<size_t>(source.get_size());
            }
            
            size_t range_size_sz = end_index - start_index;
            // Clamp to destination capacity type limits
            vector_index_type range_size = static_cast<vector_index_type>(range_size_sz > static_cast<size_t>(VECTOR_MAX_CAP) ? VECTOR_MAX_CAP : range_size_sz);
            vector_index_type new_capacity = range_size > 0 ? range_size : 1;
            
            packed_data = PackedArray<BitsPerElement>(calc_bytes_for_bpv(new_capacity, active_bpv));
            packed_data.set_bpv(active_bpv);
            set_size_capacity(range_size, new_capacity);
            
            // Copy elements from source range with value clamping
            for (size_t i = 0; i < static_cast<size_t>(range_size); ++i) {
                uint8_t value = source.packed_data.get_unsafe(start_index + i);
                packed_data.set_unsafe(i, value & get_max_value());
            }
        }
        
        // Templated range constructor - copy from another packed_vector with potentially different BitsPerElement
        // CROSS TYPE VERSION: Accepts source with different template parameters
        // This allows copying between vectors with different bit sizes with automatic value clamping
        // Parameters:
        //   source: The source packed_vector to copy from (can have different BitsPerElement/SizeFlag)
        //   start_index: Starting index (inclusive) in the source vector
        //   end_index: Ending index (exclusive) in the source vector
        // Safety mechanisms:
        //   - Values are automatically clamped to destination's MAX_VALUE using bitwise AND
        //   - Type safety enforced at compile time through template parameters
        //   - Same bounds checking as non-templated version
        template<uint8_t SourceBitsPerElement, index_size_flag SourceSizeFlag = SizeFlag>
        packed_vector(const packed_vector<SourceBitsPerElement, SourceSizeFlag>& source, 
                     size_t start_index, size_t end_index) {
            uint8_t source_bpv = source.get_bits_per_value();
            uint8_t active_bpv = (source_bpv == 0) ? BitsPerElement : source_bpv;
            if (active_bpv > BitsPerElement) {
                active_bpv = BitsPerElement;
            }

            if (start_index > end_index || start_index >= static_cast<size_t>(source.size())) {
                // Invalid range - create empty vector with capacity 1
                packed_data = PackedArray<BitsPerElement>(calc_bytes_for_bpv(1, active_bpv));
                packed_data.set_bpv(active_bpv);
                set_size_capacity(0, 1);
                return;
            }
            
            // Clamp end_index to source size
            if (end_index > static_cast<size_t>(source.size())) {
                end_index = static_cast<size_t>(source.size());
            }
            
            size_t range_size_sz = end_index - start_index;
            vector_index_type range_size = static_cast<vector_index_type>(range_size_sz > static_cast<size_t>(VECTOR_MAX_CAP) ? VECTOR_MAX_CAP : range_size_sz);
            vector_index_type new_capacity = range_size > 0 ? range_size : 1;
            
            packed_data = PackedArray<BitsPerElement>(calc_bytes_for_bpv(new_capacity, active_bpv));
            packed_data.set_bpv(active_bpv);
            set_size_capacity(range_size, new_capacity);
            
            // Copy elements from source range with value clamping for different bit sizes
            for (size_t i = 0; i < static_cast<size_t>(range_size); ++i) {
                uint8_t value = source[start_index + i];  // Use public operator[] for safety
                packed_data.set_unsafe(i, value & get_max_value());
            }
        }
        

        // Iterator class for packed_vector
        class iterator {
        private:
            PackedArray<BitsPerElement>* data_ptr;
            vector_index_type index;
            
        public:
            iterator(PackedArray<BitsPerElement>* ptr, vector_index_type idx) 
                : data_ptr(ptr), index(idx) {}
                
            uint8_t operator*() const { return data_ptr->get_unsafe(index); }
            
            iterator& operator++() { ++index; return *this; }
            iterator operator++(int) { iterator tmp = *this; ++index; return tmp; }
            iterator& operator--() { --index; return *this; }
            iterator operator--(int) { iterator tmp = *this; --index; return tmp; }
            
            iterator operator+(vector_index_type n) const { return iterator(data_ptr, index + n); }
            iterator operator-(vector_index_type n) const { return iterator(data_ptr, index - n); }
            iterator& operator+=(vector_index_type n) { index += n; return *this; }
            iterator& operator-=(vector_index_type n) { index -= n; return *this; }
            
            bool operator==(const iterator& other) const { return index == other.index; }
            bool operator!=(const iterator& other) const { return index != other.index; }
            bool operator<(const iterator& other) const { return index < other.index; }
            bool operator>(const iterator& other) const { return index > other.index; }
            bool operator<=(const iterator& other) const { return index <= other.index; }
            bool operator>=(const iterator& other) const { return index >= other.index; }
            
            vector_index_type operator-(const iterator& other) const { return index - other.index; }
            
            // Get current index for debugging
            vector_index_type get_index() const { return index; }
        };
        
        class const_iterator {
        private:
            const PackedArray<BitsPerElement>* data_ptr;
            vector_index_type index;
            
        public:
            const_iterator(const PackedArray<BitsPerElement>* ptr, vector_index_type idx) 
                : data_ptr(ptr), index(idx) {}
                
            // Convert from iterator to const_iterator
            const_iterator(const iterator& it) 
                : data_ptr(it.data_ptr), index(it.index) {}
                
            uint8_t operator*() const { return data_ptr->get_unsafe(index); }
            
            const_iterator& operator++() { ++index; return *this; }
            const_iterator operator++(int) { const_iterator tmp = *this; ++index; return tmp; }
            const_iterator& operator--() { --index; return *this; }
            const_iterator operator--(int) { const_iterator tmp = *this; --index; return tmp; }
            
            const_iterator operator+(vector_index_type n) const { return const_iterator(data_ptr, index + n); }
            const_iterator operator-(vector_index_type n) const { return const_iterator(data_ptr, index - n); }
            const_iterator& operator+=(vector_index_type n) { index += n; return *this; }
            const_iterator& operator-=(vector_index_type n) { index -= n; return *this; }
            
            bool operator==(const const_iterator& other) const { return index == other.index; }
            bool operator!=(const const_iterator& other) const { return index != other.index; }
            bool operator<(const const_iterator& other) const { return index < other.index; }
            bool operator>(const const_iterator& other) const { return index > other.index; }
            bool operator<=(const const_iterator& other) const { return index <= other.index; }
            bool operator>=(const const_iterator& other) const { return index >= other.index; }
            
            vector_index_type operator-(const const_iterator& other) const { return index - other.index; }
            
            // Get current index for debugging
            vector_index_type get_index() const { return index; }
        };

        // Iterator methods
        iterator begin() { return iterator(&packed_data, 0); }
        iterator end() { return iterator(&packed_data, get_size()); }
        const_iterator begin() const { return const_iterator(&packed_data, 0); }
        const_iterator end() const { return const_iterator(&packed_data, get_size()); }
        const_iterator cbegin() const { return const_iterator(&packed_data, 0); }
        const_iterator cend() const { return const_iterator(&packed_data, get_size()); }
        
        // Move assignment
        packed_vector& operator=(packed_vector&& other) noexcept {
            if (this != &other) {
                packed_data = std::move(other.packed_data);
                if (IS_TINY) {
                    storage.packed_size_capacity = other.storage.packed_size_capacity;
                    other.storage.packed_size_capacity = 0x10; // capacity=1, size=0
                } else {
                    storage.separate = other.storage.separate;
                    other.storage.separate.size_ = 0;
                    other.storage.separate.capacity_ = 0;
                }
            }
            return *this;
        }

        void push_back(uint8_t value) {
            value &= get_max_value();
            vector_index_type current_size = get_size();
            vector_index_type current_capacity = get_capacity();
            
            if (current_size == current_capacity) {
                vector_index_type newCapacity;
                if (VECTOR_MAX_CAP == 15) {
                    newCapacity = current_capacity + 1;
                } else if (VECTOR_MAX_CAP == 255) {
                    newCapacity = current_capacity + 10;
                } else {
                    newCapacity = current_capacity * 2;
                }
                if (newCapacity > VECTOR_MAX_CAP) newCapacity = VECTOR_MAX_CAP;
                reserve(newCapacity);
            }
            packed_data.set_unsafe(current_size, value);
            set_size(current_size + 1);
        }
        
        void pop_back() {
            vector_index_type current_size = get_size();
            if (current_size > 0) set_size(current_size - 1);
        }
        
        // Fill all elements with specified value
        void fill(uint8_t value) {
            value &= get_max_value();
            vector_index_type current_size = get_size();
            for (vector_index_type i = 0; i < current_size; ++i) {
                packed_data.set_unsafe(i, value);
            }
            // update size equal to capacity
            set_size(current_size);
        }
        
        __attribute__((always_inline)) inline uint8_t operator[](vector_index_type index) const {
            return packed_data.get_unsafe(index);
        }
        
        // Bounds-checked access
        uint8_t at(vector_index_type index) const {
            if (index >= get_size()) {
                throw std::out_of_range("packed_vector::at");
            }
            return packed_data.get_unsafe(index);
        }
        
        __attribute__((always_inline)) inline void set(vector_index_type index, uint8_t value) {
            value &= get_max_value();
            packed_data.set_unsafe(index, value);
        }
        
        // Unsafe set without bounds checking - use when storage is pre-sized
        __attribute__((always_inline)) inline void set_unsafe(vector_index_type index, uint8_t value) {
            value &= get_max_value();
            packed_data.set_unsafe(index, value);
        }
        
        uint8_t get(vector_index_type index) const {
            return (index < get_size()) ? packed_data.get_unsafe(index) : 0;
        }
        
        // Front and back access
        uint8_t front() const {
            if (get_size() == 0) throw std::out_of_range("packed_vector::front");
            return packed_data.get_unsafe(0);
        }

        // return pointer to raw data (for advanced use cases)
        const uint8_t* data() const {
            return packed_data.raw_data();
        }
        uint8_t* data() {
            return packed_data.raw_data();
        }
        
        // Resize 
        void resize(vector_index_type newSize, uint8_t value = 0) {
            vector_index_type current_capacity = get_capacity();
            vector_index_type current_size = get_size();
            
            if (newSize > current_capacity) {
                reserve(newSize);
            }
            if (newSize > current_size) {
                value &= get_max_value();
                for (vector_index_type i = current_size; i < newSize; ++i) {
                    packed_data.set_unsafe(i, value);
                }
            }
            set_size(newSize);
        }
        
        void reserve(vector_index_type newCapacity) {
            vector_index_type current_capacity = get_capacity();
            if (newCapacity > current_capacity) {
                uint8_t active_bpv = packed_data.get_bpv();
                PackedArray<BitsPerElement> new_data(calc_bytes_for_bpv(newCapacity, active_bpv));
                new_data.set_bpv(active_bpv);
                new_data.copy_elements(packed_data, get_size());
                packed_data = std::move(new_data);
                set_capacity(newCapacity);
            }
        }
        
        void assign(vector_index_type count, uint8_t value) {
            clear();
            resize(count, value);
        }
        
        void assign(mcu::min_init_list<uint8_t> init) {
            auto view = normalize_init_list(init, packed_data.get_bpv());
            clear();

            vector_index_type required = static_cast<vector_index_type>(view.count);
            if (required == 0) {
                return;
            }

            if (required > get_capacity()) {
                reserve(required);
            }

            for (vector_index_type i = 0; i < required; ++i) {
                packed_data.set_unsafe(i, view.data[i] & get_max_value());
            }
            set_size(required);
        }
        
        void clear() { set_size(0); }
        bool empty() const { return get_size() == 0; }
        vector_index_type size() const { return get_size(); }
        vector_index_type capacity() const { return get_capacity(); }
        
        uint8_t back() const {
            vector_index_type current_size = get_size();
            return (current_size > 0) ? packed_data.get_unsafe(current_size - 1) : 0;
        }
        
        static constexpr uint8_t max_value() { return MAX_VALUE; }
        static constexpr uint8_t bits_per_element() { return BitsPerElement; }
        
        // Get current runtime bits per value
        uint8_t get_bits_per_value() const { return packed_data.get_bpv(); }
        
        // Set bits per value at runtime - reinitializes the vector
        void set_bits_per_value(uint8_t bpv) {
            if(bpv == packed_data.get_bpv()) return;
            init(bpv);
        }

        // fit function to optimize memory usage
        void fit() {
            vector_index_type current_size = get_size();
            if (current_size < get_capacity()) {
                uint8_t active_bpv = packed_data.get_bpv();
                vector_index_type target_capacity = current_size ? current_size : 1;
                PackedArray<BitsPerElement> new_data(calc_bytes_for_bpv(target_capacity, active_bpv));
                new_data.set_bpv(active_bpv);
                new_data.copy_elements(packed_data, current_size);
                packed_data = std::move(new_data);
                set_capacity(target_capacity);
            }
        }
        
        // Memory usage in bytes
        size_t memory_usage() const {
            return calc_bytes(get_capacity());
        }
        
        // Comparison operators
        bool operator==(const packed_vector& other) const {
            vector_index_type current_size = get_size();
            if (current_size != other.get_size()) return false;
            for (vector_index_type i = 0; i < current_size; ++i) {
                if (packed_data.get_unsafe(i) != other.packed_data.get_unsafe(i)) return false;
            }
            return true;
        }
        
        bool operator!=(const packed_vector& other) const {
            return !(*this == other);
        }
    };

} // namespace mcu
