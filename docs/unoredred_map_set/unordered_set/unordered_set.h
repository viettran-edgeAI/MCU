// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include "../hash_kernel/hash_kernel.h"
#include "../hash_kernel/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>
/*
------------------------------------------------------------------------------------------------------------------
---------------------------------------------- UNORDERED_SET -----------------------------------------------------
------------------------------------------------------------------------------------------------------------------
*/
template<typename T>
class unordered_set : public hash_kernel, public slot_handler {
private:
    static constexpr uint8_t MAX_CAP = 255;
    static constexpr uint8_t INIT_CAP = 10;

    T* table = nullptr;
    uint8_t size_ = 0;
    uint8_t dead_size_ = 0;     // used + tombstones
    uint8_t fullness_ = 92; //(%)       . virtual_cap = cap_ * fullness_ / 100
    uint8_t virtual_cap = 0; // virtual capacity
    uint8_t step_ = 0;

    void rehash(uint8_t newCap) {
        if (newCap < size_) newCap = size_;
        if (newCap > MAX_CAP) newCap = MAX_CAP;

        auto* oldTable = table;
        auto* oldFlags = flags;
        uint8_t oldCap = cap_;

        table = new T[newCap];
        flags = new uint8_t[(newCap * 2 + 7) / 8];
        memset(flags, 0, (newCap * 2 + 7) / 8);

        size_ = 0;
        dead_size_ = 0;
        cap_ = newCap;
        virtual_cap = cap_to_virtual();
        step_ = calStep(newCap);

        for (uint8_t i = 0; i < oldCap; ++i) {
            slotState s = getStateFrom(oldFlags, i);
            if (s == slotState::Used) {
                insert(oldTable[i]);
            }
        }
        delete[] oldTable;
        delete[] oldFlags;
    }
    // safely convert between cap_ and virtual_cap 
    // ensure integrity after 2-way conversion
    uint8_t cap_to_virtual() const noexcept {
        return static_cast<uint8_t>((static_cast<uint16_t>(cap_) * fullness_) / 100);
    }
    
    uint8_t virtual_to_cap(uint8_t v_cap) const noexcept {
        return static_cast<uint8_t>((static_cast<uint16_t>(v_cap) * 100) / fullness_);
    }
    bool is_full() const noexcept {
        return size_ >= virtual_cap;
    }
public:
    /**
     * @brief Default constructor, creates an unordered_set with small initial capacity.
     */
    unordered_set() noexcept {
        rehash(4);
    }

    /**
     * @brief Constructor with specified initial capacity.
     * @param cap Initial capacity (number of elements) the set should accommodate.
     */
    explicit unordered_set(uint8_t cap) noexcept {
        rehash(cap);
    }

    /**
     * @brief Destructor, frees all allocated memory.
     */
    ~unordered_set() noexcept {
        delete[] table;
    }

    /**
     * @brief Copy constructor, creates a deep copy of another set.
     * @param other The set to copy from.
     */
    unordered_set(const unordered_set& other) noexcept : hash_kernel(),
        slot_handler(other),  // copy flags & cap_
        fullness_(other.fullness_),
        virtual_cap(other.virtual_cap),
        step_(other.step_)
    {
        cap_ = other.cap_;
        size_ = other.size_;
        dead_size_ = other.dead_size_;
        table = new T[cap_];
        for (uint8_t i = 0; i < cap_; ++i) {
            if (getState(i) == slotState::Used)
                table[i] = other.table[i];
            else
                table[i] = T(); // clear unused slots for safety
        }
    }

    /**
     * @brief Move constructor, transfers ownership of resources.
     * @param other The set to move from (will be left in a valid but unspecified state).
     */
    unordered_set(unordered_set&& other) noexcept : hash_kernel(),
    slot_handler(std::move(other)),  // ← steal flags & cap_,
    size_(other.size_),
    dead_size_(other.dead_size_),
    fullness_(other.fullness_),
    virtual_cap(other.virtual_cap),
    step_(other.step_){
        table       = other.table;
        other.table = nullptr;
        other.size_ = 0;
        other.fullness_ = 92;
        other.virtual_cap = 0;
        other.step_ = 0;
        other.dead_size_ = 0;
    }

    /**
     * @brief Copy assignment operator, replaces contents with a copy of another set.
     * @param other The set to copy from.
     * @return Reference to *this.
     */
    unordered_set& operator=(const unordered_set& other) noexcept {
        if (this != &other) {
            delete[] table;
            slot_handler::operator=(other);  // copy flags & cap_
            fullness_ = other.fullness_;
            virtual_cap = other.virtual_cap;
            step_ = other.step_;
            cap_ = other.cap_;
            size_ = other.size_;
            dead_size_ = other.dead_size_;
            table = new T[cap_];
            for (uint8_t i = 0; i < cap_; ++i) {
                if (getState(i) == slotState::Used)
                    table[i] = other.table[i];
            }
        }
        return *this;
    }

    /**
     * @brief Move assignment operator, transfers ownership of resources.
     * @param other The set to move from (will be left in a valid but unspecified state).
     * @return Reference to *this.
     */
    unordered_set& operator=(unordered_set&& other) noexcept {
        if (this != &other) {
            delete[] table;
            slot_handler::operator=(std::move(other)); // steal flags & cap_
            size_      = other.size_;
            dead_size_ = other.dead_size_;
            fullness_  = other.fullness_;
            table      = other.table;
            virtual_cap = other.virtual_cap;
            step_      = other.step_;
            // reset other
            other.table = nullptr;
            other.size_ = 0;
            other.dead_size_ = 0;
            other.cap_  = 0;
            other.fullness_ = 92;
            other.virtual_cap = 0;
            other.step_ = 0;
        }
        return *this;
    }

    template<bool IsConst>
    class base_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = T;
        using pointer           = typename std::conditional<IsConst, const T*, T*>::type;
        using reference         = typename std::conditional<IsConst, const T&, T&>::type;

    private:
        typename std::conditional<IsConst, const unordered_set*, unordered_set*>::type set_;
        uint8_t index_;

        void findNextUsed() {
            while (index_ < set_->cap_ && set_->getState(index_) != slotState::Used) {
                ++index_;
            }
        }
        void findPrevUsed() {
            if (index_ == 0) return;
            uint8_t i = index_ - 1;
            while (i > 0 && set_->getState(i) != slotState::Used) --i;
            if (set_->getState(i) == slotState::Used) index_ = i;
        }

    public:
        // default constructor for "end()"
        base_iterator() noexcept
            : set_(nullptr), index_(MAX_CAP)
        {}
        base_iterator(typename std::conditional<IsConst, const unordered_set*, unordered_set*>::type set,
                    uint8_t start)
        : set_(set), index_(start) {
            findNextUsed();
        }

        base_iterator& operator++() {
            if (index_ < set_->cap_) {
                ++index_;
                findNextUsed();
            }
            return *this;
        }
        base_iterator operator++(int) {
            base_iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        base_iterator& operator--() {
            findPrevUsed();
            return *this;
        }
        base_iterator operator--(int) {
            base_iterator tmp = *this;
            --(*this);
            return tmp;
        }

        reference operator*()  const { return set_->table[index_]; }
        pointer   operator->() const { return &set_->table[index_]; }

        bool operator==(const base_iterator& o) const { return index_ == o.index_; }
        bool operator!=(const base_iterator& o) const { return !(*this == o); }
    };

    using iterator       = base_iterator<false>;
    using const_iterator = base_iterator<true>;
    
    // Iterators set     
    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, cap_); }
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, cap_); }
    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, cap_); }

    /**
     * @brief Inserts an element into the set, with perfect forwarding.
     * @param value The value to insert.
     * @return true if insertion took place, false if the element already exists.
     * @note Uses perfect forwarding to minimize copies and handle both lvalues and rvalues.
     */
    template<typename U>
    bool insert(U&& value) noexcept {
        if (dead_size_ >= virtual_cap) {
            if (size_ == set_ability())
                return false;
            uint16_t dbl = cap_ ? cap_ * 2: INIT_CAP;
            if (dbl > MAX_CAP) dbl = MAX_CAP;
            rehash(static_cast<uint8_t>(dbl));
        }
        
        uint8_t index = hashFunction(cap_, value, best_hashers_16[cap_ - 1]);
        
        while (getState(index) != slotState::Empty) {
            auto st = getState(index);
            if(table[index] == value){
                if(st == slotState::Used){
                    return false;       // duplicate
                } 
                if(st == slotState::Deleted){
                    break;
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        slotState oldState = getState(index);
        if(oldState == slotState::Empty){
            ++dead_size_;
        }
        table[index] = std::forward<U>(value);
        setState(index, slotState::Used);
        ++size_;
        return true;
    }

    /**
     * @brief Removes an element with the specified value.
     * @param value The value to find and remove.
     * @return true if an element was removed, false otherwise.
     */
    bool erase(const T& value) noexcept {
        uint8_t index = hashFunction(cap_, value, best_hashers_16[cap_ - 1]);
        uint8_t attempt = 0;
        while(getState(index) != slotState::Empty){
            if(attempt++ == cap_){
                return false;
            }
            if(table[index] == value){
                if(getState(index) == slotState::Used){
                    setState(index, slotState::Deleted);
                    --size_;
                    return true;
                }
                else if(getState(index) == slotState::Deleted){
                    return false;
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return false;
    }

    /**
     * @brief Finds an element with the specified value.
     * @param value The value to find.
     * @return Iterator to the element if found, otherwise end().
     */
    iterator find(const T& value) noexcept {
        uint8_t index = hashFunction(cap_, value, best_hashers_16[cap_ - 1]);
        uint8_t attempt = 0;
        while(getState(index) != slotState::Empty){
            if(attempt++ >= cap_){
                return end();
            }
            if(table[index] == value){
                if(getState(index) == slotState::Used){
                    return iterator(this,index);
                }
                else if(getState(index) == slotState::Deleted){
                    return end();
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return end();
    }

    /**
     * @brief Checks if this set contains the same elements as another.
     * @param other The set to compare with.
     * @return true if sets are equal, false otherwise.
     */
    bool operator==(const unordered_set& other) const noexcept {
        if (size_ != other.size_) return false;
        for (uint8_t i = 0; i < cap_; ++i) {
            if (getState(i) == slotState::Used && !other.contains(table[i])) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Checks if this set differs from another.
     * @param other The set to compare with.
     * @return true if sets are not equal, false otherwise.
     */
    bool operator!=(const unordered_set& other) const noexcept {
        return !(*this == other);
    }

    /**
     * @brief Gets the current fullness factor.
     * @return The current fullness factor as a float (0.0 to 1.0).
     */
    float get_fullness() const noexcept {
        return static_cast<float>(fullness_) / 100.0f;
    }

    /**
     * @brief Sets the fullness factor for the set.
     * @param fullness The new fullness factor (0.1 to 1.0 or 10 to 100).
     * @return true if successful, false if the new fullness would overflow the set.
     * @note Lower fullness reduces collisions but increases memory usage:
     *       0.9 -> -71% collisions | +11% memory
     *       0.8 -> -87% collisions | +25% memory
     *       0.7 -> -94% collisions | +43% memory
     */
    bool set_fullness(float fullness) noexcept {
        // Ensure fullness is within the valid range [0.1, 1.0] or [10, 100] (% format)
        if(fullness < 0.1f) fullness = 0.1f;
        if(fullness > 1.0f && fullness < 10) fullness = 1.0f;
        if(fullness > 100) fullness = 100;

        uint8_t old_fullness_ = fullness_;

        if(fullness <= 1.0f){
            // Convert fullness to an integer percentage
            uint8_t newFullness = static_cast<uint8_t>(fullness * 100);
            fullness_ = newFullness;
        }else{
            fullness_ = static_cast<uint8_t>(fullness);
        }

        if(set_ability() < size_){
            fullness_ = old_fullness_;
            return false;
        }
        return true;
    }

    /**
     * @brief Checks if the set contains a specific value.
     * @param value The value to find.
     * @return true if the value exists in the set, false otherwise.
     */
    bool contains(const T& value) noexcept {
        if(find(value) == end()) return false;
        return true;
    }

    /**
     * @brief Shrinks the set's capacity to fit its size.
     * @return Number of bytes freed by shrinking.
     */
    size_t fit() noexcept {
        if (size_ < cap_) {
            uint8_t oldCap = cap_;
            size_t flagBytes = (oldCap * 2 + 7) / 8;
            size_t tableSaved = (oldCap - size_) * sizeof(T);
            rehash(size_);
            size_t newFlagBytes = (cap_ * 2 + 7) / 8;
            return tableSaved + (flagBytes - newFlagBytes);
        }
        return 0;
    }

    /**
     * @brief Resizes the set to a new capacity.
     * @param new_virtual_cap The new virtual capacity.
     * @return true if successful, false if the requested capacity is too large.
     */
    bool resize(uint8_t new_virtual_cap) noexcept {
        uint8_t newCap = virtual_to_cap(new_virtual_cap);
        if (newCap > MAX_CAP) return false;
        if (newCap < size_) newCap = size_;
        if (newCap == cap_) return true;
        rehash(newCap);
        return true;
    }

    /**
     * @brief Reserves space for a specified number of elements.
     * @param virtual_cap The new virtual capacity to reserve.
     * @return true if successful, false if the requested capacity is too large.
     * @note This prepares the set to hold the specified number of elements without rehashing.
     */
    bool reserve(uint8_t virtual_cap) noexcept {
        uint8_t newCap = virtual_to_cap(virtual_cap);
        if (newCap > MAX_CAP) return false;
        if (newCap < size_) newCap = size_;
        if (newCap == cap_) return true;
        rehash(newCap);
        return true;
    }

    /**
     * @brief Gets the maximum theoretical number of elements the set can hold.
     * @return Maximum capacity based on the current fullness setting.
     */
    uint16_t set_ability() const noexcept {
        return static_cast<uint16_t>(MAX_CAP) * fullness_ / 100;
    }

    /**
     * @brief Gets the current number of elements in the set.
     * @return The element count.
     */
    uint16_t size() const noexcept {
        return size_;
    }

    /**
     * @brief Gets the current virtual capacity of the set.
     * @return The current virtual capacity.
     */
    uint16_t capacity() const noexcept {
        return virtual_cap;
    }

    /**
     * @brief Checks if the set is empty.
     * @return true if the set contains no elements, false otherwise.
     */
    bool empty() const noexcept {
        return size_ == 0;
    }

    /**
     * @brief Removes all elements from the set.
     * @note Keeps the allocated memory for reuse.
     */
    void clear() noexcept {
        memset(flags, 0, (cap_ * 2 + 7) / 8);
        size_ = 0;
        dead_size_ = 0;
    }

    /**
     * @brief Calculates the total memory usage of the set.
     * @return Memory usage in bytes.
     * @note Includes the set object, elements table, and flag array.
     */
    size_t memory_usage() const noexcept {
        size_t table_bytes = static_cast<size_t>(cap_) * sizeof(T);
        size_t flags_bytes = (cap_ * 2 + 7) / 8;
        return sizeof(*this) + table_bytes + flags_bytes;
    }

    /**
     * @brief Swaps the contents of two sets.
     * @param other The set to swap with.
     */
    void swap(unordered_set& other) noexcept {
        std::swap(table, other.table);
        std::swap(flags, other.flags);
        std::swap(cap_, other.cap_);
        std::swap(size_, other.size_);
        std::swap(dead_size_, other.dead_size_);
        std::swap(fullness_, other.fullness_);
        std::swap(virtual_cap, other.virtual_cap);
        std::swap(step_, other.step_);
    }
};

    
    