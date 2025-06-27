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
├── esp32_categorizer_example.ino        # ESP32 example: interactive input
├── esp32_categorizer_example_2.ino      # ESP32 example: Python script transfer
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

#### Method 1: Python Script Transfer (Recommended for PC-generated data)
- **Example sketch:** `esp32_categorizer_example_2.ino`
- **Use case:** Automated transfer of large/PC-generated categorizer files
- **How to use:**
    1. Upload `esp32_categorizer_example_2.ino` to your ESP32 board.
    2. **Important:** For ESP32-C3 and similar boards, enable "CDC on Boot" in Arduino IDE (Tools > USB CDC On Boot: Enabled) to ensure USB serial works for data transfer.
    3. After upload, wait a few seconds for the board to enumerate the USB port.
    4. Run the transfer script:

    ```bash
    python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyACM0
    # or /dev/ttyUSB0 depending on your board/OS
    ```
    5. The script will handle the handshake and transfer the file automatically.

- **Troubleshooting:**
    - If you see "No data received from ESP32", ensure CDC on Boot is enabled and wait a few seconds after upload before running the script.
    - Confirm the correct serial port (use `ls /dev/ttyACM*` or `python3 transfer_categorizer.py --list-ports`).
    - Do not open the Serial Monitor during transfer.

#### Method 2: Interactive Input (Manual/Small Data)
- **Example sketch:** `esp32_categorizer_example.ino`
- **Use case:** Manual entry or small categorizer files, or when Python script is not available
- **How to use:**
    1. Upload `esp32_categorizer_example.ino` to your ESP32 board.
    2. Open the Serial Monitor in Arduino IDE (baud 115200).
    3. Follow the prompts to enter the categorizer data line by line (CSV format).
    4. Type `END` to finish input.
    5. The sketch will process and store the categorizer for use.

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

### 2. Example Sketches

#### A. Python Script Transfer Example (`esp32_categorizer_example_2.ino`)
- Designed for use with `transfer_categorizer.py`.
- Receives the categorizer file via serial, converts to binary, and tests categorization.
- Suitable for automated, robust transfer.

#### B. Interactive Input Example (`esp32_categorizer_example.ino`)
- Designed for manual entry via Serial Monitor.
- Prompts user for CSV input, processes, and tests categorization.
- Useful for quick tests or when PC-side script is not available.

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
bool receiveFromPySerial(Stream& serial, unsigned long timeout = 30000); // For Python script transfer
bool receiveFromSerialMonitor(bool print_file = false);                  // For interactive input
bool convertToBin(const String& csvFile);
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

- **Minimal STL Usage**: Uses custom containers for memory efficiency
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
   - For ESP32-C3: Enable CDC on Boot in Arduino IDE
   - Wait a few seconds after upload before running the transfer script

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

### Workflow 1: PC-Generated Data Transfer (Python Script)

- Use `esp32_categorizer_example_2.ino` on ESP32
- Use `transfer_categorizer.py` on PC
- Enable CDC on Boot for ESP32-C3
- Wait a few seconds after upload before running the script

```bash
# 1. PC Side - Process data
./processing_data
# Generates: categorizer_esp32.csv

# 2. Upload ESP32 sketch
# Upload esp32_categorizer_example_2.ino to ESP32

# 3. Transfer categorizer
python3 transfer_categorizer.py categorizer_esp32.csv /dev/ttyACM0

# 4. ESP32 Side - Use categorizer
# Categorizer is received and tested automatically
```

### Workflow 2: Interactive Data Input (Serial Monitor)

- Use `esp32_categorizer_example.ino` on ESP32
- Open Serial Monitor and follow prompts

```bash
# 1. Upload ESP32 sketch
# Upload esp32_categorizer_example.ino to ESP32

# 2. ESP32 Side - Interactive input
# Open Serial Monitor (baud 115200)
# Enter CSV data as prompted
# Type END to finish
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
