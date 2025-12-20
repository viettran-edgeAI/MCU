# Concept Drift Generator Toolkit

This toolkit provides a complete environment to generate and visualize synthetic categorical data streams with controlled **Concept Drift**. It is specifically designed to evaluate the robustness of adaptive machine learning models (like Random Forests) on resource-constrained microcontrollers.

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
| `num_features` | Int | Number of input features ($). |
| `num_labels` | Int | Number of possible classes ($). |
| `burn_in` | Int | Number of instances before the drift starts. |
| `n_instances` | Int | Total number of data points to generate. |
| `type` | String | `abrupt` (instant change) or `gradual` (linear transition). |
| `drift_width` | Int | (For gradual) Number of instances over which the drift occurs. |
| `drift_magnitude_prior` | Float | Target Hellinger distance for (X)$ shift (0.0 to 1.0). |
| `drift_magnitude_conditional` | Float | Target magnitude for (Y|X)$ shift (0.0 to 1.0). |
| `noise_level` | Float | Probability of a random label flip (0.0 to 1.0). |
| `drift_priors` | Bool | Enable/Disable feature distribution drift. |
| `drift_conditional` | Bool | Enable/Disable decision boundary drift. |
| `seed` | Int | Random seed for reproducibility. |

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
- **Significance**: Helps identify **Prior Probability Drift** ((Y)$). If class frequencies change drastically, the model may need to rebalance its internal weights.

---

## 🛠️ Toolkit Components

### 1. Data Generator (`generate_data.cpp`)
A high-performance C++17 program that uses a Bayesian Network model to generate data. It uses Dirichlet sampling for attribute distributions and iterative optimization to hit precise Hellinger distance targets.

### 2. Visualization Tool (`visualize_drift.py`)
A Python script using `pandas`, `seaborn`, and `scipy` to perform statistical analysis and generate the plots described above.

## 📚 References
- Webb, G. I., et al. (2016). "Characterizing concept drift." *Data Mining and Knowledge Discovery*.
- Hellinger, E. (1909). "Neue Begründung der Theorie quadratischer Formen von unendlichvielen Veränderlichen."

