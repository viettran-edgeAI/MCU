# Random Forest Pre-Training Tool

## Overview

This tool enables pre-training of optimized Random Forest models using normalized datasets (`<data_name>_nml.csv`) from the data processing pipeline. The pre-training process automatically determines optimal hyperparameters and generates highly efficient models specifically designed for ESP32 microcontrollers with memory constraints.

**Key Benefits:**
- Reduces computational workload on ESP32 devices
- Automatically optimizes hyperparameters for best performance
- Generates memory-efficient binary tree files
- Supports multiple evaluation strategies (OOB, validation, cross-validation)
- Provides comprehensive configuration override system

> **Note:** While pre-training is recommended for optimal performance, you can still deploy raw data and run inference directly on ESP32 without this step.

## Quick Start

### Prerequisites
- C++17 compatible compiler
- Normalized dataset from data processing pipeline

### Basic Usage

1. **Navigate to the pre-train directory:**
   ```bash
   # Linux/macOS
   cd /path/to/STL_MCU/tools/pre_train
   
   # Windows
   cd C:\path\to\STL_MCU\tools\pre_train
   ```

2. **Compile the program:**
   ```bash
   g++ -std=c++17 -I../../src -o pre_train random_forest_pc.cpp
   ```

3. **Configure your model:**
   Edit `model_config.json` to set your parameters (see [Configuration Guide](#configuration-guide) below)

4. **Run the training:**
   ```bash
   ./pre_train
   ```

5. **Deploy results:**
   The trained model files in `trained_model/` directory are ready for ESP32 deployment

## Output Files

The training process generates the following files in the `trained_model/` directory:

| File | Description |
|------|-------------|
| `esp32_config.json` | Complete model configuration and metadata |
| `esp32_config.csv` | CSV version of configuration for analysis |
| `tree_0.bin`, `tree_1.bin`, ... | Binary tree files optimized for ESP32 SPIFFS |

## Configuration Guide

### Model Configuration File: `model_config.json`

The configuration system is divided into two main categories:

#### A. Core Model Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `num_trees` | integer | 20 | Number of trees in the forest (recommended: 10-50) |
| `use_bootstrap` | boolean | true | Enable bootstrap sampling (disable to save 38% RAM/SPIFFS) |
| `criterion` | string | "entropy" | Node splitting criterion: `"gini"` or `"entropy"` |
| `impurity_threshold` | float | 0.1 | Threshold for node impurity (rarely needs adjustment) |
| `data_path` | string | "../data_processing/data/result/digit_data_nml.csv" | Path to normalized dataset |

#### B. Evaluation Strategy

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `use_validation` | boolean | false | Use validation set (20% of training data). Avoid with small datasets |
| `cross_validation` | boolean | true | Use k-fold cross-validation instead of OOB evaluation |
| `k_folds` | integer | 4 | Number of folds for cross-validation |

> **Recommendation:** Use cross-validation for small datasets, validation for large datasets.

#### C. Advanced Override System

The override system allows you to control automatic parameter optimization with three status modes:

##### Status Modes:
- **`"disabled"`** - Use automatic parameter optimization (recommended)
- **`"enabled"`** - Override with fixed user value (for min_split, max_depth, unity_threshold, combine_ratio)
- **`"overwrite"`** - Replace automatic values completely (for train_flag)
- **`"stacked"`** - Combine user values with automatic detection (for train_flag)

##### Override Parameters:

| Parameter | Status Options | Description |
|-----------|----------------|-------------|
| `min_split` | disabled, enabled | Minimum samples required to split a node |
| `max_depth` | disabled, enabled | Maximum tree depth |
| `unity_threshold` | disabled, enabled | Consensus threshold for tree decisions |
| `combine_ratio` | disabled, enabled | Ratio for combining OOB and validation scores |
| `train_flag` | disabled, overwrite, stacked | Training optimization flags |

##### Training Flags System

Training flags specify which metrics to optimize during model training:

**Available Flags:**
- `ACCURACY` - Overall classification accuracy
- `PRECISION` - Positive predictive value
- `RECALL` - Sensitivity/true positive rate  
- `F1_SCORE` - Harmonic mean of precision and recall

**Flag Modes:**

1. **Disabled Mode** (`"status": "disabled"`):
   ```json
   "train_flag": {
       "value": "ACCURACY",
       "status": "disabled"
   }
   ```
   - System automatically selects flags based on dataset characteristics
   - Imbalanced datasets → RECALL or F1_SCORE
   - Balanced datasets → ACCURACY

2. **Overwrite Mode** (`"status": "overwrite"`):
   ```json
   "train_flag": {
       "value": "PRECISION",
       "status": "overwrite"
   }
   ```
   - User flags completely replace automatic detection
   - Example: `"PRECISION"` or `"ACCURACY"` for single flag
   - Multiple flags: Not directly supported in JSON value, but can be combined in code

3. **Stacked Mode** (`"status": "stacked"`):
   ```json
   "train_flag": {
       "value": "ACCURACY",
       "status": "stacked"
   }
   ```
   - Combines user flags with automatically detected flags using bitwise OR
   - Example: User specifies `ACCURACY`, system detects imbalance → Result: `ACCURACY | RECALL`

## Example Configurations

### For Small Datasets (< 1000 samples)
```json
{
    "num_trees": {"value": 15},
    "cross_validation": {"value": true},
    "k_folds": {"value": 5},
    "use_validation": {"value": false},
    "train_flag": {
        "value": "F1_SCORE",
        "status": "stacked"
    }
}
```

### For Large Balanced Datasets
```json
{
    "num_trees": {"value": 30},
    "cross_validation": {"value": false},
    "use_validation": {"value": true},
    "train_flag": {
        "value": "ACCURACY",
        "status": "disabled"
    }
}
```

### For Memory-Constrained Deployment
```json
{
    "num_trees": {"value": 10},
    "use_bootstrap": {"value": false},
    "max_depth": {
        "value": 8,
        "status": "enabled"
    }
}
```

## Performance Tuning

### Memory Optimization
- **Reduce `num_trees`**: Fewer trees = less memory, but may reduce accuracy
- **Disable `use_bootstrap`**: Saves 38% RAM and SPIFFS storage
- **Enable `max_depth` override**: Limit tree depth to control memory usage

### Accuracy Optimization
- **Increase `num_trees`**: More trees generally improve accuracy (up to a point)
- **Use appropriate evaluation**: Cross-validation for small datasets, validation for large
- **Tune `train_flag`**: Match optimization target to your use case

### Training Speed
- **Disable parameter ranges**: Enable overrides for `min_split` and `max_depth` to skip hyperparameter search
- **Reduce `k_folds`**: Fewer folds = faster cross-validation
- **Smaller datasets**: Consider data reduction techniques if training is too slow

## Integration with ESP32

The generated model files are designed for seamless integration with ESP32. You have two deployment options:

### Option 1: Manual File Copy
1. Copy `trained_model/` contents to your ESP32 project
2. Load tree files from SPIFFS during initialization
3. Use the STL_MCU inference API for predictions

### Option 2: Automated Serial Transfer

This directory includes specialized transfer tools for uploading pre-trained model files directly to ESP32 via serial connection:

- **PC Side**: `pc_side/transfer_model.py` - Python script for sending model files
- **ESP32 Side**: `esp32_side/model_receiver.ino` - Arduino sketch for receiving files

#### Quick Transfer Usage

1. **Upload receiver to ESP32:**
   ```bash
   # Upload esp32_side/model_receiver.ino to your ESP32
   ```

2. **Run transfer from PC:**
   ```bash
   cd pc_side
   python3 transfer_model.py /dev/ttyUSB0  # Linux/macOS
   python3 transfer_model.py COM3         # Windows
   ```

#### Transfer Features

- **Automatic file discovery** from `trained_model/` directory
- **Combined progress bar** for tree files (single status bar for all trees)
- **File preservation** - maintains original filenames on ESP32
- **Robust error recovery** with retry mechanisms
- **SPIFFS management** with storage information display
- **Selective transfer** - JSON config only (ignores CSV version)

#### Files Transferred

- `esp32_config.json` - Model configuration (JSON format only)
- `tree_0.bin`, `tree_1.bin`, ... `tree_N.bin` - Decision tree binary data

> **Note on Pipeline Integration**: This model transfer tool is part of a modular pipeline system. Each stage (data processing, pre-training, etc.) has its own specialized transfer tools. There will be a unified transfer system in an external folder that handles the complete pipeline data transfer, similar to how `unified_transfer.py` and `unified_receiver.ino` work together for coordinated multi-stage transfers.

## Technical Details

### Architecture
- **Breadth-first tree building**: Optimized memory layout for embedded systems. Each node in the tree takes up only 4 bytes.
- **2-bit quantization**: Reduces memory footprint while maintaining accuracy
- **Binary tree serialization**: Compact storage format for SPIFFS

### Evaluation Methods
- **Out-of-Bag (OOB)**: Uses bootstrap samples for unbiased evaluation
- **Validation Set**: Hold-out evaluation with configurable ratio
- **K-fold Cross-Validation**: Robust evaluation for small datasets

### Hyperparameter Optimization
- **Grid Search**: Systematic exploration of parameter combinations
- **Automatic Range Detection**: Dataset-driven parameter range selection
- **Override System**: Manual control when needed

## Troubleshooting

### Common Issues

1. **Compilation Errors:**
   ```bash
   # Ensure correct include path
   g++ -std=c++17 -I../../src -o random_forest_pc random_forest_pc.cpp
   ```

2. **Dataset Not Found:**
   - Verify `data_path` in `model_config.json`
   - Ensure dataset is properly normalized from previous pipeline step

3. **Memory Issues During Training:**
   - Reduce `num_trees`
   - Enable `max_depth` override with smaller value
   - Disable `use_bootstrap`

4. **Poor Model Performance:**
   - Check dataset balance and adjust `train_flag` accordingly
   - Increase `num_trees` if memory allows
   - Verify data quality and normalization

### Debug Output

The training process provides detailed logging:
- Dataset analysis and class distribution
- Automatic parameter detection reasoning
- Training progress with cross-validation scores
- Final model statistics and memory usage


For complete integration examples, see the main STL_MCU documentation.

---

**Need help?** Check the main STL_MCU repository for examples and additional documentation.
    