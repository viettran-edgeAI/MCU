# Configuration Macros Guide

This library supports several compile-time and runtime configuration macros to customize behavior for different hardware and use cases.

## Storage Configuration

### Overview
The library supports three storage backends:
- **LittleFS** (default) - Internal flash storage (~1.5MB on ESP32)
- **SD_MMC** - Built-in SD card slot using SDIO interface (ESP32-CAM)
- **SD_SPI** - External SD card module using SPI interface

### Runtime Configuration (Recommended)

Since Arduino IDE pre-compiles libraries, **runtime configuration** is the most reliable method:

```cpp
#include "Rf_file_manager.h"

// Choose storage mode at runtime
const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;    // Built-in SD slot
// const RfStorageType STORAGE_MODE = RfStorageType::SD_SPI; // External SD module  
// const RfStorageType STORAGE_MODE = RfStorageType::FLASH; // Internal flash

void setup() {
    Serial.begin(115200);
    
    // Initialize with selected storage
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ Storage initialization failed!");
        return;
    }
    
    Serial.println("✅ Storage initialized successfully!");
    
    // Use file operations
    RF_FS_MKDIR("/data");
    File file = RF_FS_OPEN("/data/test.txt", FILE_WRITE);
    // ... rest of your code
}
```

### Compile-Time Configuration (Legacy)

For compile-time configuration, defines **must** be placed before any library includes:

```cpp
// ⚠️ IMPORTANT: Define BEFORE any includes
#define RF_USE_SDCARD    // Enable SD card (defaults to SD_MMC)
// #define RF_USE_SDSPI  // Add this for external SPI SD card

#include "random_forest_mcu.h"  // or "Rf_file_manager.h"

void setup() {
    // Initialize with compile-time default
    if (!RF_FS_BEGIN()) {
        Serial.println("Failed!");
        return;
    }
}
```

**Note**: Due to Arduino library caching, you may need to:
1. Clean build folder after changing defines
2. Or use runtime configuration instead (recommended)

### Storage Type Details

#### LittleFS (Default)
- **When to use**: Small datasets, quick prototyping, no SD card available
- **Capacity**: ~1.5MB on ESP32
- **Speed**: Fast for small files
- **Configuration**: No defines needed (default)

#### SD_MMC (Built-in SD Slot)
- **When to use**: ESP32-CAM or boards with built-in SD slot
- **Capacity**: Depends on SD card (up to 32GB tested)
- **Speed**: Very fast (4-bit SDIO bus)
- **Pins (ESP32-CAM)**:
  - CLK → GPIO14
  - CMD → GPIO15  
  - D0 → GPIO2
  - D1 → GPIO4
  - D2 → GPIO12
  - D3 → GPIO13
- **Runtime**: `RfStorageType::SD_MMC`
- **Compile-time**: `#define RF_USE_SDCARD` (without RF_USE_SDSPI)

#### SD_SPI (External SD Module)
- **When to use**: External SD card breakout board
- **Capacity**: Depends on SD card
- **Speed**: Moderate (SPI is slower than SDIO)
- **Default Pins** (customizable):
  - CS → GPIO5
  - MOSI → GPIO23
  - MISO → GPIO19
  - SCK → GPIO18
- **Runtime**: `RfStorageType::SD_SPI`
- **Compile-time**: `#define RF_USE_SDCARD` + `#define RF_USE_SDSPI`

### Custom SD Card Pins

For SPI SD cards, you can customize pins before including the header:

```cpp
// Custom SPI pins (must be defined before include)
#define SD_CS_PIN 15
#define SD_MOSI_PIN 13
#define SD_MISO_PIN 12
#define SD_SCK_PIN 14

#include "Rf_file_manager.h"
```

For SD_MMC, you can customize mount settings:

```cpp
#define RF_SDMMC_MOUNTPOINT "/sdcard"
#define RF_SDMMC_MODE_1BIT false  // false = 4-bit, true = 1-bit mode
#define RF_SDMMC_FORMAT_IF_FAIL false

#include "Rf_file_manager.h"
```

---

## PSRAM Configuration

### Overview
PSRAM (Pseudo-Static RAM) provides additional external RAM for ESP32 boards that support it (ESP32-WROVER, ESP32-S3).

### Compile-Time Only

**PSRAM configuration is compile-time only** and must be defined before including headers:

```cpp
// ⚠️ MUST be defined BEFORE includes
#define RF_USE_PSRAM

#include "STL_MCU.h"
#include "random_forest_mcu.h"

void setup() {
    // PSRAM is automatically used by containers when available
    mcu::vector<int> large_vector;  // Automatically uses PSRAM if RF_USE_PSRAM is defined
}
```

### When to Use PSRAM

**Enable PSRAM when:**
- Working with large datasets (>100KB)
- Training complex models with many trees
- Using large STL containers (vectors, maps, etc.)
- Available DRAM is insufficient

**Skip PSRAM when:**
- Small datasets (<50KB)
- Board doesn't have PSRAM
- Speed is critical (PSRAM is slightly slower than internal RAM)

### Supported Boards

PSRAM support is automatically detected for:
- ESP32-WROVER
- ESP32-WROVER-E
- ESP32-S3 (with PSRAM)
- ESP32-PICO-D4 (some variants)

### How It Works

When `RF_USE_PSRAM` is defined:
1. Library checks if board has PSRAM hardware
2. Sets `RF_PSRAM_AVAILABLE` based on detection
3. STL containers (vector, map, etc.) automatically allocate from PSRAM
4. Falls back to regular RAM if PSRAM allocation fails

### Memory Allocation

```cpp
#define RF_USE_PSRAM
#include "STL_MCU.h"

void setup() {
    // This vector will use PSRAM if available
    mcu::vector<uint8_t> data(100000);  // 100KB in PSRAM
    
    // Check allocation location (optional)
    #if RF_PSRAM_AVAILABLE
        Serial.println("✅ Using PSRAM");
    #else
        Serial.println("⚠️ Using regular RAM");
    #endif
}
```

---

## Debug Level Configuration

Control the verbosity of library debug messages:

```cpp
#define RF_DEBUG_LEVEL 2  // 0-3, define before includes

#include "random_forest_mcu.h"
```

### Debug Levels

- **0**: Silent mode (errors only)
- **1**: Forest operations (start, end, major events)
- **2**: Component-level messages + warnings
- **3**: Detailed memory and timing information

---

## Configuration Priority

**Order matters!** Always follow this pattern:

```cpp
// 1. Storage configuration (compile-time, optional)
#define RF_USE_SDCARD
// #define RF_USE_SDSPI

// 2. PSRAM configuration (compile-time, optional)
#define RF_USE_PSRAM

// 3. Debug level (compile-time, optional)
#define RF_DEBUG_LEVEL 2

// 4. Other defines
#define DEV_STAGE

// 5. THEN include library headers
#include "random_forest_mcu.h"

// 6. Runtime storage selection (recommended for storage)
void setup() {
    const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;
    RF_FS_BEGIN(STORAGE_MODE);
}
```

---

## Arduino IDE Library Caching Issue

### Problem
Arduino IDE caches compiled libraries. If you change a `#define` in your sketch, the library may not recompile.

### Solutions

1. **Use Runtime Configuration** (for storage only):
   ```cpp
   #include "Rf_file_manager.h"
   const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;
   RF_FS_BEGIN(STORAGE_MODE);
   ```

2. **Clean Build**:
   - Arduino IDE: Delete temporary build files in `C:\Users\<user>\AppData\Local\Temp\arduino_build_*`
   - PlatformIO: Run `pio run --target clean`

3. **Force Recompile**:
   - Make a trivial change to a library source file (add a space)
   - Save the file
   - Compile again

---

## Common Configurations

### ESP32-CAM with SD Card
```cpp
#include "Rf_file_manager.h"

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;

void setup() {
    Serial.begin(115200);
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ SD card init failed!");
        return;
    }
}
```

### ESP32 with External SD Module
```cpp
#include "Rf_file_manager.h"

const RfStorageType STORAGE_MODE = RfStorageType::SD_SPI;

void setup() {
    Serial.begin(115200);
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ SD card init failed!");
        return;
    }
}
```

### Large Dataset with PSRAM
```cpp
#define RF_USE_PSRAM
#include "random_forest_mcu.h"

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;

void setup() {
    Serial.begin(115200);
    RF_FS_BEGIN(STORAGE_MODE);
    
    // Train large model - uses PSRAM automatically
    RandomForest rf;
    rf.train("/large_dataset.csv");
}
```

---

## Troubleshooting

### "Still using LittleFS after defining RF_USE_SDCARD"
- **Cause**: Arduino library caching
- **Solution**: Use runtime configuration with `RfStorageType` enum

### "PSRAM warning but board has PSRAM"
- **Cause**: `RF_USE_PSRAM` defined after includes
- **Solution**: Move define BEFORE all includes

### "File operations fail on SD card"
- **Cause**: Directory doesn't exist or card not inserted
- **Solution**: Check Serial output for initialization messages, ensure card is formatted FAT32

### "Compilation errors after adding defines"
- **Cause**: Library headers included before defines
- **Solution**: Move ALL defines to the very top of your sketch

---

## See Also

- [PSRAM Usage Guide](PSRAM_Usage.md)
- [File Manager Tutorial](File_Manager_Tutorial.md)
- [README](../README.md)
