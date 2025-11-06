# Changelog

All notable changes to the STL_MCU library will be documented in this file.

## [Unreleased]

### Fixed
- **Critical Stack Overflow Fix**
  - Fixed stack canary watchpoint crash when loading forest directly without `build_model()`
  - Replaced 255-byte stack allocation with adaptive approach in `predict_features()`
  - Fast path: stack array for ≤32 labels (32 bytes)
  - Slow path: heap-allocated map for >32 labels
  - See `docs/Stack_Overflow_Fix.md` for technical details

### Added
- **PSRAM Support for ESP32 boards with external RAM**
  - Added `RF_USE_PSRAM` macro to enable PSRAM allocation
  - Implemented `mcu::mem_alloc` namespace with smart allocation helpers
  - Automatic PSRAM allocation with fallback to internal RAM
  - PSRAM support for `unordered_map` container
  - Utility functions: `get_total_psram()`, `get_free_psram()`, `is_psram_ptr()`
  
- **Documentation**
  - Complete PSRAM usage guide (`docs/PSRAM_Usage.md`)
  - Implementation details (`docs/PSRAM_Implementation.md`)
  - Extension guide for other containers (`docs/Extending_PSRAM_Support.md`)
  - Quick reference card (`docs/PSRAM_Quick_Reference.md`)
  
- **Examples**
  - New PSRAM usage example (`examples/PSRAM_Example/`)
  - Memory monitoring and verification code
  - Board compatibility testing

### Changed
- Updated `unordered_map` to use PSRAM-aware allocation when enabled
- Updated README.md with PSRAM feature documentation

### Performance
- Large data structures can now use PSRAM, freeing internal RAM
- Automatic memory region detection for optimal deallocation
- No performance impact when PSRAM is disabled (default behavior)

### Compatibility
- Fully backward compatible - PSRAM support is opt-in via `RF_USE_PSRAM` macro
- Works with ESP32-WROVER, ESP32-S3 with PSRAM, and compatible boards
- Graceful fallback on boards without PSRAM

---

## How to Use PSRAM

### Quick Start
```cpp
#define RF_USE_PSRAM  // Add this before includes
#include "STL_MCU.h"

void setup() {
    // Containers automatically use PSRAM when available
    mcu::unordered_map<uint16_t, uint32_t> large_map;
}
```

See [docs/PSRAM_Usage.md](docs/PSRAM_Usage.md) for detailed documentation.

---

## Previous Versions

### [1.0.0] - Initial Release
- Core STL containers for microcontrollers
- `vector`, `unordered_map`, `unordered_set`
- Memory-optimized implementations
- ESP32 support
