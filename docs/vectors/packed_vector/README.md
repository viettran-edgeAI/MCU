# `packed_vector`

A `packed_vector` stores small integers using only the minimum number of bits per element. It mimics a subset of `std::vector<uint8_t>` while aggressively optimizing for flash/RAM usage on MCUs.

---

## 🧠 Core idea

- Template parameters:
  - `BitsPerElement` (1.. platform-word-limit) – compile-time bit width for each value when the vector is created. The implementation now supports BitsPerElement > 32 using multi-word reads/writes; the exact practical maximum depends on your target's word size (see "Word size & multi-word support" below).
  - `SizeFlag` – chooses index size (`TINY`, `SMALL`, `MEDIUM`, `LARGE`). Defaults to `MEDIUM` (`uint16_t`)- max 65535 elements.
- Internally uses `PackedArray` to bit-pack data with the current runtime bits-per-value (`bpv`). `PackedArray` now uses a platform-dependent `word_t` (alias for `size_t`) as its storage word. Elements may span multiple words when `bpv > WORD_BITS` (where `WORD_BITS == sizeof(size_t)*8`).
- Supports runtime reconfiguration via `set_bits_per_value()` to shrink/grow element width (existing data is cleared, capacity reused).

---

## ✨ Key features

| Feature | Notes |
| --- | --- |
| Bit packing | Only `bpv` bits per element are stored; default equals `BitsPerElement`. Supports bit widths larger than a single native word by splitting the element across multiple words.
| Multiple index sizes | `TINY` (15 max elements), `SMALL` (`uint8_t`), `MEDIUM` (`uint16_t`), `LARGE` (`size_t`).
| Constructors | Default, capacity, size+value, initializer list, range copy (same or cross bit sizes/size flags).
| Assignment | `assign(count, value)` and `assign(min_init_list<uint8_t>)`.
| Iterators | Random-access iterators & const variants supporting pointer-like arithmetic.
| Utility | `push_back`, `resize`, `reserve`, `fill`, `fit`, `front/back`, equality operators, `memory_usage()`.

---

## 🛠 Constructors & factory helpers

```cpp
using mcu::packed_vector;
using mcu::TINY;

packed_vector<3> empty;                       // default
packed_vector<4> with_capacity(32);           // reserve space only
packed_vector<2> with_data(8, 3);             // size=8, value=3 (auto clamps to 2-bit max)

// Wide bit-width examples (multi-word read/writes supported)
packed_vector<48, uint64_t> wide48;           // 48-bit elements (use 64-bit ValueType for safety)
packed_vector<40, uint64_t> wide40;

// For values wider than native word (`WORD_BITS`), the implementation splits
// values across multiple words; use a wide ValueType (e.g. `uint64_t`) to
// avoid narrowing and to get predictable behavior across platforms.
```

### Initializer-list support

Use `mcu::min_init_list<uint8_t>` or the `MAKE_UINT8_LIST` macro family:

```cpp
// Macro can prepend the target bpv for readability. The loader strips it automatically
// when it detects values that exceed the header.
packed_vector<3> test_vec = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);
// or 
packed_vector<3> test_vec = MAKE_LIST(uint8_t, 1, 2, 0, 4, 6, 2, 7, 0);

// Direct min_init_list
auto init = mcu::min_init_list<uint8_t>((const uint8_t[]){1,2,3,0}, 4);
packed_vector<2> from_list(init);
```

If you use the macro *without* including the header value, the data is stored exactly as specified.

### Range constructors

```cpp
packed_vector<3> source = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5);
packed_vector<3> mid(source, 1, 4);           // copies indices [1,4)
packed_vector<2> cross(source, 0, 5);         // clamps values to 2-bit max automatically
```

Both same-type and cross-type constructors clamp ranges to the destination capacity (`VECTOR_MAX_CAP`) and skip invalid intervals.

---

## 📦 Runtime operations

```cpp
packed_vector<4> vec;
vec.push_back(9);
vec.resize(5, 7);        // grows with fill value (clamped to 4-bit max)
vec.fill(2);             // set all elements
vec.pop_back();
vec.fit();               // shrink capacity to current size
```

### ⚠️ Important: Assignment Operations

**`packed_vector` does not support direct assignment via `operator[]`** because elements are bit-packed and cannot return references. This is similar to `std::vector<bool>` in the C++ standard library.

```cpp
packed_vector<4> vec;
vec.push_back(5);

// ❌ WRONG - Compilation error: "lvalue required as left operand of assignment"
vec[0] = 10;

// ✅ CORRECT - Use set() method for assignment
vec.set(0, 10);

// ✅ Reading works normally
uint8_t value = vec[0];  // Returns by value
```

**Why this limitation?**
- Elements are bit-packed across byte boundaries (e.g., a 4-bit element spans half a byte)
- There's no physical memory address for a single bit-packed element
- C++ references require an addressable memory location

**API summary for element access:**

| Operation | Syntax | Use Case |
|-----------|--------|----------|
| **Read** | `value = vec[index]` | Get element value (returns by value) |
| **Write** | `vec.set(index, value)` | Set element value |
| **Safe read** | `value = vec.at(index)` | Bounds-checked read |
| **Unsafe write** | `vec.set_unsafe(index, value)` | Set without bounds check (use carefully) |
| **Append** | `vec.push_back(value)` | Add to end |

### Iterators

```cpp
for (uint8_t v : vec) {
    // iterate like a normal vector
}

auto it = vec.begin();
*(it + 2) == vec[2];
```

Iterators implement random-access semantics (`++`, `--`, `+=`, `-=`) and expose `get_index()` for debugging.

### Dynamic bits-per-value (bpv)

```cpp
packed_vector<5> dyn(4, 31);
dyn.set_bits_per_value(3);   // clears data, switches runtime bpv to 3

// repopulate with new max value of 7
for (uint8_t i = 0; i < 4; ++i) {
    dyn.push_back(i);
}
```

- `get_bits_per_value()` returns the active runtime bpv.
- Changing bpv invalidates stored data but retains capacity.
 - `get_bits_per_value()` returns the active runtime bpv.
 - Changing bpv invalidates stored data but retains capacity.
 - Note: When using large bpv values that span multiple words, choosing a matching ValueType (e.g., `uint64_t` for bpv >= 40) is recommended to avoid implicit narrowing.

---

## 🧮 Memory footprint

- `memory_usage()` returns the number of bytes allocated by the packed storage.
- `fit()` trims capacity to match the current size (minimum of 1 slot retained).
- For `TINY` flag, size and capacity share a single byte (4 bits each).

### Word size & multi-word support

- The internal storage word is `word_t = size_t` and `WORD_BITS = sizeof(word_t) * 8`.
- On 32-bit MCUs `WORD_BITS` equals 32; on 64-bit hosts it equals 64. For widths greater than `WORD_BITS`, elements are written/read across multiple words using multi-word loops. This avoids an artificial 32-bit restriction and allows large element widths (e.g., 40/48/64 bits) while remaining portable between 32-bit and 64-bit platforms.

### Memory Footprint Analysis

The total memory cost of a `packed_vector` has two parts:

1.  **Object Size (Header)**: The space for the `packed_vector` object itself, which holds management variables. This is typically on the stack.
2.  **Heap Allocation**: The dynamically allocated memory block where the packed elements are stored.

The object size depends on the `SizeFlag` and the target architecture's pointer size. For a typical 32-bit MCU (4-byte pointers):

- `sizeof(packed_vector)` = `sizeof(pointer)` + `1 (bpv)` + `2 * sizeof(index_type)`
  - **TINY**: `4 + 1 + 1` = 6 bytes (size/capacity are packed into one byte)
  - **SMALL**: `4 + 1 + 2` = 7 bytes
  - **MEDIUM**: `4 + 1 + 4` = 9 bytes
  - **LARGE**: `4 + 1 + 8` = 13 bytes

The heap allocation is `ceil(capacity × bpv / 8)` bytes, plus any overhead from the memory allocator itself.

### Estimated Total RAM Usage

This table provides more realistic estimates for a 32-bit system, assuming the vector is fitted to its size (`capacity == size`). The final column shows the estimated size of a `std::vector<uint8_t>` holding the same number of elements, highlighting the memory savings.

| Scenario | Configuration | Elements | `bpv` | Header | Data | **`packed_vector` Total*** | `std::vector` Total |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Bit flags | `packed_vector<1>` | 64 | 1 | 9 bytes | 8 bytes | **17 bytes** | ~76 bytes |
| Tiny lookup | `packed_vector<2, TINY>` | 12 | 2 | 6 bytes | 3 bytes | **9 bytes** | ~24 bytes |
| Sensor states | `packed_vector<3>` | 120 | 3 | 9 bytes | 45 bytes | **54 bytes** | ~132 bytes |
| Quantised features | `packed_vector<5>` | 200 | 5 | 9 bytes | 125 bytes | **134 bytes** | ~212 bytes |
| Full byte fallback | `packed_vector<8>` | 128 | 8 | 9 bytes | 128 bytes | **137 bytes** | ~140 bytes |

\*_Total RAM does not include the small, system-dependent overhead from the heap allocator. `std::vector` size is estimated as `12 bytes (header) + N bytes (data)` for a 32-bit system._

Example - get memory usage:

```cpp
packed_vector<1> bits(8, 1);
packed_vector<4> nybbles(8, 15);
size_t bool_bytes   = bits.memory_usage();
size_t nibble_bytes = nybbles.memory_usage();
```

---

## 🧪 Patterns & tips

- **Element assignment**: Use `vec.set(index, value)` instead of `vec[index] = value` (the latter won't compile due to bit-packing).
- It recommends to use `reserve()` before bulk `push_back()` operations to minimize reallocations.
- Use `fit()` to reclaim unused memory.
- Use `TINY' flag for very small vectors (≤15 elements) to minimize overhead.
- Values are automatically clamped to the maximum representable value: `max_value() = (1 << bpv) - 1`.

---

## ✅ Compatibility checklist

- **Standard**: designed for C++17 (no dynamic STL allocations).
- **Platforms**: tuned for MCU environments with limited RAM/flash.
- **Dependencies**: relies only on `PackedArray`, `min_init_list`, and compile-time utilities shipped with this library.

Use `packed_vector` whenever you need dense storage for small integers—sensor bitfields, categorical data, or MCU-friendly ML feature packing—without the cost of full-width bytes per element.
