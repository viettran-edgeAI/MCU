# Data Collector Tools

This directory contains tools for collecting and transferring image datasets from ESP32 devices to your PC.

## Components

### 📡 Dataset Capture Server
- **Location**: `dataset_capture_server/`
- **Purpose**: ESP32 sketch that runs a Wi-Fi web interface for capturing image datasets
- **Features**:
  - Real-time camera streaming
  - Dataset and class label configuration
  - Image capture with automatic organization
  - Camera configuration export

### 🔄 Data Transfer
- **Location**: `data_transfer/`
- **Purpose**: Transfer captured datasets from ESP32 to PC
- **Components**:
  - `esp32_side/dataset_transfer_sender.ino` - ESP32 sender sketch
  - `pc_side/dataset_receiver.py` - Python script to receive datasets

### 📁 Results
- **Location**: `result/`
- **Purpose**: Storage for transferred datasets and configuration files
- **Contents**: Dataset folders with images and camera configuration JSON files

## Quick Start

1. **Upload capture server** to ESP32:
   ```bash
   # Open dataset_capture_server.ino in Arduino IDE and upload
   ```

2. **Configure and capture** dataset via web interface (default: http://esp32-cam-ip)

3. **Transfer to PC**:
   ```bash
   cd data_transfer/pc_side
   python3 dataset_receiver.py --dataset <dataset_name> --port <serial_port>
   ```

4. **Use with HOG transform** (see `README_hog_integration.md`)

## Integration

The captured datasets are designed to work seamlessly with:
- `tools/hog_transform/` - Feature extraction pipeline
- `tools/data_quantization/` - Dataset processing and quantization

See `README_hog_integration.md` for detailed HOG workflow integration.