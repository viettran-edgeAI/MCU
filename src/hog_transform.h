#pragma once

#include <Arduino.h>
#include "STL_MCU.h"
#include "image_processing.h"

/**
 * HOG_MCU - Histogram of Oriented Gradients for Microcontrollers
 * ==============================================================
 * 
 * A memory-efficient implementation of HOG feature extraction optimized for
 * microcontrollers like ESP32. Includes integrated image processing for
 * format conversion and resizing.
 * 
 * Key Features:
 * - Automatic image format conversion (RGB565, RGB888, YUV422 → Grayscale)
 * - Intelligent resizing with multiple algorithms
 * - Fixed-size feature vectors using mcu::b_vector
 * - Optimized for embedded systems
 * - Easy configuration with sensible defaults
 * 
 * Quick Start:
 * ```cpp
 * HOG_MCU hog;
 * hog.setupForESP32CAM();              // Use defaults
 * hog.transform(camera_buffer);        // Extract features
 * const auto& features = hog.getFeatures();  // Use features
 * ```
 * 
 * Advanced Usage:
 * ```cpp
 * HOG_MCU::Config config;
 * config.input_format = ImageProcessing::PixelFormat::RGB565;
 * config.input_width = 640;
 * config.input_height = 480;
 * config.hog_img_width = 48;
 * hog.setConfig(config);
 * ```
 */

class HOG_MCU {
public:
    /**
     * HOG algorithm parameters
     */
    struct Params {
        int img_width;      // HOG input image width
        int img_height;     // HOG input image height  
        int cell_size;      // Size of each cell in pixels
        int block_size;     // Size of each block in pixels (usually 2x cell_size)
        int block_stride;   // Stride between blocks in pixels
        int nbins;          // Number of orientation histogram bins
    };

    /**
     * Unified configuration structure for easy setup
     * Combines image processing and HOG parameters in one place
     */
    struct Config {
        // Image processing settings
        ImageProcessing::PixelFormat input_format;  // Input image format
        int input_width;                           // Input image width
        int input_height;                          // Input image height
        ImageProcessing::ResizeMethod resize_method; // Resize algorithm
        
        // HOG parameters (optional - will use defaults if not specified)
        int hog_img_width;   // HOG processing width (default: 32)
        int hog_img_height;  // HOG processing height (default: 32)
        int cell_size;       // Cell size in pixels (default: 8)
        int block_size;      // Block size in pixels (default: 16)
        int block_stride;    // Block stride in pixels (default: 6)
        int nbins;           // Histogram bins (default: 4)
        
        /**
         * Default constructor with optimal settings for ESP32-CAM
         * - Input: 320x240 grayscale
         * - Output: 32x32 HOG processing
         * - ~144 features extracted
         */
        Config() :
            input_format(ImageProcessing::PixelFormat::GRAYSCALE),
            input_width(320),
            input_height(240),
            resize_method(ImageProcessing::ResizeMethod::BILINEAR),
            hog_img_width(32),
            hog_img_height(32),
            cell_size(8),
            block_size(16),
            block_stride(6),
            nbins(4) {}
            
        /**
         * Convenience constructor for simple cases
         * @param format Input pixel format
         * @param in_w Input image width
         * @param in_h Input image height
         */
        Config(ImageProcessing::PixelFormat format, int in_w, int in_h) :
            input_format(format),
            input_width(in_w),
            input_height(in_h),
            resize_method(ImageProcessing::ResizeMethod::BILINEAR),
            hog_img_width(32),
            hog_img_height(32),
            cell_size(8),
            block_size(16),
            block_stride(6),
            nbins(4) {}
    };

    /**
     * Default constructor
     * Uses optimal parameters for 32x32 pixel HOG processing
     */
    HOG_MCU();
    
    /**
     * Constructor with custom HOG parameters
     * @param p HOG parameters structure
     */
    HOG_MCU(const Params& p);
    
    /**
     * Destructor - automatically cleans up internal buffers
     */
    ~HOG_MCU();

    /**
     * Legacy configuration method for HOG parameters only
     * @param img_width HOG processing width
     * @param img_height HOG processing height  
     * @param cell_size Cell size in pixels
     * @param block_size Block size in pixels
     * @param block_stride Block stride in pixels
     * @param nbins Number of histogram bins
     */
    void set_config(int img_width, int img_height, int cell_size, int block_size, int block_stride, int nbins);
    
    /**
     * Unified configuration method - recommended for new code
     * Sets both image processing and HOG parameters in one call
     * @param config Complete configuration structure
     */
    void setConfig(const Config& config);
    
    /**
     * Quick setup method for ESP32-CAM with optimal defaults
     * Perfect for getting started quickly!
     * @param input_format Camera pixel format (default: GRAYSCALE)
     * @param input_width Camera image width (default: 320)
     * @param input_height Camera image height (default: 240)
     */
    void setupForESP32CAM(ImageProcessing::PixelFormat input_format = ImageProcessing::PixelFormat::GRAYSCALE,
                         int input_width = 320, int input_height = 240);
    
    /**
     * Set image processing configuration only
     * @param config Image processing configuration
     */
    void setImageProcessingConfig(const ImageProcessing::ProcessingConfig& config);
    
    /**
     * Transform camera buffer to HOG features (recommended method)
     * Automatically handles format conversion and resizing based on configuration
     * @param cameraBuffer Raw camera buffer (any supported format)
     */
    void transform(const void* cameraBuffer);
    
    /**
     * Transform pre-processed grayscale image to HOG features  
     * Use this if you've already converted to grayscale
     * @param grayscaleImage Grayscale image buffer
     */
    void transformGrayscale(const uint8_t* grayscaleImage);
    
    /**
     * Get computed HOG features
     * @return Reference to feature vector (up to 144 features)
     */
    const mcu::b_vector<float, 144>& getFeatures() const;
    
    /**
     * Get current image processing configuration
     * @return Current image processing settings
     */
    const ImageProcessing::ProcessingConfig& getImageProcessingConfig() const;

public:
    /**
     * Feature vector to store computed HOG descriptors
     * Fixed-size vector optimized for microcontrollers
     * Capacity: 144 features (suitable for 32x32 images with default parameters)
     */
    mcu::b_vector<float, 144> features;

private:
    Params params;
    ImageProcessing::ProcessingConfig img_config;
    uint8_t* processed_image_buffer;  // Internal buffer for processed image
    
    // Internal computation method
    void compute(const uint8_t* grayImage);
    
    // Helper methods for gradient computation
    float computeGradientMagnitude(int gx, int gy);
    float computeGradientAngle(int gx, int gy);
    
    // Initialize internal buffers
    void initializeBuffers();
    
    // Clean up internal buffers
    void cleanupBuffers();
};

