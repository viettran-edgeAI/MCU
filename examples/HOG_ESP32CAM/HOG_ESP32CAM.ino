/*
 * ESP32-CAM HOG Feature Extraction Example
 * =========================================
 * 
 * Complete example for HOG feature extraction on ESP32-CAM.
 * Supports both quick setup and advanced configuration options.
 * 
 * Features:
 * ✓ Simple 3-line setup with optimal defaults
 * ✓ Load configuration from JSON files (recommended)
 * ✓ Custom parameter configuration
 * ✓ Performance metrics and feature analysis
 * ✓ Ready for machine learning integration
 * 
 * Hardware: ESP32-CAM with OV2640 camera module
 * Power: 5V/2A recommended
 * Storage: LittleFS or SD card (optional, for config files)
 * 
 * Configuration Modes:
 * 1. QUICK_START    - Minimal setup with defaults (fastest to get started)
 * 2. CONFIG_FILE    - Load from JSON file (recommended for production)
 * 3. CUSTOM_PARAMS  - Manual parameter configuration (advanced users)
 * 
 * Author: STL_MCU Library Team
 */

#include <Arduino.h>
#include "esp_camera.h"
#include "STL_MCU.h"
#include "hog_transform.h"
#include "Rf_file_manager.h"

// === CONFIGURATION MODE ===
// Choose ONE mode by uncommenting the corresponding line:
#define MODE_QUICK_START      // Simple setup with optimal defaults
// #define MODE_CONFIG_FILE   // Load from JSON config file
// #define MODE_CUSTOM_PARAMS // Custom parameter configuration

// === STORAGE CONFIGURATION ===
const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;  // Change to FLASH if no SD card

// === CONFIG FILE PATH (for MODE_CONFIG_FILE) ===
const char* CONFIG_FILE_PATH = "/dataset_features_hogcfg.json";

// === CAPTURE SETTINGS ===
const uint32_t CAPTURE_INTERVAL_MS = 3000;  // Time between captures
const bool SHOW_FULL_FEATURES = false;       // Print all feature values (can be verbose)
const bool SHOW_STATISTICS = true;           // Show feature statistics

// === ESP32-CAM PIN DEFINITIONS (AI-Thinker Model) ===
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Global HOG processor
HOG_MCU hog;

// === FUNCTION DECLARATIONS ===
bool initCamera();
bool initCameraWithConfig(const ImageProcessing::ProcessingConfig& imgCfg);
void setupHOGConfiguration();
void printConfigurationInfo();
void processAndDisplayFeatures();
void printFeatureStatistics(const mcu::b_vector<float, 144>& features);
framesize_t mapFrameSize(int width, int height);
pixformat_t mapPixelFormat(ImageProcessing::PixelFormat format);
const char* pixelFormatToString(ImageProcessing::PixelFormat format);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=====================================");
    Serial.println("ESP32-CAM HOG Feature Extraction");
    Serial.println("=====================================\n");
    
    // Setup HOG configuration based on selected mode
    setupHOGConfiguration();
    
    // Initialize camera with appropriate settings
#ifdef MODE_CONFIG_FILE
    const auto& imgCfg = hog.getImageProcessingConfig();
    if (!initCameraWithConfig(imgCfg)) {
#else
    if (!initCamera()) {
#endif
        Serial.println("❌ Camera initialization failed!");
        Serial.println("Check power supply (5V/2A) and connections");
        while(1) delay(1000);
    }
    Serial.println("✅ Camera initialized");
    
    // Print configuration summary
    printConfigurationInfo();
    
    Serial.printf("\n🚀 Ready! Capturing every %lu ms...\n\n", CAPTURE_INTERVAL_MS);
}

void loop() {
    processAndDisplayFeatures();
    delay(CAPTURE_INTERVAL_MS);
}

void setupHOGConfiguration() {
#ifdef MODE_QUICK_START
    // ============================================
    // MODE 1: QUICK START - Minimal Setup
    // ============================================
    Serial.println("Configuration Mode: QUICK START");
    Serial.println("Using optimal default parameters\n");
    
    // One-line setup with defaults:
    // - GRAYSCALE input format
    // - 320x240 camera resolution
    // - 32x32 HOG processing size
    // - Standard HOG parameters
    hog.setupForESP32CAM();
    
    Serial.println("✅ HOG configured with defaults");

#elif defined(MODE_CONFIG_FILE)
    // ============================================
    // MODE 2: CONFIG FILE - Production Workflow
    // ============================================
    Serial.println("Configuration Mode: CONFIG FILE");
    Serial.printf("Config path: %s\n\n", CONFIG_FILE_PATH);
    
    // Initialize filesystem
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("⚠️  Failed to mount filesystem!");
        Serial.println("Falling back to default configuration...\n");
        hog.setupForESP32CAM();
    } else {
        Serial.printf("✅ Filesystem mounted (%s)\n", rf_storage_type());
        Serial.printf("Total: %lu bytes, Used: %lu bytes\n", RF_TOTAL_BYTES(), RF_USED_BYTES());
        
        // Load configuration from file
        if (hog.loadConfigFromFile(CONFIG_FILE_PATH)) {
            Serial.println("✅ Configuration loaded from file");
            Serial.printf("Feature CSV: %s\n", hog.getFeatureCsvPath().c_str());
            Serial.printf("Feature file: %s\n", hog.getFeatureFileName().c_str());
        } else {
            Serial.printf("⚠️  Failed to load config: %s\n", CONFIG_FILE_PATH);
            Serial.println("Falling back to default configuration...\n");
            hog.setupForESP32CAM();
        }
    }

#elif defined(MODE_CUSTOM_PARAMS)
    // ============================================
    // MODE 3: CUSTOM PARAMETERS - Advanced Config
    // ============================================
    Serial.println("Configuration Mode: CUSTOM PARAMETERS");
    Serial.println("Using custom HOG configuration\n");
    
    // Create custom configuration
    HOG_MCU::Config customConfig;
    
    // Image processing settings
    customConfig.input_format = ImageProcessing::PixelFormat::GRAYSCALE;
    customConfig.input_width = 320;
    customConfig.input_height = 240;
    customConfig.resize_method = ImageProcessing::ResizeMethod::NEAREST_NEIGHBOR;
    customConfig.maintain_aspect_ratio = false;
    
    // HOG processing settings
    customConfig.hog_img_width = 32;      // Processing resolution
    customConfig.hog_img_height = 32;
    customConfig.cell_size = 8;           // Cell size in pixels
    customConfig.block_size = 16;         // Block size (2x2 cells)
    customConfig.block_stride = 6;        // Overlap between blocks
    customConfig.nbins = 4;               // Orientation bins
    
    // Apply custom configuration
    hog.setup(customConfig);
    
    Serial.println("✅ HOG configured with custom parameters");
    Serial.printf("Cell size: %d, Block: %d, Stride: %d, Bins: %d\n",
                  customConfig.cell_size, customConfig.block_size, 
                  customConfig.block_stride, customConfig.nbins);
#else
    #error "No configuration mode selected! Define one of: MODE_QUICK_START, MODE_CONFIG_FILE, MODE_CUSTOM_PARAMS"
#endif
}

void printConfigurationInfo() {
    Serial.println("\n--- Configuration Summary ---");
    
    const auto& cfg = hog.getImageProcessingConfig();
    Serial.printf("Input format: %s\n", pixelFormatToString(cfg.input_format));
    Serial.printf("Camera input: %dx%d\n", cfg.input_width, cfg.input_height);
    Serial.printf("HOG processing: %dx%d\n", cfg.output_width, cfg.output_height);
    Serial.printf("Feature count: %d\n", hog.getFeatures().capacity());
    
    Serial.println("-----------------------------");
}

void processAndDisplayFeatures() {
    // Capture image from camera
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Frame capture failed");
        return;
    }
    
    // Extract HOG features
    unsigned long startTime = millis();
    hog.transform(fb->buf);
    unsigned long processingTime = millis() - startTime;
    
    // Get feature vector
    const auto& features = hog.getFeatures();
    
    // Display basic info
    Serial.printf("📸 Frame: %dx%d | ⏱️  %lu ms | 🔢 Features: %d\n", 
                  fb->width, fb->height, processingTime, features.size());
    
    // Show feature values
    if (SHOW_FULL_FEATURES) {
        Serial.print("Features: [");
        for (int i = 0; i < features.size(); ++i) {
            Serial.printf("%.6f", features[i]);
            if (i + 1 < features.size()) Serial.print(", ");
        }
        Serial.println("]\n");
    } else {
        // Show first 8 features as preview
        Serial.print("Features preview: ");
        for (int i = 0; i < min(8, (int)features.size()); i++) {
            Serial.printf("%.3f ", features[i]);
        }
        Serial.println("...");
    }
    
    // Show statistics if enabled
    if (SHOW_STATISTICS) {
        printFeatureStatistics(features);
    }
    
    // ============================================
    // 🎯 YOUR MACHINE LEARNING CODE GOES HERE!
    // ============================================
    // Example integration with random forest:
    // float prediction = myForest.predict(features.data(), features.size());
    // Serial.printf("Prediction: %.2f\n", prediction);
    //
    // Example with classification:
    // int classLabel = classifier.classify(features.data(), features.size());
    // Serial.printf("Detected class: %d\n", classLabel);
    
    Serial.println();
    esp_camera_fb_return(fb);
}

void printFeatureStatistics(const mcu::b_vector<float, 144>& features) {
    if (features.empty()) return;
    
    float minVal = features[0];
    float maxVal = features[0];
    float sum = 0.0f;
    int zeroCount = 0;
    
    for (const auto& val : features) {
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
        sum += val;
        if (val < 0.0001f) zeroCount++;
    }
    
    float mean = sum / features.size();
    
    Serial.printf("Stats: min=%.3f, max=%.3f, mean=%.3f, zeros=%d/%d\n",
                  minVal, maxVal, mean, zeroCount, features.size());
}

bool initCamera() {
    camera_config_t config;
    
    // Pin configuration
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    // Camera settings - optimized for HOG
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
    
    // Initialize camera
    if (esp_camera_init(&config) != ESP_OK) {
        return false;
    }
    
    // Optimize sensor settings
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_whitebal(s, 1);
    }
    
    return true;
}

bool initCameraWithConfig(const ImageProcessing::ProcessingConfig& imgCfg) {
    camera_config_t config;
    
    // Pin configuration
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    
    // Camera settings from config
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = mapPixelFormat(imgCfg.input_format);
    config.frame_size = mapFrameSize(imgCfg.input_width, imgCfg.input_height);
    config.jpeg_quality = (config.pixel_format == PIXFORMAT_JPEG) 
                         ? constrain(imgCfg.jpeg_quality, 10, 63) : 12;
    config.fb_count = 1;
    
    // Initialize camera
    if (esp_camera_init(&config) != ESP_OK) {
        return false;
    }
    
    // Optimize sensor settings
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_whitebal(s, 1);
    }
    
    return true;
}

framesize_t mapFrameSize(int width, int height) {
    if (width == 96 && height == 96) return FRAMESIZE_96X96;
    if (width == 160 && height == 120) return FRAMESIZE_QQVGA;
    if (width == 176 && height == 144) return FRAMESIZE_QCIF;
    if (width == 320 && height == 240) return FRAMESIZE_QVGA;
    if (width == 352 && height == 288) return FRAMESIZE_CIF;
    if (width == 480 && height == 320) return FRAMESIZE_HVGA;
    if (width == 640 && height == 480) return FRAMESIZE_VGA;
    if (width == 800 && height == 600) return FRAMESIZE_SVGA;
    if (width == 1024 && height == 768) return FRAMESIZE_XGA;
    if (width == 1280 && height == 720) return FRAMESIZE_HD;
    if (width == 1280 && height == 1024) return FRAMESIZE_SXGA;
    if (width == 1600 && height == 1200) return FRAMESIZE_UXGA;
    if (width == 1920 && height == 1080) return FRAMESIZE_FHD;
    if (width == 2048 && height == 1536) return FRAMESIZE_QXGA;
    Serial.printf("⚠️  Unsupported frame size %dx%d, using 320x240\n", width, height);
    return FRAMESIZE_QVGA;
}

pixformat_t mapPixelFormat(ImageProcessing::PixelFormat format) {
    switch (format) {
        case ImageProcessing::PixelFormat::GRAYSCALE:
            return PIXFORMAT_GRAYSCALE;
        case ImageProcessing::PixelFormat::RGB565:
            return PIXFORMAT_RGB565;
        case ImageProcessing::PixelFormat::RGB888:
            return PIXFORMAT_RGB888;
        case ImageProcessing::PixelFormat::YUV422:
            return PIXFORMAT_YUV422;
        case ImageProcessing::PixelFormat::JPEG:
            return PIXFORMAT_JPEG;
        default:
            Serial.println("⚠️  Unknown pixel format, using GRAYSCALE");
            return PIXFORMAT_GRAYSCALE;
    }
}

const char* pixelFormatToString(ImageProcessing::PixelFormat format) {
    switch (format) {
        case ImageProcessing::PixelFormat::GRAYSCALE: return "GRAYSCALE";
        case ImageProcessing::PixelFormat::RGB565: return "RGB565";
        case ImageProcessing::PixelFormat::RGB888: return "RGB888";
        case ImageProcessing::PixelFormat::YUV422: return "YUV422";
        case ImageProcessing::PixelFormat::JPEG: return "JPEG";
        default: return "UNKNOWN";
    }
}
