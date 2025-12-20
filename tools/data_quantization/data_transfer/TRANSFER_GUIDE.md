# Data Processing Files Transfer Guide

This directory contains tools for transferring processed dataset files from PC to ESP32.

## Quick Start

### 1. Unified Transfer (Recommended)
Transfer all dataset files in one session:

```bash
cd pc_side
python3 unified_transfer.py --model_name <base_name> --port <serial_port>
```

**Examples:**
```bash
python3 unified_transfer.py --model_name digit_data --port /dev/ttyUSB0
python3 unified_transfer.py -m digit_data -p /dev/ttyUSB0
```

**Transfers:**
- `<base_name>_qtz.bin` - Quantizer binary (feature quantization, label mapping, outlier filtering)
- `<base_name>_dp.csv` - Dataset parameters  
- `<base_name>_nml.bin` - Normalized binary dataset

### 2. Individual File Transfer
Transfer files separately if needed:

```bash
# Transfer quantizer
python3 transfer_quantizer.py --model_name <model_name> --port <serial_port>

# Transfer dataset parameters
python3 transfer_dp_file.py --model_name <model_name> --port <serial_port>

# Transfer binary dataset
python3 transfer_dataset.py --model_name <model_name> --port <serial_port>
```

## ESP32 Setup

Upload the corresponding receiver sketch to your ESP32:

- **Unified receiver**: `esp32_side/unified_receiver.ino` (recommended)
- **Individual receivers**: Available in `esp32_side/` folder

## File Locations

**Generated files** (source):
```
tools/data_quantization/data/result/
├── <model_name>_qtz.bin
├── <model_name>_dp.csv
└── <model_name>_nml.bin
```

**ESP32 storage** (destination):
```
/<model_name>/
├── <model_name>_qtz.bin
├── <model_name>_dp.csv
└── <model_name>_nml.bin
```

## Requirements

- Python 3.x
- pyserial: `pip install pyserial`
- ESP32 connected via USB

## Troubleshooting

**Permission denied:**
```bash
sudo usermod -a -G dialout $USER
# Log out and back in
```

**Port not found:**
```bash
ls /dev/tty*  # Linux/Mac
```

**Transfer fails:**
- Close Arduino IDE Serial Monitor
- Check serial port is correct
- Ensure ESP32 receiver sketch is running
- Try resetting ESP32

## Notes

- Chunk size is automatically synchronized with ESP32 configuration
- Transfer speed depends on your ESP32 board variant
- Large files may take several minutes
- All transfers use CRC32 verification for data integrity
