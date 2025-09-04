// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include "../hash_kernel/hash_kernel.h"
#include "../hash_kernel/initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>

// #include <cstring>
// #include <iostream>

template<typename T1, typename T2>
struct pair {
    T1 first;
    T2 second;

    // trivial default, copy and move
    constexpr pair() noexcept = default;
    constexpr pair(const T1& a, const T2& b) noexcept
    : first(a), second(b) {}
    constexpr pair(T1&& a, T2&& b) noexcept
    : first(std::move(a)), second(std::move(b)) {}
    constexpr pair(const pair&) noexcept = default;
    constexpr pair(pair&&) noexcept = default;

    constexpr pair& operator=(const pair&) noexcept = default;
    constexpr pair& operator=(pair&&) noexcept = default;

    // comparisons
    constexpr bool operator==(const pair& o) const noexcept {
        return first == o.first && second == o.second;
    }
    constexpr bool operator!=(const pair& o) const noexcept {
        return !(*this == o);
    }
    static constexpr pair<T1,T2> make_pair(const T1& a, const T2& b) noexcept {
        return pair<T1,T2>(a, b);
    }
    static constexpr pair<T1,T2> make_pair(T1&& a, T2&& b) noexcept {
        return pair<T1,T2>(std::move(a), std::move(b));
    }
};
template<typename T1, typename T2>
constexpr pair<std::decay_t<T1>, std::decay_t<T2>> make_pair(T1&& a, T2&& b) noexcept {
    return pair<std::decay_t<T1>, std::decay_t<T2>>(std::forward<T1>(a), std::forward<T2>(b));
}


// unordered_map class : for speed and flexibility, but limited to small number of elements (max 255)
template<typename V, typename T>
class unordered_map : public hash_kernel, public slot_handler {
private:
    using Pair = pair<V, T>;

    Pair* table = nullptr;
    uint8_t size_ = 0;
    uint8_t fullness_ = 92; //(%)       . virtual_cap = cap_ * fullness_ / 100
    uint8_t virtual_cap = 0; // virtual capacity
    uint8_t step_ = 0;
    // cap_        : for internal use 
    // virtual_cap : for user
    // ----------------------------------------------  : table size
    // --------------|--------------------|----------|
    //             size_             virtual_cap    cap_
    static T MAP_DEFAULT_VALUE;

    void rehash(uint8_t newCap) {
        if (newCap < size_) newCap = size_;
        if (newCap > MAX_CAP) newCap = MAX_CAP;
        if (newCap == 0) newCap = INIT_CAP;

        auto* oldTable = table;
        auto* oldFlags = flags;
        uint8_t oldCap = cap_;

        table = new Pair[newCap];
        slots_init(newCap);

        size_ = 0;
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

    bool inline is_full() const noexcept {
        return size_ >= virtual_cap;
    }
    template<typename U, typename R> friend class ChainedUnorderedMap;
    template<typename U> friend class ChainedUnorderedSet;
// protected:
public:
    int16_t getValue(V key) noexcept {
        uint8_t index     = hashFunction(cap_, key, best_hashers_16[cap_ - 1]);
        uint8_t attempts= 0;

        while(getState(index) != slotState::Empty) {
            slotState st = getState(index);
            if (attempts++ == cap_) {
                break;
            }
            if (table[index].first == key) {
                if (st == slotState::Used) {
                    // existing element
                    return table[index].second;
                }
                if (st == slotState::Deleted) {
                    // reuse this tombstone
                    break;
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return -1;
    }
    int16_t getValue(V key) const noexcept {
        uint8_t index     = hashFunction(cap_, key, best_hashers_16[cap_ - 1]);
        uint8_t attempts= 0;

        while(getState(index) != slotState::Empty) {
            slotState st = getState(index);
            if (attempts++ == cap_) {
                break;
            }
            if (table[index].first == key) {
                if (st == slotState::Used) {
                    // existing element
                    return table[index].second;
                }
                if (st == slotState::Deleted) {
                    // reuse this tombstone
                    break;
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return -1;
    }

    
public:
    // default constructor
    unordered_map() noexcept {
        rehash(4);
    }

    /**
     * @brief Constructor with specified initial capacity.
     * @param cap Initial capacity (number of elements) the map should accommodate.
     */
    explicit unordered_map(uint8_t cap) noexcept {
        rehash(cap);
    }
    // destructor
    ~unordered_map() noexcept {
        delete[] table;
    }

    /**
     * @brief Copy constructor, creates a deep copy of another map.
     * @param other The map to copy from.
     */
    unordered_map(const unordered_map& other) noexcept : hash_kernel(),
        slot_handler(other),        
        size_(other.size_),
        fullness_(other.fullness_),
        virtual_cap(other.virtual_cap),
        step_(other.step_)
    {
        table = new Pair[cap_];
        for (uint8_t i = 0; i < cap_; ++i) {
            if (getState(i) == slotState::Used)
                table[i] = other.table[i];
        }
    }


    /**
     * @brief Move constructor, transfers ownership of resources.
     * @param other The map to move from (will be left in a valid but unspecified state).
     */
    unordered_map(unordered_map&& other) noexcept : hash_kernel(),
    slot_handler(std::move(other)),  // ← steal flags & cap_
    size_(other.size_),
    fullness_(other.fullness_),
    virtual_cap(other.virtual_cap),
    step_(other.step_)
    {
        table       = other.table;
        other.table = nullptr;
        other.size_ = 0;
        other.fullness_ = 92;
        other.virtual_cap = 0;
        other.step_ = 0;
    }

    /**
     * @brief Copy assignment operator, replaces contents with a copy of another map.
     * @param other The map to copy from.
     * @return Reference to *this.
     */
    unordered_map& operator=(const unordered_map& other) noexcept {
        if (this != &other) {
            delete[] table;
            slot_handler::operator=(other);  // copy flags & cap_
            size_      = other.size_;
            fullness_  = other.fullness_;
            virtual_cap = other.virtual_cap;
            step_      = other.step_;
            table      = new Pair[cap_];
            for (uint8_t i = 0; i < cap_; ++i) {
                if (getState(i) == slotState::Used) {
                    table[i] = other.table[i];
                }
            }
        }
        return *this;
    }

    /**
     * @brief Move assignment operator, transfers ownership of resources.
     * @param other The map to move from (will be left in a valid but unspecified state).
     * @return Reference to *this.
     */
    unordered_map& operator=(unordered_map&& other) noexcept {
        if (this != &other) {
            delete[] table;
            slot_handler::operator=(std::move(other)); // steal flags & cap_
            size_      = other.size_;
            fullness_  = other.fullness_;
            table      = other.table;
            virtual_cap = other.virtual_cap;
            step_      = other.step_;
            // reset other
            other.table = nullptr;
            other.size_ = 0;
            other.cap_  = 0;
            other.fullness_ = 92;
            other.virtual_cap = 0;
            other.step_ = 0;
        }
        return *this;
    }

    // Iterator traits for STL compatibility
    template<bool IsConst>
    class base_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = typename std::conditional<IsConst, const Pair, Pair>::type;
        using pointer           = typename std::conditional<IsConst, const Pair*, Pair*>::type;
        using reference         = value_type&;

    private:
        typename std::conditional<IsConst, const unordered_map*, unordered_map*>::type map_;
        uint8_t index_;

        void advance() {
            while (index_ < map_->cap_ && map_->getState(index_) != slotState::Used)
                ++index_;
        }
        void retreat() {
            if (index_ == 0) return;
            uint8_t i = index_ - 1;
            while (i > 0 && map_->getState(i) != slotState::Used) --i;
            if (map_->getState(i) == slotState::Used) index_ = i;
        }

    public:
        // zero-arg ctor for “end()” 
        constexpr base_iterator() noexcept
            : map_(nullptr), index_(MAX_CAP)
        {}
    
        // your existing two-arg ctor
        base_iterator(decltype(map_) m, uint8_t start) noexcept
            : map_(m), index_(start)
        {
            if (map_) advance();
        }
        base_iterator& operator++()    { ++index_; advance(); return *this; }
        base_iterator  operator++(int) { auto tmp=*this; ++*this; return tmp; }
        base_iterator& operator--()    { retreat(); return *this; }
        base_iterator  operator--(int) { auto tmp=*this; --*this; return tmp; }

        reference operator*()  const { return map_->table[index_]; }
        pointer   operator->() const { return &map_->table[index_]; }

        bool operator==(const base_iterator& o) const {
            return map_==o.map_ && index_==o.index_;
        }
        bool operator!=(const base_iterator& o) const {
            return !(*this==o);
        }
    };

    using iterator       = base_iterator<false>;
    using const_iterator = base_iterator<true>;

    /**
     * @brief Returns an iterator to the beginning of the map.
     * @return Iterator pointing to the first element.
     */
    iterator begin() { return iterator(this, 0); }
    iterator end() { return iterator(this, cap_);}

    const_iterator cbegin() const { return const_iterator(this, 0); }
    const_iterator cend() const { return const_iterator(this, cap_); }
    const_iterator begin() const { return cbegin(); }
    const_iterator end() const { return cend(); }

private:
    pair<iterator, bool> insert_core(Pair&& p) noexcept {
        if (is_full()) {
            if (cap_ == MAX_CAP)
                return { end(), false };
            uint16_t dbl = cap_ ? cap_ * 2: INIT_CAP;
            if (dbl > MAX_CAP) dbl = MAX_CAP;
            rehash(static_cast<uint8_t>(dbl));
        }

        V key       = p.first;
        uint8_t index     = hashFunction(cap_, key, best_hashers_16[cap_ - 1]);
        uint8_t attempts= 0;
        bool saw_deleted = false;

        while (getState(index) != slotState::Empty) {
            slotState st = getState(index);
            if (attempts++ == cap_) {
                // too many tombstones → clean up and retry
                if (saw_deleted) {
                    rehash(cap_);
                    return insert_core(std::move(p));
                }
                return { end(), false };
            }
            if (table[index].first == key) {
                if (st == slotState::Used) {
                    // existing element
                    return { iterator(this, index), false };
                }
                if (st == slotState::Deleted) {
                    // reuse this tombstone
                    break;
                }
            }
            if (st == slotState::Deleted)
                saw_deleted = true;
            index = linearProbe(cap_, index, step_);
        }

        table[index] = std::move(p);
        setState(index, slotState::Used);
        ++size_;
        return { iterator(this, index), true };
    }
public:
    /**
     * @brief Inserts a pair into the map, copying if needed.
     * @param p The key-value pair to insert.
     * @return A pair containing an iterator to the inserted element and a boolean indicating whether insertion took place.
     */
    pair<iterator,bool> insert(const Pair& p) noexcept {
        return insert_core(Pair(p));  // Copy ilue and move
    }

    /**
     * @brief Inserts a pair into the map using move semantics.
     * @param p The key-value pair to insert.
     * @return A pair containing an iterator to the inserted element and a boolean indicating whether insertion took place.
     */
    pair<iterator, bool> insert(Pair&& p) noexcept {
        return insert_core(std::move(p));  // Move directly
    }

    /**
     * @brief Inserts a key-value pair into the map, with perfect forwarding.
     * @param key The key to insert.
     * @param value The value to insert, forwarded to avoid extra copies.
     * @return A pair containing an iterator to the inserted element and a boolean indicating whether insertion took place.
     */
    template<typename U>
    pair<iterator,bool> insert(V key, U&& value) noexcept {
        return insert_core(Pair(key, std::forward<U>(value)));
    }

    /**
     * @brief Removes an element with the specified key.
     * @param key The key to find and remove.
     * @return true if an element was removed, false otherwise.
     */
    bool erase(V key) noexcept {
        uint8_t index = hashFunction(cap_, key, best_hashers_16[cap_ - 1]);
        uint8_t attempt = 0;

        while (getState(index) != slotState::Empty) {
            if(attempt++ == cap_) return false;
            if (table[index].first == key) {
                if (getState(index) == slotState::Used) {
                    setState(index, slotState::Deleted);
                    --size_;
                    // note : consider rehash when there are too many tombstones in map
                    return true;
                } else if (getState(index) == slotState::Deleted) {
                    return false;
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return false;
    }

    /**
     * @brief Finds an element with the specified key.
     * @param key The key to find.
     * @return Iterator to the element if found, otherwise end().
     */
    iterator find(V key) noexcept {
        uint8_t index = hashFunction(cap_, key, best_hashers_16[cap_ - 1]);
        uint8_t attempt = 0;
        // Search for a cell whose is used and matched key
        slotState st = getState(index);
        while (st != slotState::Empty) {
            if (attempt++ == cap_){
                return end();
            }
            st = getState(index);
            if(table[index].first == key){
                if(st == slotState::Used){
                    return iterator(this, index);
                }
                else if(st == slotState::Deleted){
                    return end();
                }
            }
            index = linearProbe(cap_, index, step_);
        }
        return end();
    }


    /**
     * @brief Access or insert an element.
     * @param key The key to find or insert.
     * @return Reference to the mapped value at the specified key.
     * @note If the key does not exist, a new element with default-constructed value is inserted.
     */
    T& operator[](V key) noexcept {
        iterator it = find(key);
        if (it != end()) {
            return it->second;
        } else {
            return insert(key, T()).first->second; // Insert and return reference
        }
    }

    /**
     * @brief Access an element with bounds checking.
     * @param key The key to find.
     * @return Reference to the mapped value at the specified key.
     * @note Returns MAP_DEFAULT_VALUE if key is not found.
     */
    T& at(V key) noexcept {
        iterator it = find(key);
        if (it != end()) {
            return it->second;
        } else {
            return MAP_DEFAULT_VALUE; // or throw an exception
            // throw std::out_of_range("key not found !");
        }
    }
    /**
     * @brief Checks if this map contains the same elements as another.
     * @param other The map to compare with.
     * @return true if maps are equal, false otherwise.
     */
    bool operator==(const unordered_map& other) const noexcept {
        if (size_ != other.size_) return false;
        for (uint8_t i = 0; i < cap_; ++i) {
            if (getState(i) == slotState::Used && !other.contains(table[i].first)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Checks if this map differs from another.
     * @param other The map to compare with.
     * @return true if maps are not equal, false otherwise.
     */
    bool operator!=(const unordered_map& other) const noexcept {
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
     * @brief Sets the fullness factor for the map.
     * @param fullness The new fullness factor (0.1 to 1.0 or 10 to 100).
     * @return true if successful, false if the new fullness would overflow the map.
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
            fullness_ = static_cast<uint8_t>(fullness * 100);
        }else{
            fullness_ = static_cast<uint8_t>(fullness);
        }
        if(map_ability() < size_){
            fullness_ = old_fullness_;
            return false;
        }
        virtual_cap = cap_to_virtual();
        return true;
    }

    /**
     * @brief Checks if the map contains an element with the specified key.
     * @param key The key to find.
     * @return true if the key exists, false otherwise.
     */
    bool contains(V key) noexcept {
        return find(key) != end();
    }
    
    /**
     * @brief Shrinks the map's capacity to fit its size.
     * @return Number of bytes freed by shrinking.
     */
    size_t fit() noexcept {
        if (size_ < cap_) {
            uint8_t oldCap = cap_;
            size_t flagBytes = (oldCap * 2 + 7) / 8;
            
            size_t target_buckets = std::max<uint8_t>(
                (size_ * 100 + fullness_ - 1) / fullness_, INIT_CAP);
            rehash(target_buckets);
            // Calculate bytes saved:
            size_t tableSaved = (oldCap - cap_) * sizeof(Pair);
            size_t flagsSaved = flagBytes - ((cap_ * 2 + 7) / 8);
            return tableSaved + flagsSaved;
        }
        return 0;
    }
    
    /**
     * @brief Removes all elements from the map.
     * @note Keeps the allocated memory for reuse.
     */
    void clear() noexcept {
        memset(flags, 0, (cap_ * 2 + 7) / 8);
        size_ = 0;
    }

    /**
     * @brief Reserves space for a specified number of elements.
     * @param new_virtual_cap The new virtual capacity to reserve.
     * @return true if successful, false if the requested capacity is too large.
     * @note This prepares the map to hold the specified number of elements without rehashing.
     */
    bool reserve(uint8_t new_virtual_cap) noexcept {
        // uint8_t newCap = static_cast<uint8_t>(cap_ * (100.0f / fullness_));
        uint8_t newCap = virtual_to_cap(new_virtual_cap);
        if (newCap > MAX_CAP) return false;
        if (newCap < size_) newCap = size_;
        if (newCap == cap_) return true;
        rehash(newCap);
        return true;
    }

    /**
     * @brief Gets the maximum theoretical number of elements the map can hold.
     * @return Maximum capacity based on the current fullness setting.
     */
    uint16_t map_ability() const noexcept {
        return static_cast<uint16_t>(MAX_CAP) * fullness_ / 100;
    }
    /**
     * @brief Gets the current number of elements in the map.
     * @return The element count.
     */
    uint16_t size() const noexcept { return size_; }

    /**
     * @brief Gets the current virtual capacity of the map.
     * @return The current virtual capacity.
     */
    uint16_t capacity() const noexcept { 
        return virtual_cap;
    }

    /**
     * @brief Checks if the map is empty.
     * @return true if the map contains no elements, false otherwise.
     */
    bool empty() const noexcept { return size_ == 0; }

    /**
     * @brief Calculates the total memory usage of the map.
     * @return Memory usage in bytes.
     * @note Includes the map object, elements table, and flag array.
     */
    size_t memory_usage() const noexcept {
        size_t table_bytes = static_cast<size_t>(cap_) * sizeof(Pair);
        size_t flags_bytes = (cap_ * 2 + 7) / 8;
        return sizeof(*this) + table_bytes + flags_bytes;
    }

    /**
     * @brief Swaps the contents of two maps.
     * @param other The map to swap with.
     */
    void swap(unordered_map& other) noexcept {
        std::swap(table, other.table);
        std::swap(flags, other.flags);
        std::swap(cap_, other.cap_);
        std::swap(size_, other.size_);
        std::swap(fullness_, other.fullness_);
        std::swap(virtual_cap, other.virtual_cap);
        std::swap(step_, other.step_);
    }
};
template<typename V, typename T>
T unordered_map<V, T>::MAP_DEFAULT_VALUE = T(); // Default value for MAP_DEFAULT_VALUE
