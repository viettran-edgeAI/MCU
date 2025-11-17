#define DEV_STAGE
#define RF_DEBUG_LEVEL 2
#define RF_USE_PSRAM

#ifndef CAMERA_MODEL_AI_THINKER
#define CAMERA_MODEL_AI_THINKER
#endif

#include <esp_camera.h>
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "Rf_file_manager.h"
#include "camera_pins.h"

#include <vector>
#include <algorithm>
#include <cctype>

using std::vector;

const char* WIFI_SSID = "YOUR SSID";
const char* WIFI_PASSWORD = "YOUR PASSWORD";

const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t main_httpd = nullptr;
httpd_handle_t stream_httpd = nullptr;

TaskHandle_t captureTaskHandle = nullptr;
SemaphoreHandle_t cameraMutex = nullptr;

struct ResolutionOption {
	const char* key;
	framesize_t frameSize;
	uint16_t width;
	uint16_t height;
};

static const ResolutionOption RESOLUTION_TABLE[] = {
	{"QQVGA", FRAMESIZE_QQVGA, 160, 120},
	{"HQVGA", FRAMESIZE_HQVGA, 240, 176},
	{"QVGA", FRAMESIZE_QVGA, 320, 240},
	{"HVGA", FRAMESIZE_HVGA, 480, 320},
	{"VGA", FRAMESIZE_VGA, 640, 480},
	{"SVGA", FRAMESIZE_SVGA, 800, 600},
	{"XGA", FRAMESIZE_XGA, 1024, 768},
	{"SXGA", FRAMESIZE_SXGA, 1280, 1024},
	{"UXGA", FRAMESIZE_UXGA, 1600, 1200}
};

struct FormatOption {
	const char* key;
	pixformat_t pixelFormat;
};

static const FormatOption FORMAT_TABLE[] = {
	{"GRAYSCALE", PIXFORMAT_GRAYSCALE},
	{"RGB888", PIXFORMAT_RGB888},
	{"JPEG", PIXFORMAT_JPEG}
};

struct ClassStats {
	String name;
	size_t count;
	bool saved;
	bool skipped;
};

String datasetName;
String resolutionKey = "QVGA";
framesize_t currentFrameSize = FRAMESIZE_QVGA;
uint16_t currentWidth = 320;
uint16_t currentHeight = 240;
String pixelFormatKey = "GRAYSCALE";
pixformat_t currentPixelFormat = PIXFORMAT_GRAYSCALE;
uint32_t captureIntervalMs = 500;
bool sessionConfigured = false;
bool cameraReady = false;
bool capturingActive = false;
bool classPendingDecision = false;
bool sessionFinished = false;
unsigned long lastCaptureMillis = 0;
String currentClassName;
String statusMessage = "Awaiting configuration";
String lastSavedPath;
vector<ClassStats> classStats;

void computeSessionSummary(size_t& classCount, size_t& imageCount) {
void startStreamServer();
void stopStreamServer();

	classCount = classStats.size();
	imageCount = 0;
	for (const auto& cls : classStats) {
		imageCount += cls.count;
	}
}

bool cleanupDatasetRoot() {
	String root = datasetRootPath();
	if (root.equals("/")) {
		Serial.println("[cleanup] Root path is '/', skipping delete");
		return true;
	}
	if (!RF_FS_EXISTS(root.c_str())) {
		Serial.println("[cleanup] Path does not exist: " + root);
		return true;
	}
	Serial.println("[cleanup] Deleting dataset folder: " + root);
	bool ok = deleteDirectoryRecursive(root);
	Serial.println(String("[cleanup] Delete result: ") + (ok ? "success" : "failed"));
	return ok;
}

const ResolutionOption* findResolution(const String& label) {
	for (const auto& opt : RESOLUTION_TABLE) {
		if (label.equalsIgnoreCase(opt.key)) {
			return &opt;
		}
	}
	return nullptr;
}

const FormatOption* findFormat(const String& label) {
	for (const auto& opt : FORMAT_TABLE) {
		if (label.equalsIgnoreCase(opt.key)) {
			return &opt;
		}
	}
	return nullptr;
}

ClassStats* findClassStats(const String& name) {
	for (auto& entry : classStats) {
		if (entry.name == name) {
			return &entry;
		}
	}
	return nullptr;
}

bool ensureDirectory(const String& path) {
	if (path.length() == 0) {
		return false;
	}

	String normalized = path;
	if (!normalized.startsWith("/")) {
		normalized = "/" + normalized;
	}

	if (RF_FS_EXISTS(normalized.c_str())) {
		return true;
	}

	String current = "";
	int start = 0;
	while (start < normalized.length()) {
		int slash = normalized.indexOf('/', start);
		if (slash < 0) {
			slash = normalized.length();
		}
		String segment = normalized.substring(start, slash);
		if (segment.length() > 0) {
			current += "/" + segment;
			if (!RF_FS_EXISTS(current.c_str())) {
				if (!rf_mkdir(current.c_str())) {
					return false;
				}
			}
		}
		start = slash + 1;
	}
	return true;
}

bool deleteFolderRecursive(const String& path) {
	if (!RF_FS_EXISTS(path.c_str())) {
		return true;
	}

	File dir = RF_FS_OPEN(path.c_str(), "r");
	if (!dir || !dir.isDirectory()) {
		return rf_remove(path.c_str());
	}

	File entry = dir.openNextFile();
	while (entry) {
		String entryPath = String(path) + "/" + entry.name();
		if (entry.isDirectory()) {
			if (!deleteFolderRecursive(entryPath)) {
				entry.close();
				dir.close();
				return false;
			}
		} else {
			if (!rf_remove(entryPath.c_str())) {
				entry.close();
				dir.close();
				return false;
			}
		}
		entry.close();
		entry = dir.openNextFile();
	}
	dir.close();
	return rf_rmdir(path.c_str());
}

String sanitizeName(const String& raw) {
	String result;
	result.reserve(raw.length());
	for (size_t i = 0; i < raw.length(); ++i) {
		char c = raw[i];
		if (isalnum(c) || c == '_' || c == '-' || c == '.') {
			result += c;
		} else if (c == ' ') {
			result += '_';
		}
	}
	// Remove leading dots or slashes
	while (result.startsWith(".")) {
		result.remove(0, 1);
	}
	while (result.startsWith("/")) {
		result.remove(0, 1);
	}
	return result;
}

String datasetRootPath(const String& name) {
	if (!name.length()) {
		return String("/");
	}
	return String("/") + name;
}

String datasetRootPath() {
	return datasetRootPath(datasetName);
}

String datasetConfigPath(const String& name) {
	return datasetRootPath(name) + "/" + name + "_camera_config.json";
}

String datasetConfigPath() {
	return datasetConfigPath(datasetName);
}

String extractJsonValue(const String& json, const char* key) {
	String token = String("\"") + key + "\"";
	int keyIndex = json.indexOf(token);
	if (keyIndex < 0) {
		return String();
	}
	int colonIndex = json.indexOf(':', keyIndex + token.length());
	if (colonIndex < 0) {
		return String();
	}
	int valueStart = colonIndex + 1;
	while (valueStart < json.length() && isspace(json[valueStart])) {
		valueStart++;
	}
	if (valueStart >= json.length()) {
		return String();
	}
	if (json[valueStart] == '"') {
		valueStart++;
		String out;
		while (valueStart < json.length()) {
			char c = json[valueStart];
			if (c == '\\' && valueStart + 1 < json.length()) {
				valueStart++;
				out += json[valueStart++];
				continue;
			}
			if (c == '"') {
				break;
			}
			out += c;
			valueStart++;
		}
		return out;
	}
	int valueEnd = valueStart;
	while (valueEnd < json.length() && (isdigit(json[valueEnd]) || json[valueEnd] == '-' || json[valueEnd] == '.')) {
		valueEnd++;
	}
	String out = json.substring(valueStart, valueEnd);
	out.trim();
	return out;
}

bool loadExistingConfig(const String& dataset, String& resolutionOut, uint32_t& intervalOut, String& formatOut) {
	String path = datasetConfigPath(dataset);
	File file = RF_FS_OPEN(path.c_str(), "r");
	if (!file) {
		return false;
	}
	String payload;
	while (file.available()) {
		payload += char(file.read());
	}
	file.close();
	bool updated = false;
	String res = extractJsonValue(payload, "resolution");
	if (res.length()) {
		resolutionOut = res;
		updated = true;
	}
	String intervalStr = extractJsonValue(payload, "capture_interval_ms");
	if (intervalStr.length()) {
		intervalOut = std::max<uint32_t>(100, intervalStr.toInt());
		updated = true;
	}
	String fmt = extractJsonValue(payload, "pixel_format");
	if (fmt.length()) {
		formatOut = fmt;
		updated = true;
	}
	return updated;
}

bool writeCameraConfigFile() {
	String path = datasetConfigPath();
	String payload = "{\n";
	payload += "  \"dataset\": \"" + datasetName + "\",\n";
	payload += "  \"resolution\": \"" + resolutionKey + "\",\n";
	payload += "  \"width\": " + String(currentWidth) + ",\n";
	payload += "  \"height\": " + String(currentHeight) + ",\n";
	payload += "  \"pixel_format\": \"" + pixelFormatKey + "\",\n";
	payload += "  \"capture_interval_ms\": " + String(captureIntervalMs) + "\n";
	payload += "}\n";

	File file = RF_FS_OPEN(path.c_str(), "w");
	if (!file) {
		statusMessage = "Failed to open config file";
		return false;
	}
	size_t written = file.print(payload);
	file.close();
	if (written != payload.length()) {
		statusMessage = "Config write incomplete";
		return false;
	}
	return true;
}

bool initCamera(const ResolutionOption& opt, pixformat_t pixelFormat) {
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
	config.pixel_format = pixelFormat;
	config.frame_size = opt.frameSize;
	config.jpeg_quality = 12;
	config.fb_count = 2;
	config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
#ifdef RF_USE_PSRAM
	config.fb_location = CAMERA_FB_IN_PSRAM;
#endif

	esp_err_t err = esp_camera_init(&config);
	if (err != ESP_OK) {
		Serial.printf("Camera init failed: 0x%X\n", err);
		statusMessage = "Camera init failed";
		return false;
	}

	sensor_t* sensor = esp_camera_sensor_get();
	if (sensor) {
		sensor->set_pixformat(sensor, pixelFormat);
		sensor->set_framesize(sensor, opt.frameSize);
		sensor->set_quality(sensor, 12);
		sensor->set_brightness(sensor, 0);
		sensor->set_contrast(sensor, 0);
		sensor->set_gain_ctrl(sensor, 1);
		sensor->set_exposure_ctrl(sensor, 1);
		sensor->set_whitebal(sensor, 1);
	}

	cameraReady = true;
	statusMessage = "Camera ready: " + String(opt.key) + " @ " + pixelFormatKey;
	Serial.println(statusMessage);
	return true;
}

bool captureAndSaveFrame(String& savedPath) {
	if (!cameraReady || !sessionConfigured || sessionFinished) {
		return false;
	}

	if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(300)) != pdTRUE) {
		statusMessage = "Camera mutex timeout";
		return false;
	}

	camera_fb_t* fb = esp_camera_fb_get();
	if (!fb) {
		xSemaphoreGive(cameraMutex);
		statusMessage = "Capture failed";
		return false;
	}

	uint8_t* jpg_buf = fb->buf;
	size_t jpg_len = fb->len;
	bool converted = false;

	if (fb->format != PIXFORMAT_JPEG) {
		if (!frame2jpg(fb, 80, &jpg_buf, &jpg_len)) {
			esp_camera_fb_return(fb);
			xSemaphoreGive(cameraMutex);
			statusMessage = "JPEG conversion failed";
			return false;
		}
		converted = true;
	}

	String classDir = datasetRootPath() + "/" + currentClassName;
	if (!ensureDirectory(classDir)) {
		if (converted) {
			free(jpg_buf);
		}
		esp_camera_fb_return(fb);
		xSemaphoreGive(cameraMutex);
		statusMessage = "Class dir error";
		return false;
	}

	String filename = classDir + "/img_" + String(millis()) + ".jpg";
	File file = RF_FS_OPEN(filename.c_str(), "w");
	if (!file) {
		if (converted) {
			free(jpg_buf);
		}
		esp_camera_fb_return(fb);
		xSemaphoreGive(cameraMutex);
		statusMessage = "File open failed";
		return false;
	}

	size_t written = file.write(jpg_buf, jpg_len);
	file.close();

	if (converted) {
		free(jpg_buf);
	}

	esp_camera_fb_return(fb);
	xSemaphoreGive(cameraMutex);

	if (written != jpg_len) {
		statusMessage = "Incomplete write";
		return false;
	}

	savedPath = filename;
	lastSavedPath = filename;
	return true;
}

void captureTask(void* parameter) {
	while (true) {
		if (!sessionConfigured || !cameraReady || sessionFinished || !capturingActive) {
			vTaskDelay(pdMS_TO_TICKS(50));
			continue;
		}

		unsigned long now = millis();
		if (now - lastCaptureMillis >= captureIntervalMs) {
			lastCaptureMillis = now;
			String saved;
			if (captureAndSaveFrame(saved)) {
				ClassStats* stats = findClassStats(currentClassName);
				if (stats) {
					stats->count++;
				}
				statusMessage = "Captured " + saved.substring(saved.lastIndexOf('/') + 1);
			}
		} else {
			vTaskDelay(pdMS_TO_TICKS(5));
		}
	}
}

bool initWiFi() {
	Serial.print("Connecting to WiFi");
	WiFi.setSleep(false);
	WiFi.mode(WIFI_STA);
	WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
	WiFi.setHostname("ESP32-Dataset");
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

	int attempts = 0;
	while (WiFi.status() != WL_CONNECTED && attempts < 40) {
		delay(500);
		Serial.print(".");
		attempts++;
	}

	if (WiFi.status() == WL_CONNECTED) {
		Serial.println("\nWiFi connected");
		Serial.print("IP: ");
		Serial.println(WiFi.localIP());
		return true;
	}

	Serial.println("\nWiFi failed");
	return false;
}

String escapeJson(const String& input) {
	String out;
	out.reserve(input.length() + 8);
	for (size_t i = 0; i < input.length(); ++i) {
		char c = input[i];
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '\"': out += "\\\""; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c; break;
		}
	}
	return out;
}

String classesJson() {
	String json = "[";
	for (size_t i = 0; i < classStats.size(); ++i) {
		const auto& cs = classStats[i];
		json += "{\"name\":\"" + escapeJson(cs.name) + "\",";
		json += "\"count\":" + String(cs.count) + ",";
		json += "\"saved\":" + String(cs.saved ? "true" : "false") + ",";
		json += "\"skipped\":" + String(cs.skipped ? "true" : "false") + "}";
		if (i + 1 < classStats.size()) {
			json += ",";
		}
	}
	json += "]";
	return json;
}

String buildStatusJson() {
	String json = "{";
	json += "\"configured\":" + String(sessionConfigured ? "true" : "false") + ",";
	json += "\"camera_ready\":" + String(cameraReady ? "true" : "false") + ",";
	json += "\"session_finished\":" + String(sessionFinished ? "true" : "false") + ",";
	json += "\"dataset\":\"" + escapeJson(datasetName) + "\",";
	json += "\"resolution\":\"" + escapeJson(resolutionKey) + "\",";
	json += "\"pixel_format\":\"" + escapeJson(pixelFormatKey) + "\",";
	json += "\"width\":" + String(currentWidth) + ",";
	json += "\"height\":" + String(currentHeight) + ",";
	json += "\"capture_interval_ms\":" + String(captureIntervalMs) + ",";
	json += "\"capturing\":" + String(capturingActive ? "true" : "false") + ",";
	json += "\"pending_decision\":" + String(classPendingDecision ? "true" : "false") + ",";
	json += "\"current_class\":\"" + escapeJson(currentClassName) + "\",";
	ClassStats* stats = findClassStats(currentClassName);
	json += "\"current_count\":" + String(stats ? stats->count : 0) + ",";
	json += "\"status\":\"" + escapeJson(statusMessage) + "\",";
	json += "\"last_file\":\"" + escapeJson(lastSavedPath) + "\",";
	json += "\"stream_active\":" + String(stream_httpd ? "true" : "false") + ",";
	json += "\"classes\":" + classesJson();
	json += "}";
	return json;
}

String urlDecode(const String& src) {
	String decoded;
	decoded.reserve(src.length());
	for (size_t i = 0; i < src.length(); ++i) {
		char c = src[i];
		if (c == '+') {
			decoded += ' ';
		} else if (c == '%') {
			if (i + 2 < src.length()) {
				char h1 = src[i + 1];
				char h2 = src[i + 2];
				char hex[3] = {h1, h2, 0};
				decoded += static_cast<char>(strtol(hex, nullptr, 16));
				i += 2;
			}
		} else {
			decoded += c;
		}
	}
	return decoded;
}

bool getQueryParam(httpd_req_t* req, const char* key, String& value) {
	size_t len = httpd_req_get_url_query_len(req) + 1;
	if (len <= 1) {
		return false;
	}
	std::vector<char> buf(len, 0);
	if (httpd_req_get_url_query_str(req, buf.data(), len) != ESP_OK) {
		return false;
	}
	char param[64];
	if (httpd_query_key_value(buf.data(), key, param, sizeof(param)) == ESP_OK) {
		value = urlDecode(String(param));
		return true;
	}
	return false;
}

esp_err_t sendJson(httpd_req_t* req, const String& body, int code = 200) {
	httpd_resp_set_type(req, "application/json");
	httpd_resp_set_status(req, code == 200 ? "200 OK" : (code == 400 ? "400 Bad Request" : "500 Internal Server Error"));
	return httpd_resp_send(req, body.c_str(), body.length());
}

esp_err_t sendError(httpd_req_t* req, const char* message, int code = 400) {
	String body = String("{\"ok\":false,\"message\":\"") + escapeJson(message) + "\"}";
	return sendJson(req, body, code);
}

esp_err_t sendOk(httpd_req_t* req, const char* message = "ok") {
	String body = String("{\"ok\":true,\"message\":\"") + escapeJson(message) + "\"}";
	return sendJson(req, body);
}

bool configureSession(const String& dataset, const ResolutionOption& opt, uint32_t interval, const FormatOption& formatOpt, bool reusedConfig) {
	datasetName = dataset;
	resolutionKey = opt.key;
	currentFrameSize = opt.frameSize;
	currentWidth = opt.width;
	currentHeight = opt.height;
	pixelFormatKey = formatOpt.key;
	currentPixelFormat = formatOpt.pixelFormat;
	captureIntervalMs = interval;
	classStats.clear();
	currentClassName = "";
	classPendingDecision = false;
	capturingActive = false;
	sessionFinished = false;
	lastSavedPath = "";

	bool existedBefore = RF_FS_EXISTS(datasetRootPath().c_str());
	if (!ensureDirectory(datasetRootPath())) {
		statusMessage = "Dataset dir failed";
		return false;
	}

	if (!writeCameraConfigFile()) {
		return false;
	}

	if (!initCamera(opt, currentPixelFormat)) {
		return false;
	}

	sessionConfigured = true;
	String initSummary;
	if (!existedBefore) {
		initSummary = "Dataset folder created";
	} else if (reusedConfig) {
		initSummary = "Existing config loaded";
	} else {
		initSummary = "Dataset folder ready";
	}
	statusMessage = initSummary + ", camera initialized (" + resolutionKey + "/" + pixelFormatKey + ")";
	Serial.println(statusMessage);
	startStreamServer();
	return true;
}

bool startCaptureForClass(const String& className) {
	if (!sessionConfigured || !cameraReady || sessionFinished) {
		return false;
	}
	if (className.length() == 0) {
		statusMessage = "Class name required";
		return false;
	}
	if (classPendingDecision) {
		statusMessage = "Save or skip current class";
		return false;
	}

	currentClassName = className;
	ClassStats* stats = findClassStats(className);
	if (!stats) {
		ClassStats entry{className, 0, false, false};
		classStats.push_back(entry);
		stats = &classStats.back();
	}
	stats->saved = false;
	stats->skipped = false;

	if (!ensureDirectory(datasetRootPath() + "/" + className)) {
		statusMessage = "Class dir fail";
		return false;
	}

	capturingActive = true;
	classPendingDecision = false;
	lastCaptureMillis = 0;
	statusMessage = "Capturing class " + className;
	return true;
}

void stopCapture() {
	capturingActive = false;
	statusMessage = "Capture paused";
}

void endCaptureForCurrentClass() {
	capturingActive = false;
	if (currentClassName.length() == 0) {
		statusMessage = "No active class";
		return;
	}
	ClassStats* stats = findClassStats(currentClassName);
	if (!stats || stats->count == 0) {
		statusMessage = "No images captured";
		return;
	}
	classPendingDecision = true;
	statusMessage = "Decide to save or skip";
}

bool skipCurrentClass() {
	if (currentClassName.length() == 0) {
		return false;
	}
	String classPath = datasetRootPath() + "/" + currentClassName;
	if (!deleteFolderRecursive(classPath)) {
		statusMessage = "Failed to delete class";
		return false;
	}

	for (auto it = classStats.begin(); it != classStats.end(); ++it) {
		if (it->name == currentClassName) {
			classStats.erase(it);
			break;
		}
	}

	statusMessage = "Class skipped";
	currentClassName = "";
	classPendingDecision = false;
	return true;
}

bool saveCurrentClass() {
	if (currentClassName.length() == 0) {
		return false;
	}
	ClassStats* stats = findClassStats(currentClassName);
	if (!stats) {
		return false;
	}
	stats->saved = true;
	statusMessage = "Class " + currentClassName + " saved successfully";
	Serial.println(statusMessage);
	currentClassName = "";
	classPendingDecision = false;
	return true;
}

String finishSession() {
	if (!sessionConfigured && !sessionFinished) {
		statusMessage = "No active session to finish";
		Serial.println(statusMessage);
		return statusMessage;
	}
	Serial.println("[finish] Starting shutdown sequence");
	size_t classCount = 0;
	size_t imageCount = 0;
	Serial.println("[finish] Computing session summary");
	computeSessionSummary(classCount, imageCount);
	Serial.printf("[finish] Summary result: %u classes, %u images\n", static_cast<unsigned>(classCount), static_cast<unsigned>(imageCount));
	capturingActive = false;
	classPendingDecision = false;
	sessionFinished = true;
	Serial.println("[finish] Stopping stream server");
	stopStreamServer();
	Serial.println("[finish] Stream stop requested");
	Serial.println("[finish] Cleaning dataset root");
	bool cleaned = cleanupDatasetRoot();
	Serial.println("[finish] Dataset cleanup complete");
	Serial.println("[finish] Unmounting filesystem");
	RF_FS_END();
	Serial.println("[finish] Filesystem unmounted");
	String summary = "Session finished; " + String(classCount) + " class(es), " + String(imageCount) + " image(s). ";
	summary += cleaned ? "Session folder cleaned." : "Folder cleanup failed.";
	summary += " Stream closed.";
	statusMessage = summary;
	Serial.println(statusMessage);
	datasetName = "";
	resolutionKey = "QVGA";
	currentFrameSize = FRAMESIZE_QVGA;
	currentWidth = 320;
	currentHeight = 240;
	pixelFormatKey = "GRAYSCALE";
	currentPixelFormat = PIXFORMAT_GRAYSCALE;
	captureIntervalMs = 500;
	classStats.clear();
	currentClassName = "";
	lastSavedPath = "";
	cameraReady = false;
	sessionConfigured = false;
	capturingActive = false;
	classPendingDecision = false;
	return summary;
}

static esp_err_t index_handler(httpd_req_t* req) {
	const char* html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
	<meta charset="UTF-8" />
	<meta name="viewport" content="width=device-width, initial-scale=1" />
	<title>ESP32 Dataset Capture</title>
	<style>
		body { font-family: Arial, sans-serif; background: #0f172a; color: #e2e8f0; margin: 0; padding: 0; }
		header { text-align: center; padding: 20px; }
		.container { max-width: 960px; margin: auto; padding: 0 16px 32px; }
		.card { background: #1e293b; border-radius: 12px; padding: 20px; margin-bottom: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.2); }
		.card h2 { margin-top: 0; color: #38bdf8; }
		button { border: none; border-radius: 8px; padding: 10px 18px; margin: 4px; font-size: 14px; cursor: pointer; transition: opacity 0.2s ease; }
		button:disabled { opacity: 0.4; cursor: not-allowed; }
		.primary { background: #2563eb; color: white; }
		.secondary { background: #475569; color: white; }
		.danger { background: #ef4444; color: white; }
		label { display: block; margin: 12px 0 6px; }
		input, select { width: 100%; padding: 10px; border-radius: 8px; border: 1px solid #475569; background: #0f172a; color: #e2e8f0; }
		#stream { width: 100%; border-radius: 12px; border: 2px solid #38bdf8; background: #000; }
		table { width: 100%; border-collapse: collapse; margin-top: 12px; }
		th, td { padding: 8px 10px; text-align: left; border-bottom: 1px solid #334155; }
		.status-line { margin: 6px 0; font-size: 14px; }
		.badge { display: inline-block; padding: 4px 8px; border-radius: 999px; font-size: 12px; margin-right: 6px; }
		.badge.green { background: rgba(16,185,129,0.2); color: #10b981; }
		.badge.yellow { background: rgba(251,191,36,0.2); color: #fbbf24; }
		.badge.red { background: rgba(248,113,113,0.2); color: #f87171; }
		.overlay { position: fixed; top: 0; left:0; right:0; bottom:0; background: rgba(15,23,42,0.94); display:flex; align-items:center; justify-content:center; z-index:10; }
		.overlay .card { max-width: 480px; width: 90%; }
		#actionToast { margin-top: 12px; min-height: 18px; color: #94a3b8; }
		#actionToast.error { color: #f87171; }
		#actionToast.success { color: #4ade80; }
	</style>
</head>
<body>
	<header>
		<h1>ESP32 Dataset Capture Studio</h1>
		<p>Configure once, capture forever 🚀</p>
	</header>
	<div class="container">
		<div class="card">
			<img id="stream" alt="Stream preview" />
		</div>
		<div class="card">
			<h2>Capture Controls</h2>
			<label for="className">Class name</label>
			<input id="className" placeholder="e.g. walking" />
			<div>
				<button class="primary" id="btnStart">Start</button>
				<button class="secondary" id="btnStop">Stop</button>
				<button class="secondary" id="btnEnd">End</button>
				<button class="primary" id="btnSave">Save Class</button>
				<button class="danger" id="btnSkip">Skip Class</button>
				<button class="danger" id="btnFinish">Finish Session</button>
			</div>
			<div id="status"></div>
			<div id="actionToast" class="status-line"></div>
		</div>
		<div class="card">
			<h2>Class Summary</h2>
			<table>
				<thead>
					<tr><th>Name</th><th>Images</th><th>Status</th></tr>
				</thead>
				<tbody id="classTable"></tbody>
			</table>
		</div>
	</div>

	<div class="overlay" id="configOverlay">
		<div class="card">
			<h2>Session Configuration</h2>
			<label for="datasetName">Dataset name</label>
			<input id="datasetName" placeholder="my_dataset" />
			<label for="resolution">Resolution</label>
			<select id="resolution">
				<option value="QQVGA">QQVGA (160x120)</option>
				<option value="HQVGA">HQVGA (240x176)</option>
				<option value="QVGA" selected>QVGA (320x240)</option>
				<option value="HVGA">HVGA (480x320)</option>
				<option value="VGA">VGA (640x480)</option>
				<option value="SVGA">SVGA (800x600)</option>
				<option value="XGA">XGA (1024x768)</option>
				<option value="SXGA">SXGA (1280x1024)</option>
				<option value="UXGA">UXGA (1600x1200)</option>
			</select>
			<label for="format">Image format</label>
			<select id="format">
				<option value="GRAYSCALE" selected>Grayscale (recommended)</option>
				<option value="RGB888">RGB888</option>
			</select>
			<label for="interval">Capture interval (ms)</label>
			<input id="interval" type="number" value="500" min="100" />
			<button class="primary" id="btnConfigure">Start Session</button>
			<div class="status-line" id="configStatus"></div>
		</div>
	</div>

	<script>
		const streamImg = document.getElementById('stream');
		const overlay = document.getElementById('configOverlay');
		const classTable = document.getElementById('classTable');
		const statusBox = document.getElementById('status');
		const actionToast = document.getElementById('actionToast');

		const setToast = (message = '', variant = '') => {
			actionToast.textContent = message || '';
			actionToast.classList.remove('error', 'success');
			if (variant) {
				actionToast.classList.add(variant);
			}
		};

		const btn = (id) => document.getElementById(id);
		const api = (path) => fetch(path, { cache: 'no-store' })
			.then(r => r.json())
			.catch(err => ({ ok: false, message: err.message }));

		const updateStream = () => {
			const host = window.location.hostname || window.location.host.split(':')[0];
			const isIPv6 = host.includes(':');
			const base = window.location.protocol + '//' + (isIPv6 ? `[${host}]` : host);
			streamImg.src = base + ':81/stream';
		};

		function setButtons(state) {
			btn('btnStart').disabled = !state.configured || state.capturing || state.pending_decision || state.session_finished;
			btn('btnStop').disabled = !state.capturing;
			btn('btnEnd').disabled = !state.capturing;
			btn('btnSave').disabled = !state.pending_decision;
			btn('btnSkip').disabled = !state.pending_decision;
			btn('btnFinish').disabled = !state.configured || state.session_finished;
			document.getElementById('className').disabled = state.capturing || state.pending_decision;
		}

		function renderStatus(state) {
			statusBox.innerHTML = `
				<div class="status-line"><span class="badge ${state.configured ? 'green' : 'yellow'}">${state.configured ? 'Configured' : 'Waiting'}</span>
				Dataset: <strong>${state.dataset || '-'}</strong> (${state.resolution} - ${state.width}x${state.height})</div>
				<div class="status-line">Format: ${state.pixel_format || 'GRAYSCALE'} • Interval: ${state.capture_interval_ms} ms</div>
				<div class="status-line">Current class: <strong>${state.current_class || '-'}</strong> (${state.current_count})</div>
				<div class="status-line">Livestream: ${state.stream_active ? '<span class="badge green">running</span>' : '<span class="badge red">closed</span>'}</div>
				<div class="status-line">Status: ${state.status}</div>
				<div class="status-line">Last file: ${state.last_file || '-'}</div>`;
		}

		function renderClasses(state) {
			classTable.innerHTML = '';
			(state.classes || []).forEach(cls => {
				const tr = document.createElement('tr');
				tr.innerHTML = `<td>${cls.name}</td><td>${cls.count}</td><td>
					${cls.saved ? '<span class="badge green">saved</span>' : cls.skipped ? '<span class="badge red">skipped</span>' : '<span class="badge yellow">pending</span>'}
				</td>`;
				classTable.appendChild(tr);
			});
		}

		function refresh() {
			fetch('/status', { cache: 'no-store' })
				.then(r => r.json())
				.then(data => {
					if (!data.configured) {
						overlay.style.display = 'flex';
					} else {
						overlay.style.display = 'none';
					}
					if (!data.stream_active || data.session_finished) {
						streamImg.removeAttribute('src');
					} else if (!streamImg.src) {
						updateStream();
					}
					setButtons(data);
					renderStatus(data);
					renderClasses(data);
				})
				.catch(err => {
					statusBox.textContent = 'Status error: ' + err.message;
				});
		}

		btn('btnConfigure').addEventListener('click', () => {
			const dataset = document.getElementById('datasetName').value.trim();
			const resolution = document.getElementById('resolution').value;
			const format = document.getElementById('format').value;
			const interval = document.getElementById('interval').value;
			btn('btnConfigure').disabled = true;
			fetch(`/configure?dataset=${encodeURIComponent(dataset)}&resolution=${resolution}&interval=${interval}&format=${format}`)
				.then(r => r.json())
				.then(resp => {
					document.getElementById('configStatus').textContent = resp.message || '';
					if (resp.ok) {
						overlay.style.display = 'none';
						updateStream();
						refresh();
					}
				})
				.catch(err => {
					document.getElementById('configStatus').textContent = err.message;
				})
				.finally(() => btn('btnConfigure').disabled = false);
		});

		btn('btnStart').addEventListener('click', () => {
			const cls = document.getElementById('className').value.trim();
			api(`/capture/start?class=${encodeURIComponent(cls)}`).then(refresh);
		});
		btn('btnStop').addEventListener('click', () => api('/capture/stop').then(refresh));
		btn('btnEnd').addEventListener('click', () => api('/capture/end').then(refresh));
		btn('btnSave').addEventListener('click', () => api('/class/save').then(refresh));
		btn('btnSkip').addEventListener('click', () => api('/class/skip').then(refresh));
		btn('btnFinish').addEventListener('click', () => {
			setToast('Ending session...', '');
			api('/session/finish').then(resp => {
				if (resp && resp.message) {
					setToast(resp.message, resp.ok ? 'success' : 'error');
				} else {
					setToast('Finish request failed', 'error');
				}
				refresh();
			});
		});

		updateStream();
		refresh();
		setInterval(refresh, 1000);
	</script>
</body>
</html>
)rawliteral";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, html, strlen(html));
}

static esp_err_t status_handler(httpd_req_t* req) {
	String json = buildStatusJson();
	return sendJson(req, json);
}

static esp_err_t configure_handler(httpd_req_t* req) {
	if (sessionConfigured) {
		return sendError(req, "Session already configured");
	}

	String dataset, resolution, intervalStr, formatLabel;
	if (!getQueryParam(req, "dataset", dataset) || dataset.length() == 0) {
		return sendError(req, "Dataset required");
	}
	if (!getQueryParam(req, "resolution", resolution)) {
		return sendError(req, "Resolution required");
	}
	if (!getQueryParam(req, "interval", intervalStr)) {
		intervalStr = "500";
	}
	if (!getQueryParam(req, "format", formatLabel) || formatLabel.length() == 0) {
		formatLabel = "GRAYSCALE";
	}

	dataset = sanitizeName(dataset);
	if (dataset.length() == 0) {
		return sendError(req, "Invalid dataset name");
	}
	if (dataset.length() > 15) {
		return sendError(req, "Dataset name must be 1-15 characters");
	}

	uint32_t interval = std::max<uint32_t>(100, intervalStr.toInt());
	bool reusedConfig = false;
	String datasetPath = datasetRootPath(dataset);
	if (RF_FS_EXISTS(datasetPath.c_str())) {
		String configPath = datasetConfigPath(dataset);
		if (RF_FS_EXISTS(configPath.c_str())) {
			if (loadExistingConfig(dataset, resolution, interval, formatLabel)) {
				reusedConfig = true;
			} else {
				if (!deleteDirectoryRecursive(datasetPath)) {
					return sendError(req, "Failed to reset dataset folder");
				}
			}
		} else {
			if (!deleteDirectoryRecursive(datasetPath)) {
				return sendError(req, "Failed to reset dataset folder");
			}
		}
	}

	const ResolutionOption* opt = findResolution(resolution);
	if (!opt) {
		return sendError(req, "Unsupported resolution");
	}
	const FormatOption* fmt = findFormat(formatLabel);
	if (!fmt) {
		fmt = &FORMAT_TABLE[0];
	}

	if (!configureSession(dataset, *opt, interval, *fmt, reusedConfig)) {
		return sendError(req, statusMessage.c_str());
	}

	return sendOk(req, "Configured");
}

static esp_err_t capture_start_handler(httpd_req_t* req) {
	String className;
	if (!getQueryParam(req, "class", className) || className.length() == 0) {
		return sendError(req, "Class required");
	}
	className = sanitizeName(className);
	if (!startCaptureForClass(className)) {
		return sendError(req, statusMessage.c_str());
	}
	return sendOk(req, "Capture started");
}

static esp_err_t capture_stop_handler(httpd_req_t* req) {
	stopCapture();
	return sendOk(req, "Capture stopped");
}

static esp_err_t capture_end_handler(httpd_req_t* req) {
	endCaptureForCurrentClass();
	return sendOk(req, "Ended current class");
}

static esp_err_t class_save_handler(httpd_req_t* req) {
	if (!classPendingDecision) {
		return sendError(req, "No class pending");
	}
	if (!saveCurrentClass()) {
		return sendError(req, statusMessage.c_str());
	}
	return sendOk(req, "Class saved");
}

static esp_err_t class_skip_handler(httpd_req_t* req) {
	if (!classPendingDecision) {
		return sendError(req, "No class pending");
	}
	if (!skipCurrentClass()) {
		return sendError(req, statusMessage.c_str());
	}
	return sendOk(req, "Class skipped");
}

static esp_err_t session_finish_handler(httpd_req_t* req) {
	Serial.println("[http] /session/finish invoked");
	if (!sessionConfigured && !sessionFinished) {
		String msg = "No active session to finish";
		Serial.println(msg);
		return sendError(req, msg.c_str());
	}
	
	// Compute summary first without blocking operations
	size_t classCount = 0;
	size_t imageCount = 0;
	computeSessionSummary(classCount, imageCount);
	String summary = "Session finished; " + String(classCount) + " class(es), " + String(imageCount) + " image(s). ";
	
	// Mark session as finished BEFORE responding
	capturingActive = false;
	classPendingDecision = false;
	sessionFinished = true;
	sessionConfigured = false;
	cameraReady = false;
	statusMessage = summary + "Cleanup in progress...";
	
	// Send response immediately so browser can disconnect from stream
	esp_err_t result = sendOk(req, summary.c_str());
	
	// Now do blocking cleanup after response is sent
	Serial.println("[finish] Response sent, starting cleanup");
	delay(500); // Give browser time to disconnect stream
	stopStreamServer();
	cleanupDatasetRoot();
	RF_FS_END();
	
	// Reset all state
	datasetName = "";
	resolutionKey = "QVGA";
	currentFrameSize = FRAMESIZE_QVGA;
	currentWidth = 320;
	currentHeight = 240;
	pixelFormatKey = "GRAYSCALE";
	currentPixelFormat = PIXFORMAT_GRAYSCALE;
	captureIntervalMs = 500;
	classStats.clear();
	currentClassName = "";
	lastSavedPath = "";
	
	statusMessage = summary + "Cleanup complete.";
	Serial.println(statusMessage);
	
	return result;
}

static esp_err_t stream_handler(httpd_req_t* req) {
	if (!cameraReady) {
		return sendError(req, "Camera not ready");
	}

	camera_fb_t* fb = nullptr;
	esp_err_t res = ESP_OK;
	size_t jpg_buf_len = 0;
	uint8_t* jpg_buf = nullptr;
	char part_buf[64];

	res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
	httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

	while (true) {
		if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
			continue;
		}

		fb = esp_camera_fb_get();
		if (!fb) {
			xSemaphoreGive(cameraMutex);
			vTaskDelay(pdMS_TO_TICKS(30));
			continue;
		}

		if (fb->format != PIXFORMAT_JPEG) {
			if (!frame2jpg(fb, 80, &jpg_buf, &jpg_buf_len)) {
				esp_camera_fb_return(fb);
				xSemaphoreGive(cameraMutex);
				vTaskDelay(pdMS_TO_TICKS(30));
				continue;
			}
		} else {
			jpg_buf = fb->buf;
			jpg_buf_len = fb->len;
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

		vTaskDelay(pdMS_TO_TICKS(30));
	}

	return res;
}

void startControlServer() {
	if (main_httpd) {
		return;
	}
	httpd_config_t main_config = HTTPD_DEFAULT_CONFIG();
	main_config.server_port = 80;
	main_config.ctrl_port = 32768;
	main_config.max_uri_handlers = 12;
	main_config.stack_size = 6144;

	httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr };
	httpd_uri_t status_uri = { .uri = "/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = nullptr };
	httpd_uri_t configure_uri = { .uri = "/configure", .method = HTTP_GET, .handler = configure_handler, .user_ctx = nullptr };
	httpd_uri_t start_uri = { .uri = "/capture/start", .method = HTTP_GET, .handler = capture_start_handler, .user_ctx = nullptr };
	httpd_uri_t stop_uri = { .uri = "/capture/stop", .method = HTTP_GET, .handler = capture_stop_handler, .user_ctx = nullptr };
	httpd_uri_t end_uri = { .uri = "/capture/end", .method = HTTP_GET, .handler = capture_end_handler, .user_ctx = nullptr };
	httpd_uri_t save_uri = { .uri = "/class/save", .method = HTTP_GET, .handler = class_save_handler, .user_ctx = nullptr };
	httpd_uri_t skip_uri = { .uri = "/class/skip", .method = HTTP_GET, .handler = class_skip_handler, .user_ctx = nullptr };
	httpd_uri_t finish_uri = { .uri = "/session/finish", .method = HTTP_GET, .handler = session_finish_handler, .user_ctx = nullptr };

	if (httpd_start(&main_httpd, &main_config) == ESP_OK) {
		httpd_register_uri_handler(main_httpd, &index_uri);
		httpd_register_uri_handler(main_httpd, &status_uri);
		httpd_register_uri_handler(main_httpd, &configure_uri);
		httpd_register_uri_handler(main_httpd, &start_uri);
		httpd_register_uri_handler(main_httpd, &stop_uri);
		httpd_register_uri_handler(main_httpd, &end_uri);
		httpd_register_uri_handler(main_httpd, &save_uri);
		httpd_register_uri_handler(main_httpd, &skip_uri);
		httpd_register_uri_handler(main_httpd, &finish_uri);
		Serial.println("HTTP control server started on port 80");
	} else {
		Serial.println("Failed to start HTTP server");
	}
}

void startStreamServer() {
	if (stream_httpd) {
		return;
	}
	httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
	stream_config.server_port = 81;
	stream_config.ctrl_port = 32769;
	stream_config.max_uri_handlers = 4;
	stream_config.stack_size = 8192;
	stream_config.lru_purge_enable = true;

	httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = nullptr };

	if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
		httpd_register_uri_handler(stream_httpd, &stream_uri);
		Serial.println("Stream server started on port 81");
	} else {
		Serial.println("Failed to start stream server");
	}
}

void stopStreamServer() {
	if (!stream_httpd) {
		Serial.println("[stream] stopStreamServer called but server already null");
		return;
	}
	Serial.println("[stream] Stopping stream server...");
	httpd_stop(stream_httpd);
	stream_httpd = nullptr;
	Serial.println("Stream server stopped");
}

void startServers() {
	startControlServer();
	startStreamServer();
}

void setup() {
	WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
	Serial.begin(115200);
	delay(500);
	Serial.println("Dataset Capture Server booting...");

	cameraMutex = xSemaphoreCreateMutex();
	if (!cameraMutex) {
		Serial.println("Failed to create mutex");
		while (true) { delay(1000); }
	}

	if (!initWiFi()) {
		while (true) { delay(1000); }
	}

	if (!RF_FS_BEGIN(STORAGE_MODE)) {
		Serial.println("Primary storage mount failed, trying LittleFS");
		if (!RF_FS_BEGIN(RfStorageType::FLASH)) {
			Serial.println("Storage mount failed");
		}
	}

	startServers();

	xTaskCreatePinnedToCore(
		captureTask,
		"CaptureTask",
		8192,
		nullptr,
		1,
		&captureTaskHandle,
		0
	);

	Serial.println("Ready for configuration");
}

void loop() {
	vTaskDelay(pdMS_TO_TICKS(1000));
}
