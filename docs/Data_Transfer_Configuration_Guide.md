# Data Transfer Optimization & Configuration Guide

This guide explains how the data transfer protocol handles different ESP32 board variants, optimizes chunk sizes for maximum reliability and speed, and provides instructions for customizing transfer parameters.

## Overview

The data transfer system uses a CRC32-validated, ACK/NACK-based protocol to reliably send files from PC to ESP32 over USB Serial. Transfer performance depends critically on **chunk size**—the number of bytes sent in each transaction.

### Problem: USB CDC Buffer Constraints

Different ESP32 boards have different USB CDC RX buffer sizes:

| Board | USB CDC Buffer | Safe Chunk Size | Recommended Speed |
|-------|---|---|---|
| **ESP32-C3, C6** | ~384 bytes | 220 bytes | Standard (stable) |
| **ESP32-S3** | ~512 bytes | 256 bytes | Higher (good balance) |
| **ESP32 (standard)** | ~512-1024 bytes | 256 bytes | Higher (optimized) |
| **Unknown variant** | Unknown | 220 bytes | Conservative (safe) |

**Why it matters**: The USB CDC buffer must hold:
- Incoming serial bytes
- Command header (10 bytes for "ESP32_XFER" + 1 byte command)
- Payload data (CHUNK_SIZE bytes)

When chunk sizes are too large for the buffer, overruns occur → timeouts → transfer failure.

## How It Works: Automatic Board Detection

All ESP32 receiver sketches include `<Rf_board_config.h>`, which automatically detects your board at compile time and sets appropriate defaults:

```cpp
#include <Rf_board_config.h>  // Auto-detect board, set DEFAULT_CHUNK_SIZE
#include "Rf_file_manager.h"

const int BUFFER_CHUNK = USER_CHUNK_SIZE;  // Uses detected value, or user override
```

### Compile-Time Detection

The header checks `CONFIG_IDF_TARGET_*` macros to identify your board:

- `CONFIG_IDF_TARGET_ESP32C3` or `CONFIG_IDF_TARGET_ESP32C6` → 220 bytes (conservative)
- `CONFIG_IDF_TARGET_ESP32S3` → 256 bytes (balanced)
- `CONFIG_IDF_TARGET_ESP32` → 256 bytes (standard)
- Unknown → 220 bytes (safe default)

## Customizing Chunk Size

### Option 1: Use Board Defaults (Recommended)

Simply upload the sketch as-is. The board auto-detection will set the optimal chunk size.

**Verify it worked**: Open the Serial Monitor at 115200 baud. On startup, you should see:

```
=== Board Configuration ===
Board: ESP32-C3/C6 (Small CDC Buffer)
Chunk Size: 220 bytes
⚠️  WARNING: This board has a small USB CDC buffer.
   Chunk size is set conservatively to prevent overruns.
==============================
```

(Or "ESP32-S3", "ESP32", etc., depending on your board.)

### Option 2: Override for Higher Speed

If you have a larger board (e.g., ESP32 or ESP32-S3) and want to test higher chunk sizes:

**In the ESP32 sketch**, before including `<Rf_board_config.h>`, define `USER_CHUNK_SIZE`:

```cpp
#define USER_CHUNK_SIZE 512  // Override to 512 bytes for high-speed transfer

#include <Rf_board_config.h>
#include "Rf_file_manager.h"

const int BUFFER_CHUNK = USER_CHUNK_SIZE;
```

Then, update the **corresponding PC script** to match:

```python
# In transfer_dataset.py, transfer_dp_file.py, etc.
CHUNK_SIZE = 512  # Match the ESP32 sketch
```

### Option 3: Edit Rf_board_config.h

Modify `Rf_board_config.h` directly to change `RF_BOARD_DEFAULT_CHUNK`:

```cpp
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define BOARD_TYPE "ESP32 (Standard CDC Buffer)"
   #define RF_BOARD_DEFAULT_CHUNK 512  // <-- Increase for higher speed
   #define RF_BOARD_SMALL_USB_BUFFER 0
#endif
```

## PC-Side Script Configuration

All PC transfer scripts (`unified_transfer.py`, `transfer_dataset.py`, `transfer_dp_file.py`, `transfer_categorizer.py`, etc.) define `CHUNK_SIZE` at the top:

```python
# Transfer timing and size configuration
# IMPORTANT: Keep CHUNK_SIZE in sync with ESP32 sketch's BUFFER_CHUNK
CHUNK_SIZE = 220  # Default: safe for ESP32-C3
CHUNK_DELAY = 0.02  # Small delay; ACK/NACK handshake controls pacing
```

### Synchronizing PC and ESP32

**Critical Rule**: `CHUNK_SIZE` on PC must equal `USER_CHUNK_SIZE` (or `DEFAULT_CHUNK_SIZE`) on ESP32.

**Example 1: Standard ESP32-C3 (default)**
- ESP32 sketch: `BUFFER_CHUNK = 220` (auto-detected)
- PC script: `CHUNK_SIZE = 220` ✅ Match!

**Example 2: Custom high-speed on ESP32**
- ESP32 sketch: `#define USER_CHUNK_SIZE 512` before `<Rf_board_config.h>`
- PC script: `CHUNK_SIZE = 512` ✅ Match!

If they don't match → transfer hangs or fails.

## Testing & Validation

### Test a Transfer

1. **Upload the sketch** to your ESP32:
   - Open the receiver sketch (e.g., `unified_receiver.ino`)
   - Compile and upload (Arduino IDE / PlatformIO)

2. **Check Serial output**:
   - Open Serial Monitor (115200 baud)
   - You should see board config printed

3. **Run PC transfer**:
   ```bash
   cd tools/data_quantization/data_transfer/pc_side
   python3 unified_transfer.py digit_data /dev/ttyACM0
   ```

4. **Observe transfer progress**:
   - PC script shows `✅ Got response: READY` and chunk progress
   - ESP32 blinks LED during transfer
   - Transfer completes with `✓ File transferred successfully!`

### Troubleshoot Failures

| Symptom | Likely Cause | Solution |
|---------|---|---|
| Timeout after a few chunks | Chunk size too large | Reduce `CHUNK_SIZE` in PC script and `USER_CHUNK_SIZE` in sketch |
| Transfer never starts | Mismatched chunk size | Verify PC and ESP32 values are equal |
| Timeouts on ESP32-C3 only | Default too high for this board | Ensure `<Rf_board_config.h>` is included before `Rf_file_manager.h` |
| Files corrupted or incomplete | CRC mismatch | Check USB cable quality; retry transfer |

## Recommended Settings by Use Case

### Maximum Compatibility (All Boards)
- `CHUNK_SIZE = 220`
- Works on ESP32, ESP32-C3, ESP32-S3
- ~0.6 KB/s transfer rate (slower, but 100% reliable)

### Balanced (ESP32-C3 + larger boards)
- `CHUNK_SIZE = 220` on ESP32-C3 (auto-detected)
- `USER_CHUNK_SIZE = 256` on ESP32/ESP32-S3 (if customized)
- ~0.8–1.0 KB/s transfer rate

### High Speed (ESP32 / ESP32-S3 only)
- `USER_CHUNK_SIZE = 512`
- ~2.0 KB/s transfer rate
- **Not recommended** for ESP32-C3; may cause overruns

## Performance Metrics

Approximate transfer times for a 1 MB file:

| Chunk Size | Baud Rate | Est. Time | Risk Level |
|---|---|---|---|
| 220 bytes | 115200 | ~30 min | ✅ Low (stable) |
| 256 bytes | 115200 | ~25 min | ✅ Low (stable) |
| 512 bytes | 115200 | ~15 min | ⚠️ Medium (C3 risk) |

*Times vary with system load, USB cable quality, and filesystem performance.*

## Files & Locations

### ESP32 Sketches
- `tools/data_quantization/data_transfer/esp32_side/unified_receiver.ino` (includes `<Rf_board_config.h>`)
- `tools/data_quantization/data_transfer/esp32_side/dataset_receiver.ino` (includes `<Rf_board_config.h>`)
- `tools/data_quantization/data_transfer/esp32_side/dp_file_receiver.ino` (includes `<Rf_board_config.h>`)
- `tools/data_quantization/data_transfer/esp32_side/quantizer_receiver.ino` (includes `<Rf_board_config.h>`)
- `tools/pre_train/data_transfer/esp32_side/model_receiver.ino` (includes `<Rf_board_config.h>`)
- `tools/hog_transform/data_transfer/esp32_side/hog_config_receiver.ino` (includes `<Rf_board_config.h>`)

### PC Scripts
- `tools/data_quantization/data_transfer/pc_side/unified_transfer.py`
- `tools/data_quantization/data_transfer/pc_side/transfer_dataset.py`
- `tools/data_quantization/data_transfer/pc_side/transfer_dp_file.py`
- `tools/data_quantization/data_transfer/pc_side/transfer_categorizer.py`
- `tools/pre_train/data_transfer/pc_side/transfer_model.py`
- `tools/hog_transform/data_transfer/pc_side/transfer_hog_config.py`

### Configuration Header
- `src/Rf_board_config.h` – shared auto-detection, diagnostics, and override hooks

## Implementation Details

### Rf_board_config.h Features

1. **Automatic Board Detection** (compile-time)
   - Checks `CONFIG_IDF_TARGET_*` macros (ESP32 family) and other Arduino cores
   - Sets `RF_BOARD_DEFAULT_CHUNK` and capability flags per board

2. **User Override Hooks**
   - Define `RF_BOARD_SKIP_AUTODETECT` or individual macros before including the header
   - Define `USER_CHUNK_SIZE`, `RF_USE_PSRAM`, `RF_USE_SDCARD`, etc., without editing the library

3. **Diagnostic Output**
   - `print_board_info()` prints detected board, chunk size, storage preference, and warnings at startup

### Protocol Details

Each transfer uses:
- **CRC32 validation**: Every chunk checked against computed CRC
- **ACK/NACK handshake**: ESP32 acknowledges successful chunks; retries on NACK
- **Timeout handling**: Extended timeouts account for file system delays
- **Streaming reads**: ESP32 reads payload byte-by-byte to prevent buffer overrun

## Common Questions

**Q: My transfer works on ESP32 but not on ESP32-C3. Why?**

A: ESP32-C3 has a smaller USB CDC buffer. The shared `Rf_board_config.h` should auto-detect this, but verify:
   1. Include `<Rf_board_config.h>` **before** `Rf_file_manager.h`
   2. Check Serial output for the board detection message
   3. If mismatch, the PC script's `CHUNK_SIZE` may not match ESP32's `BUFFER_CHUNK`

**Q: How do I measure transfer speed?**

A: Monitor reports total file size and elapsed time:
   ```
   ✓ Finished transferring model_config.json
   ```
   Divide file size by elapsed seconds to get KB/s.

**Q: Can I change chunk size on-the-fly?**

A: No. Chunk size is fixed at compile time (C++ constant). To change:
   1. Edit `USER_CHUNK_SIZE` in the sketch
   2. Edit `CHUNK_SIZE` in PC script
   3. Recompile and re-upload ESP32 sketch
   4. Rerun PC script

**Q: Why does hog_config_receiver use SD_MMC instead of LittleFS?**

A: HOG config files are often stored on SD cards (especially on ESP32-CAM). You can change `STORAGE_MODE` if needed, but ensure your board supports the selected storage.

## References

- **Rf_board_config.h**: Explains USB CDC buffer constraints and chunk size trade-offs
- **Receiver sketches**: Detailed comments on CHUNK_SIZE configuration
- **Transfer scripts**: Comments on CHUNK_SIZE synchronization requirements
- **Rf_file_manager.h/cpp**: File system abstraction with create-on-write support
