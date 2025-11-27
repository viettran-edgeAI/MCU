# HOG Configuration Transfer Guide

This directory contains tools for transferring HOG configuration files from PC to ESP32.

## Quick Start

```bash
cd pc_side
python3 transfer_hog_config.py --model_name <model_name> --port <serial_port>
```

**Examples:**
```bash
python3 transfer_hog_config.py --model_name dataset_features --port /dev/ttyUSB0
python3 transfer_hog_config.py -m dataset_features -p /dev/ttyUSB0
```

## What Gets Transferred

- `<model_name>_hogcfg.json` - HOG configuration including:
  - Cell size and block configuration
  - Number of orientation bins
  - Normalization parameters
  - Image dimensions

## ESP32 Setup

Upload the receiver sketch to your ESP32:
```
esp32_side/hog_config_receiver.ino
```

## File Locations

**Generated file** (source):
```
tools/hog_transform/
└── <model_name>_hogcfg.json
```

**ESP32 storage** (destination):
```
/<model_name>/
└── <model_name>_hogcfg.json
```

## Requirements

- Python 3.x
- pyserial: `pip install pyserial`
- ESP32 connected via USB

## Workflow

1. Generate HOG features: `./hog_processor <config> <input> <output>`
2. Transfer configuration: `python3 transfer_hog_config.py --model_name <model_name> --port <port>`
3. Use on ESP32 with `hog_transform` library functions

## Troubleshooting

**Permission denied:**
```bash
sudo usermod -a -G dialout $USER
```

**Port busy:**
- Close Arduino IDE Serial Monitor
- Check no other program is using the port

## Notes

- Chunk size is automatically synchronized with ESP32
- Configuration files are typically small (< 1KB)
- Transfer completes in seconds
