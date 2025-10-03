#include "hog_transform.h"

// Default constructor with optimal parameters for 32x32 images
HOG_MCU::HOG_MCU() : processed_image_buffer(nullptr) {
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
HOG_MCU::HOG_MCU(const Params& p) : params(p), processed_image_buffer(nullptr) {
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
    
    if (!cameraBuffer || !processed_image_buffer) {
        return;
    }
    
    // Process the camera buffer (format conversion + resizing)
    if (ImageProcessing::processImage(cameraBuffer, img_config, processed_image_buffer)) {
        compute(processed_image_buffer);
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
        compute(grayscaleImage);
    } else {
        // Need to resize the grayscale image
        if (processed_image_buffer) {
            if (ImageProcessing::resizeBilinear(grayscaleImage, 
                                              img_config.input_width, img_config.input_height,
                                              processed_image_buffer, 
                                              params.img_width, params.img_height)) {
                compute(processed_image_buffer);
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
    img_config.maintain_aspect_ratio = false;
    
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
    }
}

void HOG_MCU::cleanupBuffers() {
    if (processed_image_buffer) {
        delete[] processed_image_buffer;
        processed_image_buffer = nullptr;
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

