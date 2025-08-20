
#include <iostream>
#include <stdexcept>
#include "initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>
#include <cstddef>
#include <cstdint>

using namespace mcu;

    template<uint8_t BitsPerElement>
    class PackedArray {
        static_assert(BitsPerElement > 0 && BitsPerElement <= 8, "Invalid bit size");
        uint8_t* data = nullptr;

    public:
        // Default constructor - creates empty array
        PackedArray() : data(nullptr) {}

        // Remove count - capacity is managed by packed_vector
        PackedArray(size_t capacity_bytes) {
            data = new uint8_t[capacity_bytes]();
        }

        ~PackedArray() {
            delete[] data;
        }

        // Copy constructor - requires byte size
        PackedArray(const PackedArray& other, size_t bytes) {
            data = new uint8_t[bytes];
            for (size_t i = 0; i < bytes; ++i) {
                data[i] = other.data[i];
            }
        }

        // Move constructor
        PackedArray(PackedArray&& other) noexcept : data(other.data) {
            other.data = nullptr;
        }

        // Copy from another PackedArray with specified byte size
        void copy_from(const PackedArray& other, size_t bytes) {
            delete[] data;
            data = new uint8_t[bytes];
            for (size_t i = 0; i < bytes; ++i) {
                data[i] = other.data[i];
            }
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
                other.data = nullptr;
            }
            return *this;
        }

        // Fast bit manipulation without bounds checking
        inline void set_unsafe(size_t index, uint8_t value) {
            value &= (1 << BitsPerElement) - 1;
            size_t bitPos = index * BitsPerElement;
            size_t byteIdx = bitPos >> 3;  // Faster than /8
            size_t bitOff = bitPos & 7;    // Faster than %8
            
            if (bitOff + BitsPerElement <= 8) {
                uint8_t mask = ((1 << BitsPerElement) - 1) << bitOff;
                data[byteIdx] = (data[byteIdx] & ~mask) | (value << bitOff);
            } else {
                uint8_t bitsInFirstByte = 8 - bitOff;
                uint8_t bitsInSecondByte = BitsPerElement - bitsInFirstByte;
                
                uint8_t mask1 = ((1 << bitsInFirstByte) - 1) << bitOff;
                uint8_t mask2 = (1 << bitsInSecondByte) - 1;
                
                data[byteIdx] = (data[byteIdx] & ~mask1) | ((value & ((1 << bitsInFirstByte) - 1)) << bitOff);
                data[byteIdx + 1] = (data[byteIdx + 1] & ~mask2) | (value >> bitsInFirstByte);
            }
        }

        inline uint8_t get_unsafe(size_t index) const {
            size_t bitPos = index * BitsPerElement;
            size_t byteIdx = bitPos >> 3;  // Faster than /8
            size_t bitOff = bitPos & 7;    // Faster than %8
            
            if (bitOff + BitsPerElement <= 8) {
                return (data[byteIdx] >> bitOff) & ((1 << BitsPerElement) - 1);
            } else {
                uint8_t bitsInFirstByte = 8 - bitOff;
                uint8_t bitsInSecondByte = BitsPerElement - bitsInFirstByte;
                
                uint8_t firstPart = (data[byteIdx] >> bitOff) & ((1 << bitsInFirstByte) - 1);
                uint8_t secondPart = (data[byteIdx + 1] & ((1 << bitsInSecondByte) - 1)) << bitsInFirstByte;
                
                return firstPart | secondPart;
            }
        }

        // Fast memory copy for resize operations
        void copy_elements(const PackedArray& src, size_t element_count) {
            if (element_count == 0) return;
            size_t bits = element_count * BitsPerElement;
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


    template <uint8_t BitsPerValue = 1>
    class ID_vector{
        static_assert(BitsPerValue > 0 && BitsPerValue <= 8, "BitsPerValue must be between 1 and 8");
    public:
        using count_type = uint8_t; // type for storing count of each ID
    private:
        PackedArray<BitsPerValue> id_array; // BitsPerValue bits per ID
        size_t capacity_bits = 0; // total bits allocated (== max storable id + 1)
        size_t max_id_ = 0; // maximum ID that can be stored
        size_t size_ = 0; // total number of ID instances stored

        constexpr static size_t MAX_RF_ID = 536870912; // 2^29
        constexpr static size_t DEFAULT_MAX_ID = 127; // default max ID (128 bits -> 16 bytes)
        constexpr static count_type MAX_COUNT = (1 << BitsPerValue) - 1; // maximum count per ID

        static constexpr size_t bits_to_bytes(size_t bits){ return (bits + 7) >> 3; }

        void allocate_bits(size_t bits){
            size_t total_bits = bits * BitsPerValue; // multiply by bits per value
            size_t bytes = bits_to_bytes(total_bits);
            id_array = PackedArray<BitsPerValue>(bytes);
            capacity_bits = bits;
        }

    public:
        // Set maximum ID that can be stored and allocate memory accordingly
        void set_maxID(size_t max_id) {
            if(max_id >= MAX_RF_ID){
                throw std::out_of_range("Max RF ID exceeds limit");
            }
            max_id_ = max_id;
            allocate_bits(max_id + 1); // need bit (max_id) inclusive
        }

        // Get current maximum ID
        size_t get_maxID() const { return max_id_; }

        // default constructor (default max ID 127 -> 128 bits -> 16 bytes)
        ID_vector(){
            set_maxID(DEFAULT_MAX_ID);
        }

        // constructor with max expected ID - calls set_maxID automatically
        explicit ID_vector(size_t max_id){
            set_maxID(max_id);
        }

        // number of stored IDs
        size_t size() const { return size_; }
        bool empty() const { return size_ == 0; }
        size_t capacity() const { return capacity_bits; }

        // check presence
        bool contains(size_t id) const {
            if(id >= capacity_bits) return false;
            return id_array.get(id) != 0;
        }

        // insert ID (order independent, data structure is inherently sorted)
        void push_back(size_t id){
            if(id > max_id_){
                throw std::out_of_range("ID exceeds maximum allowed ID");
            }
            count_type current_count = id_array.get(id);
            if(current_count < MAX_COUNT){
                id_array.set(id, current_count + 1);
                ++size_;
            } // if already at max count, ignore (do nothing)
        }

        // get count of specific ID
        count_type count(size_t id) const {
            if(id >= capacity_bits) return 0;
            return id_array.get(id);
        }

        // remove one instance of specific ID (if exists)
        bool erase(size_t id){
            if(id >= capacity_bits) return false;
            count_type current_count = id_array.get(id);
            if(current_count > 0){
                id_array.set(id, current_count - 1);
                --size_;
                return true;
            }
            return false;
        }

        // remove all instances of specific ID (if exists)
        bool erase_all(size_t id){
            if(id >= capacity_bits) return false;
            count_type current_count = id_array.get(id);
            if(current_count > 0){
                id_array.set(id, 0);
                size_ -= current_count; // subtract all instances
                return true;
            }
            return false;
        }

        // largest ID in the vector (if empty, throws)
        size_t back() const {
            if(size_ == 0) throw std::out_of_range("ID_vector is empty");
            
            // Find the highest ID with count > 0
            for(size_t id = capacity_bits; id > 0; --id) {
                if(id_array.get(id - 1) > 0) {
                    return id - 1;
                }
            }
            throw std::out_of_range("ID_vector::back() internal error");
        }

        // pop largest ID (remove one instance)
        void pop_back(){
            if(size_ == 0) return; // empty
            
            // Find the highest ID with count > 0 and decrement
            for(size_t id = capacity_bits; id > 0; --id) {
                count_type current_count = id_array.get(id - 1);
                if(current_count > 0) {
                    id_array.set(id - 1, current_count - 1);
                    --size_;
                    return;
                }
            }
        }

        void clear(){
            size_t total_bits = capacity_bits * BitsPerValue;
            size_t bytes = bits_to_bytes(total_bits);
            uint8_t* data = id_array.raw_data();
            for(size_t i=0;i<bytes;++i) data[i] = 0;
            size_ = 0;
        }

        // nth element (0-based) among all ID instances (in ascending order)
        // When an ID appears multiple times, it will be returned multiple times
        size_t operator[](size_t index) const {
            if(index >= size_) throw std::out_of_range("ID_vector::operator[] index out of range");
            
            size_t current_count = 0;
            for(size_t id = 0; id < capacity_bits; ++id) {
                count_type id_count = id_array.get(id);
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
            size_t current_id = 0; // Current ID being processed
            count_type remaining_count = 0; // Remaining instances of current ID

            void find_first() {
                current_id = 0;
                remaining_count = 0;
                
                if (!vec) {
                    current_id = 0;
                    remaining_count = 0;
                    return;
                }

                // Find the first ID with count > 0, starting from 0
                while (current_id < vec->capacity_bits) {
                    count_type id_count = vec->id_array.get(current_id);
                    if (id_count > 0) {
                        remaining_count = id_count - 1; // -1 because we're returning this instance
                        return;
                    }
                    ++current_id;
                }

                // No IDs found
                current_id = vec->capacity_bits;
                remaining_count = 0;
            }

            void find_next() {
                if (!vec) {
                    current_id = vec ? vec->capacity_bits : 0;
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
                while (current_id < vec->capacity_bits) {
                    count_type id_count = vec->id_array.get(current_id);
                    if (id_count > 0) {
                        remaining_count = id_count - 1; // -1 because we're returning this instance
                        return;
                    }
                    ++current_id;
                }

                // No more IDs found
                current_id = vec->capacity_bits;
                remaining_count = 0;
            }

        public:
            using value_type = size_t;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::forward_iterator_tag;
            using pointer = const value_type*;
            using reference = const value_type&;

            iterator() : vec(nullptr), current_id(0), remaining_count(0) {}

            // Constructor for begin() and end()
            iterator(const ID_vector* v, bool is_end) : vec(v), current_id(0), remaining_count(0) {
                if (!v || v->size_ == 0 || is_end) {
                    current_id = v ? v->capacity_bits : 0;
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
    };

    // 