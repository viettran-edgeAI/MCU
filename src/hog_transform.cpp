#include "hog_transform.h"
#include <FS.h>
#include <SPIFFS.h>

HOGDescriptorMCU::HOGDescriptorMCU(const Params& p) : params(p) {}HOGDescriptorMCU::Params calculateOptimalHOGParams(uint8_t image_size, uint8_t desired_features);

void HOGDescriptorMCU::compute(const uint8_t* grayImage, mcu::vector<float>& outVec) {
    int numBlocksY = (params.img_height - params.block_size) / params.block_stride + 1;
    int numBlocksX = (params.img_width - params.block_size) / params.block_stride + 1;

    outVec.reserve(numBlocksX * numBlocksY * (4 * params.nbins));

    for (int by = 0; by < numBlocksY; ++by) {
        for (int bx = 0; bx < numBlocksX; ++bx) {
            mcu::vector<float> blockHist(params.nbins * 4, 0.0f);

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
            norm = sqrt(norm + 1e-6f);
            for (float& v : blockHist) v /= norm;

            outVec.insert(outVec.end(), blockHist.begin(), blockHist.end());
        }
    }
}

float HOGDescriptorMCU::computeGradientMagnitude(int gx, int gy) {
    return sqrt(gx * gx + gy * gy);
}

float HOGDescriptorMCU::computeGradientAngle(int gx, int gy) {
    return atan2(gy, gx) * 180.0f / PI;
}

// Helper: extract label from filename (expects format: ..._<label>_<sample>.txt)
static float extractLabelFromFilename(const char* filename) {
    // Find last '/' or '\\'
    const char* base = filename;
    for (const char* p = filename; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    // Find first '_' after dataset name
    const char* first_ = strchr(base, '_');
    if (!first_) return -1;
    const char* second_ = strchr(first_ + 1, '_');
    if (!second_) return -1;
    // Extract label substring
    int label = 0;
    for (const char* p = first_ + 1; p < second_; ++p) {
        if (*p < '0' || *p > '9') return -1;
        label = label * 10 + (*p - '0');
    }
    return (float)label;
}

mcu::vector<float> processImageToCSV(const uint8_t* imageData, int imageSize, float label, 
                      HOGDescriptorMCU& hog, const char* csvFilePath) {
    mcu::vector<float> features;

    // Compute HOG features
    hog.compute(imageData, features);

    // Append to CSV file if path is provided
    if (csvFilePath && strlen(csvFilePath) > 0) {
        File file = SPIFFS.open(csvFilePath, FILE_APPEND);
        if (!file) {
            Serial.println("Error: Cannot open CSV file for writing");
            return features;
        }

        // Write label first, then features
        file.print(label, 1);
        for (size_t i = 0; i < features.size(); ++i) {
            file.print(",");
            file.print(features[i], 6);
        }
        file.println();
        file.close();
        Serial.println(" features written to CSV");
    }

    Serial.print("Image processed: ");
    Serial.print(features.size());

    return features;
}

HOGDescriptorMCU::Params calculateOptimalHOGParams(uint8_t image_size, uint8_t desired_features) {
    HOGDescriptorMCU::Params params;
    params.img_width = image_size;
    params.img_height = image_size;
    
    // Pre-calculated optimal configurations for common cases
    // Format: {cell_size, block_stride, nbins} -> approximate features
    
    if (image_size == 32) {
        if (desired_features <= 36) {
            // 32x32: 6 features per block, 6 blocks -> 36 features
            params.cell_size = 16;
            params.block_stride = 16;
            params.nbins = 6;
        } else if (desired_features <= 96) {
            // 32x32: 24 features per block, 4 blocks -> 96 features  
            params.cell_size = 8;
            params.block_stride = 16;
            params.nbins = 6;
        } else {
            // 32x32: 24 features per block, 9 blocks -> 216 features
            params.cell_size = 8;
            params.block_stride = 8;
            params.nbins = 6;
        }
    } else if (image_size == 48) {
        if (desired_features <= 64) {
            // 48x48: 16 features per block, 4 blocks -> 64 features
            params.cell_size = 12;
            params.block_stride = 24;
            params.nbins = 4;
        } else if (desired_features <= 144) {
            // 48x48: 16 features per block, 9 blocks -> 144 features
            params.cell_size = 8;
            params.block_stride = 16;
            params.nbins = 4;
        } else {
            // 48x48: 24 features per block, 16 blocks -> 384 features
            params.cell_size = 6;
            params.block_stride = 12;
            params.nbins = 6;
        }
    } else if (image_size == 64) {
        if (desired_features <= 96) {
            // 64x64: 24 features per block, 4 blocks -> 96 features
            params.cell_size = 16;
            params.block_stride = 32;
            params.nbins = 6;
        } else if (desired_features <= 384) {
            // 64x64: 24 features per block, 16 blocks -> 384 features
            params.cell_size = 8;
            params.block_stride = 16;
            params.nbins = 6;
        } else {
            // 64x64: 24 features per block, 49 blocks -> 1176 features
            params.cell_size = 8;
            params.block_stride = 8;
            params.nbins = 6;
        }
    } else {
        // Default fallback configuration
        params.cell_size = image_size / 4;
        params.block_stride = image_size / 2;
        params.nbins = 6;
    }
    
    params.block_size = params.cell_size * 2; // Always 2x2 cells per block
    return params;
}


// Simplified overloaded function
mcu::vector<float> processImageToCSV(const uint8_t* imageData, uint8_t image_size, uint8_t desired_features, 
                      const char* inputFileName, const char* csvFilePath) {
    mcu::vector<float> features;
    int imageSize = image_size * image_size;

    // Extract label from filename
    float label = extractLabelFromFilename(inputFileName);

    // Calculate optimal HOG parameters
    HOGDescriptorMCU::Params params = calculateOptimalHOGParams(image_size, desired_features);

    // Create HOG descriptor with calculated parameters
    HOGDescriptorMCU hog(params);

    // Compute HOG features
    hog.compute(imageData, features);

    // Append to CSV file
    if (csvFilePath && strlen(csvFilePath) > 0) {
        File file = SPIFFS.open(csvFilePath, FILE_APPEND);
        if (!file) {
            Serial.println("Error: Cannot open CSV file for writing");
            return features;
        }

        // Write label first, then features
        file.print(label, 1);
        for (float f : features) {
            file.print(",");
            file.print(f, 6);
        }
        file.println();
        file.close();
        Serial.println(" features written to CSV");
    }

    Serial.print("Image processed: ");
    Serial.print(features.size());

    return features;
}

