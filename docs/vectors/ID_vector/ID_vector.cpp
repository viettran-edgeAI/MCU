
#include <iostream>
#include <stdexcept>
#include "../../src/initializer_list.h"
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
            if(data == nullptr) return; // Safety check
            
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
            if(data == nullptr) return 0; // Safety check
            
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
            (sizeof(index_type) == 1), uint32_t,   // uint8_t -> uint32_t (4 bytes)
            typename conditional_t<
                (sizeof(index_type) == 2), uint64_t,   // uint16_t -> uint64_t (8 bytes)
                typename conditional_t<
                    (sizeof(index_type) == 4), size_t,     // uint32_t -> size_t
                    size_t  // Default to size_t for larger types
                >::type
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

        static constexpr size_t bits_to_bytes(size_t bits){ return (bits + 7) >> 3; }

        void allocate_bits(){
            size_t range = (size_t)max_id_ - (size_t)min_id_ + 1; // number of IDs in range (use size_t to avoid overflow)
            size_t total_bits = range * BitsPerValue; // multiply by bits per value
            size_t bytes = bits_to_bytes(total_bits);
            id_array = PackedArray<BitsPerValue>(bytes);
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
                size_t old_range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
                size_t old_total_bits = old_range * BitsPerValue;
                size_t old_bytes = bits_to_bytes(old_total_bits);
                PackedArray<BitsPerValue> old_array(old_bytes);
                old_array.copy_from(id_array, old_bytes);
                
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
                size_t old_range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
                size_t old_total_bits = old_range * BitsPerValue;
                size_t old_bytes = bits_to_bytes(old_total_bits);
                PackedArray<BitsPerValue> old_array(old_bytes);
                old_array.copy_from(id_array, old_bytes);
                
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
                size_t old_range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
                size_t old_total_bits = old_range * BitsPerValue;
                size_t old_bytes = bits_to_bytes(old_total_bits);
                PackedArray<BitsPerValue> old_array(old_bytes);
                old_array.copy_from(id_array, old_bytes);
                
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

        // Get current minimum ID that can be stored
        index_type get_minID() const {
            return min_id_;
        }

        // Get current maximum ID that can be stored  
        index_type get_maxID() const {
            return max_id_;
        }

        // get the smallest ID currently stored in the vector
        index_type minID(){
            if(size_ == 0) {
                throw std::out_of_range("ID_vector is empty");
            }
            // Find the lowest ID with count > 0
            for(index_type id = min_id_; id <= max_id_; ++id) {
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
            size_t range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
            size_t total_bits = range * BitsPerValue;
            size_t bytes = bits_to_bytes(total_bits);
            id_array = PackedArray<BitsPerValue>(bytes);
            id_array.copy_from(other.id_array, bytes);
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
                
                size_t range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
                size_t total_bits = range * BitsPerValue;
                size_t bytes = bits_to_bytes(total_bits);
                id_array = PackedArray<BitsPerValue>(bytes);
                id_array.copy_from(other.id_array, bytes);
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

        // number of stored IDs
        size_type size() const { return size_; }
        bool empty() const { return size_ == 0; }

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

        void clear(){
            if(size_ == 0) return; // Already empty
            
            size_t range = (size_t)max_id_ - (size_t)min_id_ + 1; // Use size_t to avoid overflow
            size_t total_bits = range * BitsPerValue;
            size_t bytes = bits_to_bytes(total_bits);
            uint8_t* data = id_array.raw_data();
            if(data != nullptr) {
                for(size_t i=0;i<bytes;++i) data[i] = 0;
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

        // takeout normalized vector of IDs (ascending order, no repetitions)
    };

    // 