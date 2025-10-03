#include "image_processing.h"
#include <math.h>

namespace ImageProcessing {

bool rgb565ToGrayscale(const uint16_t* rgb565_buffer, int width, int height, uint8_t* grayscale_buffer) {
    if (!rgb565_buffer || !grayscale_buffer || width <= 0 || height <= 0) {
        return false;
    }

    for (int i = 0; i < width * height; i++) {
        uint8_t r, g, b;
        extractRGB565(rgb565_buffer[i], r, g, b);
        grayscale_buffer[i] = rgbToGrayscale(r, g, b);
    }
    
    return true;
}

bool rgb888ToGrayscale(const uint8_t* rgb888_buffer, int width, int height, uint8_t* grayscale_buffer) {
    if (!rgb888_buffer || !grayscale_buffer || width <= 0 || height <= 0) {
        return false;
    }

    for (int i = 0; i < width * height; i++) {
        uint8_t r = rgb888_buffer[i * 3];
        uint8_t g = rgb888_buffer[i * 3 + 1];
        uint8_t b = rgb888_buffer[i * 3 + 2];
        grayscale_buffer[i] = rgbToGrayscale(r, g, b);
    }
    
    return true;
}

bool yuv422ToGrayscale(const uint8_t* yuv422_buffer, int width, int height, uint8_t* grayscale_buffer) {
    if (!yuv422_buffer || !grayscale_buffer || width <= 0 || height <= 0) {
        return false;
    }

    // YUV422 format: Y0 U0 Y1 V0 (4 bytes for 2 pixels)
    // We extract only the Y (luminance) component
    for (int i = 0; i < width * height; i++) {
        int yuv_index = (i / 2) * 4 + (i % 2) * 2;  // Calculate YUV buffer index
        grayscale_buffer[i] = yuv422_buffer[yuv_index];  // Extract Y component
    }
    
    return true;
}

bool resizeNearestNeighbor(const uint8_t* input_buffer, int input_width, int input_height,
                          uint8_t* output_buffer, int output_width, int output_height) {
    if (!input_buffer || !output_buffer || 
        input_width <= 0 || input_height <= 0 || 
        output_width <= 0 || output_height <= 0) {
        return false;
    }

    float scale_x = (float)input_width / output_width;
    float scale_y = (float)input_height / output_height;

    for (int y = 0; y < output_height; y++) {
        for (int x = 0; x < output_width; x++) {
            int src_x = clamp((int)(x * scale_x), 0, input_width - 1);
            int src_y = clamp((int)(y * scale_y), 0, input_height - 1);
            
            output_buffer[y * output_width + x] = input_buffer[src_y * input_width + src_x];
        }
    }
    
    return true;
}

bool resizeBilinear(const uint8_t* input_buffer, int input_width, int input_height,
                   uint8_t* output_buffer, int output_width, int output_height) {
    if (!input_buffer || !output_buffer || 
        input_width <= 0 || input_height <= 0 || 
        output_width <= 0 || output_height <= 0) {
        return false;
    }

    float scale_x = (float)input_width / output_width;
    float scale_y = (float)input_height / output_height;

    for (int y = 0; y < output_height; y++) {
        for (int x = 0; x < output_width; x++) {
            float src_x = x * scale_x;
            float src_y = y * scale_y;
            
            int x1 = clamp((int)src_x, 0, input_width - 1);
            int y1 = clamp((int)src_y, 0, input_height - 1);
            int x2 = clamp(x1 + 1, 0, input_width - 1);
            int y2 = clamp(y1 + 1, 0, input_height - 1);
            
            float fx = src_x - x1;
            float fy = src_y - y1;
            
            // Get four corner pixels
            uint8_t p11 = input_buffer[y1 * input_width + x1];
            uint8_t p12 = input_buffer[y1 * input_width + x2];
            uint8_t p21 = input_buffer[y2 * input_width + x1];
            uint8_t p22 = input_buffer[y2 * input_width + x2];
            
            // Bilinear interpolation
            float val = p11 * (1 - fx) * (1 - fy) +
                       p12 * fx * (1 - fy) +
                       p21 * (1 - fx) * fy +
                       p22 * fx * fy;
            
            output_buffer[y * output_width + x] = clamp((uint8_t)val, (uint8_t)0, (uint8_t)255);
        }
    }
    
    return true;
}

bool resizeAreaAverage(const uint8_t* input_buffer, int input_width, int input_height,
                      uint8_t* output_buffer, int output_width, int output_height) {
    if (!input_buffer || !output_buffer || 
        input_width <= 0 || input_height <= 0 || 
        output_width <= 0 || output_height <= 0) {
        return false;
    }

    float scale_x = (float)input_width / output_width;
    float scale_y = (float)input_height / output_height;

    for (int y = 0; y < output_height; y++) {
        for (int x = 0; x < output_width; x++) {
            float start_x = x * scale_x;
            float start_y = y * scale_y;
            float end_x = (x + 1) * scale_x;
            float end_y = (y + 1) * scale_y;
            
            int x1 = clamp((int)start_x, 0, input_width - 1);
            int y1 = clamp((int)start_y, 0, input_height - 1);
            int x2 = clamp((int)end_x, 0, input_width - 1);
            int y2 = clamp((int)end_y, 0, input_height - 1);
            
            uint32_t sum = 0;
            int count = 0;
            
            // Average all pixels in the area
            for (int sy = y1; sy <= y2; sy++) {
                for (int sx = x1; sx <= x2; sx++) {
                    sum += input_buffer[sy * input_width + sx];
                    count++;
                }
            }
            
            output_buffer[y * output_width + x] = count > 0 ? (sum / count) : 0;
        }
    }
    
    return true;
}

bool processImage(const void* input_buffer, const ProcessingConfig& config, uint8_t* output_buffer) {
    if (!input_buffer || !output_buffer || !validateConfig(config)) {
        return false;
    }

    // Temporary buffer for format conversion if needed
    uint8_t* temp_grayscale = nullptr;
    const uint8_t* grayscale_source = nullptr;
    
    // Step 1: Convert to grayscale if needed
    if (config.input_format == PixelFormat::GRAYSCALE) {
        grayscale_source = (const uint8_t*)input_buffer;
    } else {
        // Allocate temporary buffer for format conversion
        size_t temp_size = config.input_width * config.input_height;
        temp_grayscale = new uint8_t[temp_size];
        if (!temp_grayscale) {
            return false;
        }
        
        bool conversion_success = false;
        switch (config.input_format) {
            case PixelFormat::RGB565:
                conversion_success = rgb565ToGrayscale((const uint16_t*)input_buffer, 
                                                     config.input_width, config.input_height, temp_grayscale);
                break;
            case PixelFormat::RGB888:
                conversion_success = rgb888ToGrayscale((const uint8_t*)input_buffer, 
                                                     config.input_width, config.input_height, temp_grayscale);
                break;
            case PixelFormat::YUV422:
                conversion_success = yuv422ToGrayscale((const uint8_t*)input_buffer, 
                                                     config.input_width, config.input_height, temp_grayscale);
                break;
            default:
                delete[] temp_grayscale;
                return false;
        }
        
        if (!conversion_success) {
            delete[] temp_grayscale;
            return false;
        }
        
        grayscale_source = temp_grayscale;
    }
    
    // Step 2: Resize if needed
    bool resize_success = true;
    if (config.input_width != config.output_width || config.input_height != config.output_height) {
        switch (config.resize_method) {
            case ResizeMethod::NEAREST_NEIGHBOR:
                resize_success = resizeNearestNeighbor(grayscale_source, config.input_width, config.input_height,
                                                     output_buffer, config.output_width, config.output_height);
                break;
            case ResizeMethod::BILINEAR:
                resize_success = resizeBilinear(grayscale_source, config.input_width, config.input_height,
                                              output_buffer, config.output_width, config.output_height);
                break;
            case ResizeMethod::AREA_AVERAGE:
                resize_success = resizeAreaAverage(grayscale_source, config.input_width, config.input_height,
                                                 output_buffer, config.output_width, config.output_height);
                break;
            default:
                resize_success = false;
                break;
        }
    } else {
        // No resizing needed, just copy
        memcpy(output_buffer, grayscale_source, config.output_width * config.output_height);
    }
    
    // Clean up temporary buffer
    if (temp_grayscale) {
        delete[] temp_grayscale;
    }
    
    return resize_success;
}

size_t calculateOutputBufferSize(const ProcessingConfig& config) {
    switch (config.output_format) {
        case PixelFormat::GRAYSCALE:
            return config.output_width * config.output_height;
        case PixelFormat::RGB565:
            return config.output_width * config.output_height * 2;
        case PixelFormat::RGB888:
            return config.output_width * config.output_height * 3;
        case PixelFormat::YUV422:
            return config.output_width * config.output_height * 2;
        default:
            return 0;
    }
}

bool validateConfig(const ProcessingConfig& config) {
    // Check dimensions
    if (config.input_width <= 0 || config.input_height <= 0 ||
        config.output_width <= 0 || config.output_height <= 0) {
        return false;
    }
    
    // Check if dimensions are reasonable (avoid excessive memory usage)
    if (config.input_width > 4096 || config.input_height > 4096 ||
        config.output_width > 1024 || config.output_height > 1024) {
        return false;
    }
    
    // Check JPEG quality range
    if (config.jpeg_quality > 100) {
        return false;
    }
    
    return true;
}

} // namespace ImageProcessing