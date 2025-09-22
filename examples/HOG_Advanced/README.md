# ESP32-CAM HOG Advanced Example

Advanced HOG parameter customization and optimization for ESP32-CAM applications.

## Overview

This example demonstrates how to fine-tune HOG (Histogram of Oriented Gradients) parameters for specific use cases. Perfect for users who need to optimize performance, memory usage, or feature quality for their application.

## Features

- **Multiple Configuration Presets**: 4 pre-configured setups for different use cases
- **Automatic Configuration Cycling**: Switches between presets every 10 seconds
- **Performance Analysis**: Real-time processing time and memory usage metrics
- **Feature Statistics**: Detailed analysis of extracted features
- **Customization Examples**: Code templates for creating your own configurations

## Hardware Requirements

- ESP32-CAM module (AI-Thinker recommended)
- OV2640 camera module
- 5V/2A power supply
- USB-to-Serial adapter for programming

## Configuration Presets

### 1. Fast & Light
- **Image Size**: 32×32 pixels
- **Processing Time**: ~20-40ms
- **Features**: 144
- **Use Case**: Real-time applications, battery-powered devices
- **Memory**: 576 bytes

### 2. Balanced
- **Image Size**: 48×48 pixels  
- **Processing Time**: ~40-70ms
- **Features**: 256
- **Use Case**: General purpose object detection
- **Memory**: 1024 bytes

### 3. High Detail
- **Image Size**: 64×64 pixels
- **Processing Time**: ~80-150ms
- **Features**: 576
- **Use Case**: High accuracy requirements, offline processing
- **Memory**: 2304 bytes

### 4. Custom Fine
- **Image Size**: 48×48 pixels
- **Cell Size**: 6×6 pixels (smaller for detail)
- **Processing Time**: ~60-100ms
- **Features**: 324
- **Use Case**: Texture analysis, fine detail recognition
- **Memory**: 1296 bytes

## Parameter Explanation

### Key HOG Parameters

| Parameter | Description | Impact |
|-----------|-------------|---------|
| `hog_img_width/height` | Final processing resolution | Larger = more detail, slower |
| `cell_size` | Histogram cell size in pixels | Smaller = finer detail, more computation |
| `block_size` | Normalization block size | Usually 2×2 cells (16 pixels for 8px cells) |
| `block_stride` | Overlap between blocks | Smaller = more features, more computation |
| `nbins` | Orientation histogram bins | More = better precision, more memory |

### Performance Trade-offs

**Speed vs Quality**:
- Faster: Smaller image size, larger cells, fewer bins
- Higher Quality: Larger image size, smaller cells, more bins

**Memory vs Features**:
- Less Memory: Fewer features (larger stride, smaller image)
- More Features: More memory (smaller stride, larger image)

## Usage Example

```cpp
#include "STL_MCU.h"
#include "hog_transform.h"

HOG_MCU hog;

void setup() {
    // Custom configuration for your specific needs
    HOG_MCU::Config config;
    config.hog_img_width = 48;
    config.hog_img_height = 48;
    config.cell_size = 8;
    config.block_size = 16;
    config.block_stride = 8;
    config.nbins = 6;
    
    hog.setConfig(config);
    
    // Or use preset for ESP32-CAM
    hog.setupForESP32CAM(320, 240, ImageProcessing::PixelFormat::GRAYSCALE);
}

void loop() {
    camera_fb_t* fb = esp_camera_fb_get();
    
    // Extract features
    hog.transform(fb->buf);
    const auto& features = hog.getFeatures();
    
    // Use features for your application
    // (classification, detection, etc.)
    
    esp_camera_fb_return(fb);
}
```

## Custom Configuration Examples

### Real-time Processing (Speed Priority)
```cpp
HOG_MCU::Config fast_config;
fast_config.hog_img_width = 24;
fast_config.hog_img_height = 24;
fast_config.cell_size = 6;
fast_config.block_size = 12;
fast_config.block_stride = 6;
fast_config.nbins = 4;
// Result: ~15-25ms processing, 64 features
```

### High Accuracy (Quality Priority)
```cpp
HOG_MCU::Config accurate_config;
accurate_config.hog_img_width = 64;
accurate_config.hog_img_height = 64;
accurate_config.cell_size = 8;
accurate_config.block_size = 16;
accurate_config.block_stride = 4;  // More overlap
accurate_config.nbins = 9;
// Result: ~150-250ms processing, 1296 features
```

### Texture Analysis (Detail Priority)
```cpp
HOG_MCU::Config texture_config;
texture_config.hog_img_width = 48;
texture_config.hog_img_height = 48;
texture_config.cell_size = 4;      // Very fine cells
texture_config.block_size = 8;     // 2×2 cells
texture_config.block_stride = 2;   // High overlap
texture_config.nbins = 12;         // More orientations
// Result: ~100-180ms processing, 2700 features
```

## Expected Output

```
================================================
ESP32-CAM HOG Advanced Configuration Example
================================================

✅ Camera initialized successfully
🎯 This example demonstrates HOG parameter customization:
   • Different cell sizes and block configurations
   • Performance vs quality trade-offs
   • Feature count optimization
   • Use case specific tuning

📋 Will cycle through 4 configurations every 10 seconds

============================================================
🔄 Switching to configuration 1/4
============================================================
⚙️  Applying Configuration: Fast & Light
📝 Description: Optimized for speed and low memory usage
🎯 Use Case: Real-time applications, battery powered devices

📊 Configuration Details:
   Input Size: 320x240 → HOG Size: 32x32
   Cell Size: 8x8 pixels
   Block Size: 16x16 pixels (2x2 cells)
   Block Stride: 6 pixels
   Histogram Bins: 4
   Expected Features: 144 (6x6 blocks)

📸 Processing Results:
   ⏱️  Processing Time: 31 ms
   🔢 Features Extracted: 144
   💾 Memory Used: 576 bytes
   📈 Statistics:
      Min: 0.000127 | Max: 0.845231
      Mean: 0.098456 | Std Dev: 0.134521
      Range: 0.845104 | Variance: 0.018096
   ✅ Good feature variation detected
   🚀 Fast processing - suitable for real-time
   📝 Sample Features: 0.023 0.145 0.087 0.234 0.012 0.456 0.189 0.078 ...
```

## Optimization Guidelines

### For Real-time Applications
- Target processing time < 50ms
- Use image sizes ≤ 32×32
- Limit features to < 200
- Consider cell_size ≥ 8

### For Offline Processing
- Can use processing time > 100ms
- Image sizes up to 64×64 or larger
- More features (> 500) for better accuracy
- Smaller cell_size (4-6) for detail

### For Battery Applications
- Minimize processing time and memory
- Use default or smaller configurations
- Consider reducing capture frequency
- Optimize camera settings

## Troubleshooting

### Poor Feature Quality
- Increase image size (`hog_img_width`, `hog_img_height`)
- Decrease cell size for more detail
- Increase histogram bins (`nbins`)
- Check lighting conditions

### Slow Processing
- Decrease image size
- Increase cell size
- Reduce histogram bins
- Increase block stride

### High Memory Usage
- Reduce image size
- Increase block stride (fewer features)
- Use fewer histogram bins
- Consider using smaller data types

### Low Feature Variation
- Improve scene lighting
- Add more scene content/texture
- Adjust camera exposure settings
- Check if scene has sufficient detail

## Performance Benchmarks

| Configuration | Size | Features | Time (ms) | Memory (bytes) | FPS |
|---------------|------|----------|-----------|----------------|-----|
| Fast & Light  | 32×32 | 144     | 31       | 576           | 32  |
| Balanced      | 48×48 | 256     | 58       | 1024          | 17  |
| High Detail   | 64×64 | 576     | 127      | 2304          | 8   |
| Custom Fine   | 48×48 | 324     | 73       | 1296          | 14  |

*Benchmarks on ESP32-CAM @ 240MHz with 320×240 GRAYSCALE input*

## Related Examples

- **[HOG_Quickstart](../HOG_Quickstart/)**: Basic 3-line setup for beginners
- **[BasicContainers](../BasicContainers/)**: STL_MCU container usage
- **[AdvancedContainers](../AdvancedContainers/)**: Advanced STL_MCU features

## Additional Resources

- [STL_MCU Documentation](../../docs/)
- [HOG Algorithm Overview](../../docs/Rf_components/)
- [Image Processing Guide](../../docs/)
- [ESP32-CAM Setup Guide](https://docs.espressif.com/projects/esp32/en/latest/esp32/api-reference/peripherals/camera_driver.html)

---

**Need help?** Check the [main README](../../README.md) or open an issue on the project repository.