#include <iostream>
#include <filesystem>
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
        struct TargetSize {
            int width;
            int height;
        } target_size;
        bool grayscale;
        bool normalize;
        std::string description;
    } preprocessing;
    
    struct HOGParameters {
        int img_width;
        int img_height;
        int cell_size;
        int block_size;
        int block_stride;
        int nbins;
        std::string description;
    } hog_parameters;
    
    struct Output {
        std::string intermediate_path;
        std::string csv_path;
        bool shuffle_data;
        std::string description;
    } output;
    
    struct Processing {
        int max_images_per_class;
        bool verbose;
        std::string description;
    } processing;
};

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
        config.input.dataset_path = extractStringValue(content, "dataset_path");
        config.input.image_format = extractStringValue(content, "image_format");
        
        // Parse preprocessing
        config.preprocessing.target_size.width = extractIntValue(content, "width");
        config.preprocessing.target_size.height = extractIntValue(content, "height");
        config.preprocessing.grayscale = extractBoolValue(content, "grayscale");
        config.preprocessing.normalize = extractBoolValue(content, "normalize");
        
        // Parse HOG parameters
        config.hog_parameters.img_width = extractIntValue(content, "img_width");
        config.hog_parameters.img_height = extractIntValue(content, "img_height");
        config.hog_parameters.cell_size = extractIntValue(content, "cell_size");
        config.hog_parameters.block_size = extractIntValue(content, "block_size");
        config.hog_parameters.block_stride = extractIntValue(content, "block_stride");
        config.hog_parameters.nbins = extractIntValue(content, "nbins");
        
        // Parse output
        config.output.intermediate_path = extractStringValue(content, "intermediate_path");
        config.output.csv_path = extractStringValue(content, "csv_path");
        config.output.shuffle_data = extractBoolValue(content, "shuffle_data");
        
        // Parse processing
        config.processing.max_images_per_class = extractIntValue(content, "max_images_per_class");
        config.processing.verbose = extractBoolValue(content, "verbose");
        
        return config;
    }
    
private:
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

    HOGDescriptorMCU(const Params& p) : params(p) {}

    void compute(const uint8_t* grayImage, mcu::vector<float>& outVec) {
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
        
        if (config.processing.verbose) {
            std::cout << "    Loading image: " << path << std::endl;
        }
        
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
        
        // Resize to target size
        cv::Mat resized_img;
        cv::Size target_size(config.preprocessing.target_size.width, 
                           config.preprocessing.target_size.height);
        cv::resize(gray_img, resized_img, target_size);
        
        // Convert to uint8_t vector
        int expected_size = config.preprocessing.target_size.width * 
                           config.preprocessing.target_size.height;
        
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
        
        if (config.processing.verbose) {
            std::cout << "    Processed to " << data.size() << " pixels (" 
                      << config.preprocessing.target_size.width << "x" 
                      << config.preprocessing.target_size.height << ")" << std::endl;
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
        if (config.processing.verbose) {
            std::cout << "=== " << config.workflow.name << " ===" << std::endl;
            std::cout << config.workflow.description << std::endl;
            std::cout << "Dataset path: " << config.input.dataset_path << std::endl;
            std::cout << "Output CSV: " << config.output.csv_path << std::endl;
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

        // Structure to hold CSV rows
        struct CSVRow {
            std::string class_name;
            mcu::vector<float> features;
        };
        
        mcu::vector<CSVRow> csv_data;

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

                if (config.processing.verbose) {
                    std::cout << "Processing class: " << class_name << std::endl;
                }

                int images_processed = 0;
                
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
                        if (config.processing.verbose) {
                            std::cout << "  Processing: " << file_entry.path().filename() 
                                      << " (format: " << detected_format << ")" << std::endl;
                        }

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
                            if (config.processing.verbose) {
                                std::cerr << "  Skipping invalid image: " << file_entry.path() 
                                          << " (expected " << expected_size << " pixels, got " 
                                          << imageData.size() << ")" << std::endl;
                            }
                            continue;
                        }

                        // Extract HOG features
                        mcu::vector<float> features;
                        hog.compute(imageData.data(), features);

                        // Store for later processing
                        CSVRow row;
                        row.class_name = class_name;
                        row.features = std::move(features);
                        csv_data.push_back(std::move(row));
                        
                        images_processed++;
                    }
                }
                
                if (config.processing.verbose) {
                    std::cout << "  Processed " << images_processed << " images for class " << class_name << std::endl;
                }
            }
        }

        if (csv_data.empty()) {
            std::cerr << "Error: No valid images found for processing!" << std::endl;
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
        std::ofstream csv_file(config.output.csv_path, std::ios::trunc);
        if (!csv_file.is_open()) {
            std::cerr << "Error: Cannot open CSV file " << config.output.csv_path << " for writing." << std::endl;
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
        
        if (config.processing.verbose) {
            std::cout << "Processing complete! Results written to " << config.output.csv_path << std::endl;
            if (config.output.shuffle_data) {
                std::cout << "(Data was shuffled)" << std::endl;
            }
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
