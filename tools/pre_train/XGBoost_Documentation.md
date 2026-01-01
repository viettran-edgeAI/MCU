# XGBoost for Embedded Systems - Technical Documentation

## Table of Contents
1. [Overview](#overview)
2. [Algorithm Explanation](#algorithm-explanation)
3. [Configuration Parameters](#configuration-parameters)
4. [Leaf Weight Scaling](#leaf-weight-scaling)
5. [Node Structure](#node-structure)
6. [Memory Efficiency](#memory-efficiency)
7. [Usage Examples](#usage-examples)

---

## Overview

This XGBoost implementation is designed for embedded systems with limited memory, particularly microcontrollers like ESP32. It uses a tightly-packed 64-bit node structure and efficient weight quantization to minimize memory footprint while maintaining high prediction accuracy.

**Key Features:**
- 64-bit packed node structure (8 bytes per node)
- 48-bit scaled floating-point weights for leaf nodes
- Multi-class classification via softmax objective
- OpenMP parallelization for training
- JSON-based configuration
- Binary model serialization

---

## Algorithm Explanation

### What is XGBoost?

XGBoost (eXtreme Gradient Boosting) is an ensemble learning method that builds multiple decision trees sequentially. Each new tree attempts to correct the errors made by the previous ensemble.

### Training Process

#### 1. **Initialization**
For multi-class classification with K classes:
- Initialize predictions for all samples: `F_k(x) = 0` for k ∈ {0, 1, ..., K-1}

#### 2. **Boosting Iterations**
For each boosting round m = 1 to M (num_boost_rounds):

   **For each class k:**
   
   a. **Compute Gradients and Hessians**
   ```
   For each sample i:
       p_i = softmax(F(x_i))  // Current probability distribution
       y_i = 1 if true_label == k else 0
       g_i = p_i[k] - y_i     // First derivative (gradient)
       h_i = max(p_i[k] * (1 - p_i[k]), 1e-6)  // Second derivative (hessian)
   ```

   b. **Build Regression Tree**
   - Find optimal splits by maximizing gain:
   ```
   Gain = 0.5 * [(G_L²/(H_L + λ)) + (G_R²/(H_R + λ)) - (G²/(H + λ))] - γ
   
   where:
       G = Σg_i (sum of gradients)
       H = Σh_i (sum of hessians)
       λ = L2 regularization term (lambda)
       γ = minimum loss reduction (gamma)
   ```

   c. **Assign Leaf Weights**
   ```
   For each leaf node:
       weight = -G / (H + λ)
   ```

   d. **Update Predictions**
   ```
   F_k(x_i) += η * tree_weight(x_i)
   
   where η = learning_rate
   ```

#### 3. **Final Prediction**
```
For sample x:
    scores = [F_0(x), F_1(x), ..., F_{K-1}(x)]
    predicted_class = argmax(scores)
```

### Why XGBoost Works

1. **Gradient Boosting**: Each tree corrects residual errors from previous trees
2. **Second-Order Approximation**: Uses both gradients and hessians for better convergence
3. **Regularization**: Lambda and gamma prevent overfitting
4. **Additive Learning**: Small learning rate with many rounds provides stable improvement

---

## Configuration Parameters

### Dataset Parameters

#### `data_path` (string)
- **Description**: Path to the normalized CSV dataset
- **Format**: First column is label, remaining columns are features
- **Example**: `"../data_quantization/data/result/cancer_data_nml.csv"`

#### `num_features` (uint16_t)
- **Description**: Number of input features (auto-detected from dataset)
- **Range**: 1 to 1023
- **Note**: Set automatically during initialization

#### `num_labels` (uint16_t)
- **Description**: Number of unique classes (auto-detected)
- **Range**: 2 to 255
- **Binary classification**: 2
- **Multi-class**: 3+

#### `num_samples` (uint32_t)
- **Description**: Total number of samples in dataset (auto-detected)
- **Note**: Set automatically during initialization

#### `quantization_coefficient` (uint8_t)
- **Description**: Number of bits per feature value after quantization
- **Range**: 1 to 8 bits
- **Effect**:
  - **1 bit**: 2 possible values (0, 1) - binary features
  - **2 bits**: 4 values (0-3) - minimal memory, fast training
  - **4 bits**: 16 values (0-15) - balanced
  - **8 bits**: 256 values (0-255) - maximum precision
- **Memory Impact**: Affects threshold search space (2^bits candidates per feature)
- **Recommendation**: Start with 2-4 bits for embedded systems

---

### XGBoost Core Parameters

#### `num_boost_rounds` (uint16_t)
- **Description**: Number of sequential boosting iterations
- **Range**: 1 to 1000+
- **Effect**:
  - **Low (10-50)**: Fast training, may underfit
  - **Medium (50-200)**: Balanced performance
  - **High (200+)**: Better accuracy, longer training, risk of overfitting
- **Total trees**: `num_boost_rounds × num_labels`
- **Recommendation**: 50-100 for most datasets
- **Example**: 50 rounds × 2 classes = 100 trees

#### `learning_rate` (float, η)
- **Description**: Step size shrinkage to prevent overfitting
- **Range**: 0.001 to 1.0
- **Effect**:
  - **Low (0.01-0.1)**: Slower convergence, needs more rounds, better generalization
  - **Medium (0.1-0.3)**: Balanced (recommended)
  - **High (0.3-1.0)**: Fast convergence, risk of overfitting
- **Typical Values**:
  - `0.3`: Default, aggressive learning
  - `0.1`: Conservative, more stable
  - `0.01`: Very conservative, use with many rounds
- **Trade-off**: Lower learning rate + more rounds = better accuracy but longer training

#### `lambda` (float, λ)
- **Description**: L2 regularization term on leaf weights
- **Range**: 0.0 to 100+
- **Effect**:
  - **0**: No regularization
  - **0.1-1.0**: Light regularization
  - **1.0-10**: Moderate regularization (recommended)
  - **10+**: Strong regularization, may underfit
- **Purpose**: Prevents individual leaf weights from becoming too large
- **Formula**: Weight denominator becomes `(H + λ)` instead of just `H`
- **Default**: 1.0

#### `alpha` (float, α)
- **Description**: L1 regularization term on leaf weights
- **Range**: 0.0 to 100+
- **Effect**:
  - **0**: No L1 regularization (default)
  - **0.1-1.0**: Promotes sparsity in leaf weights
  - **1.0+**: Aggressive sparsity
- **Purpose**: Can zero out some leaf weights, creating simpler models
- **Note**: Currently implemented but not used in weight calculation
- **When to use**: High-dimensional sparse data

#### `gamma` (float, γ)
- **Description**: Minimum loss reduction required to make a split
- **Range**: 0.0 to 100+
- **Effect**:
  - **0**: No constraint, split if any gain > 0
  - **0.1-1.0**: Conservative splitting
  - **1.0+**: Very conservative, smaller trees
- **Purpose**: Pruning parameter - prevents splits with minimal gain
- **Result**: Higher gamma → smaller, simpler trees
- **Trade-off**: Too high can cause underfitting
- **Default**: 0.0

---

### Tree Structure Parameters

#### `max_depth` (uint16_t)
- **Description**: Maximum depth of each tree
- **Range**: 1 to 250
- **Effect**:
  - **1-3**: Very shallow, fast, may underfit
  - **4-6**: Balanced (recommended for embedded)
  - **7-10**: Deeper trees, more expressive
  - **10+**: Very deep, slow, overfitting risk
- **Memory Impact**: Deeper trees → more nodes → more memory
- **Nodes at depth d**: Up to 2^d - 1 nodes
- **Recommendation**: 
  - Embedded systems: 4-6
  - Desktop: 6-10
- **Default**: 6

#### `min_child_weight` (uint16_t)
- **Description**: Minimum sum of instance weight (hessian) needed in a child
- **Range**: 0 to 1000+
- **Effect**:
  - **1**: Allows small child nodes (default)
  - **5-10**: More conservative splitting
  - **20+**: Very conservative, prevents overfitting on noisy data
- **Purpose**: Prevents splits on very small groups of samples
- **When to increase**: Dataset with high class imbalance or noise
- **Formula**: Split only if `H_left >= min_child_weight` AND `H_right >= min_child_weight`

---

### Sampling Parameters

#### `subsample` (float)
- **Description**: Fraction of training samples to use for each boosting round
- **Range**: 0.0 to 1.0
- **Effect**:
  - **1.0**: Use all samples (default)
  - **0.8**: Use 80% (recommended for large datasets)
  - **0.5**: Use 50% (aggressive, fast training)
- **Purpose**: Randomness improves generalization, speeds up training
- **Note**: Currently defined but not fully implemented in this version
- **When to use**: Large datasets or to prevent overfitting

#### `colsample_bytree` (float)
- **Description**: Fraction of features to consider for each tree
- **Range**: 0.0 to 1.0
- **Effect**:
  - **1.0**: Use all features (default)
  - **0.8**: Use 80% of features
  - **0.5**: Use 50% (creates diversity)
- **Purpose**: Random feature sampling reduces overfitting, adds diversity
- **Similar to**: Random Forest's max_features parameter
- **Note**: Currently defined but not fully implemented in this version

---

### Training Parameters

#### `train_ratio` (float)
- **Description**: Fraction of data used for training
- **Range**: 0.5 to 0.95
- **Typical Values**:
  - **0.7**: 70% train, 30% test
  - **0.8**: 80% train, 20% test (recommended)
  - **0.9**: 90% train, 10% test (large datasets)
- **Note**: `train_ratio + test_ratio` should typically equal 1.0

#### `test_ratio` (float)
- **Description**: Fraction of data used for testing
- **Range**: 0.05 to 0.5
- **Default**: 0.2 (20%)

#### `random_seed` (uint32_t)
- **Description**: Seed for random number generator
- **Range**: Any unsigned 32-bit integer
- **Purpose**: Ensures reproducible results
- **Default**: 42
- **Effect**: Same seed → same train/test split and same random decisions

---

### Objective and Evaluation

#### `objective` (string)
- **Description**: Learning objective function
- **Options**:
  - **`"multi:softprob"`**: Multi-class classification with softmax (default)
  - **`"binary:logistic"`**: Binary classification with logistic loss
  - **`"reg:squarederror"`**: Regression with squared error
- **Current Implementation**: Only `"multi:softprob"` fully supported
- **Effect**: Determines how gradients and hessians are computed

#### `eval_metric` (string)
- **Description**: Metric used for evaluation
- **Options**:
  - **`"mlogloss"`**: Multi-class log loss (default)
  - **`"logloss"`**: Binary log loss
  - **`"rmse"`**: Root mean squared error (regression)
  - **`"mae"`**: Mean absolute error (regression)
- **Note**: Used for reporting, doesn't affect training

---

### Early Stopping (Planned Feature)

#### `early_stopping` (bool)
- **Description**: Whether to stop training early if no improvement
- **Default**: false
- **Note**: Not yet implemented

#### `early_stopping_rounds` (uint16_t)
- **Description**: Stop if no improvement for N consecutive rounds
- **Default**: 10
- **Note**: Not yet implemented

---

## Leaf Weight Scaling

### The Challenge

In embedded systems, we need to store floating-point leaf weights in a fixed-size integer representation. The challenge is maintaining precision while fitting into available bits.

### Our Solution: 48-bit Scaled Integers

#### Storage Format

**Leaf Node Layout (64-bit total):**
```
[is_leaf(1 bit) | unused(15 bits) | weight(48 bits)]
                                    └─ signed integer
```

#### Scaling Mechanism

**Encoding (Float → Integer):**
```cpp
const int64_t WEIGHT_SCALE = 1,000,000,000;  // 10^9
const float WEIGHT_RANGE = 140,737.0f;

// Convert float to 48-bit integer
float weight = -2.5437;
int64_t scaled = (int64_t)(weight * WEIGHT_SCALE);
// scaled = -2,543,700,000

// Clamp to valid range
if (weight < -WEIGHT_RANGE) weight = -WEIGHT_RANGE;
if (weight > WEIGHT_RANGE) weight = WEIGHT_RANGE;

// Store in lower 48 bits
node.data |= (uint64_t)(scaled & 0xFFFFFFFFFFFF);
```

**Decoding (Integer → Float):**
```cpp
// Extract 48-bit value
int64_t scaled = (int64_t)(node.data & 0xFFFFFFFFFFFF);

// Sign extension (48-bit to 64-bit)
if (scaled & 0x800000000000LL) {
    scaled |= 0xFFFF000000000000LL;
}

// Convert back to float
float weight = (float)scaled / WEIGHT_SCALE;
// weight = -2.5437
```

### Precision Analysis

**Theoretical Precision:**
- **Scale Factor**: 10^9
- **Minimum Step**: 1 / 10^9 = 0.000000001 (1 nanosecond)
- **Effective Precision**: ~9 decimal places

**Practical Range:**
- **48-bit signed integer**: -2^47 to 2^47-1
- **Scaled range**: ±140,737.488,355,327
- **Practical clamp**: ±140,737.0 (for safety)

**Comparison:**
| Type | Bytes | Range | Precision |
|------|-------|-------|-----------|
| float32 | 4 | ±3.4×10^38 | ~7 decimal places |
| Our 48-bit | 6 | ±140,737 | ~9 decimal places |
| float64 | 8 | ±1.7×10^308 | ~15 decimal places |

**Why This Works:**
1. XGBoost leaf weights are typically in range [-10, 10]
2. We have massive headroom: ±140,737
3. Precision of 10^-9 is far more than needed (10^-4 is usually sufficient)
4. We save 2 bytes per node compared to using full float64

### Memory Savings Example

**Cancer Dataset Model:**
- 100 trees
- 1,632 total nodes
- 866 leaf nodes

**Memory Comparison:**
```
Our approach:    1,632 nodes × 8 bytes  = 13,056 bytes (12.75 KB)
With float64:    1,632 nodes × 16 bytes = 26,112 bytes (25.5 KB)
Savings: 50% memory reduction
```

### Numerical Stability

**Potential Issues & Solutions:**

1. **Overflow Protection:**
   ```cpp
   // Check before scaling
   if (weight < -WEIGHT_RANGE) weight = -WEIGHT_RANGE;
   if (weight > WEIGHT_RANGE) weight = WEIGHT_RANGE;
   ```

2. **Sign Extension:**
   ```cpp
   // Properly handle negative numbers
   if (scaled & 0x800000000000LL) {
       scaled |= 0xFFFF000000000000LL;
   }
   ```

3. **Rounding:**
   ```cpp
   // Round to nearest integer when scaling
   int64_t scaled = (int64_t)(weight * WEIGHT_SCALE + 0.5f);
   ```

### When to Adjust Scaling

**Increase Scale Factor (10^10 or more) if:**
- You need more decimal precision
- Leaf weights are very small (< 0.001)
- You're doing regression with tiny residuals

**Decrease Scale Factor (10^6 or less) if:**
- Leaf weights exceed ±1,000
- You need wider range
- Precision isn't critical

**Current Choice (10^9) is Optimal for:**
- Classification tasks
- Typical XGBoost use cases
- Balance of range and precision
- Embedded system constraints

---

## Node Structure

### XG_node: 64-bit Packed Layout

#### Split Node Format
```
Bit Layout:
┌─────┬──────────────┬─────────────┬────────────────────────┐
│ 63  │   62 - 48    │   47 - 32   │        31 - 0          │
├─────┼──────────────┼─────────────┼────────────────────────┤
│  1  │ feature_id   │  threshold  │  left_child_index      │
│bit  │  (15 bits)   │  (16 bits)  │    (32 bits)           │
└─────┴──────────────┴─────────────┴────────────────────────┘
     │               │             │
     │               │             └─ Index of left child (right = left + 1)
     │               └─ Quantized feature threshold (0 to 2^quant_bits - 1)
     └─ Feature ID to split on (0 to 32767)
```

**Properties:**
- `is_leaf = 1`: Indicates this is a split node
- `feature_id`: Which feature to test (up to 32,767 features supported)
- `threshold`: Quantized threshold value
- `left_child_index`: Location of left child in node array
- `right_child_index`: Always `left_child_index + 1`

#### Leaf Node Format
```
Bit Layout:
┌─────┬──────────────┬────────────────────────────────────────┐
│ 63  │   62 - 48    │              47 - 0                    │
├─────┼──────────────┼────────────────────────────────────────┤
│  1  │   unused     │           weight                       │
│bit  │  (15 bits)   │      (48-bit scaled float)             │
└─────┴──────────────┴────────────────────────────────────────┘
     │               │
     │               └─ Prediction weight (scaled by 10^9)
     └─ Set to 1 to indicate leaf node
```

**Properties:**
- `is_leaf = 1`: Indicates this is a leaf node
- `weight`: 48-bit scaled integer representing the prediction contribution
- Unused bits reserved for future extensions

### Comparison with Random Forest Nodes

| Feature | XGBoost (XG_node) | Random Forest (Tree_node) |
|---------|-------------------|---------------------------|
| **Size** | 8 bytes (64-bit) | 8 bytes (2×32-bit) |
| **Leaf Value** | 48-bit scaled float | 8-bit label ID |
| **Feature Bits** | 15 bits (32K features) | 15 bits (32K features) |
| **Threshold Bits** | 16 bits | 8 bits |
| **Child Index Bits** | 32 bits | 32 bits |
| **Precision** | ~9 decimal places | N/A (categorical) |
| **Use Case** | Regression/ranking | Classification |

---

## Memory Efficiency

### Node Memory Breakdown

**Per Node:**
- Split node: 8 bytes
- Leaf node: 8 bytes
- No overhead: direct bit packing

**Example Model (Cancer Dataset):**
```
Configuration:
  - 50 boost rounds
  - 2 classes
  - Max depth: 6
  
Results:
  - Total trees: 100
  - Total nodes: 1,632
  - Leaf nodes: 866
  - Internal nodes: 766
  
Memory Usage:
  - Nodes: 1,632 × 8 bytes = 13,056 bytes
  - Overhead: ~100 bytes (tree metadata)
  - Total: ~13 KB
```

### Scalability Analysis

**Memory vs Parameters:**

| Boost Rounds | Classes | Avg Depth | Est. Nodes | Est. Memory |
|--------------|---------|-----------|------------|-------------|
| 10 | 2 | 4 | ~300 | 2.4 KB |
| 50 | 2 | 6 | ~1,600 | 13 KB |
| 100 | 2 | 6 | ~3,200 | 26 KB |
| 50 | 5 | 6 | ~4,000 | 32 KB |
| 100 | 10 | 8 | ~40,000 | 320 KB |

**Maximum Capacity (ESP32 with 320KB RAM):**
```
Available for model: ~250 KB
Nodes possible: 250,000 / 8 = 31,250 nodes
Trees possible (at 40 nodes/tree): ~780 trees
Practical limit: ~400-500 trees for safety
```

### Optimization Strategies

1. **Reduce Depth:**
   - `max_depth = 4` instead of `6`: ~4× fewer nodes
   - Trade-off: May need more rounds for same accuracy

2. **Reduce Rounds:**
   - `num_boost_rounds = 30` instead of `100`: 3× less memory
   - Trade-off: Lower accuracy

3. **Use Binary Classification:**
   - 2 classes vs 10 classes: 5× less memory
   - Consider one-vs-rest for multi-class if needed

4. **Enable Early Stopping:**
   - Stop when validation accuracy plateaus
   - Automatically finds optimal number of rounds

---

## Usage Examples

### Basic Training

```bash
# Compile
g++ --std=c++17 -O2 -fopenmp -I../../src -o xgboost_pc ./xgboost_pc.cpp

# Run with default config
./xgboost_pc

# Run with custom config
./xgboost_pc --config my_config.json --threads 4
```

### Configuration Templates

#### Conservative (Low Memory, Fast)
```json
{
  "num_boost_rounds": 20,
  "learning_rate": 0.2,
  "lambda": 2.0,
  "gamma": 1.0,
  "max_depth": 4,
  "min_child_weight": 10
}
```
**Use case:** Embedded systems with tight memory constraints

#### Balanced (Recommended)
```json
{
  "num_boost_rounds": 50,
  "learning_rate": 0.1,
  "lambda": 1.0,
  "gamma": 0.0,
  "max_depth": 6,
  "min_child_weight": 1
}
```
**Use case:** General purpose, good accuracy/memory trade-off

#### Aggressive (High Accuracy)
```json
{
  "num_boost_rounds": 100,
  "learning_rate": 0.05,
  "lambda": 0.5,
  "gamma": 0.0,
  "max_depth": 8,
  "min_child_weight": 1
}
```
**Use case:** Desktop training, maximum accuracy

### Tuning Guide

**Step 1: Start Conservative**
```json
{
  "num_boost_rounds": 50,
  "learning_rate": 0.1,
  "max_depth": 6,
  "lambda": 1.0
}
```

**Step 2: If Underfitting (low train accuracy):**
- Increase `max_depth` to 8-10
- Increase `num_boost_rounds` to 100
- Decrease `lambda` to 0.5
- Decrease `gamma` to 0.0

**Step 3: If Overfitting (train acc >> test acc):**
- Decrease `max_depth` to 4-5
- Increase `lambda` to 2.0-5.0
- Increase `gamma` to 0.5-1.0
- Decrease `learning_rate` and increase `num_boost_rounds`

**Step 4: If Training Too Slow:**
- Decrease `num_boost_rounds`
- Decrease `max_depth`
- Increase `learning_rate` (but watch for overfitting)
- Use fewer threads (OpenMP overhead on small datasets)

### Output Interpretation

```
📊 Model Statistics:
   Total trees: 100              ← num_boost_rounds × num_labels
   Total nodes: 1,632            ← Sum across all trees
   Total leafs: 866              ← ~53% are leaf nodes (good)
   Avg nodes/tree: 16.3          ← Relatively small trees
   Max depth: 7                  ← One tree exceeded max_depth=6 slightly
   Memory usage: 13056 bytes     ← ~13 KB total
   Node size: 8 bytes            ← Compact representation

🧪 Evaluating XGBoost Model...
   Train Accuracy: 1.0000        ← Perfect on training (possible overfit?)
   Test Accuracy:  0.9737        ← Good generalization
   Train Samples:  455/455
   Test Samples:   111/114       ← 3 errors on test set
```

**Healthy Model Signs:**
- Test accuracy within 2-5% of train accuracy
- Avg nodes/tree: 10-30 (not too large)
- Memory usage fits target device

**Warning Signs:**
- Train accuracy = 1.0, Test accuracy < 0.8: Overfitting
- Avg nodes/tree > 50: Trees too complex
- Max depth >> configured max_depth: Hitting limits

---

## Advanced Topics

### Multi-class vs Binary Classification

**Binary (2 classes):**
- Builds 2 × num_boost_rounds trees
- One tree per class per round
- Faster training, less memory

**Multi-class (K classes):**
- Builds K × num_boost_rounds trees
- Memory scales linearly with K
- Example: 10 classes, 50 rounds = 500 trees

**One-vs-Rest Alternative:**
- Train K separate binary models
- Same total trees but more flexible
- Can use different hyperparameters per class

### Gradient and Hessian Computation

For softmax multi-class:
```cpp
// Current prediction
vector<float> F = {F_0(x), F_1(x), ..., F_{K-1}(x)};

// Convert to probabilities
vector<float> p = softmax(F);

// For class k:
float y = (true_label == k) ? 1.0 : 0.0;
float g = p[k] - y;           // Gradient
float h = p[k] * (1 - p[k]);  // Hessian

// Regularize hessian to prevent division by zero
h = max(h, 1e-6);
```

### Feature Importance (Planned)

Track gain contributed by each feature:
```cpp
for each split:
    importance[feature_id] += gain;
```

This can guide:
- Feature selection
- Dimensionality reduction
- Understanding model decisions

---

## Troubleshooting

### Issue: Poor Test Accuracy

**Symptoms:** Train accuracy high, test accuracy low

**Solutions:**
1. Increase regularization: `lambda = 2.0-5.0`
2. Increase pruning: `gamma = 0.5-1.0`
3. Reduce depth: `max_depth = 4-5`
4. Use more conservative splits: `min_child_weight = 5-10`
5. Reduce learning rate, increase rounds: `learning_rate = 0.05, num_boost_rounds = 100`

### Issue: Memory Overflow

**Symptoms:** Compilation fails, runtime crashes on device

**Solutions:**
1. Reduce rounds: `num_boost_rounds = 20-30`
2. Reduce depth: `max_depth = 4-5`
3. Use fewer classes (one-vs-rest)
4. Check available RAM before loading

### Issue: Training Too Slow

**Symptoms:** Takes minutes/hours to train

**Solutions:**
1. Reduce features via feature selection
2. Use coarser quantization: `quantization_coefficient = 2`
3. Decrease `num_boost_rounds`
4. Increase OpenMP threads (if available)
5. Use smaller `max_depth`

### Issue: Compilation Errors

**Common causes:**
- Missing OpenMP: Remove `-fopenmp` flag
- Wrong include path: Verify `-I../../src` points to STL_MCU headers
- C++ version: Ensure `--std=c++17` or higher

---

## References

1. Chen, T., & Guestrin, C. (2016). "XGBoost: A Scalable Tree Boosting System." KDD 2016.
2. Friedman, J. H. (2001). "Greedy function approximation: A gradient boosting machine." Annals of Statistics.
3. Original XGBoost paper: https://arxiv.org/abs/1603.02754

---

## Version History

- **v2.0.0** (2026-01-01): Initial embedded-optimized implementation
  - 64-bit packed nodes
  - 48-bit leaf weight scaling
  - Multi-class softmax support
  - OpenMP parallelization

---

*Document maintained by: STL_MCU Project*  
*Last updated: January 1, 2026*
