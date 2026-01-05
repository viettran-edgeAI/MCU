#pragma once
/**
 * @file eml_memory.h
 * @brief Platform Abstraction Layer - Memory Interface
 * 
 * Declares platform-agnostic memory allocation and status functions.
 * Each platform must implement these functions.
 */

#include <cstdint>
#include <cstddef>

namespace eml {
namespace pal {

/**
 * @brief Memory type enumeration
 */
enum class EmlMemoryType : uint8_t {
    INTERNAL,   // Internal SRAM/heap
    EXTERNAL,   // External memory (PSRAM, external SRAM)
    ANY         // Let system decide
};

/**
 * @brief Memory status information
 */
struct EmlMemoryStatus {
    size_t free_heap;       // Total free heap memory
    size_t largest_block;   // Largest contiguous free block
    size_t total_heap;      // Total heap size (if available)
    bool has_external;      // Whether external memory is present
    size_t external_free;   // External memory free (if applicable)
    size_t external_total;  // External memory total (if applicable)
};

/**
 * @brief Initialize the memory subsystem
 * @return true if initialization successful
 */
bool eml_memory_init();

/**
 * @brief Get current memory status
 * @return EmlMemoryStatus structure with memory information
 */
EmlMemoryStatus eml_memory_status();

/**
 * @brief Allocate memory
 * @param size Number of bytes to allocate
 * @param type Preferred memory type
 * @return Pointer to allocated memory, or nullptr on failure
 */
void* eml_malloc(size_t size, EmlMemoryType type = EmlMemoryType::ANY);

/**
 * @brief Allocate zeroed memory
 * @param count Number of elements
 * @param size Size of each element
 * @param type Preferred memory type
 * @return Pointer to allocated and zeroed memory, or nullptr on failure
 */
void* eml_calloc(size_t count, size_t size, EmlMemoryType type = EmlMemoryType::ANY);

/**
 * @brief Reallocate memory
 * @param ptr Pointer to existing allocation (or nullptr)
 * @param size New size
 * @param type Preferred memory type
 * @return Pointer to reallocated memory, or nullptr on failure
 */
void* eml_realloc(void* ptr, size_t size, EmlMemoryType type = EmlMemoryType::ANY);

/**
 * @brief Free allocated memory
 * @param ptr Pointer to free
 */
void eml_free(void* ptr);

/**
 * @brief Check if a pointer is in external memory
 * @param ptr Pointer to check
 * @return true if pointer is in external memory (PSRAM, etc.)
 */
bool eml_is_external_ptr(const void* ptr);

/**
 * @brief Get free heap size
 * @return Free heap in bytes
 */
size_t eml_free_heap();

/**
 * @brief Get largest free block size
 * @return Largest contiguous free block in bytes
 */
size_t eml_largest_free_block();

/**
 * @brief Check if external memory (PSRAM) is available and enabled
 * @return true if external memory is usable
 */
bool eml_has_external_memory();

} // namespace pal
} // namespace eml
