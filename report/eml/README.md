# EML (Edge Machine Learning) Framework Architecture

## Overview

The EML framework provides a layered architecture with a Platform Abstraction Layer (PAL) to enable running machine learning models on multiple platforms including:

- **ESP32** (Arduino IDE) - Primary target for MCU deployment
- **POSIX** (Linux, macOS, BSD) - Desktop/server testing and development
- **Windows** (planned) - Windows desktop support

## Directory Structure

```
src/
├── eml/                              # New EML Framework
│   ├── eml.h                         # Main entry header
│   │
│   ├── core/                         # Platform-agnostic core
│   │   ├── eml_config.h              # Configuration & platform detection
│   │   ├── eml_debug.h               # Debug/logging utilities
│   │   ├── eml_random.h              # Platform-agnostic RNG
│   │   ├── containers/               # STL containers (reserved)
│   │   │   └── stl_mcu/              
│   │   └── ml/                       # ML algorithms (reserved)
│   │
│   ├── pal/                          # Platform Abstraction Layer (interfaces)
│   │   ├── eml_io.h                  # I/O interface (Serial, printf, etc.)
│   │   ├── eml_fs.h                  # Filesystem interface
│   │   ├── eml_memory.h              # Memory management interface
│   │   ├── eml_time.h                # Time & random entropy interface
│   │   └── eml_platform.h            # Platform info interface
│   │
│   └── platform/                     # Platform implementations
│       ├── esp32/                    # ESP32 + Arduino
│       │   ├── eml_esp32.h           # ESP32 extensions & config
│       │   ├── fs.cpp                # Filesystem (LittleFS, SD, FATFS)
│       │   ├── io.cpp                # Serial I/O
│       │   ├── memory.cpp            # Heap/PSRAM management
│       │   ├── time.cpp              # millis/micros, esp_random
│       │   └── platform.cpp          # Platform info
│       │
│       └── posix/                    # POSIX systems
│           ├── eml_posix.h           # POSIX extensions
│           ├── fs.cpp                # Standard C file I/O
│           ├── io.cpp                # stdio
│           ├── memory.cpp            # malloc/free
│           ├── time.cpp              # chrono, /dev/urandom
│           └── platform.cpp          # System info
│
├── STL_MCU.h                         # Original header (backward compatible)
├── EML_MCU.h                         # New primary header
├── eml_compat.h                      # Compatibility layer
├── Rf_debug_compat.h                 # Debug compatibility shim
├── Rf_board_config_compat.h          # Board config compatibility shim
└── ...                               # Other existing files
```

## Platform Selection

### Automatic Detection

The framework auto-detects platforms based on compiler defines:

```cpp
// ESP32: ESP32, ESP_PLATFORM, ARDUINO_ARCH_ESP32, CONFIG_IDF_TARGET_*
// POSIX: __linux__, __APPLE__, __unix__, __FreeBSD__, etc.
// Windows: _WIN32, _WIN64, __CYGWIN__
```

### Manual Override

Define one of these **before** including any EML header:

```cpp
#define EML_PLATFORM_ESP32    // Force ESP32 platform
#define EML_PLATFORM_POSIX    // Force POSIX platform
#define EML_PLATFORM_WINDOWS  // Force Windows platform (future)
```

### Compile-Time Error

If no platform is detected or defined, you get a compile error:

```
error: "No EML platform detected or defined. Please define one of: 
EML_PLATFORM_ESP32, EML_PLATFORM_POSIX, or EML_PLATFORM_WINDOWS"
```

## Build Configurations

### Build Stage Macros

| Macro | Description |
|-------|-------------|
| `EML_STATIC_MODEL` | Inference-only mode (excludes training code) |
| `EML_DEV_STAGE` | Development mode (enables test data, validation) |
| `EML_DEBUG_LEVEL` | Debug verbosity (0-3) |
| `EML_USE_PSRAM` | Enable PSRAM on supported platforms |

### Debug Levels

| Level | Description |
|-------|-------------|
| 0 | Silent - no messages |
| 1 | Forest - major events only |
| 2 | Component - detailed + warnings |
| 3 | Verbose - all timing/memory info |

## Usage

### New Projects

```cpp
#include <eml/eml.h>  // Main EML header

void setup() {
    eml::init();  // Initialize all subsystems
    eml::print_info();  // Show platform info
    
    // Use EML APIs
    eml::pal::eml_printf("Free heap: %zu\n", eml::free_heap());
}
```

### Existing Projects (Backward Compatible)

```cpp
#include "STL_MCU.h"          // Works as before
#include "Rf_components.h"     // Works as before
#include "random_forest_mcu.h" // Works as before

// Old APIs still work:
mcu::rf_time_now(mcu::MILLISECONDS);
mcu::eml_memory_status();
rf_storage_max_dataset_bytes();
```

## PAL Interface Summary

### eml_io.h - I/O Interface
- `eml_io_init()` - Initialize serial/console
- `eml_printf()` - Formatted print
- `eml_println()` / `eml_print()` - String output
- `eml_input_available()` / `eml_input_read()` - Input handling

### eml_fs.h - Filesystem Interface
- `eml_fs_init()` - Initialize storage
- `eml_fs_open()` / `eml_fs_close()` - File operations
- `eml_fs_read()` / `eml_fs_write()` - Data I/O
- `eml_fs_exists()` / `eml_fs_remove()` - File management
- `eml_fs_max_dataset_bytes()` - Storage limits

### eml_memory.h - Memory Interface
- `eml_memory_init()` - Initialize memory subsystem
- `eml_memory_status()` - Get memory info
- `eml_malloc()` / `eml_free()` - Allocation
- `eml_has_external_memory()` - PSRAM check

### eml_time.h - Time Interface
- `eml_time_init()` - Initialize time subsystem
- `eml_millis()` / `eml_micros()` - Timestamps
- `eml_delay_ms()` / `eml_delay_us()` - Delays
- `eml_random_entropy()` - Hardware entropy
- `eml_cpu_cycles()` - Cycle counter

### eml_platform.h - Platform Interface
- `eml_platform_init()` - Initialize platform
- `eml_platform_info()` - Get platform details
- `eml_platform_name()` - Platform name string
- `eml_platform_has_capability()` - Feature check
- `eml_platform_restart()` - System reset

## Adding a New Platform

1. Create `src/eml/platform/newplatform/` directory
2. Create `eml_newplatform.h` with platform-specific config
3. Implement all PAL interfaces (io.cpp, fs.cpp, etc.)
4. Add detection in `eml_config.h`:
   ```cpp
   #ifdef NEWPLATFORM_DEFINE
       #define EML_PLATFORM_NEWPLATFORM
       #include "../platform/newplatform/eml_newplatform.h"
   #endif
   ```

## Migration Guide

### From Old RF_* APIs

| Old API | New API |
|---------|---------|
| `rf_debug_print()` | `eml::debug_print()` or `eml_debug()` |
| `rf_time_now()` | `eml::pal::eml_time_now()` |
| `eml_memory_status()` | `eml::pal::eml_memory_status()` |
| `RF_FS_*` macros | `eml::pal::eml_fs_*()` functions |
| `RF_DEBUG_LEVEL` | `EML_DEBUG_LEVEL` |
| `RF_USE_PSRAM` | `EML_USE_PSRAM` |

### File Manager Migration

Old code:
```cpp
rf_storage_begin(RfStorageType::FLASH);
File f = rf_open("/data.bin", "r");
```

New code:
```cpp
eml::pal::eml_fs_init(eml::pal::EmlStorageType::INTERNAL_FLASH);
auto* f = eml::pal::eml_fs_open("/data.bin", eml::pal::EmlFileMode::READ);
```

## ESP32-Specific Features

- PSRAM support (`EML_USE_PSRAM`)
- Multiple storage backends (LittleFS, FATFS, SD_MMC, SD_SPI)
- Hardware RNG via `esp_random()`
- Variant detection (ESP32, S2, S3, C3, C6, H2)

## POSIX-Specific Features

- Host filesystem with configurable root path
- `/dev/urandom` for entropy
- Cross-platform (Linux, macOS, BSD)
- 64-bit support

## License

See LICENSE file in root directory.
