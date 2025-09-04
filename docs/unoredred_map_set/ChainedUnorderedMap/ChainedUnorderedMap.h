// Custom STL for MCU : super memory saver
#pragma once

#include <stdexcept>
#include "../unordered_map/unordered_map.h"
#include <type_traits>
#include <cassert>
#include <utility>    
    
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
        [[nodiscard]] inline pair_kmi keyMappingIN(const R& key) noexcept {
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
            
            // Allocate new resources
            flags = new uint8_t[(newChainCap * 2 + 7) / 8];
            memset(flags, 0, (newChainCap * 2 + 7) / 8);     // Mark all maps as empty
            // slots_init(newChainCap); // Initialize flags, all slots are empty

            if(chain_size >= 234){
                rangeMap.set_fullness(1.0);
            }
            
            chain = new unordered_map_s*[newChainCap];
            memset(chain, 0, newChainCap * sizeof(unordered_map_s*)); // Initialize all pointers to nullptr

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
                delete[] chain;
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
            chain = new unordered_map_s*[this->cap_];
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
                auto* newChain = new unordered_map_s*[newCap];
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
                delete[] chain;
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



