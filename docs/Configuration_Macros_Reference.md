# STL_MCU Configuration Macros Reference

This document provides a comprehensive guide to all configuration macros available in the STL_MCU library for running Random Forest algorithms on microcontrollers.

---

## Table of Contents

1. [Board Configuration](#board-configuration)
2. [PSRAM Configuration](#psram-configuration)
3. [Storage Configuration](#storage-configuration)
4. [Debug Configuration](#debug-configuration)
5. [Training Configuration](#training-configuration)
6. [Data Transfer Configuration](#data-transfer-configuration)
7. [MCU Platform Detection](#mcu-platform-detection)
8. [Quick Reference Table](#quick-reference-table)

---

## Board Configuration

### Board Auto-Detection
The library automatically detects your microcontroller board and configures features accordingly.

#### `RF_BOARD_SKIP_AUTODETECT`
**Purpose:** Bypass automatic board detection and provide custom configuration  
**Default:** Not defined (auto-detection enabled)  
**Usage:**
```cpp
#define RF_BOARD_SKIP_AUTODETECT
#include <STL_MCU.h>
```
**When to use:** For custom boards or when you need manual control over all board features

#### Detected Board Macros (Auto-set)
These macros are automatically defined based on detected board:
- `RF_BOARD_IS_ESP32` - ESP32 classic
- `RF_BOARD_IS_ESP32S2` - ESP32-S2
- `RF_BOARD_IS_ESP32S3` - ESP32-S3
- `RF_BOARD_IS_ESP32C3` - ESP32-C3
- `RF_BOARD_IS_ESP32C6` - ESP32-C6
- `RF_BOARD_IS_ESP32H2` - ESP32-H2
- `RF_BOARD_IS_STM32` - STM32 family
- `RF_BOARD_IS_RP2040` - Raspberry Pi Pico
- `RF_BOARD_IS_UNKNOWN` - Unrecognized board

#### Board Capability Macros (Auto-configured)
- `RF_BOARD_SUPPORTS_PSRAM` - Whether board has PSRAM capability (0 or 1)
- `RF_BOARD_SUPPORTS_SDMMC` - Whether board supports SD_MMC interface (0 or 1)
- `RF_BOARD_HAS_NATIVE_USB` - Whether board has native USB CDC (0 or 1)
- `RF_BOARD_USB_RX_BUFFER` - USB receive buffer size in bytes (256-512)
- `RF_BOARD_DEFAULT_CHUNK` - Default data chunk size for transfers (220-512)

#### Manual Override Example
```cpp
// Override auto-detected settings before including library
#define RF_BOARD_NAME "Custom ESP32"
#define RF_BOARD_SUPPORTS_PSRAM 1
#define RF_BOARD_DEFAULT_CHUNK 384
#include <STL_MCU.h>
```

---

## PSRAM Configuration

PSRAM (external SRAM) greatly expands available memory for large models and datasets.

### `RF_USE_PSRAM`
**Purpose:** Enable PSRAM for STL containers and data structures  
**Default:** Not defined (PSRAM disabled)  
**Values:** 
- Not defined or 0: Use internal DRAM only
- 1: Use PSRAM when available

**Requirements:**
- Board must support PSRAM (`RF_BOARD_SUPPORTS_PSRAM = 1`)
- PSRAM must be enabled in Arduino IDE: Tools → PSRAM → "QSPI PSRAM"

**Usage:**
```cpp
#define RF_USE_PSRAM
#include <STL_MCU.h>

void setup() {
  // Check PSRAM status
  if (RF_PSRAM_AVAILABLE) {
    Serial.printf("PSRAM available: %d bytes\n", mcu::mem_alloc::get_total_psram());
  }
}
```

**Memory Location Behavior:**
- With `RF_USE_PSRAM`: STL containers (vector, unordered_map, etc.) allocate in PSRAM first
- Without: All allocations use internal DRAM (faster but limited)

**Verification:**
```cpp
mcu::vector<int> data;
data.push_back(42);
if (data.is_data_in_psram()) {
  Serial.println("Data stored in PSRAM");
}
```

### Related Macros (Auto-set)
- `RF_PSRAM_AVAILABLE` - Runtime check: 1 if PSRAM enabled and working, 0 otherwise
- `RF_BOARD_BUILD_HAS_PSRAM` - Compile-time check: 1 if build includes PSRAM support

---

## Storage Configuration

The library supports multiple storage backends for model files and datasets.

### Storage Type Selection

#### Runtime Selection (Recommended)
```cpp
#include <Rf_file_manager.h>

void setup() {
  // LittleFS (internal flash - default)
  rf_storage_begin(RfStorageType::FLASH);
  
  // Or SD card via MMC (4-bit mode - faster)
  rf_storage_begin(RfStorageType::SD_MMC_4BIT);
  
  // Or SD card via SPI
  rf_storage_begin(RfStorageType::SD_SPI);
  
  // Or FATFS (FAT filesystem on internal flash)
  rf_storage_begin(RfStorageType::FATFS);
}
```

### Storage Backend Macros

#### `RF_HAS_SDMMC`
**Purpose:** Indicates SD_MMC (SD card over MMC interface) support  
**Auto-configured:** Based on `RF_BOARD_SUPPORTS_SDMMC`  
**Value:** 0 or 1

#### `RF_HAS_FATFS`
**Purpose:** Indicates FATFS support (FAT filesystem on internal flash)  
**Auto-configured:** 1 for all ESP32 variants, 0 otherwise  
**Value:** 0 or 1  
**Note:** FATFS uses FFat library and provides PC-compatible file system

### SD Card Pin Configuration

Override default SD card pins before including Rf_file_manager.h:

```cpp
// SPI mode SD card pins
#define SD_CS_PIN 5      // Chip Select (default: 5)
#define SD_MOSI_PIN 23   // Master Out Slave In (default: 23)
#define SD_MISO_PIN 19   // Master In Slave Out (default: 19)
#define SD_SCK_PIN 18    // Serial Clock (default: 18)

#include <Rf_file_manager.h>

void setup() {
  rf_storage_begin(RfStorageType::SD_SPI);
}
```

### SD_MMC Configuration

#### `RF_SDMMC_MOUNTPOINT`
**Purpose:** Mount point path for SD_MMC  
**Default:** "/sdcard"  
**Usage:**
```cpp
#define RF_SDMMC_MOUNTPOINT "/sd"
#include <Rf_file_manager.h>
```

#### `RF_SDMMC_FORMAT_IF_FAIL`
**Purpose:** Auto-format SD card if mount fails  
**Default:** false  
**Values:** true or false

### FATFS Configuration

#### `RF_FATFS_FORMAT_IF_FAIL`
**Purpose:** Auto-format internal flash if FATFS mount fails  
**Default:** true  
**Values:** true or false  
**Note:** FATFS formatting is faster than SD card formatting

### File Operation Macros

These macros provide unified file operations across all storage types:

```cpp
// Initialize storage (runtime selection)
RF_FS_BEGIN(RfStorageType::FLASH)

// File operations (work with any storage backend)
RF_FS_EXISTS("/model/config.json")
File f = RF_FS_OPEN("/data.csv", "r")
RF_FS_REMOVE("/old_file.bin")
RF_FS_RENAME("/old.txt", "/new.txt")
RF_FS_MKDIR("/model")
RF_FS_RMDIR("/old_folder")

// Storage info
uint64_t total = RF_TOTAL_BYTES()
uint64_t used = RF_USED_BYTES()

// Cleanup
RF_FS_END()
```

---

## Debug Configuration

### Debug Levels

#### `RF_DEBUG_LEVEL`
**Purpose:** Control verbosity of debug messages  
**Default:** 1  
**Values:**
- `0`: Silent mode - no messages except critical errors
- `1`: Forest-level messages (start, end, major events)
- `2`: Component-level messages + warnings
- `3`: All memory, timing, and detailed diagnostic info

**Usage:**
```cpp
#define RF_DEBUG_LEVEL 3  // Maximum verbosity
#include <random_forest_mcu.h>

void setup() {
  Serial.begin(115200);
  // Detailed debug output will be printed
}
```

**Output Examples:**

Level 0:
```
❌ Failed to load model: file not found
```

Level 1:
```
✅ Model loaded: digit_classifier
🌲 Training started: 20 trees
✅ Training complete: 94.2% accuracy
```

Level 2:
```
⚠️ No config file found, using auto-configuration
🔧 Setting minSplit range: 2 to 8
📊 Dataset: 5000 samples, 784 features, 10 labels
```

Level 3:
```
💾 Allocated 32KB in PSRAM for tree data
⏱️ Tree #5 trained in 1234ms
🧠 Node split: feature=42, threshold=127, gini=0.234
📈 Memory status: 245KB free, largest block: 180KB
```

### Debug Helper Macros

#### `eml_debug(level, message, value)`
Print debug message if current debug level exceeds specified level:
```cpp
eml_debug(1, "Model name: ", model_name);
eml_debug(2, "⚠️ Warning: low memory");
```

#### `eml_debug_2(level, msg1, val1, msg2, val2)`
Print two value pairs:
```cpp
eml_debug_2(1, "Samples: ", num_samples, " Features: ", num_features);
```

---

## Training Configuration

### Feature Control

#### `EML_STATIC_MODEL`
**Purpose:** Completely disable training functionality to save code space (Inference only)  
**Default:** Not defined (training enabled)  
**Effect:** If defined, training code is excluded from compilation  
**Code size savings:** ~30-40KB on ESP32

**Usage for inference-only deployment:**
```cpp
#define EML_STATIC_MODEL
#include <random_forest_mcu.h>

void setup() {
  RandomForest rf("trained_model");
  // Can only use rf.predict(), not rf.train()
}
```

**Conditional compilation:**
```cpp
#ifndef EML_STATIC_MODEL
  // Training-related code only compiled when enabled
  rf.train();
#endif
```

### Development Features

#### `EML_DEV_STAGE`
**Purpose:** Enable development and testing features  
**Default:** Not defined (disabled)  
**Effects:**
- Sets `ENABLE_TEST_DATA` to 1
- Enables test data split (train/test/validation)
- Activates additional debugging features
- May enable experimental code paths

**Usage:**
```cpp
#define EML_DEV_STAGE
#include <random_forest_mcu.h>

void setup() {
  RandomForest rf;
  rf.train(); // Will create test/validation splits
}
```

#### `ENABLE_TEST_DATA`
**Purpose:** Enable test data split during training  
**Auto-set:** 1 if `EML_DEV_STAGE` defined, 0 otherwise  
**Effect:** Allocates data for train/test/validation splits

---

## Data Transfer Configuration

- These macros optimize Serial [data_transfer tools](Data_Transfer_Configuration_Guide.md) between PC and microcontroller  
- Normaly, u don't need to change them unless you have specific requirements.

### Chunk Size Configuration

#### `USER_CHUNK_SIZE`
**Purpose:** Override default USB transfer chunk size in [data_transfer tools](Data_Transfer_Configuration_Guide.md)   
**Default:** `RF_BOARD_DEFAULT_CHUNK` (board-dependent: 220-512 bytes)  
**Range:** 64-512 bytes recommended  
**Effect:** larger chunks = faster transfers, but may cause instability on boards with small USB buffers, like ESP32-C3/C6/H2, or super mini boards.

**Usage:**
```cpp
#define USER_CHUNK_SIZE 384  // Custom chunk size
#include <STL_MCU.h>
```

**Guidelines:**
- **ESP32-C3/C6/H2 (small USB buffer):** 220-256 bytes
- **ESP32/S2/S3 (large USB buffer):** 384-512 bytes
- **USB connection issues:** Reduce to 128-192 bytes
- **Fast transfers:** Increase to 512 bytes (if stable)

#### `DEFAULT_CHUNK_SIZE`
**Purpose:** Internal default, typically equals `RF_BOARD_DEFAULT_CHUNK`  
**Note:** Use `USER_CHUNK_SIZE` to override, not this macro

### Transfer Stability

#### `RF_BOARD_CDC_WARNING`
**Purpose:** Warning flag for boards with small USB buffers  
**Auto-set:** 1 if `RF_BOARD_USB_RX_BUFFER <= 384`  
**Effect:** Reminds users to keep chunk sizes conservative

**Check at runtime:**
```cpp
void setup() {
  print_board_info(); // Shows USB buffer size and chunk recommendations
}
```

### Input Source Macros

These macros allow switching between Serial, stdin, or other input sources:

```cpp
// Default: Arduino Serial
#define RF_INPUT_AVAILABLE() Serial.available()
#define RF_INPUT_READ() Serial.read()
#define RF_INPUT_READ_LINE_UNTIL(delim) Serial.readStringUntil(delim)
#define RF_INPUT_FLUSH() Serial.flush()
```

**Custom input source example:**
```cpp
// Use Hardware Serial port 2
#define RF_INPUT_AVAILABLE() Serial2.available()
#define RF_INPUT_READ() Serial2.read()
#define RF_INPUT_READ_LINE_UNTIL(delim) Serial2.readStringUntil(delim)
#define RF_INPUT_FLUSH() Serial2.flush()

#include <STL_MCU.h>
```

---

## MCU Platform Detection

These macros help write portable code across different microcontroller platforms.

### Arduino Detection
```cpp
#ifdef ARDUINO
  // Arduino-specific code
  Serial.println("Running on Arduino");
#else
  // Native C++ code (e.g., PC testing)
  std::cout << "Running on PC" << std::endl;
#endif
```

### ESP32 Family Detection
```cpp
#if defined(ESP32) || defined(ESP_PLATFORM)
  // ESP32 platform code
  #include <esp_heap_caps.h>
#endif
```

### Specific ESP32 Variant Detection
```cpp
#ifdef ESP32S3
  // ESP32-S3 specific features
  // Has PSRAM, USB OTG, larger memory
#elif defined(ESP32C3)
  // ESP32-C3 specific features
  // RISC-V core, no PSRAM, native USB
#elif defined(ESP32)
  // Classic ESP32 features
  // Dual-core, PSRAM support, no native USB
#endif
```

### STM32 Detection
```cpp
#if defined(ARDUINO_ARCH_STM32) || defined(STM32F4) || defined(STM32F7)
  // STM32-specific code
#endif
```

### RP2040 Detection
```cpp
#ifdef ARDUINO_ARCH_RP2040
  // Raspberry Pi Pico specific code
#endif
```

---

## Quick Reference Table

| Macro | Purpose | Default | Values | File |
|-------|---------|---------|--------|------|
| `RF_USE_PSRAM` | Enable PSRAM usage | Not defined | 0, 1 | Rf_board_config.h |
| `RF_DEBUG_LEVEL` | Debug verbosity | 1 | 0-3 | Rf_file_manager.h |
| `EML_STATIC_MODEL` | Disable training | Not defined | defined/not | Rf_components.h |
| `EML_DEV_STAGE` | Development mode | Not defined | defined/not | Rf_components.h |
| `USER_CHUNK_SIZE` | USB transfer chunk | Board default | 64-512 | Rf_board_config.h |
| `SD_CS_PIN` | SD SPI chip select | 5 | Any GPIO | Rf_file_manager.h |
| `SD_MOSI_PIN` | SD SPI MOSI | 23 | Any GPIO | Rf_file_manager.h |
| `SD_MISO_PIN` | SD SPI MISO | 19 | Any GPIO | Rf_file_manager.h |
| `SD_SCK_PIN` | SD SPI clock | 18 | Any GPIO | Rf_file_manager.h |
| `RF_SDMMC_MOUNTPOINT` | SD_MMC mount path | "/sdcard" | Any path | Rf_file_manager.h |
| `RF_SDMMC_FORMAT_IF_FAIL` | Auto-format SD | false | true/false | Rf_file_manager.h |
| `RF_FATFS_FORMAT_IF_FAIL` | Auto-format FATFS | true | true/false | Rf_file_manager.h |

---

## Usage Examples

### Example 1: Production Inference-Only Deployment
```cpp
// Optimize for minimal code size and maximum speed
#define EML_STATIC_MODEL      // Exclude training code
#define RF_USE_PSRAM         // Use external RAM
#define RF_DEBUG_LEVEL 0     // Silent mode
#define USER_CHUNK_SIZE 512  // Fast transfers

#include <STL_MCU.h>
#include <random_forest_mcu.h>

void setup() {
  Serial.begin(115200);
  RF_FS_BEGIN(RfStorageType::FLASH);
  
  mcu::RandomForest rf("production_model");
  
  // Inference only
  auto result = rf.predict(input_features);
  Serial.printf("Prediction: %s\n", result.label);
}
```

### Example 2: Development with Full Diagnostics
```cpp
// Maximum debugging and testing features
#define EML_DEV_STAGE            // Enable test splits
#define RF_USE_PSRAM         // Use external RAM
#define RF_DEBUG_LEVEL 3     // Maximum verbosity
#define USER_CHUNK_SIZE 256  // Conservative for stability

#include <STL_MCU.h>
#include <random_forest_mcu.h>

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial monitor
  
  RF_FS_BEGIN(RfStorageType::SD_MMC_4BIT);
  print_board_info();  // Show board capabilities
  
  mcu::RandomForest rf("dev_model");
  rf.train();  // Will show detailed progress
}
```

### Example 3: SD Card with Custom Pins
```cpp
// Use SD card with non-standard pinout
#define SD_CS_PIN 15
#define SD_MOSI_PIN 13
#define SD_MISO_PIN 12
#define SD_SCK_PIN 14

#include <Rf_file_manager.h>
#include <random_forest_mcu.h>

void setup() {
  Serial.begin(115200);
  
  // Initialize SD card with custom pins
  rf_storage_begin(RfStorageType::SD_SPI);
  
  mcu::RandomForest rf("model_on_sd");
}
```

### Example 4: Portable Code Across Platforms
```cpp
#include <STL_MCU.h>

void setup() {
  #ifdef ARDUINO
    Serial.begin(115200);
    #define PRINT(x) Serial.println(x)
  #else
    #define PRINT(x) std::cout << x << std::endl
  #endif
  
  PRINT("Starting Random Forest...");
  
  #if RF_BOARD_SUPPORTS_PSRAM && defined(RF_USE_PSRAM)
    PRINT("PSRAM enabled");
  #else
    PRINT("Using DRAM only");
  #endif
}
```

---

## Best Practices

### 1. **Define Macros Before Including Headers**
```cpp
// ✅ Correct order
#define RF_USE_PSRAM
#define RF_DEBUG_LEVEL 2
#include <STL_MCU.h>

// ❌ Wrong order - macros won't take effect
#include <STL_MCU.h>
#define RF_USE_PSRAM
```

### 2. **Check Capabilities Before Enabling Features**
```cpp
#if RF_BOARD_SUPPORTS_PSRAM
  #define RF_USE_PSRAM  // Only enable if board supports it
#endif
```

### 3. **Use Runtime Storage Selection**
```cpp
// Prefer runtime selection over compile-time
void setup() {
  RfStorageType storage = detectBestStorage();
  rf_storage_begin(storage);
}
```

### 4. **Match Debug Level to Use Case**
- Production: `RF_DEBUG_LEVEL 0`
- Field deployment: `RF_DEBUG_LEVEL 1`
- Development: `RF_DEBUG_LEVEL 2`
- Bug hunting: `RF_DEBUG_LEVEL 3`

### 5. **Optimize Chunk Size for Your Connection**
```cpp
// Start conservative, increase if stable
#if RF_BOARD_IS_ESP32C3
  #define USER_CHUNK_SIZE 220  // Small USB buffer
#else
  #define USER_CHUNK_SIZE 384  // Larger buffer OK
#endif
```

---

## Troubleshooting

### Issue: Model won't load
**Check:**
- `RF_DEBUG_LEVEL >= 1` to see error messages
- Storage backend initialized: `RF_FS_BEGIN()`
- File paths correct (use `/model_name/file.bin` format)

### Issue: Out of memory during training
**Solutions:**
- Enable PSRAM: `#define RF_USE_PSRAM`
- Reduce model size in config


### Issue: USB data transfer fails
**Solutions:**
- Reduce chunk size: `#define USER_CHUNK_SIZE 128`
- Check USB cable quality
- Add delays between transfers
- Use `print_board_info()` to check buffer size

### Issue: Too many debug messages
**Solution:**
```cpp
#define RF_DEBUG_LEVEL 0  // Silent mode
```

### Issue: Code size too large
**Solutions:**
- Disable training: `#define EML_STATIC_MODEL`
- Reduce debug level: `#define RF_DEBUG_LEVEL 0`
- Use release build flags in Arduino IDE

---

## Platform-Specific Notes

### ESP32 Classic
- PSRAM: Optional, requires QSPI configuration
- USB: Via UART (CP2102, CH340)
- Storage: LittleFS (best), SD_MMC (4-bit), FATFS

### ESP32-S3
- PSRAM: Highly recommended (up to 8MB)
- USB: Native USB OTG
- Storage: All options available, SD_MMC 4-bit optimal

### ESP32-C3
- PSRAM: Not available
- USB: Native USB CDC
- Storage: LittleFS or SD_SPI
- Note: Limited RAM, use `EML_STATIC_MODEL` for large models

### ESP32-C6/H2
- PSRAM: Not available  
- USB: Native USB CDC
- Storage: LittleFS recommended
- Note: Small USB buffers, use `USER_CHUNK_SIZE <= 256`

### STM32
- PSRAM: Board-dependent
- USB: Native on most boards
- Storage: SD_MMC or SD_SPI

### RP2040 (Pico)
- PSRAM: Not available
- USB: Native USB CDC
- Storage: LittleFS or SD_SPI
- Note: Pico W has WiFi but limited RAM

---

## See Also

- [Data Transfer Guide](Data_Transfer_Configuration_Guide.md) - USB transfer optimization
- [PSRAM Guide](details_docs/PSRAM_Usage.md) - PSRAM configuration and usage
- [File Manager Tutorial](details_docs/File_Manager_Tutorial.md) - Storage system usage
- [Board Benchmark](benchmark/BENCHMARK_SUMMARY.md) - Performance comparison

---

*Last Updated: December 2024*  
*STL_MCU Version: 3.0+*
