# STL_MCU - Standard Library Toolkit for Microcontrollers

A memory-optimized Standard Template Library (STL) implementation specifically designed for ESP32 microcontrollers running on Arduino IDE. This library provides essential STL containers and machine learning utilities with minimal memory footprint.

## Features

### Core Containers
- **`mcu::vector`** - Dynamic array with optimized memory management
- **`mcu::unordered_map`** - Hash map for key-value storage (max 255 elements)
- **`mcu::unordered_set`** - Hash set for unique element storage
- **`mcu::ChainedUnorderedMap`** - Extended hash map for large key ranges
- **`mcu::ChainedUnorderedSet`** - Extended hash set for large element ranges
- **`mcu::Stack`** - LIFO stack implementation
- **`mcu::pair`** - Key-value pair utility

### Machine Learning Utilities
- **`mcu::HOGDescriptorMCU`** - Histogram of Oriented Gradients feature extractor
- **`mcu::Categorizer`** - Data preprocessing and categorization for ML workflows

### Memory Optimization
- Optimized for ESP32 memory constraints
- Built-in memory usage monitoring
- Container fitting and optimization functions
- SPIFFS integration for data persistence
- **PSRAM support** for ESP32 boards with external RAM (optional)

## Installation

1. Download or clone this repository
2. Copy the `STL_MCU` folder to your Arduino IDE `libraries` directory:
   - **Windows**: `Documents\Arduino\libraries\`
   - **Mac**: `Documents/Arduino/libraries/`
   - **Linux**: `~/Arduino/libraries/`
3. Restart Arduino IDE
4. The library will appear in `Sketch > Include Library > STL_MCU`

## Hardware Requirements

- **ESP32** microcontroller (any variant)
- **Arduino IDE** 1.8.0 or higher
- **ESP32 Arduino Core** 2.0.0 or higher

## Quick Start

```cpp
#include <STL_MCU.h>

void setup() {
  Serial.begin(115200);
  
  // Create a vector of integers
  mcu::vector<int> numbers;
  numbers.push_back(10);
  numbers.push_back(20);
  numbers.push_back(30);
  
  // Create a map for sensor readings
  mcu::unordered_map<uint8_t, float> sensors;
  sensors[1] = 25.6;  // Temperature sensor
  sensors[2] = 60.3;  // Humidity sensor
  
  // Print results
  Serial.print("Vector size: ");
  Serial.println(numbers.size());
  
  for(auto it = sensors.begin(); it != sensors.end(); ++it) {
    Serial.print("Sensor ");
    Serial.print(it->first);
    Serial.print(": ");
    Serial.println(it->second);
  }
}

void loop() {
  // Your main code here
}
```

## Examples

The library includes several comprehensive examples:

### 1. BasicContainers
Demonstrates basic usage of `vector`, `unordered_map`, `unordered_set`, and `pair`.

### 2. AdvancedContainers
Shows advanced container usage including `ChainedUnorderedMap`, `ChainedUnorderedSet`, and `Stack`.

### 3. HOGFeatures
Illustrates HOG feature extraction for computer vision applications.

### 4. DataCategorization
Demonstrates data preprocessing and categorization for machine learning.

## API Reference

### mcu::vector<T>
```cpp
void push_back(const T& value);
void pop_back();
T& operator[](size_t index);
size_t size() const;
size_t capacity() const;
void reserve(size_t n);
void clear();
bool empty() const;
```

### mcu::unordered_map<K, V>
```cpp
V& operator[](const K& key);
pair<iterator, bool> insert(const pair<K,V>& p);
size_t erase(const K& key);
iterator find(const K& key);
size_t size() const;
void clear();
```

### mcu::HOGDescriptorMCU
```cpp
struct Params {
  int img_width, img_height;
  int cell_size, block_size, block_stride;
  int nbins;
};

HOGDescriptorMCU(const Params& p);
void compute(const uint8_t* grayImage, mcu::vector<float>& outVec);
size_t getFeatureSize() const;
```

### mcu::Categorizer
```cpp
Categorizer(uint16_t numFeatures, uint8_t groupsPerFeature);
void updateFeatureRange(uint16_t featureIdx, float value);
uint8_t categorizeFeature(uint16_t featureIdx, float value) const;
mcu::vector<uint8_t> categorizeSample(const mcu::vector<float>& sample) const;
bool saveToBinary(const char* filename) const;
bool loadFromBinary(const char* filename);
```

## Configuration

### Storage Configuration (Runtime - Recommended)

Choose your storage backend at runtime for maximum flexibility:

```cpp
#include "Rf_file_manager.h"

// Choose one:
const RfStorageType STORAGE_MODE = RfStorageType::LITTLEFS;  // Internal flash (default)
// const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;    // Built-in SD slot (ESP32-CAM)
// const RfStorageType STORAGE_MODE = RfStorageType::SD_SPI;    // External SD module

void setup() {
    Serial.begin(115200);
    
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("❌ Storage init failed!");
        return;
    }
    
    Serial.println("✅ Storage ready!");
}
```

### PSRAM Configuration (Compile-Time)

For ESP32 boards with PSRAM (WROVER, S3), enable PSRAM **before** including headers:

```cpp
#define RF_USE_PSRAM  // Must be BEFORE includes
#include "random_forest_mcu.h"

void setup() {
    Serial.begin(115200);
    
    // Containers automatically use PSRAM when available
    mcu::vector<uint8_t> largeData(100000);  // Allocated in PSRAM
}
```

**📖 See [Configuration_Macros.md](docs/Configuration_Macros.md) for complete details on all configuration options.**

## Memory Usage Guidelines

### PSRAM Support (ESP32 with External RAM)
For ESP32 boards with PSRAM, you can enable automatic PSRAM allocation:

```cpp
#define RF_USE_PSRAM  // Enable before including headers
#include "STL_MCU.h"

void setup() {
    Serial.begin(115200);
    
    // Check PSRAM availability
    Serial.printf("Total PSRAM: %u bytes\n", mcu::mem_alloc::get_total_psram());
    Serial.printf("Free PSRAM: %u bytes\n", mcu::mem_alloc::get_free_psram());
    
    // Containers will automatically use PSRAM when available
    mcu::unordered_map<uint16_t, uint32_t> largeMap;
    // ... allocations use PSRAM first, fallback to internal RAM
}
```

**See [PSRAM_Usage.md](docs/PSRAM_Usage.md) for detailed documentation.**

### Recommendations
- Use `mcu::vector` instead of standard arrays when dynamic sizing is needed
- Prefer `mcu::unordered_map` for small datasets (< 255 elements)
- Use `ChainedUnorderedMap` for larger key ranges
- Call `fit()` on containers periodically to optimize memory usage
- Monitor memory with `ESP.getFreeHeap()` and container-specific `memory_usage()` methods

### Memory Optimization Tips
```cpp
// Reserve capacity to avoid reallocations
mcu::vector<int> data;
data.reserve(100);

// Use fit() to minimize memory usage
mcu::ChainedUnorderedMap<int> map;
// ... add data ...
map.fit();

// Check memory usage
Serial.println(map.memory_usage());
```

## File System Integration

The library supports SPIFFS for data persistence:

```cpp
#include <ESP32_HOG.h>

void setup() {
  // Initialize SPIFFS
  if(mcu::initializeSPIFFS()) {
    Serial.println("SPIFFS ready");
    
    // Save/load categorizer data
    mcu::Categorizer cat(4, 3);
    cat.saveToBinary("/config.bin");
    
    mcu::Categorizer loaded;
    loaded.loadFromBinary("/config.bin");
  }
}
```

## Performance Characteristics

| Container | Insert | Find | Erase | Memory Overhead |
|-----------|--------|------|-------|-----------------|
| vector | O(1) amortized | O(n) | O(n) | Low |
| unordered_map | O(1) average | O(1) average | O(1) average | Medium |
| unordered_set | O(1) average | O(1) average | O(1) average | Medium |
| ChainedUnorderedMap | O(1) average | O(1) average | O(1) average | Higher |
| Stack | O(1) | N/A | O(1) | Low |

## Limitations

- `unordered_map` and `unordered_set` are limited to 255 elements max
- `ChainedUnorderedMap` and `ChainedUnorderedSet` support larger datasets but use more memory
- Some advanced STL features are not implemented to maintain memory efficiency
- Designed specifically for ESP32; may not work on other Arduino platforms

## Troubleshooting

### Common Issues

**Compilation Error: "type not found"**
- Ensure you're using the `mcu::` namespace prefix
- Include the correct headers (`STL_MCU.h` or `ESP32_HOG.h`)

**Memory Issues**
- Monitor free heap with `ESP.getFreeHeap()`
- Use `memory_usage()` methods to track container memory
- Call `fit()` on containers to optimize memory usage

**SPIFFS Errors**
- Ensure SPIFFS is properly initialized with `mcu::initializeSPIFFS()`
- Check available SPIFFS space
- Verify file paths start with "/"

### Debug Tips
```cpp
// Monitor memory usage
Serial.print("Free heap: ");
Serial.println(ESP.getFreeHeap());

// Check container memory usage
Serial.print("Map memory: ");
Serial.println(myMap.memory_usage());

// Print categorizer info
myCategorizer.printInfo();
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test with the provided examples
5. Submit a pull request

## License

MIT License - see LICENSE file for details.

## Author

**Viettran** <tranvaviet@gmail.com>

---

For more examples and detailed documentation, check the `examples` folder in this library.
