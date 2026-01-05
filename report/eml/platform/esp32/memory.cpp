/**
 * @file memory.cpp
 * @brief ESP32 Platform - Memory Implementation
 * 
 * Implements eml_memory.h interface for ESP32.
 */

#include "../../pal/eml_memory.h"
#include "eml_esp32.h"
#include <esp_heap_caps.h>
#include <cstdlib>

#if EML_ESP32_PSRAM_AVAILABLE
#include <esp_psram.h>
#endif

namespace eml {
namespace pal {

bool eml_memory_init() {
    // Memory subsystem is initialized by ESP-IDF automatically
    return true;
}

EmlMemoryStatus eml_memory_status() {
    EmlMemoryStatus status{};
    
    const uint32_t internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (esp_psram_is_initialized()) {
        // Report PSRAM status
        status.free_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        status.largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        status.total_heap = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        status.has_external = true;
        status.external_free = status.free_heap;
        status.external_total = status.total_heap;
    } else {
        // PSRAM not initialized, use internal
        status.free_heap = heap_caps_get_free_size(internal_caps);
        status.largest_block = heap_caps_get_largest_free_block(internal_caps);
        status.total_heap = heap_caps_get_total_size(internal_caps);
        status.has_external = false;
        status.external_free = 0;
        status.external_total = 0;
    }
#else
    // No PSRAM, report internal only
    status.free_heap = heap_caps_get_free_size(internal_caps);
    status.largest_block = heap_caps_get_largest_free_block(internal_caps);
    status.total_heap = heap_caps_get_total_size(internal_caps);
    status.has_external = false;
    status.external_free = 0;
    status.external_total = 0;
#endif
    
    return status;
}

void* eml_malloc(size_t size, EmlMemoryType type) {
    if (size == 0) return nullptr;
    
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (type == EmlMemoryType::EXTERNAL || type == EmlMemoryType::ANY) {
        void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) return ptr;
    }
    
    if (type == EmlMemoryType::INTERNAL || type == EmlMemoryType::ANY) {
        return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return nullptr;
#else
    (void)type;
    return std::malloc(size);
#endif
}

void* eml_calloc(size_t count, size_t size, EmlMemoryType type) {
    if (count == 0 || size == 0) return nullptr;
    
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (type == EmlMemoryType::EXTERNAL || type == EmlMemoryType::ANY) {
        void* ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) return ptr;
    }
    
    if (type == EmlMemoryType::INTERNAL || type == EmlMemoryType::ANY) {
        return heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return nullptr;
#else
    (void)type;
    return std::calloc(count, size);
#endif
}

void* eml_realloc(void* ptr, size_t size, EmlMemoryType type) {
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (type == EmlMemoryType::EXTERNAL || type == EmlMemoryType::ANY) {
        void* new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (new_ptr) return new_ptr;
    }
    
    if (type == EmlMemoryType::INTERNAL || type == EmlMemoryType::ANY) {
        return heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    
    return nullptr;
#else
    (void)type;
    return std::realloc(ptr, size);
#endif
}

void eml_free(void* ptr) {
    if (!ptr) return;
    
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

bool eml_is_external_ptr(const void* ptr) {
#if EML_ESP32_PSRAM_AVAILABLE
    if (!ptr) return false;
    // Check if pointer is in PSRAM address range
    // PSRAM typically starts at 0x3F800000 for ESP32
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    return (addr >= 0x3F800000 && addr < 0x3FC00000) ||
           (addr >= 0x3C000000 && addr < 0x3E000000);  // ESP32-S3 range
#else
    (void)ptr;
    return false;
#endif
}

size_t eml_free_heap() {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
#endif
    return heap_caps_get_free_size(caps);
}

size_t eml_largest_free_block() {
    const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    if (esp_psram_is_initialized()) {
        return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
#endif
    return heap_caps_get_largest_free_block(caps);
}

bool eml_has_external_memory() {
#if EML_ESP32_PSRAM_AVAILABLE && EML_USE_PSRAM
    return esp_psram_is_initialized();
#else
    return false;
#endif
}

} // namespace pal
} // namespace eml
