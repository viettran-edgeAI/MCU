#include "hog_transform.h"
#include "Rf_file_manager.h"
#include <cctype>

namespace {

bool findValueStart(const String& json, const char* key, int& valueIndex) {
    String pattern = "\"" + String(key) + "\"";
    int keyIndex = json.indexOf(pattern);
    if (keyIndex < 0) {
        return false;
    }

    int colonIndex = json.indexOf(':', keyIndex + pattern.length());
    if (colonIndex < 0) {
        return false;
    }

    int index = colonIndex + 1;
    while (index < json.length() && isspace(static_cast<unsigned char>(json[index]))) {
        ++index;
    }

    if (index >= json.length()) {
        return false;
    }

    valueIndex = index;
    return true;
}

bool extractStringValue(const String& json, const char* key, String& out) {
    int index = 0;
    if (!findValueStart(json, key, index)) {
        return false;
    }

    if (json[index] != '"') {
        return false;
    }
    ++index;
    int end = json.indexOf('"', index);
    if (end < 0) {
        return false;
    }
    out = json.substring(index, end);
    out.trim();
    return true;
}

bool extractIntValue(const String& json, const char* key, int& out) {
    int index = 0;
    if (!findValueStart(json, key, index)) {
        return false;
    }

    int start = index;
    if (json[index] == '-') {
        ++index;
    }
    while (index < json.length() && isdigit(static_cast<unsigned char>(json[index]))) {
        ++index;
    }

    if (index == start) {
        return false;
    }

    out = json.substring(start, index).toInt();
    return true;
}

bool extractBoolValue(const String& json, const char* key, bool& out) {
    int index = 0;
    if (!findValueStart(json, key, index)) {
        return false;
    }

    int end = index;
    while (end < json.length() && isalpha(static_cast<unsigned char>(json[end]))) {
        ++end;
    }

    if (end == index) {
        return false;
    }

    String token = json.substring(index, end);
    token.toLowerCase();
    token.trim();

    if (token.startsWith("true")) {
        out = true;
        return true;
    }
    if (token.startsWith("false")) {
        out = false;
        return true;
    }
    return false;
}

ImageProcessing::PixelFormat parsePixelFormat(String value) {
    value.trim();
    value.toUpperCase();

    if (value == "RGB565") {
        return ImageProcessing::PixelFormat::RGB565;
    }
    if (value == "RGB888") {
        return ImageProcessing::PixelFormat::RGB888;
    }
    if (value == "YUV422") {
        return ImageProcessing::PixelFormat::YUV422;
    }
    if (value == "JPEG") {
        return ImageProcessing::PixelFormat::JPEG;
    }

    return ImageProcessing::PixelFormat::GRAYSCALE;
}

ImageProcessing::ResizeMethod parseResizeMethod(String value) {
    value.trim();
    value.toUpperCase();

    if (value == "NEAREST" || value == "NEAREST_NEIGHBOR") {
        return ImageProcessing::ResizeMethod::NEAREST_NEIGHBOR;
    }
    if (value == "AREA" || value == "AREA_AVERAGE") {
        return ImageProcessing::ResizeMethod::AREA_AVERAGE;
    }

    return ImageProcessing::ResizeMethod::BILINEAR;
}

String extractFileName(String path) {
    path.trim();
    if (path.length() == 0) {
        return path;
    }

    path.replace('\\', '/');
    int slash = path.lastIndexOf('/');
    if (slash < 0) {
        return path;
    }
    if (slash == path.length() - 1) {
        return String();
    }
    return path.substring(slash + 1);
}

} // namespace

// Default constructor with optimal parameters for 32x32 images
HOG_MCU::HOG_MCU() : processed_image_buffer(nullptr), 
                     gradient_x_buffer(nullptr), gradient_y_buffer(nullptr),
                     magnitude_buffer(nullptr), angle_bin_buffer(nullptr),
                     block_histogram_buffer(nullptr), cell_histogram_buffer(nullptr),
                     cell_grid_buffer(nullptr), cells_x(0), cells_y(0),
                     feature_csv_path_(), feature_file_name_() {
    params.img_width = 32;
    params.img_height = 32;
    params.cell_size = 8;
    params.block_size = 16;
    params.block_stride = 6;
    params.nbins = 4;
    
    // Set default image processing configuration
    img_config.input_format = ImageProcessing::PixelFormat::GRAYSCALE;
    img_config.output_format = ImageProcessing::PixelFormat::GRAYSCALE;
    img_config.input_width = 320;
    img_config.input_height = 240;
    img_config.output_width = 32;
    img_config.output_height = 32;
    img_config.resize_method = ImageProcessing::ResizeMethod::BILINEAR;
    img_config.maintain_aspect_ratio = false;
    
    initializeBuffers();
}

// Constructor with custom parameters
HOG_MCU::HOG_MCU(const Params& p) : params(p), processed_image_buffer(nullptr),
                                    gradient_x_buffer(nullptr), gradient_y_buffer(nullptr),
                                    magnitude_buffer(nullptr), angle_bin_buffer(nullptr),
                                    block_histogram_buffer(nullptr), cell_histogram_buffer(nullptr),
                                    cell_grid_buffer(nullptr), cells_x(0), cells_y(0),
                                    feature_csv_path_(), feature_file_name_() {
    // Set default image processing configuration
    img_config.input_format = ImageProcessing::PixelFormat::GRAYSCALE;
    img_config.output_format = ImageProcessing::PixelFormat::GRAYSCALE;
    img_config.input_width = 320;
    img_config.input_height = 240;
    img_config.output_width = params.img_width;
    img_config.output_height = params.img_height;
    img_config.resize_method = ImageProcessing::ResizeMethod::BILINEAR;
    img_config.maintain_aspect_ratio = false;
    
    initializeBuffers();
}

HOG_MCU::~HOG_MCU() {
    cleanupBuffers();
}

void HOG_MCU::setImageProcessingConfig(const ImageProcessing::ProcessingConfig& config) {
    img_config = config;
    
    // Update HOG parameters to match output dimensions
    params.img_width = config.output_width;
    params.img_height = config.output_height;
    
    // Reinitialize buffers with new dimensions
    cleanupBuffers();
    initializeBuffers();
}

const ImageProcessing::ProcessingConfig& HOG_MCU::getImageProcessingConfig() const {
    return img_config;
}

void HOG_MCU::transform(const void* cameraBuffer) {
    features.clear();
    
    if (!cameraBuffer) {
        return;
    }
    
    // Fast path: if input already matches HOG dimensions and is grayscale, skip processing
    if (img_config.input_format == ImageProcessing::PixelFormat::GRAYSCALE &&
        img_config.input_width == params.img_width && 
        img_config.input_height == params.img_height) {
        computeOptimized((const uint8_t*)cameraBuffer);
        return;
    }
    
    // Normal path: process the camera buffer (format conversion + resizing)
    if (!processed_image_buffer) {
        return;
    }
    
    if (ImageProcessing::processImage(cameraBuffer, img_config, processed_image_buffer)) {
        computeOptimized(processed_image_buffer);
    }
}

void HOG_MCU::transformGrayscale(const uint8_t* grayscaleImage) {
    features.clear();
    
    if (!grayscaleImage) {
        return;
    }
    
    // If the input is already the correct size, use it directly
    if (img_config.input_width == params.img_width && 
        img_config.input_height == params.img_height) {
        computeOptimized(grayscaleImage);
    } else {
        // Need to resize the grayscale image
        if (processed_image_buffer) {
            if (ImageProcessing::resizeBilinear(grayscaleImage, 
                                              img_config.input_width, img_config.input_height,
                                              processed_image_buffer, 
                                              params.img_width, params.img_height)) {
                computeOptimized(processed_image_buffer);
            }
        }
    }
}

const mcu::b_vector<float, 144>& HOG_MCU::getFeatures() const {
    return features;
}

void HOG_MCU::set_config(int img_width, int img_height, int cell_size, int block_size, int block_stride, int nbins) {
    params.img_width = img_width;
    params.img_height = img_height;
    params.cell_size = cell_size;
    params.block_size = block_size;
    params.block_stride = block_stride;
    params.nbins = nbins;
    
    // Update image processing output dimensions
    img_config.output_width = img_width;
    img_config.output_height = img_height;
    
    // Reinitialize buffers
    cleanupBuffers();
    initializeBuffers();
}

void HOG_MCU::setConfig(const Config& config) {
    // Configure image processing
    img_config.input_format = config.input_format;
    img_config.output_format = ImageProcessing::PixelFormat::GRAYSCALE;
    img_config.input_width = config.input_width;
    img_config.input_height = config.input_height;
    img_config.output_width = config.hog_img_width;
    img_config.output_height = config.hog_img_height;
    img_config.resize_method = config.resize_method;
    img_config.maintain_aspect_ratio = config.maintain_aspect_ratio;
    img_config.jpeg_quality = config.jpeg_quality;
    
    // Configure HOG parameters
    params.img_width = config.hog_img_width;
    params.img_height = config.hog_img_height;
    params.cell_size = config.cell_size;
    params.block_size = config.block_size;
    params.block_stride = config.block_stride;
    params.nbins = config.nbins;
    
    // Reinitialize buffers with new configuration
    cleanupBuffers();
    initializeBuffers();
}

bool HOG_MCU::loadConfigFromFile(const char* path) {
    if (!path || path[0] == '\0') {
        Serial.println("HOG_MCU: Invalid configuration path");
        return false;
    }

    String requestedPath(path);
    File configFile = RF_FS_OPEN(requestedPath, RF_FILE_READ);
    if (!configFile && !requestedPath.startsWith("/")) {
        String altPath = "/" + requestedPath;
        configFile = RF_FS_OPEN(altPath, RF_FILE_READ);
        if (configFile) {
            requestedPath = altPath;
        }
    }

    if (!configFile) {
        Serial.print("HOG_MCU: Failed to open config file ");
        Serial.println(path);
        return false;
    }

    String content;
    size_t fileSize = configFile.size();
    if (fileSize > 0) {
        content.reserve(fileSize);
    }

    while (configFile.available()) {
        content += char(configFile.read());
    }
    configFile.close();

    if (content.length() == 0) {
        Serial.println("HOG_MCU: Config file is empty");
        return false;
    }

    Config newConfig;
    String parsedString;
    String parsedModelName;
    String parsedFeatureCsv;
    String parsedFeatureFile;
    int parsedInt = 0;
    bool parsedBool = false;
    int featureLength = 0;

    if (extractStringValue(content, "input_format", parsedString)) {
        newConfig.input_format = parsePixelFormat(parsedString);
    }
    if (extractIntValue(content, "input_width", parsedInt)) {
        newConfig.input_width = parsedInt;
    }
    if (extractIntValue(content, "input_height", parsedInt)) {
        newConfig.input_height = parsedInt;
    }
    if (extractStringValue(content, "resize_method", parsedString)) {
        newConfig.resize_method = parseResizeMethod(parsedString);
    }
    if (extractBoolValue(content, "maintain_aspect_ratio", parsedBool)) {
        newConfig.maintain_aspect_ratio = parsedBool;
    }
    if (extractIntValue(content, "jpeg_quality", parsedInt)) {
        if (parsedInt < 0) parsedInt = 0;
        if (parsedInt > 100) parsedInt = 100;
        newConfig.jpeg_quality = static_cast<uint8_t>(parsedInt);
    }

    if (extractIntValue(content, "hog_img_width", parsedInt)) {
        newConfig.hog_img_width = parsedInt;
    }
    if (extractIntValue(content, "hog_img_height", parsedInt)) {
        newConfig.hog_img_height = parsedInt;
    }
    if (extractIntValue(content, "cell_size", parsedInt)) {
        newConfig.cell_size = parsedInt;
    }
    if (extractIntValue(content, "block_size", parsedInt)) {
        newConfig.block_size = parsedInt;
    }
    if (extractIntValue(content, "block_stride", parsedInt)) {
        newConfig.block_stride = parsedInt;
    }
    if (extractIntValue(content, "nbins", parsedInt)) {
        newConfig.nbins = parsedInt;
    }

    extractStringValue(content, "model_name", parsedModelName);
    if (extractStringValue(content, "feature_csv", parsedFeatureCsv)) {
        parsedFeatureCsv.trim();
    }
    if (extractStringValue(content, "feature_file_name", parsedFeatureFile)) {
        parsedFeatureFile.trim();
    }
    if (extractIntValue(content, "feature_length", featureLength)) {
        if (featureLength > 144) {
            Serial.println("HOG_MCU: Warning - feature length exceeds 144; extra values will be ignored.");
        }
    }

    if (parsedFeatureCsv.length() == 0 && parsedModelName.length() > 0) {
        parsedFeatureCsv = parsedModelName + ".csv";
    }
    if (parsedFeatureFile.length() == 0 && parsedFeatureCsv.length() > 0) {
        parsedFeatureFile = extractFileName(parsedFeatureCsv);
    }

    if (newConfig.input_width <= 0 || newConfig.input_height <= 0 ||
        newConfig.hog_img_width <= 0 || newConfig.hog_img_height <= 0 ||
        newConfig.cell_size <= 0 || newConfig.block_size <= 0 ||
        newConfig.block_stride <= 0 || newConfig.nbins <= 0) {
        Serial.println("HOG_MCU: Invalid parameters in configuration file");
        return false;
    }

    if (newConfig.block_size > newConfig.hog_img_width || newConfig.block_size > newConfig.hog_img_height) {
        Serial.println("HOG_MCU: Block size must fit within HOG image dimensions");
        return false;
    }

    if (newConfig.cell_size > newConfig.block_size) {
        Serial.println("HOG_MCU: Cell size must not exceed block size");
        return false;
    }

    int blocksX = (newConfig.hog_img_width - newConfig.block_size) / newConfig.block_stride + 1;
    int blocksY = (newConfig.hog_img_height - newConfig.block_size) / newConfig.block_stride + 1;
    if (blocksX <= 0 || blocksY <= 0) {
        Serial.println("HOG_MCU: Invalid block stride or dimensions in configuration");
        return false;
    }

    setConfig(newConfig);

    feature_csv_path_ = parsedFeatureCsv;
    feature_file_name_ = parsedFeatureFile;

    Serial.print("HOG_MCU: Loaded configuration from ");
    Serial.println(requestedPath);

    return true;
}

void HOG_MCU::setupForESP32CAM(ImageProcessing::PixelFormat input_format, int input_width, int input_height) {
    Config config(input_format, input_width, input_height);
    // Use default HOG parameters (32x32, cell_size=8, block_size=16, block_stride=6, nbins=4)
    setConfig(config);
}

void HOG_MCU::initializeBuffers() {
    size_t buffer_size = params.img_width * params.img_height;
    if (buffer_size > 0) {
        processed_image_buffer = new uint8_t[buffer_size];
        if (!processed_image_buffer) {
            Serial.println("Error: Failed to allocate image processing buffer");
        }
        
        // Allocate gradient computation buffers
        gradient_x_buffer = new int16_t[buffer_size];
        gradient_y_buffer = new int16_t[buffer_size];
        magnitude_buffer = new uint16_t[buffer_size];
        angle_bin_buffer = new uint8_t[buffer_size];
        
        // Allocate histogram buffers (max size for typical HOG configs)
        block_histogram_buffer = new float[16];  // 4 cells * 4 bins
        cell_histogram_buffer = new float[9];    // Max 9 bins
        
        // Allocate cell grid buffer for caching all cell histograms
        cells_x = params.img_width / params.cell_size;
        cells_y = params.img_height / params.cell_size;
        size_t cell_grid_size = cells_x * cells_y * params.nbins;
        cell_grid_buffer = new float[cell_grid_size];
        
        if (!gradient_x_buffer || !gradient_y_buffer || !magnitude_buffer || 
            !angle_bin_buffer || !block_histogram_buffer || !cell_histogram_buffer ||
            !cell_grid_buffer) {
            Serial.println("Error: Failed to allocate HOG optimization buffers");
            cleanupBuffers();
        }
    }
}

void HOG_MCU::cleanupBuffers() {
    if (processed_image_buffer) {
        delete[] processed_image_buffer;
        processed_image_buffer = nullptr;
    }
    if (gradient_x_buffer) {
        delete[] gradient_x_buffer;
        gradient_x_buffer = nullptr;
    }
    if (gradient_y_buffer) {
        delete[] gradient_y_buffer;
        gradient_y_buffer = nullptr;
    }
    if (magnitude_buffer) {
        delete[] magnitude_buffer;
        magnitude_buffer = nullptr;
    }
    if (angle_bin_buffer) {
        delete[] angle_bin_buffer;
        angle_bin_buffer = nullptr;
    }
    if (block_histogram_buffer) {
        delete[] block_histogram_buffer;
        block_histogram_buffer = nullptr;
    }
    if (cell_histogram_buffer) {
        delete[] cell_histogram_buffer;
        cell_histogram_buffer = nullptr;
    }
    if (cell_grid_buffer) {
        delete[] cell_grid_buffer;
        cell_grid_buffer = nullptr;
    }
}

void HOG_MCU::compute(const uint8_t* grayImage) {
    int numBlocksY = (params.img_height - params.block_size) / params.block_stride + 1;
    int numBlocksX = (params.img_width - params.block_size) / params.block_stride + 1;

    for (int by = 0; by < numBlocksY; ++by) {
        for (int bx = 0; bx < numBlocksX; ++bx) {
            mcu::b_vector<float, 16> blockHist; // 4 cells * 4 bins = 16 features per block
            blockHist.clear();
            
            // Initialize block histogram
            for (int i = 0; i < params.nbins * 4; ++i) {
                blockHist.push_back(0.0f);
            }

            for (int cy = 0; cy < 2; ++cy) {
                for (int cx = 0; cx < 2; ++cx) {
                    mcu::b_vector<float, 4> hist; // nbins = 4
                    hist.clear();
                    for (int i = 0; i < params.nbins; ++i) {
                        hist.push_back(0.0f);
                    }
                    
                    int startX = bx * params.block_stride + cx * params.cell_size;
                    int startY = by * params.block_stride + cy * params.cell_size;

                    for (int y = 0; y < params.cell_size; ++y) {
                        for (int x = 0; x < params.cell_size; ++x) {
                            int ix = startX + x;
                            int iy = startY + y;
                            if (ix <= 0 || ix >= params.img_width - 1 || iy <= 0 || iy >= params.img_height - 1)
                                continue;

                            int gx = grayImage[iy * params.img_width + (ix + 1)] -
                                     grayImage[iy * params.img_width + (ix - 1)];
                            int gy = grayImage[(iy + 1) * params.img_width + ix] -
                                     grayImage[(iy - 1) * params.img_width + ix];

                            float mag = computeGradientMagnitude(gx, gy);
                            float angle = computeGradientAngle(gx, gy);
                            if (angle < 0) angle += 180.0f;

                            int bin = (int)(angle / (180.0f / params.nbins));
                            if (bin >= params.nbins) bin = params.nbins - 1;

                            hist[bin] += mag;
                        }
                    }

                    for (int i = 0; i < params.nbins; ++i) {
                        blockHist[(cy * 2 + cx) * params.nbins + i] = hist[i];
                    }
                }
            }

            // Normalize block histogram
            float norm = 0.0f;
            for (int i = 0; i < blockHist.size(); ++i) {
                norm += blockHist[i] * blockHist[i];
            }
            norm = sqrt(norm + 1e-6f);
            for (int i = 0; i < blockHist.size(); ++i) {
                blockHist[i] /= norm;
            }

            // Add normalized block features to feature vector
            for (int i = 0; i < blockHist.size(); ++i) {
                if (features.size() < 144) { // Ensure we don't exceed capacity
                    features.push_back(blockHist[i]);
                }
            }
        }
    }
}

float HOG_MCU::computeGradientMagnitude(int gx, int gy) {
    return sqrt(gx * gx + gy * gy);
}

float HOG_MCU::computeGradientAngle(int gx, int gy) {
    return atan2(gy, gx) * 180.0f / PI;
}

// Optimized gradient computation using integer arithmetic
void HOG_MCU::computeGradientsOptimized(const uint8_t* grayImage) {
    const int width = params.img_width;
    const int height = params.img_height;
    
    // Compute gradients for all pixels
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            int idx = y * width + x;
            
            // Compute gradients using integer arithmetic
            int gx = (int)grayImage[idx + 1] - (int)grayImage[idx - 1];
            int gy = (int)grayImage[idx + width] - (int)grayImage[idx - width];
            
            gradient_x_buffer[idx] = gx;
            gradient_y_buffer[idx] = gy;
            
            // Magnitude: use integer approximation |gx| + |gy| (faster than sqrt)
            int abs_gx = gx >= 0 ? gx : -gx;
            int abs_gy = gy >= 0 ? gy : -gy;
            magnitude_buffer[idx] = abs_gx + abs_gy;
            
            // Integer-based bin classification (no atan2!)
            // For 4 bins: [0°-45°, 45°-90°, 90°-135°, 135°-180°]
            uint8_t bin = 0;
            if (params.nbins == 4) {
                // Compare abs_gx and abs_gy to determine bin
                if (abs_gx >= abs_gy) {
                    // Dominantly horizontal (0°-45° or 135°-180°)
                    bin = (gx >= 0) ? 0 : 2;  // 0° or 180° quadrant
                } else {
                    // Dominantly vertical (45°-90° or 90°-135°)
                    bin = (gy >= 0) ? 1 : 3;  // 90° quadrant or opposite
                }
            } else {
                // Fallback for non-4-bin configs (rarely used)
                float angle = atan2f(gy, gx) * 180.0f / PI;
                if (angle < 0) angle += 180.0f;
                bin = (int)(angle / (180.0f / params.nbins));
                if (bin >= params.nbins) bin = params.nbins - 1;
            }
            angle_bin_buffer[idx] = bin;
        }
    }
}

// Pre-compute all cell histograms once to avoid redundant pixel walks
void HOG_MCU::computeCellGrid(const uint8_t* grayImage) {
    const int width = params.img_width;
    const int height = params.img_height;
    const int nbins = params.nbins;
    
    // Clear cell grid
    size_t cell_grid_size = cells_x * cells_y * nbins;
    for (size_t i = 0; i < cell_grid_size; ++i) {
        cell_grid_buffer[i] = 0.0f;
    }
    
    // Compute histogram for each cell in the grid
    for (int cell_y = 0; cell_y < cells_y; ++cell_y) {
        for (int cell_x = 0; cell_x < cells_x; ++cell_x) {
            int startX = cell_x * params.cell_size;
            int startY = cell_y * params.cell_size;
            
            // Index into cell grid buffer: (cell_y * cells_x + cell_x) * nbins
            int cell_idx = (cell_y * cells_x + cell_x) * nbins;
            
            // Accumulate histogram for this cell
            for (int y = 0; y < params.cell_size; ++y) {
                for (int x = 0; x < params.cell_size; ++x) {
                    int ix = startX + x;
                    int iy = startY + y;
                    
                    if (ix <= 0 || ix >= width - 1 || iy <= 0 || iy >= height - 1)
                        continue;
                    
                    int idx = iy * width + ix;
                    uint8_t bin = angle_bin_buffer[idx];
                    cell_grid_buffer[cell_idx + bin] += magnitude_buffer[idx];
                }
            }
        }
    }
}

// Optimized compute method using pre-allocated buffers
void HOG_MCU::computeOptimized(const uint8_t* grayImage) {
    if (!grayImage || !gradient_x_buffer || !gradient_y_buffer || 
        !magnitude_buffer || !angle_bin_buffer || !block_histogram_buffer ||
        !cell_grid_buffer) {
        // Fallback to original compute if buffers not initialized
        compute(grayImage);
        return;
    }
    
    // Step 1: Pre-compute all gradients once
    computeGradientsOptimized(grayImage);
    
    // Step 2: Pre-compute all cell histograms once
    computeCellGrid(grayImage);
    
    const int numBlocksY = (params.img_height - params.block_size) / params.block_stride + 1;
    const int numBlocksX = (params.img_width - params.block_size) / params.block_stride + 1;
    const int nbins = params.nbins;
    const int cells_per_block = params.block_size / params.cell_size; // typically 2
    
    // Step 3: Build blocks from cached cells
    for (int by = 0; by < numBlocksY; ++by) {
        for (int bx = 0; bx < numBlocksX; ++bx) {
            // Clear block histogram
            for (int i = 0; i < nbins * 4; ++i) {
                block_histogram_buffer[i] = 0.0f;
            }
            
            // Determine which cells this block spans
            int block_start_x = bx * params.block_stride;
            int block_start_y = by * params.block_stride;
            int start_cell_x = block_start_x / params.cell_size;
            int start_cell_y = block_start_y / params.cell_size;
            
            // Assemble block histogram from cached cells (2x2 cells per block)
            for (int cy = 0; cy < cells_per_block; ++cy) {
                for (int cx = 0; cx < cells_per_block; ++cx) {
                    int cell_x = start_cell_x + cx;
                    int cell_y = start_cell_y + cy;
                    
                    if (cell_x < cells_x && cell_y < cells_y) {
                        int cell_idx = (cell_y * cells_x + cell_x) * nbins;
                        int block_offset = (cy * cells_per_block + cx) * nbins;
                        
                        // Copy cell histogram to block histogram
                        for (int i = 0; i < nbins; ++i) {
                            block_histogram_buffer[block_offset + i] = cell_grid_buffer[cell_idx + i];
                        }
                    }
                }
            }
            
            // Normalize block histogram
            float norm = 0.0f;
            for (int i = 0; i < nbins * 4; ++i) {
                norm += block_histogram_buffer[i] * block_histogram_buffer[i];
            }
            norm = sqrtf(norm + 1e-6f);
            
            // Add normalized features to output
            for (int i = 0; i < nbins * 4; ++i) {
                if (features.size() < 144) {
                    features.push_back(block_histogram_buffer[i] / norm);
                }
            }
        }
    }
}
