#include <iostream>
#include <filesystem>
// #include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <random>
#include <algorithm>
#include "STL_MCU.h"

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
    
        void compute(const uint8_t* grayImage, mcu::vector<float>& outVec){
            int numBlocksY = (params.img_height - params.block_size) / params.block_stride + 1;
            int numBlocksX = (params.img_width - params.block_size) / params.block_stride + 1; // Initialize numBlocksX
        
            // outVec.clear();
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
        
                                    float mag = computeGradientMagnitude(gx, gy);
                                    float angle = computeGradientAngle(gx, gy);
                                    if (angle < 0) angle += 180.0f;
        
                                    int bin = (int)(angle / (180.0f / params.nbins));
                                    if (bin >= params.nbins) bin = params.nbins - 1;
        
                                    hist[bin] += mag;
                                }
                            }
        
                            for (int i = 0; i < params.nbins; ++i)
                                blockHist[(cy * 2 + cx) * params.nbins + i] = hist[i];
                        }
                    }
        
                    float norm = 0.0f;
                    for (float v : blockHist) norm += v * v;
                    norm = std::sqrt(norm + 1e-6f);
                    for (float& v : blockHist) v /= norm;
        
                    outVec.insert(outVec.end(), blockHist.begin(), blockHist.end());
                }
            }
        }
    
    private:
        Params params;
    
        float computeGradientMagnitude(int gx, int gy) {
            return std::sqrt(gx * gx + gy * gy);
        }
    
        float computeGradientAngle(int gx, int gy) {
            return std::atan2(gy, gx) * 180.0f / M_PI;
        }
    };

namespace fs = std::filesystem;

mcu::vector<uint8_t> parse_txt_image_file(const std::string& path) {
    std::ifstream file(path);
    mcu::vector<uint8_t> data;
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
                // Skip lines until the opening brace is found
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
                    data.push_back(static_cast<uint8_t>(std::stoi(match[1].str())));
                } catch (const std::out_of_range& oor) {
                    std::cerr << "Warning: Number out of range in " << path << " for value '" << match[1].str() << "'. Skipping." << std::endl;
                } catch (const std::invalid_argument& ia) {
                    std::cerr << "Warning: Invalid number in " << path << " for value '" << match[1].str() << "'. Skipping." << std::endl;
                }
            }
            search_begin = match.suffix().first;
            // Basic protection against empty matches causing infinite loops with some regex engines/patterns
            if (search_begin == search_end && match.empty()) break; 
            if (!match.empty() && match[0].length() == 0 && search_begin != search_end) {
                 // If an empty match occurred and we are not at the end, advance to avoid infinite loop.
                 // This case should ideally not happen with \d+ but is a safeguard.
                 ++search_begin;
            }
        }

        if (array_data_started && line.find('}') != std::string::npos) {
            // Stop processing after the line containing the closing brace
            break;
        }
    }

    if (data.size() != 28 * 28) {
        std::cerr << "Warning: Image " << path << " has " << data.size() << " values (expected " << 28 * 28 << ")\n";
    }
    return data;
}

int main() {
    std::string root_folder_path = "digit_array";
    std::string csv_output_path = "digit_data.csv";

    // // input : 64*64 image, output: 1176 features
    // HOGDescriptorMCU::Params params = {
    //     64, 64, // img_width, img_height
    //     8,      // cell_size
    //     16,     // block_size (2x2 cells)
    //     8,      // block_stride
    //     6       // nbins
    // };

    // // input : 48*48 image, output: 256 features
    // HOGDescriptorMCU::Params params = {
    //     48, // img_width
    //     48, // img_height
    //     6,  // cell_size
    //     12, // block_size (pixel dimension: 2 * cell_size, for a 2x2 cell block)
    //     12, // block_stride
    //     4   // nbins
    // };

    // input : 32*32 image, output: 144 features
    HOGDescriptorMCU::Params params = {
        32, 32,     // Image size
        8,          // Cell size
        16,          // Block stride
        6,          // Bins per cell
        4           // Block size (2x2 cells)
    };
    
    HOGDescriptorMCU hog(params);

    // Structure to hold CSV rows before shuffling
    struct CSVRow {
        std::string class_name;
        mcu::vector<float> features;
    };
    
    mcu::vector<CSVRow> csv_data;

    // Iterate through subfolders in the root directory
    for (const auto& subfolder_entry : fs::directory_iterator(root_folder_path)) {
        if (subfolder_entry.is_directory()) {
            std::filesystem::path subfolder_path = subfolder_entry.path();
            std::string class_name = subfolder_path.filename().string(); // Use subfolder name directly as class name

            std::cout << "Processing subfolder: " << subfolder_path << " with class name '" << class_name << "'...\n";

            // Iterate through .txt files in the current subfolder
            for (const auto& file_entry : fs::directory_iterator(subfolder_path)) {
                if (file_entry.path().extension() == ".txt") {
                    std::cout << "  Processing file " << file_entry.path() << "...\n";

                    mcu::vector<uint8_t> imageData = parse_txt_image_file(file_entry.path().string());

                    if (imageData.size() != static_cast<size_t>(params.img_width * params.img_height)) {
                        std::cerr << "  Skipping invalid image: " << file_entry.path() 
                                  << " (expected " << params.img_width * params.img_height << " pixels, got " << imageData.size() << ")\n";
                        continue;
                    }

                    mcu::vector<float> features_for_current_image;
                    // The HOG compute function will fill features_for_current_image
                    hog.compute(imageData.data(), features_for_current_image);

                    // Store the data for later shuffling
                    CSVRow row;
                    row.class_name = class_name;
                    row.features = std::move(features_for_current_image);
                    csv_data.push_back(std::move(row));
                }
            }
        }
    }

    // Shuffle the CSV data randomly
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(csv_data.begin(), csv_data.end(), gen);

    // Write shuffled data to CSV file
    std::ofstream csv_file;
    csv_file.open(csv_output_path, std::ios::trunc); // Use trunc to overwrite existing file

    if (!csv_file.is_open()) {
        std::cerr << "Error: Cannot open CSV file " << csv_output_path << " for writing." << std::endl;
        return 1;
    }

    std::cout << "Writing " << csv_data.size() << " shuffled rows to CSV file...\n";
    
    for (const auto& row : csv_data) {
        csv_file << row.class_name; // Write class name first
        for (float f : row.features) {
            csv_file << "," << f;
        }
        csv_file << "\n";
    }

    csv_file.close();
    std::cout << "Processing complete. Results written to " << csv_output_path << " (shuffled)" << std::endl;

    return 0;
}
