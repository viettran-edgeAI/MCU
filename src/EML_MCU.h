// #pragma once
// /**
//  * @file EML_MCU.h
//  * @brief Edge Machine Learning MCU Library - Main Header
//  * 
//  * This is the new primary header for the EML framework.
//  * It includes all core components and provides the full API.
//  * 
//  * For backward compatibility, STL_MCU.h will include this file.
//  */

// // =============================================================================
// // EML FRAMEWORK CORE
// // =============================================================================

// #include "eml/eml.h"
// #include "eml/core/eml_random.h"
// #include "eml/core/eml_debug.h"

// // =============================================================================
// // STANDARD LIBRARY INCLUDES
// // =============================================================================

// #include <stdexcept>
// #include <algorithm>
// #include <array>
// #include <cstddef>
// #include <cstdint>
// #include <cstdlib>
// #include <cstring>
// #include <limits>
// #include <new>
// #include <type_traits>
// #include <cassert>
// #include <utility>

// // =============================================================================
// // PLATFORM-SPECIFIC INCLUDES
// // =============================================================================

// #ifdef EML_PLATFORM_ESP32
//     #include <esp_heap_caps.h>
// #endif

// // =============================================================================
// // HASH KERNEL AND INITIALIZER LIST
// // =============================================================================

// #include "hash_kernel.h"
// #include "initializer_list.h"

// #define hashers best_hashers_16

// // =============================================================================
// // MCU NAMESPACE - Core Container Library
// // =============================================================================

// namespace mcu {
//     // Memory allocation helpers - automatically use PSRAM when enabled
//     namespace mem_alloc {
//         namespace detail {
//             constexpr uint8_t FLAG_PSRAM = 0x1;
//             constexpr size_t header_payload = sizeof(size_t) + sizeof(uint8_t);
//             constexpr size_t header_padding = (alignof(std::max_align_t) - (header_payload % alignof(std::max_align_t))) % alignof(std::max_align_t);

//             struct alignas(std::max_align_t) AllocationHeader {
//                 size_t count;
//                 uint8_t flags;
//                 std::array<uint8_t, header_padding> padding{};

//                 AllocationHeader(size_t c, uint8_t f) noexcept : count(c), flags(f) {}

//                 static constexpr size_t stride() noexcept { return sizeof(AllocationHeader); }
//                 bool uses_psram() const noexcept { return (flags & FLAG_PSRAM) != 0; }
//             };
//         }

//         template<typename T>
//         inline T* allocate(size_t count) {
//             const size_t recorded_count = count;
//             const size_t actual_count = (count == 0) ? 1 : count;
//             if (actual_count == 0) {
//                 return nullptr;
//             }

//             constexpr size_t stride = detail::AllocationHeader::stride();
//             const size_t alignment = std::max<size_t>(alignof(T), alignof(size_t));
//             const size_t total_bytes = stride + actual_count * sizeof(T) + alignment + sizeof(size_t);

//             uint8_t flags = 0;
//             void* raw = nullptr;

// #if defined(EML_PLATFORM_ESP32) && RF_PSRAM_AVAILABLE
//             raw = heap_caps_malloc(total_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
//             if (raw) {
//                 flags = detail::FLAG_PSRAM;
//             }
// #endif

//             if (!raw) {
//                 raw = std::malloc(total_bytes);
//                 if (!raw) {
//                     return nullptr;
//                 }
//             }

//             auto* base = static_cast<uint8_t*>(raw);
//             auto* header [[maybe_unused]] = new(raw) detail::AllocationHeader(recorded_count, flags);

//             uint8_t* data_start = base + stride;
//             uintptr_t aligned_addr = (reinterpret_cast<uintptr_t>(data_start) + alignment - 1) & ~(alignment - 1);
//             size_t offset = aligned_addr - reinterpret_cast<uintptr_t>(data_start);
//             while (offset < sizeof(size_t)) {
//                 aligned_addr += alignment;
//                 offset += alignment;
//             }

//             auto* aligned_data = reinterpret_cast<uint8_t*>(aligned_addr);
//             auto* offset_slot = reinterpret_cast<size_t*>(aligned_data) - 1;
//             *offset_slot = offset;

//             auto* data = reinterpret_cast<T*>(aligned_data);

//             if constexpr (!std::is_trivially_default_constructible_v<T>) {
//                 for (size_t i = 0; i < recorded_count; ++i) {
//                     new (data + i) T();
//                 }
//             }

//             return data;
//         }

//         template<typename T>
//         inline void deallocate(T* ptr) {
//             if (!ptr) {
//                 return;
//             }

//             constexpr size_t stride = detail::AllocationHeader::stride();
//             auto* data_bytes = reinterpret_cast<uint8_t*>(ptr);
//             size_t offset = *(reinterpret_cast<const size_t*>(data_bytes) - 1);
//             auto* raw = data_bytes - offset - stride;
//             auto* header = reinterpret_cast<detail::AllocationHeader*>(raw);

//             if constexpr (!std::is_trivially_destructible_v<T>) {
//                 for (size_t i = 0; i < header->count; ++i) {
//                     ptr[i].~T();
//                 }
//             }

//             const bool uses_psram [[maybe_unused]] = header->uses_psram();
//             header->~AllocationHeader();

// #if defined(EML_PLATFORM_ESP32) && RF_PSRAM_AVAILABLE
//             if (uses_psram) {
//                 heap_caps_free(raw);
//                 return;
//             }
// #endif

//             std::free(raw);
//         }

//         // Get allocation info for debugging
//         inline bool is_psram_ptr(const void* ptr) {
// #if defined(EML_PLATFORM_ESP32) && RF_PSRAM_AVAILABLE
//             if (!ptr) {
//                 return false;
//             }
//             constexpr size_t stride = detail::AllocationHeader::stride();
//             auto* data_bytes = reinterpret_cast<const uint8_t*>(ptr);
//             size_t offset = *(reinterpret_cast<const size_t*>(data_bytes) - 1);
//             auto* raw = data_bytes - offset - stride;
//             auto* header = reinterpret_cast<const detail::AllocationHeader*>(raw);
//             return header->uses_psram();
// #else
//             (void)ptr;
//             return false;
// #endif
//         }

//         inline size_t get_free_psram() {
// #if defined(EML_PLATFORM_ESP32) && RF_PSRAM_AVAILABLE
//             return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
// #else
//             return 0;
// #endif
//         }

//         inline size_t get_total_psram() {
// #if defined(EML_PLATFORM_ESP32) && RF_PSRAM_AVAILABLE
//             return heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
// #else
//             return 0;
// #endif
//         }
//     }

//     // pair class - included in all builds
//     template<typename T1, typename T2>
//     struct pair {
//         T1 first;
//         T2 second;

//         [[gnu::always_inline]] constexpr pair() noexcept = default;
//         [[gnu::always_inline]] constexpr pair(const T1& a, const T2& b) noexcept : first(a), second(b) {}
//         [[gnu::always_inline]] constexpr pair(T1&& a, T2&& b) noexcept : first(std::move(a)), second(std::move(b)) {}

//         template<typename U1, typename U2>
//         [[gnu::always_inline]] constexpr pair(const pair<U1, U2>& p) noexcept : first(p.first), second(p.second) {}

//         template<typename U1, typename U2>
//         [[gnu::always_inline]] constexpr pair(pair<U1, U2>&& p) noexcept : first(std::move(p.first)), second(std::move(p.second)) {}

//         [[gnu::always_inline]] constexpr pair(const pair&) noexcept = default;
//         [[gnu::always_inline]] constexpr pair(pair&&) noexcept = default;

//         [[gnu::always_inline]] constexpr pair& operator=(const pair&) noexcept = default;
//         [[gnu::always_inline]] constexpr pair& operator=(pair&&) noexcept = default;

//         template<typename U1, typename U2>
//         [[gnu::always_inline]] constexpr pair& operator=(const pair<U1, U2>& p) noexcept {
//             first = p.first;
//             second = p.second;
//             return *this;
//         }

//         template<typename U1, typename U2>
//         [[gnu::always_inline]] constexpr pair& operator=(pair<U1, U2>&& p) noexcept {
//             first = std::move(p.first);
//             second = std::move(p.second);
//             return *this;
//         }

//         [[nodiscard, gnu::pure]] constexpr bool operator==(const pair& o) const noexcept {
//             return first == o.first && second == o.second;
//         }
//         [[nodiscard, gnu::pure]] constexpr bool operator!=(const pair& o) const noexcept {
//             return !(*this == o);
//         }
//         [[nodiscard, gnu::pure]] constexpr bool operator<(const pair& o) const noexcept {
//             return first < o.first || (!(o.first < first) && second < o.second);
//         }
//         [[nodiscard, gnu::pure]] constexpr bool operator<=(const pair& o) const noexcept {
//             return !(o < *this);
//         }
//         [[nodiscard, gnu::pure]] constexpr bool operator>(const pair& o) const noexcept {
//             return o < *this;
//         }
//         [[nodiscard, gnu::pure]] constexpr bool operator>=(const pair& o) const noexcept {
//             return !(*this < o);
//         }

//         [[gnu::always_inline]] static inline constexpr pair<T1, T2> make_pair(const T1& a, const T2& b) noexcept {
//             return pair<T1, T2>(a, b);
//         }
//         [[gnu::always_inline]] static inline constexpr pair<T1, T2> make_pair(T1&& a, T2&& b) noexcept {
//             return pair<T1, T2>(std::move(a), std::move(b));
//         }
//     };

//     // Global make_pair for API compatibility
//     template<typename T1, typename T2>
//     [[gnu::always_inline]] inline constexpr pair<std::decay_t<T1>, std::decay_t<T2>> make_pair(T1&& a, T2&& b) noexcept {
//         return pair<std::decay_t<T1>, std::decay_t<T2>>(std::forward<T1>(a), std::forward<T2>(b));
//     }

// } // namespace mcu

// // =============================================================================
// // NOTE: The rest of the STL containers (vector, unordered_map, etc.) remain
// // in the original STL_MCU.h file which will include this header.
// // =============================================================================
