/**
 * @file memory.cpp
 * @brief POSIX Platform - Memory Implementation
 * 
 * Implements eml_memory.h interface for POSIX systems.
 */

#include "../../pal/eml_memory.h"
#include "eml_posix.h"
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
    #include <sys/sysinfo.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <sys/types.h>
    #include <sys/sysctl.h>
#endif

namespace eml {
namespace pal {

bool eml_memory_init() {
    // Nothing specific to initialize on POSIX
    return true;
}

EmlMemoryStatus eml_memory_status() {
    EmlMemoryStatus status{};
    
#if defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        status.free_heap = si.freeram * si.mem_unit;
        status.largest_block = status.free_heap;  // Approximation
        status.total_heap = si.totalram * si.mem_unit;
    }
#elif defined(__APPLE__)
    // Get total physical memory
    int64_t total_mem = 0;
    size_t len = sizeof(total_mem);
    if (sysctlbyname("hw.memsize", &total_mem, &len, nullptr, 0) == 0) {
        status.total_heap = static_cast<size_t>(total_mem);
    }
    
    // Get free memory (approximation via Mach VM stats)
    vm_size_t page_size;
    mach_port_t mach_port = mach_host_self();
    vm_statistics_data_t vm_stats;
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);
    
    if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
        host_statistics(mach_port, HOST_VM_INFO, (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
        status.free_heap = static_cast<size_t>(vm_stats.free_count) * page_size;
        status.largest_block = status.free_heap;
    }
#else
    // Fallback: report reasonable defaults
    status.free_heap = 256 * 1024 * 1024;  // Assume 256MB free
    status.largest_block = status.free_heap;
    status.total_heap = 1024 * 1024 * 1024;  // Assume 1GB total
#endif
    
    status.has_external = false;
    status.external_free = 0;
    status.external_total = 0;
    
    return status;
}

void* eml_malloc(size_t size, EmlMemoryType type) {
    (void)type;  // Memory type not relevant on POSIX
    return std::malloc(size);
}

void* eml_calloc(size_t count, size_t size, EmlMemoryType type) {
    (void)type;
    return std::calloc(count, size);
}

void* eml_realloc(void* ptr, size_t size, EmlMemoryType type) {
    (void)type;
    return std::realloc(ptr, size);
}

void eml_free(void* ptr) {
    std::free(ptr);
}

bool eml_is_external_ptr(const void* ptr) {
    (void)ptr;
    return false;  // No external memory on POSIX
}

size_t eml_free_heap() {
    EmlMemoryStatus status = eml_memory_status();
    return status.free_heap;
}

size_t eml_largest_free_block() {
    EmlMemoryStatus status = eml_memory_status();
    return status.largest_block;
}

bool eml_has_external_memory() {
    return false;
}

} // namespace pal
} // namespace eml
