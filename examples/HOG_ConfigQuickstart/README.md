# HOG Config Quickstart Example

A minimal ESP32-CAM example that loads HOG configuration from a JSON file and extracts features from camera images. **Optimized for fast inference (~10-15ms).**

## Features

- ✅ Loads configuration from filesystem (LittleFS/SD card)
- ✅ Minimal setup - no manual enum conversions needed
- ✅ **Optimized for real-time inference** with pre-allocated buffers
- ✅ Captures images every 3 seconds
- ✅ Prints full feature vector to Serial Monitor
- ✅ Works with ESP32-CAM AI-Thinker board

## Performance

| Camera Input | Processing Time | Use Case |
|--------------|----------------|----------|
| 96x96 (recommended) | ~10-15ms | Balanced speed/quality |
| 160x120 (QQVGA) | ~20-25ms | Better image quality |
| 32x32 (fast path) | ~5-8ms | Maximum speed |

See `docs/HOG_Performance_Optimization.md` for detailed optimization guide.

## Hardware

- ESP32-CAM AI-Thinker board
- 5V/2A power supply (recommended)
- USB-TTL adapter for programming

## Configuration File

This example expects a configuration file at: `/digit/digit_hogcfg.json`

**Optimized configuration (recommended):**

```json
{
  "hog": {
    "hog_img_width": 32,
    "hog_img_height": 32,
    "cell_size": 8,
    "block_size": 16,
    "block_stride": 6,
    "nbins": 4
  },
  "esp32": {
    "input_format": "GRAYSCALE",
    "input_width": 96,
    "input_height": 96,
    "resize_method": "NEAREST_NEIGHBOR",
    "maintain_aspect_ratio": false,
    "jpeg_quality": 80
  }
}
```

**Key optimization:** Use 96x96 input (3x downscale) instead of 320x240 (10x downscale) for 70% faster processing.

See `tools/hog_transform/CONFIG_FORMAT.md` for supported enum values.

## Setup

### 1. Generate Configuration File

Use the `hog_processor` tool to process your dataset and generate the config:

```bash
cd tools/hog_transform
./hog_processor hog_config.json
```

This creates:
- `model_name.csv` - Dataset with HOG features
- `model_name_hogcfg.json` - ESP32 configuration file

### 2. Upload Config to ESP32

Upload the generated `*_hogcfg.json` file to your ESP32 filesystem:

**Option A: Using Arduino IDE (LittleFS)**
1. Place the config file in a `data/` folder in your sketch directory
2. Install "ESP32 Sketch Data Upload" plugin
3. Tools → ESP32 Sketch Data Upload

**Option B: Using SD Card**
1. Copy the config file to SD card at `/digit/digit_hogcfg.json`
2. Insert SD card into ESP32-CAM

### 3. Update Config Path

Edit the sketch to match your config file location:

```cpp
constexpr const char* CONFIG_FILE_PATH = "/digit/digit_hogcfg.json";
```

## Usage

1. Open Serial Monitor at 115200 baud
2. ESP32 will mount filesystem and load config
3. Camera captures every 3 seconds
4. Feature vector printed to Serial Monitor

## Output Example

```
ESP32-CAM HOG Config Quickstart
================================
Filesystem mounted (LittleFS)
Loaded config: /digit/digit_hogcfg.json
Camera input: 320x240 format=0
HOG size: 32x32
Capturing every 3000 ms

Frame 320x240 | 45 ms | 144 features
Features: [0.123456, 0.234567, 0.345678, ...]

Frame 320x240 | 43 ms | 144 features
Features: [0.198765, 0.287654, 0.376543, ...]
```

## Troubleshooting

**"Filesystem mount failed"**
- Make sure LittleFS is initialized or SD card is inserted
- Falls back to default HOG configuration automatically

**"Config not found"**
- Verify config file is uploaded to correct path
- Check CONFIG_FILE_PATH matches your file location
- Falls back to default HOG configuration

**"Camera init failed"**
- Check power supply (5V/2A recommended)
- Verify camera module connections
- Check GPIO pin definitions match your board

## Integration with ML Model

Use the extracted features for prediction:

```cpp
const auto& features = hog.getFeatures();

// Your ML model prediction
int prediction = myModel.predict(features.data(), features.size());
Serial.printf("Predicted class: %d\n", prediction);
```

## See Also

- `HOG_Quickstart/` - Even simpler example without config file
- `HOG_Advanced/` - Advanced configuration and optimization
- `tools/hog_transform/CONFIG_FORMAT.md` - Config file format reference
