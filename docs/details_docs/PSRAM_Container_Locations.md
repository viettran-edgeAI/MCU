# PSRAM Migration - Quick Reference for STL_MCU.h

## Summary: Replace These Patterns

```cpp
// ALLOCATE
new T[count]              →  mem_alloc::allocate<T>(count)
new Type[size]            →  mem_alloc::allocate<Type>(size)

// DEALLOCATE  
delete[] ptr              →  mem_alloc::deallocate(ptr)
```

---

## Containers in STL_MCU.h to Update

### ✅ Already Done:
- `unordered_map_s` (lines ~54-600) - ✅ Complete

### 🔄 Need Updates:

#### 1. **unordered_set_s** (lines ~738-1100)
**Find and replace:**
- Line ~687: `table = new T[newCap];` → `table = mem_alloc::allocate<T>(newCap);`
- Line ~754: `table = new T[cap_];` → `table = mem_alloc::allocate<T>(cap_);`
- Line ~798: `table = new T[cap_];` → `table = mem_alloc::allocate<T>(cap_);`
- All `delete[] table;` → `mem_alloc::deallocate(table);`

#### 2. **b_vector** (lines ~1274-1650)
**Find and replace:**
- Line ~1329: `heap_array = new T[capacity_];` → `heap_array = mem_alloc::allocate<T>(capacity_);`
- Line ~1366: `heap_array = new T[capacity_];` → `heap_array = mem_alloc::allocate<T>(capacity_);`
- Line ~1425: `T* newArray = new T[newCapacity];` → `T* newArray = mem_alloc::allocate<T>(newCapacity);`
- Line ~1439: `T* new_heap = new T[new_capacity];` → `T* new_heap = mem_alloc::allocate<T>(new_capacity);`
- Line ~1477: `heap_array = new T[initialCapacity];` → `heap_array = mem_alloc::allocate<T>(initialCapacity);`
- All `delete[] heap_array;` → `mem_alloc::deallocate(heap_array);`
- All `delete[] newArray;` → `mem_alloc::deallocate(newArray);`

#### 3. **vector** (lines ~2241-2700)
**Find allocations/deallocations and apply same pattern**

#### 4. **packed_vector** (search for it)
**Find allocations/deallocations and apply same pattern**

#### 5. **ID_vector** (search for it)
**Find allocations/deallocations and apply same pattern**

---

## Step-by-Step Process

### For Each Container:

1. **Search for allocations:**
   ```
   Find: new T\[
   or:   new.*\[
   ```

2. **Replace pattern:**
   ```cpp
   // Before
   ptr = new T[size];
   
   // After  
   ptr = mem_alloc::allocate<T>(size);
   ```

3. **Search for deallocations:**
   ```
   Find: delete\[\]
   ```

4. **Replace pattern:**
   ```cpp
   // Before
   delete[] ptr;
   
   // After
   mem_alloc::deallocate(ptr);
   ```

5. **Verify move operations:** (should NOT have allocations)
   ```cpp
   // Move constructor - NO changes needed
   Container(Container&& other) noexcept {
       data = other.data;  // Just copy pointer
       other.data = nullptr;
   }
   ```

---

## Testing Each Container

After updating each container, test with:

```cpp
#define RF_USE_PSRAM
#include "STL_MCU.h"

void test_container() {
    size_t before = mcu::mem_alloc::get_free_psram();
    
    {
        mcu::your_container<int> test;
        test.reserve(1000);
        // Use container...
    }
    
    size_t after = mcu::mem_alloc::get_free_psram();
    
    // Should be equal (or close due to fragmentation)
    Serial.printf("PSRAM leak check: %d bytes\n", before - after);
}
```

---

## Example: Updating unordered_set_s

### Location 1: rehash() function
```cpp
// BEFORE (line ~687)
void rehash(uint8_t newCap) {
    // ...
    table = new T[newCap];
    flags = new uint8_t[(newCap * 2 + 7) / 8];
    // ...
    delete[] oldTable;
    delete[] oldFlags;
}

// AFTER
void rehash(uint8_t newCap) {
    // ...
    table = mem_alloc::allocate<T>(newCap);           // ← Change 1
    flags = new uint8_t[(newCap * 2 + 7) / 8];       // flags stays same (small)
    // ...
    mem_alloc::deallocate(oldTable);                  // ← Change 2
    delete[] oldFlags;                                // flags stays same
}
```

### Location 2: Copy Constructor
```cpp
// BEFORE (line ~754)
unordered_set_s(const unordered_set_s& other) {
    // ...
    table = new T[cap_];
    // ...
}

// AFTER
unordered_set_s(const unordered_set_s& other) {
    // ...
    table = mem_alloc::allocate<T>(cap_);  // ← Change
    // ...
}
```

### Location 3: Copy Assignment
```cpp
// BEFORE (line ~790+)
unordered_set_s& operator=(const unordered_set_s& other) {
    if (this != &other) {
        delete[] table;
        // ...
        table = new T[cap_];
        // ...
    }
    return *this;
}

// AFTER
unordered_set_s& operator=(const unordered_set_s& other) {
    if (this != &other) {
        mem_alloc::deallocate(table);              // ← Change 1
        // ...
        table = mem_alloc::allocate<T>(cap_);      // ← Change 2
        // ...
    }
    return *this;
}
```

### Location 4: Move Assignment
```cpp
// BEFORE (line ~820+)
unordered_set_s& operator=(unordered_set_s&& other) noexcept {
    if (this != &other) {
        delete[] table;
        // ...
        table = other.table;  // Transfer, no allocation
        other.table = nullptr;
    }
    return *this;
}

// AFTER
unordered_set_s& operator=(unordered_set_s&& other) noexcept {
    if (this != &other) {
        mem_alloc::deallocate(table);  // ← Only this changes
        // ...
        table = other.table;           // No allocation
        other.table = nullptr;
    }
    return *this;
}
```

### Location 5: Destructor
```cpp
// BEFORE
~unordered_set_s() {
    delete[] table;
}

// AFTER
~unordered_set_s() {
    mem_alloc::deallocate(table);  // ← Change
}
```

---

## Don't Forget!

1. ✅ Update allocation: `new T[n]` → `mem_alloc::allocate<T>(n)`
2. ✅ Update deallocation: `delete[] ptr` → `mem_alloc::deallocate(ptr)`
3. ✅ Leave move operations unchanged (they don't allocate)
4. ✅ Test each container after changes
5. ✅ Check for memory leaks with PSRAM monitoring

---

## Final Checklist Per Container

- [ ] Found all `new T[...]` allocations
- [ ] Replaced with `mem_alloc::allocate<T>(...)`
- [ ] Found all `delete[]` deallocations
- [ ] Replaced with `mem_alloc::deallocate(...)`
- [ ] Verified move constructor has NO allocation
- [ ] Verified move assignment only deallocates old data
- [ ] Compiled without errors
- [ ] Tested with `RF_USE_PSRAM` defined
- [ ] Tested without `RF_USE_PSRAM` (backward compatibility)
- [ ] Verified no memory leaks

---

That's the complete pattern! Apply to each container and you're done. 🚀
