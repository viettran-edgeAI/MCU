# PSRAM Usage Guide

## Overview
The STL_MCU library supports automatic PSRAM (SPI RAM) allocation on ESP32 boards. When enabled, data structures will preferentially allocate memory in PSRAM, freeing up internal RAM for other critical operations.

## Enabling PSRAM

### Method 1: Define Before Include (Recommended)
```cpp
#define RF_USE_PSRAM
#include "STL_MCU.h"
#include "random_forest_mcu.h"

void setup() {
    Serial.begin(115200);
    
    // Check PSRAM availability
    if (mcu::mem_alloc::get_total_psram() > 0) {
        Serial.printf("PSRAM Available: %d bytes\n", 
                      mcu::mem_alloc::get_total_psram());
        Serial.printf("PSRAM Free: %d bytes\n", 
                      mcu::mem_alloc::get_free_psram());
    } else {
        Serial.println("PSRAM not available!");
    }
}
```

### Method 2: Compiler Flag
Add to your `platformio.ini`:
```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
build_flags = 
    -DRF_USE_PSRAM
    -DBOARD_HAS_PSRAM
```

Or in Arduino IDE, add to your build flags.

## Requirements
- ESP32 board with PSRAM (e.g., ESP32-WROVER, ESP32-S3 with PSRAM)
- PSRAM must be enabled in board configuration
- For PlatformIO: Add `-DBOARD_HAS_PSRAM` to build flags
- For Arduino IDE: Select board variant with PSRAM support

## Memory Allocation Behavior

### When RF_USE_PSRAM is Defined:
1. **Primary allocation**: Attempts to allocate in PSRAM first
2. **Fallback**: If PSRAM allocation fails, falls back to internal RAM
3. **Automatic detection**: Deallocates from the correct memory region

### When RF_USE_PSRAM is NOT Defined:
- All allocations use standard internal RAM
- No PSRAM overhead or dependencies

## Supported Data Structures
Currently PSRAM support is implemented for:
- ✅ `unordered_map_s<V, T>`
- 🔄 `unordered_set_s<T>` (coming soon)
- 🔄 `vector<T>` (coming soon)
- 🔄 `b_vector<T>` (coming soon)

## Example: Random Forest with PSRAM

```cpp
#define RF_USE_PSRAM
#include "random_forest_mcu.h"

using namespace mcu;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Print PSRAM info
    Serial.printf("Total PSRAM: %u bytes\n", mem_alloc::get_total_psram());
    Serial.printf("Free PSRAM: %u bytes\n", mem_alloc::get_free_psram());
    
    // Create forest - will use PSRAM when available
    Rf_forest forest("my_model");
    
    if (forest.loadForest()) {
        Serial.println("Forest loaded successfully");
        Serial.printf("PSRAM usage: %u bytes\n", 
                      mem_alloc::get_total_psram() - mem_alloc::get_free_psram());
    }
}

void loop() {
    // Your inference code
}
```

## Debugging PSRAM Usage

Check if a pointer is in PSRAM:
```cpp
unordered_map_s<uint8_t, uint16_t> my_map;

// Check allocation location
if (mcu::mem_alloc::is_psram_ptr(&my_map)) {
    Serial.println("Map is allocated in PSRAM");
} else {
    Serial.println("Map is allocated in internal RAM");
}
```

## Performance Considerations

### Advantages:
- Frees up internal RAM for stack and critical data
- Allows larger datasets and models
- Automatic fallback ensures compatibility

### Trade-offs:
- PSRAM access is slower than internal RAM (~40-80 MHz vs 240 MHz)
- Best for:
  - Large data structures accessed less frequently
  - Model storage (tree nodes, feature data)
  - Buffers and temporary storage

### Recommendations:
- Use PSRAM for: Large arrays, forest containers, dataset storage
- Keep in internal RAM: Frequently accessed variables, tight loops, ISR data

## Troubleshooting

### PSRAM not detected
1. Verify board has PSRAM: Check board specifications
2. Enable PSRAM in board config:
   - Arduino IDE: Select PSRAM-enabled board variant
   - PlatformIO: Add `board_build.arduino.memory_type = qio_qspi` or similar

### Compilation errors
- Ensure `esp_heap_caps.h` is available (ESP-IDF component)
- Update ESP32 Arduino Core to latest version
- Check that `RF_USE_PSRAM` is defined before including headers

### Runtime allocation failures
- Check available PSRAM with `mem_alloc::get_free_psram()`
- Monitor fragmentation
- Consider reducing dataset size or number of trees

## Future Enhancements
- [ ] PSRAM support for all STL containers
- [ ] Configurable allocation strategy (PSRAM-first vs RAM-first)
- [ ] Hybrid allocation for optimal performance
- [ ] Memory usage statistics and reporting
