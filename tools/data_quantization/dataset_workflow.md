# ESP32 Dataset Processing Workflow

## Overview

The dataset processing has been separated into two distinct steps for better modularity and debugging:

1. **CSV Normalization** (`processing_data.cpp`) - Quantizes and normalizes raw datasets
2. **Binary Conversion** (`csv_to_binary.cpp`) - Converts normalized CSV to ESP32-compatible binary

## Step 1: CSV Normalization

### Purpose
- Load raw dataset CSV with headers
- Apply Z-score outlier detection and clipping
- Quantize features to 2-bit values (0,1,2,3) using quantile binning
- Generate normalized CSV without headers
- Create quantizer and parameter files for ESP32

### Usage
```bash
# Navigate to pc_side directory
cd pc_side

# Compile
g++ -std=c++17 -I../../src -o processing_data processing_data.cpp

# Run (processes dataset from data/ folder)
./processing_data
```

### Input
- `data/digit_data.csv` or `data/walker_fall.csv` - Raw dataset with headers

### Outputs (saved to data/ folder)
- `data/<dataset_name>_nml.csv` - Normalized dataset (no header, values 0-3)
- `data/<dataset_name>_qtz.bin` - Feature quantization rules for ESP32
- `data/<dataset_name>_dp.csv` - Dataset metadata for ESP32
- `data/<dataset_name>_nml.bin` - Binary dataset for ESP32

### Configuration
```cpp
static constexpr uint8_t quantization_coefficient = 2;  // 2 bits per feature
static const int MAX_NUM_FEATURES = 1023;               // Maximum features
static const int MAX_LABELS = 254;                      // Maximum labels
```

## Step 2: Binary Conversion

### Purpose
- Convert normalized CSV to ESP32 Rf_data compatible binary format
- Validate feature values are in range [0,3]
- Pack 4 features per byte using 2-bit encoding
- Generate exact ESP32 binary structure

### Usage
```bash
# Navigate to pc_side directory
cd pc_side

# Compile
g++ -std=c++11 -o convert_dataset_to_binary convert_dataset_to_binary.cpp

# Convert CSV to binary
./convert_dataset_to_binary <input.csv> <output.bin> <num_features>

# Example (files are in data/ folder)
./convert_dataset_to_binary ../data/digit_data_nml.csv ../data/digit_data_nml.bin 234
```

### Input Format
Normalized CSV without header:
```
label,feature1,feature2,...,featureN
0,1,2,3,1,0,2,3,...
1,0,3,2,1,2,0,1,...
```

### Output Format
ESP32-compatible binary:
```
Header (6 bytes):
  - numSamples: 4 bytes (uint32_t, little-endian)
  - numFeatures: 2 bytes (uint16_t, little-endian)

Each Sample (variable bytes):
  - sampleID: 2 bytes (uint16_t, little-endian)
  - label: 1 byte (uint8_t)
  - features: packed bytes (4 features per byte, 2 bits each)
```

### Binary Validation
The converter automatically:
- Validates all feature values are in range [0,3]
- Checks file size matches expected structure
- Verifies binary format by reading back samples
- Reports ESP32 compatibility status

## Complete Workflow

```bash
# Navigate to the pc_side directory
cd pc_side

# Step 1: Normalize raw dataset (outputs saved to ../data/)
./processing_data

# Step 2: Convert to binary (if using separate binary converter)
./convert_dataset_to_binary ../data/<dataset_name>_nml.csv ../data/<dataset_name>_nml.bin 234
```

## ESP32 Data Transfer Options

After processing, you have **two main approaches** to transfer data to ESP32:

### Option 1: Automatic Transfer via Serial Port ⚡
**⚠️ Important: Close all Serial Monitor windows before automatic transfer!**

#### Method 1A: Unified Transfer (Recommended)
Transfer all files in one operation:
```bash
cd pc_side
python3 unified_transfer.py <dataset_name> <serial_port>

# Example for digit_data:
python3 unified_transfer.py digit_data /dev/ttyUSB0
```
**ESP32 Side:** Upload `esp32_side/unified_receiver.ino`

#### Method 1B: Individual File Transfer
Transfer each file separately:
```bash
cd pc_side

# Transfer quantizer
python3 transfer_quantizer.py ../data/<dataset_name>_qtz.bin <serial_port>

# Transfer dataset parameters  
python3 transfer_dataset_params.py ../data/<dataset_name>_dp.csv <serial_port>

# Transfer binary dataset
python3 transfer_dataset.py ../data/<dataset_name>_nml.bin <serial_port>
```
**ESP32 Side:** Upload corresponding individual receiver sketches from `esp32_side/`

### Option 2: Manual Transfer via Serial Monitor 📝
For manual input using CSV files through Serial Monitor:

1. **Create manual_transfer folder:**
   ```bash
   mkdir manual_transfer
   cp data/<dataset_name>_qtz.bin manual_transfer/
   cp data/<dataset_name>_dp.csv manual_transfer/
   # Note: Use CSV versions for manual entry
   ```

2. **Upload appropriate ESP32 sketch:**
   - `esp32_side/ctg_serialMonitor_receiver.ino` for quantizer
   - `esp32_side/dataset_params_receiver.ino` for parameters
   - `esp32_side/csv_dataset_receiver.ino` for dataset

3. **Manual entry:** Copy/paste file contents into Serial Monitor

### Files transferred to ESP32:
- `<dataset_name>_nml.bin` or `<dataset_name>_nml.csv` (dataset)
- `<dataset_name>_qtz.bin` (quantizer)
- `<dataset_name>_dp.csv` (parameters)

## Advantages of Separation

### 1. **Modularity**
- CSV normalization can be run independently
- Binary conversion can be tested with different datasets
- Each step can be debugged separately

### 2. **Flexibility**
- Easy to convert multiple normalized CSVs to different binary formats
- Can test with subsets by modifying CSV manually
- Simple to add new validation or conversion features

### 3. **Debugging**
- Clear separation of responsibilities
- CSV output can be inspected manually
- Binary conversion logs detailed validation information

### 4. **Performance**
- Only run normalization when dataset changes
- Fast binary conversion for testing different configurations
- No need to reprocess entire pipeline for binary format changes

## Troubleshooting

### CSV Normalization Issues
```bash
# Navigate to pc_side directory
cd pc_side

# Check if input data exists
ls -la ../data/

# Run with different compiler if needed
g++ -std=c++14 -I../../src -o processing_data processing_data.cpp
```

### Binary Conversion Issues
```bash
# Check CSV format (should have no header)
head -5 ../data/<dataset_name>_nml.csv

# Verify feature count
head -1 ../data/<dataset_name>_nml.csv | tr ',' '\n' | wc -l  # Should be features + 1

# Test with smaller dataset
head -10 ../data/<dataset_name>_nml.csv > test_small.csv
./convert_dataset_to_binary test_small.csv test_small.bin 234
```

### ESP32 Compatibility
The binary files are guaranteed ESP32-compatible if:
- ✅ All feature values are in range [0,3]
- ✅ File size matches expected calculation
- ✅ Binary format verification passes
- ✅ Sample count < 10,000 (ESP32 limit)

### ESP32-C3 Transfer Issues
ESP32-C3 Super Mini may have USB-CDC stability issues with large files. If transfers fail:

**Solutions:**
1. **Use the unified transfer approach:**
   ```bash
   cd pc_side
   python3 unified_transfer.py <dataset_name> /dev/ttyACM0
   ```

2. **For persistent issues, modify unified_transfer.py:**
   - Reduce `CHUNK_SIZE` to 256 or 128 bytes
   - Increase timeouts (`ACK_TIMEOUT = 20`)
   - Add delays after each chunk (`time.sleep(0.01)`)

3. **Use traditional UART if available:**
   - Connect to TX/RX pins with USB-Serial adapter
   - Use `/dev/ttyUSB0` instead of `/dev/ttyACM0`

4. **Alternative: Use manual transfer method**
   - Create `manual_transfer/` folder with CSV files
   - Use Serial Monitor for manual data entry

**Example error patterns:**
```
❌ Failed to get ACK for a chunk.
❌ Timeout waiting for 'OK'. Got: ...wrote 768 bytes (4864 / 18687)
```

## File Structure

```
tools/data_transfer/
├── DATASET_WORKFLOW.md          # This documentation
├── data/                        # Raw datasets AND generated files
│   ├── digit_data.csv          # Input: Digit recognition dataset
│   ├── walker_fall.csv         # Input: Walker fall detection dataset
│   ├── <dataset_name>_nml.csv  # Output: Normalized CSV
│   ├── <dataset_name>_nml.bin  # Output: ESP32 binary format
│   ├── <dataset_name>_qtz.bin  # Output: Quantizer rules
│   └── <dataset_name>_dp.csv   # Output: Dataset parameters
├── manual_transfer/             # Optional: CSV files for manual entry
│   ├── <dataset_name>_qtz.bin  # Copy for manual Serial Monitor input
│   └── <dataset_name>_dp.csv   # Copy for manual Serial Monitor input
├── pc_side/                     # PC-side processing tools
│   ├── processing_data.cpp     # CSV normalization (main pipeline)
│   ├── convert_dataset_to_binary.cpp  # Binary conversion (standalone)
│   ├── unified_transfer.py     # Complete transfer utility (recommended)
│   ├── transfer_quantizer.py # Individual quantizer transfer
│   ├── transfer_dataset.py     # Individual dataset transfer
│   ├── transfer_dataset_params.py  # Individual params transfer
│   └── tranfer_bin_from_esp32.py   # Retrieve data from ESP32
└── esp32_side/                  # ESP32 receiver sketches
    ├── unified_receiver.ino     # Complete receiver (recommended)
    ├── binary_dataset_receiver.ino     # Dataset only receiver
    ├── dataset_params_receiver.ino     # Parameters only receiver
    ├── ctg_pySerial_receiver.ino       # Quantizer from Python
    ├── ctg_serialMonitor_receiver.ino  # Quantizer from Serial Monitor
    └── csv_dataset_receiver.ino        # CSV dataset receiver
```

### Generated Files (after processing):
```
data/
├── <dataset_name>_nml.csv      # Normalized CSV
├── <dataset_name>_nml.bin      # ESP32 binary format  
├── <dataset_name>_qtz.bin      # Quantizer rules
└── <dataset_name>_dp.csv       # Dataset parameters
```

### Transfer Method Summary:
| Method | Use Case | Serial Monitor | Files Needed |
|--------|----------|----------------|--------------|
| **Unified Auto** | Recommended for all transfers | ❌ Close before transfer | All 3 files automatically |
| **Individual Auto** | Debugging specific files | ❌ Close before transfer | One file at a time |
| **Manual** | No Python/serial issues | ✅ Required | CSV files only |

This separated workflow provides better control, debugging capabilities, and flexibility for your ESP32 random forest implementation.