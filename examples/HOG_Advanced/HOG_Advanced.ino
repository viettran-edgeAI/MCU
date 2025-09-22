/*
 * ESP32-CAM HOG Advanced Example
 * ==============================
 * 
 * Advanced HOG parameter customization and optimization for ESP32-CAM.
 * Perfect for users who want to fine-tune HOG parameters for their specific application.
 * 
 * Features:
 * ✓ Custom HOG parameter configuration
 * ✓ Multiple configuration presets for different use cases
 * ✓ Performance comparison between settings
 * ✓ Automatic cycling through different configurations
 * ✓ Detailed feature analysis and statistics
 * 
 * Hardware: ESP32-CAM with OV2640 camera module
 * Power: 5V/2A recommended
 * 
 * Author: STL_MCU Library Team
 * Date: 2025
 */

#include <Arduino.h>
#include "esp_camera.h"
#include "STL_MCU.h"
#include "hog_transform.h"

// Camera pin definitions for ESP32-CAM (AI-Thinker model)
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

// Configuration presets for different use cases
struct HOGPreset {
    const char* name;
    const char* description;
    HOG_MCU::Config config;
    int expected_features;
    const char* use_case;
};

// Define various HOG configurations to test
HOGPreset presets[] = {
    {
        "Fast & Light",
        "Optimized for speed and low memory usage",
        HOG_MCU::Config(), // Default 32x32
        144,
        "Real-time applications, battery powered devices"
    },
    {
        "Balanced",
        "Good balance between speed and feature quality",
        HOG_MCU::Config(),
        256,
        "General purpose object detection"
    },
    {
        "High Detail",
        "Maximum feature detail for complex recognition",
        HOG_MCU::Config(),
        576,
        "High accuracy requirements, offline processing"
    },
    {
        "Custom Fine",
        "Fine-grained cells for detailed texture analysis",
        HOG_MCU::Config(),
        324,
        "Texture analysis, fine detail recognition"
    }
};

const int NUM_PRESETS = sizeof(presets) / sizeof(presets[0]);
int currentPreset = 0;
unsigned long lastSwitch = 0;
const unsigned long PRESET_DURATION = 10000; // 10 seconds per preset

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("================================================");
    Serial.println("ESP32-CAM HOG Advanced Configuration Example");
    Serial.println("================================================");
    Serial.println();
    
    // Initialize camera
    if (!initCamera()) {
        Serial.println("❌ Camera initialization failed!");
        printTroubleshootingTips();
        while(1) delay(1000);
    }
    Serial.println("✅ Camera initialized successfully");
    
    // Setup custom configurations
    setupPresetConfigurations();
    
    Serial.println("🎯 This example demonstrates HOG parameter customization:");
    Serial.println("   • Different cell sizes and block configurations");
    Serial.println("   • Performance vs quality trade-offs");
    Serial.println("   • Feature count optimization");
    Serial.println("   • Use case specific tuning");
    Serial.println();
    
    Serial.printf("📋 Will cycle through %d configurations every %d seconds\n", 
                  NUM_PRESETS, PRESET_DURATION/1000);
    Serial.println();
    
    lastSwitch = millis();
}

void loop() {
    // Check if it's time to switch to next preset
    if (millis() - lastSwitch > PRESET_DURATION) {
        currentPreset = (currentPreset + 1) % NUM_PRESETS;
        lastSwitch = millis();
        Serial.println("\n" + String("=").repeat(60));
        Serial.printf("🔄 Switching to configuration %d/%d\n", currentPreset + 1, NUM_PRESETS);
        Serial.println("=".repeat(60));
    }
    
    // Apply current preset configuration
    applyCurrentPreset();
    
    // Capture and process image
    processImageWithCurrentConfig();
    
    delay(2000); // Wait 2 seconds between captures
}

void setupPresetConfigurations() {
    // Preset 1: Fast & Light (default is already good)
    // Keep defaults: 32x32, cell_size=8, block_size=16, stride=6, bins=4
    
    // Preset 2: Balanced (larger processing size)
    presets[1].config.hog_img_width = 48;
    presets[1].config.hog_img_height = 48;
    presets[1].config.cell_size = 8;
    presets[1].config.block_size = 16;
    presets[1].config.block_stride = 8;
    presets[1].config.nbins = 4;
    
    // Preset 3: High Detail (maximum resolution)
    presets[2].config.hog_img_width = 64;
    presets[2].config.hog_img_height = 64;
    presets[2].config.cell_size = 8;
    presets[2].config.block_size = 16;
    presets[2].config.block_stride = 8;
    presets[2].config.nbins = 6; // More orientation bins
    
    // Preset 4: Custom Fine (smaller cells for detail)
    presets[3].config.hog_img_width = 48;
    presets[3].config.hog_img_height = 48;
    presets[3].config.cell_size = 6;  // Smaller cells
    presets[3].config.block_size = 12; // 2x2 cells
    presets[3].config.block_stride = 6;
    presets[3].config.nbins = 6;
    
    // All presets use the same image processing settings
    for (int i = 0; i < NUM_PRESETS; i++) {
        presets[i].config.input_format = ImageProcessing::PixelFormat::GRAYSCALE;
        presets[i].config.input_width = 320;
        presets[i].config.input_height = 240;
        presets[i].config.resize_method = ImageProcessing::ResizeMethod::BILINEAR;
    }
}

void applyCurrentPreset() {
    static int lastAppliedPreset = -1;
    
    if (lastAppliedPreset != currentPreset) {
        HOGPreset& preset = presets[currentPreset];
        
        Serial.printf("⚙️  Applying Configuration: %s\n", preset.name);
        Serial.printf("📝 Description: %s\n", preset.description);
        Serial.printf("🎯 Use Case: %s\n", preset.use_case);
        Serial.println();
        
        // Display detailed configuration
        printConfigurationDetails(preset.config);
        
        // Apply the configuration
        hog.setConfig(preset.config);
        
        lastAppliedPreset = currentPreset;
        Serial.println();
    }
}

void processImageWithCurrentConfig() {
    // Capture image
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Failed to capture image");
        return;
    }
    
    // Extract HOG features with timing
    unsigned long startTime = millis();
    hog.transform(fb->buf);
    unsigned long processingTime = millis() - startTime;
    
    // Analyze results
    const auto& features = hog.getFeatures();
    analyzeFeatures(features, processingTime);
    
    esp_camera_fb_return(fb);
}

void printConfigurationDetails(const HOG_MCU::Config& config) {
    Serial.println("📊 Configuration Details:");
    Serial.printf("   Input Size: %dx%d → HOG Size: %dx%d\n", 
                  config.input_width, config.input_height,
                  config.hog_img_width, config.hog_img_height);
    Serial.printf("   Cell Size: %dx%d pixels\n", config.cell_size, config.cell_size);
    Serial.printf("   Block Size: %dx%d pixels (%dx%d cells)\n", 
                  config.block_size, config.block_size,
                  config.block_size/config.cell_size, config.block_size/config.cell_size);
    Serial.printf("   Block Stride: %d pixels\n", config.block_stride);
    Serial.printf("   Histogram Bins: %d\n", config.nbins);
    
    // Calculate expected features
    int blocks_x = (config.hog_img_width - config.block_size) / config.block_stride + 1;
    int blocks_y = (config.hog_img_height - config.block_size) / config.block_stride + 1;
    int expected_features = blocks_x * blocks_y * 4 * config.nbins; // 4 cells per block
    Serial.printf("   Expected Features: %d (%dx%d blocks)\n", expected_features, blocks_x, blocks_y);
}

void analyzeFeatures(const mcu::b_vector<float, 144>& features, unsigned long processingTime) {
    HOGPreset& preset = presets[currentPreset];
    
    Serial.printf("📸 Processing Results:\n");
    Serial.printf("   ⏱️  Processing Time: %lu ms\n", processingTime);
    Serial.printf("   🔢 Features Extracted: %d\n", features.size());
    Serial.printf("   💾 Memory Used: %d bytes\n", features.size() * sizeof(float));
    
    if (features.size() > 0) {
        // Calculate feature statistics
        float minVal = features[0];
        float maxVal = features[0];
        float sum = 0.0f;
        float sumSquares = 0.0f;
        
        for (int i = 0; i < features.size(); i++) {
            float val = features[i];
            minVal = min(minVal, val);
            maxVal = max(maxVal, val);
            sum += val;
            sumSquares += val * val;
        }
        
        float mean = sum / features.size();
        float variance = (sumSquares / features.size()) - (mean * mean);
        float stddev = sqrt(variance);
        
        Serial.printf("   📈 Statistics:\n");
        Serial.printf("      Min: %.6f | Max: %.6f\n", minVal, maxVal);
        Serial.printf("      Mean: %.6f | Std Dev: %.6f\n", mean, stddev);
        Serial.printf("      Range: %.6f | Variance: %.6f\n", maxVal - minVal, variance);
        
        // Feature quality assessment
        if (maxVal - minVal < 0.01f) {
            Serial.println("   ⚠️  Low feature variation - check lighting/scene content");
        } else if (stddev > 0.1f) {
            Serial.println("   ✅ Good feature variation detected");
        } else {
            Serial.println("   📊 Moderate feature variation");
        }
        
        // Performance assessment
        if (processingTime < 50) {
            Serial.println("   🚀 Fast processing - suitable for real-time");
        } else if (processingTime < 100) {
            Serial.println("   ⚖️  Moderate processing - good for most applications");
        } else {
            Serial.println("   🐌 Slower processing - consider for offline/batch processing");
        }
        
        // Show sample features
        Serial.print("   📝 Sample Features: ");
        int sampleCount = min(8, (int)features.size());
        for (int i = 0; i < sampleCount; i++) {
            Serial.printf("%.3f ", features[i]);
        }
        if (features.size() > sampleCount) {
            Serial.print("...");
        }
        Serial.println();
    }
}

void printTroubleshootingTips() {
    Serial.println("\n🛠️  Troubleshooting Tips:");
    Serial.println("   • Check 5V/2A power supply");
    Serial.println("   • Verify camera module connections");
    Serial.println("   • Ensure ESP32-CAM model compatibility");
    Serial.println("   • Try reseating the camera module");
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
    
    // Camera settings
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
        s->set_brightness(s, 0);     // -2 to 2
        s->set_contrast(s, 1);       // -2 to 2
        s->set_saturation(s, 0);     // -2 to 2
        s->set_special_effect(s, 0); // 0 to 6 (0=No Effect)
        s->set_whitebal(s, 1);       // Enable white balance
        s->set_awb_gain(s, 1);       // Enable AWB gain
        s->set_wb_mode(s, 0);        // 0 to 4
        s->set_exposure_ctrl(s, 1);  // Enable exposure control
        s->set_aec2(s, 0);           // Disable AEC2
        s->set_ae_level(s, 0);       // -2 to 2
        s->set_aec_value(s, 300);    // 0 to 1200
        s->set_gain_ctrl(s, 1);      // Enable gain control
        s->set_agc_gain(s, 0);       // 0 to 30
        s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
    }
    
    return true;
}

/*
 * 🎯 CUSTOM CONFIGURATION EXAMPLES:
 * =================================
 * 
 * Manual Configuration:
 * --------------------
 * HOG_MCU::Config custom_config;
 * custom_config.hog_img_width = 48;
 * custom_config.hog_img_height = 48;
 * custom_config.cell_size = 12;
 * custom_config.block_size = 24;
 * custom_config.block_stride = 12;
 * custom_config.nbins = 8;
 * hog.setConfig(custom_config);
 * 
 * For Different Applications:
 * --------------------------
 * // Fast real-time processing
 * config.hog_img_width = 24;
 * config.cell_size = 6;
 * config.nbins = 4;
 * 
 * // High accuracy offline processing
 * config.hog_img_width = 64;
 * config.cell_size = 8;
 * config.nbins = 9;
 * 
 * // Texture analysis
 * config.cell_size = 4;
 * config.block_stride = 2;
 * config.nbins = 12;
 * 
 * Performance Tuning Tips:
 * -----------------------
 * • Smaller cell_size = more detail, slower processing
 * • Larger hog_img_width = more features, slower processing  
 * • More nbins = better orientation precision, more memory
 * • Smaller block_stride = more overlap, more features
 * • Balance is key - test with your specific use case!
 */