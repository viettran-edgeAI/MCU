/**
 * @file platform.cpp
 * @brief POSIX Platform - Platform Info Implementation
 * 
 * Implements eml_platform.h interface for POSIX systems.
 */

#include "../../pal/eml_platform.h"
#include "../../pal/eml_io.h"
#include "eml_posix.h"
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
    #include <sys/sysinfo.h>
    #include <sys/utsname.h>
#elif defined(__APPLE__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
#endif

namespace eml {
namespace pal {

static char g_platform_name[128] = {0};

bool eml_platform_init() {
    // Build platform name string
#if defined(__linux__)
    struct utsname uts;
    if (uname(&uts) == 0) {
        snprintf(g_platform_name, sizeof(g_platform_name), 
                 "Linux %s (%s)", uts.release, uts.machine);
    } else {
        strcpy(g_platform_name, "Linux");
    }
#elif defined(__APPLE__)
    char version[64] = {0};
    size_t len = sizeof(version);
    if (sysctlbyname("kern.osrelease", version, &len, nullptr, 0) == 0) {
        snprintf(g_platform_name, sizeof(g_platform_name), "macOS %s", version);
    } else {
        strcpy(g_platform_name, "macOS");
    }
#else
    strcpy(g_platform_name, posix::variant_name());
#endif
    
    return true;
}

EmlPlatformInfo eml_platform_info() {
    EmlPlatformInfo info{};
    
    if (g_platform_name[0] == '\0') {
        eml_platform_init();
    }
    
    info.name = g_platform_name;
    info.variant = posix::variant_name();
    info.cpu_freq_mhz = 0;  // Not easily available on POSIX
    info.flash_size = 0;    // Not applicable
    
#if defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        info.ram_size = static_cast<uint32_t>(si.totalram * si.mem_unit);
    }
#elif defined(__APPLE__)
    int64_t total_mem = 0;
    size_t len = sizeof(total_mem);
    if (sysctlbyname("hw.memsize", &total_mem, &len, nullptr, 0) == 0) {
        info.ram_size = static_cast<uint32_t>(total_mem);
    }
#else
    info.ram_size = 0;
#endif
    
    info.external_ram_size = 0;
    
    // Build capabilities
    EmlPlatformCaps caps = EmlPlatformCaps::HAS_FPU;
    
    if (posix::is_64bit()) {
        caps = caps | EmlPlatformCaps::IS_64BIT;
    }
    
    info.capabilities = caps;
    
    return info;
}

const char* eml_platform_name() {
    if (g_platform_name[0] == '\0') {
        eml_platform_init();
    }
    return g_platform_name;
}

const char* eml_platform_root_path() {
    return EML_POSIX_ROOT_PATH;
}

size_t eml_platform_default_chunk_size() {
    return posix::default_chunk_size();
}

size_t eml_platform_rx_buffer_size() {
    return posix::default_rx_buffer_size();
}

bool eml_platform_has_capability(EmlPlatformCaps cap) {
    EmlPlatformInfo info = eml_platform_info();
    return has_cap(info.capabilities, cap);
}

[[noreturn]] void eml_platform_restart() {
    // On POSIX, we just exit
    eml_println("Platform restart requested - exiting process");
    exit(0);
}

uint64_t eml_platform_uptime_seconds() {
#if defined(__linux__)
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        return static_cast<uint64_t>(si.uptime);
    }
#elif defined(__APPLE__)
    struct timeval boottime;
    size_t len = sizeof(boottime);
    int mib[2] = { CTL_KERN, KERN_BOOTTIME };
    if (sysctl(mib, 2, &boottime, &len, nullptr, 0) == 0) {
        time_t now = time(nullptr);
        return static_cast<uint64_t>(now - boottime.tv_sec);
    }
#endif
    return 0;
}

void eml_platform_print_info() {
    EmlPlatformInfo info = eml_platform_info();
    
    eml_println("\n=== EML Platform Configuration ===");
    eml_printf("Platform: %s\n", info.name);
    eml_printf("Variant: %s\n", info.variant);
    
    if (info.ram_size > 0) {
        eml_printf("RAM: %u bytes (%.1f GB)\n", 
                  info.ram_size, 
                  static_cast<double>(info.ram_size) / (1024.0 * 1024.0 * 1024.0));
    }
    
    eml_printf("64-bit: %s\n", posix::is_64bit() ? "yes" : "no");
    eml_printf("Default chunk size: %zu bytes\n", eml_platform_default_chunk_size());
    eml_printf("Root path: %s\n", eml_platform_root_path());
    eml_println("================================\n");
}

} // namespace pal
} // namespace eml
