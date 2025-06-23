#pragma once

#include <Arduino.h>
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

    HOGDescriptorMCU(const Params& p);
    void compute(const uint8_t* grayImage, mcu::vector<float>& outVec);

private:
    Params params;
    float computeGradientMagnitude(int gx, int gy);
    float computeGradientAngle(int gx, int gy);

    // Calculate optimal HOG parameters for given image size and desired features
    HOGDescriptorMCU::Params calculateOptimalHOGParams(uint8_t image_size, uint8_t desired_features);
};

/*
 * processImageToCSV
 * -----------------
 * Compute HOG features for a grayscale image and (optionally) append the result to a CSV file.
 * 
 * - imageData: pointer to the grayscale image array (uint8_t, size image_size*image_size)
 * - image_size: width/height of the square image (e.g., 32 for 32x32)
 * - desired_features: desired number of output features (the function will select the closest valid configuration)
 * - inputFileName: filename string (e.g., "digit_0_0.txt"), used to extract the label automatically
 * - csvFilePath: (optional) path to the CSV file in SPIFFS; if empty, no file output is performed
 * 
 * Returns: mcu::vector<float> containing the computed HOG features.
 * 
 * Notes:
 * - The label is automatically extracted from the filename (e.g., "digit_0_0.txt" -> label=0).
 * - The function is optimized for embedded systems: no std::string, minimal heap usage.
 * - If csvFilePath is provided and not empty, the label and features are appended as a new row.
 * - The actual number of features may differ slightly from desired_features, depending on valid HOG configurations.
 * - The function is suitable for batch or single-image processing.
 */
mcu::vector<float> processImageToCSV(const uint8_t* imageData, uint8_t image_size, uint8_t desired_features, 
                      const char* inputFileName, const char* csvFilePath = "");

// Original interface - returns features
mcu::vector<float> processImageToCSV(const uint8_t* imageData, int imageSize, float label, 
                      HOGDescriptorMCU& hog, const char* csvFilePath);

