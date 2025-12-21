# Concept Drift Generator Toolkit

This toolkit provides a complete environment to generate and visualize synthetic data streams with controlled **Concept Drift**. It is inspired by [THU-Concept-Drift-Datasets-v1.0](https://github.com/songqiaohu/THU-Concept-Drift-Datasets-v1.0) and specifically designed to evaluate the robustness of adaptive machine learning models (like Random Forests) on resource-constrained microcontrollers.

## 🚀 Quick Start

The easiest way to run the entire pipeline (compilation, generation, and visualization) is using the provided shell script:

```bash
bash run_toolkit.sh
```

This will:
1. Compile the C++ Data Generator.
2. Generate datasets based on `parameters.json`.
3. Create advanced visualizations in the `drift_concept_visualize/` folder.

---

## ⚙️ Configuration (`parameters.json`)

The toolkit supports multiple drift scenarios in a single run. Define them as a JSON array in `parameters.json`.

### Parameter Reference:

| Parameter | Type | Description |
| :--- | :--- | :--- |
| `name` | String | Unique identifier for the dataset and output files. |
| `num_features` | Int | Number of input features. |
| `num_labels` | Int | Number of possible classes (2 for binary, >2 for multi-class). |
| `burn_in` | Int | Number of instances before the drift starts. |
| `n_instances` | Int | Total number of data points to generate. |
| `type` | String | Drift type: `abrupt`, `gradual`, `sudden`, or `recurrent`. |
| `drift_width` | Int | (For gradual) Number of instances over which the drift occurs. |
| `num_drift_points` | Int | (For sudden/recurrent) Number of drift change points. |
| `boundary_type` | String | Decision boundary: `linear`, `circular`, `chocolate`, `torus`, or `hash`. |
| `drift_magnitude_prior` | Float | Magnitude for feature distribution shift (0.0 to 1.0). |
| `drift_magnitude_conditional` | Float | Magnitude for decision boundary shift (0.0 to 1.0). |
| `drift_magnitude_linear` | Float | Rotation angle factor for linear boundaries (0.0 to 1.0 = 0 to π). |
| `noise_level` | Float | Probability of a random label flip (0.0 to 1.0). |
| `drift_priors` | Bool | Enable/Disable feature distribution drift. |
| `drift_conditional` | Bool | Enable/Disable decision boundary drift. |
| `seed` | Int | Random seed for reproducibility. |
| `x_spinaxis` | Float | X-coordinate of rotation axis for linear boundaries (THU-style). |
| `y_spinaxis` | Float | Y-coordinate of rotation axis for linear boundaries (THU-style). |
| `add_noise` | Bool | Add Gaussian noise to features (default: true). |
| `add_redundant` | Bool | Add redundant features (default: false). |
| `num_redundant_features` | Int | Number of redundant features to add (default: 3). |

### Drift Types (Inspired by THU Datasets):

| Type | Description | Example |
| :--- | :--- | :--- |
| `abrupt` | Instant decision boundary change at `burn_in` | Sensor failure, sudden mode change |
| `gradual` | Linear transition over `drift_width` instances | Gradual wear, slow environmental change |
| `sudden` | Step-wise changes at `num_drift_points` intervals | Periodic reconfiguration |
| `recurrent` | Oscillating drift pattern (back-and-forth) | Day/night cycles, seasonal patterns |

### Boundary Types (THU Dataset Styles):

| Type | Description | THU Equivalent |
| :--- | :--- | :--- |
| `linear` | Rotating hyperplane decision boundary | Linear_Gradual_Rotation, Linear_Abrupt |
| `circular` | Angle-based classification (pie slices) | CakeRotation datasets |
| `chocolate` | Grid-based rotation classification | ChocolateRotation datasets |
| `torus` | Rolling torus intersection classification | RollingTorus datasets |
| `hash` | Feature-hash based (for high-dimensional data) | Custom for MCU testing |

---

## 📊 Understanding the Charts

The toolkit generates several advanced visualizations to characterize the drift:

### 1. Distribution Divergence over Time (`*_dist_divergence.png`)
Tracks how much the current data deviates from the initial "Reference" concept.
- **Wasserstein Distance**: Measures the "work" required to transform one distribution into another. High values indicate significant feature shifts.
- **Hellinger Distance**: A bounded metric (0-1) for probability divergence.
- **Drift Point**: Marked with a red dashed line. Shaded regions indicate the transition window for gradual drift.

### 2. Feature Space Geometry Drift (`*_centroid_drift.png` & `*_pca_time.png`)
Visualizes the physical movement of data in the feature space.
- **Centroid Drift**: Plots the Euclidean distance between the reference centroid ($\mu_{ref}$) and the moving window centroid ($\mu_t$). A steady climb indicates incremental or gradual drift.
- **3D PCA Projection**: Projects high-dimensional data into 3D. Points are colored by **Time** (dark purple $\rightarrow$ yellow). Large 'X' markers show the shift in the center of mass before and after drift.

### 3. Label Distribution over Time (`*_label_dist_time.png`)
A line graph showing the relative frequency of each class over time.
- **Significance**: Helps identify **Prior Probability Drift** ($P(Y)$). If class frequencies change drastically, the model may need to rebalance its internal weights.

---

## 🛠️ Toolkit Components

### 1. Data Generator (`generate_data.cpp`)
A high-performance C++17 program that generates synthetic datasets with controlled concept drift. Supports:
- THU-style geometric drift patterns (Linear, CakeRotation, ChocolateRotation, RollingTorus)
- Multi-class classification
- Configurable noise and redundant features
- Hash-based classification for high-dimensional data

**Usage:**
```bash
# Compile
g++ -std=c++17 -O3 -o generate_data generate_data.cpp

# Run with default parameters.json
./generate_data

# Custom config and output
./generate_data -c my_params.json -o my_datasets/

# Show help
./generate_data --help
```

### 2. Visualization Tool (`visualize_drift.py`)
A Python script using `pandas`, `seaborn`, and `scipy` to perform statistical analysis and generate the plots described above.

---

## 📚 References
- THU Concept Drift Datasets: [songqiaohu/THU-Concept-Drift-Datasets-v1.0](https://github.com/songqiaohu/THU-Concept-Drift-Datasets-v1.0)
- Hu, S., et al. (2024). "CADM+: Confusion-Based Learning Framework With Drift Detection and Adaptation." *IEEE TNNLS*.
- Webb, G. I., et al. (2016). "Characterizing concept drift." *Data Mining and Knowledge Discovery*.
- Hellinger, E. (1909). "Neue Begründung der Theorie quadratischer Formen von unendlichvielen Veränderlichen."
