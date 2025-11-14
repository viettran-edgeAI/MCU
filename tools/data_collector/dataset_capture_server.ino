#define DEV_STAGE
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

/*
 * Dataset capture server for ESP32-CAM
 * ------------------------------------
 * This sketch hosts a Wi-Fi dashboard that lets you:
 *   1. Pick or set a dataset name that will become the top-level folder
 *   2. Choose the class label you want to capture before starting a session
 *   3. Stream frames, capture into a temporary session, and save them under the chosen class
 *   4. Export the ESP32 camera configuration so downstream tooling (data transfer, HOG) can reuse it
 *
 * Usage expectations:
 *   - Set the dataset name as soon as the board boots up.
 *   - Enter the class label for the next capture batch before pressing "Start Capture." Once configured,
 *     the class name is fixed until you stop or discard the session.
 *   - After capturing, finish the session, and let the existing save flow move files into the class folder.
 */

#include <Arduino.h>
#include <esp_err.h>

#include "esp_camera.h"
#include "Rf_file_manager.h"
#include "hog_mcu/hog_transform.h"

#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "driver/ledc.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include <cctype>
#include <limits>

using namespace mcu;

// WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

constexpr RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;
constexpr unsigned long CAPTURE_INTERVAL_MS = 750;
constexpr const char* DATASET_NAME = "gesture";
constexpr const char* SESSION_FOLDER_NAME = "_sessions";
constexpr const char* CONFIG_FILE_PATH = "/digit_data/digit_data_hogcfg.json";
constexpr bool USE_CAMERA_CONFIG_FILE = true;
constexpr bool ENABLE_MANUAL_CAMERA_SETUP = true;
constexpr const char* CAMERA_CONFIG_EXPORT_FILENAME = "camera_config.json";

struct ManualCameraSetupConfig {
    bool enabled;
    ImageProcessing::PixelFormat format;
    int width;
    int height;
    uint8_t jpegQuality;
};

constexpr ManualCameraSetupConfig MANUAL_CAMERA_SETUP{
    true,
    ImageProcessing::PixelFormat::GRAYSCALE,
    96,
    96,
    80
};

enum class CameraConfigMode : uint8_t {
    File,
    Manual,
    Default
};

httpd_handle_t main_httpd = nullptr;

SemaphoreHandle_t cameraMutex = nullptr;
SemaphoreHandle_t sessionMutex = nullptr;
SemaphoreHandle_t fsMutex = nullptr;

HOG_MCU hog;

CameraConfigMode cameraConfigMode = CameraConfigMode::Default;
String cameraConfigExportPath;
String cameraConfigSourcePath;

String activeDatasetName;
bool datasetReady = false;
String activeClassName;
bool classReady = false;

constexpr int FLASH_LED_PIN = 4;
constexpr ledc_mode_t FLASH_LED_PWM_SPEED_MODE = LEDC_HIGH_SPEED_MODE;
constexpr ledc_timer_t FLASH_LED_PWM_TIMER = LEDC_TIMER_1;
constexpr ledc_channel_t FLASH_LED_PWM_CHANNEL = LEDC_CHANNEL_2;
constexpr int FLASH_LED_PWM_FREQ = 5000;
constexpr int FLASH_LED_PWM_RESOLUTION = 8;
constexpr uint8_t FLASH_LEVELS[] = {0, 48, 128, 255};
constexpr const char* FLASH_LEVEL_LABELS[] = {"Off", "Low", "Medium", "High"};
constexpr size_t FLASH_LEVEL_COUNT = sizeof(FLASH_LEVELS) / sizeof(FLASH_LEVELS[0]);

volatile size_t flashLevelIndex = 0;
volatile bool captureTaskShouldStop = false;
volatile bool finishCompleted = false;

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

enum class SessionPhase : uint8_t {
    Idle,
    Capturing,
    Paused,
    AwaitingDecision,
    Saving,
    Discarding,
    Finishing
};

struct CaptureSession {
    SessionPhase phase{SessionPhase::Idle};
    String tempFolder;
    size_t imageCount{0};
    uint32_t nextIndex{0};
    size_t totalBytes{0};
    unsigned long lastCaptureMillis{0};
};

struct DatasetStats {
    String lastClass;
    size_t lastCount{0};
    size_t lastBytes{0};
    unsigned long lastSavedMillis{0};
    size_t totalSaved{0};
};

CaptureSession session;
DatasetStats datasetStats;
TaskHandle_t captureTaskHandle = nullptr;

String datasetRootPath;
String sessionRootPath;

bool initCamera(const ImageProcessing::ProcessingConfig& cfg);
bool initWiFi();
void startCameraServer();
void captureTask(void* parameter);
void setupFlash();
void applyFlashLevel(size_t index);
size_t cycleFlashLevel();
const char* currentFlashLabel();

bool prepareCameraConfiguration();
void applyManualCameraSetup();
bool exportCameraConfiguration();
String buildCameraConfigJson();
const char* pixelFormatToString(ImageProcessing::PixelFormat format);
String cameraConfigFileName();

framesize_t resolveFrameSize(int width, int height);
pixformat_t resolvePixelFormat(ImageProcessing::PixelFormat format);

class FsLockGuard {
public:
    FsLockGuard(SemaphoreHandle_t handle, TickType_t waitTicks)
        : mutex(handle), locked(false) {
        if (mutex != nullptr) {
            locked = (xSemaphoreTake(mutex, waitTicks) == pdTRUE);
        }
    }

    ~FsLockGuard() {
        if (locked && mutex != nullptr) {
            xSemaphoreGive(mutex);
        }
    }

    bool isLocked() const { return locked; }

private:
    SemaphoreHandle_t mutex;
    bool locked;
};

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

String cameraConfigFileName() {
    String name = (datasetReady && activeDatasetName.length()) ? activeDatasetName : String(DATASET_NAME);
    name += "_camera_config.json";
    return name;
}

void applyManualCameraSetup() {
    hog.setupForESP32CAM(
        MANUAL_CAMERA_SETUP.format,
        MANUAL_CAMERA_SETUP.width,
        MANUAL_CAMERA_SETUP.height
    );

    ImageProcessing::ProcessingConfig cfg = hog.getImageProcessingConfig();
    cfg.input_format = MANUAL_CAMERA_SETUP.format;
    cfg.input_width = MANUAL_CAMERA_SETUP.width;
    cfg.input_height = MANUAL_CAMERA_SETUP.height;
    cfg.jpeg_quality = MANUAL_CAMERA_SETUP.jpegQuality;
    hog.setImageProcessingConfig(cfg);
}

String buildCameraConfigJson() {
    const auto& cfg = hog.getImageProcessingConfig();
    String json;
    json.reserve(256);
    json += "{\n";
    json += "  \"source\": \"";
    json += (cameraConfigMode == CameraConfigMode::Manual) ? "manual" : "default";
    json += "\",\n";
    json += "  \"input_format\": \"";
    json += pixelFormatToString(cfg.input_format);
    json += "\",\n";
    json += "  \"input_width\": ";
    json += cfg.input_width;
    json += ",\n";
    json += "  \"input_height\": ";
    json += cfg.input_height;
    json += ",\n";
    json += "  \"output_width\": ";
    json += cfg.output_width;
    json += ",\n";
    json += "  \"output_height\": ";
    json += cfg.output_height;
    json += ",\n";
    json += "  \"jpeg_quality\": ";
    json += cfg.jpeg_quality;
    json += "\n";
    json += "}\n";
    return json;
}

bool exportCameraConfiguration() {
    const String configFileName = cameraConfigFileName();
    cameraConfigExportPath = datasetRootPath;
    if (!cameraConfigExportPath.endsWith("/")) {
        cameraConfigExportPath += "/";
    }
    cameraConfigExportPath += configFileName;

    FsLockGuard lock(fsMutex, pdMS_TO_TICKS(1000));
    if (!lock.isLocked()) {
        return false;
    }

    if (RF_FS_EXISTS(cameraConfigExportPath)) {
        RF_FS_REMOVE(cameraConfigExportPath);
    }

    File dst = RF_FS_OPEN(cameraConfigExportPath.c_str(), "w");
    if (!dst) {
        return false;
    }

    String json = buildCameraConfigJson();
    dst.print(json);
    dst.close();
    return true;
}

bool prepareCameraConfiguration() {
    cameraConfigMode = CameraConfigMode::Default;
    cameraConfigSourcePath = "";

    bool loadedFromFile = false;
    if (USE_CAMERA_CONFIG_FILE && RF_FS_EXISTS(CONFIG_FILE_PATH)) {
        if (hog.loadConfigFromFile(CONFIG_FILE_PATH)) {
            Serial.printf("Loaded config: %s\n", CONFIG_FILE_PATH);
            cameraConfigMode = CameraConfigMode::File;
            cameraConfigSourcePath = CONFIG_FILE_PATH;
            loadedFromFile = true;
        } else {
            Serial.printf("Config parse failed: %s, falling back\n", CONFIG_FILE_PATH);
        }
    } else if (USE_CAMERA_CONFIG_FILE) {
        Serial.printf("Config not found: %s\n", CONFIG_FILE_PATH);
    }

    if (!loadedFromFile) {
        if (ENABLE_MANUAL_CAMERA_SETUP && MANUAL_CAMERA_SETUP.enabled) {
            applyManualCameraSetup();
            cameraConfigMode = CameraConfigMode::Manual;
            Serial.println("Using manual camera setup");
        } else {
            hog.setupForESP32CAM();
            cameraConfigMode = CameraConfigMode::Default;
            Serial.println("Using default ESP32-CAM setup");
        }
    }

    if (!exportCameraConfiguration()) {
        Serial.println("⚠️  Failed to export camera config to dataset root");
        return false;
    }

    Serial.printf("Camera config exported to %s\n", cameraConfigExportPath.c_str());
    return true;
}

bool ensureDirectory(const String& path);
bool deleteDirectoryRecursive(const String& path);
static bool deleteDirectoryRecursiveLocked(const String& path);
bool saveFrameToFile(const String& folder, uint32_t index, camera_fb_t* fb, size_t& bytesWritten);
String sanitizeClassName(const String& input);
String sanitizeDatasetName(const String& input);
bool configureActiveDataset(const String& datasetName, String& message);
bool configureActiveClass(const String& className, String& message);
const char* sessionPhaseToString(SessionPhase phase);

bool startCaptureSession(String& message);
bool stopCaptureSession(size_t& captured, String& message);
bool pauseCaptureSession(String& message);
bool finishCaptureSession(String& message);
bool discardCaptureSession(String& message);
bool saveCaptureSession(const String& className, size_t& movedCount, size_t& movedBytes, String& message);
bool moveSessionFilesToClass(const String& sessionFolder, const String& classFolder, const String& sessionPrefix,
                             size_t& movedCount, size_t& movedBytes, String& errorMessage);

static esp_err_t index_handler(httpd_req_t* req);
static esp_err_t status_handler(httpd_req_t* req);
static esp_err_t preview_handler(httpd_req_t* req);
static esp_err_t start_handler(httpd_req_t* req);
static esp_err_t stop_handler(httpd_req_t* req);
static esp_err_t pause_handler(httpd_req_t* req);
static esp_err_t finish_handler(httpd_req_t* req);
static esp_err_t flash_handler(httpd_req_t* req);
static esp_err_t save_handler(httpd_req_t* req);
static esp_err_t discard_handler(httpd_req_t* req);
static esp_err_t dataset_handler(httpd_req_t* req);

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    Serial.begin(115200);
    delay(500);
    Serial.println("ESP32-CAM Dataset Capture");
    Serial.println("============================");

    cameraMutex = xSemaphoreCreateMutex();
    sessionMutex = xSemaphoreCreateMutex();

    if (cameraMutex == nullptr || sessionMutex == nullptr) {
        Serial.println("Failed to create mutexes");
        while (true) { delay(1000); }
    }

    if (!initWiFi()) {
        Serial.println("WiFi init failed");
        while (true) { delay(1000); }
    }

    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("Filesystem mount failed");
        while (true) { delay(1000); }
    }

    fsMutex = xSemaphoreCreateMutex();
    if (fsMutex == nullptr) {
        Serial.println("Failed to create filesystem mutex");
        while (true) { delay(1000); }
    }

    activeDatasetName = DATASET_NAME;
    datasetReady = false;
    datasetRootPath = "/" + activeDatasetName;
    sessionRootPath = datasetRootPath + "/" + SESSION_FOLDER_NAME;

    if (!ensureDirectory(datasetRootPath)) {
        Serial.println("Failed to create dataset root");
        while (true) { delay(1000); }
    }

    if (!ensureDirectory(sessionRootPath)) {
        Serial.println("Failed to create session root");
        while (true) { delay(1000); }
    }

    if (!prepareCameraConfiguration()) {
        Serial.println("Camera configuration failed");
        while (true) { delay(1000); }
    }

    const auto& cfg = hog.getImageProcessingConfig();
    Serial.printf("Camera input request: %dx%d format=%s\n",
                  cfg.input_width,
                  cfg.input_height,
                  pixelFormatToString(cfg.input_format));

    if (!initCamera(cfg)) {
        Serial.println("Camera init failed");
        while (true) { delay(1000); }
    }

    setupFlash();
    applyFlashLevel(0);
    captureTaskShouldStop = false;
    finishCompleted = false;

    session.phase = SessionPhase::Idle;
    session.tempFolder = "";
    session.imageCount = 0;
    session.nextIndex = 0;
    session.totalBytes = 0;
    session.lastCaptureMillis = 0;

    datasetStats.lastClass = "";
    datasetStats.lastCount = 0;
    datasetStats.lastBytes = 0;
    datasetStats.lastSavedMillis = 0;
    datasetStats.totalSaved = 0;

    startCameraServer();

    xTaskCreatePinnedToCore(
        captureTask,
        "CaptureTask",
        8192,
        nullptr,
        1,
        &captureTaskHandle,
        0
    );

    Serial.println("\n✓ Dataset capture server ready");
    Serial.print("Dashboard URL: http://");
    Serial.println(WiFi.localIP());
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

bool initWiFi() {
    Serial.print("Connecting to WiFi");

    WiFi.setSleep(false);
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname("ESP32-CAM-DATASET");

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
    }

    Serial.println("\n✗ WiFi connection failed");
    return false;
}

framesize_t resolveFrameSize(int width, int height) {
    const auto matches = [&](int w, int h) {
        return (width == w && height == h) || (width == h && height == w);
    };

    framesize_t fallbackSize = FRAMESIZE_QVGA;
    int fallbackWidth = 320;
    int fallbackHeight = 240;
    int bestDiff = std::numeric_limits<int>::max();

    const auto updateFallback = [&](int w, int h, framesize_t size) {
        const auto consider = [&](int cw, int ch) {
            int diff = abs(width - cw) + abs(height - ch);
            if (diff < bestDiff) {
                bestDiff = diff;
                fallbackSize = size;
                fallbackWidth = cw;
                fallbackHeight = ch;
            }
        };
        consider(w, h);
        if (w != h) {
            consider(h, w);
        }
    };

#ifdef FRAMESIZE_96X96
    updateFallback(96, 96, FRAMESIZE_96X96);
    if (matches(96, 96)) return FRAMESIZE_96X96;
#endif
#ifdef FRAMESIZE_160X120
    updateFallback(160, 120, FRAMESIZE_160X120);
    if (matches(160, 120)) return FRAMESIZE_160X120;
#endif
#ifdef FRAMESIZE_QQVGA
    updateFallback(160, 120, FRAMESIZE_QQVGA);
    if (matches(160, 120)) return FRAMESIZE_QQVGA;
#endif
#ifdef FRAMESIZE_QQVGA2
    updateFallback(128, 160, FRAMESIZE_QQVGA2);
    if (matches(128, 160)) return FRAMESIZE_QQVGA2;
#endif
#ifdef FRAMESIZE_QCIF
    updateFallback(176, 144, FRAMESIZE_QCIF);
    if (matches(176, 144)) return FRAMESIZE_QCIF;
#endif
#ifdef FRAMESIZE_HQVGA
    updateFallback(240, 176, FRAMESIZE_HQVGA);
    if (matches(240, 176)) return FRAMESIZE_HQVGA;
#endif
#ifdef FRAMESIZE_240X240
    updateFallback(240, 240, FRAMESIZE_240X240);
    if (matches(240, 240)) return FRAMESIZE_240X240;
#endif
#ifdef FRAMESIZE_QVGA
    updateFallback(320, 240, FRAMESIZE_QVGA);
    if (matches(320, 240)) return FRAMESIZE_QVGA;
#endif
#ifdef FRAMESIZE_CIF
    updateFallback(352, 288, FRAMESIZE_CIF);
    if (matches(352, 288)) return FRAMESIZE_CIF;
#endif
#ifdef FRAMESIZE_HVGA
    updateFallback(480, 320, FRAMESIZE_HVGA);
    if (matches(480, 320)) return FRAMESIZE_HVGA;
#endif
#ifdef FRAMESIZE_VGA
    updateFallback(640, 480, FRAMESIZE_VGA);
    if (matches(640, 480)) return FRAMESIZE_VGA;
#endif
#ifdef FRAMESIZE_SVGA
    updateFallback(800, 600, FRAMESIZE_SVGA);
    if (matches(800, 600)) return FRAMESIZE_SVGA;
#endif
#ifdef FRAMESIZE_XGA
    updateFallback(1024, 768, FRAMESIZE_XGA);
    if (matches(1024, 768)) return FRAMESIZE_XGA;
#endif
#ifdef FRAMESIZE_SXGA
    updateFallback(1280, 1024, FRAMESIZE_SXGA);
    if (matches(1280, 1024)) return FRAMESIZE_SXGA;
#endif
#ifdef FRAMESIZE_SXGA2
    updateFallback(1280, 960, FRAMESIZE_SXGA2);
    if (matches(1280, 960)) return FRAMESIZE_SXGA2;
#endif
#ifdef FRAMESIZE_720P
    updateFallback(1280, 720, FRAMESIZE_720P);
    if (matches(1280, 720)) return FRAMESIZE_720P;
#endif
#ifdef FRAMESIZE_HD
    updateFallback(1280, 720, FRAMESIZE_HD);
    if (matches(1280, 720)) return FRAMESIZE_HD;
#endif
#ifdef FRAMESIZE_UXGA
    updateFallback(1600, 1200, FRAMESIZE_UXGA);
    if (matches(1600, 1200)) return FRAMESIZE_UXGA;
#endif
#ifdef FRAMESIZE_1080P
    updateFallback(1920, 1080, FRAMESIZE_1080P);
    if (matches(1920, 1080)) return FRAMESIZE_1080P;
#endif
#ifdef FRAMESIZE_FHD
    updateFallback(1920, 1080, FRAMESIZE_FHD);
    if (matches(1920, 1080)) return FRAMESIZE_FHD;
#endif
#ifdef FRAMESIZE_P_FHD
    updateFallback(1920, 1080, FRAMESIZE_P_FHD);
    if (matches(1920, 1080)) return FRAMESIZE_P_FHD;
#endif
#ifdef FRAMESIZE_QXGA
    updateFallback(2048, 1536, FRAMESIZE_QXGA);
    if (matches(2048, 1536)) return FRAMESIZE_QXGA;
#endif
#ifdef FRAMESIZE_P_3MP
    updateFallback(2048, 1536, FRAMESIZE_P_3MP);
    updateFallback(2304, 1536, FRAMESIZE_P_3MP);
    if (matches(2048, 1536) || matches(2304, 1536)) return FRAMESIZE_P_3MP;
#endif
#ifdef FRAMESIZE_QHD
    updateFallback(2560, 1440, FRAMESIZE_QHD);
    if (matches(2560, 1440)) return FRAMESIZE_QHD;
#endif
#ifdef FRAMESIZE_WQXGA
    updateFallback(2560, 1600, FRAMESIZE_WQXGA);
    if (matches(2560, 1600)) return FRAMESIZE_WQXGA;
#endif
#ifdef FRAMESIZE_5MP
    updateFallback(2592, 1944, FRAMESIZE_5MP);
    if (matches(2592, 1944)) return FRAMESIZE_5MP;
#endif

    if (bestDiff == std::numeric_limits<int>::max()) {
        Serial.printf("[Camera] Unsupported resolution %dx%d, defaulting to QVGA\n", width, height);
        return FRAMESIZE_QVGA;
    }

    if (bestDiff != 0) {
        Serial.printf("[Camera] Unsupported resolution %dx%d, using closest %dx%d\n",
                      width, height, fallbackWidth, fallbackHeight);
    }

    return fallbackSize;
}

pixformat_t resolvePixelFormat(ImageProcessing::PixelFormat format) {
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
            Serial.println("[Camera] Unknown pixel format, defaulting to GRAYSCALE");
            return PIXFORMAT_GRAYSCALE;
    }
}

void setupFlash() {
    ledc_timer_config_t timerConfig{};
    timerConfig.speed_mode = FLASH_LED_PWM_SPEED_MODE;
    timerConfig.timer_num = FLASH_LED_PWM_TIMER;
    timerConfig.duty_resolution = static_cast<ledc_timer_bit_t>(FLASH_LED_PWM_RESOLUTION);
    timerConfig.freq_hz = FLASH_LED_PWM_FREQ;
    timerConfig.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t timerRes = ledc_timer_config(&timerConfig);
    if (timerRes != ESP_OK) {
        Serial.printf("[Flash] ledc_timer_config failed: %d\n", static_cast<int>(timerRes));
    }

    ledc_channel_config_t channelConfig{};
    channelConfig.speed_mode = FLASH_LED_PWM_SPEED_MODE;
    channelConfig.channel = FLASH_LED_PWM_CHANNEL;
    channelConfig.timer_sel = FLASH_LED_PWM_TIMER;
    channelConfig.intr_type = LEDC_INTR_DISABLE;
    channelConfig.gpio_num = FLASH_LED_PIN;
    channelConfig.duty = 0;
    channelConfig.hpoint = 0;
    esp_err_t channelRes = ledc_channel_config(&channelConfig);
    if (channelRes != ESP_OK) {
        Serial.printf("[Flash] ledc_channel_config failed: %d\n", static_cast<int>(channelRes));
    }

    applyFlashLevel(0);
}

void applyFlashLevel(size_t index) {
    if (index >= FLASH_LEVEL_COUNT) {
        index = 0;
    }
    flashLevelIndex = index;
    uint32_t duty = FLASH_LEVELS[index];
    ledc_set_duty(FLASH_LED_PWM_SPEED_MODE, FLASH_LED_PWM_CHANNEL, duty);
    ledc_update_duty(FLASH_LED_PWM_SPEED_MODE, FLASH_LED_PWM_CHANNEL);
}

size_t cycleFlashLevel() {
    size_t next = (flashLevelIndex + 1U) % FLASH_LEVEL_COUNT;
    applyFlashLevel(next);
    return flashLevelIndex;
}

const char* currentFlashLabel() {
    size_t idx = flashLevelIndex;
    if (idx >= FLASH_LEVEL_COUNT) {
        idx = 0;
    }
    return FLASH_LEVEL_LABELS[idx];
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
    config.xclk_freq_hz = 10000000;

    pixformat_t pixelFormat = resolvePixelFormat(cfg.input_format);
    framesize_t frameSize = resolveFrameSize(cfg.input_width, cfg.input_height);

    uint8_t quality = cfg.jpeg_quality == 0 ? 12 : cfg.jpeg_quality;
    if (quality < 10) {
        quality = 10;
    } else if (quality > 63) {
        quality = 63;
    }

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

    int actualWidth = cfg.input_width;
    int actualHeight = cfg.input_height;

    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) {
        actualWidth = fb->width;
        actualHeight = fb->height;
        esp_camera_fb_return(fb);
    }

    Serial.printf("✓ Camera initialized (%dx%d %s)\n",
                  actualWidth,
                  actualHeight,
                  pixelFormatToString(cfg.input_format));
    return true;
}

bool ensureDirectory(const String& path) {
    if (path.length() == 0) {
        return false;
    }

    String normalized = path;
    if (!normalized.startsWith("/")) {
        normalized = "/" + normalized;
    }

    if (normalized == "/") {
        return true;
    }

    FsLockGuard lock(fsMutex, pdMS_TO_TICKS(500));
    if (!lock.isLocked()) {
        return false;
    }

    String partial;
    size_t start = 1;
    while (start < normalized.length()) {
        int nextSlash = normalized.indexOf('/', start);
        String segment;
        if (nextSlash == -1) {
            segment = normalized.substring(start);
            start = normalized.length();
        } else {
            segment = normalized.substring(start, nextSlash);
            start = static_cast<size_t>(nextSlash + 1);
        }

        if (segment.length() == 0) {
            continue;
        }

        if (partial.length() == 0) {
            partial = "/" + segment;
        } else {
            partial += "/";
            partial += segment;
        }

        if (!RF_FS_EXISTS(partial)) {
            if (!RF_FS_MKDIR(partial)) {
                return false;
            }
        }
    }

    return true;
}

static bool deleteDirectoryRecursiveLocked(const String& path) {
    if (!RF_FS_EXISTS(path)) {
        return true;
    }

    File dir = RF_FS_OPEN(path.c_str(), "r");
    if (!dir) {
        return false;
    }

    if (!dir.isDirectory()) {
        dir.close();
        return RF_FS_REMOVE(path);
    }

    File entry = dir.openNextFile();
    while (entry) {
        String entryName = String(entry.name());
        String entryPath;
        if (entryName.startsWith("/")) {
            entryPath = entryName;
        } else {
            entryPath = path;
            if (!entryPath.endsWith("/")) {
                entryPath += "/";
            }
            entryPath += entryName;
        }

        if (entry.isDirectory()) {
            entry.close();
            if (!deleteDirectoryRecursiveLocked(entryPath)) {
                dir.close();
                return false;
            }
        } else {
            entry.close();
            if (!RF_FS_REMOVE(entryPath)) {
                dir.close();
                return false;
            }
        }

        entry = dir.openNextFile();
    }

    dir.close();
    return RF_FS_RMDIR(path);
}

bool deleteDirectoryRecursive(const String& path) {
    FsLockGuard lock(fsMutex, pdMS_TO_TICKS(1000));
    if (!lock.isLocked()) {
        return false;
    }
    return deleteDirectoryRecursiveLocked(path);
}

String sanitizeClassName(const String& input) {
    String result;
    result.reserve(input.length());

    for (size_t i = 0; i < input.length(); ++i) {
        char c = input.charAt(i);
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += c;
        } else if (c == '-' || c == '_') {
            result += c;
        } else if (c == ' ' || c == '.') {
            result += '_';
        }
    }

    while (result.startsWith("_") || result.startsWith("-")) {
        result.remove(0, 1);
    }

    while (result.endsWith("_") || result.endsWith("-")) {
        result.remove(result.length() - 1);
    }

    return result;
}

String sanitizeDatasetName(const String& input) {
    String candidate = sanitizeClassName(input);
    candidate.trim();
    if (candidate.length() == 0) {
        return "";
    }

    String lower = candidate;
    lower.toLowerCase();
    String reservedSession = String(SESSION_FOLDER_NAME);
    String reservedSessionBare = reservedSession;
    if (reservedSessionBare.startsWith("_")) {
        reservedSessionBare.remove(0, 1);
    }

    const String singleSession = "_session";
    const String singleSessionBare = "session";

    if (lower == reservedSession || lower == reservedSessionBare ||
        lower == singleSession || lower == singleSessionBare) {
        return "";
    }

    return candidate;
}

bool configureActiveClass(const String& className, String& message) {
    String candidate = sanitizeClassName(className);
    candidate.trim();
    if (candidate.length() == 0) {
        message = "Invalid class name";
        return false;
    }

    String lower = candidate;
    lower.toLowerCase();
    if (lower == "_session" || lower == "session") {
        message = "Class name reserved";
        return false;
    }

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        message = "Session busy";
        return false;
    }

    bool canConfigure = (session.phase == SessionPhase::Idle && session.tempFolder.length() == 0 && !finishCompleted);
    xSemaphoreGive(sessionMutex);

    if (!canConfigure) {
        message = "Stop capture before changing class";
        return false;
    }

    activeClassName = candidate;
    classReady = true;
    message = activeClassName;
    return true;
}

bool configureActiveDataset(const String& datasetName, String& message) {
    String sanitized = sanitizeDatasetName(datasetName);
    if (sanitized.length() == 0) {
        message = "Invalid dataset name";
        return false;
    }

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        message = "Session busy";
        return false;
    }

    bool canConfigure = (session.phase == SessionPhase::Idle && session.tempFolder.length() == 0);
    xSemaphoreGive(sessionMutex);

    if (!canConfigure) {
        message = "Stop current capture before changing dataset";
        return false;
    }

    String targetRoot = "/" + sanitized;
    String targetSessionRoot = targetRoot + "/" + SESSION_FOLDER_NAME;

    if (!ensureDirectory(targetRoot)) {
        message = "Failed to prepare dataset root";
        return false;
    }

    if (!ensureDirectory(targetSessionRoot)) {
        message = "Failed to prepare session folder";
        return false;
    }

    String previousDatasetName = activeDatasetName;
    String previousRootPath = datasetRootPath;
    String previousSessionPath = sessionRootPath;
    bool previousReady = datasetReady;

    activeDatasetName = sanitized;
    datasetRootPath = targetRoot;
    sessionRootPath = targetSessionRoot;
    datasetReady = false;

    if (!exportCameraConfiguration()) {
        activeDatasetName = previousDatasetName;
        datasetRootPath = previousRootPath;
        sessionRootPath = previousSessionPath;
        datasetReady = previousReady;
        message = "Failed to export camera configuration";
        return false;
    }

    datasetReady = true;
    message = datasetRootPath;
    return true;
}

const char* sessionPhaseToString(SessionPhase phase) {
    switch (phase) {
        case SessionPhase::Idle:
            return "Idle";
        case SessionPhase::Capturing:
            return "Capturing";
        case SessionPhase::Paused:
            return "Paused";
        case SessionPhase::AwaitingDecision:
            return "Awaiting";
        case SessionPhase::Saving:
            return "Saving";
        case SessionPhase::Discarding:
            return "Discarding";
        case SessionPhase::Finishing:
            return "Finishing";
        default:
            return "Unknown";
    }
}

bool saveFrameToFile(const String& folder, uint32_t index, camera_fb_t* fb, size_t& bytesWritten) {
    bytesWritten = 0;
    if (folder.length() == 0 || fb == nullptr) {
        return false;
    }

    FsLockGuard lock(fsMutex, pdMS_TO_TICKS(1000));
    if (!lock.isLocked()) {
        return false;
    }

    uint8_t* jpg_buf = fb->buf;
    size_t jpg_len = fb->len;
    bool needsFree = false;

    if (fb->format != PIXFORMAT_JPEG) {
        if (!frame2jpg(fb, 90, &jpg_buf, &jpg_len)) {
            return false;
        }
        needsFree = true;
    }

    char filename[128];
    snprintf(filename, sizeof(filename), "%s/img_%06lu.jpg", folder.c_str(), static_cast<unsigned long>(index + 1));

    File file = RF_FS_OPEN(filename, "w");
    if (!file) {
        if (needsFree) {
            free(jpg_buf);
        }
        Serial.printf("[Capture] Failed to open file: %s\n", filename);
        return false;
    }

    size_t written = file.write(jpg_buf, jpg_len);
    file.close();

    if (needsFree) {
        free(jpg_buf);
    }

    if (written != jpg_len) {
        RF_FS_REMOVE(filename);
        Serial.printf("[Capture] Incomplete write: %s\n", filename);
        return false;
    }

    bytesWritten = written;
    return true;
}

bool startCaptureSession(String& message) {
    if (!datasetReady) {
        message = "Dataset name not configured";
        return false;
    }
    if (!classReady) {
        message = "Class name not configured";
        return false;
    }

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    if (finishCompleted) {
        xSemaphoreGive(sessionMutex);
        message = "Capture finished";
        return false;
    }

    if (session.phase == SessionPhase::Capturing) {
        xSemaphoreGive(sessionMutex);
        message = "Capture already running";
        return false;
    }

    if (session.phase == SessionPhase::AwaitingDecision ||
        session.phase == SessionPhase::Saving ||
        session.phase == SessionPhase::Discarding ||
        session.phase == SessionPhase::Finishing) {
        xSemaphoreGive(sessionMutex);
        message = "Pending session must be saved or discarded";
        return false;
    }

    if (session.phase == SessionPhase::Paused) {
        session.phase = SessionPhase::Capturing;
        message = "Capture resumed";
        xSemaphoreGive(sessionMutex);
        return true;
    }

    // New session
    if (!ensureDirectory(sessionRootPath)) {
        xSemaphoreGive(sessionMutex);
        message = "Failed to prepare session root";
        return false;
    }

    String newFolder = sessionRootPath + "/" + String(millis());
    if (!ensureDirectory(newFolder)) {
        xSemaphoreGive(sessionMutex);
        message = "Failed to create session folder";
        return false;
    }

    session.phase = SessionPhase::Capturing;
    session.tempFolder = newFolder;
    session.imageCount = 0;
    session.nextIndex = 0;
    session.totalBytes = 0;
    session.lastCaptureMillis = 0;

    xSemaphoreGive(sessionMutex);

    message = "Capture started";
    return true;
}

bool stopCaptureSession(size_t& captured, String& message) {
    captured = 0;
    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    if (finishCompleted) {
        message = "Capture finished";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    if (session.phase != SessionPhase::Capturing && session.phase != SessionPhase::Paused) {
        captured = session.imageCount;
        message = "No active capture";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    session.phase = SessionPhase::AwaitingDecision;
    captured = session.imageCount;
    message = "Capture stopped";

    xSemaphoreGive(sessionMutex);
    return true;
}

bool pauseCaptureSession(String& message) {
    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    if (finishCompleted) {
        message = "Capture finished";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    if (session.phase != SessionPhase::Capturing) {
        message = "Capture not running";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    session.phase = SessionPhase::Paused;
    message = "Capture paused";
    xSemaphoreGive(sessionMutex);
    return true;
}

bool finishCaptureSession(String& message) {
    if (finishCompleted) {
        message = "Capture already finished";
        return false;
    }

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    session.phase = SessionPhase::Finishing;
    captureTaskShouldStop = true;
    xSemaphoreGive(sessionMutex);

    unsigned long waitStart = millis();
    while (captureTaskHandle != nullptr && (millis() - waitStart) < 3000UL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (captureTaskHandle != nullptr) {
        vTaskDelete(captureTaskHandle);
        captureTaskHandle = nullptr;
    }

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        session.phase = SessionPhase::Idle;
        session.tempFolder = "";
        session.imageCount = 0;
        session.nextIndex = 0;
        session.totalBytes = 0;
        session.lastCaptureMillis = 0;
        xSemaphoreGive(sessionMutex);
    }

    bool sessionsRemoved = deleteDirectoryRecursive(sessionRootPath);
    bool sessionsPrepared = ensureDirectory(datasetRootPath) && ensureDirectory(sessionRootPath);

    applyFlashLevel(0);
    finishCompleted = true;
    captureTaskShouldStop = true;

    esp_camera_deinit();

    if (!sessionsRemoved || !sessionsPrepared) {
        message = "Finished with warnings";
        return false;
    }

    message = "Capture finished";
    return true;
}

bool discardCaptureSession(String& message) {
    String folder;

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    if (session.phase == SessionPhase::Capturing || session.phase == SessionPhase::Paused) {
        xSemaphoreGive(sessionMutex);
        message = "Stop capture before discarding";
        return false;
    }

        if (finishCompleted) {
            xSemaphoreGive(sessionMutex);
            message = "Nothing to discard";
            return true;
        }

    if (session.phase == SessionPhase::Idle && session.tempFolder.length() == 0) {
        xSemaphoreGive(sessionMutex);
        message = "Nothing to discard";
        return true;
    }

    session.phase = SessionPhase::Discarding;
    folder = session.tempFolder;

    xSemaphoreGive(sessionMutex);

    bool removed = deleteDirectoryRecursive(folder);

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        session.phase = SessionPhase::Idle;
        session.tempFolder = "";
        session.imageCount = 0;
        session.nextIndex = 0;
        session.totalBytes = 0;
        session.lastCaptureMillis = 0;
        xSemaphoreGive(sessionMutex);
    }

    if (!removed) {
        message = "Failed to clear session data";
        return false;
    }

    message = "Session discarded";
    return true;
}

bool moveSessionFilesToClass(const String& sessionFolder, const String& classFolder, const String& sessionPrefix,
                             size_t& movedCount, size_t& movedBytes, String& errorMessage) {
    movedCount = 0;
    movedBytes = 0;

    FsLockGuard lock(fsMutex, pdMS_TO_TICKS(1000));
    if (!lock.isLocked()) {
        errorMessage = "Filesystem busy";
        return false;
    }

    File dir = RF_FS_OPEN(sessionFolder.c_str(), "r");
    if (!dir) {
        errorMessage = "Session folder missing";
        return false;
    }

    if (!dir.isDirectory()) {
        dir.close();
        errorMessage = "Invalid session folder";
        return false;
    }

    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String subName = String(entry.name());
            entry.close();
            String subPath;
            if (subName.startsWith("/")) {
                subPath = subName;
            } else {
                subPath = sessionFolder;
                if (!subPath.endsWith("/")) {
                    subPath += "/";
                }
                subPath += subName;
            }
            if (!deleteDirectoryRecursive(subPath)) {
                dir.close();
                errorMessage = "Nested directories removed with warnings";
                return false;
            }
            entry = dir.openNextFile();
            continue;
        }

        String entryName = String(entry.name());
        size_t fileSize = entry.size();
        entry.close();

        String sourcePath;
        if (entryName.startsWith("/")) {
            sourcePath = entryName;
        } else {
            sourcePath = sessionFolder;
            if (!sourcePath.endsWith("/")) {
                sourcePath += "/";
            }
            sourcePath += entryName;
        }

        String baseName = sourcePath.substring(sourcePath.lastIndexOf('/') + 1);
        String targetPath = classFolder;
        if (!targetPath.endsWith("/")) {
            targetPath += "/";
        }
        targetPath += sessionPrefix;
        targetPath += "_";
        targetPath += baseName;

        if (RF_FS_EXISTS(targetPath)) {
            String base = targetPath;
            String ext;
            int dotIndex = targetPath.lastIndexOf('.');
            if (dotIndex > 0) {
                base = targetPath.substring(0, dotIndex);
                ext = targetPath.substring(dotIndex);
            }

            uint32_t attempt = 1;
            bool uniqueFound = false;
            while (attempt < 1000 && !uniqueFound) {
                String candidate = base + "_" + String(attempt) + ext;
                if (!RF_FS_EXISTS(candidate)) {
                    targetPath = candidate;
                    uniqueFound = true;
                    break;
                }
                attempt++;
            }

            if (!uniqueFound) {
                dir.close();
                errorMessage = "Unable to generate unique filename";
                return false;
            }
        }

        if (!rf_rename(sourcePath, targetPath)) {
            dir.close();
            errorMessage = "Failed to move file";
            return false;
        }

        movedCount++;
        movedBytes += fileSize;

        entry = dir.openNextFile();
    }

    dir.close();
    RF_FS_RMDIR(sessionFolder);
    return true;
}

bool saveCaptureSession(const String& className, size_t& movedCount, size_t& movedBytes, String& message) {
    movedCount = 0;
    movedBytes = 0;

    String sessionFolder;
    size_t sessionImages = 0;
    size_t sessionBytes = 0;

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        message = "Session lock timeout";
        return false;
    }

    if (finishCompleted) {
        message = "Capture finished";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    if (session.phase != SessionPhase::AwaitingDecision) {
        message = "No session ready to save";
        xSemaphoreGive(sessionMutex);
        return false;
    }

    session.phase = SessionPhase::Saving;
    sessionFolder = session.tempFolder;
    sessionImages = session.imageCount;
    sessionBytes = session.totalBytes;

    xSemaphoreGive(sessionMutex);

    if (sessionImages == 0) {
        discardCaptureSession(message);
        message = "No images captured";
        return false;
    }

    String classFolder = datasetRootPath;
    if (!classFolder.endsWith("/")) {
        classFolder += "/";
    }
    classFolder += className;

    if (!ensureDirectory(classFolder)) {
        message = "Failed to create class folder";
        if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            session.phase = SessionPhase::AwaitingDecision;
            xSemaphoreGive(sessionMutex);
        }
        return false;
    }

    String sessionPrefix = sessionFolder.substring(sessionFolder.lastIndexOf('/') + 1);
    String errorMessage;

    bool moved = moveSessionFilesToClass(sessionFolder, classFolder, sessionPrefix, movedCount, movedBytes, errorMessage);

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (moved) {
            session.phase = SessionPhase::Idle;
            session.tempFolder = "";
            session.imageCount = 0;
            session.nextIndex = 0;
            session.totalBytes = 0;
            session.lastCaptureMillis = 0;

            datasetStats.lastClass = className;
            datasetStats.lastCount = movedCount;
            datasetStats.lastBytes = movedBytes;
            datasetStats.lastSavedMillis = millis();
            datasetStats.totalSaved += movedCount;
        } else {
            session.phase = SessionPhase::AwaitingDecision;
        }
        xSemaphoreGive(sessionMutex);
    }

    if (!moved) {
        message = errorMessage.length() ? errorMessage : "Failed to move files";
        return false;
    }

    message = "Session saved";
    return true;
}

void captureTask(void* parameter) {
    Serial.println("[Capture] Task started");
    unsigned long lastAttempt = 0;

    while (true) {
        if (captureTaskShouldStop) {
            break;
        }

        SessionPhase phaseSnapshot = SessionPhase::Idle;
        String folderSnapshot;
        uint32_t indexSnapshot = 0;
        unsigned long lastCaptureSnapshot = 0;

        if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            phaseSnapshot = session.phase;
            folderSnapshot = session.tempFolder;
            indexSnapshot = session.nextIndex;
            lastCaptureSnapshot = session.lastCaptureMillis;
            xSemaphoreGive(sessionMutex);
        }

        if (phaseSnapshot != SessionPhase::Capturing) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        unsigned long now = millis();
        if (now - lastCaptureSnapshot < CAPTURE_INTERVAL_MS) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(cameraMutex);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytesWritten = 0;
        bool saved = saveFrameToFile(folderSnapshot, indexSnapshot, fb, bytesWritten);

        esp_camera_fb_return(fb);
        xSemaphoreGive(cameraMutex);

        if (saved) {
            if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                session.imageCount += 1;
                session.nextIndex += 1;
                session.totalBytes += bytesWritten;
                session.lastCaptureMillis = millis();
                xSemaphoreGive(sessionMutex);
            }
        } else {
            if (millis() - lastAttempt > 2000) {
                Serial.println("[Capture] Failed to store frame");
                lastAttempt = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    Serial.println("[Capture] Task stopping");
    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (session.phase == SessionPhase::Capturing || session.phase == SessionPhase::Paused) {
            session.phase = SessionPhase::Idle;
        }
        xSemaphoreGive(sessionMutex);
    }
    captureTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

static esp_err_t send_text(httpd_req_t* req, const char* contentType, const char* payload) {
    httpd_resp_set_type(req, contentType);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, payload, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t index_handler(httpd_req_t* req) {
    const char* html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM Dataset Capture</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; background: #101010; color: #f5f5f5; margin: 0; padding: 20px; }
        h1 { text-align: center; color: #4CAF50; }
        .container { max-width: 860px; margin: auto; }
        .card { background: #1e1e1e; border-radius: 12px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }
        button { padding: 12px 20px; margin: 6px; border: none; border-radius: 6px; font-size: 1rem; cursor: pointer; }
        button:disabled { opacity: 0.4; cursor: not-allowed; }
        #startBtn { background: #4CAF50; color: white; }
        #pauseBtn { background: #FF9800; color: white; }
        #stopBtn { background: #E53935; color: white; }
        #finishBtn { background: #673AB7; color: white; }
        #saveBtn { background: #1976D2; color: white; }
        #discardBtn { background: #757575; color: white; }
        #flashBtn { background: #607D8B; color: white; }
        #preview { width: 100%; border-radius: 12px; border: 3px solid #4CAF50; background: #000; min-height: 200px; object-fit: contain; }
        .status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; }
        .status-item { background: #262626; border-radius: 8px; padding: 12px; }
        .label { font-size: 0.85rem; color: #aaaaaa; }
        .value { font-size: 1.2rem; margin-top: 4px; }
        .controls { display: flex; flex-wrap: wrap; justify-content: center; gap: 10px; }
        .save-panel { display: none; margin-top: 12px; }
        .save-panel input { padding: 10px; border-radius: 6px; border: none; width: calc(100% - 120px); margin-right: 10px; }
        .notice { color: #FFCA28; margin-top: 10px; font-size: 0.9rem; }
        .dataset-row { display: flex; flex-wrap: wrap; gap: 10px; }
        .dataset-row input { flex: 1; min-width: 220px; padding: 10px; border-radius: 6px; border: none; background: #0d0d0d; color: #f5f5f5; }
        .dataset-card { border: 1px solid #2e7d32; }
        .dataset-hint { color: #cfd8dc; font-size: 0.9rem; margin-top: 8px; }
    </style>
</head>
<body>
<div class="container">
    <h1>📸 ESP32-CAM Dataset Capture</h1>
    <div class="card dataset-card">
        <div class="dataset-row">
            <input type="text" id="datasetInput" placeholder="Enter dataset name">
            <button id="datasetBtn">Set Dataset</button>
        </div>
        <div class="dataset-row">
            <input type="text" id="classLabelInput" placeholder="Enter class name">
            <button id="classBtn">Set Class</button>
        </div>
        <div class="dataset-hint" id="datasetHint">Dataset name required before capture.</div>
        <div class="dataset-hint" id="classHint">Class name required before capture.</div>
    </div>
    <div class="card">
        <div class="controls">
            <button id="startBtn">Start Capture</button>
            <button id="pauseBtn" disabled>Pause</button>
            <button id="stopBtn" disabled>Stop Capture</button>
            <button id="finishBtn">Finish</button>
        </div>
        <div class="controls">
            <button id="flashBtn">Flash: Off</button>
        </div>
        <div class="save-panel" id="savePanel">
            <button id="saveBtn">Save</button>
            <button id="discardBtn">Discard</button>
            <div class="notice" id="saveNotice"></div>
        </div>
        <img id="preview" alt="Live preview loading...">
    </div>
    <div class="card">
        <div class="status-grid">
            <div class="status-item"><div class="label">Phase</div><div class="value" id="phase">-</div></div>
            <div class="status-item"><div class="label">Captured</div><div class="value" id="captured">0</div></div>
            <div class="status-item"><div class="label">Capture Interval (ms)</div><div class="value" id="interval">-</div></div>
            <div class="status-item"><div class="label">Session Size (KB)</div><div class="value" id="sessionBytes">0</div></div>
            <div class="status-item"><div class="label">Storage Used</div><div class="value" id="storageUsage">-</div></div>
            <div class="status-item"><div class="label">Last Saved</div><div class="value" id="lastSaved">-</div></div>
            <div class="status-item"><div class="label">Dataset Root</div><div class="value" id="datasetRoot">-</div></div>
            <div class="status-item"><div class="label">Flash</div><div class="value" id="flashState">Off</div></div>
            <div class="status-item"><div class="label">Finished</div><div class="value" id="finishedState">No</div></div>
        </div>
    </div>
</div>
<script>
const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');
const pauseBtn = document.getElementById('pauseBtn');
const saveBtn = document.getElementById('saveBtn');
const discardBtn = document.getElementById('discardBtn');
const finishBtn = document.getElementById('finishBtn');
const flashBtn = document.getElementById('flashBtn');
const previewImg = document.getElementById('preview');
const classLabelInput = document.getElementById('classLabelInput');
const classBtn = document.getElementById('classBtn');
const classHint = document.getElementById('classHint');
const savePanel = document.getElementById('savePanel');
const saveNotice = document.getElementById('saveNotice');
const datasetInput = document.getElementById('datasetInput');
const datasetBtn = document.getElementById('datasetBtn');
const datasetHint = document.getElementById('datasetHint');
let statusTimer = null;
let previewTimer = null;
let finished = false;
let previewPending = false;

previewImg.onload = () => {
    previewPending = false;
};

previewImg.onerror = () => {
    previewPending = false;
};

function refreshPreview() {
    if (finished || previewPending) {
        return;
    }
    previewPending = true;
    previewImg.src = `/preview.jpg?ts=${Date.now()}`;
}

function updateStatus() {
    fetch(`/status?ts=${Date.now()}`, { cache: 'no-store' })
        .then(res => res.json())
        .then(data => {
            document.getElementById('phase').textContent = data.phase;
            document.getElementById('captured').textContent = data.captured;
            document.getElementById('interval').textContent = data.capture_interval;
            document.getElementById('sessionBytes').textContent = (data.session_bytes / 1024).toFixed(1);
            document.getElementById('storageUsage').textContent = `${(data.used_bytes / 1024).toFixed(1)} / ${(data.total_bytes / 1024).toFixed(1)} KB`;
            document.getElementById('lastSaved').textContent = data.last_saved_desc;
            document.getElementById('datasetRoot').textContent = data.dataset_root;
            datasetHint.textContent = data.dataset_ready ? `Dataset: ${data.dataset_name}` : 'Dataset name required before capture.';
            classHint.textContent = data.class_ready ? `Class: ${data.class_name}` : 'Class name required before capture.';
            const controlsLocked = data.phase !== 'Idle';
            datasetInput.disabled = controlsLocked;
            datasetBtn.disabled = controlsLocked;
            classLabelInput.disabled = controlsLocked;
            classBtn.disabled = controlsLocked;
            document.getElementById('flashState').textContent = data.flash_label;
            document.getElementById('finishedState').textContent = data.finished ? 'Yes' : 'No';
            flashBtn.textContent = `Flash: ${data.flash_label}`;

            if (data.finished) {
                finished = true;
                startBtn.disabled = true;
                pauseBtn.disabled = true;
                stopBtn.disabled = true;
                finishBtn.disabled = true;
                flashBtn.disabled = true;
                savePanel.style.display = 'none';
                if (statusTimer) {
                    clearInterval(statusTimer);
                    statusTimer = null;
                }
                return;
            }

            startBtn.disabled = !(data.phase === 'Idle' || data.phase === 'Paused') || !data.dataset_ready || !data.class_ready;
            startBtn.textContent = data.phase === 'Paused' ? 'Resume Capture' : 'Start Capture';
            pauseBtn.disabled = data.phase !== 'Capturing';
            stopBtn.disabled = !(data.phase === 'Capturing' || data.phase === 'Paused');
            finishBtn.disabled = data.phase === 'Saving' || data.phase === 'Discarding' || data.phase === 'Finishing';
            flashBtn.disabled = data.phase === 'Saving' || data.phase === 'Discarding' || data.phase === 'Finishing';

            if (data.phase === 'Capturing') {
                savePanel.style.display = 'none';
                saveNotice.textContent = '';
                refreshPreview();
            } else if (data.phase === 'Awaiting') {
                savePanel.style.display = 'block';
                saveNotice.textContent = `${data.captured} images ready to save.`;
                refreshPreview();
            } else if (data.phase === 'Idle') {
                savePanel.style.display = 'none';
                saveNotice.textContent = '';
            }
        })
        .catch(() => {
            if (!finished) {
                document.getElementById('phase').textContent = 'Error';
            }
        });
}

startBtn.addEventListener('click', () => {
    fetch('/start', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            updateStatus();
        })
        .catch(err => alert('Start failed: ' + err));
});

stopBtn.addEventListener('click', () => {
    fetch('/stop', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(`${data.message}. Captured: ${data.captured}`);
            updateStatus();
        })
        .catch(err => alert('Stop failed: ' + err));
});

pauseBtn.addEventListener('click', () => {
    fetch('/pause', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            updateStatus();
        })
        .catch(err => alert('Pause failed: ' + err));
});

finishBtn.addEventListener('click', () => {
    if (!confirm('Finish capture session and shut down streaming?')) {
        return;
    }
    fetch('/finish', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            finished = data.success;
            if (finished) {
                if (statusTimer) {
                    clearInterval(statusTimer);
                    statusTimer = null;
                }
                if (previewTimer) {
                    clearInterval(previewTimer);
                    previewTimer = null;
                }
            }
            startBtn.disabled = true;
            pauseBtn.disabled = true;
            stopBtn.disabled = true;
            finishBtn.disabled = true;
            flashBtn.disabled = true;
            document.getElementById('finishedState').textContent = data.success ? 'Yes' : 'No';
            document.getElementById('phase').textContent = data.success ? 'Finished' : document.getElementById('phase').textContent;
            if (!data.success) {
                updateStatus();
            }
        })
        .catch(err => alert('Finish failed: ' + err));
});

datasetBtn.addEventListener('click', () => {
    const name = datasetInput.value.trim();
    if (!name) {
        alert('Enter a dataset name');
        return;
    }
    fetch(`/dataset?name=${encodeURIComponent(name)}`, { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            if (data.dataset_ready) {
                datasetHint.textContent = `Dataset: ${data.dataset_name}`;
                datasetInput.disabled = true;
                datasetBtn.disabled = true;
            } else {
                datasetHint.textContent = data.message;
            }
            updateStatus();
        })
        .catch(err => alert('Failed to set dataset name: ' + err));
});

classBtn.addEventListener('click', () => {
    const name = classLabelInput.value.trim();
    if (!name) {
        alert('Enter a class name');
        return;
    }
    fetch(`/class?name=${encodeURIComponent(name)}`, { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            if (data.class_ready) {
                classHint.textContent = `Class: ${data.class_name}`;
                classLabelInput.disabled = true;
                classBtn.disabled = true;
            } else {
                classHint.textContent = data.message;
            }
            updateStatus();
        })
        .catch(err => alert('Failed to set class name: ' + err));
});

flashBtn.addEventListener('click', () => {
    fetch('/flash', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            flashBtn.textContent = `Flash: ${data.label}`;
            document.getElementById('flashState').textContent = data.label;
        })
        .catch(err => alert('Flash toggle failed: ' + err));
});

saveBtn.addEventListener('click', () => {
    fetch('/save', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            updateStatus();
        })
        .catch(err => alert('Save failed: ' + err));
});

discardBtn.addEventListener('click', () => {
    fetch('/discard', { method: 'POST' })
        .then(res => res.json())
        .then(data => {
            alert(data.message);
            updateStatus();
        })
        .catch(err => alert('Discard failed: ' + err));
});

statusTimer = setInterval(updateStatus, 1000);
previewTimer = setInterval(() => {
    refreshPreview();
}, 800);
updateStatus();
refreshPreview();
</script>
</body>
</html>
)rawliteral";

    return send_text(req, "text/html", html);
}

static esp_err_t status_handler(httpd_req_t* req) {
    SessionPhase phaseSnapshot = SessionPhase::Idle;
    size_t capturedCount = 0;
    size_t sessionBytes = 0;
    unsigned long lastCapture = 0;
    String rootPath = datasetRootPath;
    bool datasetReadySnapshot = datasetReady;
    String datasetNameSnapshot = activeDatasetName;
    bool classReadySnapshot = classReady;
    String classNameSnapshot = activeClassName;
    String lastClass = datasetStats.lastClass;
    size_t lastCount = datasetStats.lastCount;
    size_t lastBytes = datasetStats.lastBytes;
    unsigned long lastSavedMillis = datasetStats.lastSavedMillis;
    bool finishedSnapshot = finishCompleted;

    if (xSemaphoreTake(sessionMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        phaseSnapshot = session.phase;
        capturedCount = session.imageCount;
        sessionBytes = session.totalBytes;
        lastCapture = session.lastCaptureMillis;
    rootPath = datasetRootPath;
    datasetReadySnapshot = datasetReady;
    datasetNameSnapshot = activeDatasetName;
    classReadySnapshot = classReady;
    classNameSnapshot = activeClassName;
        lastClass = datasetStats.lastClass;
        lastCount = datasetStats.lastCount;
        lastBytes = datasetStats.lastBytes;
        lastSavedMillis = datasetStats.lastSavedMillis;
        xSemaphoreGive(sessionMutex);
    }

    char lastSavedDesc[96];
    if (lastClass.length() > 0) {
        snprintf(lastSavedDesc, sizeof(lastSavedDesc), "%s (%u images)", lastClass.c_str(), static_cast<unsigned>(lastCount));
    } else {
        snprintf(lastSavedDesc, sizeof(lastSavedDesc), "-");
    }

    size_t usedBytes = 0;
    size_t totalBytes = 0;
    {
        FsLockGuard lock(fsMutex, pdMS_TO_TICKS(200));
        if (lock.isLocked()) {
            usedBytes = RF_USED_BYTES();
            totalBytes = RF_TOTAL_BYTES();
        }
    }

    size_t flashIdx = flashLevelIndex;
    const char* flashLabel = currentFlashLabel();

    char response[760];
    snprintf(response, sizeof(response),
             "{\"phase\":\"%s\",\"captured\":%u,\"session_bytes\":%u,\"last_capture\":%lu,"
             "\"capture_interval\":%lu,\"dataset_root\":\"%s\",\"dataset_name\":\"%s\",\"dataset_ready\":%s,"
             "\"class_name\":\"%s\",\"class_ready\":%s,\"storage\":\"%s\","
             "\"last_saved_desc\":\"%s\",\"last_saved_bytes\":%u,"
             "\"used_bytes\":%u,\"total_bytes\":%u,\"flash_level\":%u,\"flash_label\":\"%s\",\"finished\":%s}",
             sessionPhaseToString(phaseSnapshot),
             static_cast<unsigned>(capturedCount),
             static_cast<unsigned>(sessionBytes),
             lastCapture,
             CAPTURE_INTERVAL_MS,
             rootPath.c_str(),
             datasetNameSnapshot.c_str(),
             datasetReadySnapshot ? "true" : "false",
             classNameSnapshot.c_str(),
             classReadySnapshot ? "true" : "false",
             rf_storage_type(),
             lastSavedDesc,
             static_cast<unsigned>(lastBytes),
             static_cast<unsigned>(usedBytes),
             static_cast<unsigned>(totalBytes),
             static_cast<unsigned>(flashIdx),
             flashLabel,
             finishedSnapshot ? "true" : "false");

    return send_text(req, "application/json", response);
}

static esp_err_t preview_handler(httpd_req_t* req) {
    if (finishCompleted) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Stream finished", HTTPD_RESP_USE_STRLEN);
    }

    if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera busy");
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(cameraMutex);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Frame capture failed");
    }

    uint8_t* jpg_buf = fb->buf;
    size_t jpg_len = fb->len;
    bool needsFree = false;

    if (fb->format != PIXFORMAT_JPEG) {
        if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
            esp_camera_fb_return(fb);
            xSemaphoreGive(cameraMutex);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JPEG conversion failed");
        }
        needsFree = true;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t res = httpd_resp_send(req, reinterpret_cast<const char*>(jpg_buf), jpg_len);

    if (needsFree) {
        free(jpg_buf);
    }

    esp_camera_fb_return(fb);
    xSemaphoreGive(cameraMutex);

    return res;
}

static esp_err_t start_handler(httpd_req_t* req) {
    String message;
    bool ok = startCaptureSession(message);

    char response[160];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str());

    return send_text(req, "application/json", response);
}

static esp_err_t stop_handler(httpd_req_t* req) {
    size_t captured = 0;
    String message;
    bool ok = stopCaptureSession(captured, message);

    char response[192];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\",\"captured\":%u}",
             ok ? "true" : "false",
             message.c_str(),
             static_cast<unsigned>(captured));

    return send_text(req, "application/json", response);
}

static esp_err_t pause_handler(httpd_req_t* req) {
    String message;
    bool ok = pauseCaptureSession(message);

    char response[160];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str());

    return send_text(req, "application/json", response);
}

static esp_err_t flash_handler(httpd_req_t* req) {
    size_t idx;
    const char* label;

    if (finishCompleted) {
        applyFlashLevel(0);
        idx = flashLevelIndex;
        label = currentFlashLabel();
    } else {
        idx = cycleFlashLevel();
        label = currentFlashLabel();
    }

    char response[160];
    snprintf(response, sizeof(response),
             "{\"level\":%u,\"label\":\"%s\"}",
             static_cast<unsigned>(idx),
             label);

    return send_text(req, "application/json", response);
}

static esp_err_t finish_handler(httpd_req_t* req) {
    String message;
    bool ok = finishCaptureSession(message);

    char response[192];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str());

    esp_err_t res = send_text(req, "application/json", response);

    if (ok && main_httpd != nullptr) {
        httpd_handle_t handle = main_httpd;
        main_httpd = nullptr;
        httpd_stop(handle);
    }

    return res;
}

static esp_err_t save_handler(httpd_req_t* req) {
    if (!classReady || activeClassName.length() == 0) {
        return send_text(req, "application/json", "{\"success\":false,\"message\":\"Class not configured\"}");
    }

    size_t movedCount = 0;
    size_t movedBytes = 0;
    String message;
    bool ok = saveCaptureSession(activeClassName, movedCount, movedBytes, message);

    char response[224];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\",\"moved\":%u,\"bytes\":%u}",
             ok ? "true" : "false",
             message.c_str(),
             static_cast<unsigned>(movedCount),
             static_cast<unsigned>(movedBytes));

    return send_text(req, "application/json", response);
}

static esp_err_t discard_handler(httpd_req_t* req) {
    String message;
    bool ok = discardCaptureSession(message);

    char response[160];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str());

    return send_text(req, "application/json", response);
}

static esp_err_t dataset_handler(httpd_req_t* req) {
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_text(req, "application/json", "{\"success\":false,\"message\":\"Dataset name missing\"}");
    }

    char nameBuf[64];
    if (httpd_query_key_value(query, "name", nameBuf, sizeof(nameBuf)) != ESP_OK) {
        return send_text(req, "application/json", "{\"success\":false,\"message\":\"Dataset parameter required\"}");
    }

    String message;
    bool ok = configureActiveDataset(String(nameBuf), message);

    char response[384];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\",\"dataset_ready\":%s,\"dataset_name\":\"%s\",\"dataset_root\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str(),
             datasetReady ? "true" : "false",
             activeDatasetName.c_str(),
             datasetRootPath.c_str());

    return send_text(req, "application/json", response);
}

static esp_err_t class_handler(httpd_req_t* req) {
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return send_text(req, "application/json", "{\"success\":false,\"message\":\"Class name missing\"}");
    }

    char nameBuf[64];
    if (httpd_query_key_value(query, "name", nameBuf, sizeof(nameBuf)) != ESP_OK) {
        return send_text(req, "application/json", "{\"success\":false,\"message\":\"Class parameter required\"}");
    }

    String message;
    bool ok = configureActiveClass(String(nameBuf), message);

    char response[256];
    snprintf(response, sizeof(response),
             "{\"success\":%s,\"message\":\"%s\",\"class_ready\":%s,\"class_name\":\"%s\"}",
             ok ? "true" : "false",
             message.c_str(),
             classReady ? "true" : "false",
             activeClassName.c_str());

    return send_text(req, "application/json", response);
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32770;
    config.max_uri_handlers = 12;
    config.stack_size = 6144;

    if (httpd_start(&main_httpd, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t preview_uri = {
            .uri = "/preview.jpg",
            .method = HTTP_GET,
            .handler = preview_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t start_uri = {
            .uri = "/start",
            .method = HTTP_POST,
            .handler = start_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t stop_uri = {
            .uri = "/stop",
            .method = HTTP_POST,
            .handler = stop_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t pause_uri = {
            .uri = "/pause",
            .method = HTTP_POST,
            .handler = pause_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t flash_uri = {
            .uri = "/flash",
            .method = HTTP_POST,
            .handler = flash_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t finish_uri = {
            .uri = "/finish",
            .method = HTTP_POST,
            .handler = finish_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t discard_uri = {
            .uri = "/discard",
            .method = HTTP_POST,
            .handler = discard_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t dataset_uri = {
            .uri = "/dataset",
            .method = HTTP_POST,
            .handler = dataset_handler,
            .user_ctx = nullptr
        };

        httpd_uri_t class_uri = {
            .uri = "/class",
            .method = HTTP_POST,
            .handler = class_handler,
            .user_ctx = nullptr
        };

        httpd_register_uri_handler(main_httpd, &index_uri);
        httpd_register_uri_handler(main_httpd, &status_uri);
        httpd_register_uri_handler(main_httpd, &preview_uri);
        httpd_register_uri_handler(main_httpd, &start_uri);
        httpd_register_uri_handler(main_httpd, &stop_uri);
        httpd_register_uri_handler(main_httpd, &pause_uri);
        httpd_register_uri_handler(main_httpd, &flash_uri);
        httpd_register_uri_handler(main_httpd, &finish_uri);
        httpd_register_uri_handler(main_httpd, &save_uri);
        httpd_register_uri_handler(main_httpd, &discard_uri);
        httpd_register_uri_handler(main_httpd, &dataset_uri);
    httpd_register_uri_handler(main_httpd, &class_uri);

        Serial.println("[HTTP] Server started (port 80)");
    } else {
        Serial.println("[HTTP] Failed to start server");
    }
}
