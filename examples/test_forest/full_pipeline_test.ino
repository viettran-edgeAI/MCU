#define EML_DEV_STAGE
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#include "esp_camera.h"
#include "hog_mcu/hog_transform.h"
#include "random_forest_mcu.h"

using namespace mcu;

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

// ESP32-CAM (AI-Thinker) pin configuration
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

HOG_MCU hog;
constexpr const char* CONFIG_FILE_PATH = "/digit/digit_hogcfg.json";
constexpr unsigned long CAPTURE_DELAY_MS = 3000;

bool initCamera();

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-CAM HOG Config Quickstart");
    Serial.println("================================");

    bool hasFs = RF_FS_BEGIN(STORAGE_MODE);
    if (!hasFs) {
        Serial.println("Filesystem mount failed, using default HOG config.");
        hog.setupForESP32CAM();
    } else {
        Serial.printf("Filesystem mounted (%s)\n", rf_storage_type());
        if (hog.loadConfigFromFile(CONFIG_FILE_PATH)) {
            Serial.printf("Loaded config: %s\n", CONFIG_FILE_PATH);
        } else {
            Serial.printf("Config not found: %s\n", CONFIG_FILE_PATH);
            hog.setupForESP32CAM();
        }
    }

    const auto& cfg = hog.getImageProcessingConfig();
    Serial.printf("Camera input: %dx%d format=%s\n", cfg.input_width, cfg.input_height, 
                  cfg.input_format == ImageProcessing::PixelFormat::GRAYSCALE ? "GRAYSCALE" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB565 ? "RGB565" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB888 ? "RGB888" :
                  cfg.input_format == ImageProcessing::PixelFormat::YUV422 ? "YUV422" : "JPEG");
    Serial.printf("HOG size: %dx%d\n", cfg.output_width, cfg.output_height);

    if (!initCamera()) {
        Serial.println("Camera init failed.");
        while (true) {
            delay(1000);
        }
    }

    Serial.printf("Capturing every %lu ms\n\n", CAPTURE_DELAY_MS);

    manage_files();

    const char* model_name = "digit_data";
    RandomForest forest = RandomForest(model_name);

    // Build and train the model
    if (!forest.build_model()) {
        Serial.println("❌ FAILED");
        return;
    }

    // forest.training(2); // limit to 3 epochs

    // Load trained forest from filesystem
    Serial.print("Loading forest... ");
    if (!forest.loadForest()) {
        Serial.println("❌ FAILED");
        return;
    }

    Serial.printf("bits per node: %d\n", forest.bits_per_node());
    Serial.printf("model size in ram: %d\n", forest.model_size_in_ram());
    forest.warmup_prediction();
}

void loop() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Frame capture failed");
        delay(500);
        return;
    }

    unsigned long start = millis();
    hog.transform(fb->buf);
    unsigned long elapsed = millis() - start;

    const auto& features = hog.getFeatures();
    rf_predict_result_t result = forest.predict(features);

    Serial.printf("Frame %dx%d | %lu ms | %d features\n", fb->width, fb->height, elapsed, static_cast<int>(features.size()));
    Serial.printf("Prediction: %s\n", result.label);

    // // Print feature vector
    // Serial.print("Features: [");
    // for (int i = 0; i < features.size(); ++i) {
    //     Serial.print(features[i], 6);
    //     if (i + 1 < features.size()) {
    //         Serial.print(", ");
    //     }
    // }
    // Serial.println("]\n");

    esp_camera_fb_return(fb);
    delay(CAPTURE_DELAY_MS);
}

bool initCamera() {
    camera_config_t config = {};
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

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%X\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_whitebal(sensor, 1);
    }

    return true;
}
