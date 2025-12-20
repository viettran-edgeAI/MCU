# Data Transfer System for random_forest_mcu Pipeline

## Overview

The complete pipeline includes data collection, processing, pre-training. Each step generates files and they need to be transferred to the microcontroller: 

![Data Transfer Flow](./imgs/pc_side.jpg)

### Key Concepts

- **Individual Transfer Tools**: Each tool folder (`data_collector/`, `data_quantization/`, `hog_transform/`, `pre_train/`) contains a `data_transfer/` subfolder with specialized scripts for transferring that tool's output files
- **Unified Transfer**: The top-level `tools/data_transfer/` folder provides a single command to transfer all necessary files for a complete model deployment
- **Function**: Transfer data from PC to ESP32, except `data_collector/` which receives data from ESP32 to PC

## Directory Structure

```
tools/
├── data_transfer/                      # Unified transfer for complete model deployment
│   ├── README.md                       # Main transfer documentation
│   ├── pc_side/
│   │   └── unifier_transfer.py        # Single script to transfer all files
│   └── esp32_side/
│       └── unifier_receiver.ino       # ESP32 receiver for unified transfer
│
├── data_collector/                     # Capture datasets from ESP32 camera
│   ├── README.md
│   ├── dataset_capture_server/        # Web interface for image capture
│   ├── result/                        # Captured datasets
│   └── data_transfer/
│       ├── pc_side/
│       │   └── dataset_receiver.py    # Receive datasets from ESP32
│       └── esp32_side/
│           └── dataset_transfer_sender.ino
│
├── data_quantization/                  # Process and quantize datasets
│   ├── README.md
│   ├── data/result/                   # Quantized outputs
│   └── data_transfer/
│       ├── README.md
│       ├── TRANSFER_GUIDE.md
│       ├── pc_side/
│       │   ├── unified_transfer.py        # Transfer quantized files
│       │   ├── transfer_quantizer.py      # Individual quantizer transfer
│       │   ├── transfer_dataset.py        # Individual dataset transfer
│       │   └── transfer_dataset_params.py # Individual params transfer
│       └── esp32_side/
│           ├── unified_receiver.ino
│           ├── binary_dataset_receiver.ino
│           ├── dataset_params_receiver.ino
│           └── manual_transfer/           # Manual Serial Monitor transfer
│
├── hog_transform/                      # Extract HOG features from images
│   ├── README.md
│   ├── result/                        # HOG features and config
│   └── data_transfer/
│       ├── README.md
│       ├── TRANSFER_GUIDE.md
│       ├── pc_side/
│       │   └── transfer_hog_config.py    # Transfer HOG configuration
│       └── esp32_side/
│           └── hog_config_receiver.ino
│
└── pre_train/                          # Pre-train Random Forest models
    ├── README.md
    ├── trained_model/                 # Trained model files
    └── data_transfer/
        ├── TRANSFER_GUIDE.md
        ├── pc_side/
        │   └── transfer_model.py         # Transfer trained models
        └── esp32_side/
            └── model_receiver.ino
```

## Tool-Specific Transfer Details

### 1. Data Collector (ESP32 → PC)

**Purpose**: Receive image datasets captured by ESP32 camera

**Direction**: ESP32 → PC (reverse direction)

**Files Transferred**: 
- Images (PNG/JPG format)
- Camera configuration JSON

**Quick Usage**:
```bash
# 1. Capture images via web interface (ESP32)
# 2. Transfer to PC:
cd tools/data_collector/data_transfer/pc_side
python3 dataset_receiver.py --dataset <dataset_name> --port /dev/ttyUSB0
```

**📖 [Detailed Documentation](../tools/data_collector/README.md)** 
---

### 2. Data Quantization Transfer (PC → ESP32)

**Purpose**: Transfer quantized datasets and parameters to ESP32

**Files Transferred**:
- `<model_name>_nml.bin` - Binary normalized dataset
- `<model_name>_qtz.bin` - Category/quantizer information
- `<model_name>_dp.csv` - Dataset parameters

**Transfer Options**:

**Option A - Unified Transfer (Recommended)**:
```bash
cd tools/data_quantization/data_transfer/pc_side
python3 unified_transfer.py <model_name> /dev/ttyUSB0
```

**Option B - Individual Files**:
```bash
cd tools/data_quantization/data_transfer/pc_side
python3 transfer_quantizer.py ../data/result/<name>_qtz.bin /dev/ttyUSB0
python3 transfer_dataset_params.py ../data/result/<name>_dp.csv /dev/ttyUSB0
python3 transfer_dataset.py ../data/result/<name>_nml.bin /dev/ttyUSB0
```

**Option C - Manual via Serial Monitor**:
- Use CSV versions with manual copy/paste
- ESP32 sketches in `manual_transfer/` folder

**📖 [Detailed Documentation](../tools/data_quantization/data_transfer/README.md)**

---

### 3. HOG Transform Transfer (PC → ESP32)

**Purpose**: Transfer HOG configuration to ESP32 for feature extraction

**Files Transferred**:
- `<model_name>_hogcfg.json` - HOG parameters and camera config

**Quick Usage**:
```bash
cd tools/hog_transform/data_transfer/pc_side
python3 transfer_hog_config.py <model_name> /dev/ttyUSB0
```

**What's Inside the Config**:
- Camera input format (GRAYSCALE, RGB565, etc.)
- Image dimensions and resize method
- HOG parameters (cell size, block size, bins, etc.)

**📖 [Detailed Documentation](../tools/hog_transform/data_transfer/README.md)**

---

### 4. Pre-trained Model Transfer (PC → ESP32)

**Purpose**: Transfer trained Random Forest model to ESP32

**Files Transferred**:
- `<model_name>_config.json` - Model configuration and metadata
- `tree_0.bin`, `tree_1.bin`, ... - Binary decision trees

**Quick Usage**:
```bash
cd tools/pre_train/data_transfer/pc_side
python3 transfer_model.py /dev/ttyUSB0
```

**Features**:
- Automatic file discovery from `trained_model/` directory
- Combined progress bar for all tree files
- Robust error recovery with retries

**📖 [Detailed Documentation](../tools/pre_train/data_transfer/TRANSFER_GUIDE.md)**

---

### 5. Unified Transfer (PC → ESP32) ⭐ **Recommended**

**Purpose**: Transfer ALL files in a single command - replaces individual transfers

**Files Transferred in One Session**:
- Dataset files (`*_nml.bin`, `*_qtz.bin`, `*_dp.csv`)
- HOG configuration (`*_hogcfg.json`)
- Model files (`*_config.json`, `*_forest.bin`, `*_npd.bin`, `*_nlg.csv`)

**Quick Usage**:
```bash
# 1. Upload receiver sketch to ESP32
#    File: tools/data_transfer/esp32_side/unifier_receiver.ino

# 2. Run unified transfer
cd tools/data_transfer/pc_side
python3 unifier_transfer.py --model_name <model_name> --port /dev/ttyUSB0

# Example:
python3 unifier_transfer.py -m digit_data -p /dev/ttyUSB0
```

**Advantages**:
- ✅ Single command for complete deployment
- ✅ Organized storage: all files under `/model_name/` on ESP32
- ✅ Automatic file discovery from multiple tool directories
- ✅ CRC verification for reliability
- ✅ Saves time compared to individual transfers

**ESP32 File Organization**:
```
/model_name/
  ├── model_name_nml.bin          # From data_quantization
  ├── model_name_qtz.bin          # From data_quantization
  ├── model_name_dp.csv           # From data_quantization
  ├── model_name_hogcfg.json      # From hog_transform
  ├── model_name_config.json      # From pre_train
  ├── model_name_forest.bin       # From pre_train
  ├── model_name_npd.bin          # From pre_train (optional)
  └── model_name_nlg.csv          # From pre_train (optional)
```

**📖 [Detailed Documentation](../tools/data_transfer/README.md)**

---

## Transfer Protocol Details

All PC-to-ESP32 transfers use a common reliable protocol:

### Protocol Features
- **Chunked Transfer**: Large files split into manageable chunks
- **CRC32 Verification**: Each chunk verified for integrity
- **ACK/NACK System**: ESP32 acknowledges or requests retry for each chunk
- **Automatic Retry**: Failed chunks retried up to 5 times
- **Session Management**: START_SESSION → FILE_INFO → FILE_CHUNK → END_SESSION

### Transfer Commands
```
START_SESSION <basename>     # Initialize transfer session
FILE_INFO <name> <size>      # Announce file details
FILE_CHUNK <offset> <len> <crc> <data>  # Send data chunk
END_SESSION                  # Finalize transfer
```

### Chunk Size Configuration
Chunk size is automatically parsed from `src/Rf_board_config.h`:
```cpp
#define USER_CHUNK_SIZE 220  // Default: 220 bytes per chunk
```

**Note**: ESP32-C3 USB-CDC may require smaller chunks (128-256 bytes) for stability

---

## Quick Start Guide

### For Complete Model Deployment (Recommended)

```bash
# 1. Prepare all files using the pipeline tools
cd tools/data_quantization
./quantize_dataset.sh -p data/digit_data.csv

cd ../hog_transform
make run

cd ../pre_train
./pre_train -training

# 2. Upload unified receiver to ESP32
# Open: tools/data_transfer/esp32_side/unifier_receiver.ino
# Upload via Arduino IDE

# 3. Transfer everything at once
cd ../data_transfer/pc_side
python3 unifier_transfer.py -m digit_data -p /dev/ttyUSB0
```

### For Individual Component Transfer

```bash
# Dataset only
cd tools/data_quantization/data_transfer/pc_side
python3 unified_transfer.py digit_data /dev/ttyUSB0

# HOG config only
cd tools/hog_transform/data_transfer/pc_side
python3 transfer_hog_config.py digit_data /dev/ttyUSB0

# Model only
cd tools/pre_train/data_transfer/pc_side
python3 transfer_model.py /dev/ttyUSB0
```

---

## Troubleshooting

### Common Issues

**1. No response from ESP32**
- Verify receiver sketch is uploaded
- Check serial port: `ls /dev/tty*` (Linux) or Device Manager (Windows)
- Close Arduino Serial Monitor before transfer

**2. Transfer stalls or CRC errors**
- Try different USB cable
- Reduce `USER_CHUNK_SIZE` in `Rf_board_config.h`
- Allow script to retry (default: 5 attempts)

**3. Missing files**
- Ensure model exists in expected directories:
  - `data_quantization/data/result/`
  - `hog_transform/result/`
  - `pre_train/trained_model/`

**4. Permission denied (Linux)**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

**5. ESP32-C3 USB-CDC issues**
- Reduce chunk size to 128 or 256 bytes
- Increase ACK timeout in transfer scripts
- Consider using traditional UART (USB-Serial adapter)

**6. Chunk size warning**
- Ensure `src/Rf_board_config.h` exists
- Script defaults to 220 bytes if not found
- Verify `USER_CHUNK_SIZE` macro is defined

---

## Best Practices

### 1. Use Unified Transfer When Possible
- Saves time and reduces errors
- Ensures consistent file organization on ESP32
- Easier to manage multiple models

### 2. Always Close Serial Monitor
- Arduino Serial Monitor locks the serial port
- Close it before running any transfer script

### 3. Verify File Organization
```cpp
// On ESP32, load files by model name:
FileManager fm;
fm.loadDataset("/digit_data/digit_data_nml.bin");
fm.loadQuantizer("/digit_data/digit_data_qtz.bin");
hog.loadConfig("/digit_data/digit_data_hogcfg.json");
rf.loadModel("/digit_data/digit_data_config.json");
```

### 4. Use Version Control for Configs
- Keep `*_config.json` and `*_hogcfg.json` in version control
- Document changes to hyperparameters
- Track which configs work best for each model

### 5. Test Incrementally
- Test each transfer individually before using unified transfer
- Verify ESP32 file system after transfer
- Check file sizes and CRC checksums

---

## File Size Reference

Typical file sizes for a digit recognition model:

| File Type | Size Range | Notes |
|-----------|------------|-------|
| `*_nml.bin` | 10-500 KB | Depends on dataset size |
| `*_qtz.bin` | 1-5 KB | Quantizer parameters |
| `*_dp.csv` | < 1 KB | Dataset metadata |
| `*_hogcfg.json` | < 1 KB | HOG configuration |
| `*_config.json` | < 5 KB | Model metadata |
| `*_forest.bin` | 10-200 KB | Depends on num_trees and depth |
| `tree_*.bin` | 1-20 KB each | Individual tree files |

**Total typical size**: 50-700 KB per model

---

## Additional Resources

- **Main Library Documentation**: [STL_MCU.md](STL_MCU.md)
- **Configuration Guide**: [Configuration_Macros_Reference.md](Configuration_Macros_Reference.md)
- **File Manager Tutorial**: [details_docs/File_Manager_Tutorial.md](details_docs/File_Manager_Tutorial.md)
- **PSRAM Usage**: [details_docs/PSRAM_Usage.md](details_docs/PSRAM_Usage.md)

---

## Summary

The STL_MCU data transfer system provides flexible options for moving files between PC and ESP32:

- 🎯 **Unified Transfer**: Single command for complete model deployment (recommended)
- 🔧 **Individual Transfers**: Tool-specific scripts for granular control
- 🔄 **Bidirectional**: Collect data from ESP32, deploy models to ESP32
- ✅ **Reliable**: CRC verification and automatic retry mechanisms
- 📁 **Organized**: Model-specific folders on ESP32 for easy management


**Note:** This tools only tested on ESP32 boards. For other microcontrollers, modifications may be needed.
