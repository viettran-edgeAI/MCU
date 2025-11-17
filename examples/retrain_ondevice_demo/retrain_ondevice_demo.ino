#define DEV_STAGE
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#ifndef CAMERA_MODEL_AI_THINKER
#define CAMERA_MODEL_AI_THINKER
#endif

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "random_forest_mcu.h"
#include "hog_mcu/hog_transform.h"
#include "camera_pins.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
// #include <vector>

using namespace mcu;

// -----------------------------------------------------------------------------
// WiFi credentials (update as needed)
// -----------------------------------------------------------------------------
const char* WIFI_SSID = "Ba Bong";
const char* WIFI_PASSWORD = "12345678";

// -----------------------------------------------------------------------------
// HTTP streaming constants
// -----------------------------------------------------------------------------
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t main_httpd = nullptr;
httpd_handle_t stream_httpd = nullptr;

// -----------------------------------------------------------------------------
// Core objects and RTOS resources
// -----------------------------------------------------------------------------
RandomForest forest;
HOG_MCU hog;

TaskHandle_t inferenceTaskHandle = nullptr;
TaskHandle_t retrainTaskHandle = nullptr;

SemaphoreHandle_t cameraMutex = nullptr;
SemaphoreHandle_t stateMutex = nullptr;

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

// -----------------------------------------------------------------------------
// Session & feedback state
// -----------------------------------------------------------------------------
String currentModelName;
String currentConfigPath;

volatile bool filesystemReady = false;
volatile bool sessionConfigured = false;
volatile bool inferenceActive = false;
volatile bool trainingActive = false;
volatile bool sessionFinished = false;
volatile bool streamReady = false;

vector<String> modelClasses;

unsigned long inferenceIntervalMs = 1200;
unsigned long autoFeedbackTimeoutMs = 1200;

enum class FeedbackState : uint8_t {
    None = 0,
    AwaitingAuto,
    AwaitingCorrection
};

struct PendingPrediction {
    bool valid = false;
    unsigned long timestamp = 0;
    char predicted_label[RF_MAX_LABEL_LENGTH] = "N/A";
    unsigned long inference_time_us = 0;
};

PendingPrediction pendingPrediction;
volatile FeedbackState feedbackState = FeedbackState::None;

struct SharedInferenceInfo {
    char last_prediction[RF_MAX_LABEL_LENGTH] = "N/A";
    unsigned long inference_time_us = 0;
    bool ready = false;
    uint32_t total_predictions = 0;
    uint32_t completed_feedback = 0; // manual corrections
    uint32_t auto_confirmations = 0;
    uint32_t wrong_predictions = 0;
    char status_message[160] = "Awaiting configuration";
    float last_training_accuracy = -1.0f;
} sharedInfo;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void startControlServer();
void startStreamServer();
void stopStreamServer();
static esp_err_t index_handler(httpd_req_t* req);
static esp_err_t status_handler(httpd_req_t* req);
static esp_err_t configure_handler(httpd_req_t* req);
static esp_err_t feedback_start_handler(httpd_req_t* req);
static esp_err_t feedback_submit_handler(httpd_req_t* req);
static esp_err_t retrain_handler(httpd_req_t* req);
static esp_err_t finish_handler(httpd_req_t* req);
static esp_err_t stream_handler(httpd_req_t* req);

bool initWiFi();
bool configureSession(const String& model, uint32_t intervalMs);
bool initCameraFromConfig(const ImageProcessing::ProcessingConfig& cfg);
framesize_t getESP32FrameSize(int width, int height);
pixformat_t getESP32PixelFormat(ImageProcessing::PixelFormat format);
void inferenceTask(void* parameter);
void retrainTask(void* parameter);

void setStatusMessage(const char* message);
void applyAutoFeedback(bool forceImmediate = false);
void resolvePendingPrediction(const char* label);
String sanitizeName(const String& raw);
String urlDecode(const String& src);
String jsonEscape(const char* input);
bool getQueryParam(httpd_req_t* req, const char* key, String& value);
esp_err_t sendJson(httpd_req_t* req, const String& body, int code = 200);
esp_err_t sendError(httpd_req_t* req, const char* message, int code = 400);
esp_err_t sendOk(httpd_req_t* req, const char* message = "ok");

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n==============================");
    Serial.println("Interactive Inference + Feedback Server");
    Serial.println("==============================\n");

    cameraMutex = xSemaphoreCreateMutex();
    stateMutex = xSemaphoreCreateMutex();
    if (!cameraMutex || !stateMutex) {
        Serial.println("Failed to allocate mutexes");
        while (true) delay(1000);
    }

    if (!initWiFi()) {
        Serial.println("WiFi init failed. Device halted.");
        while (true) delay(1000);
    }

    filesystemReady = RF_FS_BEGIN(STORAGE_MODE);
    if (!filesystemReady) {
        Serial.println("Filesystem mount failed - retraining disabled.");
    } else {
        Serial.printf("Filesystem mounted (%s)\n", rf_storage_type());
    }

    setStatusMessage("Awaiting configuration");
    startControlServer();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

// -----------------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------------
void setStatusMessage(const char* message) {
    if (!stateMutex) return;
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(sharedInfo.status_message, message, sizeof(sharedInfo.status_message) - 1);
        sharedInfo.status_message[sizeof(sharedInfo.status_message) - 1] = '\0';
        xSemaphoreGive(stateMutex);
    }
}

String sanitizeName(const String& raw) {
    String result;
    result.reserve(raw.length());
    for (size_t i = 0; i < raw.length(); ++i) {
        char c = raw[i];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
            result += c;
        } else if (c == ' ') {
            result += '_';
        }
    }
    while (result.startsWith(".")) {
        result.remove(0, 1);
    }
    while (result.startsWith("/")) {
        result.remove(0, 1);
    }
    return result;
}

String urlDecode(const String& src) {
    String decoded;
    decoded.reserve(src.length());
    for (size_t i = 0; i < src.length(); ++i) {
        char c = src[i];
        if (c == '%' && i + 2 < src.length()) {
            char hi = src[i + 1];
            char lo = src[i + 2];
            char hex[3] = {hi, lo, '\0'};
            decoded += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else if (c == '+') {
            decoded += ' ';
        } else {
            decoded += c;
        }
    }
    return decoded;
}

String jsonEscape(const char* input) {
    if (!input) {
        return String();
    }
    String out;
    while (*input) {
        char c = *input++;
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<uint8_t>(c) < 0x20) {
                    char buf[7];
                    uint8_t uc = static_cast<uint8_t>(c);
                    snprintf(buf, sizeof(buf), "\\u%04x", uc);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

bool getQueryParam(httpd_req_t* req, const char* key, String& value) {
    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len <= 1) {
        return false;
    }

    std::unique_ptr<char[]> buf(new char[buf_len]);
    if (httpd_req_get_url_query_str(req, buf.get(), buf_len) != ESP_OK) {
        return false;
    }

    char val[128];
    if (httpd_query_key_value(buf.get(), key, val, sizeof(val)) == ESP_OK) {
        value = urlDecode(String(val));
        return true;
    }
    return false;
}

esp_err_t sendJson(httpd_req_t* req, const String& body, int code) {
    char statusBuf[8];
    snprintf(statusBuf, sizeof(statusBuf), "%d", code);
    httpd_resp_set_status(req, statusBuf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t sendError(httpd_req_t* req, const char* message, int code) {
    String payload = String("{\"error\":\"") + message + "\"}";
    return sendJson(req, payload, code);
}

esp_err_t sendOk(httpd_req_t* req, const char* message) {
    String payload = String("{\"result\":\"") + message + "\"}";
    return sendJson(req, payload, 200);
}

// -----------------------------------------------------------------------------
// Camera helpers
// -----------------------------------------------------------------------------
framesize_t getESP32FrameSize(int width, int height) {
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
    Serial.printf("[Camera] Unsupported resolution %dx%d, fallback to QVGA\n", width, height);
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

bool initCameraFromConfig(const ImageProcessing::ProcessingConfig& cfg) {
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
    config.xclk_freq_hz = 10000000;

    pixformat_t pixelFormat = getESP32PixelFormat(cfg.input_format);
    framesize_t frameSize = getESP32FrameSize(cfg.input_width, cfg.input_height);

    uint8_t quality = cfg.jpeg_quality == 0 ? 12 : cfg.jpeg_quality;
    quality = std::min<uint8_t>(63, std::max<uint8_t>(10, quality));

    config.pixel_format = pixelFormat;
    config.frame_size = frameSize;
    config.jpeg_quality = quality;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
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
        sensor->set_pixformat(sensor, pixelFormat);
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

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        esp_camera_fb_return(fb);
    }

    Serial.printf("Camera ready (%dx%d, %s)\n", cfg.input_width, cfg.input_height,
                  cfg.input_format == ImageProcessing::PixelFormat::GRAYSCALE ? "GRAYSCALE" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB565 ? "RGB565" :
                  cfg.input_format == ImageProcessing::PixelFormat::RGB888 ? "RGB888" :
                  cfg.input_format == ImageProcessing::PixelFormat::YUV422 ? "YUV422" : "JPEG");
    return true;
}

// -----------------------------------------------------------------------------
// WiFi helper
// -----------------------------------------------------------------------------
bool initWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("ESP32-Feedback");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    Serial.println("\nWiFi connection failed");
    return false;
}

// -----------------------------------------------------------------------------
// Session configuration
// -----------------------------------------------------------------------------
bool configureSession(const String& model, uint32_t intervalMs) {
    if (!filesystemReady) {
        setStatusMessage("Filesystem not ready");
        return false;
    }
    if (trainingActive) {
        setStatusMessage("Training in progress");
        return false;
    }
    if (sessionFinished) {
        setStatusMessage("Session finished - reboot required");
        return false;
    }

    String sanitized = sanitizeName(model);
    if (!sanitized.length()) {
        setStatusMessage("Model name required");
        return false;
    }

    String cfgPath = String("/") + sanitized + "/" + sanitized + "_hogcfg.json";
    if (!RF_FS_EXISTS(cfgPath.c_str())) {
        setStatusMessage("Config file not found");
        return false;
    }

    if (!hog.loadConfigFromFile(cfgPath.c_str())) {
        setStatusMessage("Failed to parse HOG config");
        return false;
    }

    Serial.printf("Loaded config: %s\n", cfgPath.c_str());
    const auto& camCfg = hog.getImageProcessingConfig();

    esp_camera_deinit();
    if (!initCameraFromConfig(camCfg)) {
        setStatusMessage("Camera init failed");
        return false;
    }

    currentModelName = sanitized;
    currentConfigPath = cfgPath;

    forest.init(currentModelName.c_str());
    forest.enable_retrain();
    forest.set_feedback_timeout(intervalMs * 2);

    Serial.print("Loading forest... ");
    if (!forest.loadForest()) {
        Serial.println("FAILED");
        setStatusMessage("Forest load failed");
        return false;
    }
    Serial.println("OK");
    modelClasses = forest.get_all_original_labels();

    if (!forest.able_to_inference()) {
        setStatusMessage("Forest not ready to infer");
        return false;
    }

    forest.warmup_prediction();

    inferenceIntervalMs = std::max<uint32_t>(200, intervalMs);
    autoFeedbackTimeoutMs = inferenceIntervalMs;

    pendingPrediction = PendingPrediction();
    feedbackState = FeedbackState::None;
    sessionConfigured = true;
    inferenceActive = true;

    if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        sharedInfo.total_predictions = 0;
        sharedInfo.completed_feedback = 0;
        sharedInfo.auto_confirmations = 0;
        sharedInfo.wrong_predictions = 0;
        sharedInfo.ready = false;
        sharedInfo.last_training_accuracy = -1.0f;
        strncpy(sharedInfo.last_prediction, "N/A", sizeof(sharedInfo.last_prediction) - 1);
        sharedInfo.last_prediction[sizeof(sharedInfo.last_prediction) - 1] = '\0';
        xSemaphoreGive(stateMutex);
    }

    if (!streamReady) {
        startStreamServer();
        streamReady = true;
    }

    if (!inferenceTaskHandle) {
        xTaskCreatePinnedToCore(
            inferenceTask,
            "InferenceTask",
            8192,
            nullptr,
            1,
            &inferenceTaskHandle,
            0
        );
    }

    setStatusMessage("Inference running");
    return true;
}

// -----------------------------------------------------------------------------
// Feedback helpers
// -----------------------------------------------------------------------------
void resolvePendingPrediction(const char* label, bool manualOverride) {
    if (!pendingPrediction.valid || !label) {
        return;
    }

    bool wasWrong = manualOverride && strncmp(label, pendingPrediction.predicted_label, RF_MAX_LABEL_LENGTH) != 0;

    forest.add_actual_label(label);
    pendingPrediction.valid = false;
    feedbackState = FeedbackState::None;

    if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (manualOverride) {
            sharedInfo.completed_feedback++;
            if (wasWrong) {
                sharedInfo.wrong_predictions++;
            }
        } else {
            sharedInfo.auto_confirmations++;
        }
        xSemaphoreGive(stateMutex);
    }
}

void applyAutoFeedback(bool forceImmediate) {
    if (!pendingPrediction.valid) {
        return;
    }
    if (feedbackState == FeedbackState::AwaitingCorrection && !forceImmediate) {
        return;
    }
    unsigned long elapsed = millis() - pendingPrediction.timestamp;
    if (!forceImmediate && elapsed < autoFeedbackTimeoutMs) {
        return;
    }

    resolvePendingPrediction(pendingPrediction.predicted_label, false);
    setStatusMessage("Prediction auto-confirmed");
}

// -----------------------------------------------------------------------------
// Inference task
// -----------------------------------------------------------------------------
void inferenceTask(void* parameter) {
    Serial.println("[Inference] Task started");
    unsigned long lastInference = 0;

    while (true) {
        if (!sessionConfigured || sessionFinished) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (trainingActive) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (feedbackState == FeedbackState::AwaitingAuto) {
            applyAutoFeedback(false);
        }

        if (feedbackState != FeedbackState::None) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!inferenceActive) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (millis() - lastInference < inferenceIntervalMs) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (!cameraMutex || xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(cameraMutex);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        hog.transform(fb->buf);
        const auto& features = hog.getFeatures();
        rf_predict_result_t result;
        forest.predict(features, result);

        esp_camera_fb_return(fb);
        xSemaphoreGive(cameraMutex);

        lastInference = millis();

        if (!result.success) {
            setStatusMessage("Inference failed");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            strncpy(sharedInfo.last_prediction, result.label, sizeof(sharedInfo.last_prediction) - 1);
            sharedInfo.last_prediction[sizeof(sharedInfo.last_prediction) - 1] = '\0';
            sharedInfo.inference_time_us = result.prediction_time;
            sharedInfo.ready = true;
            sharedInfo.total_predictions++;
            xSemaphoreGive(stateMutex);
        }

        strncpy(pendingPrediction.predicted_label, result.label, sizeof(pendingPrediction.predicted_label) - 1);
        pendingPrediction.predicted_label[sizeof(pendingPrediction.predicted_label) - 1] = '\0';
        pendingPrediction.timestamp = millis();
        pendingPrediction.inference_time_us = result.prediction_time;
        pendingPrediction.valid = true;
        feedbackState = FeedbackState::AwaitingAuto;

        Serial.printf("[Inference] %s (%lu us)\n", result.label, result.prediction_time);
    }
}

// -----------------------------------------------------------------------------
// Retraining task
// -----------------------------------------------------------------------------
void retrainTask(void* parameter) {
    Serial.println("[Retrain] Task started");
    setStatusMessage("Training in progress...");

    applyAutoFeedback(true);
    inferenceActive = false;

    bool success = false;
    forest.flush_pending_data();
    Serial.println("[Retrain] Pending data flushed");

    float accuracy = -1.0f;
    if (forest.build_model()) {
#if defined(RF_ENABLE_TRAINING) && RF_ENABLE_TRAINING
        accuracy = forest.best_training_score();
#endif
        success = forest.loadForest();
    }

    if (success) {
        forest.warmup_prediction();
        if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            sharedInfo.last_training_accuracy = accuracy;
            xSemaphoreGive(stateMutex);
        }
        char msg[96];
        if (accuracy >= 0.0f) {
            snprintf(msg, sizeof(msg), "Training complete (accuracy: %.2f%%)", accuracy * 100.0f);
        } else {
            snprintf(msg, sizeof(msg), "Training complete");
        }
        setStatusMessage(msg);
    } else {
        setStatusMessage("Training failed");
        if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            sharedInfo.last_training_accuracy = -1.0f;
            xSemaphoreGive(stateMutex);
        }
    }

    trainingActive = false;
    inferenceActive = success && sessionConfigured && !sessionFinished;

    retrainTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

// -----------------------------------------------------------------------------
// HTTP helpers & handlers
// -----------------------------------------------------------------------------
static esp_err_t index_handler(httpd_req_t* req) {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Feedback Trainer</title>
    <style>
        body { font-family: Arial, sans-serif; background: #0e131a; color: #f7f7f7; margin: 0; padding: 20px; }
        .card { background: #191f2b; padding: 20px; border-radius: 12px; max-width: 980px; margin: 20px auto; box-shadow: 0 10px 30px rgba(0,0,0,0.45); }
        .card.subtle { background: #121722; box-shadow: none; border: 1px solid #1f2735; }
        h1 { color: #4CAF50; margin-top: 0; }
        label { display: block; margin-bottom: 6px; font-weight: bold; }
        input { width: 100%; padding: 10px; border-radius: 6px; border: none; margin-bottom: 12px; background: #0d1118; color: #fff; }
        button { background: #4CAF50; color: white; border: none; border-radius: 6px; padding: 12px 18px; font-size: 1em; cursor: pointer; margin: 4px; transition: opacity 0.2s; }
        button.secondary { background: #2196F3; }
        button.danger { background: #f44336; }
        button.outline { background: transparent; border: 1px solid #4CAF50; }
        button:disabled { opacity: 0.5; cursor: not-allowed; }
        img { width: 100%; max-width: 640px; border-radius: 12px; margin-top: 16px; border: 3px solid #2d3545; box-shadow: 0 6px 24px rgba(0,0,0,0.45); }
        .status { margin-top: 10px; font-size: 0.92em; color: #cbd5f5; }
        .prediction { font-size: 2.4em; color: #4CAF50; margin: 10px 0; letter-spacing: 0.04em; }
        .row { display: flex; flex-wrap: wrap; gap: 16px; }
        .col { flex: 1; min-width: 320px; }
        .stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 12px; margin-top: 16px; }
        .stat-card { background: #1f2633; border-radius: 10px; padding: 14px; }
        .stat-label { text-transform: uppercase; font-size: 0.75em; color: #93a3c7; letter-spacing: 0.1em; }
        .stat-value { font-size: 1.8em; font-weight: bold; margin-top: 6px; color: #f7f7f7; }
    .class-grid { margin-top: 18px; display: flex; flex-wrap: wrap; gap: 10px; }
    .class-pill { padding: 8px 14px; border-radius: 999px; border: 1px solid #4CAF50; background: #0d1118; font-size: 0.9em; }
    .class-pill.active { background: #4CAF50; color: #060909; box-shadow: 0 0 8px rgba(76, 175, 80, 0.5); }
        #feedback-panel { display: none; margin-top: 16px; padding: 16px; }
        #feedback-panel input { margin-bottom: 8px; }
        #feedback-panel button { margin-top: 4px; }
        .subtext { font-size: 0.85em; color: #8ea0c8; }
    </style>
</head>
<body>
    <div class="card" id="config-card">
        <h1>Configure Session</h1>
        <label>Model name</label>
        <input type="text" id="model" placeholder="enter_model_name" value="model_name">
        <label>Inference interval (ms)</label>
        <input type="number" id="interval" min="200" value="1500">
        <button id="start-btn">Start Inference</button>
        <div class="status" id="config-status"></div>
    </div>

    <div class="card" id="inference-card" style="display:none;">
        <div class="row">
            <div class="col">
                <h1>Live Stream</h1>
                <img id="stream" alt="Video stream">
                <div class="class-grid" id="class-list">
                    <div class="class-pill">Classes will appear here</div>
                </div>
            </div>
            <div class="col">
                <h1>Inference</h1>
                <div>Prediction:</div>
                <div class="prediction" id="pred">--</div>
                <div>Inference time: <span id="time">--</span> μs</div>
                <div class="status" id="state"></div>
                <div class="stats-grid">
                    <div class="stat-card">
                        <div class="stat-label">Total Predictions</div>
                        <div class="stat-value" id="total-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Auto Confirmed</div>
                        <div class="stat-value" id="auto-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Manual Corrections</div>
                        <div class="stat-value" id="manual-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Wrong Predictions</div>
                        <div class="stat-value" id="wrong-count">0</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Model Accuracy</div>
                        <div class="stat-value" id="accuracy-label">--</div>
                    </div>
                </div>
                <div class="status" id="accuracy-status">Awaiting training...</div>
                <button id="wrong-btn" class="secondary">Wrong Prediction</button>
                <button id="retrain-btn">Retrain Model</button>
                <button id="finish-btn" class="danger">Finish Session</button>
                <div id="feedback-panel" class="card subtle">
                    <label>Enter correct label</label>
                    <input type="text" id="feedback-label" placeholder="actual label">
                    <button id="submit-feedback" class="secondary">Submit Label</button>
                    <button id="cancel-feedback" class="outline">Cancel</button>
                    <div class="subtext">Inference is paused while waiting for your confirmation.</div>
                </div>
            </div>
        </div>
    </div>

<script>
const configCard = document.getElementById('config-card');
const inferenceCard = document.getElementById('inference-card');
const configStatus = document.getElementById('config-status');
const predEl = document.getElementById('pred');
const timeEl = document.getElementById('time');
const stateEl = document.getElementById('state');
const totalCountEl = document.getElementById('total-count');
const wrongBtn = document.getElementById('wrong-btn');
const retrainBtn = document.getElementById('retrain-btn');
const finishBtn = document.getElementById('finish-btn');
const feedbackPanel = document.getElementById('feedback-panel');
const feedbackInput = document.getElementById('feedback-label');
const submitFeedbackBtn = document.getElementById('submit-feedback');
const cancelFeedbackBtn = document.getElementById('cancel-feedback');
const streamImg = document.getElementById('stream');
const autoCountEl = document.getElementById('auto-count');
const manualCountEl = document.getElementById('manual-count');
const wrongCountEl = document.getElementById('wrong-count');
const accuracyLabel = document.getElementById('accuracy-label');
const accuracyStatus = document.getElementById('accuracy-status');
const classListEl = document.getElementById('class-list');

let streamLoaded = false;
let awaitingCorrection = false;

function setStream() {
    if (streamLoaded) return;
    const host = window.location.hostname || window.location.host.split(':')[0];
    const isIPv6 = host.includes(':');
    const base = window.location.protocol + '//' + (isIPv6 ? '[' + host + ']' : host);
    streamImg.src = base + ':81/stream';
    streamLoaded = true;
}

document.getElementById('start-btn').onclick = () => {
    const model = document.getElementById('model').value.trim();
    const interval = document.getElementById('interval').value.trim();
    configStatus.textContent = 'Configuring...';
    fetch(`/configure?model=${encodeURIComponent(model)}&interval=${encodeURIComponent(interval)}`)
        .then(r => r.json())
        .then(data => { configStatus.textContent = data.result || data.error || JSON.stringify(data); })
        .catch(err => configStatus.textContent = err.message);
};

wrongBtn.onclick = () => {
    fetch('/feedback/start').then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = true;
        feedbackPanel.style.display = 'block';
        feedbackInput.value = '';
        feedbackInput.focus();
    });
};

submitFeedbackBtn.onclick = () => {
    const label = feedbackInput.value.trim();
    if (!label) {
        alert('Please enter the correct label');
        return;
    }
    fetch(`/feedback/submit?label=${encodeURIComponent(label)}`).then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = false;
        feedbackPanel.style.display = 'none';
    });
};

cancelFeedbackBtn.onclick = () => {
    fetch('/feedback/cancel').then(r => r.json()).then(data => {
        if (data.error) {
            alert(data.error);
            return;
        }
        awaitingCorrection = false;
        feedbackPanel.style.display = 'none';
    });
};

retrainBtn.onclick = () => {
    fetch('/retrain').then(r => r.json()).then(data => {
        alert(data.result || data.error);
    });
};

finishBtn.onclick = () => {
    fetch('/finish').then(r => r.json()).then(data => {
        alert(data.result || data.error);
        if (!data.error) {
            inferenceCard.style.display = 'none';
            configCard.style.display = 'block';
            configStatus.textContent = 'Session finished. Refresh or reset the board to start over.';
            streamImg.src = '';
            streamLoaded = false;
        }
    });
};

function renderClasses(list, prediction) {
    if (!classListEl) return;
    classListEl.innerHTML = '';
    if (!list || !list.length) {
        const placeholder = document.createElement('div');
        placeholder.className = 'class-pill';
        placeholder.textContent = 'No classes available';
        classListEl.appendChild(placeholder);
        return;
    }
    for (const cls of list) {
        const pill = document.createElement('div');
        pill.className = 'class-pill';
        pill.textContent = cls;
        if (cls === prediction) {
            pill.classList.add('active');
        }
        classListEl.appendChild(pill);
    }
}

function refreshStatus() {
    fetch(`/status?ts=${Date.now()}`)
        .then(r => r.json())
        .then(data => {
            if (data.session_finished) {
                configCard.style.display = 'block';
                inferenceCard.style.display = 'none';
                configStatus.textContent = 'Session finished. Refresh or reset to start again.';
                streamImg.src = '';
                streamLoaded = false;
                return;
            }
            if (!data.configured) {
                configCard.style.display = 'block';
                inferenceCard.style.display = 'none';
                return;
            }
            configCard.style.display = 'none';
            inferenceCard.style.display = 'block';
            setStream();

            predEl.textContent = data.prediction;
            timeEl.textContent = data.inference_time_us;
            stateEl.textContent = data.message;
            totalCountEl.textContent = data.total_predictions;
            manualCountEl.textContent = data.manual_corrections ?? data.completed_feedback ?? 0;
            autoCountEl.textContent = data.auto_confirmations ?? 0;
            wrongCountEl.textContent = data.wrong_predictions ?? 0;
            const accuracyVal = parseFloat(data.last_training_accuracy);
            if (!isNaN(accuracyVal) && accuracyVal >= 0) {
                const pct = (accuracyVal * 100).toFixed(2) + '%';
                accuracyLabel.textContent = pct;
                accuracyStatus.textContent = 'Last training accuracy: ' + pct;
            } else {
                accuracyLabel.textContent = '--';
                accuracyStatus.textContent = 'Awaiting training...';
            }
            renderClasses(data.classes || [], data.prediction);

            if (data.feedback_state === 'AwaitingCorrection') {
                awaitingCorrection = true;
                feedbackPanel.style.display = 'block';
            } else if (!awaitingCorrection) {
                feedbackPanel.style.display = 'none';
            }
        })
        .catch(err => console.log(err));
}

setInterval(refreshStatus, 700);
refreshStatus();
</script>
</body>
</html>
)rawliteral";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

static String feedbackStateString() {
    switch (feedbackState) {
        case FeedbackState::AwaitingAuto: return "AwaitingAuto";
        case FeedbackState::AwaitingCorrection: return "AwaitingCorrection";
        default: return "None";
    }
}

static esp_err_t status_handler(httpd_req_t* req) {
    if (!stateMutex || xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return sendError(req, "State busy");
    }

    String predictionEsc = jsonEscape(sharedInfo.last_prediction);
    String messageEsc = jsonEscape(sharedInfo.status_message);
    String pendingEsc = pendingPrediction.valid ? jsonEscape(pendingPrediction.predicted_label) : String();
    String classesPayload = "[";
    for (size_t i = 0; i < modelClasses.size(); ++i) {
        classesPayload += "\"" + jsonEscape(modelClasses[i].c_str()) + "\"";
        if (i + 1 < modelClasses.size()) {
            classesPayload += ",";
        }
    }
    classesPayload += "]";

    String payload = "{";
    payload += "\"configured\":" + String(sessionConfigured ? "true" : "false");
    payload += ",\"prediction\":\"" + predictionEsc + "\"";
    payload += ",\"inference_time_us\":" + String(sharedInfo.inference_time_us);
    payload += ",\"ready\":" + String(sharedInfo.ready ? "true" : "false");
    payload += ",\"total_predictions\":" + String(sharedInfo.total_predictions);
    payload += ",\"completed_feedback\":" + String(sharedInfo.completed_feedback);
    payload += ",\"manual_corrections\":" + String(sharedInfo.completed_feedback);
    payload += ",\"auto_confirmations\":" + String(sharedInfo.auto_confirmations);
    payload += ",\"wrong_predictions\":" + String(sharedInfo.wrong_predictions);
    payload += ",\"last_training_accuracy\":" + String(sharedInfo.last_training_accuracy, 4);
    payload += ",\"message\":\"" + messageEsc + "\"";
    payload += ",\"feedback_state\":\"" + feedbackStateString() + "\"";
    payload += ",\"training\":" + String(trainingActive ? "true" : "false");
    payload += ",\"session_finished\":" + String(sessionFinished ? "true" : "false");
    if (pendingPrediction.valid) {
        payload += ",\"pending_label\":\"" + pendingEsc + "\"";
    }
    payload += ",\"classes\":" + classesPayload;
    payload += "}";

    xSemaphoreGive(stateMutex);
    return sendJson(req, payload, 200);
}

static esp_err_t configure_handler(httpd_req_t* req) {
    String model, intervalStr;
    if (!getQueryParam(req, "model", model)) {
        return sendError(req, "Missing model");
    }
    if (!getQueryParam(req, "interval", intervalStr)) {
        intervalStr = "1500";
    }

    uint32_t interval = std::max<uint32_t>(200, intervalStr.toInt());

    if (configureSession(model, interval)) {
        return sendOk(req, "Session configured");
    }
    return sendError(req, "Configuration failed");
}

static esp_err_t feedback_start_handler(httpd_req_t* req) {
    if (!sessionConfigured || !pendingPrediction.valid) {
        return sendError(req, "No prediction awaiting feedback");
    }
    if (feedbackState == FeedbackState::AwaitingCorrection) {
        return sendError(req, "Already awaiting correction");
    }

    feedbackState = FeedbackState::AwaitingCorrection;
    inferenceActive = false;
    setStatusMessage("Awaiting manual label");
    return sendOk(req, "Provide actual label");
}

static esp_err_t feedback_submit_handler(httpd_req_t* req) {
    if (feedbackState != FeedbackState::AwaitingCorrection) {
        return sendError(req, "No correction expected");
    }

    String label;
    if (!getQueryParam(req, "label", label) || !label.length()) {
        return sendError(req, "Label required");
    }

    if (modelClasses.empty()) {
        return sendError(req, "Model classes unknown");
    }

    String canonical;
    for (const auto& cls : modelClasses) {
        if (cls.equalsIgnoreCase(label)) {
            canonical = cls;
            break;
        }
    }
    if (!canonical.length()) {
        return sendError(req, "Invalid class label");
    }

    resolvePendingPrediction(canonical.c_str(), true);
    inferenceActive = true;
    setStatusMessage("Feedback received");
    return sendOk(req, "Feedback applied");
}

static esp_err_t feedback_cancel_handler(httpd_req_t* req) {
    if (feedbackState != FeedbackState::AwaitingCorrection || !pendingPrediction.valid) {
        return sendError(req, "No feedback to cancel");
    }
    resolvePendingPrediction(pendingPrediction.predicted_label, false);
    inferenceActive = true;
    setStatusMessage("Feedback canceled");
    return sendOk(req, "Feedback canceled");
}

static esp_err_t retrain_handler(httpd_req_t* req) {
    if (!sessionConfigured) {
        return sendError(req, "Configure session first");
    }
    if (trainingActive) {
        return sendError(req, "Training already in progress");
    }

    trainingActive = true;
    inferenceActive = false;

    if (!retrainTaskHandle) {
        xTaskCreatePinnedToCore(
            retrainTask,
            "RetrainTask",
            8192,
            nullptr,
            1,
            &retrainTaskHandle,
            1
        );
    }

    return sendOk(req, "Training started");
}

static esp_err_t finish_handler(httpd_req_t* req) {
    if (trainingActive) {
        return sendError(req, "Training in progress", 409);
    }

    sessionFinished = true;
    sessionConfigured = false;
    inferenceActive = false;

    applyAutoFeedback(true);
    pendingPrediction = PendingPrediction();
    feedbackState = FeedbackState::None;

    forest.flush_pending_data();
    forest.releaseForest();
    esp_camera_deinit();
    stopStreamServer();
    streamReady = false;
    setStatusMessage("Session finished");

    if (stateMutex && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        sharedInfo.ready = false;
        xSemaphoreGive(stateMutex);
    }

    return sendOk(req, "Session finished");
}

// -----------------------------------------------------------------------------
// Streaming server
// -----------------------------------------------------------------------------
static esp_err_t stream_handler(httpd_req_t* req) {
    if (!sessionConfigured) {
        return sendError(req, "Not configured");
    }

    camera_fb_t* fb = nullptr;
    esp_err_t res = ESP_OK;
    size_t jpg_buf_len = 0;
    uint8_t* jpg_buf = nullptr;
    char part_buf[128];

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true) {
        if (!cameraMutex || xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(cameraMutex);
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        if (fb->format != PIXFORMAT_JPEG) {
            bool converted = frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len);
            if (!converted) {
                esp_camera_fb_return(fb);
                xSemaphoreGive(cameraMutex);
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
        } else {
            jpg_buf_len = fb->len;
            jpg_buf = fb->buf;
        }

        xSemaphoreGive(cameraMutex);

        size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, jpg_buf_len);
        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_buf_len);
        }
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        }

        if (fb->format != PIXFORMAT_JPEG && jpg_buf) {
            free(jpg_buf);
            jpg_buf = nullptr;
        }
        esp_camera_fb_return(fb);
        fb = nullptr;

        if (res != ESP_OK) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return res;
}

void startControlServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 10;
    config.stack_size = 6144;

    if (httpd_start(&main_httpd, &config) == ESP_OK) {
        httpd_uri_t index_uri = {"/", HTTP_GET, index_handler, nullptr};
        httpd_uri_t status_uri = {"/status", HTTP_GET, status_handler, nullptr};
        httpd_uri_t configure_uri = {"/configure", HTTP_GET, configure_handler, nullptr};
        httpd_uri_t feedback_start_uri = {"/feedback/start", HTTP_GET, feedback_start_handler, nullptr};
        httpd_uri_t feedback_submit_uri = {"/feedback/submit", HTTP_GET, feedback_submit_handler, nullptr};
    httpd_uri_t feedback_cancel_uri = {"/feedback/cancel", HTTP_GET, feedback_cancel_handler, nullptr};
        httpd_uri_t retrain_uri = {"/retrain", HTTP_GET, retrain_handler, nullptr};
        httpd_uri_t finish_uri = {"/finish", HTTP_GET, finish_handler, nullptr};

        httpd_register_uri_handler(main_httpd, &index_uri);
        httpd_register_uri_handler(main_httpd, &status_uri);
        httpd_register_uri_handler(main_httpd, &configure_uri);
        httpd_register_uri_handler(main_httpd, &feedback_start_uri);
        httpd_register_uri_handler(main_httpd, &feedback_submit_uri);
    httpd_register_uri_handler(main_httpd, &feedback_cancel_uri);
        httpd_register_uri_handler(main_httpd, &retrain_uri);
        httpd_register_uri_handler(main_httpd, &finish_uri);
        Serial.println("HTTP control server started (port 80)");
    } else {
        Serial.println("Failed to start control server");
    }
}

void startStreamServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {"/stream", HTTP_GET, stream_handler, nullptr};
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("HTTP stream server started (port 81)");
    } else {
        Serial.println("Failed to start stream server");
    }
}

void stopStreamServer() {
    if (stream_httpd) {
        httpd_stop(stream_httpd);
        stream_httpd = nullptr;
        Serial.println("Stream server stopped");
    }
}
