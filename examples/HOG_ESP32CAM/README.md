# ESP32-CAM HOG Feature Extraction Example

Complete example for extracting HOG (Histogram of Oriented Gradients) features from camera images on ESP32-CAM. Perfect for machine learning applications requiring visual feature extraction.

## 🎯 Overview

This example demonstrates three different ways to configure HOG feature extraction:

1. **QUICK START** - Minimal setup with optimal defaults (fastest to get started)
2. **CONFIG FILE** - Load from JSON file (recommended for production)
3. **CUSTOM PARAMS** - Manual parameter configuration (advanced users)

## ✨ Features

- ✅ **Simple Setup** - Just 3 lines of code to get started
- ✅ **Production Ready** - Load configuration from JSON files
- ✅ **Flexible** - Choose between defaults or custom parameters
- ✅ **Performance Metrics** - Real-time processing statistics
- ✅ **Feature Analysis** - Detailed feature vector statistics
- ✅ **ML Ready** - Easy integration with classifiers

## 🔧 Hardware Requirements

- **ESP32-CAM board** (AI-Thinker model recommended)
- **OV2640 camera module** (usually included)
- **5V/2A power supply** (stable power is crucial!)
- **USB-to-Serial adapter** for programming
- **SD card or LittleFS** (optional, for config file mode)

## 🚀 Quick Start (3 Lines of Code!)

```cpp
HOG_MCU hog;                           // 1. Create HOG processor
hog.setupForESP32CAM();                // 2. Configure with defaults
hog.transform(camera_buffer);          // 3. Extract features!
```

This uses optimal defaults:
- GRAYSCALE input format
- 320×240 camera resolution
- 32×32 HOG processing size
- ~144 features per image
- ~40-60ms processing time

## 📋 Configuration Modes

### Mode 1: Quick Start (Default)

Best for: **Beginners, rapid prototyping, testing**

```cpp
#define MODE_QUICK_START
```

Uses optimal default parameters. No configuration needed!

**Performance:**
- Processing time: ~40-60ms
- Feature count: 144
- Memory usage: ~576 bytes

### Mode 2: Config File (Recommended for Production)

Best for: **Production deployments, parameter matching between training and inference**

```cpp
#define MODE_CONFIG_FILE
const char* CONFIG_FILE_PATH = "/model_hogcfg.json";
```

**Benefits:**
- ✅ Guarantees identical parameters between PC training and ESP32 inference
- ✅ Easy updates without recompiling
- ✅ Documents exact training configuration
- ✅ Simplifies multi-device deployment

**Workflow:**

1. **Generate config on PC:**
   ```bash
   cd tools/hog_transform
   ./hog_processor hog_config.json
   ```
   This creates:
   - `model_name.csv` - Training features for ML
   - `model_name_hogcfg.json` - ESP32 configuration

2. **Upload config to ESP32:**
   - Arduino IDE: Tools → ESP32 Sketch Data Upload
   - Or manually copy to SD card/LittleFS

3. **Load in sketch:**
   ```cpp
   hog.loadConfigFromFile("/model_hogcfg.json");
   ```

### Mode 3: Custom Parameters (Advanced)

Best for: **Fine-tuning, optimization, research**

```cpp
#define MODE_CUSTOM_PARAMS
```

Allows manual configuration of all HOG parameters:

```cpp
HOG_MCU::Config customConfig;
customConfig.hog_img_width = 32;      // Processing resolution
customConfig.hog_img_height = 32;
customConfig.cell_size = 8;           // Cell size in pixels
customConfig.block_size = 16;         // Block size (2x2 cells)
customConfig.block_stride = 6;        // Overlap between blocks
customConfig.nbins = 4;               // Orientation bins
hog.setup(customConfig);
```

## ⚙️ Configuration Parameters

### Image Processing Parameters

| Parameter | Description | Default | Impact |
|-----------|-------------|---------|---------|
| `input_format` | Pixel format | GRAYSCALE | Grayscale is fastest |
| `input_width` | Camera width | 320 | Larger = more detail |
| `input_height` | Camera height | 240 | Larger = more detail |
| `resize_method` | Resize algorithm | NEAREST_NEIGHBOR | Fastest method |

### HOG Parameters

| Parameter | Description | Default | Impact |
|-----------|-------------|---------|---------|
| `hog_img_width` | Processing width | 32 | Larger = more features, slower |
| `hog_img_height` | Processing height | 32 | Larger = more features, slower |
| `cell_size` | Histogram cell size | 8 | Smaller = finer detail, more computation |
| `block_size` | Normalization block | 16 | Usually 2×2 cells |
| `block_stride` | Block overlap | 6 | Smaller = more features, slower |
| `nbins` | Orientation bins | 4 | More = better precision, more memory |

### Performance Trade-offs

**For Speed:**
- Smaller HOG image size (32×32)
- Larger cell size (8×8)
- Larger block stride (8)
- Fewer bins (4)
- → ~20-40ms, 144 features

**For Quality:**
- Larger HOG image size (64×64)
- Smaller cell size (6×6)
- Smaller block stride (4)
- More bins (6)
- → ~80-150ms, 576 features

**Balanced (Recommended):**
- 32×32 or 48×48 HOG size
- 8×8 cell size
- Stride of 6
- 4 bins
- → ~40-70ms, 144-256 features

## 📊 Output Settings

Customize what information is displayed:

```cpp
const bool SHOW_FULL_FEATURES = false;    // Print all feature values
const bool SHOW_STATISTICS = true;        // Show min/max/mean statistics
const uint32_t CAPTURE_INTERVAL_MS = 3000; // Time between captures
```

## 🎯 Machine Learning Integration

The extracted features are ready for ML classification:

```cpp
// Get features
const auto& features = hog.getFeatures();

// Example: Random Forest prediction
float prediction = myForest.predict(features.data(), features.size());
Serial.printf("Prediction: %.2f\n", prediction);

// Example: Classification
int classLabel = classifier.classify(features.data(), features.size());
Serial.printf("Detected class: %d\n", classLabel);
```

## 📈 Expected Output

```
=====================================
ESP32-CAM HOG Feature Extraction
=====================================

Configuration Mode: QUICK START
Using optimal default parameters

✅ HOG configured with defaults
✅ Camera initialized

--- Configuration Summary ---
Input format: GRAYSCALE
Camera input: 320x240
HOG processing: 32x32
Feature count: 144
-----------------------------

🚀 Ready! Capturing every 3000 ms...

📸 Frame: 320x240 | ⏱️  42 ms | 🔢 Features: 144
Features preview: 0.123 0.456 0.789 0.234 0.567 0.890 0.345 0.678 ...
Stats: min=0.001, max=0.987, mean=0.234, zeros=12/144
```

## 🔍 Troubleshooting

### Camera Init Failed
- Check power supply (need 5V/2A)
- Verify all pin connections
- Try pressing reset button

### Filesystem Mount Failed
- Check SD card is inserted (for SD mode)
- Verify LittleFS is uploaded (for Flash mode)
- Try changing STORAGE_MODE setting

### Config File Not Found
- Verify file path matches CONFIG_FILE_PATH
- Check file is uploaded to correct location
- Falls back to default config automatically

### Slow Processing
- Reduce HOG image size (try 32×32)
- Increase cell_size (try 8 or 10)
- Increase block_stride (try 8)
- Use GRAYSCALE instead of RGB

### High Memory Usage
- Reduce HOG image size
- Use fewer orientation bins
- Limit feature vector size

## 📚 Related Documentation

- **HOG Processor Tool**: `tools/hog_transform/README.md`
- **Config File Format**: `docs/details_docs/HOG_Migration_Guide.md`
- **Camera Integration**: `docs/details_docs/HOG_Camera_Integration.md`
- **File Manager**: `docs/details_docs/File_Manager_Tutorial.md`

## 💡 Tips & Best Practices

1. **Always test with default parameters first** before customizing
2. **Use config files for production** to ensure consistency
3. **Monitor processing time** to ensure real-time performance
4. **Balance feature count vs speed** for your application
5. **Use GRAYSCALE for fastest processing** unless color is needed
6. **Keep feature count under 256** for embedded ML models
7. **Stable 5V/2A power is critical** for reliable operation

## 🔗 Example Use Cases

- **Object Detection**: Use with Random Forest classifier
- **Gesture Recognition**: Real-time hand gesture classification
- **Quality Inspection**: Detect defects in manufactured parts
- **Face Recognition**: Extract facial features for identification
- **Motion Detection**: Analyze movement patterns
- **Scene Classification**: Categorize indoor/outdoor environments

## 📝 License

This example is part of the STL_MCU library.
See the main library LICENSE file for details.
