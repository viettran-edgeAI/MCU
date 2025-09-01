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
- C++17 compiler (required for STL features and constexpr functions)
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
| `split_ratio` | object | see below | Dataset splitting ratios for train/test/validation |
| `use_bootstrap` | boolean | true | Enable bootstrap sampling (disable to save 38% RAM/SPIFFS) |
| `criterion` | string | "entropy" | Node splitting criterion: `"gini"` or `"entropy"` |
| `impurity_threshold` | float | 0.1 | Threshold for node impurity (rarely needs adjustment) |
| `data_path` | string | "../data_processing/data/result/digit_data_nml.csv" | Path to normalized dataset |

##### Split Ratio Configuration

The `split_ratio` parameter controls how the dataset is divided:

```json
"split_ratio": {
    "train_ratio": 0.7,
    "test_ratio": 0.15,
    "valid_ratio": 0.15,
    "description": "Ratios for splitting the dataset into training, testing, and validation sets."
}
```

- **`train_ratio`**: Proportion of data for training (default: 0.7)
- **`test_ratio`**: Proportion of data for testing (default: 0.15)
- **`valid_ratio`**: Proportion of data for validation (default: 0.15)

> **Note:** When `use_validation` is false, only `train_ratio` and `test_ratio` are used. The ratios should sum to 1.0 for optimal data utilization.

#### B. Evaluation Strategy

| Parameter | Type | Default | Options | Description |
|-----------|------|---------|---------|-------------|
| `training_score` | string | "oob_score" | ["oob_score", "valid_score", "k-fold_score"] | Method for evaluating model performance during training |
| `k_folds` | integer | 4 | | Number of folds for k-fold cross-validation |

##### Training Score Methods:

1. **`"oob_score"`** (default):
   - Uses Out-of-Bag validation with bootstrap sampling
   - No separate validation set required
   - Memory efficient, good for small datasets
   - Uses only `train_ratio` and `test_ratio` from split configuration

2. **`"valid_score"`**:
   - Uses separate validation set for evaluation
   - More reliable for larger datasets
   - Requires all three ratios: `train_ratio`, `test_ratio`, `valid_ratio`
   - Automatically falls back to `oob_score` if validation set too small

3. **`"k-fold_score"`**:
   - Uses k-fold cross validation
   - Most robust evaluation method
   - Slower training but better generalization estimates
   - Ideal for final model validation
   - Uses only training data (ignores validation split)

> **Recommendation:** Use `oob_score` for quick training, `valid_score` for large datasets, `k-fold_score` for small datasets.

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
    "split_ratio": {
        "train_ratio": 0.8,
        "test_ratio": 0.2,
        "valid_ratio": 0.0
    },
    "training_score": {"value": "k-fold_score"},
    "k_folds": {"value": 5},
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
    "split_ratio": {
        "train_ratio": 0.7,
        "test_ratio": 0.15,
        "valid_ratio": 0.15
    },
    "training_score": {"value": "valid_score"},
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
    "split_ratio": {
        "train_ratio": 0.75,
        "test_ratio": 0.25,
        "valid_ratio": 0.0
    },
    "use_bootstrap": {"value": false},
    "training_score": {"value": "oob_score"},
    "max_depth": {
        "value": 8,
        "status": "enabled"
    }
}
```

## Performance Tuning

### Data Splitting Strategy
- **Training Ratio**: Higher values (0.7-0.8) for small datasets, moderate (0.6-0.7) for large datasets
- **Test Ratio**: 0.15-0.25 for reliable performance estimates
- **Validation Ratio**: 0.1-0.2 when using validation, 0.0 when using cross-validation only
- **Balance Check**: Ensure ratios sum to 1.0 for complete data utilization

### Memory Optimization
- **Reduce `num_trees`**: Fewer trees = less memory, but may reduce accuracy
- **Disable `use_bootstrap`**: Saves 38% RAM and SPIFFS storage
- **Enable `max_depth` override**: Limit tree depth to control memory usage
- **Use `oob_score`**: Most memory-efficient evaluation method

### Accuracy Optimization
- **Increase `num_trees`**: More trees generally improve accuracy (up to a point)
- **Choose appropriate `training_score`**: 
  - `oob_score` for quick training
  - `valid_score` for large datasets
  - `k-fold_score` for robust evaluation
- **Tune `train_flag`**: Match optimization target to your use case
- **Balanced split ratios**: Use 70/15/15 for validation or 75/25/0 for OOB evaluation

### Training Speed
- **Use `oob_score`**: Fastest evaluation method
- **Disable parameter ranges**: Enable overrides for `min_split` and `max_depth` to skip hyperparameter search
- **Reduce `k_folds`**: Fewer folds = faster k-fold cross-validation
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
- **Out-of-Bag (OOB)**: Uses bootstrap samples for unbiased evaluation (`training_score: "oob_score"`)
- **Validation Set**: Hold-out evaluation with configurable ratio (`training_score: "valid_score"`)
- **K-fold Cross-Validation**: Robust evaluation for small datasets (`training_score: "k-fold_score"`)

### Hyperparameter Optimization
- **Grid Search**: Systematic exploration of parameter combinations
- **Automatic Range Detection**: Dataset-driven parameter range selection
- **Override System**: Manual control when needed

## Recent Improvements

### v2024.09.2 Updates (Current)

#### Major Configuration Overhaul
- **Unified Training Score System**: Replaced `use_validation`, `cross_validation`, and `combine_ratio` with single `training_score` parameter
- **Simplified Evaluation**: Three clear options: `"oob_score"`, `"valid_score"`, `"k-fold_score"`
- **Cleaner Configuration**: Removed complex ratio combining logic for better user experience
- **Automatic Fallback**: `valid_score` automatically switches to `oob_score` when validation set too small

### Migration Guide from v2024.09.1

If you're upgrading from the previous version, update your `model_config.json`:

**Old Configuration (v2024.09.1):**
```json
{
    "use_validation": {"value": true},
    "cross_validation": {"value": false},
    "combine_ratio": {"value": 0.7, "status": "disabled"}
}
```

**New Configuration (v2024.09.2):**
```json
{
    "training_score": {"value": "valid_score"}
}
```

**Migration Rules:**
- `cross_validation: true` → `training_score: "k-fold_score"`
- `use_validation: true, cross_validation: false` → `training_score: "valid_score"`
- `use_validation: false, cross_validation: false` → `training_score: "oob_score"`
- Remove `combine_ratio` parameter (no longer needed)

### v2024.09.1 Updates

#### Bug Fixes
- **Critical Tree Building Fix**: Fixed feature indexing bug in `build_tree()` partitioning logic that was causing poor model accuracy
- **Cross-Validation Memory Management**: Resolved segmentation faults in `get_cross_validation_score()` due to improper object lifecycle management
- **Configuration Parsing**: Enhanced JSON parsing robustness for nested configuration objects

#### New Features
- **Flexible Split Ratios**: Added `split_ratio` configuration with independent `train_ratio`, `test_ratio`, and `valid_ratio` controls
- **Enhanced Debug Output**: Added configuration display showing parsed split ratios during initialization
- **Improved Memory Safety**: Better handling of MCU container objects and reduced memory corruption risks

#### Performance Improvements
- **Cross-Validation Implementation**: Complete implementation of k-fold cross-validation with proper index-based data management
- **Optimized Data Splitting**: Direct ratio-based splitting instead of remainder calculations
- **Better Tree Statistics**: Enhanced forest analysis with comprehensive node and depth statistics

> **Important:** These updates require C++17 compiler. Models trained with this version show significantly improved accuracy compared to previous versions due to the tree building bug fix.

## Troubleshooting

### Common Issues

1. **Compilation Errors:**
   ```bash
   # Ensure correct include path and C++17 standard
   g++ -std=c++17 -I../../src -o pre_train random_forest_pc.cpp
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
   - Ensure split ratios are appropriate for your dataset size

5. **Legacy Issues (Fixed in v2024.09):**
   - **Tree Building Bug**: Fixed critical feature indexing issue that caused poor accuracy
   - **Cross-Validation Crashes**: Resolved memory management issues in k-fold validation
   - **Configuration Parsing**: Enhanced robustness for nested JSON objects like `split_ratio`

### Debug Output

The training process provides detailed logging:
- **Configuration Summary**: Displays all parsed parameters including split ratios
- **Dataset Analysis**: Class distribution and balance analysis
- **Automatic Parameter Detection**: Shows reasoning behind parameter selection
- **Training Progress**: Cross-validation scores with progress indicators
- **Final Model Statistics**: Tree count, nodes, depth, and memory usage
- **Performance Metrics**: Precision, recall, F1-score, and accuracy for each class


For complete integration examples, see the main STL_MCU documentation.

---

**Need help?** Check the main STL_MCU repository for examples and additional documentation.
    