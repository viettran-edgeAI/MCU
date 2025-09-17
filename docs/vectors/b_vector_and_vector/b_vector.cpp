#include <stdexcept>
#include "../../../src/hash_kernel.h"
#include "../../../src/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>

namespace mcu {
    // Forward declaration
    template<typename T> class vector;

    // vector with small buffer optimization (SBO)
    template<typename T, size_t sboSize = 0>
    class b_vector : hash_kernel {
    private:
        using vector_index_type = size_t;
        
        // Small Buffer Optimization - auto-adjust SBO size based on sizeof(T)
        // If sboSize is explicitly provided (non-zero), use it; otherwise auto-calculate
        static constexpr size_t calculateSboSize() {
            if constexpr (sboSize != 0) {
                return sboSize;  // User-specified size
            } else {
                // Auto-calculate based on sizeof(T)
                constexpr size_t type_size = sizeof(T);
                if constexpr (type_size == 1) {
                    return 16;  // 32 bytes for 1-byte types (char, uint8_t, etc.)
                } else if constexpr (type_size == 2) {
                    return 8;  // 16 elements for 2-byte types (short, uint16_t, etc.)
                } else if constexpr (type_size == 4) {
                    return 4;   // 8 elements for 4-byte types (int, float, uint32_t, etc.)
                } else if constexpr (type_size == 8) {
                    return 4;   // 4 elements for 8-byte types (double, uint64_t, etc.)
                } else if constexpr (type_size <= 16) {
                    return 4;   // 2 elements for types up to 16 bytes
                } else {
                    return 2;   // 1 element for very large types
                }
            }
        }
        
        static constexpr size_t SBO_SIZE = calculateSboSize();
        
        // Union to save memory - either use internal buffer or heap pointer
        union {
            T* heap_array;
            alignas(T) char buffer[sizeof(T) * SBO_SIZE];
        };
        
        vector_index_type size_    = 0;
        vector_index_type capacity_ = SBO_SIZE;
        bool using_heap = false;  // Track whether we're using heap or buffer
        
        // based on size_t, set the maximum capacity
        static constexpr size_t VECTOR_MAX_CAP = SIZE_MAX / 2; // Safe maximum for size_t

        // Get pointer to current data (buffer or heap)
        T* data_ptr() noexcept {
            return using_heap ? heap_array : reinterpret_cast<T*>(buffer);
        }
        
        const T* data_ptr() const noexcept {
            return using_heap ? heap_array : reinterpret_cast<const T*>(buffer);
        }

        // internal resize without preserving size
        void i_resize(vector_index_type newCapacity) noexcept {
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
        b_vector(const mcu::min_init_list<T>& init) noexcept : size_(init.size()) {
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

        // Implicit conversion operator to vector
        operator vector<T>() const noexcept {
            vector<T> result;
            result.clear();
            const T* ptr = data_ptr();
            for (vector_index_type i = 0; i < size_; ++i) {
                result.push_back(ptr[i]);
            }
            return result;
        }

        // Assignment from vector (enables implicit conversion in assignment)
        b_vector& operator=(const vector<T>& other) noexcept {
            // First, correctly destroy the elements in `this`
            if (using_heap) {
                delete[] heap_array;
            } else {
                T* p = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) p[i].~T();
            }
            
            // Determine if we need heap or can use SBO
            size_ = other.size();
            if (size_ <= SBO_SIZE) {
                // Use SBO
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    new(buffer_ptr + i) T(i < size_ ? other[i] : T());
                }
            } else {
                // Use heap
                using_heap = true;
                capacity_ = size_;
                heap_array = new T[capacity_];
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = other[i];
                }
            }
            
            return *this;
        }

        void fill(const T& value) noexcept {
            T* ptr = data_ptr();
            for (vector_index_type i = 0; i < size_; ++i) {
                ptr[i] = value;
            }
            size_ = capacity_;
        }

        // Reserve at least newCapacity
        void reserve(vector_index_type newCapacity) noexcept {
            if (newCapacity > capacity_) {
                if (newCapacity > SBO_SIZE && !using_heap) {
                    switch_to_heap(newCapacity);
                } else if (using_heap) {
                    i_resize(newCapacity);
                }
            }
        }

        // Append element
        void push_back(const T& value) noexcept {
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 20 : 1;
                else
                    doubled = capacity_ ? capacity_ + 100 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                
                if (doubled > SBO_SIZE && !using_heap) {
                    switch_to_heap(doubled);
                } else if (using_heap) {
                    i_resize(doubled);
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
                    i_resize(doubled);
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
                    i_resize(newCapacity);
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
            if (size_ < capacity_) i_resize(size_);
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

        size_t memory_usage() const noexcept {
            size_t buffer_bytes = sizeof(buffer);
            size_t heap_bytes = using_heap ? static_cast<size_t>(capacity_) * sizeof(T) : 0;
            return sizeof(*this) + (using_heap ? heap_bytes : buffer_bytes);
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

        /**
         * Resizes the container so that it contains n elements.

        - If n is smaller than the current container size, the content is reduced to its first n elements, removing those beyond (and destroying them).

        - If n is greater than the current container size, the content is expanded by inserting at the end as many elements as needed to reach a size of n. If val is specified, the new elements are initialized as copies of val, otherwise, they are value-initialized.

        - If n is also greater than the current container capacity, an automatic reallocation of the allocated storage space takes place.
         */
        void resize(vector_index_type newSize) noexcept {
            if (newSize < size_) {
                // Case 1: Shrinking - reduce size, destroy elements beyond new size
                // For simple types, just update size; for complex types, call destructors
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    T* ptr = data_ptr();
                    for (vector_index_type i = newSize; i < size_; ++i) {
                        ptr[i].~T();
                    }
                }
                size_ = newSize;
            }
            else if (newSize > size_) {
                // Case 2: Growing - expand size, value-initialize new elements
                if (newSize > capacity_) {
                    // Need to increase capacity first
                    reserve(newSize);
                }
                
                T* ptr = data_ptr();
                // Value-initialize new elements (default constructor)
                for (vector_index_type i = size_; i < newSize; ++i) {
                    new(ptr + i) T();
                }
                size_ = newSize;
            }
            // Case 3: newSize == size_ - do nothing
        }
        
        void resize(vector_index_type newSize, const T& value) noexcept {
            if (newSize < size_) {
                // Case 1: Shrinking - reduce size, destroy elements beyond new size
                if constexpr (!std::is_trivially_destructible_v<T>) {
                    T* ptr = data_ptr();
                    for (vector_index_type i = newSize; i < size_; ++i) {
                        ptr[i].~T();
                    }
                }
                size_ = newSize;
            }
            else if (newSize > size_) {
                // Case 2: Growing - expand size, initialize new elements with provided value
                if (newSize > capacity_) {
                    // Need to increase capacity first
                    reserve(newSize);
                }
                
                T* ptr = data_ptr();
                // Initialize new elements with the provided value
                for (vector_index_type i = size_; i < newSize; ++i) {
                    new(ptr + i) T(value);
                }
                size_ = newSize;
            }
            // Case 3: newSize == size_ - do nothing
        }

        // Accessors
        vector_index_type size() const noexcept { return size_; }
        vector_index_type capacity() const noexcept { return capacity_; }

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

} // namespace mcu
