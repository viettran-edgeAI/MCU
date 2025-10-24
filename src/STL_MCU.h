// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
#include "hash_kernel.h"
#include "initializer_list.h"
#include <type_traits>
#include <cassert>
#include <utility>

#define hashers best_hashers_16 // change to best_hashers_8 to save 255 bytes of disk space, but more collisions

// PSRAM Configuration
// Users can define RF_USE_PSRAM before including this header to enable PSRAM allocation
// Example: #define RF_USE_PSRAM
#if defined(RF_USE_PSRAM) && (defined(ESP32) || defined(ARDUINO_ARCH_ESP32))
    #include "esp_heap_caps.h"
    #define RF_PSRAM_AVAILABLE 1
#else
    #define RF_PSRAM_AVAILABLE 0
#endif

// #include <cstring>
// #include <iostream>
namespace mcu {

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
        uint8_t dead_size_ = 0;     // used + tombstones 
        uint8_t fullness_ = 92; //(%)       . virtual_cap = cap_ * fullness_ / 100
        uint8_t virtual_cap = 0; // virtual capacity
        uint8_t step_ = 0;
        // cap_        : for internal use 
        // virtual_cap : for user
        // ----------------------------------------------  : table size
        // --------------|--------------------|----------|
        //             size_             virtual_cap    cap_
        static T MAP_DEFAULT_VALUE;

        bool rehash(uint8_t newCap) noexcept {
            if (newCap < size_) newCap = size_;
            if (newCap > MAX_CAP) newCap = MAX_CAP;
            if (newCap == 0) newCap = INIT_CAP;

            Pair* newTable = mem_alloc::allocate<Pair>(newCap);
            if (!newTable) {
                return false;
            }

            const size_t flagBytes = (static_cast<size_t>(newCap) * 2 + 7) / 8;
            uint8_t* newFlags = new (std::nothrow) uint8_t[flagBytes];
            if (!newFlags) {
                mem_alloc::deallocate(newTable);
                return false;
            }
            std::fill_n(newFlags, flagBytes, static_cast<uint8_t>(0));

            Pair* oldTable = table;
            uint8_t* oldFlags = flags;
            uint8_t oldCap = cap_;

            table = newTable;
            flags = newFlags;
            cap_ = newCap;
            size_ = 0;
            dead_size_ = 0;
            virtual_cap = cap_to_virtual();
            step_ = calStep(newCap);

            if (oldTable && oldFlags) {
                for (uint8_t i = 0; i < oldCap; ++i) {
                    if (getStateFrom(oldFlags, i) == slotState::Used) {
                        insert(oldTable[i]);
                    }
                }
            }

            if (oldFlags) {
                delete[] oldFlags;
            }
            mem_alloc::deallocate(oldTable);
            return true;
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
    protected:
        int16_t getValue(V key) noexcept {
            if (cap_ == 0 || table == nullptr) {
                return -1;
            }
            uint8_t index     = hashFunction(cap_, key, hashers[cap_ - 1]);
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
            if (cap_ == 0 || table == nullptr) {
                return -1;
            }
            uint8_t index     = hashFunction(cap_, key, hashers[cap_ - 1]);
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
            mem_alloc::deallocate(table);
        }

        /**
         * @brief Copy constructor, creates a deep copy of another map.
         * @param other The map to copy from.
         */
        unordered_map(const unordered_map& other) noexcept : hash_kernel(),
            slot_handler(other),        
            size_(other.size_),
            dead_size_(other.dead_size_),
            fullness_(other.fullness_),
            virtual_cap(other.virtual_cap),
            step_(other.step_)
        {
            table = mem_alloc::allocate<Pair>(cap_);
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
        dead_size_(other.dead_size_),
        fullness_(other.fullness_),
        virtual_cap(other.virtual_cap),
        step_(other.step_)
        {
            table       = other.table;
            other.table = nullptr;
            other.size_ = 0;
            other.dead_size_ = 0;
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
                mem_alloc::deallocate(table);
                slot_handler::operator=(other);  // copy flags & cap_
                size_      = other.size_;
                fullness_  = other.fullness_;
                virtual_cap = other.virtual_cap;
                step_      = other.step_;
                table      = mem_alloc::allocate<Pair>(cap_);
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
                mem_alloc::deallocate(table);
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
            if (dead_size_ >= virtual_cap) {
                if (size_ == map_ability())
                    return { end(), false };
                uint16_t dbl = cap_ ? cap_ * 2: INIT_CAP;
                if (dbl > MAX_CAP) dbl = MAX_CAP;
                if (!rehash(static_cast<uint8_t>(dbl))) {
                    return { end(), false };
                }
            }

            if (cap_ == 0 || table == nullptr) {
                return { end(), false };
            }

            V key       = p.first;
            uint8_t index     = hashFunction(cap_, key, hashers[cap_ - 1]);

            while (getState(index) != slotState::Empty) {
                slotState st = getState(index);
                if (table[index].first == key) {
                    if (st == slotState::Used) {
                        // existing element
                        return { iterator(this, index), false };
                    }
                    if (st == slotState::Deleted) {         // reuse this tombstone
                        break;
                    }
                }
                index = linearProbe(cap_, index, step_);
            }

            slotState old_state = getState(index);
            table[index] = std::move(p);
            setState(index, slotState::Used);
            ++size_;
            if (old_state == slotState::Empty) {
                ++dead_size_;  
            }
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
            if (cap_ == 0 || table == nullptr) {
                return false;
            }
            uint8_t index = hashFunction(cap_, key, hashers[cap_ - 1]);
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
            if (cap_ == 0 || table == nullptr) {
                return end();
            }
            uint8_t index = hashFunction(cap_, key, hashers[cap_ - 1]);
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
                const uint8_t oldCap = cap_;
                const size_t oldFlagBytes = (static_cast<size_t>(oldCap) * 2 + 7) / 8;

                uint8_t target_buckets = std::max<uint8_t>(
                    (size_ * 100 + fullness_ - 1) / fullness_, INIT_CAP);
                if (!rehash(target_buckets)) {
                    return 0;
                }
                // Calculate bytes saved:
                size_t tableSaved = (oldCap > cap_) ?
                    static_cast<size_t>(oldCap - cap_) * sizeof(Pair) : 0;
                size_t flagsSaved = (oldFlagBytes > ((static_cast<size_t>(cap_) * 2 + 7) / 8)) ?
                    oldFlagBytes - ((static_cast<size_t>(cap_) * 2 + 7) / 8) : 0;
                return tableSaved + flagsSaved;
            }
            return 0;
        }
        
        /**
         * @brief Removes all elements from the map.
         * @note Keeps the allocated memory for reuse.
         */
        void clear() noexcept {
            if (!flags) {
                size_ = 0;
                dead_size_ = 0;
                return;
            }
            std::fill_n(flags, (static_cast<size_t>(cap_) * 2 + 7) / 8, static_cast<uint8_t>(0));
            size_ = 0;
            dead_size_ = 0;
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
            return rehash(newCap);
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
         * @brief Checks if the table pointer is allocated in PSRAM.
         * @return true if table is in PSRAM, false if in DRAM or null.
         */
        bool is_table_in_psram() const noexcept {
            return mem_alloc::is_psram_ptr(table);
        }

        /**
         * @brief Gets the table pointer for debugging (returns nullptr if not allocated).
         * @return Pointer to the internal table (may be in PSRAM or DRAM).
         */
        const Pair* get_table_ptr() const noexcept {
            return table;
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
            std::swap(dead_size_, other.dead_size_);
            std::swap(fullness_, other.fullness_);
            std::swap(virtual_cap, other.virtual_cap);
            std::swap(step_, other.step_);
        }
    };
    template<typename V, typename T>
    T unordered_map<V, T>::MAP_DEFAULT_VALUE = T(); // Default value for MAP_DEFAULT_VALUE
    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------- UNORDERED_SET -----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

    template<typename T>
    class unordered_set : public hash_kernel, public slot_handler {
    private:
        T* table = nullptr;
        uint8_t size_ = 0;
        uint8_t dead_size_ = 0;     // used + tombstones
        uint8_t fullness_ = 92; //(%)       . virtual_cap = cap_ * fullness_ / 100
        uint8_t virtual_cap = 0; // virtual capacity
        uint8_t step_ = 0;

        bool rehash(uint8_t newCap) noexcept {
            if (newCap < size_) newCap = size_;
            if (newCap > MAX_CAP) newCap = MAX_CAP;
            if (newCap == 0) newCap = INIT_CAP;

            T* newTable = mem_alloc::allocate<T>(newCap);
            if (!newTable) {
                return false;
            }

            const size_t flagBytes = (static_cast<size_t>(newCap) * 2 + 7) / 8;
            uint8_t* newFlags = new (std::nothrow) uint8_t[flagBytes];
            if (!newFlags) {
                mem_alloc::deallocate(newTable);
                return false;
            }
            std::fill_n(newFlags, flagBytes, static_cast<uint8_t>(0));

            T* oldTable = table;
            uint8_t* oldFlags = flags;
            uint8_t oldCap = cap_;

            table = newTable;
            flags = newFlags;
            cap_ = newCap;
            size_ = 0;
            dead_size_ = 0;
            virtual_cap = cap_to_virtual();
            step_ = calStep(newCap);

            if (oldTable && oldFlags) {
                for (uint8_t i = 0; i < oldCap; ++i) {
                    if (getStateFrom(oldFlags, i) == slotState::Used) {
                        insert(oldTable[i]);
                    }
                }
            }

            if (oldFlags) {
                delete[] oldFlags;
            }
            mem_alloc::deallocate(oldTable);
            return true;
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
            mem_alloc::deallocate(table);
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
            table = mem_alloc::allocate<T>(cap_);
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
                mem_alloc::deallocate(table);
                slot_handler::operator=(other);  // copy flags & cap_
                fullness_ = other.fullness_;
                virtual_cap = other.virtual_cap;
                step_ = other.step_;
                cap_ = other.cap_;
                size_ = other.size_;
                dead_size_ = other.dead_size_;
                table = mem_alloc::allocate<T>(cap_);
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
                mem_alloc::deallocate(table);
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
                if (!rehash(static_cast<uint8_t>(dbl))) {
                    return false;
                }
            }

            if (cap_ == 0 || table == nullptr) {
                return false;
            }
            
            uint8_t index = hashFunction(cap_, value, hashers[cap_ - 1]);
            
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
            if (cap_ == 0 || table == nullptr) {
                return false;
            }
            uint8_t index = hashFunction(cap_, value, hashers[cap_ - 1]);
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
            if (cap_ == 0 || table == nullptr) {
                return end();
            }
            uint8_t index = hashFunction(cap_, value, hashers[cap_ - 1]);
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
                const uint8_t oldCap = cap_;
                const size_t oldFlagBytes = (static_cast<size_t>(oldCap) * 2 + 7) / 8;
                if (!rehash(size_)) {
                    return 0;
                }
                const size_t newFlagBytes = (static_cast<size_t>(cap_) * 2 + 7) / 8;
                const size_t tableSaved = (oldCap > cap_) ?
                    static_cast<size_t>(oldCap - cap_) * sizeof(T) : 0;
                const size_t flagsSaved = (oldFlagBytes > newFlagBytes) ?
                    oldFlagBytes - newFlagBytes : 0;
                return tableSaved + flagsSaved;
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
            return rehash(newCap);
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
            return rehash(newCap);
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
            if (!flags) {
                size_ = 0;
                dead_size_ = 0;
                return;
            }
            std::fill_n(flags, (static_cast<size_t>(cap_) * 2 + 7) / 8, static_cast<uint8_t>(0));
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
         * @brief Checks if the table pointer is allocated in PSRAM.
         * @return true if table is in PSRAM, false if in DRAM or null.
         */
        bool is_table_in_psram() const noexcept {
            return mem_alloc::is_psram_ptr(table);
        }

        /**
         * @brief Gets the table pointer for debugging (returns nullptr if not allocated).
         * @return Pointer to the internal table (may be in PSRAM or DRAM).
         */
        const T* get_table_ptr() const noexcept {
            return table;
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

    

/*
    ------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------- VECTOR ---------------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
*/
    // Forward declarations (for b_vector <-> vector conversion mechanism)
    template<typename T, size_t sboSize>
    class b_vector;
    
    template<typename T>
    class vector;

    // vector with small buffer optimization (SBO)
    template<typename T, size_t sboSize = 0>
    class b_vector : hash_kernel {
    private:
        using vector_index_type = size_t;
        
        // Helper function to construct from another b_vector (reduces code duplication)
        template<size_t otherSboSize>
        void construct_from_other(const b_vector<T, otherSboSize>& other) noexcept {
            if (other.size() <= SBO_SIZE) {
                // Can fit in our SBO buffer
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    if (i < size_) {
                        new(buffer_ptr + i) T(other[i]);
                    } else {
                        new(buffer_ptr + i) T();
                    }
                }
            } else {
                // Need to use heap
                using_heap = true;
                capacity_ = size_;
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = other[i];
                }
            }
        }
        
        // Helper function to assign from another b_vector (reduces code duplication)
        template<size_t otherSboSize>
        void assign_from_other(const b_vector<T, otherSboSize>& other) noexcept {
            // First, correctly destroy the elements in `this`
            if (using_heap) {
                mem_alloc::deallocate(heap_array);
            } else {
                T* p = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    p[i].~T();
                }
            }

            size_ = other.size();
            if (size_ <= SBO_SIZE) {
                // Use SBO
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    if (i < size_) {
                        new(buffer_ptr + i) T(other[i]);
                    } else {
                        new(buffer_ptr + i) T();
                    }
                }
            } else {
                // Use heap
                using_heap = true;
                capacity_ = size_;
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = other[i];
                }
            }
        }
        
        // Small Buffer Optimization - auto-adjust SBO size based on sizeof(T)
        // If sboSize is explicitly provided (non-zero), use it; otherwise auto-calculate
        static constexpr size_t calculateSboSize() {
            if constexpr (sboSize != 0) {
                return sboSize;  // User-specified size
            } else {
                // Auto-calculate based on sizeof(T)
                constexpr size_t type_size = sizeof(T);
                if constexpr (type_size == 1) {
                    return 32;  // 32 bytes for 1-byte types (char, uint8_t, etc.)
                } else if constexpr (type_size == 2) {
                    return 16;  // 16 elements for 2-byte types (short, uint16_t, etc.)
                } else if constexpr (type_size == 4) {
                    return 8;   // 8 elements for 4-byte types (int, float, uint32_t, etc.)
                } else if constexpr (type_size == 8) {
                    return 4;   // 4 elements for 8-byte types (double, uint64_t, etc.)
                } else if constexpr (type_size <= 16) {
                    return 2;   // 2 elements for types up to 16 bytes
                } else {
                    return 1;   // 1 element for very large types
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
            T* newArray = mem_alloc::allocate<T>(newCapacity);
            vector_index_type toCopy = (size_ < newCapacity ? size_ : newCapacity);
            customCopy(heap_array, newArray, toCopy);
            mem_alloc::deallocate(heap_array);
            heap_array = newArray;
            capacity_ = newCapacity;
            if (size_ > capacity_) size_ = capacity_;
        }
        
        // Switch from buffer to heap storage
        void switch_to_heap(vector_index_type new_capacity) noexcept {
            if (using_heap) return;
            
            T* old_buffer_ptr = reinterpret_cast<T*>(buffer);
            T* new_heap = mem_alloc::allocate<T>(new_capacity);
            
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
        explicit b_vector(vector_index_type initialCapacity) noexcept : size_(0) {
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
                heap_array = mem_alloc::allocate<T>(initialCapacity);
                // No need to initialize elements since size is 0
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
                heap_array = mem_alloc::allocate<T>(initialCapacity);
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
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (unsigned i = 0; i < init.size(); ++i)
                    heap_array[i] = init.data_[i];
            }
        }

        // Copy constructor
        b_vector(const b_vector& other) noexcept : size_(other.size_), using_heap(other.using_heap) {
            if (other.using_heap) {
                capacity_ = other.capacity_;
                heap_array = mem_alloc::allocate<T>(capacity_);
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

        // Unified templated copy constructor for different SBO sizes (handles both const and non-const)
        template<size_t otherSboSize>
        b_vector(const b_vector<T, otherSboSize>& other) noexcept : size_(other.size()) {
            construct_from_other(other);
        }
        
        template<size_t otherSboSize>
        b_vector(b_vector<T, otherSboSize>& other) noexcept : size_(other.size()) {
            construct_from_other(other);
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
                mem_alloc::deallocate(heap_array);
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
                T* new_heap = mem_alloc::allocate<T>(other.capacity_);
                if (!new_heap) {
                    // Allocation failed. Leave `this` unmodified to maintain a valid state.
                    return *this;
                }
                customCopy(other.heap_array, new_heap, other.size_);

                // New data is ready. Now, safely destroy the old data.
                if (using_heap) {
                    mem_alloc::deallocate(heap_array);
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
                    mem_alloc::deallocate(heap_array);
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

        // Unified templated copy assignment for different SBO sizes (const version)
        template<size_t otherSboSize>
        b_vector& operator=(const b_vector<T, otherSboSize>& other) noexcept {
            if (reinterpret_cast<const void*>(this) == reinterpret_cast<const void*>(&other)) {
                return *this;
            }
            assign_from_other(other);
            return *this;
        }
        
        // Unified templated copy assignment for different SBO sizes (non-const version)
        template<size_t otherSboSize>
        b_vector& operator=(b_vector<T, otherSboSize>& other) noexcept {
            if (reinterpret_cast<const void*>(this) == reinterpret_cast<const void*>(&other)) {
                return *this;
            }
            assign_from_other(other);
            return *this;
        }

        // Templated move constructor for different SBO sizes
        template<size_t otherSboSize>
        b_vector(b_vector<T, otherSboSize>&& other) noexcept : size_(other.size()) {
            if (other.size() <= SBO_SIZE) {
                // Can fit in our SBO buffer
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    if (i < size_) {
                        new(buffer_ptr + i) T(std::move(other[i]));
                    } else {
                        new(buffer_ptr + i) T();
                    }
                }
            } else {
                // Need to use heap - can't move the heap buffer due to different types,
                // so we copy instead
                using_heap = true;
                capacity_ = size_;
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = std::move(other[i]);
                }
            }
            
            // Clear the other vector
            other.clear();
        }

        // Move assignment
        b_vector& operator=(b_vector&& other) noexcept {
            if (this != &other) {
                // First, correctly destroy the elements in `this`
                if (using_heap) {
                    mem_alloc::deallocate(heap_array);
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

        // Templated move assignment for different SBO sizes
        template<size_t otherSboSize>
        b_vector& operator=(b_vector<T, otherSboSize>&& other) noexcept {
            if (reinterpret_cast<const void*>(this) == reinterpret_cast<const void*>(&other)) {
                return *this;
            }

            // First, correctly destroy the elements in `this`
            if (using_heap) {
                mem_alloc::deallocate(heap_array);
                heap_array = nullptr;
            } else {
                T* p = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    p[i].~T();
                }
            }

            size_ = other.size();
            if (size_ <= SBO_SIZE) {
                // Use SBO
                using_heap = false;
                capacity_ = SBO_SIZE;
                T* buffer_ptr = reinterpret_cast<T*>(buffer);
                for (vector_index_type i = 0; i < SBO_SIZE; ++i) {
                    if (i < size_) {
                        new(buffer_ptr + i) T(std::move(other[i]));
                    } else {
                        new(buffer_ptr + i) T();
                    }
                }
            } else {
                // Use heap
                using_heap = true;
                capacity_ = size_;
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = std::move(other[i]);
                }
            }

            // Clear the other vector
            other.clear();
            
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
                mem_alloc::deallocate(heap_array);
                heap_array = nullptr;
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
                heap_array = mem_alloc::allocate<T>(capacity_);
                for (vector_index_type i = 0; i < size_; ++i) {
                    heap_array[i] = other[i];
                }
            }
            
            return *this;
        }

        void fill(const T& value) noexcept {
            T* ptr = data_ptr();
            for (vector_index_type i = 0; i < capacity_; ++i) {
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
            static_assert(std::is_arithmetic<T>::value || less_comparable<T>::value,
                          "Type T must be numeric or support operator< (returning convertible-to-bool) for sorting.");
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
            T* newArray = mem_alloc::allocate<T>(newCapacity);
            size_t toCopy = (size_ < newCapacity ? size_ : newCapacity);
            customCopy(array, newArray, toCopy);
            mem_alloc::deallocate(array);
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
            : array(mem_alloc::allocate<T>(1)), size_(0), capacity_(1) {}

        // Constructor with initial capacity
        explicit vector(size_t initialCapacity) noexcept
            : array(mem_alloc::allocate<T>((initialCapacity == 0) ? 1 : initialCapacity)),
            size_(initialCapacity), // Set size equal to capacity
            capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            // fix for error : access elements throgh operator[] when vector just initialized
            for (size_t i = 0; i < size_; ++i) {
                array[i] = T();
            }
        }

        // Constructor with initial size and value
        explicit vector(size_t initialCapacity, const T& value) noexcept
            : array(mem_alloc::allocate<T>((initialCapacity == 0) ? 1 : initialCapacity)),
            size_(initialCapacity), capacity_((initialCapacity == 0) ? 1 : initialCapacity) {
            for (size_t i = 0; i < size_; ++i) {
                array[i] = value;
            }
        }

        // Constructor: from min_init_list<T>
        vector(const mcu::min_init_list<T>& init) noexcept
            : array(mem_alloc::allocate<T>(init.size())), size_(init.size()), capacity_(init.size()) {
            for (unsigned i = 0; i < init.size(); ++i)
                array[i] = init.data_[i];
        }

        // Copy constructor
        vector(const vector& other) noexcept
            : array(mem_alloc::allocate<T>(other.capacity_)),
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
            mem_alloc::deallocate(array);
        }

        // Copy assignment
        vector& operator=(const vector& other) noexcept {
            if (this != &other) {
                T* newArray = mem_alloc::allocate<T>(other.capacity_);
                customCopy(other.array, newArray, other.size_);
                mem_alloc::deallocate(array);
                array    = newArray;
                size_    = other.size_;
                capacity_ = other.capacity_;
            }
            return *this;
        }

        // Move assignment
        vector& operator=(vector&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(array);
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
            mem_alloc::deallocate(array);
            
            // Allocate new space
            size_ = other.size();
            capacity_ = size_ > 0 ? size_ : 1;
            array = mem_alloc::allocate<T>(capacity_);
            
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
            static_assert(std::is_arithmetic<T>::value || less_comparable<T>::value,
                          "Type T must be numeric or support operator< (returning convertible-to-bool) for sorting.");
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
    /*
    -------------------------------------------------------------------------------------------------------------------
    ------------------------------------------------- PACKED VECTOR ---------------------------------------------------
    -------------------------------------------------------------------------------------------------------------------
    */
    
    template<uint8_t BitsPerElement>
    class PackedArray {
        static_assert(BitsPerElement > 0 && BitsPerElement <= 8, "Invalid bit size");
    uint8_t* data = nullptr;
    uint8_t bpv_ = BitsPerElement;  // Runtime bits per value
    size_t capacity_bytes_ = 0;

    public:
        // Default constructor - creates empty array
        PackedArray() : data(nullptr), bpv_(BitsPerElement), capacity_bytes_(0) {}

        // Remove count - capacity is managed by packed_vector
        PackedArray(size_t capacity_bytes) : bpv_(BitsPerElement), capacity_bytes_(capacity_bytes) {
            if (capacity_bytes_ > 0) {
                data = mem_alloc::allocate<uint8_t>(capacity_bytes_);
                if (data) {
                    std::fill_n(data, capacity_bytes_, static_cast<uint8_t>(0));
                } else {
                    capacity_bytes_ = 0;
                }
            } else {
                data = nullptr;
            }
        }

        ~PackedArray() {
            mem_alloc::deallocate(data);
            capacity_bytes_ = 0;
        }

        // Copy constructor - requires byte size
        PackedArray(const PackedArray& other, size_t bytes)
            : bpv_(other.bpv_), capacity_bytes_(bytes) {
            if (capacity_bytes_ > 0) {
                data = mem_alloc::allocate<uint8_t>(capacity_bytes_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_bytes_, data);
                    } else {
                        std::fill_n(data, capacity_bytes_, static_cast<uint8_t>(0));
                    }
                } else {
                    capacity_bytes_ = 0;
                }
            } else {
                data = nullptr;
            }
        }

        // Move constructor
        PackedArray(PackedArray&& other) noexcept
            : data(other.data), bpv_(other.bpv_), capacity_bytes_(other.capacity_bytes_) {
            other.data = nullptr;
            other.capacity_bytes_ = 0;
        }

        // Copy from another PackedArray with specified byte size
        void copy_from(const PackedArray& other, size_t bytes) {
            if (this == &other) {
                bpv_ = other.bpv_;
                capacity_bytes_ = bytes;
                return;
            }

            mem_alloc::deallocate(data);
            data = nullptr;
            capacity_bytes_ = bytes;
            if (capacity_bytes_ > 0) {
                data = mem_alloc::allocate<uint8_t>(capacity_bytes_);
                if (data) {
                    if (other.data) {
                        std::copy(other.data, other.data + capacity_bytes_, data);
                    } else {
                        std::fill_n(data, capacity_bytes_, static_cast<uint8_t>(0));
                    }
                } else {
                    capacity_bytes_ = 0;
                }
            } else {
                data = nullptr;
            }
            bpv_ = other.bpv_;
        }

        // Copy assignment
        PackedArray& operator=(const PackedArray& other) {
            if (this != &other) {
                copy_from(other, other.capacity_bytes_);
            }
            return *this;
        }

        // Move assignment
        PackedArray& operator=(PackedArray&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(data);
                data = other.data;
                bpv_ = other.bpv_;
                capacity_bytes_ = other.capacity_bytes_;
                other.data = nullptr;
                other.capacity_bytes_ = 0;
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
            uint8_t clamped = value & get_max_value();
            vector_index_type target_capacity = get_capacity();

            if (target_capacity == 0) {
                set_size(0);
                return;
            }

            for (vector_index_type i = 0; i < target_capacity; ++i) {
                packed_data.set_unsafe(i, clamped);
            }
            set_size(target_capacity);
        }
        
        __attribute__((always_inline)) inline uint8_t operator[](vector_index_type index) const {
            vector_index_type current_size = get_size();
            if (current_size == 0) {
                return 0;
            }
            if (index >= current_size) {
                return packed_data.get_unsafe(current_size - 1);
            }
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

    /*  
    ------------------------------------------------------------------------------------------------------------------
    --------------------------------------------------- ID_VECTOR ----------------------------------------------------
    ------------------------------------------------------------------------------------------------------------------
    */

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
            (sizeof(index_type) == 1), uint16_t,   // uint8_t -> uint32_t (4 bytes)
            typename conditional_t<
                (sizeof(index_type) == 2), size_t,   // uint16_t -> uint64_t (8 bytes)
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
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1; // number of IDs in range
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
                index_type old_range = (size_t)max_id_  -  (size_t)min_id_ + 1;
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
                index_type old_range = (size_t)max_id_  -  (size_t)min_id_ + 1;
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
                index_type old_range = (size_t)max_id_  -  (size_t)min_id_ + 1;
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
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
            size_t total_bits = range * BitsPerValue;
            size_t bytes = bits_to_bytes(total_bits);
            id_array = PackedArray<BitsPerValue>(bytes);
            id_array.copy_from(other.id_array, bytes);
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
                
                index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
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
            
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
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
            index_type range = (size_t)max_id_  -  (size_t)min_id_ + 1;
            size_t total_bits = range * BitsPerValue;
            size_t bytes = bits_to_bytes(total_bits);
            return sizeof(ID_vector) + bytes;
        }

        // takeout normalized vector of IDs (ascending order, no repetitions)
    };

    /*
    ------------------------------------------------------------------------------------------------------------------
    ---------------------------------------------- CHAINED UNORDERED MAP -------------------------------------------------
    ----------------------------------------------------------------------------------------------------------------------
    */

    // Chained Unordered Map : dedicated to machine learning tasks, big data processing: key is always in numeric form, 
    //                         can contain more elements than unordered_map (~ 60000 elements)
    // ------------------------------------------- ChainedUnorderedMap ----------------------------------------
    /*
        - chain is made up of consecutive maps (unordered_map)
        - there are 3 types of maps:
            + Available maps    : maps that have been activated and are being used .
                                : flags - Used. (contains at least 1 element) or - Empty. (when just initialized)
            + reserve map type 1: maps that has not been activated.
                                : flags - Empty. (4 bytes)
            + reserve map type 2: flags - deleted. is an available map but is empty during deletion and shrink to fit after that.
                                does not require reactivation to use (12 bytes)
        - Each map corresponds to a range of values: cmap_ability = 255 * fullness_ / 100;
        - the rangeMap vector will be responsible for referencing the mapID to the ranges.
            + each map have cmap_ability - which is maximum capacity of each map can handle.(same for all maps)
            + suppose cmap_ability = 229. 0->228 : range 0 | 229 - 457 : range 1 | 458 - 688 : range 3 .. 
            + the first element added is 500 => range 2 - mapID 0, then the same goes for map 1, 2..
                + note: 
                    - mapID is the index in the chain.
                    - mapID is also the index in the vector rangeMap.range will be the value at that index.

        - the maps will be initialized and attached to the chain in turn according to time: 
            + the first map in the chain is mapID 0, the second is mapID 1, and so on.
            + The first range that appears will be assigned with mapID 0, similarly, the next ranges appear will be 
            assigned to the next maps in turn.

        - Maps are always used contiguously and consecutively from the beginning of the chain. If there is a type 2 
        reserve map between these maps (called a gap between maps), the next element added that does not belong to 
        any existing range in the chain, will be inserted into that map (and assigned a new range to that map).

        - activated maps will not be disabled anymore, but will only be returned to the type 2 reserve map.
        in other words, the chain will not be collapsed, unless - remap() is performed

        - only range assigned to the mapID will be used, the rest will be ignored.
        
        - after a map change from avaiable -> reserve type2, it still keep range respond to it until it being reused.

        - cap_ will only change at : remap() and wrapper functions that call remap 
    */

    // note : - in chainedUnorderedMap, slot_handler manages maps instead of pair elements 
    //        - cap_ : total number of maps in the chain (available + reserve type 1, 2) - size of dynamic array (chain)


    template <typename R, typename T>
    class ChainedUnorderedMap : public slot_handler, hash_kernel{
    private:
        using Pair_RT = pair<R, T>;             // for iterator - changed from Pair_16
        using pair_kmi = pair<int16_t, uint8_t>;   // mapID - range, for keyMappingIN 
        using unordered_map_s = unordered_map<R, T>;
                                                                                                        
        unordered_map_s** chain = nullptr;  
        unordered_map<uint8_t, uint8_t> rangeMap;        // Which mapID corresponds to which range?
                                                        // key-range, value-mapID(both unique). only contains used map
        uint8_t fullness_ = 92;                 // maximum map fill level (in %) at each map
        uint8_t cmap_ability = 234;             // Maximum capacity of each map (234 = 92% of 255)
        uint8_t chain_size = 0;                 // number of used maps in the chain ( which are not empty )
    
        
        // maximum capacity of each member map in the chain
        inline void recalculate_cmap_ability() noexcept {
            cmap_ability = static_cast<uint8_t>(255 * fullness_ / 100);
        }
        // activate a new map
        // @ return : true - mapID is activated
        void activate_map(uint8_t mapID) noexcept{
            if (mapID >= cap_) {
                return;
            }
            if(chain[mapID] != nullptr) {
                return;   // mapID already activated
            }else{
                chain[mapID] = new unordered_map_s();
                // rangeMap[mapID] = 0;  // rangeMap change
                chain_size++;
                chain[mapID]->set_fullness(fullness_); // set fullness of the map
                return;
            }
        }

        // action-IN : for keyMappingIN - updated to use generic key type R
        [[nodiscard]] inline pair_kmi keyMappingIN(const R& key) const noexcept {
            size_t tranform_key = preprocess_hash_input(key);
            uint8_t range;
            if constexpr (std::is_integral<R>::value){
                range = static_cast<uint8_t>(tranform_key / cmap_ability);
            }else if constexpr (std::is_floating_point<R>::value){
                range = tranform_key % cmap_ability;
            }
            int16_t mapID = rangeMap.getValue(range); // Get mapID from range
            return pair_kmi(mapID, range); // Return pair of mapID and range
        }


        // in use (only available maps) , contains at least 1 element
        inline bool map_in_use(uint8_t mapID) const noexcept {
            if(getState(mapID) == slotState::Used) {
                return true;
            }
            return false;
        }

        // return number of maps in chain (available + reserve)
        uint16_t chainCap() const noexcept{
            return cap_;
        }
        void remap(uint16_t newChainCap) noexcept {
            // Ensure the new capacity can accommodate existing maps
            if (newChainCap < chain_size) newChainCap = chain_size;
            if (newChainCap > MAX_CAP) newChainCap = MAX_CAP;

            // Store old resources if they exist
            unordered_map_s** oldChain = chain;
            uint8_t* oldFlags = flags;
            uint8_t oldCap = cap_;

            const size_t flagBytes = (newChainCap * 2 + 7) / 8;
            uint8_t* newFlags = nullptr;
            try {
                newFlags = new uint8_t[flagBytes];
            } catch (...) {
                newFlags = nullptr;
            }
            if (!newFlags) {
                return; // Allocation failed, keep existing resources intact
            }
            memset(newFlags, 0, flagBytes);

            unordered_map_s** newChain = mem_alloc::allocate<unordered_map_s*>(newChainCap);
            if (!newChain) {
                delete[] newFlags;
                return; // Keep existing resources if allocation fails
            }
            memset(newChain, 0, newChainCap * sizeof(unordered_map_s*));

            if (chain_size >= 234) {
                rangeMap.set_fullness(1.0);
            }

            chain = newChain;
            flags = newFlags;
            cap_ = static_cast<uint8_t>(newChainCap);

            // Copy old chain data if it exists
            if (oldChain != nullptr) {
                const uint8_t copyLimit = (oldCap < cap_) ? oldCap : cap_;
                for (uint8_t i = 0; i < copyLimit; i++) {
                    if (oldChain[i] != nullptr) {
                        chain[i] = oldChain[i]; // Keep existing maps
                        slotState s = getStateFrom(oldFlags, i);
                        if (s != slotState::Empty) {
                            setState(i, s); // Preserve states
                        }
                    }
                }
                // Free old resources
                mem_alloc::deallocate(oldChain);
                delete[] oldFlags;
            }
        }

    public:
        // default constructor
        ChainedUnorderedMap() : slot_handler() { 
            // Initialize with INIT_CAP maps
            remap(INIT_CAP);  // Use remap to initialize resources

            // First, make 3 maps available, 7 reserve type 1 maps
            for(uint8_t i=0; i<INIT_CAP; i++){
                if(i < 3) activate_map(i);  // Activate first 3 maps, but it empty 
            }
        }
        
        // Constructor with capacity
        explicit ChainedUnorderedMap(uint16_t chainCapicity) : slot_handler() { 
            uint8_t numMapRequired = static_cast<uint8_t>(chainCapicity / cmap_ability + 1);      
            uint8_t numReserve = 3;    // num reserve map 

            if(numMapRequired >= 3 && numMapRequired < MAX_CAP - 6) numReserve = 6; 
            uint8_t newChainCap = numMapRequired + numReserve;
            
            // Use remap to initialize resources with calculated capacity
            remap(newChainCap);
            
            // Activate required maps
            for(uint8_t i=0; i < numMapRequired; i++){
                activate_map(i);
            }
        }
                        
        ~ChainedUnorderedMap() {
            if (chain) {
                for (uint8_t i = 0; i < cap_; ++i)
                    delete chain[i];
                mem_alloc::deallocate(chain);
            }
            if (flags) {
                slots_release();  // frees the flags buffer
            }
        }
        // Copy Constructor
        ChainedUnorderedMap(const ChainedUnorderedMap& o) noexcept : slot_handler(o),
            rangeMap(o.rangeMap),
            fullness_(o.fullness_),
            cmap_ability(o.cmap_ability),
            chain_size(o.chain_size)
        {
            this->cap_ = o.cap_; // assign base member if needed
            chain = mem_alloc::allocate<unordered_map_s*>(this->cap_);
            std::memset(chain, 0, this->cap_ * sizeof(chain[0]));
            for (uint8_t i = 0; i < this->cap_; ++i) {
                if (o.chain[i])
                    chain[i] = new unordered_map_s(*o.chain[i]);
            }
        }

        // Move Constructor
        ChainedUnorderedMap(ChainedUnorderedMap&& o) noexcept : slot_handler(std::move(o)),   // steal cap_ & flags
            rangeMap(std::move(o.rangeMap)),    
            fullness_(std::exchange(o.fullness_,   92)),
            cmap_ability(std::exchange(o.cmap_ability, static_cast<uint8_t>(255*92/100))),
            chain_size(std::exchange(o.chain_size,  0)){
                chain = std::exchange(o.chain, nullptr);
        }
        // Copy‐assignment
        ChainedUnorderedMap& operator=(const ChainedUnorderedMap& o) noexcept {
            if (this != &o) {
                ChainedUnorderedMap tmp(o);
                swap(*this, tmp);
            }
            return *this;
        }

        // Move‐assignment
        ChainedUnorderedMap& operator=(ChainedUnorderedMap&& o) noexcept {
            if (this != &o) {
                swap(*this, o);
            }
            return *this;
        }

    // ---------------------------------------------------------------------------------------------------
    // -------------------------------------- iterator class ---------------------------------------------
    // ---------------------------------------------------------------------------------------------------
    // Iterator class for efficient traversal of ChainedUnorderedMap elements

    // combine iterator + const_iterator into one template
    template<bool IsConst>
    class basic_iterator {
    public:
        using MapType   = std::conditional_t<IsConst, const ChainedUnorderedMap<R, T>, ChainedUnorderedMap<R, T>>;
        using InnerIter = std::conditional_t<IsConst, typename unordered_map_s::const_iterator, typename unordered_map_s::iterator>;
        using value_type = Pair_RT; // This is pair<R, T>
        using reference  = std::conditional_t<IsConst, const Pair_RT&, Pair_RT&>;
        using pointer    = std::conditional_t<IsConst, const Pair_RT*, Pair_RT*>;
        using iterator_category = std::forward_iterator_tag;
        using difference_type   = std::ptrdiff_t;

    private:
        MapType* parent;
        uint8_t  mapID;
        InnerIter current;
        
        // Fast path to move to next valid element
        inline void advanceToValid() {
            while (mapID < parent->cap_) {
                if (parent->map_in_use(mapID)) { // map_in_use checks if chain[mapID] is valid and state is Used
                    current = parent->chain[mapID]->begin();
                    if (current != parent->chain[mapID]->end()) 
                        return; // Found a valid element
                }
                ++mapID;
            }
            mapID = MAX_CAP;  // Mark as end iterator
            current = InnerIter(); // Reset current for end iterator
        }

    public:
        basic_iterator() : parent(nullptr), mapID(MAX_CAP), current() {}

        // Constructor with position
        basic_iterator(MapType* p, uint8_t mid, InnerIter it)
        : parent(p), mapID(mid), current(it) 
        {
            if (mapID == MAX_CAP) { // Already an end iterator
                return;
            }

            if (mapID < parent->cap_) {
                if (parent->map_in_use(mapID)) {
                    // mapID is valid, and the map itself is active.
                    // Check if 'it' is at the end of this particular map.
                    if (current == parent->chain[mapID]->end()) {
                        // Yes, 'it' is at the end of chain[mapID].
                        // Advance to the next element in the ChainedUnorderedMap or to end().
                        ++(*this);
                    }
                    // else: current is a valid iterator within chain[mapID].
                } else {
                    // mapID is within cap_, but this map is not in use.
                    // Advance to find the next valid map or set to end().
                    advanceToValid();
                }
            } else {
                // mapID >= parent->cap_ but not MAX_CAP. This is an invalid state.
                // Normalize to an end iterator.
                this->mapID = MAX_CAP;
                this->current = InnerIter(); 
            }
        }

        // Dereference - return direct reference to the pair in the inner map
        reference operator*() const {
            // Since keys are stored directly, we can return the pair directly
            return *current;
        }
        
        // Arrow operator - return direct pointer to the pair in the inner map
        pointer operator->() const {
            // Since keys are stored directly, we can return the pointer directly
            return &(*current);
        }

        // Pre-increment
        basic_iterator& operator++() {
            if (is_end()) { // Already at the true end
                return *this;
            }
                
            // Assumes iterator is dereferenceable or one past the end of a sub-map.
            if (mapID < parent->cap_ && parent->map_in_use(mapID)) {
                ++current;
                if (current == parent->chain[mapID]->end()) {
                    ++mapID; // Move to next map index
                    advanceToValid(); // Find next valid element or set to end
                }
            } else {
                // Iterator was in an invalid state or pointed beyond allocated maps but not to MAX_CAP.
                advanceToValid();
            }
            return *this;
        }
            
        // Post-increment
        basic_iterator operator++(int) { 
            basic_iterator tmp = *this; 
            ++(*this); 
            return tmp; 
        }

        // Compare iterators
        bool operator==(const basic_iterator& o) const {
            // Fast path for end iterator comparison
            if (mapID == MAX_CAP && o.mapID == MAX_CAP) return true;
            
            // Compare normal iterators
            return parent == o.parent && mapID == o.mapID && current == o.current;
        }
        
        bool operator!=(const basic_iterator& o) const { 
            return !(*this == o); 
        }
        
        // Check if this is end iterator (helper function)
        bool is_end() const {
            return mapID == MAX_CAP;
        }

        // Expose mapID for erase operations
        friend class ChainedUnorderedMap;
    };

    // aliases
    using iterator       = basic_iterator<false>;
    using const_iterator = basic_iterator<true>;

    // begin/end methods remain the same
    iterator begin() {
        for (uint8_t i=0; i<cap_; ++i)
            if (map_in_use(i))
                return iterator(this, i, chain[i]->begin());
        return iterator();
    }
    iterator end() {
        return iterator();
    }
    const_iterator begin() const {
        for (uint8_t i=0; i<cap_; ++i)
            if (map_in_use(i))
                return const_iterator(this, i, chain[i]->begin());
        return const_iterator();
    }
    const_iterator end() const {
        return const_iterator();
    }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    // ---------------------------------------------------------------------------------------------
    // -------------------------------------- end of iterator class --------------------------------
    // ---------------------------------------------------------------------------------------------

        // Core insert implementation optimized for memory efficiency
        // Returns success status directly to avoid pair creation overhead
        // action-insert : Instructs which map the element should be inserted into with the given range.
        /* @ detail :
            - if range does not have a corresponding activated map, automatically assign it a reserve map in cap_ (prefer reserve type 2).
            - if both are exhausted -> return a map outside the range of cap_ -> remap (this is done by wrapper functions)
        */
    private:
        template<typename V>
        bool insert_core(const R& key, V&& value) {
            pair_kmi keyMap = keyMappingIN(key); // Get mapID and range from key
            int16_t mapID = keyMap.first; // Extract mapID from pair
            uint8_t range = keyMap.second; // Extract range from pair
            if(mapID >=0 ){
                return chain[mapID]->insert(key, std::forward<V>(value)).second;
            }
            // Second pass: find suitable empty or reserve map
            int16_t emptyMapID = -1;  // Track first empty map separately
            
            for (int16_t i = 0; i < cap_; i++) {
                slotState state = getState(i);
                if(state == slotState::Empty){
                    if(chain[i] != nullptr){
                        // Priority 1: Available but empty map. this is first element inserted to this map
                        if(chain[i]->empty()){
                            // assign range to this map
                            rangeMap[range] = i;

                            setState(i, slotState::Used); // marked as Used for the first time
                            return chain[i]->insert(key, std::forward<V>(value)).second;
                            chain_size++;
                        }
                    }else{
                        // Priority 3: Type 1 reserve map (track but continue looking)
                        if(emptyMapID == -1){
                            emptyMapID = i; // Track first empty map separately
                        }
                    }
                }
                if (state == slotState::Deleted) {
                    // Priority 2: Type 2 reserve map
                    // assign range to this map
                    rangeMap[range] = i;

                    setState(i, slotState::Used); // changed mark to Used
                    return chain[i]->insert(key, std::forward<V>(value)).second;
                }
            }
            if (emptyMapID != -1) {
                // Use the first empty map
                activate_map(emptyMapID);
                rangeMap[range] = emptyMapID; // assign range to this map

                setState(emptyMapID, slotState::Used); 
                return chain[emptyMapID]->insert(key, std::forward<V>(value)).second;
            } else if (cap_ < MAX_CAP) {    
                // Suggest capacity extension. remap and try again
                uint16_t newChainCap = cap_ + 4;
                if (newChainCap > MAX_CAP) newChainCap = MAX_CAP;
                remap(newChainCap);
                // Try to insert again after remapping
                return insert_core(key, std::forward<V>(value));
            } else{
                return false; // No suitable map found and no capacity to extend
            }
        }
        bool erase_core(R key) {
            pair_kmi keyMap = keyMappingIN(key); // Get mapID and range from key
            int16_t mapID = keyMap.first; // Extract mapID from pair
            uint8_t range = keyMap.second; // Extract range from pair
            if (mapID < 0) {
                return false; // Key not found
            }
            bool erased = chain[mapID]->erase(key); // Erase from the inner map
            if (erased) {
                // Check if the map is now empty
                if (chain[mapID]->empty()) {
                    rangeMap.erase(range); // Remove the range mapping
                    setState(mapID, slotState::Deleted); // Mark as Deleted
                    chain[mapID]->fit(); // minimize memory usage
                    chain_size--;
                }
            }
            return erased; // Return success status
        }
    public:
        // Memory-efficient insert methods that avoid creating iterators when not needed
        bool insert(R key, const T& value) {
            return insert_core(key, value);
        }

        bool insert(R key, T&& value) {
            return insert_core(key, std::move(value));
        }

        bool insert(const Pair_RT& p) {
            return insert_core(p.first, p.second);
        }

        bool insert(Pair_RT&& p) {
            return insert_core(p.first, std::move(p.second));
        }


        // Public erase method
        bool erase(R key) {
            return erase_core(key);
        }
        bool erase(iterator pos) {
            if (pos == end()) return false;
            uint8_t mapID = pos.mapID;
            if (chain[mapID] && getState(mapID) == slotState::Used) {
                // Get the key directly from the iterator
                R key = pos->first;  // Direct access since no mapping needed
                size_t erased = chain[mapID]->erase(key);
                if (erased) {
                    // If the inner map is now empty, clean up
                    if (chain[mapID]->empty()) {
                        // Find and remove the range mapping
                        pair_kmi keyMap = keyMappingIN(key);
                        uint8_t range = keyMap.second;
                        rangeMap.erase(range);
                        setState(mapID, slotState::Deleted);
                        chain[mapID]->fit();
                        chain_size--;
                    }
                    return true;
                }
            }
            return false;
        }

        // Erase by range - useful for clearing specific portions
        size_t erase(iterator first, iterator last) {
            size_t count = 0;
            while (first != last) {
                iterator cur = first++;
                if (erase(cur)) ++count;
            }
            return count;
        }

        // Finds an element with the specified key
        // Returns iterator to element if found, end() otherwise
        iterator find(R key) {
            pair_kmi keyMap = keyMappingIN(key); // Get mapID and range from key
            int16_t mapID = keyMap.first; // Extract mapID from pair
            uint8_t range = keyMap.second; // Extract range from pair
            if (mapID < 0) {
                return end(); // Key not found
            }
            typename unordered_map_s::iterator innerIt = chain[mapID]->find(key); // Find in the inner map
            
            if (innerIt != chain[mapID]->end()) {
                return iterator(this, mapID, innerIt); // Return iterator to found element
            }
            return end(); // Key not found
        }

        // Non-const version
        T& at(const R& key) {
            pair_kmi keyMap = keyMappingIN(key);
            int16_t mapID = keyMap.first;
            if (mapID < 0) {
                // Key not found - no map contains this range
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            // Check if map is actually in use
            if (!map_in_use(mapID)) {
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            // Find the key directly in the map (no inner key conversion needed)
            auto it = chain[mapID]->find(key);
            if (it == chain[mapID]->end()) {
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            return it->second;
        }

        // Const version
        const T& at(const R& key) const {
            pair_kmi keyMap = keyMappingIN(key);
            int16_t mapID = keyMap.first;
            if (mapID < 0) {
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            if (!map_in_use(mapID)) {
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            auto it = chain[mapID]->find(key);
            if (it == chain[mapID]->end()) {
                throw std::out_of_range("Key not found in ChainedUnorderedMap");
            }
            
            return it->second;
        }
        
        // Operator overload for accessing elements
        // Returns a reference to the value associated with the key
        // Throws std::out_of_range if the key is not found
        T& operator[](R key) {
            pair_kmi keyMap = keyMappingIN(key); // Get mapID and range from key
            int16_t mapID = keyMap.first; // Extract mapID from pair
            uint8_t range = keyMap.second; // Extract range from pair
            if (mapID < 0) {
                // value‐insert a default‐constructed T
                if (!insert(key, T{})) {
                    throw std::bad_alloc();
                }
                mapID = rangeMap.getValue(range); // Recalculate mapID after insertion
            }
            return (*chain[mapID])[key];
        }

        // actually set new fullness for each inner map in the chain and re-insert all elements
        // total number of elements remains the same 
        /*
        Note : - Reducing fullness will result in reducing the maximum range of key.
                - e.g: if fullness = 0.5, the maximum key is 32767 (32767 = 0.5 * 65535)
                - with MCU, it recommends to set_fullness before inserting elements
        */
        // return : pair<bool, uint16_t> - first : success or not | second : maximum key after fullness change
        pair<bool, uint16_t> set_fullness(float fullness) {
            // Ensure fullness is within the valid range [0.1, 1.0] or [10, 100] (% format)
            if(fullness < 0.1f) fullness = 0.1f;
            if(fullness > 1.0f && fullness < 10) fullness = 1.0f;
            if(fullness > 100) fullness = 100;

            uint8_t newFullness = 0;
            uint16_t old_max_key = static_cast<uint16_t>(fullness * 65535);

            // Convert fullness to an integer percentage
            if(fullness <= 1.0f) {
                newFullness = static_cast<uint8_t>(fullness * 100);
            }else{
                newFullness = static_cast<uint8_t>(fullness);
            }
            if (newFullness == fullness_) return pair<bool,uint16_t>(true, old_max_key); // No change in fullness
            if (newFullness < fullness_){
                // check if new map_ability is less than current size()
                uint16_t newCmapAbility = static_cast<uint16_t>(newFullness) * MAX_CAP / 100;
                if (newCmapAbility * MAX_CAP < size()) {
                    return pair<bool,uint16_t>(false,old_max_key); // Not enough capacity to shrink
                }
            }
            // store old parameters
            uint8_t old_cap = cap_;
            uint8_t old_fullness_ = fullness_;

            // Need to rehash - collect all key-value pairs
            struct KeyValue { uint16_t key; T value; };
            
            // Count total elements across all maps to minimize allocations
            uint16_t totalElements = size();
            KeyValue* allElements = new (std::nothrow) KeyValue[totalElements];
            if (allElements == nullptr) {
                return pair<bool,uint16_t>(false, old_max_key); // Memory allocation failed
            }
            
            // Extract all elements into the buffer
            uint16_t elemIdx = 0;
            for (uint8_t i = 0; i < cap_; i++) {
                if (map_in_use(i)) {
                    auto& map = *(chain[i]);
                    for (auto it = map.begin(); it != map.end(); ++it) {
                        // Keys are stored directly, no mapping conversion needed
                        allElements[elemIdx].key = it->first;
                        allElements[elemIdx].value = std::move(it->second);
                        elemIdx++;
                    }
                    // Clear the map for new insertions
                    map.clear();
                }
            }

            // Update fullness and recalculate capacity ability
            fullness_ = newFullness;
            recalculate_cmap_ability();
            
            // clean up and return the chain to a completely empty state
            for (uint8_t i = 0; i < cap_; i++) {
                if (chain[i] != nullptr) {
                    delete chain[i];
                    chain[i] = nullptr;
                }
                setState(i, slotState::Empty);
            }
            chain_size = 0; // Reset chain size
            rangeMap.clear(); // Clear rangeMap
            // activate maps based on new fullness
            uint8_t requiredMaps = static_cast<uint8_t>((totalElements + cmap_ability - 1) / cmap_ability);
            for (uint8_t i = 0; i < requiredMaps; ++i) {
                activate_map(i);
            }
            // Reinsert all elements into the chain
            for (uint16_t i = 0; i < totalElements; ++i) {
                uint16_t key = allElements[i].key;
                T value = std::move(allElements[i].value);
                if(!insert(key, std::move(value))){
                    // restore the entire chain by deleting the current chain (the one that is inserting elements)
                    // and creating a completely new chain with the old fullness_
                    // clean up the chain
                    for (uint8_t j = 0; j < cap_; j++) {
                        if (chain[j] != nullptr) {
                            delete chain[j];
                            chain[j] = nullptr;
                        }
                        setState(j, slotState::Empty);
                    }
                    chain_size = 0; // Reset chain size
                    fullness_ = old_fullness_; // Restore old fullness
                    recalculate_cmap_ability();
                    remap(old_cap); // Restore old capacity
                    rangeMap.clear(); // Clear rangeMap
                    // activate maps based on old fullness
                    uint8_t oldRequiredMaps = static_cast<uint8_t>((totalElements + cmap_ability - 1) / cmap_ability);  
                    for (uint8_t j = 0; j < oldRequiredMaps; ++j) {
                        activate_map(j);
                    }
                    // Reinsert all elements into the chain
                    for (uint16_t k = 0; k < totalElements; ++k) {
                        uint16_t key = allElements[k].key;
                        T value = std::move(allElements[k].value);
                        insert(key, std::move(value));
                    }
                    // Free temporary buffer
                    delete[] allElements;
                    return pair<bool,uint16_t>(false, old_max_key); // Insertion failed
                }
            }
            // Free temporary buffer
            delete[] allElements;
            uint16_t new_max_key = static_cast<uint16_t>(fullness_ * 65535);
            return pair<bool,uint16_t>(true,new_max_key); // Fullness set successfully
        }

        float get_fullness() const noexcept {
            return static_cast<float>(fullness_) / 100.0f;
        }
        /*
        Note: For chains, the reserve() function will not actually prepare the actual number of elements required,
            since it cannot accurately estimate the number of maps that need to be prepared. 
            ( It is impossible to predict how many elements each map will contain.)
        */
        bool reserve(uint16_t newCap) {
            if (newCap < size() || newCap > map_ability()) return false;
        
            uint8_t requiredMaps = static_cast<uint8_t>((newCap + cmap_ability - 1) / cmap_ability);
            uint8_t reserveMaps  = (requiredMaps < 3) ? 3 : 6;
            uint16_t totalMaps   = requiredMaps + reserveMaps;
            if (totalMaps > MAX_CAP) totalMaps = MAX_CAP;
        
            // DON'T set cap_ here! Let remap do it safely.
            remap(static_cast<uint8_t>(totalMaps));
        
            // Now cap_, flags, chain[] are all correct.
            for (uint8_t i = 0; i < requiredMaps; ++i)
                activate_map(i);
        
            return true;
        }

        // check if map is full 
        bool is_full() {
            for (uint8_t i = 0; i < cap_; i++) {
                if(chain[i] != nullptr){
                    if (!chain[i]->is_full()) return false;
                }
            }
            return true;
        }

        // maximum numver of elements the chain can contain now 
        [[nodiscard]] uint16_t capacity() const noexcept{                
            return cap_ * cmap_ability;
        }

        // maximum theoretical capacity of the chain
        uint16_t map_ability() const noexcept {
            return cmap_ability * MAX_CAP;
        }

        // Compare two chains by size and element values
        bool operator==(const ChainedUnorderedMap& other) const noexcept {
            if (size() != other.size()) return false;
            for (auto it = begin(); it != end(); ++it) {
                auto oth = other.find(it->first());
                if (oth == other.end() || it->second() != oth->second())
                    return false;
            }
            return true;
        }
        size_t memory_usage() const noexcept {
            size_t total = 0;
            // count sub-maps
            for (uint8_t i = 0; i < cap_; ++i){
            if(map_in_use(i)){
                total += chain[i]->memory_usage();
            }else{
                if(chain[i]){
                    total+=14;
                }else{
                    total+=4;
                }
            }
            }

            total += (cap_*2 + 7)/8;    // flags
            total += sizeof(*this) + rangeMap.memory_usage(); // this + rangeMap 
            return total;
        }

        bool operator!=(const ChainedUnorderedMap& other) const noexcept {
            return !(*this == other);
        }
        // Optimizes memory usage by removing type 2 reserve maps and calling fit() on remaining maps
        // Keep the Used maps and drag them next to each other
        // Returns number of bytes freed (approximate)
        size_t fit() {
            if (chain == nullptr) return 0;
        
            size_t bytesFreed = 0;
            uint8_t activeMaps = 0;
        
            // First pass: count active maps and free type 2 reserve maps
            for (uint8_t i = 0; i < cap_; i++) {
                if (chain[i] != nullptr) {
                    if (getState(i) == slotState::Used) {
                        // Active map: call its fit() method to minimize memory usage
                        bytesFreed += chain[i]->fit();
                        activeMaps++;
                    } else if (getState(i) == slotState::Deleted) {
                        // Type 2 reserve map: delete it to free memory
                        delete chain[i];
                        chain[i] = nullptr;
                        setState(i, slotState::Empty);
                        bytesFreed += sizeof(unordered_map_s) + 32; // Approximate size
                    }
                }
            }
        
            // If no active maps or only one, no need for compaction
            if (activeMaps <= 1) return bytesFreed;
        
            // Second pass: compact the chain if there are gaps between maps
            // Need to handle rangeMap differently since it's now an unordered_map
            uint8_t destIdx = 0;
            for (uint8_t srcIdx = 0; srcIdx < cap_; srcIdx++) {
                if (chain[srcIdx] != nullptr && getState(srcIdx) == slotState::Used) {
                    if (destIdx != srcIdx) {
                        // Move map from srcIdx to destIdx
                        chain[destIdx] = chain[srcIdx];
                        
                        // Find the range associated with srcIdx and update it to destIdx
                        for (auto it = rangeMap.begin(); it != rangeMap.end(); ++it) {
                            if (it->second == srcIdx) {
                                // Update the range to point to the new index
                                uint8_t range = it->first;
                                rangeMap[range] = destIdx;
                                break;
                            }
                        }
                        
                        setState(destIdx, slotState::Used);
        
                        // Clear the source position
                        chain[srcIdx] = nullptr;
                        setState(srcIdx, slotState::Empty);
                    }
                    destIdx++;
                }
            }

            // Optional: reduce array capacity if utilization is very low
            if (activeMaps < cap_ / 3 && cap_ > INIT_CAP) {
                uint16_t newCap = std::max(static_cast<uint16_t>(INIT_CAP), static_cast<uint16_t>(activeMaps * 2));
        
                // Create new smaller chain
                auto* newChain = mem_alloc::allocate<unordered_map_s*>(newCap);
                memset(newChain, 0, newCap * sizeof(unordered_map_s*));
        
                // Create new flags array
                uint8_t* newFlags = new uint8_t[(newCap * 2 + 7) / 8];
                memset(newFlags, 0, (newCap * 2 + 7) / 8);
        
                // Copy active maps
                for (uint8_t i = 0; i < activeMaps; i++) {
                    newChain[i] = chain[i];
                    setState(i, slotState::Used, newFlags);
                }
        
                // Free old arrays
                mem_alloc::deallocate(chain);
                delete[] flags;
        
                // Update pointers
                chain = newChain;
                flags = newFlags;
        
                // Update capacity
                uint16_t oldCap = cap_;
                cap_ = newCap;
        
                // No need to resize rangeMap since it's now an unordered_map
                // rangeMap's size automatically adjusts as entries are added/removed
        
                // Bytes freed calculation
                bytesFreed += (oldCap - newCap) * sizeof(unordered_map_s*);
                bytesFreed += ((oldCap * 2 + 7) / 8) - ((newCap * 2 + 7) / 8);
            }
        
            return bytesFreed;
        }

        // current number of elements in chain
        uint16_t size() const noexcept {                        
            uint16_t total = 0;
            for(uint8_t i = 0; i < cap_; i++) {
                if(chain[i] != nullptr) total += chain[i]->size();                     
            }
            return total;
        }
        // Clear all data and free per‐map memory
        void clear() noexcept {
            // 1) delete all sub‐maps
            for (uint8_t i = 0; i < cap_; i++) {
                delete chain[i];
                chain[i] = nullptr;
            }

            // 2) reset flags to all‐Empty
            slots_init(cap_);            // zeroes & marks every slot Empty

            // 3) reset rangeMap to default
            rangeMap.clear();   
            rangeMap.fit();
            chain_size = 0;
        }
        bool empty() const {
            for (uint8_t i = 0; i < cap_; i++) {
                if(map_in_use(i)) {
                    return false; // At least one map is in use
                }
            }
            return true;
        }
        // swap helper for copy-and-swap
        friend void swap(ChainedUnorderedMap& a, ChainedUnorderedMap& b) noexcept {
            using std::swap;
            swap(a.fullness_,   b.fullness_);
            swap(a.cmap_ability, b.cmap_ability);
            swap(a.cap_,        b.cap_);
            swap(a.flags,       b.flags);        // from slot_handler
            swap(a.chain,       b.chain);
            swap(a.chain_size,  b.chain_size);
            swap(a.rangeMap,    b.rangeMap);
        }
    };



    // -----------------------------------------------------------------------------------------
    // ---------------------------------- ChainedUnorderedSet class -----------------------------------
    // -----------------------------------------------------------------------------------------
    template <typename T>
    class ChainedUnorderedSet : public slot_handler, hash_kernel{
    private:
        using unordered_set_s = unordered_set<T>;
        using pair_kmi = pair<int16_t, uint8_t>; // setID , range 

        unordered_set_s** chain = nullptr;
        unordered_map<uint8_t, uint8_t> rangeMap; // setID -> range

        uint8_t chain_size = 0; // number of sets in the chain
        uint8_t fullness_ = 92; // maximum map fill level (in %)
        uint8_t cset_ability = 234; // Maximum capacity of each set (234 = 92% of 255)

        // maximum capacity of each member set in the chain
        inline void recalculate_cset_ability() noexcept {
            cset_ability = static_cast<uint8_t>(255 * fullness_ / 100);
        }

        // activate a new map
        // @ return : true - setID is activated
        void activate_set(uint8_t setID) noexcept{
            if (setID >= cap_) {
                return;
            }
            if(chain[setID] != nullptr) {
                return;   // setID already activated
            }else{
                chain[setID] = new unordered_set_s();
                chain_size++;
                chain[setID]->set_fullness(fullness_); // set fullness of the map
                return;
            }
        }

        inline pair_kmi keyMappingIN(const T& key) noexcept {
            size_t tranform_key = preprocess_hash_input(key);
            uint8_t range;
            if constexpr (std::is_integral<T>::value){
                range = tranform_key / cset_ability;
            }else if constexpr (std::is_floating_point<T>::value){
                range = tranform_key % cset_ability;
            }
            int16_t setID = rangeMap.getValue(range); // Get setID from range
            return pair_kmi(setID, range); // Return pair of setID and innerKey
        }

        // in use (only available maps) , contains at least 1 element
        inline bool set_in_use(uint8_t setID) const noexcept {
            if(getState(setID) == slotState::Used) {
                return true;
            }
            return false;
        }

        // return number of maps in chain (available + reserve)
        uint16_t chainCap() const noexcept{
            return cap_;
        }


        void remap(uint16_t newChainCap) noexcept {
            // Ensure the new capacity can accommodate existing maps
            if (newChainCap < chain_size) newChainCap = chain_size;
            if (newChainCap > MAX_CAP) newChainCap = MAX_CAP;

            // Store old resources if they exist
            unordered_set_s** oldChain = chain;
            uint8_t* oldFlags = flags;
            uint8_t oldCap = cap_;

            const size_t flagBytes = (newChainCap * 2 + 7) / 8;
            uint8_t* newFlags = nullptr;
            try {
                newFlags = new uint8_t[flagBytes];
            } catch (...) {
                newFlags = nullptr;
            }
            if (!newFlags) {
                return;
            }
            memset(newFlags, 0, flagBytes);

            unordered_set_s** newChain = mem_alloc::allocate<unordered_set_s*>(newChainCap);
            if (!newChain) {
                delete[] newFlags;
                return;
            }
            memset(newChain, 0, newChainCap * sizeof(unordered_set_s*));

            if (chain_size >= 234) {
                rangeMap.set_fullness(1.0);
            }

            chain = newChain;
            flags = newFlags;
            cap_ = static_cast<uint8_t>(newChainCap);

            // Copy old chain data if it exists
            if (oldChain != nullptr) {
                const uint8_t copyLimit = (oldCap < cap_) ? oldCap : cap_;
                for (uint8_t i = 0; i < copyLimit; i++) {
                    if (oldChain[i] != nullptr) {
                        chain[i] = oldChain[i]; // Keep existing maps
                        slotState s = getStateFrom(oldFlags, i);
                        if (s != slotState::Empty) {
                            setState(i, s); // Preserve states
                        }
                    }
                }
                // Free old resources
                mem_alloc::deallocate(oldChain);
                delete[] oldFlags;
            }
        }

    public:
        // default constructor
        ChainedUnorderedSet() : slot_handler() { 
            // Initialize with INIT_CAP maps
            remap(INIT_CAP);  // Use remap to initialize resources

            // First, make 3 maps available, 7 reserve type 1 maps
            for(uint8_t i=0; i<INIT_CAP; i++){
                if(i < 3) activate_set(i);  // Activate first 3 maps, but it empty 
            }
            // activate_set(0); // Activate first set 
        }
        
        // Constructor with capacity
        explicit ChainedUnorderedSet(uint16_t chainCapicity) : slot_handler() { 
            uint8_t numSetRequired = static_cast<uint8_t>(chainCapicity / cset_ability + 1);      
            uint8_t numReserve = 3;    // num reserve map 

            if(numSetRequired >= 3 && numSetRequired < MAX_CAP - 6) numReserve = 6; 
            uint8_t newChainCap = numSetRequired + numReserve;
            
            // Use remap to initialize resources with calculated capacity
            remap(newChainCap);
            
            // Activate required maps
            for(uint8_t i=0; i < numSetRequired; i++){
                activate_set(i);
            }
        }
                        
        ~ChainedUnorderedSet() {
            if (chain) {
                for (uint8_t i = 0; i < cap_; ++i)
                    delete chain[i];
                mem_alloc::deallocate(chain);
            }
            if (flags) {
                slots_release();  // frees the flags buffer
            }
        }
        // Copy Constructor
        ChainedUnorderedSet(const ChainedUnorderedSet& o) noexcept : slot_handler(o),
            fullness_(o.fullness_),
            cset_ability(o.cset_ability),
            chain_size(o.chain_size),
            rangeMap(o.rangeMap)
        {
            this->cap_ = o.cap_; // assign base member if needed
            chain = mem_alloc::allocate<unordered_set_s*>(this->cap_);
            std::memset(chain, 0, this->cap_ * sizeof(chain[0]));
            for (uint8_t i = 0; i < this->cap_; ++i) {
                if (o.chain[i])
                    chain[i] = new unordered_set_s(*o.chain[i]);
            }
        }

        // Move Constructor
        ChainedUnorderedSet(ChainedUnorderedSet&& o) noexcept : slot_handler(std::move(o)),   // steal cap_ & flags
            fullness_(std::exchange(o.fullness_,   92)),
            cset_ability(std::exchange(o.cset_ability, static_cast<uint8_t>(255*92/100))),
            chain_size(std::exchange(o.chain_size,  0)),
            rangeMap(std::move(o.rangeMap)){
                chain = std::exchange(o.chain, nullptr);
        }
        // Copy‐assignment
        ChainedUnorderedSet& operator=(const ChainedUnorderedSet& o) noexcept {
            if (this != &o) {
                ChainedUnorderedSet tmp(o);
                swap(*this, tmp);
            }
            return *this;
        }

        // Move‐assignment
        ChainedUnorderedSet& operator=(ChainedUnorderedSet&& o) noexcept {
            if (this != &o) {
                swap(*this, o);
            }
            return *this;
        }

    // ---------------------------------------------------------------------------------------------------
    // -------------------------------------- iterator class ---------------------------------------------
    // ---------------------------------------------------------------------------------------------------
    // Iterator class for efficient traversal of ChainedUnorderedSet elements

    template<bool IsConst>
    class base_iterator {
    public:
        using setType = typename std::conditional<IsConst, const ChainedUnorderedSet, ChainedUnorderedSet>::type;
        using InnerIter = typename std::conditional<IsConst, typename unordered_set_s::const_iterator, typename unordered_set_s::iterator>::type;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;  // Changed from uint16_t to the inner set's value type
        using reference = value_type;
        using pointer = typename std::conditional<IsConst, const value_type*, value_type*>::type;

    private:
        setType* parent;
        uint8_t setID;
        InnerIter current;

        inline void advanceToValid() {
            while (setID < parent->cap_) {
                if (parent->set_in_use(setID)) {
                    current = parent->chain[setID]->begin();
                    if (current != parent->chain[setID]->end()) {
                        return;
                    }
                }
                setID++;
            }
            setID = MAX_CAP; // Set to end state
        }
    public:
        base_iterator() : parent(nullptr), setID(MAX_CAP) {} // Default constructor for end iterator

        // Constructor with position
        base_iterator(setType* p, uint8_t sid, InnerIter it)
            : parent(p), setID(sid), current(it) 
        {
            // Immediately advance if current position is invalid
            if (setID < parent->cap_ && current == parent->chain[setID]->end())
                ++(*this);
        }

        // Dereference - return inner value directly without transformation
        reference operator*() const {
            return *current;
        }

        pointer operator->() const {
            return &(*current);
        }
        
        // Pre-increment - optimized for single pass
        base_iterator& operator++() {
            if (setID >= parent->cap_) 
                return *this; // Already at end
                
            ++current;
            if (current == parent->chain[setID]->end()) {
                ++setID;
                advanceToValid();
            }
            return *this;
        }
        
        // Post-increment
        base_iterator operator++(int) { 
            base_iterator tmp = *this; 
            ++(*this); 
            return tmp; 
        }

        // Compare iterators
        bool operator==(const base_iterator& o) const {
            // Fast path for end iterator comparison
            if (setID == MAX_CAP && o.setID == MAX_CAP) return true;
            
            // Compare normal iterators
            return parent == o.parent && setID == o.setID && current == o.current;
        }
        
        bool operator!=(const base_iterator& o) const { 
            return !(*this == o); 
        }
        
        // Check if this is end iterator (helper function)
        bool is_end() const {
            return setID == MAX_CAP;
        }
    };

    // Iterator type aliases
    using iterator = base_iterator<false>;
    using const_iterator = base_iterator<true>;

    // Iterator access methods
    iterator begin() {
        for (uint8_t i=0; i<cap_; ++i)
            if (set_in_use(i))
                return iterator(this, i, chain[i]->begin());
        return iterator();
    }

    iterator end() {
        return iterator();
    }

    const_iterator begin() const {
        for (uint8_t i=0; i<cap_; ++i)
            if (set_in_use(i))
                return const_iterator(this, i, chain[i]->begin());
        return const_iterator();
    }

    const_iterator end() const {
        return const_iterator();
    }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    // ---------------------------------------------------------------------------------------------
    // -------------------------------------- end of iterator class --------------------------------
    // ---------------------------------------------------------------------------------------------

        // Core insert implementation optimized for memory efficiency
        // Returns success status directly to avoid pair creation overhead
        // action-insert : Instructs which map the element should be inserted into with the given range.
        /* @ detail :
            - if range does not have a corresponding activated map, automatically assign it a reserve map in cap_ (prefer reserve type 2).
            - if both are exhausted -> return a map outside the range of cap_ -> remap (this is done by wrapper functions)
        */
    public:
        /**
         * @brief Inserts a key into the ChainedUnorderedSet.
         * @param key The key to insert.
         * @return true if the key was successfully inserted, false otherwise.
         * @note This function handles the insertion logic, including checking for existing keys,
         *       managing the internal mapping of keys to sets, and resizing if necessary.
         */
        bool insert(T key) {
            pair_kmi keyPair = keyMappingIN(key); // Get setID and innerKey
            int16_t setID = keyPair.first; // Get setID from pair
            uint8_t range = keyPair.second; // Get range from pair
            if(setID >=0 ){
                return chain[setID]->insert(key);
            }
            // Second pass: find suitable empty or reserve map
            int16_t emptyMapID = -1;  // Track first empty map separately
            
            for (int16_t i = 0; i < cap_; i++) {
                slotState state = getState(i);
                if(state == slotState::Empty){
                    if(chain[i] != nullptr){
                        // Priority 1: Available but empty map. this is first element inserted to this map
                        if(chain[i]->empty()){
                            // assign range to this map
                            rangeMap[range] = i;
                            setState(i, slotState::Used); // marked as Used for the first time
                            return chain[i]->insert(key);
                            chain_size++;
                        }
                    }else{
                        // Priority 3: Type 1 reserve map (track but continue looking)
                        if(emptyMapID == -1){
                            emptyMapID = i; // Track first empty map separately
                        }
                    }
                }
                if (state == slotState::Deleted) {
                    // Priority 2: Type 2 reserve map
                    // assign range to this map
                    rangeMap[range] = i;
                    setState(i, slotState::Used); // changed mark to Used
                    return chain[i]->insert(key);
                }
            }
            if (emptyMapID != -1) {
                // Use the first empty map
                activate_set(emptyMapID);
                rangeMap[range] = emptyMapID; // assign range to this map
                setState(emptyMapID, slotState::Used); 
                return chain[emptyMapID]->insert(key);
            } else if (cap_ < MAX_CAP) {    
                // Suggest capacity extension. remap and try again
                uint16_t newChainCap = cap_ + 4;
                if (newChainCap > MAX_CAP) newChainCap = MAX_CAP;
                remap(newChainCap);
                // Try to insert again after remapping
                return insert(key);
            } else{
                return false; // No suitable map found and no capacity to extend
            }
        }

        /**
         * @brief Removes a key from the ChainedUnorderedSet.
         * @param key The key to remove.
         * @return true if the key was successfully removed, false if not found.
         * @note If removing the key results in an empty set, the set will be marked as deleted
         *       and the range mapping will be updated accordingly.
         */
        bool erase(const T& key) {
            pair_kmi keyPair = keyMappingIN(key); // Get setID and innerKey
            int16_t setID = keyPair.first; // Get setID from pair
            uint8_t range = keyPair.second; // Get range from pair
            if (setID < 0) {
                return false; // Key not found
            }
            bool erased = chain[setID]->erase(key); // Erase from the inner map
            if (erased) {
                // Check if the map is now empty
                if (chain[setID]->empty()) {
                    rangeMap.erase(range); // Remove the range mapping
                    setState(setID, slotState::Deleted); // Mark as Deleted
                    chain[setID]->fit(); // minimize memory usage
                    chain_size--;
                }
            }
            return erased; // Return success status
        }

        /**
         * @brief Finds a key in the ChainedUnorderedSet.
         * @param key The key to find.
         * @return Iterator pointing to the found element, or end() if not found.
         * @note Uses the keyMappingIN function to determine which inner set to search.
         */
        iterator find(const T& key) {
            pair_kmi keyPair = keyMappingIN(key); 
            int16_t setID = keyPair.first; // Get setID from pair

            if (setID < 0) {
                return end(); // Key not found
            }
            typename unordered_set_s::iterator innerIt = chain[setID]->find(key); // Find in the inner map
            
            if (innerIt != chain[setID]->end()) {
                return iterator(this, setID, innerIt); // Return iterator to found element
            }
            return end(); // Key not found
        }

        /**
         * @brief Sets the fullness factor for all inner sets.
         * @param fullness The new fullness factor (0.1-1.0 or 10-100).
         * @return A pair containing success status and maximum key value after change.
         * @note Reducing fullness may reduce the maximum range of keys that can be stored.
         *       This operation rebuilds the entire set structure and should be done before
         *       inserting elements for best performance.
         */
        pair<bool, uint16_t> set_fullness(float fullness) {
            // Ensure fullness is within the valid range [0.1, 1.0] or [10, 100] (% format)
            if(fullness < 0.1f) fullness = 0.1f;
            if(fullness > 1.0f && fullness < 10) fullness = 1.0f;
            if(fullness > 100) fullness = 100;

            uint8_t newFullness = 0;
            uint16_t old_max_key = static_cast<uint16_t>(fullness * 65535);

            // Convert fullness to an integer percentage
            if(fullness <= 1.0f) {
                newFullness = static_cast<uint8_t>(fullness * 100);
            }else{
                newFullness = static_cast<uint8_t>(fullness);
            }
            
            if (newFullness == fullness_) return pair<bool,uint16_t>(true, old_max_key); // No change needed
            
            // Check if new capacity is enough for current elements
            if (newFullness < fullness_){
                uint16_t newCsetAbility = static_cast<uint16_t>(newFullness) * MAX_CAP / 100;
                if (newCsetAbility * MAX_CAP < size()) {
                    return pair<bool,uint16_t>(false, old_max_key); // Not enough capacity
                }
            }
            
            // Store old parameters
            uint8_t old_cap = cap_;
            uint8_t old_fullness_ = fullness_;

            // Need to rehash - collect all elements
            uint16_t totalElements = size();
            uint16_t* allElements = new (std::nothrow) uint16_t[totalElements];
            if (allElements == nullptr) {
                return pair<bool,uint16_t>(false, old_max_key); // Memory allocation failed
            }
            
            // Extract all elements into the buffer
            uint16_t elemIdx = 0;
            for (uint8_t i = 0; i < cap_; i++) {
                if (set_in_use(i)) {
                    auto& set = *(chain[i]);
                    for (auto it = set.begin(); it != set.end(); ++it) {
                        allElements[elemIdx++] = *it;
                    }
                    set.clear();
                }
            }

            // Update fullness and recalculate capacity ability
            fullness_ = newFullness;
            recalculate_cset_ability();
            
            // Reset chain state
            for (uint8_t i = 0; i < cap_; i++) {
                if (chain[i] != nullptr) {
                    delete chain[i];
                    chain[i] = nullptr;
                }
                setState(i, slotState::Empty);
            }
            chain_size = 0;
            rangeMap.clear();
            
            // Activate required sets
            uint8_t requiredSets = static_cast<uint8_t>((totalElements + cset_ability - 1) / cset_ability);
            for (uint8_t i = 0; i < requiredSets; ++i) {
                activate_set(i);
            }
            
            // Reinsert all elements
            bool success = true;
            for (uint16_t i = 0; i < totalElements; ++i) {
                if (!insert(allElements[i])) {
                    success = false;
                    break;
                }
            }
            
            if (!success) {
                // Restore original state if insertion failed
                for (uint8_t j = 0; j < cap_; j++) {
                    if (chain[j] != nullptr) {
                        delete chain[j];
                        chain[j] = nullptr;
                    }
                    setState(j, slotState::Empty);
                }
                
                chain_size = 0;
                fullness_ = old_fullness_;
                recalculate_cset_ability();
                remap(old_cap);
                rangeMap.clear();

                
                uint8_t oldRequiredSets = static_cast<uint8_t>((totalElements + cset_ability - 1) / cset_ability);
                for (uint8_t j = 0; j < oldRequiredSets; ++j) {
                    activate_set(j);
                }
                
                for (uint16_t k = 0; k < totalElements; ++k) {
                    insert(allElements[k]);
                }
                
                delete[] allElements;
                return pair<bool,uint16_t>(false, old_max_key);
            }
            
            delete[] allElements;
            uint16_t new_max_key = static_cast<uint16_t>(fullness_ * 65535 / 100);
            return pair<bool,uint16_t>(true, new_max_key);
        }

        /**
         * @brief Gets the current fullness factor.
         * @return The current fullness factor as a float between 0.0 and 1.0.
         */
        float get_fullness() const noexcept {
            return static_cast<float>(fullness_) / 100.0f;
        }

        /**
         * @brief Reserves space for at least the specified number of elements.
         * @param newCap The number of elements to reserve space for.
         * @return true if reservation was successful, false otherwise.
         * @note Due to the chained structure, this is only an approximation as it
         *       cannot predict how elements will distribute across inner sets.
         */
        bool reserve(uint16_t newCap) {
            if (newCap < size() || newCap > set_ability()) return false;
        
            uint8_t requiredMaps = static_cast<uint8_t>((newCap + cset_ability - 1) / cset_ability);
            uint8_t reserveMaps  = (requiredMaps < 3) ? 3 : 6;
            uint16_t totalMaps   = requiredMaps + reserveMaps;
            if (totalMaps > MAX_CAP) totalMaps = MAX_CAP;
        
            // DON'T set cap_ here! Let remap do it safely.
            remap(static_cast<uint8_t>(totalMaps));
        
            // Now cap_, flags, chain[] are all correct.
            for (uint8_t i = 0; i < requiredMaps; ++i)
                activate_set(i);
        
            return true;
        }

        /**
         * @brief Checks if the set is full.
         * @return true if all inner sets are full, false otherwise.
         */
        bool is_full() {
            for (uint8_t i = 0; i < cap_; i++) {
                if(chain[i] != nullptr){
                    if (!chain[i]->is_full()) return false;
                }
            }
            return true;
        }

        /**
         * @brief Gets the current capacity of the set.
         * @return The maximum number of elements that can be stored without resizing.
         */
        [[nodiscard]] uint16_t capacity() const noexcept{                
            return cap_ * cset_ability;
        }

        /**
         * @brief Gets the maximum theoretical capacity of the set.
         * @return The maximum number of elements the set can hold with current fullness.
         */
        uint16_t set_ability() const noexcept {
            return cset_ability * MAX_CAP;
        }

        /**
         * @brief Compares this set with another for equality.
         * @param other The set to compare with.
         * @return true if both sets have the same elements, false otherwise.
         */
        bool operator==(const ChainedUnorderedSet& other) const noexcept {
            if (size() != other.size()) return false;
            for (auto it = begin(); it != end(); ++it) {
                auto oth = other.find(it->first());
                if (oth == other.end() || it->second() != oth->second())
                    return false;
            }
            return true;
        }

        /**
         * @brief Reports the total memory usage of this set.
         * @return Size in bytes of memory used by this set.
         * @note Includes memory used by inner sets, flags, and overhead.
         */
        size_t memory_usage() const noexcept {
            size_t total = 0;
            // count sub-maps
            for (uint8_t i = 0; i < cap_; ++i){
            if(set_in_use(i)){
                total += chain[i]->memory_usage();
            }else{
                if(chain[i]){
                    total+=14;
                }else{
                    total+=4;
                }
            }
            }

            total += (cap_*2 + 7)/8;    // flags
            total += sizeof(*this) + rangeMap.memory_usage() ;
            return total;
        }

        /**
         * @brief Compares this set with another for inequality.
         * @param other The set to compare with.
         * @return true if sets differ, false if they are equal.
         */
        bool operator!=(const ChainedUnorderedSet& other) const noexcept {
            return !(*this == other);
        }

        /**
         * @brief Optimizes memory usage of the set.
         * @return Number of bytes freed by the optimization.
         * @note Removes unused sets, compacts the chain, and minimizes memory allocation.
         */
        size_t fit() {
            if (chain == nullptr) return 0;
        
            size_t bytesFreed = 0;
            uint8_t activeSets = 0;
        
            // First pass: count active sets and free unused ones
            for (uint8_t i = 0; i < cap_; i++) {
                if (chain[i] != nullptr) {
                    if (getState(i) == slotState::Used) {
                        bytesFreed += chain[i]->fit();
                        activeSets++;
                    } else if (getState(i) == slotState::Deleted) {
                        delete chain[i];
                        chain[i] = nullptr;
                        setState(i, slotState::Empty);
                        bytesFreed += sizeof(unordered_set_s) + 32; // Approximate size
                    }
                }
            }
        
            if (activeSets <= 1) return bytesFreed;
        
            // Second pass: compact the chain if there are gaps
            uint8_t destIdx = 0;
            for (uint8_t srcIdx = 0; srcIdx < cap_; srcIdx++) {
                if (chain[srcIdx] != nullptr && getState(srcIdx) == slotState::Used) {
                    if (destIdx != srcIdx) {
                        chain[destIdx] = chain[srcIdx];
                        
                        // Update range mappings
                        for (auto it = rangeMap.begin(); it != rangeMap.end(); ++it) {
                            if (it->second == srcIdx) {
                                uint8_t range = it->first;
                                rangeMap[range] = destIdx;
                                break;
                            }
                        }
                        
                        setState(destIdx, slotState::Used);
                        chain[srcIdx] = nullptr;
                        setState(srcIdx, slotState::Empty);
                    }
                    destIdx++;
                }
            }


            // Optional: reduce array size if utilization is low
            if (activeSets < cap_ / 3 && cap_ > INIT_CAP) {
                uint16_t newCap = std::max(static_cast<uint16_t>(INIT_CAP), 
                                        static_cast<uint16_t>(activeSets * 2));
        
                unordered_set_s** newChain = mem_alloc::allocate<unordered_set_s*>(newCap);
                memset(newChain, 0, newCap * sizeof(unordered_set_s*));
        
                uint8_t* newFlags = new uint8_t[(newCap * 2 + 7) / 8];
                memset(newFlags, 0, (newCap * 2 + 7) / 8);
        
                // Copy active sets
                for (uint8_t i = 0; i < activeSets; i++) {
                    newChain[i] = chain[i];
                    setState(i, slotState::Used, newFlags);
                }
        
                // Free old arrays
                mem_alloc::deallocate(chain);
                delete[] flags;
        
                chain = newChain;
                flags = newFlags;
        
                uint16_t oldCap = cap_;
                cap_ = newCap;
        
                bytesFreed += (oldCap - newCap) * sizeof(unordered_set_s*);
                bytesFreed += ((oldCap * 2 + 7) / 8) - ((newCap * 2 + 7) / 8);
            }
        
            return bytesFreed;
        }

        /**
         * @brief Gets the total number of elements in the set.
         * @return The number of elements across all inner sets.
         */
        size_t size() const noexcept {                        
            size_t total = 0;
            for(uint8_t i = 0; i < cap_; i++) {
                if(chain[i] != nullptr) total += chain[i]->size();                     
            }
            return total;
        }

        /**
         * @brief Removes all elements from the set.
         * @note Deletes all inner sets and resets all state.
         */
        void clear() noexcept {
            // 1) delete all sub‐maps
            for (uint8_t i = 0; i < cap_; i++) {
                delete chain[i];
                chain[i] = nullptr;
            }

            // 2) reset flags to all‐Empty
            slots_init(cap_);            // zeroes & marks every slot Empty

            // 3) reset rangeMap to default
            rangeMap.clear();   
            rangeMap.fit();
            chain_size = 0;
        }

        /**
         * @brief Checks if the set is empty.
         * @return true if the set contains no elements, false otherwise.
         */
        bool empty() const {
            for (uint8_t i = 0; i < cap_; i++) {
                if(set_in_use(i)) {
                    return false; // At least one map is in use
                }
            }
            return true;
        }

        // swap helper for copy-and-swap
        friend void swap(ChainedUnorderedSet& a, ChainedUnorderedSet& b) noexcept {
            using std::swap;
            swap(a.fullness_,   b.fullness_);
            swap(a.cset_ability, b.cset_ability);
            swap(a.cap_,        b.cap_);
            swap(a.flags,       b.flags);        // from slot_handler
            swap(a.chain,       b.chain);
            swap(a.chain_size,  b.chain_size);
            swap(a.rangeMap,    b.rangeMap);
        }
    };

    // ------------------------------------------- Stack ----------------------------------------
    template <typename T>
    class Stack {
        using index_type = typename index_type<T>::type; // Use index_type from slot_handler

    private:
        T* arr;           
        index_type capacity; 
        index_type size;    

        // 255 with uint8_t, 65535 with uint16_t, otherwise 2000000000
        static constexpr index_type STACK_MAX_CAP_ = 
            std::is_same<T, uint8_t>::value ? 255 :
            std::is_same<T, int>::value || std::is_same<T, size_t>::value ? 2000000000 : 65535;

        // Resize the stack when needed
        void resize(index_type newCapacity) {
            if (newCapacity > STACK_MAX_CAP_) {
                newCapacity = STACK_MAX_CAP_;
            }
            if (newCapacity <= capacity) {
                return;
            }
            if (newCapacity == 0) {
                newCapacity = 1;
            }

            T* newArr = mem_alloc::allocate<T>(newCapacity);
            for (index_type i = 0; i < size; i++) {
                newArr[i] = arr[i];
            }
            mem_alloc::deallocate(arr);
            arr = newArr;
            capacity = newCapacity;
        }

    public:
        // Constructor initializes an empty stack with capacity 1
        Stack() : capacity(1), size(0) {
            arr = mem_alloc::allocate<T>(capacity);
        }
        
        // Destructor frees the dynamic memory
        ~Stack() {
            mem_alloc::deallocate(arr);
        }
        // Copy constructor
        Stack(const Stack& other) : capacity(other.capacity), size(other.size) {
            arr = mem_alloc::allocate<T>(capacity);
            for (index_type i = 0; i < size; i++) {
                arr[i] = other.arr[i];
            }
        }
        // Move constructor
        Stack(Stack&& other) noexcept : arr(other.arr), capacity(other.capacity), size(other.size) {
            other.arr = nullptr;
            other.size = 0;
            other.capacity = 0;
        }
        // Copy assignment operator
        Stack& operator=(const Stack& other) {
            if (this != &other) {
                mem_alloc::deallocate(arr);
                capacity = other.capacity;
                size = other.size;
                arr = mem_alloc::allocate<T>(capacity);
                for (index_type i = 0; i < size; i++) {
                    arr[i] = other.arr[i];
                }
            }
            return *this;
        }
        // Move assignment operator
        Stack& operator=(Stack&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(arr);
                arr = other.arr;
                capacity = other.capacity;
                size = other.size;
                other.arr = nullptr;
                other.size = 0;
                other.capacity = 0;
            }
            return *this;
        }
        

        // Push: Add an element to the top of the stack
        void push(const T& value) noexcept {
            if (size == capacity) {
                resize(capacity + capacity / 2 + 1);  // Increase capacity
            }
            arr[size++] = value;
        }

        // Pop: Remove and return the top element (LIFO)
        T pop() {
            if (size == 0) {
                throw std::underflow_error("Stack underflow: No elements to pop.");
            }
            return arr[--size];
        }

        // Return the top element without removing it
        T top() const {
            if (size == 0) {
                throw std::underflow_error("Stack is empty: No top element.");
            }
            return arr[size - 1];
        }

        // Check if the stack is empty
        bool empty() const {
            return size == 0;
        }

        // Get the current number of elements in the stack
        index_type getSize() const {
            return size;
        }

        // Clear the stack
        void clear() {
            size = 0;
        }

        bool uses_psram() const noexcept {
            return mem_alloc::is_psram_ptr(arr);
        }
    };
    // ------------------------------------------- Queue ----------------------------------------   
    template <typename T>
    class Queue {
        using index_type = typename index_type<T>::type;
    private:
        T* arr;           // Dynamic array for queue elements
        index_type capacity; // Current capacity of the array
        index_type size;     // Number of elements currently in the queue
        index_type head;     // Index of the front element
        index_type tail;     // Index where the next element will be inserted

        // 255 with uint8_t, 2000000000 with int, size_t, otherwise 65535
        static constexpr index_type QUEUE_MAX_CAP_ =
            std::is_same<T, uint8_t>::value ? 255 :
            std::is_same<T, int>::value || std::is_same<T, size_t>::value ? 2000000000 : 65535;

        // Resize and re-order elements when the array is full
        void resize(index_type newCapacity) {
            if (newCapacity > QUEUE_MAX_CAP_) {
                newCapacity = QUEUE_MAX_CAP_;
            }
            if (newCapacity <= capacity) {
                return;
            }
            if (newCapacity == 0) {
                newCapacity = 1;
            }

            const index_type oldCapacity = capacity == 0 ? 1 : capacity;
            T* newArr = mem_alloc::allocate<T>(newCapacity);
            for (index_type i = 0; i < size; i++) {
                newArr[i] = arr[(head + i) % oldCapacity];
            }
            mem_alloc::deallocate(arr);
            arr = newArr;
            capacity = newCapacity;
            head = 0;
            tail = (size == capacity) ? 0 : size;
        }

    public:
        // Constructor initializes an empty queue with capacity 1
        Queue() : capacity(1), size(0), head(0), tail(0) {
            arr = mem_alloc::allocate<T>(capacity);
        }
        
        // Destructor to free dynamic memory
        ~Queue() {
            mem_alloc::deallocate(arr);
        }
        // Copy constructor
        Queue(const Queue& other)
            : capacity(other.capacity == 0 ? 1 : other.capacity),
              size(other.size),
              head(0),
              tail(0) {
            arr = mem_alloc::allocate<T>(capacity);
            const index_type sourceCapacity = (other.capacity == 0) ? 1 : other.capacity;
            for (index_type i = 0; i < size; i++) {
                arr[i] = other.arr[(other.head + i) % sourceCapacity];
            }
            tail = (size == capacity) ? 0 : size;
        }
        // Move constructor
        Queue(Queue&& other) noexcept : arr(other.arr), capacity(other.capacity), size(other.size), head(other.head), tail(other.tail) {
            other.arr = nullptr;
            other.size = 0;
            other.head = 0;
            other.tail = 0;
        }
        // Copy assignment operator   
        Queue& operator=(const Queue& other) {
            if (this != &other) {
                const index_type sourceCapacity = (other.capacity == 0) ? 1 : other.capacity;
                if (capacity != sourceCapacity || arr == nullptr) {
                    mem_alloc::deallocate(arr);
                    arr = mem_alloc::allocate<T>(sourceCapacity);
                    capacity = sourceCapacity;
                }

                size = other.size;
                for (index_type i = 0; i < size; i++) {
                    arr[i] = other.arr[(other.head + i) % sourceCapacity];
                }
                head = 0;
                tail = (size == capacity) ? 0 : size;
            }
            return *this;
        }
        // Move assignment operator
        Queue& operator=(Queue&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(arr);
                arr = other.arr;
                capacity = other.capacity;
                size = other.size;
                head = other.head;
                tail = other.tail;
                other.arr = nullptr;
                other.size = 0;
                other.head = 0;
                other.tail = 0;
            }
            return *this;
        }

        // Enqueue: Add an element to the tail of the queue
        void enqueue(const T& value) {
            if (size == capacity) {
                resize(capacity + 5);  // Increase capacity
            }
            arr[tail] = value;
            tail = (tail + 1) % capacity;
            size++;
        }

        // Dequeue: Remove and return the front element (FIFO)
        T dequeue() {
            if (size == 0) {
                throw std::underflow_error("Queue underflow: No elements to dequeue.");
            }
            T value = arr[head];
            head = (head + 1) % capacity;
            size--;
            return value;
        }

        // Return the front element without removing it
        T front() const {
            if (size == 0) {
                throw std::underflow_error("Queue is empty: No front element.");
            }
            return arr[head];
        }
        // Check if the queue is empty
        bool empty() const {
            return size == 0;
        }

        // Get the current number of elements in the queue
        index_type getSize() const {
            return size;
        }

        // Clear the queue
        void clear() {
            size = 0;
            head = 0;
            tail = 0;
        }

        bool uses_psram() const noexcept {
            return mem_alloc::is_psram_ptr(arr);
        }
    };

    // ------------------------------------------- DeQueue ----------------------------------------
    template <typename T>
    class DeQueue {
        using index_type = typename index_type<T>::type;
    private:
        T* arr;
        index_type capacity;
        index_type size;
        index_type head;
        index_type tail;

        static constexpr index_type QUEUE_MAX_CAP_ =
        std::is_same<T, uint8_t>::value ? 255 :
        std::is_same<T, int>::value || std::is_same<T, size_t>::value ? 2000000000 : 65535;


        void resize(index_type newCapacity) {
            if (newCapacity > QUEUE_MAX_CAP_) {
                newCapacity = QUEUE_MAX_CAP_;
            }
            if (newCapacity <= capacity) {
                return;
            }
            if (newCapacity == 0) {
                newCapacity = 1;
            }

            const index_type oldCapacity = capacity == 0 ? 1 : capacity;
            T* newArr = mem_alloc::allocate<T>(newCapacity);
            for (index_type i = 0; i < size; i++) {
                newArr[i] = arr[(head + i) % oldCapacity];
            }
            mem_alloc::deallocate(arr);
            arr = newArr;
            capacity = newCapacity;
            head = 0;
            tail = (size == capacity) ? 0 : size;
        }

    public:
        // default constructor
        DeQueue() : capacity(1), size(0), head(0), tail(0) {
            arr = mem_alloc::allocate<T>(capacity);
        }

        ~DeQueue() {
            mem_alloc::deallocate(arr);
        }
        // copy constructor
        DeQueue(const DeQueue& other)
            : capacity(other.capacity == 0 ? 1 : other.capacity),
              size(other.size),
              head(0),
              tail(0) {
            arr = mem_alloc::allocate<T>(capacity);
            const index_type sourceCapacity = (other.capacity == 0) ? 1 : other.capacity;
            for (index_type i = 0; i < size; i++) {
                arr[i] = other.arr[(other.head + i) % sourceCapacity];
            }
            tail = (size == capacity) ? 0 : size;
        }
        // move constructor
        DeQueue(DeQueue&& other) noexcept : arr(other.arr), capacity(other.capacity), size(other.size), head(other.head), tail(other.tail) {
            other.arr = nullptr;
            other.size = 0;
            other.head = 0;
            other.tail = 0;
        }
        // copy assignment operator
        DeQueue& operator=(const DeQueue& other) {
            if (this != &other) {
                const index_type sourceCapacity = (other.capacity == 0) ? 1 : other.capacity;
                if (capacity != sourceCapacity || arr == nullptr) {
                    mem_alloc::deallocate(arr);
                    arr = mem_alloc::allocate<T>(sourceCapacity);
                    capacity = sourceCapacity;
                }

                size = other.size;
                for (index_type i = 0; i < size; i++) {
                    arr[i] = other.arr[(other.head + i) % sourceCapacity];
                }
                head = 0;
                tail = (size == capacity) ? 0 : size;
            }
            return *this;
        }
        // move assignment operator
        DeQueue& operator=(DeQueue&& other) noexcept {
            if (this != &other) {
                mem_alloc::deallocate(arr);
                arr = other.arr;
                capacity = other.capacity;
                size = other.size;
                head = other.head;
                tail = other.tail;
                other.arr = nullptr;
                other.size = 0;
                other.head = 0;
                other.tail = 0;
            }
            return *this;
        }

        void enqueueFront(const T& value) {
            if (size == capacity) {
                resize(capacity + 5);
            }
            head = (head == 0) ? capacity - 1 : head - 1;
            arr[head] = value;
            size++;
        }

        void enqueueBack(const T& value) {
            if (size == capacity) {
                resize(capacity + 5);
            }
            arr[tail] = value;
            tail = (tail + 1) % capacity;
            size++;
        }

        T dequeueFront() {
            if (size == 0) {
                throw std::underflow_error("DeQueue underflow: No elements to dequeue from front.");
            }
            T value = arr[head];
            head = (head + 1) % capacity;
            size--;
            return value;
        }

        T dequeueBack() {
            if (size == 0) {
                throw std::underflow_error("DeQueue underflow: No elements to dequeue from back.");
            }
            tail = (tail == 0) ? capacity - 1 : tail - 1;
            T value = arr[tail];
            size--;
            return value;
        }

        T front() const {
            if (size == 0) {
                throw std::underflow_error("DeQueue is empty: No front element.");
            }
            return arr[head];
        }

        T back() const {
            if (size == 0) {
                throw std::underflow_error("DeQueue is empty: No back element.");
            }
            return arr[(tail == 0) ? capacity - 1 : tail - 1];
        }

        bool empty() const {
            return size == 0;
        }

        index_type getSize() const {
            return size;
        }

        void clear() {
            size = 0;
            head = 0;
            tail = 0;
        }

        bool uses_psram() const noexcept {
            return mem_alloc::is_psram_ptr(arr);
        }
    };

}   // namespace MCU