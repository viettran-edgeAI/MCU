# `vector` & `b_vector`

Dynamic array containers optimized for microcontrollers, designed as drop-in replacements for `std::vector` with additional memory-saving features and built-in sorting capabilities.

---

## 🧠 Core Concept

![vector and b_vector architecture](../../imgs/vector&b_vector.jpg)

### `mcu::vector<T>`
**Heap-only dynamic array** - similar to `std::vector` but optimized for embedded systems:
- Always allocates on heap (supports PSRAM on ESP32)
- Smaller object footprint than `std::vector`
- Built-in `sort()` method
- No code bloat from template instantiation

### `mcu::b_vector<T, sboSize>`
**Small Buffer Optimization (SBO) vector** - stack buffer for small collections:
- Template parameter `sboSize`: buffer size (auto-calculated if omitted)
- Uses stack buffer when `size <= sboSize`
- Switches to heap when exceeding buffer
- Ideal for frequently-small collections
- Avoids heap allocation overhead

**Template Parameters:**
```cpp
template<typename T>
class vector;

template<typename T, size_t sboSize = 0>
class b_vector;
```

- **`T`**: Element type (any copyable/movable type)
- **`sboSize`** (b_vector only): Stack buffer size
  - `0` = auto-calculate based on `sizeof(T)`
  - Explicit value = use that many elements

**Auto-Calculated SBO Sizes:**
```cpp
sizeof(T) == 1:  sboSize = 32  // char, uint8_t, bool
sizeof(T) == 2:  sboSize = 16  // short, uint16_t
sizeof(T) == 4:  sboSize = 8   // int, float, uint32_t
sizeof(T) == 8:  sboSize = 6   // double, uint64_t
sizeof(T) <= 64: sboSize = 4   // medium structs
sizeof(T) > 64:  sboSize = 1   // large structs
```

---

## ✨ Key Features

| Feature | `vector<T>` | `b_vector<T, sboSize>` |
|---------|-------------|------------------------|
| **Heap allocation** | Always | When size > sboSize |
| **Stack buffer** | No | Yes (sboSize elements) |
| **PSRAM support** | Yes (ESP32) | Yes (ESP32) |
| **Built-in sort** | ✅ | ✅ |
| **Implicit conversion** | To b_vector | To vector |
| **Object size** | ~12-16 bytes | ~16-24 bytes + buffer |
| **Best for** | General purpose | Small collections |

### Common Features

Both containers support:
- **Standard operations**: `push_back`, `pop_back`, `insert`, `erase`, `resize`, `reserve`, `clear`
- **Element access**: `operator[]`, `at()`, `front()`, `back()`, `data()`
- **Iterators**: Random-access iterators compatible with STL algorithms
- **Capacity management**: `size()`, `capacity()`, `empty()`, `fit()`
- **Perfect forwarding**: `emplace_back()`, `emplace()`
- **Memory tracking**: `memory_usage()`

---

## 🛠 Constructors

```cpp
using mcu::vector;
using mcu::b_vector;

// Default constructors
vector<int> v1;                          // empty, capacity=1
b_vector<int> b1;                        // empty, uses SBO buffer

// Capacity constructors
vector<int> v2(100);                     // size=100, values default-initialized
b_vector<int> b2(20);                    // size=20, heap if > sboSize

// Fill constructors
vector<float> v3(50, 3.14f);             // 50 elements, all = 3.14
b_vector<float> b3(10, 2.5f);            // 10 elements, all = 2.5

// Initializer list
vector<uint8_t> v4({1, 2, 3, 4, 5});     // From min_init_list
b_vector<uint8_t> b4({10, 20, 30});      // From min_init_list

// Custom SBO size
b_vector<int, 64> large_buffer;          // 64-element stack buffer
b_vector<int, 4> tiny_buffer;            // 4-element stack buffer

// Explicit sboSize=0 (auto-calculate)
b_vector<uint32_t, 0> auto_sized;        // sboSize=8 (auto for 4-byte type)
```

### Copy & Move

```cpp
vector<int> original = {1, 2, 3};

// Copy construction
vector<int> copy1(original);             // Deep copy
b_vector<int> copy2(original);           // vector → b_vector (implicit)

// Move construction
vector<int> moved1(std::move(original)); // Efficient move
b_vector<int> moved2(std::move(copy1));  // Cross-type move

// Assignment
vector<int> v5;
v5 = copy2;                              // b_vector → vector (implicit)
```

---

## 📦 Runtime Operations

### Adding Elements

```cpp
vector<int> vec;

// Append
vec.push_back(10);                       // Copy element
vec.emplace_back(20);                    // Construct in-place

// Insert at position
vec.insert(0, 5);                        // Insert at index 0
vec.emplace(2, 15);                      // Construct at index 2

// Insert range
std::vector<int> other = {100, 200};
vec.insert(vec.begin() + 1, other.begin(), other.end());
```

### Removing Elements

```cpp
vec.pop_back();                          // Remove last element
vec.erase(2);                            // Remove element at index 2
vec.erase(vec.begin() + 1);              // Remove via iterator

// Erase range
vec.erase(vec.begin(), vec.begin() + 3); // Remove first 3 elements

vec.clear();                             // Remove all (keeps capacity)
```

### Capacity Management

```cpp
// Pre-allocate
vec.reserve(1000);                       // Reserve capacity
vec.resize(500);                         // Resize (default-initialize new)
vec.resize(600, 42);                     // Resize with fill value

// Optimize memory
vec.fit();                               // Shrink capacity to size

// Fill
vec.fill(99);                            // Set all elements to 99
vec.assign(10, 7);                       // Replace with 10×7

// Query
size_t sz = vec.size();                  // Current size
size_t cap = vec.capacity();             // Current capacity
bool empty = vec.empty();                // size() == 0?
```

---

## 🎯 Element Access

### Safe Access

```cpp
vector<int> vec = {10, 20, 30, 40};

// Read - bounds-checked (returns default on OOB)
int val1 = vec[2];                       // 30
int val2 = vec[100];                     // Returns last element (40)

// Write
vec[1] = 25;                             // Modify element

// Bounds-checked with assert (debug builds)
int& ref = vec.at(2);                    // Throws/asserts if OOB

// Front/back
int first = vec.front();                 // 10
int last = vec.back();                   // 40

// Direct pointer access
int* data = vec.data();                  // Raw pointer to elements
```

**Safety Features:**
- `operator[]`: Returns last element if index >= size (embedded-safe)
- `at()`: Asserts in debug builds, safe fallback in release
- `front()`/`back()`: Assert on empty vector
- No undefined behavior on out-of-bounds access

---

## 🔄 Iterators

```cpp
vector<int> vec = {1, 2, 3, 4, 5};

// Range-based for
for (int v : vec) {
    // v is a copy
}

for (int& v : vec) {
    v *= 2;  // Modify in place
}

// Manual iteration
for (auto it = vec.begin(); it != vec.end(); ++it) {
    *it += 10;
}

// STL algorithms
std::find(vec.begin(), vec.end(), 3);
std::transform(vec.begin(), vec.end(), vec.begin(), 
               [](int x) { return x * 2; });

// Reverse iteration
for (auto it = vec.end() - 1; it >= vec.begin(); --it) {
    // Process backwards
}
```

**Iterator Features:**
- **Category**: Random-access iterator
- **Operations**: `++`, `--`, `+`, `-`, `+=`, `-=`, comparison operators
- **Compatibility**: Works with all STL algorithms

---

## 🔀 Built-in Sorting

Both `vector` and `b_vector` include optimized quicksort with safety features:

```cpp
vector<int> vec = {5, 2, 8, 1, 9, 3};
vec.sort();  // In-place sort: [1, 2, 3, 5, 8, 9]

// Supports numeric types
vector<float> floats = {3.14f, 1.41f, 2.71f};
floats.sort();

// Supports custom types with operator<
struct Point {
    int x, y;
    bool operator<(const Point& other) const {
        return x < other.x || (x == other.x && y < other.y);
    }
};

vector<Point> points = {{3, 5}, {1, 2}, {3, 1}};
points.sort();  // Sorts by x, then y
```

**Sorting Safety Features:**
- Recursion depth limit (24 levels) prevents stack overflow
- Falls back to bubble sort if recursion limit exceeded
- Comprehensive bounds checking
- Safe for embedded systems with limited stack

**Performance:**
- Average: O(n log n)
- Worst case: O(n²) with fallback
- Space: O(log n) stack (limited to 24 frames)

---

## 🏗 Small Buffer Optimization (SBO) - `b_vector`

### How SBO Works

```cpp
b_vector<int, 8> vec;  // 8-element stack buffer

// Small collection (≤8 elements) - uses stack
vec.push_back(1);  // stack
vec.push_back(2);  // stack
vec.push_back(3);  // stack
// ... up to 8 elements

// Exceeds buffer - switches to heap
vec.push_back(9);  // heap allocation, copies 1-8 to heap

// Further operations use heap
vec.push_back(10); // heap
```

### SBO State Management

```cpp
b_vector<uint8_t, 32> vec;

// Check current state (internal)
bool on_heap = vec.using_heap;  // false initially
size_t cap = vec.capacity();    // 32 (SBO_SIZE)

// Add elements
for (int i = 0; i < 32; ++i) {
    vec.push_back(i);  // All on stack
}

vec.push_back(33);  // Triggers heap allocation
// using_heap = true, capacity grows to 64
```

### Memory Layout

**`b_vector<T, sboSize>` object:**
```cpp
union {
    T* heap_array;           // 4-8 bytes (when using heap)
    char buffer[sizeof(T) * sboSize];  // Used when size <= sboSize
};
size_t size_;                // 4-8 bytes
size_t capacity_;            // 4-8 bytes
bool using_heap;             // 1 byte
```

**Total object size:**
- Stack mode: ~16 bytes + `sizeof(T) * sboSize`
- Heap mode: ~16 bytes (buffer unused)

---

## 🧮 Memory Footprint

### `vector<T>` Memory

```cpp
vector<int> vec(100);
size_t bytes = vec.memory_usage();  // Object + heap allocation

// Formula:
// sizeof(vector<T>) ≈ 12-16 bytes (pointer + 2×size_t)
// Heap: capacity * sizeof(T)
// Total: sizeof(vector<T>) + capacity * sizeof(T)
```

**Example (32-bit system):**
```cpp
vector<uint8_t> bytes(100);
// Object: 12 bytes
// Heap: 100 bytes
// Total: 112 bytes
```

### `b_vector<T, sboSize>` Memory

```cpp
b_vector<uint8_t, 32> vec(20);  // 20 < 32, uses SBO
size_t bytes = vec.memory_usage();

// Object size: sizeof(b_vector) ≈ 16 bytes + buffer
// Buffer: sizeof(uint8_t) * 32 = 32 bytes
// Total: ~48 bytes (all on stack)

vec.resize(50);  // Exceeds SBO, allocates heap
// Object: ~16 bytes (buffer unused)
// Heap: 50 bytes
// Total: ~66 bytes
```

### Memory Comparison

| Container | Elements | Element Type | Object | Data | Total | Location |
|-----------|----------|--------------|--------|------|-------|----------|
| `std::vector<int>` | 100 | int | ~24 bytes | 400 bytes | ~424 bytes | Heap |
| `mcu::vector<int>` | 100 | int | ~12 bytes | 400 bytes | ~412 bytes | Heap |
| `b_vector<int, 128>` | 100 | int | ~528 bytes | 0 bytes | ~528 bytes | Stack |
| `b_vector<int, 64>` | 100 | int | ~16 bytes | 400 bytes | ~416 bytes | Heap |

**Key Insights:**
- `mcu::vector`: ~3% smaller than `std::vector` (object overhead)
- `b_vector` (SBO mode): No heap allocation, ~25% more total memory
- `b_vector` (heap mode): Similar to `vector` with small overhead
- Break-even: When `size ≈ sboSize`, trade-off depends on allocation frequency

---

## 🎯 Usage Patterns & Best Practices

### When to Use `vector`

✅ **Ideal Use Cases:**
- **General-purpose**: Default choice for dynamic arrays
- **Large collections**: > 100 elements typically
- **PSRAM allocation**: ESP32 with PSRAM enabled
- **Predictable growth**: Known capacity ahead of time
- **Frequent resizing**: Amortized O(1) push_back

❌ **Not Recommended:**
- Very small collections (< 10 elements) with frequent creation/destruction
- Hot path code with allocation-sensitive timing

### When to Use `b_vector`

✅ **Ideal Use Cases:**
- **Small collections**: Frequently < sboSize elements
- **Temporary vectors**: Created and destroyed often
- **Stack allocation**: Avoid heap fragmentation
- **Known max size**: Set sboSize to typical maximum
- **Allocation-sensitive**: Real-time or interrupt contexts

❌ **Not Recommended:**
- Collections that frequently exceed sboSize (3-10x slower)
- Very large sboSize (wastes stack space)
- Frequent copying (expensive to copy large buffers)

### Choosing SBO Size

```cpp
// Rule of thumb: Set sboSize to 90th percentile of expected size

// Small: Typical size 4-8 elements
b_vector<int, 8> sensor_readings;

// Medium: Typical size 20-30 elements
b_vector<uint8_t, 32> packet_buffer;

// Large: Typical size 100-200 elements (rare)
b_vector<float, 256> feature_vector;

// Dynamic: Unknown typical size → use vector
vector<int> dynamic_data;
```

### Capacity Pre-Allocation

```cpp
// ✅ Good: Reserve before bulk operations
vector<int> vec;
vec.reserve(1000);  // Single allocation
for (int i = 0; i < 1000; ++i) {
    vec.push_back(i);  // No reallocations
}

// ❌ Bad: Repeated reallocations
vector<int> vec2;
for (int i = 0; i < 1000; ++i) {
    vec2.push_back(i);  // Multiple reallocations
}

// 🎯 Best: Use constructor
vector<int> vec3(1000);  // Direct allocation
for (int i = 0; i < 1000; ++i) {
    vec3[i] = i;  // No allocations
}
```

### Memory Optimization

```cpp
vector<int> vec;
vec.reserve(1000);
// ... populate with only 100 elements
vec.fit();  // Shrink capacity from 1000 to 100

// Check memory usage
size_t before = vec.memory_usage();
vec.fit();
size_t after = vec.memory_usage();
size_t saved = before - after;  // Bytes freed
```

---

## ⚡ Performance Characteristics

### Benchmark Results (vs `std::vector`)

Full benchmark details: [Vector Performance Comparison](../../STL_MCU.md#performance-comparison)

**Summary:**

| Operation | `mcu::vector` | `b_vector` (size < SBO) | `b_vector` (size > SBO) |
|-----------|---------------|-------------------------|-------------------------|
| push_back | **1.5x faster** | **1.7x faster** | 3.6x slower |
| Random access | Same | Same | Same |
| Iteration | Same | 1.05x slower | 4.2x slower |
| Copy | 2.8x slower | 9x slower | 10x slower |

**Key Takeaways:**
1. **`mcu::vector`**: Consistently 30-50% faster insertions, comparable access
2. **`b_vector` (SBO)**: Best for small collections, 70% faster insertions
3. **`b_vector` (heap)**: Avoid frequent heap mode transitions
4. **Copy operations**: Use `std::vector` if copying is frequent

### Complexity Analysis

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| `push_back()` | O(1) amortized | May trigger reallocation |
| `pop_back()` | O(1) | |
| `insert(pos)` | O(n) | Shifts elements |
| `erase(pos)` | O(n) | Shifts elements |
| `operator[]` | O(1) | Bounds-checked |
| `sort()` | O(n log n) avg | O(n²) worst case |
| `reserve()` | O(n) | Copies existing elements |
| `resize()` | O(n) | Initializes new elements |

---

## 🔄 Implicit Conversions

`vector` and `b_vector` can be seamlessly converted to each other:

```cpp
vector<int> vec1 = {1, 2, 3, 4, 5};

// Implicit conversion: vector → b_vector
b_vector<int> b_vec1 = vec1;  // Copies elements

// Implicit conversion: b_vector → vector
b_vector<int, 16> b_vec2 = {10, 20, 30};
vector<int> vec2 = b_vec2;  // Copies elements

// Assignment conversions
vector<int> vec3;
vec3 = b_vec2;  // b_vector → vector

b_vector<int> b_vec3;
b_vec3 = vec1;  // vector → b_vector

// Cross-SBO size conversions
b_vector<int, 32> large = {1, 2, 3};
b_vector<int, 8> small = large;  // Copies to smaller SBO
```

**Conversion Behavior:**
- Always creates a copy (not a view)
- SBO state depends on destination size
- Efficient: Direct element copy, no unnecessary allocations

---

## 🔒 Thread Safety

**Both `vector` and `b_vector` are NOT thread-safe:**
- Requires external synchronization for concurrent access
- Read-only operations from multiple threads: UNSAFE
- Modifications from multiple threads: UNDEFINED BEHAVIOR

**Safe Multi-threading:**
```cpp
#include <mutex>

std::mutex vec_mutex;
vector<int> shared_vec;

// Thread 1: Write
{
    std::lock_guard<std::mutex> lock(vec_mutex);
    shared_vec.push_back(10);
}

// Thread 2: Read
{
    std::lock_guard<std::mutex> lock(vec_mutex);
    int val = shared_vec[0];
}
```

---

## ✅ Compatibility & Requirements

**Standard:** C++17 or later
- Uses `if constexpr`, `std::is_arithmetic_v`, `std::is_trivially_destructible_v`
- Requires `<type_traits>`, `<utility>`, `<cassert>`

**Platforms:** Optimized for microcontrollers
- ESP32, ESP8266, STM32, Arduino
- 32-bit and 64-bit architectures
- Tested on ARM Cortex-M, RISC-V, x86

**Dependencies:**
- `mem_alloc`: Custom allocator with PSRAM support (ESP32)
- `min_init_list<T>`: Lightweight initializer list alternative
- `hash_kernel`: For `sort()` with non-arithmetic types

**Memory Allocator Features:**
- Automatic PSRAM allocation on ESP32 when available
- Falls back to DRAM if PSRAM unavailable
- Proper alignment for all types
- No exceptions (returns nullptr on allocation failure)

---

## 📝 API Quick Reference

### Construction & Assignment
```cpp
vector<T>();                              // Default
vector<T>(capacity);                      // Reserve capacity
vector<T>(size, value);                   // Fill constructor
vector<T>(init_list);                     // From min_init_list

b_vector<T, sboSize>();                   // Default with SBO
b_vector<T, sboSize>(capacity);           // May use heap
b_vector<T, sboSize>(size, value);        // Fill constructor
b_vector<T, sboSize>(init_list);          // From min_init_list

vec = other;                              // Copy/move assignment
vec.assign(count, value);                 // Replace with count×value
```

### Element Access
```cpp
T& vec[index];                            // Bounds-checked (safe fallback)
T& vec.at(index);                         // Bounds-checked (asserts)
T& vec.front();                           // First element
T& vec.back();                            // Last element
T* vec.data();                            // Pointer to elements
```

### Capacity & Size
```cpp
size_t vec.size();                        // Current element count
size_t vec.capacity();                    // Current capacity
bool vec.empty();                         // size() == 0?
vec.reserve(new_capacity);                // Pre-allocate
vec.resize(new_size);                     // Grow/shrink (default-init)
vec.resize(new_size, fill_value);         // Grow/shrink (with fill)
vec.fit();                                // Shrink capacity to size
vec.clear();                              // size=0, keep capacity
vec.fill(value);                          // Set all elements
```

### Modifiers
```cpp
vec.push_back(value);                     // Append (copy)
vec.emplace_back(args...);                // Append (construct in-place)
vec.pop_back();                           // Remove last element
vec.insert(index, value);                 // Insert at position
vec.emplace(index, args...);              // Construct at position
vec.insert(pos, first, last);             // Insert range
vec.erase(index);                         // Remove at index
vec.erase(pos);                           // Remove via iterator
vec.erase(first, last);                   // Remove range
vec.sort();                               // In-place quicksort
```

### Iteration
```cpp
for (auto& elem : vec) { ... }            // Range-based for
T* it = vec.begin();                      // Iterator to first
T* it = vec.end();                        // Iterator past last
```

### Memory & Info
```cpp
size_t vec.memory_usage();                // Total bytes used
```

---

## 🔗 See Also

- **[STL_MCU Overview](../../STL_MCU.md)** - Complete library documentation
- **[packed_vector](../packed_vector/README.md)** - Bit-packed vector for small integers
- **[ID_vector](../ID_vector/README.md)** - Optimized for repeated integer IDs
- **[Hash Containers](../../unordered_map_set/README.md)** - Memory-efficient hash maps/sets

---

*`vector` and `b_vector` are part of the STL_MCU library, designed for embedded systems and memory-constrained applications on microcontrollers.*
