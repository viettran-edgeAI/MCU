#include <Arduino.h>
#include "esp_camera.h"
#include "STL_MCU.h"
#include "hog_transform.h"
#include "Rf_file_manager.h"

// --- Storage Configuration ---
// Choose storage mode: LITTLEFS (internal flash), SD_MMC (ESP32-CAM SD slot), or SD_SPI (external module)
const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;  // Change to LITTLEFS if no SD card

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

constexpr const char* CONFIG_FILE_PATH = "/digit/digit_hogcfg.json";  // Adjust to match your model folder
constexpr uint32_t CAPTURE_INTERVAL_MS = 3000;

HOG_MCU hog;

bool initCameraWithConfig(const ImageProcessing::ProcessingConfig& imgCfg);
framesize_t mapFrameSize(int width, int height);
pixformat_t mapPixelFormat(ImageProcessing::PixelFormat format);
const char* pixelFormatToString(ImageProcessing::PixelFormat format);
void printFeatureVector(const mcu::b_vector<float, 144>& features);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-CAM HOG Config Capture");
    Serial.println("-------------------------------");

    bool fsReady = RF_FS_BEGIN(STORAGE_MODE);
    if (!fsReady) {
        Serial.println("Filesystem mount failed, falling back to default HOG settings.");
        hog.setupForESP32CAM();
    } else {
        Serial.printf("Filesystem mounted (%s)\n", rf_storage_type());
        Serial.printf("Total space: %lu bytes, Used: %lu bytes\n", RF_TOTAL_BYTES(), RF_USED_BYTES());

        if (hog.loadConfigFromFile(CONFIG_FILE_PATH)) {
            Serial.printf("Loaded config: %s\n", CONFIG_FILE_PATH);
            Serial.printf("Feature CSV: %s\n", hog.getFeatureCsvPath().c_str());
            Serial.printf("Feature file name: %s\n", hog.getFeatureFileName().c_str());
        } else {
            Serial.printf("Config not found or invalid: %s\n", CONFIG_FILE_PATH);
            Serial.println("Using default HOG configuration.");
            hog.setupForESP32CAM();
        }
    }

    const auto& imgCfg = hog.getImageProcessingConfig();
    Serial.printf("Input format: %s\n", pixelFormatToString(imgCfg.input_format));
    Serial.printf("Camera input size: %dx%d\n", imgCfg.input_width, imgCfg.input_height);
    Serial.printf("HOG output size: %dx%d\n", imgCfg.output_width, imgCfg.output_height);

    if (!initCameraWithConfig(imgCfg)) {
        Serial.println("Camera initialization failed. Check wiring and power.");
        while (true) {
            delay(1000);
        }
    }

    Serial.printf("Capturing every %lu ms...\n\n", static_cast<unsigned long>(CAPTURE_INTERVAL_MS));
}

void loop() {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Failed to capture frame.");
        delay(1000);
        return;
    }

    unsigned long start = millis();
    hog.transform(fb->buf);
    unsigned long elapsed = millis() - start;

    const auto& features = hog.getFeatures();
    Serial.printf("Frame %dx%d | %lu ms | %d features\n", fb->width, fb->height, elapsed, static_cast<int>(features.size()));
    printFeatureVector(features);

    esp_camera_fb_return(fb);
    delay(CAPTURE_INTERVAL_MS);
}

bool initCameraWithConfig(const ImageProcessing::ProcessingConfig& imgCfg) {
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
    config.pixel_format = mapPixelFormat(imgCfg.input_format);
    config.frame_size = mapFrameSize(imgCfg.input_width, imgCfg.input_height);
    config.jpeg_quality = (config.pixel_format == PIXFORMAT_JPEG) ? constrain(imgCfg.jpeg_quality, 10, 63) : 12;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("esp_camera_init failed: 0x%X\n", static_cast<unsigned>(err));
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
    Serial.printf("Unsupported frame size %dx%d, defaulting to 320x240.\n", width, height);
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
            Serial.println("Unknown pixel format, defaulting to GRAYSCALE.");
            return PIXFORMAT_GRAYSCALE;
    }
}

const char* pixelFormatToString(ImageProcessing::PixelFormat format) {
    switch (format) {
        case ImageProcessing::PixelFormat::GRAYSCALE:
            return "GRAYSCALE";
        case ImageProcessing::PixelFormat::RGB565:
            return "RGB565";
        case ImageProcessing::PixelFormat::RGB888:
            return "RGB888";
        case ImageProcessing::PixelFormat::YUV422:
            return "YUV422";
        case ImageProcessing::PixelFormat::JPEG:
            return "JPEG";
        default:
            return "UNKNOWN";
    }
}

void printFeatureVector(const mcu::b_vector<float, 144>& features) {
    const int count = static_cast<int>(features.size());
    Serial.print("Features: [");
    for (int i = 0; i < count; ++i) {
        Serial.print(features[i], 6);
        if (i + 1 < count) {
            Serial.print(", ");
        }
    }
    Serial.println("]");
}
