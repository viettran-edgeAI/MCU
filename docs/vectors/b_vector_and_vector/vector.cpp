#include <stdexcept>
#include "../../../src/hash_kernel.h"
#include "../../../src/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>

namespace mcu {
    // Forward declaration
    template<typename T, size_t sboSize> class b_vector;
    
    template<typename T>
    class vector : hash_kernel{
    private:
        T*      array    = nullptr;
        size_t size_    = 0;
        size_t capacity_ = 0;
        
        // Maximum capacity for the vector
        static constexpr size_t VECTOR_MAX_CAP = 2000000000;

        // internal resize without preserving size
        void i_resize(size_t newCapacity) noexcept {
            if (newCapacity == capacity_) return;
            if (newCapacity == 0) newCapacity = 1;
            T* newArray = new T[newCapacity];
            size_t toCopy = (size_ < newCapacity ? size_ : newCapacity);
            customCopy(array, newArray, toCopy);
            delete[] array;
            array = newArray;
            capacity_ = newCapacity;
            if (size_ > capacity_) size_ = capacity_;
        }

        // Internal helper: copy 'count' elements from src to dst
        void customCopy(const T* src, T* dst, size_t count) noexcept {
            for (size_t i = 0; i < count; ++i) {
                dst[i] = src[i];
            }
        }

    public:
        // Default: allocate capacity=1
        vector() noexcept
            : array(new T[1]), size_(0), capacity_(1) {}

        // Constructor with initial capacity
        explicit vector(size_t initialCapacity) noexcept
            : array(new T[(initialCapacity == 0) ? 1 : initialCapacity]),
            size_(initialCapacity), // Set size equal to capacity
            capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            // fix for error : access elements throgh operator[] when vector just initialized
            for (size_t i = 0; i < size_; ++i) {
                array[i] = T();
            }
        }

        // Constructor with initial size and value
        explicit vector(size_t initialCapacity, const T& value) noexcept
            : array(new T[(initialCapacity == 0) ? 1 : initialCapacity]),
            size_(initialCapacity), capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            for (size_t i = 0; i < size_; ++i) {
                array[i] = value;
            }
        }

        // Constructor: from min_init_list<T>
        vector(const mcu::min_init_list<T>& init) noexcept
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

        // Implicit conversion operator to b_vector
        template<size_t sboSize = 32>
        operator b_vector<T, sboSize>() const noexcept {
            b_vector<T, sboSize> result;
            result.clear();
            for (size_t i = 0; i < size_; ++i) {
                result.push_back(array[i]);
            }
            return result;
        }

        // Assignment from b_vector (enables implicit conversion in assignment)
        template<size_t sboSize>
        vector& operator=(const b_vector<T, sboSize>& other) noexcept {
            // Clear current content
            delete[] array;
            
            // Allocate new space
            size_ = other.size();
            capacity_ = size_ > 0 ? size_ : 1;
            array = new T[capacity_];
            
            // Copy elements
            for (size_t i = 0; i < size_; ++i) {
                array[i] = other[i];
            }
            
            return *this;
        }

        // Reserve at least newCapacity
        void reserve(size_t newCapacity) noexcept {
            if (newCapacity > capacity_) i_resize(newCapacity);
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
                i_resize(doubled);
            }
            array[size_++] = value;
        }

        // Insert at position
        void insert(size_t pos, const T& value) noexcept {
            if (pos > size_) return;
            if (size_ == capacity_) {
                size_t doubled;
                if(VECTOR_MAX_CAP == 255)
                    doubled = capacity_ ? capacity_ + 10 : 1;
                else
                    doubled = capacity_ ? capacity_ * 20 : 1;
                if (doubled > VECTOR_MAX_CAP) doubled = VECTOR_MAX_CAP;
                i_resize(doubled);
            }
            for (size_t i = size_; i > pos; --i) {
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
            size_t pos = position - array;
            size_t count = last - first;
            if (pos > size_) return;
            if (size_ + count > capacity_) {
                size_t newCapacity = capacity_ ? capacity_ * 2 : 1;
                if (newCapacity > VECTOR_MAX_CAP) newCapacity = VECTOR_MAX_CAP;
                i_resize(newCapacity);
            }
            for (size_t i = size_ + count - 1; i >= pos + count; --i) {
                array[i] = array[i - count];
            }
            for (size_t i = 0; i < count; ++i) {
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
        size_t partition(size_t low, size_t high) noexcept {
            // Safety: null pointer check
            if (array == nullptr) return low;
            
            // Safety: boundary validation
            if (low >= size_ || high >= size_) return low;
            if (low > high) return low;
            
            // Safety: handle unsigned underflow case
            if (high == 0 && low > 0) return low;
            
            T pivot = array[high];
            size_t i = low;
            
            // Safety: bound-checked loop
            for (size_t j = low; j < high && j < size_; ++j) {
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
        void quickSort(size_t low, size_t high) noexcept {
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
            
            size_t pivotIndex = partition(low, high);
            
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
        void bubbleSortFallback(size_t low, size_t high) noexcept {
            // Safety: basic validation
            if (array == nullptr || low >= high || high >= size_) return;
            
            // Safety: prevent infinite loops
            const size_t max_iterations = (high - low + 1) * (high - low + 1);
            size_t iteration_count = 0;
            
            for (size_t i = low; i <= high && i < size_; ++i) {
                for (size_t j = low; j < high - (i - low) && j < size_; ++j) {
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
        void erase(size_t pos) noexcept {
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

        void fill(const T& value) noexcept {
            for (size_t i = 0; i < size_; ++i) {
                array[i] = value;
            }
            size_ = capacity_;
        }

        // Shrink capacity to fit size
        void fit() noexcept {
            if (size_ < capacity_) i_resize(size_);
        }
        T& back() noexcept {
            if (size_ == 0){
                static T default_value = T();
                return default_value; // Return default value if empty
            }
            return array[size_ - 1];
        }
        const T& back() const noexcept {
            if (size_ == 0){
                static T default_value = T();
                return default_value; // Return default value if empty
            }
            return array[size_ - 1];
        }
        T& front() noexcept {
            if (size_ == 0){
                static T default_value = T();
                return default_value; // Return default value if empty
            }
            return array[0];
        }
        const T& front() const noexcept {
            if (size_ == 0){
                static T default_value = T();
                return default_value; // Return default value if empty
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

        /**
         * Resizes the container so that it contains n elements.

        - If n is smaller than the current container size, the content is reduced to its first n elements, removing those beyond (and destroying them).

        - If n is greater than the current container size, the content is expanded by inserting at the end as many elements as needed to reach a size of n. If val is specified, the new elements are initialized as copies of val, otherwise, they are value-initialized.

        - If n is also greater than the current container capacity, an automatic reallocation of the allocated storage space takes place.
         */
        void resize(size_t newSize, const T& value) noexcept {
            if (newSize > VECTOR_MAX_CAP) {
                newSize = VECTOR_MAX_CAP;
            }
            
            if (newSize < size_) {
                // Shrink: reduce size to newSize
                size_ = newSize;
            } else if (newSize > size_) {
                // Expand: need to add elements
                if (newSize > capacity_) {
                    // Need to reallocate
                    i_resize(newSize);
                }
                
                // Fill new elements with the provided value
                for (size_t i = size_; i < newSize; ++i) {
                    array[i] = value;
                }
                size_ = newSize;
            }
            // If newSize == size_, do nothing
        }
        
        void resize(size_t newSize) noexcept {
            if (newSize > VECTOR_MAX_CAP) {
                newSize = VECTOR_MAX_CAP;
            }
            
            if (newSize < size_) {
                // Shrink: reduce size to newSize
                size_ = newSize;
            } else if (newSize > size_) {
                // Expand: need to add elements
                if (newSize > capacity_) {
                    // Need to reallocate
                    i_resize(newSize);
                }
                
                // Fill new elements with default-initialized values
                for (size_t i = size_; i < newSize; ++i) {
                    array[i] = T();
                }
                size_ = newSize;
            }
            // If newSize == size_, do nothing
        }

        // Accessors
        size_t size() const noexcept { return size_; }
        size_t capacity() const noexcept { return capacity_; }

        // Operator[] returns default T() on out-of-range
        T& operator[](size_t index) noexcept {
            static T default_value = T();
            return (index < size_) ? array[index] : default_value;
        }
    
        const T& operator[](size_t index) const noexcept {
            static T default_value = T();
            return (index < size_) ? array[index] : default_value;
        }

        // Iterators (raw pointers)
        T* begin() noexcept { return array; }
        T* end()   noexcept { return array + size_; }
        const T* begin() const noexcept { return array; }
        const T* end()   const noexcept { return array + size_; }

        size_t memory_usage() const noexcept {
            size_t heap_bytes = static_cast<size_t>(capacity_) * sizeof(T);
            return sizeof(*this) + heap_bytes;
        }
    };
} // namespace mcu
    