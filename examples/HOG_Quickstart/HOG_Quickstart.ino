/*
 * ESP32-CAM HOG Quickstart - Basic Example
 * ========================================
 * 
 * Simple and minimal setup for HOG feature extraction on ESP32-CAM.
 * Perfect for beginners who want to get started quickly!
 * 
 * Features:
 * ✓ Minimal 3-line setup
 * ✓ Uses optimal default HOG parameters
 * ✓ Only requires frame size and pixel format configuration
 * ✓ Ready for machine learning integration
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

// Create HOG processor
HOG_MCU hog;

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32-CAM HOG Quickstart");
    Serial.println("========================");
    
    // Step 1: Initialize camera
    if (!initCamera()) {
        Serial.println("❌ Camera initialization failed!");
        Serial.println("Check power supply (5V/2A) and connections");
        while(1) delay(1000);
    }
    Serial.println("✅ Camera initialized");
    
    // Step 2: Setup HOG with optimal defaults (ONE LINE!)
    hog.setupForESP32CAM();  // Uses: GRAYSCALE, 320x240 → 32x32, optimal HOG params
    Serial.println("✅ HOG configured with defaults:");
    Serial.println("   - Input: 320x240 GRAYSCALE");
    Serial.println("   - Processing: 32x32 pixels");
    Serial.println("   - Features: ~144 per image");
    Serial.println("   - Processing time: ~40-60ms");
    
    Serial.println("\n🚀 Ready! Capturing images every 3 seconds...\n");
}

void loop() {
    // Step 3: Capture image
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("❌ Failed to capture image");
        delay(1000);
        return;
    }
    
    // Step 4: Extract HOG features (ONE LINE!)
    unsigned long startTime = millis();
    hog.transform(fb->buf);
    unsigned long processingTime = millis() - startTime;
    
    // Step 5: Use the features
    const auto& features = hog.getFeatures();
    
    Serial.printf("📸 Image: %dx%d | ⏱️ Processed: %lu ms | 🔢 Features: %d\n", 
                  fb->width, fb->height, processingTime, features.size());
    
    // Show first few features as example
    Serial.print("Features: ");
    for (int i = 0; i < min(8, (int)features.size()); i++) {
        Serial.printf("%.3f ", features[i]);
    }
    Serial.println("...");
    
    // 🎯 YOUR MACHINE LEARNING CODE GOES HERE!
    // Example integration:
    // float prediction = myRandomForest.predict(features.data(), features.size());
    // Serial.printf("Prediction: %.2f\n", prediction);
    
    esp_camera_fb_return(fb);
    delay(3000);  // Wait 3 seconds before next capture
}

bool initCamera() {
    camera_config_t config;
    
    // Pin configuration for ESP32-CAM AI-Thinker model
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
    
    // Camera settings - optimized for HOG feature extraction
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;  // ✅ Grayscale for efficiency
    config.frame_size = FRAMESIZE_QVGA;         // ✅ 320x240 - good balance
    config.jpeg_quality = 12;
    config.fb_count = 1;
    
    // Initialize camera
    if (esp_camera_init(&config) != ESP_OK) {
        return false;
    }
    
    // Basic sensor optimization for consistent results
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 0);        // Neutral brightness
        s->set_contrast(s, 1);          // Slightly enhanced contrast
        s->set_exposure_ctrl(s, 1);     // Enable exposure control
        s->set_gain_ctrl(s, 1);         // Enable gain control
        s->set_whitebal(s, 1);          // Enable white balance
    }
    
    return true;
}

/*
 * 🎯 INTEGRATION EXAMPLES:
 * =======================
 * 
 * Basic Usage (what this example shows):
 * -------------------------------------
 * HOG_MCU hog;
 * hog.setupForESP32CAM();           // One-line setup!
 * hog.transform(camera_buffer);     // Extract features
 * const auto& features = hog.getFeatures();  // Use features
 * 
 * With Different Camera Settings:
 * ------------------------------
 * // For RGB565 color input:
 * hog.setupForESP32CAM(ImageProcessing::PixelFormat::RGB565, 320, 240);
 * 
 * // For higher resolution:
 * hog.setupForESP32CAM(ImageProcessing::PixelFormat::GRAYSCALE, 640, 480);
 * 
 * Machine Learning Integration:
 * ----------------------------
 * #include "your_classifier.h"
 * 
 * const auto& features = hog.getFeatures();
 * float prediction = classifier.predict(features.data(), features.size());
 * 
 * if (prediction > 0.5) {
 *     Serial.println("Object detected!");
 * }
 * 
 * 🔧 For advanced HOG parameter customization, see the "HOG_Advanced" example!
 */