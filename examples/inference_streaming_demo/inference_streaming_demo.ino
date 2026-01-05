#define EML_DEV_STAGE
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#include "esp_camera.h"
#include "random_forest_mcu.h"
#include "hog_mcu/hog_transform.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

using namespace mcu;

const char* WIFI_SSID = "YOUR SSID";
const char* WIFI_PASSWORD = "YOUR PASSWORD";

// HTTP streaming
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = nullptr;
httpd_handle_t camera_httpd = nullptr;

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC;

// FreeRTOS task handles
TaskHandle_t inferenceTaskHandle = nullptr;
TaskHandle_t streamTaskHandle = nullptr;

// Shared resources
SemaphoreHandle_t cameraMutex = nullptr;
volatile char currentPrediction[32] = "N/A";
volatile unsigned long inferenceTime = 0;
volatile bool inferenceReady = false;
volatile unsigned long lastStatusMillis = 0;

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
constexpr const char* CONFIG_FILE_PATH = "/gesture/gesture_hogcfg.json";
constexpr unsigned long INFERENCE_DELAY_MS = 500;

bool initCamera(const ImageProcessing::ProcessingConfig& cfg);
bool initWiFi();
void startCameraServer();
void inferenceTask(void* parameter);
void streamTask(void* parameter);

// Convert ImageProcessing dimensions to ESP32 camera framesize_t
framesize_t getESP32FrameSize(int width, int height) {
    // Match exact dimensions to ESP32 camera frame sizes
    if (width == 96 && height == 96) return FRAMESIZE_96X96;
    if (width == 160 && height == 120) return FRAMESIZE_QQVGA;
    if (width == 176 && height == 144) return FRAMESIZE_QCIF;
    if (width == 240 && height == 176) return FRAMESIZE_HQVGA;
    if (width == 240 && height == 240) return FRAMESIZE_240X240;
    if (width == 320 && height == 240) return FRAMESIZE_QVGA;
    if (width == 400 && height == 296) return FRAMESIZE_CIF;
    if (width == 480 && height == 320) return FRAMESIZE_HVGA;
    if (width == 640 && height == 480) return FRAMESIZE_VGA;
    if (width == 800 && height == 600) return FRAMESIZE_SVGA;
    if (width == 1024 && height == 768) return FRAMESIZE_XGA;
    if (width == 1280 && height == 720) return FRAMESIZE_HD;
    if (width == 1280 && height == 1024) return FRAMESIZE_SXGA;
    if (width == 1600 && height == 1200) return FRAMESIZE_UXGA;
    
    // Fallback: return QVGA as default
    Serial.printf("[Camera] Warning: Unsupported resolution %dx%d, using QVGA (320x240)\n", width, height);
    return FRAMESIZE_QVGA;
}

pixformat_t getESP32PixelFormat(ImageProcessing::PixelFormat format) {
    switch (format) {
        case ImageProcessing::PixelFormat::GRAYSCALE: return PIXFORMAT_GRAYSCALE;
        case ImageProcessing::PixelFormat::RGB565:    return PIXFORMAT_RGB565;
        case ImageProcessing::PixelFormat::RGB888:    return PIXFORMAT_RGB888;
        case ImageProcessing::PixelFormat::YUV422:    return PIXFORMAT_YUV422;
        case ImageProcessing::PixelFormat::JPEG:      return PIXFORMAT_JPEG;
        default:
            Serial.println("[Camera] Unknown pixel format, using GRAYSCALE");
            return PIXFORMAT_GRAYSCALE;
    }
}

RandomForest forest;

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
    
    Serial.begin(115200);
    delay(1000);
    Serial.println("ESP32-CAM ML Inference + Stream");
    Serial.println("================================");

    // Initialize camera mutex
    cameraMutex = xSemaphoreCreateMutex();
    if (cameraMutex == nullptr) {
        Serial.println("Failed to create mutex!");
        while (true) delay(1000);
    }

    // Initialize WiFi BEFORE camera to avoid conflicts
    if (!initWiFi()) {
        Serial.println("WiFi init failed.");
        while (true) delay(1000);
    }

    // Small delay after WiFi init
    delay(500);

    // Initialize filesystem and HOG
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

    if (!initCamera(cfg)) {
        Serial.println("Camera init failed.");
        while (true) delay(1000);
    }

    // manage_files();

    // Initialize and load model
    const char* model_name = "gesture";
    forest.init(model_name);

    Serial.print("Loading forest... ");
    if (!forest.loadForest()) {
        Serial.println("❌ FAILED");
        return;
    }
    Serial.println("✓");

    Serial.printf("bits per node: %d\n", forest.bits_per_node());
    Serial.printf("model size in ram: %d\n", forest.model_size_in_ram());
    forest.warmup_prediction();

    // Initialize shared variables
    strcpy((char*)currentPrediction, "Initializing...");
    inferenceTime = 0;
    inferenceReady = false;
    lastStatusMillis = millis();

    // Start HTTP server for streaming
    startCameraServer();

    // Create FreeRTOS tasks
    xTaskCreatePinnedToCore(
        inferenceTask,
        "InferenceTask",
        8192,  // Stack size
        nullptr,
        1,     // Priority
        &inferenceTaskHandle,
        0      // Core 0
    );

    // Small delay between task creation
    delay(100);

    xTaskCreatePinnedToCore(
        streamTask,
        "StreamTask",
        4096,  // Stack size
        nullptr,
        1,     // Priority
        &streamTaskHandle,
        1      // Core 1
    );

    Serial.println("\n✓ System ready!");
    Serial.print("Dashboard URL: http://");
    Serial.println(WiFi.localIP());
    Serial.print("Stream URL: http://");
    Serial.print(WiFi.localIP());
    Serial.println(":81/stream");
}

void loop() {
    // Empty - tasks handle everything
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// Inference task - runs ML model predictions
void inferenceTask(void* parameter) {
    Serial.println("[Inference] Task started");
    
    while (true) {
        if (inferenceReady) {
            unsigned long sinceStatus = millis() - lastStatusMillis;
            if (sinceStatus < INFERENCE_DELAY_MS) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            // Reset if status has not consumed result for a while
            inferenceReady = false;
        }

        // Only run inference when not actively streaming
        if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            camera_fb_t* fb = esp_camera_fb_get();
            
            if (fb) {
                unsigned long start = millis();
                hog.transform(fb->buf);
                
                const auto& features = hog.getFeatures();
                rf_predict_result_t result;
                forest.predict(features, result);
                
                inferenceTime = millis() - start;
                
                // Update shared prediction result
                strncpy((char*)currentPrediction, result.label, sizeof(currentPrediction) - 1);
                currentPrediction[sizeof(currentPrediction) - 1] = '\0';
                inferenceReady = true;
                
                Serial.printf("[Inference] Prediction: %s | Time: %lu ms\n", 
                              result.label, inferenceTime);
                
                esp_camera_fb_return(fb);
            }
            
            xSemaphoreGive(cameraMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(INFERENCE_DELAY_MS));
    }
}

// Stream task - handles HTTP video streaming
void streamTask(void* parameter) {
    Serial.println("[Stream] Task started");
    
    // This task is event-driven by HTTP requests
    // The actual streaming is handled by the HTTP server callbacks
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// HTTP stream handler
static esp_err_t stream_handler(httpd_req_t* req) {
    camera_fb_t* fb = nullptr;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t* jpg_buf = nullptr;
    char part_buf[128];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    // Add HTML overlay header
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        // Wait for mutex with longer timeout for streaming priority
        if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            fb = esp_camera_fb_get();
            
            if (!fb) {
                xSemaphoreGive(cameraMutex);
                Serial.println("[Stream] Camera capture failed");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;  // Try again instead of breaking
            }
            
            // Convert grayscale to JPEG if needed
            if (fb->format != PIXFORMAT_JPEG) {
                bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
                if (!jpeg_converted) {
                    esp_camera_fb_return(fb);
                    xSemaphoreGive(cameraMutex);
                    Serial.println("[Stream] JPEG compression failed");
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
            } else {
                jpg_buf_len = fb->len;
                jpg_buf = fb->buf;
            }
            
            // Release mutex after getting frame
            xSemaphoreGive(cameraMutex);
        } else {
            Serial.println("[Stream] Mutex timeout");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Send the frame
        if (res == ESP_OK) {
            size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpg_buf_len);
            res = httpd_resp_send_chunk(req, part_buf, hlen);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }

        // Cleanup
        if (fb) {
            if (fb->format != PIXFORMAT_JPEG && jpg_buf) {
                free(jpg_buf);
                jpg_buf = nullptr;
            }
            esp_camera_fb_return(fb);
            fb = nullptr;
        }

        if (res != ESP_OK) {
            Serial.println("[Stream] Send failed, stopping");
            break;
        }
        
        // Small delay to prevent overwhelming the system
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return res;
}

// HTTP status handler - returns prediction info
static esp_err_t status_handler(httpd_req_t* req) {
    char json_response[256];
    
    // Read volatile variables into local copies
    char pred_copy[32];
    unsigned long time_copy = inferenceTime;
    bool ready_copy = inferenceReady;
    strncpy(pred_copy, (const char*)currentPrediction, sizeof(pred_copy) - 1);
    pred_copy[sizeof(pred_copy) - 1] = '\0';
    
    snprintf(json_response, sizeof(json_response),
             "{\"prediction\":\"%s\",\"inference_time\":%lu,\"ready\":%s}",
             pred_copy, time_copy, ready_copy ? "true" : "false");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    
    Serial.printf("[Status] Sent: %s\n", json_response);
    lastStatusMillis = millis();
    if (inferenceReady) {
        inferenceReady = false;
    }
    
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// HTTP index handler - serves HTML page with video and prediction overlay
static esp_err_t index_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, sizeof(index_html) - 1);
}

void startCameraServer() {
    httpd_config_t main_config = HTTPD_DEFAULT_CONFIG();
    main_config.server_port = 80;
    main_config.ctrl_port = 32768;
    main_config.max_uri_handlers = 8;
    main_config.stack_size = 4096;

    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = nullptr
    };

    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = nullptr
    };

    Serial.println("[HTTP] Starting main server...");
    if (httpd_start(&camera_httpd, &main_config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        Serial.println("[HTTP] Main server started (port 80)");
    } else {
        Serial.println("[HTTP] Failed to start main server");
    }

    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = 32769;
    stream_config.max_uri_handlers = 4;
    stream_config.stack_size = 8192;
    stream_config.lru_purge_enable = true;

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = nullptr
    };

    Serial.println("[HTTP] Starting stream server...");
    if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("[HTTP] Stream server started (port 81)");
    } else {
        Serial.println("[HTTP] Failed to start stream server");
    }
}

bool initWiFi() {
    Serial.print("Connecting to WiFi");
    
    // Disable WiFi sleep mode for stability
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    
    // Set WiFi config before connecting
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("ESP32-CAM-ML");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Hostname: ");
        Serial.println(WiFi.getHostname());
        return true;
    } else {
        Serial.println("\n✗ WiFi connection failed");
        return false;
    }
}

bool initCamera(const ImageProcessing::ProcessingConfig& cfg) {
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
    config.xclk_freq_hz = 10000000;  // Reduced from 20MHz for stability

    pixformat_t pixelFormat = getESP32PixelFormat(cfg.input_format);
    framesize_t frameSize = getESP32FrameSize(cfg.input_width, cfg.input_height);

    uint8_t quality = cfg.jpeg_quality == 0 ? 12 : cfg.jpeg_quality;
    if (quality < 10) {
        quality = 10;
    }
    if (quality > 63) {
        quality = 63;
    }

    config.pixel_format = pixelFormat;
    config.frame_size = frameSize;
    config.jpeg_quality = quality;
    config.fb_count = 2;  // Use 2 frame buffers for better streaming
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;  // Prevent buffer overflow
#ifdef RF_USE_PSRAM
    config.fb_location = CAMERA_FB_IN_PSRAM;
#endif

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%X\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        sensor->set_framesize(sensor, frameSize);
        if (pixelFormat == PIXFORMAT_JPEG) {
            sensor->set_quality(sensor, quality);
        }
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_whitebal(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, 0);
    }

    // Dummy frame grab to stabilize camera
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }

    Serial.printf("✓ Camera initialized (%dx%d %s)\n",
                  cfg.input_width,
                  cfg.input_height,
                  cfg.input_format == ImageProcessing::PixelFormat::GRAYSCALE ? "GRAYSCALE" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB565 ? "RGB565" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB888 ? "RGB888" :
                  cfg.input_format == ImageProcessing::PixelFormat::YUV422 ? "YUV422" : "JPEG");
    return true;
}
