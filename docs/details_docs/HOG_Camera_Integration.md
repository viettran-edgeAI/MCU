# HOG Camera Integration Guide

## Overview

The HOG_MCU library now provides standardized camera frame size support for easier integration with ESP32-CAM and other camera modules.

## New Features

### 1. CameraFrameSize Enum

A new enum `ImageProcessing::CameraFrameSize` provides standard camera resolutions:

```cpp
enum class CameraFrameSize {
    FRAMESIZE_96X96,      // 96x96
    FRAMESIZE_QQVGA,      // 160x120
    FRAMESIZE_QCIF,       // 176x144
    FRAMESIZE_HQVGA,      // 240x176
    FRAMESIZE_240X240,    // 240x240
    FRAMESIZE_QVGA,       // 320x240
    FRAMESIZE_CIF,        // 400x296
    FRAMESIZE_HVGA,       // 480x320
    FRAMESIZE_VGA,        // 640x480
    FRAMESIZE_SVGA,       // 800x600
    FRAMESIZE_XGA,        // 1024x768
    FRAMESIZE_HD,         // 1280x720
    FRAMESIZE_SXGA,       // 1280x1024
    FRAMESIZE_UXGA        // 1600x1200
};
```

### 2. Enhanced setupForESP32CAM Method

New overload that accepts `CameraFrameSize`:

```cpp
void setupForESP32CAM(ImageProcessing::CameraFrameSize framesize,
                     ImageProcessing::PixelFormat input_format = ImageProcessing::PixelFormat::GRAYSCALE);
```

### 3. Helper Functions

**getFrameSizeDimensions()**: Convert CameraFrameSize to width/height:

```cpp
int width, height;
ImageProcessing::getFrameSizeDimensions(ImageProcessing::CameraFrameSize::FRAMESIZE_QVGA, width, height);
// width = 320, height = 240
```

## Usage Examples

### Example 1: Using CameraFrameSize Enum

```cpp
#include "hog_mcu/hog_transform.h"

HOG_MCU hog;

void setup() {
    // Setup using standard frame size
    hog.setupForESP32CAM(ImageProcessing::CameraFrameSize::FRAMESIZE_QVGA, 
                         ImageProcessing::PixelFormat::GRAYSCALE);
}
```

### Example 2: Traditional Width/Height Method

```cpp
HOG_MCU hog;

void setup() {
    // Setup using explicit dimensions
    hog.setupForESP32CAM(ImageProcessing::PixelFormat::GRAYSCALE, 320, 240);
}
```

### Example 3: ESP32-CAM Integration

```cpp
#include "esp_camera.h"
#include "hog_mcu/hog_transform.h"

HOG_MCU hog;

// Helper function to convert ImageProcessing enum to ESP32 camera enum
framesize_t getESP32FrameSize(int width, int height) {
    if (width == 96 && height == 96) return FRAMESIZE_96X96;
    if (width == 160 && height == 120) return FRAMESIZE_QQVGA;
    if (width == 240 && height == 176) return FRAMESIZE_HQVGA;
    if (width == 320 && height == 240) return FRAMESIZE_QVGA;
    if (width == 640 && height == 480) return FRAMESIZE_VGA;
    // Add more as needed...
    return FRAMESIZE_QVGA; // Default
}

pixformat_t getESP32PixelFormat(ImageProcessing::PixelFormat format) {
    switch (format) {
        case ImageProcessing::PixelFormat::GRAYSCALE: return PIXFORMAT_GRAYSCALE;
        case ImageProcessing::PixelFormat::RGB565:    return PIXFORMAT_RGB565;
        case ImageProcessing::PixelFormat::RGB888:    return PIXFORMAT_RGB888;
        case ImageProcessing::PixelFormat::YUV422:    return PIXFORMAT_YUV422;
        case ImageProcessing::PixelFormat::JPEG:      return PIXFORMAT_JPEG;
        default: return PIXFORMAT_GRAYSCALE;
    }
}

bool initCamera() {
    // Configure HOG first
    hog.setupForESP32CAM(ImageProcessing::CameraFrameSize::FRAMESIZE_QVGA,
                         ImageProcessing::PixelFormat::GRAYSCALE);
    
    // Get HOG config to configure camera
    const auto& cfg = hog.getImageProcessingConfig();
    
    // Setup ESP32 camera with matching settings
    camera_config_t camera_config = {};
    // ... set pin configurations ...
    
    camera_config.pixel_format = getESP32PixelFormat(cfg.input_format);
    camera_config.frame_size = getESP32FrameSize(cfg.input_width, cfg.input_height);
    camera_config.fb_count = 2;
    
    esp_err_t err = esp_camera_init(&camera_config);
    return (err == ESP_OK);
}

void loop() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        // Transform camera frame to HOG features
        hog.transform(fb->buf);
        
        // Get features
        const auto& features = hog.getFeatures();
        
        // Use features for ML inference...
        
        esp_camera_fb_return(fb);
    }
}
```

## Benefits

1. **Type Safety**: Enum-based configuration prevents invalid resolution combinations
2. **Clarity**: Named constants are more readable than numeric values
3. **Consistency**: Guaranteed alignment between HOG and camera configurations
4. **Simplicity**: Less boilerplate code for common use cases
5. **Compatibility**: Works seamlessly with ESP32-CAM framesize_t enums

## Migration from Old Code

### Before:
```cpp
hog.setupForESP32CAM(ImageProcessing::PixelFormat::GRAYSCALE, 320, 240);
framesize_t frameSize = FRAMESIZE_QVGA; // Manually defined
```

### After:
```cpp
hog.setupForESP32CAM(ImageProcessing::CameraFrameSize::FRAMESIZE_QVGA,
                     ImageProcessing::PixelFormat::GRAYSCALE);
const auto& cfg = hog.getImageProcessingConfig();
framesize_t frameSize = getESP32FrameSize(cfg.input_width, cfg.input_height);
```

## Notes

- The helper functions ensure consistent mapping between library enums and ESP32 camera enums
- All standard ESP32-CAM resolutions are supported
- Default fallback is QVGA (320x240) if an unsupported resolution is specified
- The camera frame size shown in the web stream will always match the configured input size
