# Random Forest Pre-Training Tool

## Overview

This tool allows pre-training Random Forest models from quantized datasets and from user-specified configuration files.

**Key Benefits:**
- Allows users to customize all settings and configurations for the model through the model_config.json file.
- generate ready-to-deploy model files for ESP32.
- Reduces computational workload on ESP32 devices: user can specify best configuration at model_config.json file until reaching satisfactory performance.
- Automatically optimizes hyperparameters for best performance
- Supports multiple evaluation strategies (OOB, validation, cross-validation)
- **Two execution modes**: Fast build-only mode for prototyping, full training mode for optimization

![Pre-training workflow](../../docs/imgs/pre_train_tool.jpg)

> **Note:** While pre-training is recommended for optimal performance, you can still deploy raw data and run inference directly on ESP32 without this step. Use build-only mode for quick prototyping and training mode for final optimization.

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
   # Build model only (fast, no grid search)
   ./pre_train
   
   # Full training with grid search (slower, optimized parameters)
   ./pre_train -training
   ```

5. **Deploy results:**
   The trained model files in `trained_model/` directory are ready for ESP32 deployment

## Execution Modes

The tool supports two execution modes to accommodate different workflow needs:

### Build Model Only Mode (Default)
```bash
./pre_train
```
**When to use:**
- Quick model prototyping and testing
- Large datasets where grid search would be too time-consuming
- Memory-constrained development environments
- When you want to use pre-configured parameters from `model_config.json`

**What it does:**
- Builds forest using first values from parameter ranges (`min_split_range[0]`, `min_leaf_range[0]`, `max_depth_range[0]`)
- Uses config file values if parameters are marked as "enabled" in `model_config.json`
- Calculates node layout and bit requirements for MCU deployment
- Evaluates model on test set and saves complete configuration
- **Fast execution** - skips expensive hyperparameter grid search

### Training Mode (Grid Search)
```bash
./pre_train -training
```
**When to use:**
- Final model optimization for production deployment
- Small to medium datasets where grid search is feasible
- When you want automatic hyperparameter optimization
- Maximum accuracy requirements

**What it does:**
- Performs comprehensive grid search across all parameter combinations
- Finds optimal `min_split`, `min_leaf`, and `max_depth` values
- Uses cross-validation or validation sets for robust evaluation
- Saves best-performing model configuration
- **Slow execution** - can take hours on large datasets

### Command Line Options

| Option | Description |
|--------|-------------|
| `-training`, `--training` | Enable training mode with grid search |
| `-h`, `--help` | Display help message and exit |

**Examples:**
```bash
# Show help
./pre_train --help

# Quick build (default behavior)
./pre_train

# Full training with optimization
./pre_train -training
```

### Decision Tree Mode

When you specify only **one tree** (`num_trees: 1`) in the configuration, the program automatically switches to **Decision Tree Mode**. This mode is optimized for training a single, comprehensive decision tree rather than an ensemble.

**Automatic Adjustments:**
- `use_bootstrap` is automatically set to `false`
- `bootstrap_ratio` is automatically set to `1.0`
- All training samples are used (no sampling)
- All features are considered at each split (no random feature subset)
- If `training_score` is set to `"oob_score"`, it will automatically switch to `"valid_score"` since OOB is not applicable for a single tree

**When to use Decision Tree Mode:**
- **Interpretability**: Single tree is easier to visualize and understand
- **Debugging**: Simpler model for testing algorithms and data pipelines
- **Memory-constrained scenarios**: Single tree requires minimal memory
- **Baseline comparison**: Compare ensemble performance against single tree
- **Fast inference**: Single tree has the lowest prediction latency

**Example Configuration:**
```json
{
    "num_trees": {"value": 1},
    "use_bootstrap": {"value": true},  // Will be overridden to false
    "max_depth": {
        "value": 50,
        "status": "enabled"  // Control tree complexity
    }
}
```

> **Note**: Even if you set `use_bootstrap: true` in the config file, it will be automatically overridden to `false` when `num_trees` is 1. The bootstrap ratio will also be set to 1.0 to ensure all training data is used.

**Trade-offs:**
- ✅ **Pros**: Faster training, simpler model, full dataset utilization, better interpretability
- ⚠️ **Cons**: May overfit on small datasets, less robust than ensemble, lower accuracy on complex problems

## Configuration Guide

### Model Configuration File: `model_config.json`

The configuration system is divided into two main categories:

#### A. Core Model Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `num_trees` | integer | 20 | Number of trees in the forest (recommended: 10-50) |
| `split_ratio` | object | see below | Dataset splitting ratios for train/test/validation |
| `use_bootstrap` | boolean | true | Enable bootstrap sampling |
| `criterion` | string | "entropy" | Node splitting criterion: `"gini"` or `"entropy"` |
| `impurity_threshold` | float | 0.1 | Threshold for node impurity (rarely needs adjustment) |
| `data_path` | string | "../data_quantization/data/result/digit_data_nml.csv" | Path to normalized dataset |
| `max_samples` | integer | 0 | Maximum number of samples to keep in dataset. When exceeded, oldest samples are removed (FIFO). Set to 0 for unlimited samples. Useful for online learning on memory-constrained devices |

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

> **Note:** Normally the program will automatically generate the appropriate ratio based on the dataset, u don't need to edit it unless you want to specify custom ratios.

> **Note:** When `use_validation` is false, only `train_ratio` and `test_ratio` are used. The ratios should sum to 1.0 for optimal data utilization. Anyway the program will automatically adjust the ratio to make sure they sum to 1.

##### Max Samples Configuration

The `max_samples` parameter controls memory usage for online learning scenarios:

```json
"max_samples": {
    "value": 0,
    "description": "Maximum number of samples to keep in the dataset..."
}
```

- **Value `0`** (default): Unlimited samples - dataset can grow indefinitely
- **Value `> 0`**: Dataset size limit - when exceeded, oldest samples are automatically removed (FIFO)

**Use Cases:**
- **ESP32 Online Learning**: Set to prevent memory overflow as dataset grows from user feedback
- **Adaptive Models**: Maintain recent data while discarding outdated patterns

**Behavior:**
- When `addNewData()` is called and dataset size exceeds `max_samples`:
  - Oldest samples are removed first (FIFO queue behavior)
  - New samples are added to maintain the specified limit
  - Node layout is recalculated based on current dataset size
  - Training continues with the updated dataset


#### B. Evaluation Strategy

Training Score Methods:

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
- **`"enabled"`** - Override with fixed user value (for min_split, max_depth)
- **`"overwrite"`** - Replace automatic values completely (for metric_score)
- **`"stacked"`** - Combine user values with automatic detection (for metric_score)

##### Override Parameters:

| Parameter | Status Options | Description |
|-----------|----------------|-------------|
| `min_split` | disabled, enabled | Minimum samples required to split a node |
| `max_depth` | disabled, enabled | Maximum tree depth |
| `metric_score` | disabled, overwrite, stacked | Training optimization flags |

##### Metric Score Configuration

The `metric_score` parameter allows you to specify which performance metrics to optimize during training. It supports multiple flags and three status modes.

>**Note:** Normally, the system automatically selects the best metric based on dataset characteristics and u no need to edit this unless u have specific requirements.

**Available Flags:**
- `ACCURACY` - Overall classification accuracy
- `PRECISION` - Positive predictive value
- `RECALL` - Sensitivity/true positive rate  
- `F1_SCORE` - Harmonic mean of precision and recall

**Flag Modes:**

1. **Disabled Mode** (`"status": "disabled"`):
   ```json
   "metric_score": {
       "value": "ACCURACY",
       "status": "disabled"
   }
   ```
   - System automatically selects flags based on dataset characteristics
   - Imbalanced datasets → RECALL or F1_SCORE
   - Balanced datasets → ACCURACY

2. **Overwrite Mode** (`"status": "overwrite"`):
   ```json
   "metric_score": {
       "value": "PRECISION",
       "status": "overwrite"
   }
   ```
   - User flags completely replace automatic detection
   - Example: `"PRECISION"` or `"ACCURACY"` for single flag
   - Multiple flags: Not directly supported in JSON value, but can be combined in code

3. **Stacked Mode** (`"status": "stacked"`):
   ```json
   "metric_score": {
       "value": "ACCURACY",
       "status": "stacked"
   }
   ```
   - Combines user flags with automatically detected flags using bitwise OR
   - Example: User specifies `ACCURACY`, system detects imbalance → Result: `ACCURACY | RECALL`

##### Min Split Configuration

The `min_split` parameter controls the minimum number of samples required to split a node during tree building. This affects tree complexity and model generalization.

```json
"min_split": {
    "value": 4,
    "status": "disabled"
}
```

**Status Modes:**

1. **Disabled Mode** (`"status": "disabled"`):
   - System automatically determines optimal `min_split` values through grid search (training mode)
   - Default behavior - recommended for most use cases
   - During build-only mode, uses first value from automatic range

2. **Enabled Mode** (`"status": "enabled"`):
   ```json
   "min_split": {
       "value": 4,
       "status": "enabled"
   }
   ```
   - Overrides automatic optimization with fixed user value
   - Useful when you want to enforce memory or performance constraints
   - In build-only mode: uses specified value without grid search
   - In training mode: skips grid search for `min_split`, uses this value directly

**Recommendations:**
- **Small datasets (< 1000 samples)**: Use `2-4` for better generalization
- **Large datasets (> 10000 samples)**: Use `4-8` to reduce overfitting
- **Memory-constrained devices**: Increase value to reduce tree complexity
- **High accuracy needed**: Decrease value to allow more complex trees (if memory allows)

##### Min Leaf Configuration

The `min_leaf` parameter controls the minimum number of samples required at a leaf node. This is a regularization technique to prevent overfitting.

```json
"min_leaf": {
    "value": 2,
    "status": "disabled"
}
```

**Status Modes:**

1. **Disabled Mode** (`"status": "disabled"`):
   - System automatically determines optimal `min_leaf` values through grid search (training mode)
   - Default behavior - recommended for most use cases
   - During build-only mode, uses first value from automatic range

2. **Enabled Mode** (`"status": "enabled"`):
   ```json
   "min_leaf": {
       "value": 2,
       "status": "enabled"
   }
   ```
   - Overrides automatic optimization with fixed user value
   - Useful for controlling overfitting or enforcing minimum leaf sizes
   - In build-only mode: uses specified value without grid search
   - In training mode: skips grid search for `min_leaf`, uses this value directly

**Recommendations:**
- **Small datasets**: Use `2-4` to avoid too many leaves
- **Large datasets**: Use `1-2` for better model complexity
- **Imbalanced datasets**: Increase value to ensure sufficient minority class samples in leaves
- **High variance models**: Increase value to regularize
- **Underfitting**: Decrease value to allow more complex leaves

##### Max Depth Configuration

The `max_depth` parameter controls the maximum depth of trees in the forest. This is critical for embedded systems with memory constraints.

```json
"max_depth": {
    "value": 50,
    "status": "enabled"
}
```

**Status Modes:**

1. **Disabled Mode** (`"status": "disabled"`):
   - System automatically determines optimal `max_depth` values through grid search (training mode)
   - Default behavior - recommended when memory is not a constraint
   - During build-only mode, uses first value from automatic range

2. **Enabled Mode** (`"status": "enabled"`):
   ```json
   "max_depth": {
       "value": 20,
       "status": "enabled"
   }
   ```
   - Overrides automatic optimization with fixed user value
   - Essential for memory-constrained embedded systems
   - In build-only mode: uses specified value without grid search
   - In training mode: skips grid search for `max_depth`, uses this value directly
   - Controls maximum number of levels in decision trees

**Recommendations:**
- **small, medium datasets:** set to `disabled` for best accuracy
- **Large datasets (> 10000 samples)**: set to `enabled` with 50 , allow trees to grow to their maximum depth. shorten training time


**Modes:**

1. **Enabled** (`true`):
   - Allows the model to accept and incorporate new data samples collected during inference and expand the dataset size
   - Necessary for adaptive models that improve over time
   - Works in conjunction with `max_samples` to control the dataset size

2. **Disabled** (`false`):
   - allows new data to be accepted, but the dataset size remains constant.
   - Old samples are replaced by new ones when the limit is reached (FIFO behavior)
   - usually combined with `enable_auto_config` = false.

##### Enable Retrain Configuration

The `enable_retrain` parameter controls whether the model can be retrained on-device with newly collected data on ESP32.

```json
"enable_retrain": {
    "value": true,
    "options": [true, false]
}
```

**Modes:**

1. **Enabled** (`true`):
   - Allows the model to be retrained on the ESP32 using newly collected data

2. **Disabled** (`false`):
   - Model remains static after embedding on ESP32
   - No retraining occurs, even if new data is collected
   - infer faster on ESP32.
   - flags `enable_auto_config` have no effect when retraining is disabled.

##### Enable Auto Config Configuration

The `enable_auto_config` parameter controls whether the model automatically reconfigures itself based on newly observed data during online learning on ESP32.

```json
"enable_auto_config": {
    "value": false,
    "options": [true, false]
}
```

**Modes:**

1. **Disabled** (`false` - default):
   - Uses configuration from pre-trained `model_config.json` without modification
   - Model parameters (`num_trees`, `max_depth`, etc.) remain fixed
   - Retraining uses original hyperparameters

2. **Enabled** (`true`):
   - On ESP32, each session of building a model based on a new dataset will compute optimal parameters automatically.
   - Recommended if you feel your current dataset is not big enough and can grow much bigger in the future on esp32.
   - `enable_retrain` must be `true` for this to have effect.

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
    "metric_score": {
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
    "metric_score": {
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

### For Online Learning on ESP32
```json
{
    "num_trees": {"value": 15},
    "split_ratio": {
        "train_ratio": 0.8,
        "test_ratio": 0.2,
        "valid_ratio": 0.0
    },
    "max_samples": {"value": 5000},
    "enable_retrain": {"value": true},
    "training_score": {"value": "oob_score"}
}
```

> **Note:** `max_samples` is crucial for online learning scenarios where the dataset grows over time through user feedback. When the limit is reached, the oldest samples are automatically removed (FIFO queue), ensuring memory usage stays within ESP32 constraints while maintaining recent relevant data.

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
- **Set `max_samples`**: Limit dataset size for online learning scenarios (prevents unbounded growth)
- **Use `oob_score`**: Most memory-efficient evaluation method

### Accuracy Optimization
- **Increase `num_trees`**: More trees generally improve accuracy (up to a point)
- **Choose appropriate `training_score`**: 
  - `oob_score` for quick training
  - `valid_score` for large datasets
  - `k-fold_score` for robust evaluation
- **Tune `metric_score`**: Match optimization target to your use case
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
   - **For large datasets**: Use build-only mode (`./pre_train`) instead of training mode

4. **Poor Model Performance:**
   - Check dataset balance and adjust `metric_score` accordingly
   - Increase `num_trees` if memory allows
   - Verify data quality and normalization
   - Ensure split ratios are appropriate for your dataset size
   - **For optimization**: Use training mode (`./pre_train -training`) for automatic hyperparameter tuning

5. **Slow Training Performance:**
   - **Use build-only mode** for quick prototyping: `./pre_train`
   - Enable parameter overrides in `model_config.json` to skip grid search
   - Reduce `k_folds` for faster cross-validation
   - Consider data sampling for very large datasets

6. **Legacy Issues (Fixed in v2024.09):**
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
    