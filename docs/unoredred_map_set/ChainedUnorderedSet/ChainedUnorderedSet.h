// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include "../unordered_map/unordered_map.h"
#include "../unordered_set/unordered_set.h"
#include <type_traits>
#include <cassert>
#include <utility>   

// -----------------------------------------------------------------------------------------
// ---------------------------------- ChainedUnorderedSet class -----------------------------------
// -----------------------------------------------------------------------------------------
template <typename T>
class ChainedUnorderedSet : public slot_handler, hash_kernel{
public:
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
        
        // Allocate new resources
        flags = new uint8_t[(newChainCap * 2 + 7) / 8];
        memset(flags, 0, (newChainCap * 2 + 7) / 8);     // Mark all maps as empty
        // slots_init(newChainCap); // Initialize flags, all slots are empty

        if(chain_size >= 234){
            rangeMap.set_fullness(1.0);
        }
        
        chain = new unordered_set_s*[newChainCap];
        memset(chain, 0, newChainCap * sizeof(unordered_set_s*)); // Initialize all pointers to nullptr

        cap_ = static_cast<uint8_t>(newChainCap);
        // rangeMap.resize(newChainCap);        // rangeMap change
        
        if (chain != nullptr) {
            for (uint8_t i = 0; i < oldCap; i++) {
                if (oldChain[i] != nullptr) {
                    chain[i] = oldChain[i]; // Keep existing maps
                    slotState s = getStateFrom(oldFlags, i);
                    if (s != slotState::Empty) {
                        setState(i, s); // Preserve states
                    }
                }
            }
            // Free old resources
            delete[] oldChain;
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
            delete[] chain;
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
        chain = new unordered_set_s*[this->cap_];
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
        if (activeSets < cap_ / 3 && cap_ > SET_INIT_CAP) {
            uint16_t newCap = std::max(static_cast<uint16_t>(SET_INIT_CAP), 
                                    static_cast<uint16_t>(activeSets * 2));
    
            unordered_set_s** newChain = new unordered_set_s*[newCap];
            memset(newChain, 0, newCap * sizeof(unordered_set_s*));
    
            uint8_t* newFlags = new uint8_t[(newCap * 2 + 7) / 8];
            memset(newFlags, 0, (newCap * 2 + 7) / 8);
    
            // Copy active sets
            for (uint8_t i = 0; i < activeSets; i++) {
                newChain[i] = chain[i];
                setState(i, slotState::Used, newFlags);
            }
    
            // Free old arrays
            delete[] chain;
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
