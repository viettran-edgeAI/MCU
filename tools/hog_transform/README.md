# HOG Feature Extraction Pipeline

## Overview
This toolkit provides two approaches for extracting HOG (Histogram of Oriented Gradients) features from image datasets:

1. **🚀 Unified Pipeline** (`hog_processor`) - **Recommended**: Single command processes images directly to CSV
2. **🔧 Manual Step-by-Step** - Legacy workflow using separate tools for debugging/customization

---

## Approach 1: Unified Pipeline (Recommended)

### Features
- **Config-driven**: All parameters controlled via JSON configuration
- **Automated pipeline**: Single command processes entire dataset  
- **Multi-format input**: Supports PNG, JPG, JPEG, BMP, TIFF, and TXT (Arduino arrays)
- **Auto-format detection**: Automatically detects and processes mixed file formats
- **Image preprocessing**: Resize, grayscale conversion, normalization using OpenCV
- **Memory optimized**: Designed for embedded systems compatibility
- **Shuffling support**: Randomizes output for ML training
- **Verbose logging**: Detailed progress reporting
- **Class limitation**: Control how many images per class to process

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install libopencv-dev
```

### Quick Start
```bash
# 1. Build the tool
cd /path/to/STL_MCU/tools/hog_transform
make hog_processor

# 2. Run with default settings
make run

# 3. Or run with custom config
make run-custom CONFIG=custom_config.json
```

### Directory Structure Expected
```
your_dataset_folder/
├── class_0/
│   ├── image_001.png
│   ├── image_002.jpg
│   └── ...
├── class_1/
│   ├── image_001.png
│   └── ...
└── class_N/
    └── ...
```

### Configuration
Edit `hog_config.json`:
```json
{
  "input": {
    "dataset_path": "digit_dataset",
    "image_format": "auto"
  },
  "preprocessing": {
    "target_size": {"width": 32, "height": 32},
    "grayscale": true,
    "normalize": true
  },
  "hog_parameters": {
    "img_width": 32, "img_height": 32,
    "cell_size": 8, "block_size": 16,
    "block_stride": 6, "nbins": 4
  },
  "output": {
    "csv_path": "dataset_features.csv",
    "shuffle_data": true
  },
  "processing": {
    "max_images_per_class": -1,
    "verbose": true
  }
}
```

### Output Format
CSV file with format:
```
class_name,feature_1,feature_2,...,feature_N
0,0.123,0.456,...,0.789
1,0.234,0.567,...,0.890
...
```

---

## Approach 2: Manual Step-by-Step Procedure

For debugging, customization, or educational purposes, you can use the original two-step process:

### Step 1: Image Preprocessing
Use **`img_to_progment.ipynb`** to convert images to Arduino-compatible arrays:

**What it does:**
- Loads images from `digit_dataset/` folder (PNG, JPG, etc.)
- Resizes to 32x32 pixels
- Converts to grayscale  
- Saves as C-style uint8_t arrays in .txt files
- Outputs to `digit_array/` folder with class subfolders

**Usage:**
```python
# Run the Jupyter notebook
jupyter notebook img_to_progment.ipynb

# Or convert to Python script and run:
jupyter nbconvert --to script img_to_progment.ipynb
python img_to_progment.py
```

**Input:** `digit_dataset/` (PNG, JPG images in class folders)  
**Output:** `digit_array/` (.txt files with Arduino arrays)

### Step 2: HOG Feature Extraction
Use **`hog_transform.cpp`** tool:

```bash
# Build the legacy tool
make hog_transform

# Run on preprocessed arrays (hardcoded paths in main())
./hog_transform
```

**Input:** `digit_array/` (.txt files)  
**Output:** `digit_data.csv` (HOG features)

### When to Use Manual Procedure:
- **Debugging**: Need to inspect intermediate .txt array files
- **Custom preprocessing**: Want to modify image processing steps in the notebook
- **Educational**: Learning how the pipeline works step-by-step
- **Legacy compatibility**: Working with existing .txt array datasets
- **Memory constraints**: Process very large datasets in smaller chunks

---

## Comparison

| Aspect | Unified Pipeline | Manual Procedure |
|--------|------------------|------------------|
| **Ease of use** | ⭐⭐⭐⭐⭐ Single command | ⭐⭐ Multiple steps |
| **Configuration** | ⭐⭐⭐⭐⭐ JSON config | ⭐⭐ Edit source code |
| **Debugging** | ⭐⭐⭐ Limited visibility | ⭐⭐⭐⭐⭐ Full visibility |
| **Customization** | ⭐⭐⭐ Config parameters | ⭐⭐⭐⭐⭐ Full code access |
| **Performance** | ⭐⭐⭐⭐⭐ Optimized C++ | ⭐⭐⭐ Python + C++ |
| **Maintenance** | ⭐⭐⭐⭐⭐ Single tool | ⭐⭐ Two separate tools |

---

## Examples

### Quick Test (3 images per class)
```bash
make test
```

### Process Full Dataset
```bash
# Edit hog_config.json to set max_images_per_class: -1
make run
```

### Custom Configuration
```bash
# Create custom_config.json with your settings
make run-custom CONFIG=custom_config.json
```

---

## HOG Feature Calculation
For the default config (32x32 image, 8px cells, 16px blocks, 6px stride, 4 bins):
- Blocks per row: (32-16)/6 + 1 = 3
- Blocks per column: (32-16)/6 + 1 = 3  
- Total blocks: 3 × 3 = 9
- Features per block: 4 bins × 4 cells = 16
- **Total features: 9 × 16 = 144**

---

## Troubleshooting

### Build Issues
```bash
# Install OpenCV if missing
sudo apt-get install libopencv-dev

# Check OpenCV installation
pkg-config --cflags --libs opencv4
```

### Common Issues
1. **Dataset path not found**: Check `dataset_path` in config
2. **No images found**: Ensure image files have supported extensions
3. **OpenCV errors**: Verify OpenCV4 is installed
4. **Invalid image size**: Check that images can be resized to target dimensions

### Debug Tips
- Set `verbose: true` for detailed progress information
- Use `make test` for quick functionality verification
- Check intermediate files when using manual procedure
- Verify image files are readable and in correct format

---

## Migration Guide

**From Manual Workflow:**
```bash
# Old way (2 steps)
jupyter notebook img_to_progment.ipynb  # Convert images
./hog_transform                         # Extract features

# New way (1 step)
./hog_processor hog_config.json        # Direct processing
```

**Benefits of Migration:**
- ✅ Eliminates manual Jupyter notebook step
- ✅ Unified configuration via JSON
- ✅ Better error handling and logging  
- ✅ Support for multiple image formats
- ✅ Automated format detection
- ✅ Production-ready pipeline
