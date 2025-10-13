# `packed_vector`

A `packed_vector` stores small integers using only the minimum number of bits per element. It mimics a subset of `std::vector<uint8_t>` while aggressively optimizing for flash/RAM usage on MCUs.

---

## 🧠 Core idea

- Template parameters:
  - `BitsPerElement` (1–8) – compile-time bit width for each value when the vector is created.
  - `SizeFlag` – chooses index size (`TINY`, `SMALL`, `MEDIUM`, `LARGE`). Defaults to `MEDIUM` (`uint16_t`)- max 65535 elements.
- Internally uses `PackedArray` to bit-pack data with the current runtime bits-per-value (`bpv`).
- Supports runtime reconfiguration via `set_bits_per_value()` to shrink/grow element width (existing data is cleared, capacity reused).

---

## ✨ Key features

| Feature | Notes |
| --- | --- |
| Bit packing | Only `bpv` bits per element are stored; default equals `BitsPerElement`.
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
```

### Initializer-list support

Use `mcu::min_init_list<uint8_t>` or the `MAKE_UINT8_LIST` macro family:

```cpp
// Macro can prepend the target bpv for readability. The loader strips it automatically
// when it detects values that exceed the header.
packed_vector<3> from_macro = MAKE_UINT8_LIST(3, 1, 2, 3, 4, 5, 6, 7, 0);

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

---

## 🧮 Memory footprint

- `memory_usage()` returns the number of bytes allocated by the packed storage.
- `fit()` trims capacity to match the current size (minimum of 1 slot retained).
- For `TINY` flag, size and capacity share a single byte (4 bits each).

### Quick estimator

The packed storage consumes `ceil(size × bpv / 8)` bytes (plus a handful of bookkeeping bytes for size/capacity). A few practical examples:

| Scenario | Configuration | Stored elements | Bits / value (`bpv`) | Estimated bytes* |
| --- | --- | ---: | ---: | ---: |
| Bit flags | `packed_vector<1>` | 64 | 1 | 8 |
| Tiny lookup | `packed_vector<2, mcu::TINY>` | 12 | 2 | 3 |
| Sensor states | `packed_vector<3>` | 120 | 3 | 45 |
| Quantised features | `packed_vector<5>` | 200 | 5 | 125 |
| Full byte fallback | `packed_vector<8>` | 128 | 8 | 128 |

\*Estimates assume the vector has already been fitted to size; additional headroom increases `memory_usage()` proportionally.

Example comparison:

```cpp
packed_vector<1> bits(8, 1);
packed_vector<4> nybbles(8, 15);
size_t bool_bytes   = bits.memory_usage();
size_t nibble_bytes = nybbles.memory_usage();
```

---

## 🧪 Patterns & tips

- Values passed to mutating functions (`push_back`, `set`, `fill`, `resize`, etc.) are automatically masked to the current `bpv`.
- Use `assign(min_init_list)` to efficiently replace contents from raw arrays or macro lists.
- Cross-size range copies make it simple to downsample or upsample bit widths between packed vectors without manual looping.
- Combine with `index_size_flag::TINY` when you need tight storage for ≤15 elements with minimal overhead (ideal for lookup tables or bitfields).
- It recommends to use `reserve()` before bulk `push_back()` operations to minimize reallocations.
- Use `fit()` after bulk operations to reclaim unused memory.

---

## ✅ Compatibility checklist

- **Standard**: designed for C++17 (no dynamic STL allocations).
- **Platforms**: tuned for MCU environments with limited RAM/flash.
- **Dependencies**: relies only on `PackedArray`, `min_init_list`, and compile-time utilities shipped with this library.

Use `packed_vector` whenever you need dense storage for small integers—sensor bitfields, categorical data, or MCU-friendly ML feature packing—without the cost of full-width bytes per element.
