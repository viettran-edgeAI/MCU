// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include "hash_kernel.h"
#include "initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>

// #include <cstring>
// #include <iostream>
namespace mcu {
// vector with small buffer optimization (SBO)
    template<typename T, index_size_flag SizeFlag = index_size_flag::MEDIUM, size_t sboSize = 0>
    class b_vector : hash_kernel {
    private:
        using vector_index_type = typename vector_index_type<SizeFlag>::type;
        
        // Small Buffer Optimization - internal buffer size based on template parameter or index type
        static constexpr vector_index_type SBO_SIZE = 
            (sboSize > 0) ? static_cast<vector_index_type>(sboSize) :
            (std::is_same<vector_index_type, uint8_t>::value) ? 8 :
            (std::is_same<vector_index_type, uint16_t>::value) ? 16 : 32;
        
        // Union to save memory - either use internal buffer or heap pointer
        union {
            T* heap_array;
            alignas(T) char buffer[sizeof(T) * SBO_SIZE];
        };
        
        vector_index_type size_    = 0;
        vector_index_type capacity_ = SBO_SIZE;
        bool using_heap = false;  // Track whether we're using heap or buffer
        
        // based on vector_index_type, set the maximum capacity
        static constexpr int VECTOR_MAX_CAP = 
            (std::is_same<vector_index_type, uint8_t>::value) ? 255 :
            (std::is_same<vector_index_type, uint16_t>::value) ? 65535 :
            2000000000; // for uint32_t and larger types

        // Get pointer to current data (buffer or heap)
        T* data_ptr() noexcept {
            return using_heap ? heap_array : reinterpret_cast<T*>(buffer);
        }
        
        const T* data_ptr() const noexcept {
            return using_heap ? heap_array : reinterpret_cast<const T*>(buffer);
        }
        
        // Switch from buffer to heap storage
        void switch_to_heap(vector_index_type new_capacity) noexcept {
            if (using_heap) return;
            
            T* old_buffer_ptr = reinterpret_cast<T*>(buffer);
            T* new_heap = new T[new_capacity];
            
            // Copy elements from buffer to heap
            customCopy(old_buffer_ptr, new_heap, size_);
            
            heap_array = new_heap;
            capacity_ = new_capacity;
            using_heap = true;
        }

        // Internal helper: copy 'count' elements from src to dst
        void customCopy(const T* src, T* dst, vector_index_type count) noexcept {
            for (vector_index_type i = 0; i < count; ++i) {
                dst[i] = src[i];
            }
        }

    public:
        // Default: use internal buffer
        b_vector() noexcept : size_(0), capacity_(SBO_SIZE), using_heap(false) {
            // Initialize buffer memory
            for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                new(reinterpret_cast<T*>(buffer) + i) T();
            }
        }

        // Constructor with initial capacity
        explicit b_vector(vector_index_type initialCapacity) noexcept : size_(initialCapacity) {
            if (initialCapacity <= SBO_SIZE) {
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T();
                }
            } else {
                using_heap = true;
                capacity_ = initialCapacity;
                heap_array = new T[initialCapacity];
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = T();
                }
            }
        }

        // Constructor with initial size and value
        explicit b_vector(vector_index_type initialCapacity, const T& value) noexcept : size_(initialCapacity) {
            if (initialCapacity <= SBO_SIZE) {
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T(i < size_ ? value : T());
                }
            } else {
                using_heap = true;
                capacity_ = initialCapacity;
                heap_array = new T[initialCapacity];
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = value;
                }
            }
        }

        // Constructor: from min_init_list<T>
        b_vector(const min_init_list<T>& init) noexcept : size_(init.size()) {
            if (init.size() <= SBO_SIZE) {
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T(i < size_ ? init.data_[i] : T());
                }
            } else {
                using_heap = true;
                capacity_ = init.size();
                heap_array = new T[capacity_];
                for (unsigned i = 0; i < init.size(); ++i)
                    heap_array[i] = init.data_[i];
            }
        }

        // Copy constructor
        b_vector(const b_vector& other) noexcept : size_(other.size_), using_heap(other.using_heap) {
            if (other.using_heap) {
                capacity_ = other.capacity_;
                heap_array = new T[capacity_];
                customCopy(other.heap_array, heap_array, size_);
            } else {
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                const T* other_buffer_ptr = reinterpret_cast<const T*>(other.buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T(other_buffer_ptr[i]);
                }
            }
        }

        // Move constructor
        b_vector(b_vector&& other) noexcept : size_(other.size_), capacity_(other.capacity_), using_heap(other.using_heap) {
            if (other.using_heap) {
                heap_array = other.heap_array;
                other.heap_array = nullptr;
            } else {
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                T* other_buffer_ptr = reinterpret_cast<T*>(other.buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T(std::move(other_buffer_ptr[i]));
                }
            }
            other.size_ = 0;
            other.capacity_ = SBO_SIZE;
            other.using_heap = false;
        }

        // Destructor
        ~b_vector() noexcept {
            if (using_heap) {
                delete[] heap_array;
            } else {
                // FIX: Manually call destructor for constructed objects in buffer to prevent leaks.
                // This assumes the flawed but consistent model of initializing the whole buffer.
                T* p = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    p[i].~T();
                }
            }
        }

        // Copy assignment
        b_vector& operator=(const b_vector& other) noexcept {
            if (this == &other) {
                return *this;
            }

            if (other.using_heap) {
                // `other` is on the heap. We need to allocate a new heap buffer.
                // To ensure safety, allocate and copy to a temporary buffer *before* modifying `this`.
                T* new_heap = new T[other.capacity_];
                if (!new_heap) {
                    // Allocation failed. Leave `this` unmodified to maintain a valid state.
                    return *this;
                }
                customCopy(other.heap_array, new_heap, other.size_);

                // New data is ready. Now, safely destroy the old data.
                if (using_heap) {
                    delete[] heap_array;
                } else {
                    T* p = reinterpret_cast<T*>(buffer);
                    for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                        p[i].~T();
                    }
                }

                // Commit the new state.
                heap_array = new_heap;
                capacity_ = other.capacity_;
                size_ = other.size_;
                using_heap = true;
            } else {
                // `other` is using SBO. `this` will also use SBO. No allocation needed.
                if (using_heap) {
                    // `this` is on the heap, so deallocate it.
                    delete[] heap_array;
                    // The SBO buffer is now uninitialized, so we must use placement-new.
                    T* this_buf = reinterpret_cast<T*>(buffer);
                    const T* other_buf = reinterpret_cast<const T*>(other.buffer);
                    for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                        new (&this_buf[i]) T(other_buf[i]);
                    }
                } else {
                    // Both `this` and `other` are SBO. Objects are already constructed.
                    // We can perform a more efficient element-wise assignment.
                    T* this_buf = reinterpret_cast<T*>(buffer);
                    const T* other_buf = reinterpret_cast<const T*>(other.buffer);
                    for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                        this_buf[i] = other_buf[i];
                    }
                }

                // Commit the new state.
                size_ = other.size_;
                capacity_ = SBO_SIZE;
                using_heap = false;
            }

            return *this;
        }

        // Move assignment
        b_vector& operator=(b_vector&& other) noexcept {
            if (this != &other) {
                // First, correctly destroy the elements in `this`
                if (using_heap) {
                    delete[] heap_array;
                } else {
                    T* p = reinterpret_cast<T*>(buffer);
                    for (vector_index_type i = 0; i < SBO_SIZE; ++i) p[i].~T();
                }
                
                // Steal other's data
                size_ = other.size_;
                capacity_ = other.capacity_;
                using_heap = other.using_heap;
                
                if (other.using_heap) {
                    heap_array = other.heap_array;
                    other.heap_array = nullptr;
                } else {
                    // CRASH FIX: Use placement-new to move-construct into our uninitialized buffer.
                    T* this_buf = reinterpret_cast<T*>(buffer);
                    T* other_buf = reinterpret_cast<T*>(other.buffer);
                    for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                        new (&this_buf[i]) T(std::move(other_buf[i]));
                    }
                }
                
                // Reset other
                other.size_ = 0;
                other.capacity_ = SBO_SIZE;
                other.using_heap = false;
            }
            return *this;
        }
        void fill(const T& value) noexcept {
            T* ptr = data_ptr();
            for (vector_index_type i = 0; i < size_; ++i) {
                ptr[i] = value;
            }
        }

        // Reserve at least newCapacity
        void reserve(vector_index_type newCapacity) noexcept {
            if (newCapacity > capacity_) {
                if (newCapacity > SBO_SIZE && !using_heap) {
                    switch_to_heap(newCapacity);
                } else if (using_heap) {
                    resize(newCapacity);
                }
            }
        }

        // Append element
        void push_back(const T& value) noexcept {
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 10 : 1;
                else
                    doubled = capacity_ ? capacity_ * 2 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                
                if (doubled > SBO_SIZE && !using_heap) {
                    switch_to_heap(doubled);
                } else if (using_heap) {
                    resize(doubled);
                }
            }
            data_ptr()[size_++] = value;
        }

        // Insert at position
        void insert(vector_index_type pos, const T& value) noexcept {
            if (pos > size_) return;
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 10 : 1;
                else
                    doubled = capacity_ ? capacity_ * 2 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                
                if (doubled > SBO_SIZE && !using_heap) {
                    switch_to_heap(doubled);
                } else if (using_heap) {
                    resize(doubled);
                }
            }
            T* ptr = data_ptr();
            for (vector_index_type i = size_; i > pos; --i) {
                ptr[i] = ptr[i - 1];
            }
            ptr[pos] = value;
            ++size_;
        }

        template<typename InputIterator>
        void insert(const T* position, InputIterator first, InputIterator last) noexcept {
            vector_index_type pos = position - data_ptr();
            vector_index_type count = last - first;
            if (pos > size_) return;
            if (size_ + count > capacity_) {
                size_t newCapacity = capacity_ ? capacity_ * 2 : 1;
                if (newCapacity > VECTOR_MAX_CAP) newCapacity = VECTOR_MAX_CAP;
                
                if (newCapacity > SBO_SIZE && !using_heap) {
                    switch_to_heap(newCapacity);
                } else if (using_heap) {
                    resize(newCapacity);
                }
            }
            T* ptr = data_ptr();
            for (vector_index_type i = size_ + count - 1; i >= pos + count; --i) {
                ptr[i] = ptr[i - count];
            }
            for (vector_index_type i = 0; i < count; ++i) {
                ptr[pos + i] = *(first + i);
            }
            size_ += count;
        }

        // Erase element at position
        void erase(vector_index_type pos) noexcept {
            if (pos >= size_) return;
            T* ptr = data_ptr();
            customCopy(ptr + pos + 1, ptr + pos, size_ - pos - 1);
            --size_;
        }

        bool empty() const noexcept {
            return size_ == 0;
        }

        // Clear contents (keep capacity)
        void clear() noexcept {
            size_ = 0;
        }

        // Shrink capacity to fit size
        void fit() noexcept {
            if (size_ < capacity_) resize(size_);
        }

        T& back() noexcept {
            assert(!empty() && "b_vector::back() called on empty vector");
            // Remove the dangerous fallback - just assert and return last valid element
            return data_ptr()[size_ - 1];
        }

        const T& back() const noexcept {
            assert(!empty() && "b_vector::back() called on empty vector");
            // Remove the dangerous fallback - just assert and return last valid element
            return data_ptr()[size_ - 1];
        }
        T& front() noexcept {
            assert(!empty() && "b_vector::front() called on empty vector");
            // Remove the dangerous fallback - just assert and return first valid element
            return data_ptr()[0];
        }

        void pop_back() noexcept {
            if (empty()) {
                return;
            }
            --size_;
            // For embedded systems with simple types, often no explicit destructor needed
            // Only call destructor if T has non-trivial destructor
            if constexpr (!std::is_trivially_destructible_v<T>) {
                data_ptr()[size_].~T();
            }
        }
        void sort() noexcept {
            // Safety check: null data pointer
            T* ptr = data_ptr();
            if (ptr == nullptr) return;
            
            // Safety check: basic size validation
            if (size_ <= 1) return;
            
            // Safety check: size consistency
            if (size_ > capacity_) {
                size_ = capacity_; // Fix corrupted size
            }
            
            // Safety check: prevent integer overflow on large arrays
            if (size_ >= VECTOR_MAX_CAP) return;
            
            quickSort(0, size_ - 1);
        }
        
    private:

        bool is_less(const T& a, const T& b) noexcept {
            if constexpr (std::is_arithmetic_v<T>) {
                // For numeric types, direct comparison
                return a < b;
            } else {
                // For non-numeric types, use hash preprocessing
                size_t hash_a = this->preprocess_hash_input(a);
                size_t hash_b = this->preprocess_hash_input(b);
                return hash_a < hash_b;
            }
        }

        // Partition function with comprehensive safety checks
        vector_index_type partition(vector_index_type low, vector_index_type high) noexcept {
            T* ptr = data_ptr();
            
            // Safety: null pointer check
            if (ptr == nullptr) return low;
            
            // Safety: boundary validation
            if (low >= size_ || high >= size_) return low;
            if (low > high) return low;
            
            // Safety: handle unsigned underflow case
            if (high == 0 && low > 0) return low;
            
            T pivot = ptr[high];
            vector_index_type i = low;
            
            // Safety: bound-checked loop
            for (vector_index_type j = low; j < high && j < size_; ++j) {
                // Additional bounds check during iteration
                if (i >= size_) break;
                
                if (is_less(ptr[j], pivot)) {
                    // Safety: validate both indices before swap
                    if (i < size_ && j < size_) {
                        T temp = ptr[i];
                        ptr[i] = ptr[j];
                        ptr[j] = temp;
                    }
                    ++i;
                    
                    // Safety: prevent index overflow
                    if (i >= size_) break;
                }
            }
            
            // Safety: final bounds check before pivot placement
            if (i < size_ && high < size_) {
                T temp = ptr[i];
                ptr[i] = ptr[high];
                ptr[high] = temp;
            }
            
            return i;
        }
        
        // Quicksort with stack overflow protection and safety checks
        void quickSort(vector_index_type low, vector_index_type high) noexcept {
            T* ptr = data_ptr();
            
            // Safety: null pointer check
            if (ptr == nullptr) return;
            
            // Safety: boundary validation
            if (low >= size_ || high >= size_) return;
            if (low >= high) return;
            
            // Safety: stack overflow protection
            static uint8_t recursion_depth = 0;
            const uint8_t MAX_RECURSION_DEPTH = 24; // Conservative limit for embedded
            
            if (recursion_depth >= MAX_RECURSION_DEPTH) {
                // Fall back to iterative bubble sort for safety
                bubbleSortFallback(low, high);
                return;
            }
            
            // Safety: detect infinite recursion patterns
            if (high - low > size_) return; // Invalid range
            
            ++recursion_depth;
            
            vector_index_type pivotIndex = partition(low, high);
            
            // Safety: validate pivot index before recursive calls
            if (pivotIndex >= low && pivotIndex <= high && pivotIndex < size_) {
                // Sort left partition with underflow protection
                if (pivotIndex > low && pivotIndex > 0) {
                    quickSort(low, pivotIndex - 1);
                }
                // Sort right partition  
                if (pivotIndex < high && pivotIndex + 1 < size_) {
                    quickSort(pivotIndex + 1, high);
                }
            }
            
            --recursion_depth;
        }
        
        // Safe fallback sorting when recursion limit reached
        void bubbleSortFallback(vector_index_type low, vector_index_type high) noexcept {
            T* ptr = data_ptr();
            
            // Safety: basic validation
            if (ptr == nullptr || low >= high || high >= size_) return;
            
            // Safety: prevent infinite loops
            const vector_index_type max_iterations = (high - low + 1) * (high - low + 1);
            vector_index_type iteration_count = 0;
            
            for (vector_index_type i = low; i <= high && i < size_; ++i) {
                for (vector_index_type j = low; j < high - (i - low) && j < size_; ++j) {
                    // Safety: iteration limit check
                    if (++iteration_count > max_iterations) return;
                    
                    // Safety: bounds check for each access
                    if (j + 1 <= high && j < size_ && j + 1 < size_) {
                        if (!is_less(ptr[j], ptr[j + 1]) && !is_less(ptr[j + 1], ptr[j])) {
                            // Elements are equal, no swap needed
                        } else if (!is_less(ptr[j], ptr[j + 1])) {
                            T temp = ptr[j];
                            ptr[j] = ptr[j + 1];
                            ptr[j + 1] = temp;
                        }
                    }
                }
            }
        }
    public:

        // Returns a pointer such that [data(), data() + size()) is a valid range. For a non-empty %b_vector, data() == &front().
        T* data() noexcept { return data_ptr(); }
        const T* data() const noexcept { return data_ptr(); }

        // Internal resize: allocate newCapacity, copy old data, free old (only for heap storage)
        void resize(vector_index_type newCapacity) noexcept {
            if (!using_heap || newCapacity == capacity_) return;
            if (newCapacity == 0) newCapacity = 1;
            T* newArray = new T[newCapacity];
            vector_index_type toCopy = (size_ < newCapacity ? size_ : newCapacity);
            customCopy(heap_array, newArray, toCopy);
            delete[] heap_array;
            heap_array = newArray;
            capacity_ = newCapacity;
            if (size_ > capacity_) size_ = capacity_;
        }

        void extend(vector_index_type newCapacity) noexcept {
            if (newCapacity > capacity_) {
                if (newCapacity > SBO_SIZE && !using_heap) {
                    switch_to_heap(newCapacity);
                } else if (using_heap) {
                    resize(newCapacity);
                }
            }
        }

        // Accessors
        vector_index_type size() const noexcept { return size_; }
        vector_index_type cap() const noexcept { return capacity_; }

        // Operator[] with improved bounds checking
        T& operator[](vector_index_type index) noexcept {
            // Safety: Check for empty vector first
            if (size_ == 0) {
                // For empty vector, return a static default value to prevent crashes
                static T default_value{};
                return default_value;
            }
            
            // Safety: Bounds check with proper index validation
            if (index >= size_) {
                // Return last valid element instead of asserting for embedded safety
                return data_ptr()[size_ - 1];
            }
            
            // Safety: Verify data pointer is valid
            T* ptr = data_ptr();
            if (ptr == nullptr) {
                static T default_value{};
                return default_value;
            }
            
            return ptr[index];
        }
    
        const T& operator[](vector_index_type index) const noexcept {
            // Safety: Check for empty vector first
            if (size_ == 0) {
                static const T default_value{};
                return default_value;
            }
            
            // Safety: Bounds check with proper index validation
            if (index >= size_) {
                // Return last valid element instead of asserting for embedded safety
                return data_ptr()[size_ - 1];
            }
            
            // Safety: Verify data pointer is valid
            const T* ptr = data_ptr();
            if (ptr == nullptr) {
                static const T default_value{};
                return default_value;
            }
            
            return ptr[index];
        }

        // Add safe at() method with bounds checking for debugging
        T& at(vector_index_type index) noexcept {
            // This method can use assert for debugging while operator[] is safe
            assert(index < size_ && "b_vector::at() index out of range");
            assert(size_ > 0 && "b_vector::at() called on empty vector");
            assert(data_ptr() != nullptr && "b_vector::at() null data pointer");
            
            if (index >= size_ || size_ == 0) {
                static T default_value{};
                return default_value;
            }
            
            return data_ptr()[index];
        }

        const T& at(vector_index_type index) const noexcept {
            assert(index < size_ && "b_vector::at() index out of range");
            assert(size_ > 0 && "b_vector::at() called on empty vector");
            assert(data_ptr() != nullptr && "b_vector::at() null data pointer");
            
            if (index >= size_ || size_ == 0) {
                static const T default_value{};
                return default_value;
            }
            
            return data_ptr()[index];
        }

        // Iterators (raw pointers)
        T* begin() noexcept { return data_ptr(); }
        T* end()   noexcept { return data_ptr() + size_; }
        const T* begin() const noexcept { return data_ptr(); }
        const T* end()   const noexcept { return data_ptr() + size_; }
    };


    template<typename T, index_size_flag SizeFlag = index_size_flag::MEDIUM>
    class vector : hash_kernel{
    private:
        using vector_index_type = typename vector_index_type<SizeFlag>::type;
        T*      array    = nullptr;
        vector_index_type size_    = 0;
        vector_index_type capacity_ = 0;
        
        // based on vector_index_type, set the maximum capacity
        static constexpr int VECTOR_MAX_CAP = 
            (std::is_same<vector_index_type, uint8_t>::value) ? 255 :
            (std::is_same<vector_index_type, uint16_t>::value) ? 65535 :
            2000000000; // for uint32_t and larger types


        // Internal helper: copy 'count' elements from src to dst
        void customCopy(const T* src, T* dst, vector_index_type count) noexcept {
            for (vector_index_type i = 0; i < count; ++i) {
                dst[i] = src[i];
            }
        }

    public:
        // Default: allocate capacity=1
        vector() noexcept
            : array(new T[1]), size_(0), capacity_(1) {}

        // Constructor with initial capacity
        explicit vector(vector_index_type initialCapacity) noexcept
            : array(new T[(initialCapacity == 0) ? 1 : initialCapacity]),
            size_(initialCapacity), // Set size equal to capacity
            capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            // fix for error : access elements throgh operator[] when vector just initialized
            for (vector_index_type i = 0; i < size_; ++i) {
                array[i] = T();
            }
        }

        // Constructor with initial size and value
        explicit vector(vector_index_type initialCapacity, const T& value) noexcept
            : array(new T[(initialCapacity == 0) ? 1 : initialCapacity]),
            size_(initialCapacity), capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            for (vector_index_type i = 0; i < size_; ++i) {
                array[i] = value;
            }
        }

        // Constructor: from min_init_list<T>
        vector(const min_init_list<T>& init) noexcept
            : array(new T[init.size()]), size_(init.size()), capacity_(init.size()) {
            for (unsigned i = 0; i < init.size(); ++i)
                array[i] = init.data_[i];
        }

        // Copy constructor
        vector(const vector& other) noexcept
            : array(new T[other.capacity_]),
            size_(other.size_), capacity_(other.capacity_) {
            customCopy(other.array, array, size_);
        }

        // Move constructor
        vector(vector&& other) noexcept
            : array(other.array), size_(other.size_), capacity_(other.capacity_) {
            other.array    = nullptr;
            other.size_    = 0;
            other.capacity_ = 0;
        }

        // Destructor
        ~vector() noexcept {
            delete[] array;
        }

        // Copy assignment
        vector& operator=(const vector& other) noexcept {
            if (this != &other) {
                T* newArray = new T[other.capacity_];
                customCopy(other.array, newArray, other.size_);
                delete[] array;
                array    = newArray;
                size_    = other.size_;
                capacity_ = other.capacity_;
            }
            return *this;
        }

        // Move assignment
        vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                delete[] array;
                array      = other.array;
                size_      = other.size_;
                capacity_  = other.capacity_;
                other.array    = nullptr;
                other.size_    = 0;
                other.capacity_ = 0;
            }
            return *this;
        }

        // Reserve at least newCapacity
        void reserve(vector_index_type newCapacity) noexcept {
            if (newCapacity > capacity_) resize(newCapacity);
        }

        // Append element
        void push_back(const T& value) noexcept {
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 10 : 1;
                else
                    doubled = capacity_ ? capacity_ * 20 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                resize(doubled);
            }
            array[size_++] = value;
        }

        // Insert at position
        void insert(vector_index_type pos, const T& value) noexcept {
            if (pos > size_) return;
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 10 : 1;
                else
                    doubled = capacity_ ? capacity_ * 20 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                resize(doubled);
            }
            for (vector_index_type i = size_; i > pos; --i) {
                array[i] = array[i - 1];
            }
            array[pos] = value;
            ++size_;
        }
        /*
        Inserts a range into the %vector.

        Parameters:
        __position – A const_iterator into the %vector.
        __first – An input iterator.
        __last – An input iterator.
        */
        template<typename InputIterator>
        void insert(const T* position, InputIterator first, InputIterator last) noexcept {
            vector_index_type pos = position - array;
            vector_index_type count = last - first;
            if (pos > size_) return;
            if (size_ + count > capacity_) {
                size_t newCapacity = capacity_ ? capacity_ * 2 : 1;
                if (newCapacity > VECTOR_MAX_CAP) newCapacity = VECTOR_MAX_CAP;
                resize(newCapacity);
            }
            for (vector_index_type i = size_ + count - 1; i >= pos + count; --i) {
                array[i] = array[i - count];
            }
            for (vector_index_type i = 0; i < count; ++i) {
                array[pos + i] = *(first + i);
            }
            size_ += count;
        }

        void sort() noexcept {
            // Safety check: null array pointer
            if (array == nullptr) return;
            
            // Safety check: basic size validation
            if (size_ <= 1) return;
            
            // Safety check: size consistency
            if (size_ > capacity_) {
                size_ = capacity_; // Fix corrupted size
            }
            
            // Safety check: prevent integer overflow on large arrays
            if (size_ >= VECTOR_MAX_CAP) return;
            
            quickSort(0, size_ - 1);
        }
    private:
        // Helper function to compare two elements, using preprocessing for non-numeric types
        bool is_less(const T& a, const T& b) noexcept {
            if constexpr (std::is_arithmetic_v<T>) {
                // For numeric types, direct comparison
                return a < b;
            } else {
                // For non-numeric types, use hash preprocessing
                size_t hash_a = this->preprocess_hash_input(a);
                size_t hash_b = this->preprocess_hash_input(b);
                return hash_a < hash_b;
            }
        }

        // Partition function with comprehensive safety checks
        vector_index_type partition(vector_index_type low, vector_index_type high) noexcept {
            // Safety: null pointer check
            if (array == nullptr) return low;
            
            // Safety: boundary validation
            if (low >= size_ || high >= size_) return low;
            if (low > high) return low;
            
            // Safety: handle unsigned underflow case
            if (high == 0 && low > 0) return low;
            
            T pivot = array[high];
            vector_index_type i = low;
            
            // Safety: bound-checked loop
            for (vector_index_type j = low; j < high && j < size_; ++j) {
                // Additional bounds check during iteration
                if (i >= size_) break;
                
                if (is_less(array[j], pivot)) {
                    // Safety: validate both indices before swap
                    if (i < size_ && j < size_) {
                        T temp = array[i];
                        array[i] = array[j];
                        array[j] = temp;
                    }
                    ++i;
                    
                    // Safety: prevent index overflow
                    if (i >= size_) break;
                }
            }
            
            // Safety: final bounds check before pivot placement
            if (i < size_ && high < size_) {
                T temp = array[i];
                array[i] = array[high];
                array[high] = temp;
            }
            
            return i;
        }
        
        // Quicksort with stack overflow protection and safety checks
        void quickSort(vector_index_type low, vector_index_type high) noexcept {
            // Safety: null pointer check
            if (array == nullptr) return;
            
            // Safety: boundary validation
            if (low >= size_ || high >= size_) return;
            if (low >= high) return;
            
            // Safety: stack overflow protection
            static uint8_t recursion_depth = 0;
            const uint8_t MAX_RECURSION_DEPTH = 24; // Conservative limit for embedded
            
            if (recursion_depth >= MAX_RECURSION_DEPTH) {
                // Fall back to iterative bubble sort for safety
                bubbleSortFallback(low, high);
                return;
            }
            
            // Safety: detect infinite recursion patterns
            if (high - low > size_) return; // Invalid range
            
            ++recursion_depth;
            
            vector_index_type pivotIndex = partition(low, high);
            
            // Safety: validate pivot index before recursive calls
            if (pivotIndex >= low && pivotIndex <= high && pivotIndex < size_) {
                // Sort left partition with underflow protection
                if (pivotIndex > low && pivotIndex > 0) {
                    quickSort(low, pivotIndex - 1);
                }
                // Sort right partition
                if (pivotIndex < high && pivotIndex + 1 < size_) {
                    quickSort(pivotIndex + 1, high);
                }
            }
            
            --recursion_depth;
        }
        
        // Safe fallback sorting when recursion limit reached
        void bubbleSortFallback(vector_index_type low, vector_index_type high) noexcept {
            // Safety: basic validation
            if (array == nullptr || low >= high || high >= size_) return;
            
            // Safety: prevent infinite loops
            const vector_index_type max_iterations = (high - low + 1) * (high - low + 1);
            vector_index_type iteration_count = 0;
            
            for (vector_index_type i = low; i <= high && i < size_; ++i) {
                for (vector_index_type j = low; j < high - (i - low) && j < size_; ++j) {
                    // Safety: iteration limit check
                    if (++iteration_count > max_iterations) return;
                    
                    // Safety: bounds check for each access
                    if (j + 1 <= high && j < size_ && j + 1 < size_) {
                        if (!is_less(array[j], array[j + 1]) && !is_less(array[j + 1], array[j])) {
                            // Elements are equal, no swap needed
                        } else if (!is_less(array[j], array[j + 1])) {
                            T temp = array[j];
                            array[j] = array[j + 1];
                            array[j + 1] = temp;
                        }
                    }
                }
            }
        }

    public:
        // Erase element at position
        void erase(vector_index_type pos) noexcept {
            if (pos >= size_) return;
            customCopy(array + pos + 1, array + pos, size_ - pos - 1);
            --size_;
        }

        bool empty() const noexcept {
            return size_ == 0;
        }

        // Clear contents (keep capacity)
        void clear() noexcept {
            size_ = 0;
        }

        // Shrink capacity to fit size
        void fit() noexcept {
            if (size_ < capacity_) resize(size_);
        }
        T& back() noexcept {
            if (size_ == 0){
                return array[0]; // Return default value if empty
            }
            return array[size_ - 1];
        }
        const T& back() const noexcept {
            if (size_ == 0){
                return array[0]; // Return default value if empty
            }
            return array[size_ - 1];
        }
        T& front() noexcept {
            if (size_ == 0){
                return array[0]; // Return default value if empty
            }
            return array[0];
        }
        const T& front() const noexcept {
            if (size_ == 0){
                return array[0]; // Return default value if empty
            }
            return array[0];
        }

        // emplace_back: construct an element in place at the end
        
        void pop_back() noexcept {
            if (size_ == 0) {
                return; // Do nothing if empty
            }
            --size_;
        }
        // Returns a pointer such that [data(), data() + size()) is a valid range. For a non-empty %vector, data() == &front().
        T* data() noexcept { return array; }
        const T* data() const noexcept { return array; }

        // Internal resize: allocate newCapacity, copy old data, free old
        void resize(vector_index_type newCapacity) noexcept {
            if (newCapacity == capacity_) return;
            if (newCapacity == 0) newCapacity = 1;
            T* newArray = new T[newCapacity];
            vector_index_type toCopy = (size_ < newCapacity ? size_ : newCapacity);
            customCopy(array, newArray, toCopy);
            delete[] array;
            array = newArray;
            capacity_ = newCapacity;
            if (size_ > capacity_) size_ = capacity_;
        }
        void extend(vector_index_type newCapacity) noexcept {
            if (newCapacity > capacity_) resize(newCapacity);
        }

        // Accessors
        vector_index_type size() const noexcept { return size_; }
        vector_index_type cap() const noexcept { return capacity_; }

        // Operator[] returns default T() on out-of-range
        T& operator[](vector_index_type index) noexcept {
            static T default_value = T();
            return (index < size_) ? array[index] : default_value;
        }
    
        const T& operator[](vector_index_type index) const noexcept {
            static T default_value = T();
            return (index < size_) ? array[index] : default_value;
        }

        // Iterators (raw pointers)
        T* begin() noexcept { return array; }
        T* end()   noexcept { return array + size_; }
        const T* begin() const noexcept { return array; }
        const T* end()   const noexcept { return array + size_; }
    };
    

}   // namespace MCU