/*
 * Dataset Transfer Sender for ESP32
 *
 * Streams a dataset stored on the ESP32 filesystem to a host PC over Serial,
 * preserving the existing folder hierarchy. Designed to complement the
 * hog_transform/data_transfer tooling.
 *
 * Usage:
 *   1. Flash this sketch to the ESP32.
 *   2. On the PC, run fetch_dataset_from_esp32.py (provided alongside).
 *   3. The PC script requests a dataset root (e.g. "gesture") and mirrors the
 *      directory tree locally.
 */

#include "Rf_file_manager.h"

#include <cstring>

// Storage configuration ------------------------------------------------------
const RfStorageType STORAGE_MODE = RfStorageType::SD_MMC_1BIT;

// Serial transfer protocol ---------------------------------------------------
const uint8_t CMD_HEADER[] = "ESP32_XFER";
const uint8_t CMD_DATASET_REQUEST = 0x21;   // PC -> ESP32 request dataset
const uint8_t CMD_DATASET_FILE_INFO = 0x22; // ESP32 -> PC file metadata
const uint8_t CMD_DATASET_FILE_CHUNK = 0x23; // ESP32 -> PC data chunk
const uint8_t CMD_DATASET_FILE_END = 0x24;  // ESP32 -> PC file completed
const uint8_t CMD_DATASET_DONE = 0x25;      // ESP32 -> PC dataset completed

const char* RESP_READY = "READY";
const char* RESP_ERROR = "ERROR";
const char* RESP_DONE = "DONE";

const uint16_t CHUNK_SIZE = USER_CHUNK_SIZE; // Synced with board configuration
const uint32_t HEADER_WAIT_MS = 200;

struct TransferStats {
    uint32_t files = 0;
    uint64_t bytes = 0;
};

String defaultDatasetRoot = "/gesture";
const char* RESERVED_FOLDER_SESSION = "_sessions";
const char* RESERVED_FOLDER_SESSION_SINGLE = "_session";

// Forward declarations -------------------------------------------------------
bool wait_for_header();
bool read_command(uint8_t& command);
void handle_dataset_request();
void send_text_response(const char* text);
bool send_dataset(const String& rootPath);
bool send_directory_recursive(const String& currentPath,
                              const String& basePath,
                              TransferStats& stats);
bool send_single_file(const String& fullPath,
                      const String& basePath,
                      TransferStats& stats);
bool is_reserved_folder(const String& name);
bool is_reserved_path(const String& relativePath);
String derive_dataset_name(const String& rootPath);
String build_camera_config_filename(const String& datasetName);
void send_command(uint8_t command, const uint8_t* payload, size_t length);

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(1000);

    while (!Serial) {
        delay(10);
    }

    delay(500);
    Serial.println();
    Serial.println("============================================");
    Serial.println("📦 ESP32 Dataset Transfer Sender");
    Serial.println("============================================");

    Serial.print("💾 Mounting storage... ");
    if (!RF_FS_BEGIN(STORAGE_MODE)) {
        Serial.println("FAILED");
        Serial.println("⚠️  Storage initialization failed. Cannot continue.");
        while (true) { delay(1000); }
    }
    Serial.println("OK");

    Serial.printf("📂 Default dataset root: %s\n",
                  defaultDatasetRoot.c_str());
    Serial.println("🔌 Waiting for PC request (CMD_DATASET_REQUEST)...\n");
}

void loop() {
    uint8_t command = 0;
    if (!read_command(command)) {
        delay(5);
        return;
    }

    switch (command) {
        case CMD_DATASET_REQUEST:
            handle_dataset_request();
            break;
        default:
            Serial.printf("⚠️  Unknown command: 0x%02X\n", command);
            break;
    }
}

// Protocol helpers -----------------------------------------------------------

bool wait_for_header() {
    const size_t headerLen = sizeof(CMD_HEADER) - 1; // exclude null terminator
    uint8_t buffer[sizeof(CMD_HEADER) - 1];
    uint32_t start = millis();

    while (Serial.available() < headerLen) {
        if (millis() - start > HEADER_WAIT_MS) {
            return false;
        }
        delay(2);
        yield();
    }

    size_t read = Serial.readBytes(buffer, headerLen);
    if (read != headerLen) {
        return false;
    }
    return memcmp(buffer, CMD_HEADER, headerLen) == 0;
}

bool read_command(uint8_t& command) {
    if (Serial.available() < (int)sizeof(CMD_HEADER)) {
        return false;
    }

    if (!wait_for_header()) {
        return false;
    }

    while (!Serial.available()) {
        delay(1);
    }
    command = Serial.read();
    return true;
}

void send_text_response(const char* text) {
    Serial.println(text);
    Serial.flush();
}

void send_command(uint8_t command, const uint8_t* payload, size_t length) {
    Serial.write(CMD_HEADER, sizeof(CMD_HEADER) - 1);
    Serial.write(command);
    if (payload != nullptr && length > 0) {
        Serial.write(payload, length);
    }
    Serial.flush();
}

// Dataset handling -----------------------------------------------------------

void handle_dataset_request() {
    while (Serial.available() < 1) {
        delay(1);
    }
    uint8_t pathLen = Serial.read();

    String requested = defaultDatasetRoot;
    if (pathLen > 0) {
        char pathBuf[256];
        if (pathLen >= sizeof(pathBuf)) {
            send_text_response(RESP_ERROR);
            Serial.println("Path too long");
            return;
        }

        while (Serial.available() < pathLen) {
            delay(1);
        }
        Serial.readBytes(pathBuf, pathLen);
        pathBuf[pathLen] = '\0';
        requested = String(pathBuf);
    }

    if (requested.length() == 0) {
        requested = defaultDatasetRoot;
    }

    if (!requested.startsWith("/")) {
        requested = "/" + requested;
    }

    Serial.printf("\n📥 Dataset request received: %s\n", requested.c_str());

    if (!RF_FS_EXISTS(requested.c_str())) {
        Serial.println("❌ Dataset root not found");
        send_text_response(RESP_ERROR);
        return;
    }

    File checker = RF_FS_OPEN(requested.c_str(), RF_FILE_READ);
    if (!checker || !checker.isDirectory()) {
        Serial.println("❌ Dataset root is not a directory");
        if (checker) {
            checker.close();
        }
        send_text_response(RESP_ERROR);
        return;
    }
    checker.close();

    String datasetName = derive_dataset_name(requested);
    String configFileName = build_camera_config_filename(datasetName);
    String configPath = requested;
    if (!configPath.endsWith("/")) {
        configPath += "/";
    }
    configPath += configFileName;
    if (RF_FS_EXISTS(configPath.c_str())) {
        Serial.printf("📄 Camera config found (%s)\n", configFileName.c_str());
    } else {
        Serial.printf("⚠️  Camera config missing (%s)\n", configFileName.c_str());
    }

    send_text_response(RESP_READY);

    if (!send_dataset(requested)) {
        Serial.println("⚠️  Dataset transfer finished with warnings");
    } else {
        Serial.println("✅ Dataset transfer completed");
    }
    send_text_response(RESP_DONE);
}

bool send_dataset(const String& rootPath) {
    TransferStats stats;
    bool ok = send_directory_recursive(rootPath, rootPath, stats);

    uint8_t payload[12];
    uint32_t totalFiles = stats.files;
    uint64_t totalBytes = stats.bytes;

    payload[0] = static_cast<uint8_t>(totalFiles & 0xFF);
    payload[1] = static_cast<uint8_t>((totalFiles >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((totalFiles >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((totalFiles >> 24) & 0xFF);

    for (int i = 0; i < 8; ++i) {
        payload[4 + i] = static_cast<uint8_t>((totalBytes >> (8 * i)) & 0xFF);
    }

    send_command(CMD_DATASET_DONE, payload, sizeof(payload));

    Serial.printf("📊 Sent %lu files (%.2f KB)\n",
                  static_cast<unsigned long>(stats.files),
                  totalBytes / 1024.0);
    return ok;
}

bool send_directory_recursive(const String& currentPath,
                              const String& basePath,
                              TransferStats& stats) {
    File dir = RF_FS_OPEN(currentPath.c_str(), RF_FILE_READ);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        Serial.printf("⚠️  Unable to open directory: %s\n", currentPath.c_str());
        return false;
    }

    bool ok = true;
    File entry = dir.openNextFile();
    while (entry) {
        String entryName = String(entry.name());
        bool isDir = entry.isDirectory();
        size_t entrySize = entry.size();
        entry.close();

        String fullPath;
        if (entryName.startsWith("/")) {
            fullPath = entryName;
        } else if (currentPath.endsWith("/")) {
            fullPath = currentPath + entryName;
        } else {
            fullPath = currentPath + "/" + entryName;
        }

        if (isDir) {
            String leafName = fullPath;
            int slashIndex = leafName.lastIndexOf('/');
            if (slashIndex >= 0 && slashIndex + 1 < leafName.length()) {
                leafName = leafName.substring(slashIndex + 1);
            }

            if (is_reserved_folder(leafName)) {
                Serial.printf("⏭️  Skipping reserved folder: %s\n", fullPath.c_str());
            } else {
                ok = send_directory_recursive(fullPath, basePath, stats) && ok;
            }
        } else {
            ok = send_single_file(fullPath, basePath, stats) && ok;
        }

        entry = dir.openNextFile();
        yield();
    }

    dir.close();
    return ok;
}

bool send_single_file(const String& fullPath,
                      const String& basePath,
                      TransferStats& stats) {
    String relativePath = fullPath;
    if (relativePath.startsWith("/")) {
        relativePath.remove(0, 1);
    }

    if (is_reserved_path(relativePath)) {
        Serial.printf("⏭️  Skipping reserved path: %s\n", relativePath.c_str());
        return true;
    }

    File file = RF_FS_OPEN(fullPath.c_str(), RF_FILE_READ);
    if (!file) {
        Serial.printf("⚠️  Unable to open file: %s\n", fullPath.c_str());
        return false;
    }

    uint32_t fileSize = file.size();
    uint16_t pathLen = static_cast<uint16_t>(relativePath.length());

    const size_t payloadLen = 2 + pathLen + 4;
    uint8_t* payload = reinterpret_cast<uint8_t*>(malloc(payloadLen));
    if (!payload) {
        Serial.println("❌ Memory allocation failed for metadata payload");
        file.close();
        return false;
    }

    payload[0] = static_cast<uint8_t>(pathLen & 0xFF);
    payload[1] = static_cast<uint8_t>((pathLen >> 8) & 0xFF);
    memcpy(payload + 2, relativePath.c_str(), pathLen);
    payload[2 + pathLen + 0] = static_cast<uint8_t>(fileSize & 0xFF);
    payload[2 + pathLen + 1] = static_cast<uint8_t>((fileSize >> 8) & 0xFF);
    payload[2 + pathLen + 2] = static_cast<uint8_t>((fileSize >> 16) & 0xFF);
    payload[2 + pathLen + 3] = static_cast<uint8_t>((fileSize >> 24) & 0xFF);

    send_command(CMD_DATASET_FILE_INFO, payload, payloadLen);
    free(payload);

    static uint8_t chunkBuffer[CHUNK_SIZE + 2];

    uint32_t bytesSent = 0;
    while (file.available()) {
        size_t toRead = file.read(chunkBuffer + 2, CHUNK_SIZE);
        uint16_t chunkLen = static_cast<uint16_t>(toRead);

        chunkBuffer[0] = static_cast<uint8_t>(chunkLen & 0xFF);
        chunkBuffer[1] = static_cast<uint8_t>((chunkLen >> 8) & 0xFF);

        send_command(CMD_DATASET_FILE_CHUNK, chunkBuffer, chunkLen + 2);

        bytesSent += chunkLen;
        delay(2);
        yield();
    }

    file.close();

    uint8_t endPayload[4];
    endPayload[0] = static_cast<uint8_t>(bytesSent & 0xFF);
    endPayload[1] = static_cast<uint8_t>((bytesSent >> 8) & 0xFF);
    endPayload[2] = static_cast<uint8_t>((bytesSent >> 16) & 0xFF);
    endPayload[3] = static_cast<uint8_t>((bytesSent >> 24) & 0xFF);
    send_command(CMD_DATASET_FILE_END, endPayload, sizeof(endPayload));

    stats.files += 1;
    stats.bytes += bytesSent;
    return true;
}

bool is_reserved_folder(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower == RESERVED_FOLDER_SESSION || lower == RESERVED_FOLDER_SESSION_SINGLE;
}

bool is_reserved_path(const String& relativePath) {
    String lower = relativePath;
    lower.toLowerCase();
    if (lower == RESERVED_FOLDER_SESSION || lower == RESERVED_FOLDER_SESSION_SINGLE) {
        return true;
    }
    const String sessionsPrefix = String(RESERVED_FOLDER_SESSION) + "/";
    const String sessionPrefix = String(RESERVED_FOLDER_SESSION_SINGLE) + "/";
    if (lower.startsWith(sessionsPrefix) || lower.startsWith(sessionPrefix)) {
        return true;
    }
    if (lower.indexOf("/_sessions/") != -1 || lower.indexOf("/_session/") != -1) {
        return true;
    }
    return false;
}

String derive_dataset_name(const String& rootPath) {
    String name = rootPath;
    if (name.startsWith("/")) {
        name.remove(0, 1);
    }
    while (name.endsWith("/")) {
        name.remove(name.length() - 1);
    }
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0 && lastSlash + 1 < name.length()) {
        name = name.substring(lastSlash + 1);
    }
    if (name.length() == 0) {
        name = "dataset";
    }
    return name;
}

String build_camera_config_filename(const String& datasetName) {
    String filename = datasetName;
    filename += "_camera_config.json";
    return filename;
}
