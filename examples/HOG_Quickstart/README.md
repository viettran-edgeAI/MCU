# HOG Quickstart Example

The **fastest way** to get HOG feature extraction working on ESP32-CAM!

## 🎯 Purpose

This example is designed for **beginners** who want to start extracting HOG features immediately without dealing with complex configurations. Just 3 lines of code and you're ready! Also demonstrates loading configuration from files for production deployments.

## 🚀 What You Get

- ✅ **One-line setup** with optimal defaults
- ✅ **Config file support** for production deployment
- ✅ **Minimal code** - easy to understand and modify
- ✅ **Production ready** performance (~40-60ms processing)
- ✅ **ML integration ready** with example code snippets
- ✅ **Perfect parameter matching** between training and inference

## 💡 Two Setup Methods

### Method 1: Quick Start (3 Lines)
```cpp
HOG_MCU hog;                           // 1. Create HOG processor
hog.setupForESP32CAM();                // 2. Configure with defaults
hog.transform(camera_buffer);          // 3. Extract features!
```

### Method 2: Config File (Recommended for Production)
```cpp
#include "Rf_file_manager.h"

HOG_MCU hog;
RF_FS_BEGIN();                         // Mount filesystem
hog.loadConfigFromFile("/model_hogcfg.json");  // Load config
hog.transform(camera_buffer);          // Extract features!
```

## 📁 Using Configuration Files

Configuration files ensure **exact parameter matching** between training (PC) and inference (ESP32).

### Step 1: Generate Config on PC
```bash
cd tools/hog_transform
./hog_processor hog_config.json
```

This creates two files:
- `model_name.csv` - Training features for ML
- `model_name_hogcfg.json` - ESP32 configuration

### Step 2: Upload Config to ESP32
Upload `model_name_hogcfg.json` to LittleFS or SD card using:
- Arduino IDE: Tools → ESP32 Sketch Data Upload
- Manual file copy to storage

### Step 3: Load in Your Sketch
```cpp
#define USE_CONFIG_FILE "/model_name_hogcfg.json"
#include "Rf_file_manager.h"

void setup() {
    // Initialize filesystem
    RF_FS_BEGIN();
    
    // Load configuration
    if (hog.loadConfigFromFile(USE_CONFIG_FILE)) {
        Serial.println("Config loaded successfully!");
        Serial.printf("Feature CSV: %s\n", hog.getFeatureCsvPath().c_str());
        Serial.printf("Feature count: %d\n", hog.getFeatures().capacity());
    }
}
```

### Benefits of Config Files
✓ **Guaranteed consistency** - Same parameters used in training and inference  
✓ **Easy updates** - Change parameters without recompiling  
✓ **Documentation** - Config file documents exact training setup  
✓ **Multi-device** - Deploy same config to multiple devices  
✓ **Version control** - Track configuration changes over time

## 📋 Default Configuration

| Setting | Value | Why This Works |
|---------|-------|----------------|
| **Input Format** | GRAYSCALE | Fastest processing, good for HOG |
| **Camera Resolution** | 320×240 (QVGA) | Good balance of detail vs speed |
| **HOG Processing Size** | 32×32 pixels | Optimal for ESP32 performance |
| **Cell Size** | 8×8 pixels | Standard HOG cell size |
| **Block Size** | 16×16 pixels | 2×2 cells per block (standard) |
| **Block Stride** | 6 pixels | Good overlap for robust features |
| **Histogram Bins** | 4 | Efficient for embedded systems |
| **Output Features** | ~144 | Perfect size for most ML models |

## 🔧 Hardware Setup

### Required Components
- **ESP32-CAM board** (AI-Thinker model recommended)
- **OV2640 camera module** (usually included)
- **5V/2A power supply** (stable power is crucial!)
- **USB-to-Serial adapter** for programming

### Connections
- No additional connections needed - use standard ESP32-CAM pinout
- Ensure camera module is properly seated
- Connect power to 5V pin (not 3.3V for camera operation)

## 📊 Expected Output

```
ESP32-CAM HOG Quickstart
========================
✅ Camera initialized
✅ HOG configured with defaults:
   - Input: 320x240 GRAYSCALE
   - Processing: 32x32 pixels
   - Features: ~144 per image
   - Processing time: ~40-60ms

🚀 Ready! Capturing images every 3 seconds...

📸 Image: 320x240 | ⏱️ Processed: 45 ms | 🔢 Features: 144
Features: 0.123 0.456 0.789 0.234 0.567 0.890 0.123 0.345 ...
📸 Image: 320x240 | ⏱️ Processed: 42 ms | 🔢 Features: 144
Features: 0.198 0.543 0.276 0.891 0.034 0.756 0.432 0.189 ...
```

## 🎨 Simple Customizations

### Different Camera Formats
```cpp
// For RGB565 color cameras
hog.setupForESP32CAM(ImageProcessing::PixelFormat::RGB565, 320, 240);

// For higher resolution (slower but more detail)
hog.setupForESP32CAM(ImageProcessing::PixelFormat::GRAYSCALE, 640, 480);

// For lower resolution (faster processing)
hog.setupForESP32CAM(ImageProcessing::PixelFormat::GRAYSCALE, 160, 120);
```

### Machine Learning Integration
```cpp
// With your classifier
const auto& features = hog.getFeatures();
float prediction = myClassifier.predict(features.data(), features.size());

if (prediction > 0.7) {
    Serial.println("Object detected with high confidence!");
}
```

## ⚡ Performance

| Metric | Typical Value | Notes |
|--------|---------------|-------|
| **Processing Time** | 40-60ms | On ESP32 @240MHz |
| **Memory Usage** | <2KB | Stack allocated, very efficient |
| **Frame Rate** | ~15 FPS | Including camera capture time |
| **Feature Count** | 144 | Fixed size, perfect for ML |

## 🛠️ Troubleshooting

### Camera Issues
| Problem | Solution |
|---------|----------|
| Camera init failed | Check 5V power supply and connections |
| No image captured | Verify camera module is seated properly |
| Dark/poor images | Improve lighting or adjust camera position |

### Performance Issues
| Problem | Solution |
|---------|----------|
| Processing too slow | Reduce camera resolution or use faster pixel format |
| Out of memory | Restart ESP32 periodically or reduce buffer sizes |
| Inconsistent timing | Disable WiFi or other background tasks |

### Feature Quality Issues
| Problem | Solution |
|---------|----------|
| All zero features | Check lighting and camera focus |
| No feature variation | Change camera angle or scene content |
| Noisy features | Improve lighting conditions |

## 🔗 Next Steps

1. **Get it working**: Start with this basic example
2. **Test your setup**: Verify features are being extracted correctly
3. **Add your ML model**: Integrate with your classifier
4. **Need more control?** → Check out the `HOG_Advanced` example for custom HOG parameters
5. **Production deployment**: Optimize settings for your specific use case

## 💡 Pro Tips

- **Lighting is key**: Consistent, good lighting improves feature quality significantly
- **Keep it simple**: Start with defaults, optimize later if needed
- **Test thoroughly**: Try different scenes and lighting conditions
- **Monitor memory**: Use `Rf_memory_status().first` to check available memory
- **Power matters**: Use a quality 5V/2A power supply for stable operation

## 📚 Related Examples

- **`HOG_Advanced`** - For custom HOG parameter tuning
- **`BasicContainers`** - Learn about STL_MCU data structures
- **`SensorDataLogger`** - Save HOG features to storage

---

🎉 **You're ready to extract HOG features!** This quickstart gets you up and running in minutes with production-quality results.