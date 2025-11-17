#pragma once

#include <Arduino.h>
#include "STL_MCU.h"

namespace ImageProcessing {

    // Supported image formats
    enum class PixelFormat {
        GRAYSCALE,          // 8-bit grayscale
        RGB565,             // 16-bit RGB565
        RGB888,             // 24-bit RGB888
        YUV422,             // YUV422 format
        JPEG                // JPEG compressed
    };

    // Resize methods
    enum class ResizeMethod {
        NEAREST_NEIGHBOR,   // Fast, lower quality
        BILINEAR,          // Good balance of speed and quality
        AREA_AVERAGE       // Good for downsampling
    };

    // Standard camera frame sizes (compatible with ESP32-CAM)
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

    // Helper function to get width and height from CameraFrameSize
    inline void getFrameSizeDimensions(CameraFrameSize framesize, int& width, int& height) {
        switch (framesize) {
            case CameraFrameSize::FRAMESIZE_96X96:    width = 96;   height = 96;   break;
            case CameraFrameSize::FRAMESIZE_QQVGA:    width = 160;  height = 120;  break;
            case CameraFrameSize::FRAMESIZE_QCIF:     width = 176;  height = 144;  break;
            case CameraFrameSize::FRAMESIZE_HQVGA:    width = 240;  height = 176;  break;
            case CameraFrameSize::FRAMESIZE_240X240:  width = 240;  height = 240;  break;
            case CameraFrameSize::FRAMESIZE_QVGA:     width = 320;  height = 240;  break;
            case CameraFrameSize::FRAMESIZE_CIF:      width = 400;  height = 296;  break;
            case CameraFrameSize::FRAMESIZE_HVGA:     width = 480;  height = 320;  break;
            case CameraFrameSize::FRAMESIZE_VGA:      width = 640;  height = 480;  break;
            case CameraFrameSize::FRAMESIZE_SVGA:     width = 800;  height = 600;  break;
            case CameraFrameSize::FRAMESIZE_XGA:      width = 1024; height = 768;  break;
            case CameraFrameSize::FRAMESIZE_HD:       width = 1280; height = 720;  break;
            case CameraFrameSize::FRAMESIZE_SXGA:     width = 1280; height = 1024; break;
            case CameraFrameSize::FRAMESIZE_UXGA:     width = 1600; height = 1200; break;
            default:                                   width = 320;  height = 240;  break;
        }
    }

    // Configuration structure for image processing
    struct ProcessingConfig {
        PixelFormat input_format;
        PixelFormat output_format;
        int input_width;
        int input_height;
        int output_width;
        int output_height;
        ResizeMethod resize_method;
        bool maintain_aspect_ratio;
        uint8_t jpeg_quality;  // For JPEG input/output (0-100)
        
        // Default constructor
        ProcessingConfig() :
            input_format(PixelFormat::GRAYSCALE),
            output_format(PixelFormat::GRAYSCALE),
            input_width(320),
            input_height(240),
            output_width(32),
            output_height(32),
            resize_method(ResizeMethod::BILINEAR),
            maintain_aspect_ratio(false),
            jpeg_quality(80) {}
    };

    /**
     * Convert RGB565 to grayscale
     * @param rgb565_buffer: Input RGB565 buffer
     * @param width: Image width
     * @param height: Image height
     * @param grayscale_buffer: Output grayscale buffer (must be pre-allocated)
     * @return: Success status
     */
    bool rgb565ToGrayscale(const uint16_t* rgb565_buffer, int width, int height, uint8_t* grayscale_buffer);

    /**
     * Convert RGB888 to grayscale
     * @param rgb888_buffer: Input RGB888 buffer
     * @param width: Image width
     * @param height: Image height
     * @param grayscale_buffer: Output grayscale buffer (must be pre-allocated)
     * @return: Success status
     */
    bool rgb888ToGrayscale(const uint8_t* rgb888_buffer, int width, int height, uint8_t* grayscale_buffer);

    /**
     * Convert YUV422 to grayscale (extracts Y channel)
     * @param yuv422_buffer: Input YUV422 buffer
     * @param width: Image width
     * @param height: Image height
     * @param grayscale_buffer: Output grayscale buffer (must be pre-allocated)
     * @return: Success status
     */
    bool yuv422ToGrayscale(const uint8_t* yuv422_buffer, int width, int height, uint8_t* grayscale_buffer);

    /**
     * Resize image using nearest neighbor interpolation
     * @param input_buffer: Input image buffer
     * @param input_width: Input image width
     * @param input_height: Input image height
     * @param output_buffer: Output image buffer (must be pre-allocated)
     * @param output_width: Output image width
     * @param output_height: Output image height
     * @return: Success status
     */
    bool resizeNearestNeighbor(const uint8_t* input_buffer, int input_width, int input_height,
                              uint8_t* output_buffer, int output_width, int output_height);

    /**
     * Resize image using bilinear interpolation
     * @param input_buffer: Input image buffer
     * @param input_width: Input image width
     * @param input_height: Input image height
     * @param output_buffer: Output image buffer (must be pre-allocated)
     * @param output_width: Output image width
     * @param output_height: Output image height
     * @return: Success status
     */
    bool resizeBilinear(const uint8_t* input_buffer, int input_width, int input_height,
                       uint8_t* output_buffer, int output_width, int output_height);

    /**
     * Resize image using area averaging (good for downsampling)
     * @param input_buffer: Input image buffer
     * @param input_width: Input image width
     * @param input_height: Input image height
     * @param output_buffer: Output image buffer (must be pre-allocated)
     * @param output_width: Output image width
     * @param output_height: Output image height
     * @return: Success status
     */
    bool resizeAreaAverage(const uint8_t* input_buffer, int input_width, int input_height,
                          uint8_t* output_buffer, int output_width, int output_height);

    /**
     * Generic image processing function that handles format conversion and resizing
     * @param input_buffer: Input image buffer
     * @param config: Processing configuration
     * @param output_buffer: Output image buffer (must be pre-allocated)
     * @return: Success status
     */
    bool processImage(const void* input_buffer, const ProcessingConfig& config, uint8_t* output_buffer);

    /**
     * Calculate output buffer size needed for given configuration
     * @param config: Processing configuration
     * @return: Required buffer size in bytes
     */
    size_t calculateOutputBufferSize(const ProcessingConfig& config);

    /**
     * Validate processing configuration
     * @param config: Configuration to validate
     * @return: True if configuration is valid
     */
    bool validateConfig(const ProcessingConfig& config);

    /**
     * Extract RGB components from RGB565 pixel
     * @param rgb565: RGB565 pixel value
     * @param r: Output red component (0-255)
     * @param g: Output green component (0-255)
     * @param b: Output blue component (0-255)
     */
    inline void extractRGB565(uint16_t rgb565, uint8_t& r, uint8_t& g, uint8_t& b) {
        r = ((rgb565 >> 11) & 0x1F) << 3;  // 5 bits -> 8 bits
        g = ((rgb565 >> 5) & 0x3F) << 2;   // 6 bits -> 8 bits
        b = (rgb565 & 0x1F) << 3;          // 5 bits -> 8 bits
    }

    /**
     * Convert RGB to grayscale using standard luminance formula
     * @param r: Red component (0-255)
     * @param g: Green component (0-255)
     * @param b: Blue component (0-255)
     * @return: Grayscale value (0-255)
     */
    inline uint8_t rgbToGrayscale(uint8_t r, uint8_t g, uint8_t b) {
        // Standard luminance formula: Y = 0.299*R + 0.587*G + 0.114*B
        // Using integer arithmetic for efficiency
        return (77 * r + 150 * g + 29 * b) >> 8;
    }

    /**
     * Clamp value to specified range
     * @param value: Input value
     * @param min_val: Minimum value
     * @param max_val: Maximum value
     * @return: Clamped value
     */
    template<typename T>
    inline T clamp(T value, T min_val, T max_val) {
        return (value < min_val) ? min_val : ((value > max_val) ? max_val : value);
    }

} // namespace ImageProcessing