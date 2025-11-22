#include <iostream>
#include <filesystem>
#include <system_error>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <random>
#include <algorithm>
#include <string>
#include <opencv2/opencv.hpp>
#include "STL_MCU.h"

// Simple JSON parser for our config file
struct Config {
    struct Workflow {
        std::string name;
        std::string description;
    } workflow;
    
    struct Input {
        std::string dataset_path;
        std::string image_format;
        std::string description;
    } input;
    
    struct Preprocessing {
        struct InputCameraConfig {
            std::string input_format = "GRAYSCALE";
            int input_width = 320;
            int input_height = 240;
            std::string resize_method = "BILINEAR";
            bool maintain_aspect_ratio = false;
            int jpeg_quality = 80;
        } input_camera_config;
        bool grayscale = true;
        bool normalize = true;
        std::string description;
    } preprocessing;
    
    struct HOGParameters {
        int img_width = 32;
        int img_height = 32;
        int cell_size = 8;
        int block_size = 16;
        int block_stride = 6;
        int nbins = 4;
        std::string description;
    } hog_parameters;
    
    struct Output {
        std::string intermediate_path;
        std::string model_name = "hog_features";
        bool shuffle_data = true;
        std::string description;
    } output;
    
    struct Processing {
        int max_images_per_class = -1;
        bool verbose = false;
        std::string description;
    } processing;
};

namespace {

std::filesystem::path computeModelBasePath(const std::string& modelName) {
    if (modelName.empty()) {
        return std::filesystem::path("hog_features");
    }

    std::filesystem::path base(modelName);
    if (base.extension() == ".csv") {
        base.replace_extension("");
    }
    if (base.filename().empty()) {
        base /= "hog_features";
    }
    return base;
}

bool ensureParentDirectoryExists(const std::filesystem::path& target) {
    const auto parent = target.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        std::cerr << "Error: Failed to create directory " << parent << " (" << ec.message() << ")" << std::endl;
        return false;
    }
    return true;
}

bool writeModelConfigFile(const Config& config,
                          size_t featureLength,
                          const std::string& modelName,
                          const std::string& csvOutputPath,
                          const std::filesystem::path& cfgPath) {
    if (featureLength == 0) {
        std::cerr << "Error: Feature length is zero, cannot write model configuration." << std::endl;
        return false;
    }

    if (!ensureParentDirectoryExists(cfgPath)) {
        return false;
    }

    const std::string featureFileName = std::filesystem::path(csvOutputPath).filename().string();

    std::ofstream cfgFile(cfgPath, std::ios::trunc);
    if (!cfgFile.is_open()) {
        std::cerr << "Error: Cannot open config file " << cfgPath << " for writing." << std::endl;
        return false;
    }

    cfgFile << "{\n";
    // cfgFile << "  \"model_name\": \"" << modelName << "\",\n";
    // cfgFile << "  \"feature_csv\": \"" << csvOutputPath << "\",\n";
    // cfgFile << "  \"feature_file_name\": \"" << featureFileName << "\",\n";
    // cfgFile << "  \"feature_length\": " << featureLength << ",\n";
    // cfgFile << "  \"preprocessing\": {\n";
    // cfgFile << "    \"grayscale\": " << (config.preprocessing.grayscale ? "true" : "false") << ",\n";
    // cfgFile << "    \"normalize\": " << (config.preprocessing.normalize ? "true" : "false") << ",\n";
    // cfgFile << "    \"target_width\": " << config.preprocessing.target_size.width << ",\n";
    // cfgFile << "    \"target_height\": " << config.preprocessing.target_size.height << "\n";
    // cfgFile << "  },\n";
    cfgFile << "  \"camera_config\": {\n";
    cfgFile << "    \"input_format\": \"" << config.preprocessing.input_camera_config.input_format << "\",\n";
    cfgFile << "    \"input_width\": " << config.preprocessing.input_camera_config.input_width << ",\n";
    cfgFile << "    \"input_height\": " << config.preprocessing.input_camera_config.input_height << ",\n";
    cfgFile << "    \"resize_method\": \"" << config.preprocessing.input_camera_config.resize_method << "\",\n";
    cfgFile << "    \"maintain_aspect_ratio\": " << (config.preprocessing.input_camera_config.maintain_aspect_ratio ? "true" : "false") << ",\n";
    cfgFile << "    \"jpeg_quality\": " << config.preprocessing.input_camera_config.jpeg_quality << "\n";
    cfgFile << "  },\n";
    cfgFile << "  \"hog\": {\n";
    cfgFile << "    \"hog_img_width\": " << config.hog_parameters.img_width << ",\n";
    cfgFile << "    \"hog_img_height\": " << config.hog_parameters.img_height << ",\n";
    cfgFile << "    \"cell_size\": " << config.hog_parameters.cell_size << ",\n";
    cfgFile << "    \"block_size\": " << config.hog_parameters.block_size << ",\n";
    cfgFile << "    \"block_stride\": " << config.hog_parameters.block_stride << ",\n";
    cfgFile << "    \"nbins\": " << config.hog_parameters.nbins << "\n";
    cfgFile << "  }\n";
    cfgFile << "}\n";

    cfgFile.close();
    return true;
}

} // namespace

class SimpleJSONParser {
public:
    static Config parseConfig(const std::string& filename) {
        Config config;
        std::ifstream file(filename);
        
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open config file " << filename << std::endl;
            throw std::runtime_error("Config file not found");
        }
        
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        // Parse workflow
        config.workflow.name = extractStringValue(content, "name");
        config.workflow.description = extractStringValue(content, "description");
        
        // Parse input
        config.input.dataset_path = extractStringValue(content, "dataset_name");
        config.input.image_format = extractStringValue(content, "image_format");
        
        // Parse preprocessing
        config.preprocessing.grayscale = extractBoolValue(content, "grayscale");
        config.preprocessing.normalize = extractBoolValue(content, "normalize");
        
        // Parse input_camera_config settings from preprocessing section
        const std::string parsedInputFormat = extractStringValue(content, "input_format");
        if (!parsedInputFormat.empty()) {
            config.preprocessing.input_camera_config.input_format = parsedInputFormat;
        }
        if (containsKey(content, "input_width")) {
            config.preprocessing.input_camera_config.input_width = extractIntValue(content, "input_width");
        }
        if (containsKey(content, "input_height")) {
            config.preprocessing.input_camera_config.input_height = extractIntValue(content, "input_height");
        }
        const std::string parsedResizeMethod = extractStringValue(content, "resize_method");
        if (!parsedResizeMethod.empty()) {
            config.preprocessing.input_camera_config.resize_method = parsedResizeMethod;
        }
        if (containsKey(content, "maintain_aspect_ratio")) {
            config.preprocessing.input_camera_config.maintain_aspect_ratio = extractBoolValue(content, "maintain_aspect_ratio");
        }
        if (containsKey(content, "jpeg_quality")) {
            config.preprocessing.input_camera_config.jpeg_quality = extractIntValue(content, "jpeg_quality");
        }
        
        // Parse HOG parameters
        config.hog_parameters.img_width = extractIntValue(content, "img_width");
        config.hog_parameters.img_height = extractIntValue(content, "img_height");
        config.hog_parameters.cell_size = extractIntValue(content, "cell_size");
        config.hog_parameters.block_size = extractIntValue(content, "block_size");
        config.hog_parameters.block_stride = extractIntValue(content, "block_stride");
        config.hog_parameters.nbins = extractIntValue(content, "nbins");
        
        // Parse output
        config.output.intermediate_path = extractStringValue(content, "intermediate_path");
        const std::string parsedModelName = extractStringValue(content, "model_name");
        if (!parsedModelName.empty()) {
            config.output.model_name = parsedModelName;
        }
        if (containsKey(content, "shuffle_data")) {
            config.output.shuffle_data = extractBoolValue(content, "shuffle_data");
        }
        
        // Parse processing
        if (containsKey(content, "max_images_per_class")) {
            config.processing.max_images_per_class = extractIntValue(content, "max_images_per_class");
        }
        if (containsKey(content, "verbose")) {
            config.processing.verbose = extractBoolValue(content, "verbose");
        }
        
        return config;
    }
    
private:
    static bool containsKey(const std::string& json, const std::string& key) {
        std::regex pattern("\\\"" + key + "\\\"\\s*:");
        return std::regex_search(json, pattern);
    }

    static std::string extractStringValue(const std::string& json, const std::string& key) {
        std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(json, match, pattern)) {
            return match[1].str();
        }
        return "";
    }
    
    static int extractIntValue(const std::string& json, const std::string& key) {
        std::regex pattern("\"" + key + "\"\\s*:\\s*(\\d+)");
        std::smatch match;
        if (std::regex_search(json, match, pattern)) {
            return std::stoi(match[1].str());
        }
        return 0;
    }
    
    static bool extractBoolValue(const std::string& json, const std::string& key) {
        std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
        std::smatch match;
        if (std::regex_search(json, match, pattern)) {
            return match[1].str() == "true";
        }
        return false;
    }
};

class HOGDescriptorMCU {
public:
    struct Params {
        int img_width;
        int img_height;
        int cell_size;
        int block_size;
        int block_stride;
        int nbins;
    };

    HOGDescriptorMCU(const Params& p) : params(p) {
        // Allocate buffers for precomputation
        size_t buffer_size = static_cast<size_t>(params.img_width) * static_cast<size_t>(params.img_height);
        magnitude_buffer.resize(buffer_size);
        angle_bin_buffer.resize(buffer_size);

        // Allocate cell grid buffer
        cells_x = params.img_width / params.cell_size;
        cells_y = params.img_height / params.cell_size;
        size_t cell_grid_size = static_cast<size_t>(cells_x) * static_cast<size_t>(cells_y) * params.nbins;
        cell_grid_buffer.resize(cell_grid_size);
    }

    void compute(const uint8_t* grayImage, mcu::vector<float>& outVec) {
        // Precompute per-pixel magnitude and bin using atan2 for accuracy
        computeMagnitudesAndBins(grayImage);
        // Build cell histograms once
        computeCellGrid();
        int numBlocksY = (params.img_height - params.block_size) / params.block_stride + 1;
        int numBlocksX = (params.img_width - params.block_size) / params.block_stride + 1;

        outVec.clear();
        outVec.reserve(numBlocksX * numBlocksY * (4 * params.nbins));

        for (int by = 0; by < numBlocksY; ++by) {
            for (int bx = 0; bx < numBlocksX; ++bx) {
                mcu::vector<float> blockHist(params.nbins * 4, 0.0f); // 2x2 cells

                for (int cy = 0; cy < 2; ++cy) {
                    for (int cx = 0; cx < 2; ++cx) {
                        mcu::vector<float> hist(params.nbins, 0.0f);
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

                                float magnitude = std::sqrt(gx * gx + gy * gy);
                                float angle = std::atan2(gy, gx);
                                if (angle < 0) angle += 2 * M_PI;

                                float binSize = (2 * M_PI) / params.nbins;
                                int bin = static_cast<int>(angle / binSize) % params.nbins;

                                hist[bin] += magnitude;
                            }
                        }

                        for (int i = 0; i < params.nbins; ++i) {
                            blockHist[cy * 2 * params.nbins + cx * params.nbins + i] = hist[i];
                        }
                    }
                }

                // L2 normalization
                float norm = 0.0f;
                for (float val : blockHist) {
                    norm += val * val;
                }
                norm = std::sqrt(norm) + 1e-6f;

                for (float val : blockHist) {
                    outVec.push_back(val / norm);
                }
            }
        }
    }

private:
    Params params;
    mcu::vector<float> magnitude_buffer;
    mcu::vector<uint8_t> angle_bin_buffer;
    mcu::vector<float> cell_grid_buffer;
    int cells_x;
    int cells_y;

    void computeMagnitudesAndBins(const uint8_t* grayImage) {
        const int width = params.img_width;
        const int height = params.img_height;
        const float binSize = (2.0f * M_PI) / params.nbins;

        // Compute magnitudes and angle bins for all pixels
        for (int y = 1; y < height - 1; ++y) {
            for (int x = 1; x < width - 1; ++x) {
                int idx = y * width + x;

                int gx = static_cast<int>(grayImage[idx + 1]) - static_cast<int>(grayImage[idx - 1]);
                int gy = static_cast<int>(grayImage[idx + width]) - static_cast<int>(grayImage[idx - width]);

                float magnitude = std::sqrt(static_cast<float>(gx * gx + gy * gy));
                magnitude_buffer[idx] = magnitude;

                float angle = std::atan2(static_cast<float>(gy), static_cast<float>(gx));
                if (angle < 0.0f) angle += 2.0f * M_PI;

                int bin = static_cast<int>(angle / binSize) % params.nbins;
                if (bin < 0) bin = 0;
                if (bin >= params.nbins) bin = params.nbins - 1;

                angle_bin_buffer[idx] = static_cast<uint8_t>(bin);
            }
        }
    }

    void computeCellGrid() {
        const int width = params.img_width;
        const int height = params.img_height;
        const int nbins = params.nbins;

        // Clear cell grid
        for (size_t i = 0; i < cell_grid_buffer.size(); ++i) {
            cell_grid_buffer[i] = 0.0f;
        }

        // Accumulate histograms per cell
        for (int cell_y = 0; cell_y < cells_y; ++cell_y) {
            for (int cell_x = 0; cell_x < cells_x; ++cell_x) {
                int startX = cell_x * params.cell_size;
                int startY = cell_y * params.cell_size;
                int cell_idx = (cell_y * cells_x + cell_x) * nbins;

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
};

class ImageProcessor {
public:
    static mcu::vector<uint8_t> loadImageData(const std::string& filepath, const std::string& format, const Config& config) {
        if (format == "txt") {
            return parse_txt_image_file(filepath, config);
        } else if (format == "png" || format == "jpg" || format == "jpeg" || format == "bmp" || format == "tiff") {
            return parse_image_file(filepath, config);
        } else {
            std::cerr << "Error: Unsupported image format: " << format << std::endl;
            return mcu::vector<uint8_t>();
        }
    }
    
private:
    static mcu::vector<uint8_t> parse_image_file(const std::string& path, const Config& config) {
        mcu::vector<uint8_t> data;
        
        // Load image using OpenCV
        cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
        if (img.empty()) {
            std::cerr << "Error: Cannot load image " << path << std::endl;
            return data;
        }
        
        // Convert to grayscale if requested
        cv::Mat gray_img;
        if (config.preprocessing.grayscale) {
            cv::cvtColor(img, gray_img, cv::COLOR_BGR2GRAY);
        } else {
            gray_img = img;
        }
        
        // Resize to HOG image size
        cv::Mat resized_img;
        cv::Size target_size(config.hog_parameters.img_width, 
                           config.hog_parameters.img_height);
        cv::resize(gray_img, resized_img, target_size);
        
        // Convert to uint8_t vector
        int expected_size = resized_img.rows * resized_img.cols;
        
        if (resized_img.channels() == 1) {
            // Grayscale image
            data.reserve(expected_size);
            for (int y = 0; y < resized_img.rows; y++) {
                for (int x = 0; x < resized_img.cols; x++) {
                    uint8_t pixel_value = resized_img.at<uint8_t>(y, x);
                    
                    // Apply normalization if requested
                    if (config.preprocessing.normalize) {
                        // Already in 0-255 range for uint8_t
                        pixel_value = std::min(255, std::max(0, static_cast<int>(pixel_value)));
                    }
                    
                    data.push_back(pixel_value);
                }
            }
        } else {
            // Multi-channel image - convert to grayscale manually
            data.reserve(expected_size);
            for (int y = 0; y < resized_img.rows; y++) {
                for (int x = 0; x < resized_img.cols; x++) {
                    cv::Vec3b pixel = resized_img.at<cv::Vec3b>(y, x);
                    // Convert BGR to grayscale using standard weights
                    uint8_t gray_value = static_cast<uint8_t>(
                        0.299 * pixel[2] + 0.587 * pixel[1] + 0.114 * pixel[0]
                    );
                    
                    if (config.preprocessing.normalize) {
                        gray_value = std::min(255, std::max(0, static_cast<int>(gray_value)));
                    }
                    
                    data.push_back(gray_value);
                }
            }
        }
        
        return data;
    }
    
private:
    static mcu::vector<uint8_t> parse_txt_image_file(const std::string& path, const Config& config) {
        mcu::vector<uint8_t> data;
        std::ifstream file(path);
        std::string line;

        if (!file.is_open()) {
            std::cerr << "Error: Cannot open " << path << std::endl;
            return data;
        }

        std::regex number_regex(R"((\d+))");
        bool array_data_started = false;

        while (std::getline(file, line)) {
            std::string segment_to_parse;

            if (!array_data_started) {
                size_t open_brace_pos = line.find('{');
                if (open_brace_pos != std::string::npos) {
                    array_data_started = true;
                    segment_to_parse = line.substr(open_brace_pos + 1);
                } else {
                    continue;
                }
            } else {
                segment_to_parse = line;
            }

            size_t close_brace_pos = segment_to_parse.find('}');
            if (close_brace_pos != std::string::npos) {
                segment_to_parse = segment_to_parse.substr(0, close_brace_pos);
            }

            std::smatch match;
            auto search_begin = segment_to_parse.cbegin();
            auto search_end = segment_to_parse.cend();
            
            while (std::regex_search(search_begin, search_end, match, number_regex)) {
                if (match[1].matched) {
                    try {
                        uint8_t value = static_cast<uint8_t>(std::stoi(match[1].str()));
                        if (config.preprocessing.normalize) {
                            // Normalize to 0-255 range if needed
                            value = std::min(255, std::max(0, static_cast<int>(value)));
                        }
                        data.push_back(value);
                    } catch (const std::exception& e) {
                        if (config.processing.verbose) {
                            std::cerr << "Warning: Invalid number in " << path << ": " << match[1].str() << std::endl;
                        }
                    }
                }
                search_begin = match.suffix().first;
                if (search_begin == search_end && match.empty()) break;
                if (!match.empty() && match[0].length() == 0 && search_begin != search_end) {
                    ++search_begin;
                }
            }

            if (array_data_started && line.find('}') != std::string::npos) {
                break;
            }
        }

        return data;
    }
};

class UnifiedProcessor {
public:
    static int processDataset(const Config& config) {
        std::filesystem::path basePath = computeModelBasePath(config.output.model_name);
        const std::string modelNameForConfig = basePath.generic_string();
        
        // Output to result/ directory
        std::filesystem::path resultDir("result");
        std::filesystem::path csvPath = resultDir / basePath.filename();
        csvPath += ".csv";
        std::filesystem::path cfgPath = resultDir / basePath.filename();
        cfgPath += "_hogcfg.json";
        const std::string csvOutputPath = csvPath.generic_string();
        const std::string cfgOutputPath = cfgPath.generic_string();

        if (!ensureParentDirectoryExists(csvPath)) {
            return 1;
        }

        size_t feature_length = 0;

        if (config.processing.verbose) {
            std::cout << "=== " << config.workflow.name << " ===" << std::endl;
            std::cout << config.workflow.description << std::endl;
            std::cout << "Dataset path: " << config.input.dataset_path << std::endl;
            std::cout << "Output CSV: " << csvOutputPath << std::endl;
            std::cout << "Input camera config: " << cfgOutputPath << std::endl;
            std::cout << "Max images per class: " << config.processing.max_images_per_class << std::endl;
        }

        // Setup HOG descriptor
        HOGDescriptorMCU::Params hog_params = {
            config.hog_parameters.img_width,
            config.hog_parameters.img_height,
            config.hog_parameters.cell_size,
            config.hog_parameters.block_size,
            config.hog_parameters.block_stride,
            config.hog_parameters.nbins
        };
        
        HOGDescriptorMCU hog(hog_params);

        // Structure to hold CSV rows (only needed if shuffling)
        struct CSVRow {
            std::string class_name;
            mcu::vector<float> features;
        };
        
        mcu::vector<CSVRow> csv_data;
        
        // Open CSV file for writing (if not shuffling, write directly)
        std::ofstream csv_file;
        if (!config.output.shuffle_data) {
            csv_file.open(csvPath, std::ios::trunc);
            if (!csv_file.is_open()) {
                std::cerr << "Error: Cannot open CSV file " << csvOutputPath << " for writing." << std::endl;
                return 1;
            }
        }

        // Process each class folder
        namespace fs = std::filesystem;
        
        if (!fs::exists(config.input.dataset_path)) {
            std::cerr << "Error: Dataset path does not exist: " << config.input.dataset_path << std::endl;
            return 1;
        }

        for (const auto& subfolder_entry : fs::directory_iterator(config.input.dataset_path)) {
            if (subfolder_entry.is_directory()) {
                std::filesystem::path subfolder_path = subfolder_entry.path();
                std::string class_name = subfolder_path.filename().string();

                int images_processed = 0;
                int images_skipped = 0;
                
                // Process images in this class folder
                for (const auto& file_entry : fs::directory_iterator(subfolder_path)) {
                    if (config.processing.max_images_per_class > 0 && 
                        images_processed >= config.processing.max_images_per_class) {
                        break;
                    }
                    
                    // Check if file has a supported image extension
                    std::string extension = file_entry.path().extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                    
                    bool is_supported_format = false;
                    std::string detected_format;
                    
                    if (extension == ".txt") {
                        is_supported_format = true;
                        detected_format = "txt";
                    } else if (extension == ".png") {
                        is_supported_format = true;
                        detected_format = "png";
                    } else if (extension == ".jpg" || extension == ".jpeg") {
                        is_supported_format = true;
                        detected_format = "jpg";
                    } else if (extension == ".bmp") {
                        is_supported_format = true;
                        detected_format = "bmp";
                    } else if (extension == ".tiff" || extension == ".tif") {
                        is_supported_format = true;
                        detected_format = "tiff";
                    }
                    
                    if (is_supported_format) {
                        // Load and validate image data
                        mcu::vector<uint8_t> imageData = ImageProcessor::loadImageData(
                            file_entry.path().string(), 
                            detected_format, 
                            config
                        );

                        size_t expected_size = static_cast<size_t>(
                            config.hog_parameters.img_width * config.hog_parameters.img_height
                        );
                        
                        if (imageData.size() != expected_size) {
                            images_skipped++;
                            continue;
                        }

                        // Extract HOG features
                        mcu::vector<float> features;
                        hog.compute(imageData.data(), features);

                        if (feature_length == 0) {
                            feature_length = features.size();
                        }

                        // If not shuffling, write directly to file
                        if (!config.output.shuffle_data) {
                            csv_file << class_name;
                            for (float f : features) {
                                csv_file << "," << f;
                            }
                            csv_file << "\n";
                        } else {
                            // Store for later processing (shuffling)
                            CSVRow row;
                            row.class_name = class_name;
                            row.features = std::move(features);
                            csv_data.push_back(std::move(row));
                        }

                        images_processed++;
                    }
                }
                
                if (config.processing.verbose) {
                    std::cout << "Processing class: " << class_name << " - " << images_processed 
                              << " processed" << (images_skipped > 0 ? " (" + std::to_string(images_skipped) + " skipped)" : "") << std::endl;
                }
            }
        }

        // Close the file if we were writing directly
        if (!config.output.shuffle_data) {
            if (feature_length == 0) {
                csv_file.close();
                std::error_code removeEc;
                std::filesystem::remove(csvPath, removeEc);
                std::cerr << "Error: No valid images found for processing!" << std::endl;
                return 1;
            }

            csv_file.close();

            if (!writeModelConfigFile(config, feature_length, modelNameForConfig, csvOutputPath, cfgPath)) {
                return 1;
            }

            if (config.processing.verbose) {
                std::cout << "Processing complete! Results written to " << csvOutputPath << std::endl;
                std::cout << "Config saved to " << cfgOutputPath << std::endl;
            }

            return 0;
        }

        if (csv_data.empty()) {
            std::cerr << "Error: No valid images found for processing!" << std::endl;
            return 1;
        }

        if (feature_length == 0) {
            std::cerr << "Error: Failed to compute HOG features for the dataset." << std::endl;
            return 1;
        }

        // Shuffle data if requested
        if (config.output.shuffle_data) {
            if (config.processing.verbose) {
                std::cout << "Shuffling dataset..." << std::endl;
            }
            std::random_device rd;
            std::mt19937 gen(rd());
            std::shuffle(csv_data.begin(), csv_data.end(), gen);
        }

        // Write to CSV file
        csv_file.open(csvPath, std::ios::trunc);
        if (!csv_file.is_open()) {
            std::cerr << "Error: Cannot open CSV file " << csvOutputPath << " for writing." << std::endl;
            return 1;
        }

        if (config.processing.verbose) {
            std::cout << "Writing " << csv_data.size() << " rows to CSV file..." << std::endl;
        }
        
        for (const auto& row : csv_data) {
            csv_file << row.class_name;
            for (float f : row.features) {
                csv_file << "," << f;
            }
            csv_file << "\n";
        }

        csv_file.close();
        
        if (!writeModelConfigFile(config, feature_length, modelNameForConfig, csvOutputPath, cfgPath)) {
            return 1;
        }

        if (config.processing.verbose) {
            std::cout << "Processing complete! Results written to " << csvOutputPath << std::endl;
            if (config.output.shuffle_data) {
                std::cout << "(Data was shuffled)" << std::endl;
            }
            std::cout << "Config saved to " << cfgOutputPath << std::endl;
        }

        return 0;
    }
};

int main(int argc, char* argv[]) {
    std::string config_file = "hog_config.json";
    
    // Allow config file to be specified as command line argument
    if (argc > 1) {
        config_file = argv[1];
    }
    
    try {
        // Load configuration
        Config config = SimpleJSONParser::parseConfig(config_file);
        
        // Process dataset
        return UnifiedProcessor::processDataset(config);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
