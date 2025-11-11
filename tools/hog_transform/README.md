# HOG Feature Extraction Pipeline

## Quick Start (TL;DR)
```bash
# 1. Install dependencies (run once)
sudo apt-get update
sudo apt-get install build-essential libopencv-dev pkg-config

# Verify OpenCV4 is installed 
pkg-config --modversion opencv4  # Must show 4.x.x

# 2. Build
cd /path/to/STL_MCU/tools/hog_transform
make clean && make

# 3. Run
make run                          # Full dataset
make test                         # Quick test (3 images/class)
make help                         # Show all commands
```

**Still having issues?** → Jump to [Troubleshooting](#-troubleshooting) section

---

## Overview
This tool extracts HOG (Histogram of Oriented Gradients) features from image datasets for machine learning on microcontrollers. It provides a **config-driven unified pipeline** that processes images directly from source files to feature CSV in a single command.

### Features
- **Config-driven**: All parameters controlled via JSON configuration
- **Automated pipeline**: Single command processes entire dataset  
- **Multi-format input**: Supports PNG, JPG, JPEG, BMP, TIFF
- **Auto-format detection**: Automatically detects and processes mixed file formats
- **Image preprocessing**: Resize, grayscale conversion, normalization using OpenCV
- **Memory optimized**: Designed for embedded systems compatibility
- **Shuffling support**: Randomizes output for ML training
- **Class limitation**: Control how many images per class to process
- **ESP32 config generation**: Automatically generates `_hogcfg.json` for ESP32 deployment
- **Config file transfer**: Utility to transfer config files to ESP32 over serial (see `data_transfer/`)

### Prerequisites

#### Compiler & Build Tools
```bash
# Ubuntu/Debian - Install GCC with C++17 support
sudo apt-get update
sudo apt-get install build-essential g++ make
g++ --version  # Should be 7.0+ (preferably 10+)
```

#### OpenCV 4.x
**Critical: You MUST install OpenCV4, not OpenCV 3!**

```bash
# Ubuntu/Debian
sudo apt-get install libopencv-dev

# Verify installation (IMPORTANT!)
pkg-config --cflags --libs opencv4
# Should output paths with -I and -l flags, NOT "Package opencv4 was not found"
```

⚠️ **If you see "Package opencv4 was not found"**, you need to install OpenCV4:
```bash
# Full OpenCV4 installation from source (if apt version is old)
sudo apt-get install cmake git libgtk-3-dev pkg-config \
  libavcodec-dev libavformat-dev libswscale-dev

git clone https://github.com/opencv/opencv.git
cd opencv
git checkout 4.8.0  # or latest 4.x tag
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

#### pkg-config
```bash
# Check if pkg-config is installed
pkg-config --version

# If not installed (Ubuntu/Debian)
sudo apt-get install pkg-config
```

### Quick Start
```bash
# 1. Build the tool
cd /path/to/STL_MCU/tools/hog_transform
make

# 2. Run with default settings
make run

# 3. Or run with custom config
make run-custom CONFIG=custom_config.json
```

### Directory Structure Expected
```
your_dataset_folder/
├── class_0/
│   ├── image_001.png
│   ├── image_002.jpg
│   └── ...
├── class_1/
│   ├── image_001.png
│   └── ...
└── class_N/
    └── ...
```

### Configuration
Edit `hog_config.json`:
```json
{
  "workflow": {
    "name": "HOG Feature Extraction",
    "description": "Process images to HOG features for ML"
  },
  "input": {
    "dataset_path": "digit_dataset",
    "image_format": "auto"
  },
  "preprocessing": {
    "target_size": {"width": 32, "height": 32},
    "grayscale": true,
    "normalize": true
  },
  "hog_parameters": {
    "img_width": 32, "img_height": 32,
    "cell_size": 8, "block_size": 16,
    "block_stride": 6, "nbins": 4
  },
  "output": {
    "model_name": "dataset_features"
  },
  "processing": {
    "max_images_per_class": -1,
    "verbose": true
  },
  "esp32": {
    "input_format": "CAMERA_JPEG",
    "input_width": 320,
    "input_height": 240,
    "resize_method": "RESIZE_BILINEAR",
    "maintain_aspect_ratio": false,
    "jpeg_quality": 12
  }
}
```

### Output Files

The tool generates two files:

1. **`model_name.csv`** - Feature vectors for training:
```
class_name,feature_1,feature_2,...,feature_N
0,0.123,0.456,...,0.789
1,0.234,0.567,...,0.890
...
```

2. **`model_name_hogcfg.json`** - ESP32 configuration:
```json
{
  "workflow": {...},
  "preprocessing": {...},
  "hog_parameters": {...},
  "esp32": {...}
}
```

This configuration file is used by your ESP32 to ensure identical preprocessing parameters:
```cpp
#include "hog_transform.h"
#include "Rf_file_manager.h"

HogTransform hog;

// Load config from SD card or LittleFS
if (hog.loadConfigFromFile("/dataset_features_hogcfg.json")) {
    // Config loaded successfully
    // Extract features from camera image
    std::vector<float> features = hog.extract(image_data, width, height);
}
```

---

## Examples

### Quick Test (3 images per class)
```bash
make test
```

### Process Full Dataset
```bash
# Edit hog_config.json to set max_images_per_class: -1
make run
```

### Custom Configuration
```bash
# Create custom_config.json with your settings
make run-custom CONFIG=custom_config.json
```

---

## Transferring Config to ESP32

After generating the HOG config file, you can transfer it to your ESP32 using the included data transfer utility:

```bash
# 1. Navigate to data_transfer directory
cd data_transfer/pc_side

# 2. Install pyserial if needed
pip3 install pyserial

# 3. Transfer config to ESP32
python3 transfer_hog_config.py dataset_features /dev/ttyUSB0
```

**Prerequisites:**
- Upload `hog_config_receiver.ino` (in `data_transfer/esp32_side/`) to your ESP32 first
- Close Arduino Serial Monitor before transferring

**See [data_transfer/README.md](data_transfer/README.md) for detailed instructions.**

---

## HOG Feature Calculation
For the default config (32x32 image, 8px cells, 16px blocks, 6px stride, 4 bins):
- Blocks per row: (32-16)/6 + 1 = 3
- Blocks per column: (32-16)/6 + 1 = 3  
- Total blocks: 3 × 3 = 9
- Features per block: 4 bins × 4 cells = 16
- **Total features: 9 × 16 = 144**

---

## Troubleshooting

### 🔴 Build Errors

#### Error: "command not found: make"
```bash
# Install build tools
sudo apt-get install build-essential make
```

#### Error: "command not found: g++"
```bash
# Install compiler
sudo apt-get install build-essential
g++ --version  # Verify installation
```

#### Error: "package opencv4 was not found"
**This is the most common issue!** You have OpenCV3 or no OpenCV at all.

```bash
# Check what you have installed
pkg-config --list-all | grep -i opencv

# Solution: Install OpenCV4
sudo apt-get update
sudo apt-get install libopencv-dev

# Verify it's OpenCV4+
pkg-config --modversion opencv4
# Should show 4.x.x (not 3.x.x)
```

If apt has only OpenCV3, install from source:
```bash
# Remove old OpenCV3
sudo apt-get remove libopencv-dev

# Install build dependencies
sudo apt-get install cmake git libgtk-3-dev pkg-config \
  libavcodec-dev libavformat-dev libswscale-dev

# Build OpenCV4 from source
cd /tmp
git clone https://github.com/opencv/opencv.git
cd opencv
git checkout 4.8.0
mkdir build && cd build
cmake -D CMAKE_BUILD_TYPE=Release -D CMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install

# Update pkg-config path
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

#### Error: "undefined reference to `cv::...'"
Similar to opencv4 not found - the compiler can't link to OpenCV libraries.

```bash
# Verify OpenCV is installed correctly
pkg-config --cflags --libs opencv4
# Should show many -I and -l flags

# If empty or error, reinstall OpenCV as shown above
```

#### Error: "error: 'filesystem' file not found"
Your compiler is too old and doesn't support C++17 filesystem.

```bash
# Update compiler
sudo apt-get install g++-10
export CXX=g++-10

# Then rebuild
make clean
make
```

### 🟡 Runtime Errors

#### Error: "Cannot open config file"
```bash
# Check if config file exists and is readable
ls -la hog_config.json
file hog_config.json  # Should be "JSON data"

# Check JSON syntax (optional - for validation)
python3 -m json.tool hog_config.json > /dev/null && echo "Valid JSON"
```

#### Error: "Dataset path does not exist"
```bash
# Verify dataset_path in hog_config.json exists
ls -la digit_dataset/
ls -la digit_dataset/0/ digit_dataset/1/  # etc.

# Make sure image files exist and have correct extensions
find digit_dataset -type f | head -10
```

#### Error: "No images found" or "No valid images found"
```bash
# Check image formats - must be: png, jpg, jpeg, bmp, tiff, tif, or txt
find digit_dataset -type f -name "*.png" | wc -l
find digit_dataset -type f -name "*.jpg" | wc -l

# Verify images are readable
file digit_dataset/0/image_001.png  # Should show image type
```

#### Error: "Invalid image size" or features don't match expected count
```bash
# Check your HOG parameters in config
# Default: 32x32 → 144 features
# Formula: ((img_size - block_size) / block_stride + 1)^2 * nbins * 4

# If changing parameters, verify:
# - block_size <= img_size (e.g., 16 <= 32)
# - cell_size * 2 == block_size (standard: 8 * 2 = 16)
# - All values are positive integers
```

### 🟢 Verification Steps

#### Step 1: Verify Prerequisites
```bash
# Check compiler
g++ --version
# Expected: g++ >= 7.0 (preferably 10+)

# Check OpenCV4
pkg-config --modversion opencv4
# Expected: 4.x.x

# Check pkg-config
pkg-config --version
# Expected: any version >= 0.26

# Test make
make --version
# Expected: GNU Make 3.82+
```

#### Step 2: Manual Build Test
```bash
# If make doesn't work, try manual build
g++ -std=c++17 -I../../src -I/usr/include/opencv4 \
    hog_processor.cpp -o hog_processor \
    $(pkg-config --libs opencv4)

# If this works, the issue is with the Makefile
# If this fails, see error messages above
```

#### Step 3: Test with Minimal Config
```bash
# Create a test with just 1 image per class
cat > test_config.json << 'EOF'
{
  "workflow": {"name": "Test", "description": "Quick test"},
  "input": {
    "dataset_path": "digit_dataset",
    "image_format": "auto"
  },
  "preprocessing": {
    "target_size": {"width": 32, "height": 32},
    "grayscale": true,
    "normalize": true
  },
  "hog_parameters": {
    "img_width": 32, "img_height": 32,
    "cell_size": 8, "block_size": 16,
    "block_stride": 6, "nbins": 4
  },
  "output": {
    "model_name": "test_model",
    "shuffle_data": false
  },
  "processing": {
    "max_images_per_class": 1,
    "verbose": true
  }
}
EOF

./hog_processor test_config.json
```

#### Step 4: Run Full Pipeline
```bash
make clean
make
make run
# or
make test  # For quick test with 3 images per class
```

### Common Issues Checklist

| Issue | Check | Fix |
|-------|-------|-----|
| Build fails | `g++ --version` | `sudo apt-get install build-essential` |
| OpenCV not found | `pkg-config --modversion opencv4` | Install OpenCV4 (see above) |
| Linking errors | `pkg-config --libs opencv4` | Reinstall OpenCV4 |
| C++ error | Check g++ version >= 7 | Update compiler with `sudo apt-get install g++-10` |
| Config not found | `ls -la hog_config.json` | Ensure file exists in current directory |
| Dataset not found | `ls -la digit_dataset/` | Check dataset_path in config matches reality |
| No images processed | Check image extensions | Only .png .jpg .jpeg .bmp .tiff .tif .txt |
| Wrong feature count | Check HOG parameters | Verify formulas in config match expected output |

### Getting Help

If you still have issues, run this diagnostic:
```bash
cat > diagnose.sh << 'EOF'
#!/bin/bash
echo "=== Build Environment Diagnostic ==="
echo "1. Compiler:"
g++ --version | head -1
echo ""
echo "2. Build tools:"
make --version | head -1
echo ""
echo "3. OpenCV4 check:"
pkg-config --modversion opencv4 2>&1 || echo "ERROR: opencv4 not found"
echo ""
echo "4. OpenCV build flags:"
pkg-config --cflags --libs opencv4 2>&1 | head -1
echo ""
echo "5. Directory contents:"
ls -la | head -15
echo ""
echo "6. Dataset check:"
echo "   Classes found: $(ls -d digit_dataset/*/ 2>/dev/null | wc -l)"
echo "   Images found: $(find digit_dataset -type f 2>/dev/null | wc -l)"
EOF
chmod +x diagnose.sh
./diagnose.sh
```

---
