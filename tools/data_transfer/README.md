# Unified Data Transfer for ESP32

This directory now hosts the single unified workflow for moving every model asset from the PC into `/model_name/` on the ESP32. The PC script pulls datasets, the optional HOG config, and the trained model files before streaming them into a model-specific folder; the ESP32 sketch receives the chunks, verifies their CRCs, and keeps files together for future use.

## Quick workflow

1. Upload `esp32_side/unifier_receiver.ino` to the ESP32 (Arduino IDE or CLI).
2. Run the transfer command from `tools/data_transfer/pc_side`:

```bash
python3 unifier_transfer.py --model_name <model_name> --port <serial_port>
```

Examples:

```bash
python3 unifier_transfer.py --model_name digit_data --port /dev/ttyUSB0
python3 unifier_transfer.py -m digit_data -p /dev/ttyUSB0
python3 unifier_transfer.py --model_name gesture --port COM3
```

The script summarizes which files it found, streams each file via chunks with CRC checks, and finishes the session once the ESP32 replies with `OK`.

## Files aspirationally transferred

| Source directory | Pattern | Role |
|------------------|---------|------|
| `data_quantization/data/result/` | `{model_name}_ctg.csv`, `{model_name}_dp.csv`, `{model_name}_nml.bin` | Category labels, descriptor payload, and normalized dataset for testing/retraining |
| `hog_transform/result/` (or repo root) | `{model_name}_hogcfg.json` | HOG configuration, included when present |
| `pre_train/trained_model/` | `{model_name}_config.json`, `_forest.bin`, `_npd.bin`, `_nlg.csv` | Model configuration and artifacts (config + forest required, others optional) |

Every transferred file lands under `/model_name/` on the ESP32, enabling multiple models to coexist:

```
/digit_data/
  ├── digit_data_nml.bin
  ├── digit_data_hogcfg.json
  ├── digit_data_config.json
  ├── digit_data_forest.bin
  ├── digit_data_npd.bin
  └── digit_data_nlg.csv
```

## Protocol and reliability

- The Python sender sequences `START_SESSION`, `FILE_INFO`, repeated `FILE_CHUNK`s, and `END_SESSION` commands. Each chunk includes offset, length, CRC32, and payload.
- The ESP32 acknowledges each chunk (`ACK offset`) or requests a retry (`NACK offset`).
- `pc_side/config_parser.py` pulls `USER_CHUNK_SIZE` from `src/Rf_board_config.h` so PC and ESP32 use identical chunk sizes.
- Full-file CRC32 validation guarantees the transferred file matches the PC side before it’s saved.

## Troubleshooting

- **No response from ESP32**: Verify the receiver sketch is uploaded and you’re using the correct serial port (`ls /dev/tty*` or Device Manager).
- **Transfer stalls or CRC errors**: Try a different USB cable, reduce `USER_CHUNK_SIZE` in `Rf_board_config.h`, and allow the script to retry (default 5 attempts per chunk).
- **Missing files**: Ensure the requested model exists under `data_quantization/data/result/`, `hog_transform/result/`, and `pre_train/trained_model/`.
- **Permission denied on Linux**: add your user to the `dialout` group (`sudo usermod -a -G dialout $USER`).
- **Chunk size warning**: If the script warns it cannot locate `Rf_board_config.h`, it will default to 220 bytes per chunk – make sure your copy of `src/Rf_board_config.h` exists near the top of the repo so the parser can read `USER_CHUNK_SIZE`.

## One command replaces three

Previously you needed separate scripts for dataset, HOG config, and model files (with different receiver sketches). Now `unifier_transfer.py` handles everything in one session, saving time and keeping files organized.
