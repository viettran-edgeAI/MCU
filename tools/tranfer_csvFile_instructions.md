# RfCategorizer ESP32 Library

This library enables transferring and using machine learning feature categorizers on ESP32 microcontrollers for embedded systems applications.

## Overview

The workflow consists of two stages:

### Stage 1: PC Environment (Data Processing)
- Process and normalize dataset using `processing_data.cpp`
- Generate categorizer configuration as CSV file
- Save categorizer for ESP32 transfer

### Stage 2: ESP32 Environment (Runtime)
- Receive categorizer data via Serial port
- Convert CSV to optimized binary format
- Load/release categorizer from SPIFFS to RAM as needed
- Categorize new sensor samples in real-time

## Files Structure

```
MCU/
├── processing_data.cpp      # PC-side data processing
├── RfCategorizer.h         # ESP32 library header
├── RfCategorizer.cpp       # ESP32 library implementation
├── esp32_categorizer_example.ino  # ESP32 example sketch
├── transfer_categorizer.py # PC-side transfer utility
└── README.md              # This file
```

## PC Side Usage

### 1. Process Dataset

Compile and run the data processing program:

```bash
cd MCU/
g++ -o processing_data processing_data.cpp -std=c++17
./processing_data
```

This will:
- Read `full_dataset_truncated.csv`
- Apply Z-score outlier filtering
- Generate categorized output: `walker_fall_standard.csv`
- Save categorizer configuration: `categorizer_esp32.csv`

### 2. Transfer to ESP32

**Method 1: Python Script Transfer (Recommended for PC-generated data)**
Install required Python package:
```bash
pip install pyserial
```

Transfer categorizer to ESP32:
```bash
python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyUSB0
```

On Windows:
```bash
python transfer_categorizer.py categorizer_esp32.csv COM3
```

List available serial ports:
```bash
python3 transfer_categorizer.py --list-ports
```

**Method 2: Interactive Input (For manual data entry)**
Use the Serial Monitor to directly input categorizer data on the ESP32:
1. Send `input` command to ESP32
2. Follow the interactive prompts
3. Enter CSV data line by line
4. Type `END` to finish

## ESP32 Side Usage

### 1. Library Installation

Copy the library files to your Arduino libraries folder:
```
~/Arduino/libraries/STL_MCU/
├── Rf_categorizer.h
├── Rf_categorizer.cpp
├── Rf_file_manager.h
├── Rf_file_manager.cpp
└── STL_MCU.h
```

Or place them in your project folder.

### 2. Basic Usage

```cpp
#include "Rf_categorizer.h"

Rf_categorizer categorizer;

void setup() {
    Serial.begin(115200);
    SPIFFS.begin(true);
    
    // Option 1: Receive from Serial (EOF method)
    if (categorizer.receiveFromSerial(Serial, 30000)) {
        categorizer = Rf_categorizer("/categorizer.bin");
    }
    
    // Option 2: Interactive CSV input method
    if (categorizer.receiveFromSerialMonitor(234, false)) {
        Serial.println("Categorizer data received successfully!");
    }
    
    // Option 3: Load existing categorizer
    // categorizer = Rf_categorizer("/categorizer.bin");
}

void loop() {
    // Load categorizer into RAM when needed
    if (categorizer.loadCtg()) {
        
        // Categorize sensor data
        mcu::b_vector<float> sensorData = {1.2f, 3.4f, 5.6f, 7.8f};
        auto categories = categorizer.categorizeSample(sensorData);
        
        // Use categorized data for ML inference
        for (uint16_t i = 0; i < categories.size(); i++) {
            Serial.print(categories[i]);
            Serial.print(" ");
        }
        Serial.println();
        
        // Release from RAM to save memory
        categorizer.releaseCtg();
    }
    
    delay(1000);
}
```

### 3. Memory Management

The library provides efficient memory management:

- **SPIFFS Storage**: Categorizer stored in flash memory (persistent)
- **RAM Loading**: Load only when needed with `loadCtg()`
- **Memory Release**: Free RAM with `releaseCtg()`
- **Memory Usage**: Check usage with `getMemoryUsage()`

### 4. Serial Commands (Example Sketch)

The example sketch supports these commands:

- `receive` - Receive new categorizer from Serial (EOF method)
- `input` - Interactive categorizer data input (CSV method)
- `load` - Load categorizer into RAM
- `release` - Release categorizer from RAM  
- `info` - Show categorizer information
- `test` - Test with sample data
- `categorize x1,x2,x3...` - Categorize custom sample

## API Reference

### Rf_categorizer Class

#### Constructors
```cpp
Rf_categorizer();                          // Default constructor
Rf_categorizer(const String& binFilename); // Load from binary file
```

#### Data Input Methods
```cpp
bool receiveFromSerial(HardwareSerial& serial, unsigned long timeout = 30000);
bool receiveFromSerialMonitor(bool exact_columns = 234, bool print_file = false);
bool convertToBin();
```

#### Memory Management  
```cpp
bool loadCtg();        // Load from SPIFFS to RAM
void releaseCtg();     // Release from RAM
```

#### Categorization
```cpp
uint8_t categorizeFeature(uint16_t featureIdx, float value) const;
mcu::b_vector<uint8_t> categorizeSample(const mcu::b_vector<float>& sample) const;
```

#### Utility
```cpp
bool isValid() const;
uint16_t getNumFeatures() const;
uint8_t getGroupsPerFeature() const;
String getFilename() const;
bool getIsLoaded() const;
void printInfo() const;
size_t getMemoryUsage() const;
```

## Memory Usage

Typical memory usage for different dataset sizes:

| Features | Groups | RAM Usage | SPIFFS Usage |
|----------|--------|-----------|--------------|
| 10       | 4      | ~800 bytes | ~200 bytes   |
| 30       | 4      | ~2.4 KB    | ~600 bytes   |
| 100      | 8      | ~12 KB     | ~3 KB        |

## Performance Considerations

### Optimizations for ESP32

- **Minimal STL Usage**: Uses `std::vector` only where necessary
- **Efficient Binary Format**: Compact storage format
- **On-Demand Loading**: Load/release pattern saves RAM
- **Fast Categorization**: O(1) for discrete, O(log n) for continuous features

### Memory Management Tips

1. **Load Only When Needed**: Use `loadCtg()` before inference, `releaseCtg()` after
2. **Monitor Memory**: Check `getMemoryUsage()` and `ESP.getFreeHeap()`
3. **SPIFFS Optimization**: Store in binary format for fast loading
4. **Batch Processing**: Process multiple samples while loaded

## Troubleshooting

### Common Issues

1. **SPIFFS Not Initialized**
   ```cpp
   if (!SPIFFS.begin(true)) {
       Serial.println("SPIFFS initialization failed!");
   }
   ```

2. **Serial Transfer Timeout**
   - Increase timeout value
   - Check baud rate (115200)
   - Verify CSV file format

3. **Memory Issues**
   - Check available heap: `ESP.getFreeHeap()`
   - Use `releaseCtg()` to free memory
   - Consider reducing dataset size

4. **Categorization Errors**
   - Ensure categorizer is loaded: `getIsLoaded()`
   - Verify sample size matches `getNumFeatures()`
   - Check input data range

### Debug Output

Enable debug information:
```cpp
categorizer.printInfo();
Serial.println("Free heap: " + String(ESP.getFreeHeap()));
```

## Example Workflows

### Workflow 1: PC-Generated Data Transfer

Complete workflow using Python script transfer:

```bash
# 1. PC Side - Process data
./processing_data
# Generates: categorizer_esp32.csv

# 2. Upload ESP32 sketch
# Upload esp32_categorizer_example.ino to ESP32

# 3. Transfer categorizer
python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyUSB0

# 4. ESP32 Side - Use categorizer
# Send commands via Serial Monitor:
# > load
# > test
# > categorize 1.2,3.4,5.6,7.8
# > release
# > info
```

### Workflow 2: Interactive Data Input

Complete workflow using interactive input:

```bash
# 1. Upload ESP32 sketch
# Upload esp32_categorizer_example.ino to ESP32

# 2. ESP32 Side - Interactive input
# Send commands via Serial Monitor:
# > input
# [Follow prompts to enter CSV data]
# > load
# > test
# > info
```

**Example Interactive Input Session:**
```
> input
Enter base filename (no extension), e.g. animal_data:
my_categorizer
📁 Will save to: /my_categorizer.csv
📥 Enter CSV lines (separated by space or newline). Type END to finish.
2,4
1,3,0.5,1.0,1.5
0,3,10.2,15.7,20.1
END
🔚 END received, closing file.
Enter the filename of the CSV file you just created (without extension):
my_categorizer
✅ Categorizer data successfully processed and saved!
```

## License

This library is designed for embedded systems research and development. Optimize memory usage and processing speed for your specific application requirements.
