# Pre-trained Model Transfer Guide

This directory contains tools for transferring pre-trained random forest models from PC to ESP32.

## Quick Start

```bash
cd pc_side
python3 transfer_model.py --model_name <model_name> --port <serial_port>
```

**Examples:**
```bash
python3 transfer_model.py --model_name my_model --port /dev/ttyUSB0
python3 transfer_model.py -m my_model -p /dev/ttyUSB0
```

## What Gets Transferred

Complete random forest model including:

1. `<model_name>_config.json` - Model metadata (features, classes, trees)
2. `<model_name>_forest.bin` - Unified forest binary (all trees)
3. `<model_name>_npd.bin` - Node predictor model (optional)
4. `<model_name>_nlg.csv` - Tree log (optional)

## ESP32 Setup

Upload the receiver sketch to your ESP32:
```
esp32_side/model_receiver.ino
```

## File Locations

**Generated files** (source):
```
tools/pre_train/trained_model/
├── <model_name>_config.json
├── <model_name>_forest.bin
├── <model_name>_npd.bin (optional)
└── <model_name>_nlg.csv (optional)
```

**ESP32 storage** (destination):
```
/<model_name>/
├── <model_name>_config.json
├── <model_name>_forest.bin
├── <model_name>_npd.bin
└── <model_name>_nlg.csv
```

## Requirements

- Python 3.x
- pyserial: `pip install pyserial`
- ESP32 connected via USB
- Sufficient ESP32 storage (models can be large)

## Workflow

1. Train model: Use pre_train tool to generate model files
2. Transfer model: `python3 transfer_model.py --model_name <model_name> --port <port>`
3. Load on ESP32: Use `random_forest_mcu.h` to load and run predictions

## Storage Recommendations

| Model Size | Recommended Storage |
|------------|-------------------|
| < 100KB    | LittleFS (flash) |
| 100KB - 1MB | SD card or LittleFS with large partition |
| > 1MB      | SD card required |

## Troubleshooting

**Permission denied:**
```bash
sudo usermod -a -G dialout $USER
```

**Transfer timeout:**
- Large models take time (minutes for multi-MB files)
- Don't interrupt the transfer
- Check USB cable quality

**Out of storage:**
- Check ESP32 filesystem capacity
- Use SD card for large models
- Reduce model size (fewer trees, simpler structure)

## Notes

- Chunk size is automatically synchronized with ESP32
- Transfer progress is displayed for each file
- CRC32 verification ensures data integrity
- Failed chunks are automatically retried (up to 5 times)
