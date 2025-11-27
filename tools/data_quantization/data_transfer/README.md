# ESP32 Data Transfer System

Transfer files from PC to ESP32 via Serial connection with organized model-based directory structure.

## System Architecture

```
PC (Python Scripts)  ←→  Serial Connection  ←→  ESP32 (Arduino Sketches)
      pc_side/                                     esp32_side/
```

## Features

- ✅ **Robust Transfer Protocol**: CRC32 verification and ACK/NACK retry mechanism
- ✅ **Model Organization**: Files stored in `/model_name/filename` structure
- ✅ **LittleFS File System**: Modern, robust file system with directory support
- ✅ **Multiple Transfer Modes**: Unified, individual file, and manual transfers
- ✅ **Error Recovery**: Automatic retry on transmission errors
- ✅ **Progress Tracking**: LED indicators and serial feedback

## Directory Structure

```
data_transfer/
├── esp32_side/              # Arduino sketches for ESP32
│   ├── unified_receiver.ino              # Complete dataset transfer (categorizer + params + binary)
│   ├── dataset_receiver.ino              # Binary dataset transfer
│   ├── categorizer_receiver.ino          # Categorizer CSV transfer
│   ├── dp_file_receiver.ino             # Dataset parameters CSV transfer
│   └── manual_transfer/                  # Manual copy-paste transfer methods
│       ├── csv_dataset_receiver.ino
│       ├── ctg_receiver.ino
│       └── dataset_params_receiver.ino
├── pc_side/                 # Python scripts for PC
│   ├── unified_transfer.py               # Complete dataset transfer script
│   ├── transfer_dataset.py               # Binary dataset transfer script
│   ├── transfer_categorizer.py           # Categorizer transfer script
│   └── transfer_dp_file.py              # Dataset parameters transfer script
├── MIGRATION_GUIDE.md       # Detailed migration guide from SPIFFS to LittleFS
├── CHANGES_SUMMARY.md       # Summary of all changes made
└── README.md               # This file
```

## Quick Start

### 1. Upload ESP32 Sketch

Choose and upload ONE of the receiver sketches to your ESP32:

#### Option A: Unified Receiver (Recommended)
Transfers complete dataset in one session.
```bash
# Upload: esp32_side/unified_receiver.ino
```

#### Option B: Individual Receivers
Transfer files one at a time.
```bash
# Upload ONE of:
# - esp32_side/dataset_receiver.ino
# - esp32_side/categorizer_receiver.ino
# - esp32_side/dp_file_receiver.ino
```

### 2. Configure Model Name (Optional)

By default, files are saved to `/default_model/` directory.

**To change the model name**, edit before uploading:
```cpp
char modelName[64] = "your_model_name"; // Change this line
```

Or update PC-side scripts to send model name (for unified_receiver.ino).

### 3. Run PC-Side Transfer Script

**For unified transfer:**
```python
python3 pc_side/unified_transfer.py --port /dev/ttyUSB0 --model mnist_model --basename digit
```

**For individual transfers:**
```python
python3 pc_side/transfer_dataset.py --port /dev/ttyUSB0 --file digit_nml.bin
python3 pc_side/transfer_categorizer.py --port /dev/ttyUSB0 --file digit_ctg.csv
python3 pc_side/transfer_dp_file.py --port /dev/ttyUSB0 --file digit_dp.csv
```

## File Organization

### On ESP32 LittleFS:
```
/
├── mnist_model/
│   ├── digit_nml.bin          # Binary dataset
│   ├── digit_ctg.csv          # Categorizer
│   └── digit_dp.csv           # Dataset parameters
├── gesture_model/
│   ├── gesture_nml.bin
│   ├── gesture_ctg.csv
│   └── gesture_dp.csv
└── default_model/
    ├── test_nml.bin
    ├── test_ctg.csv
    └── test_dp.csv
```

## Transfer Modes

### 1. Unified Transfer (Best for Complete Datasets)

**Use when:** Transferring a complete dataset with categorizer and parameters.

**Advantages:**
- Single coordinated session
- Automatic cleanup of old files
- Better error handling
- Progress tracking

**Files uploaded:** `unified_receiver.ino` on ESP32, `unified_transfer.py` on PC

**Example:**
```python
python3 pc_side/unified_transfer.py \
    --port /dev/ttyUSB0 \
    --model mnist_model \
    --basename digit \
    --categorizer digit_ctg.csv \
    --params digit_dp.csv \
    --dataset digit_nml.bin
```

### 2. Individual Transfer (For Single Files)

**Use when:** Updating only one file or testing.

**Advantages:**
- Simple, focused transfers
- Good for updates
- Independent operation

**Files:** Upload specific receiver (.ino) for the file type you're transferring.

**Examples:**
```python
# Transfer binary dataset
python3 pc_side/transfer_dataset.py --port /dev/ttyUSB0 --file digit_nml.bin

# Transfer categorizer
python3 pc_side/transfer_categorizer.py --port /dev/ttyUSB0 --file digit_ctg.csv

# Transfer parameters
python3 pc_side/transfer_dp_file.py --port /dev/ttyUSB0 --file digit_dp.csv
```

### 3. Manual Transfer (Copy-Paste via Serial Monitor)

**Use when:** Serial connection is unstable or for small CSV files.

**Advantages:**
- Works with Arduino IDE Serial Monitor
- No special scripts needed
- Good for debugging

**Files:** Use sketches in `manual_transfer/` directory

**Process:**
1. Upload appropriate manual_transfer sketch
2. Open Arduino Serial Monitor
3. Copy-paste file content
4. Follow on-screen instructions

## LED Indicators

All receivers use LED on pin 2 (built-in LED on most ESP32 boards):

- **3 blinks on startup**: System ready
- **LED ON**: Transfer in progress
- **2 quick blinks**: File received successfully
- **Fast blinking**: Error state
- **5 blinks**: Session completed successfully

## Protocol Details

### V2 Protocol (Current)
- Chunk-based transfer with CRC32 verification
- ACK/NACK mechanism for reliability
- Automatic retry on errors
- Configurable chunk size (default 256 bytes)

### Data Flow:
```
PC                          ESP32
│                           │
├─ TRANSFER_V2 ───────────→ │
│                           ├─ Initialize
│                           │
├─ File Info ──────────────→ │
│                           ├─ Create file
│                           │
│                           ├─ READY_V2 ───────→
│                           │
├─ Chunk (offset, len, CRC) │
│                           ├─ Verify CRC
│                           ├─ ACK/NACK ────────→
│                           │
├─ ... more chunks ...      │
│                           │
├─ TRANSFER_END ───────────→ │
│                           ├─ Finalize & verify
│                           │
│                           ├─ TRANSFER_COMPLETE→
```

## Configuration

### Transfer Parameters (Must match on both sides!)

**In ESP32 sketches:**
```cpp
const int BUFFER_CHUNK = 256;      // Chunk size in bytes
const int BUFFER_DELAY_MS = 20;    // Delay between chunks
```

**In Python scripts:**
```python
CHUNK_SIZE = 256              # Must match BUFFER_CHUNK
CHUNK_DELAY = 0.02           # Must match BUFFER_DELAY_MS
```

### Adjust for Different Connections:

**USB Serial (High Speed):**
```cpp
const int BUFFER_CHUNK = 512;
const int BUFFER_DELAY_MS = 10;
```

**USB CDC (ESP32-S2/S3):**
```cpp
const int BUFFER_CHUNK = 256;
const int BUFFER_DELAY_MS = 20;
```

**Bluetooth Serial (Slower):**
```cpp
const int BUFFER_CHUNK = 128;
const int BUFFER_DELAY_MS = 50;
```

## Troubleshooting

### Issue: "LittleFS Mount Failed"
**Solution:** 
- First time use will format (takes time)
- Ensure `LittleFS.begin(true)` is called
- Try manual format: `LittleFS.format()`

### Issue: Transfer hangs or times out
**Solution:**
- Check USB cable quality
- Reduce CHUNK_SIZE
- Increase CHUNK_DELAY_MS
- Close other serial programs
- Try different USB port

### Issue: CRC errors or NACK responses
**Solution:**
- Increase CHUNK_DELAY
- Check for electromagnetic interference
- Try shorter/better USB cable
- Reduce CHUNK_SIZE

### Issue: Files not found after transfer
**Solution:**
- Verify model name matches in code
- Check file path includes model name: `/model_name/filename`
- List directory to verify: `LittleFS.open("/model_name")`

### Issue: "File already exists" error
**Solution:**
- Old file cleanup should be automatic
- Manually delete: `LittleFS.remove("/model_name/filename")`
- Check safeDeleteFile() function is working

## Migration from SPIFFS

See [MIGRATION_GUIDE.md](MIGRATION_GUIDE.md) for complete migration instructions.

**Key points:**
- SPIFFS files are NOT accessible after switching to LittleFS
- All files must be re-transferred
- Update file paths in application code to include model name
- Benefits: Better organization, multiple models, modern file system

## PC-Side Script Requirements

### Python Version
- Python 3.7 or higher

### Required Packages
```bash
pip install pyserial
```

### Serial Port Access (Linux)
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and log back in

# Or run with sudo
sudo python3 pc_side/transfer_dataset.py ...
```

## Best Practices

1. **Always verify transfers**: Check serial output for "TRANSFER_COMPLETE"
2. **Use model names**: Organize files by model for better management
3. **Test with small files first**: Verify configuration before large transfers
4. **Keep backups**: Save important files before re-transferring
5. **Match parameters**: Ensure CHUNK_SIZE and DELAY match on both sides
6. **Good cables**: Use quality USB cables for reliable transfers
7. **Close other programs**: Don't have multiple programs accessing serial port

## Examples

### Transfer Complete MNIST Dataset
```bash
# 1. Upload unified_receiver.ino to ESP32
# 2. Run unified transfer
python3 pc_side/unified_transfer.py \
    --port /dev/ttyUSB0 \
    --model mnist_model \
    --basename digit \
    --categorizer data/digit_ctg.csv \
    --params data/digit_dp.csv \
    --dataset data/digit_nml.bin
```

### Transfer Single Updated File
```bash
# 1. Upload dataset_receiver.ino to ESP32
# 2. Edit modelName to match your model
# 3. Transfer updated file
python3 pc_side/transfer_dataset.py \
    --port /dev/ttyUSB0 \
    --file data/updated_digit_nml.bin
```

### Multiple Models on Same ESP32
```bash
# Model 1: MNIST digits
python3 pc_side/unified_transfer.py ... --model mnist_model ...

# Model 2: Gesture recognition  
python3 pc_side/unified_transfer.py ... --model gesture_model ...

# Model 3: Walker detection
python3 pc_side/unified_transfer.py ... --model walker_model ...

# All models stored in separate directories:
# /mnist_model/, /gesture_model/, /walker_model/
```

## Advanced Usage

### Verify File Transfer in Application
```cpp
void verifyModel(const char* modelName) {
    char path[128];
    
    // Check dataset
    snprintf(path, sizeof(path), "/%s/%s_nml.bin", modelName, basename);
    if (!LittleFS.exists(path)) {
        Serial.println("Dataset missing!");
        return;
    }
    
    // Check categorizer
    snprintf(path, sizeof(path), "/%s/%s_ctg.csv", modelName, basename);
    if (!LittleFS.exists(path)) {
        Serial.println("Categorizer missing!");
        return;
    }
    
    // Check parameters
    snprintf(path, sizeof(path), "/%s/%s_dp.csv", modelName, basename);
    if (!LittleFS.exists(path)) {
        Serial.println("Parameters missing!");
        return;
    }
    
    Serial.println("All files present!");
}
```

### List Available Models
```cpp
void listModels() {
    File root = LittleFS.open("/");
    File dir = root.openNextFile();
    
    Serial.println("Available models:");
    while(dir) {
        if (dir.isDirectory()) {
            Serial.print("  - ");
            Serial.println(dir.name());
        }
        dir = root.openNextFile();
    }
}
```

### Delete Old Model
```cpp
void deleteModel(const char* modelName) {
    char path[128];
    snprintf(path, sizeof(path), "/%s", modelName);
    
    // Delete all files in directory
    File dir = LittleFS.open(path);
    File file = dir.openNextFile();
    while(file) {
        String filePath = String(path) + "/" + String(file.name());
        LittleFS.remove(filePath);
        file = dir.openNextFile();
    }
    
    // Remove directory
    LittleFS.rmdir(path);
}
```

## Support

For issues or questions:
1. Check the troubleshooting section above
2. Review [MIGRATION_GUIDE.md](MIGRATION_GUIDE.md)
3. Check [CHANGES_SUMMARY.md](CHANGES_SUMMARY.md) for technical details
4. Verify serial output for error messages
5. Test with LED indicators

## License

Part of the STL_MCU library project.
