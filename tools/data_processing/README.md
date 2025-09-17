# Quantization Data Visualizer

This tool provides comprehensive PCA-based visualization and analysis to assess the effects of data quantization on classification datasets. It creates multi-angle 3D scatter plots comparing original and quantized data distributions with detailed statistical analysis.

## Features

- **Multi-View PCA Analysis**: 4 different viewing angles for comprehensive visualization
- **Smart Sampling**: Reduces visual density for large datasets while maintaining class representation
- **Class Distribution**: Shows how different classes are distributed in principal component space
- **Quantization Impact Assessment**: Compares data dispersion before and after quantization
- **Classification Quality Scoring**: Evaluates class separability and provides recommendations
- **Comprehensive Variance Analysis**: Detailed explanation of principal component significance
- **Multiple Datasets Support**: Works with iris, cancer, digit, and walker_fall datasets

## Key Enhancements

- **3×3 Grid Layout**: Organized visualization with original data (row 1), quantized data (row 2), and impact assessment (row 3)
- **Smart Class Limiting**: Automatically shows only top 5 most frequent classes for datasets with >5 classes
- **Dense Data Sampling**: Automatically samples large datasets (>500 points) for clearer visualization
- **Three Viewing Angles**: Standard, Top-Front, and Side views for comprehensive perspective
- **Quantization Impact Assessment**: Visual charts showing variance retention, comparison metrics, and recommendations
- **Enhanced Metrics Dashboard**: Bottom row includes variance comparison bars, retention percentages, and summary recommendations

## Requirements

- Python 3.7+
- numpy>=1.21.0
- pandas>=1.3.0
- matplotlib>=3.4.0
- scikit-learn>=1.0.0

## Installation

### Quick Setup
```bash
chmod +x install_and_test.sh
./install_and_test.sh
```

### Manual Setup
```bash
# Create virtual environment
python3 -m venv venv_visualizer
source venv_visualizer/bin/activate

# Install dependencies
pip install -r requirements.txt

# Create plots directory
mkdir -p plots
```

## Usage

```bash
python quantization_visualizer.py <model_name>
```

### Examples
```bash
# Visualize iris dataset
python quantization_visualizer.py iris_data

# Visualize cancer dataset
python quantization_visualizer.py cancer_data

# Visualize digit dataset
python quantization_visualizer.py digit_data

# Visualize walker fall detection dataset
python quantization_visualizer.py walker_fall
```

## Data Format

The program expects:
- **Original data**: `data/<model_name>.csv`
- **Quantized data**: `data/result/<model_name>_nml.csv`

Both files should:
- Have class labels in the first column
- Have feature data in subsequent columns
- Can have header rows (will be auto-detected and handled)

## Output

The visualizer generates:

1. **Console Output**: 
   - Dataset information (shape, number of classes)
   - Comprehensive PCA variance analysis with detailed explanations
   - Class separation quality assessment
   - Quantization impact evaluation
   - Clear recommendations for classification use

2. **3×3 Grid Visualization Layout**:
   - **Row 1**: Original data from 3 different viewing angles
   - **Row 2**: Quantized data from matching viewing angles  
   - **Row 3**: Quantization impact assessment charts
   - Auto-sampling for datasets with >500 points per visualization
   - Shows only top 5 most frequent classes for datasets with >5 classes

3. **Quantization Impact Assessment Dashboard**:
   - **Variance Comparison**: Bar chart comparing PC1-PC3 between original and quantized
   - **Retention Metrics**: Color-coded retention percentages (green ≥90%, orange ≥80%, red <80%)
   - **Summary & Recommendation**: Clear guidance with visual indicators

## Interpretation

### Explained Variance Percentages
- **PC1 (Principal Component 1)**: Captures the direction of maximum data variation
  - High PC1% (>70%): Data varies mainly along one primary direction
  - Moderate PC1% (30-70%): Data has significant variation in multiple directions
  - Low PC1% (<30%): Data variation is distributed across many dimensions

### Variance Analysis
- **Total 3PC Variance**: Percentage of information preserved in 3D visualization
  - >95%: Excellent 3D representation of the data
  - 80-95%: Good 3D representation
  - <80%: 3D view may miss important data structure

### Classification Quality Indicators
- **Class Separation Score**: Measures how distinguishable classes are in the feature space
- **Separation Retention**: How well quantization preserves class boundaries
- **Visual Cluster Analysis**: Well-separated, tight clusters indicate good classification potential

### Quantization Impact Assessment
- **✓ Recommended**: Quantized version maintains >90% quality metrics
- **⚠ Acceptable**: Quantized version retains 80-90% of original quality
- **✗ Not Recommended**: Significant quality loss (>20%) in quantized version

### Multiple View Benefits
- **Standard View (-150°, 110°)**: General overview of class distribution
- **Top-Front View (20°, 45°)**: Better view of class overlap and separation
- **Side View (-90°, 0°)**: Shows data spread along different axes

### Visual Assessment Dashboard
- **Variance Bars**: Direct comparison of PC1, PC2, PC3 between original and quantized
- **Retention Traffic Lights**: Green (≥90%), Orange (≥80%), Red (<80%) for quick assessment
- **Recommendation Panel**: Clear visual guidance with summary metrics
- **Class Limitation**: Automatically focuses on top 5 classes for complex datasets (10+ classes)

## Supported Datasets

| Dataset | Classes | Description |
|---------|---------|-------------|
| iris_data | 3 | Iris flower species classification |
| cancer_data | 2 | Breast cancer diagnosis (malignant/benign) |
| digit_data | 10 | Handwritten digit recognition |
| walker_fall | 2 | Fall detection from sensor data |

## Example Output

```
Dataset: iris_data
Original data shape: (150, 4)
Quantized data shape: (149, 4)
Number of classes: 3

================================================================================
PCA ANALYSIS & CLASSIFICATION QUALITY ASSESSMENT
================================================================================
EXPLAINED VARIANCE ANALYSIS:
----------------------------------------
Explained variance represents the proportion of the dataset's total variation
captured by each principal component. Higher percentages mean more information
is preserved in fewer dimensions.

Original Data - Explained Variance:
  PC1: 69.5% (most important variation direction)
  PC2: 22.6% (second most important direction)
  PC3: 5.2% (third most important direction)
  Total (3 PCs): 97.3% (total info preserved in 3D)

Quantized Data - Explained Variance:
  PC1: 69.4%
  PC2: 22.7%
  PC3: 5.1%
  Total (3 PCs): 97.3%

Variance Retention after Quantization:
  PC1: 99.8%
  PC2: 100.4%
  PC3: 100.0%

----------------------------------------
CLASSIFICATION QUALITY ASSESSMENT:
----------------------------------------
Class Separation Score (higher = better for classification):
  Original Data:  1.000
  Quantized Data: 1.000
  Separation Retention: 100.0%

----------------------------------------
RECOMMENDATION FOR CLASSIFICATION:
----------------------------------------
✓ QUANTIZED version recommended - minimal quality loss

Plot saved as: plots/iris_data_pca_comparison.png
```

## Troubleshooting

**File not found errors**: Ensure the CSV files exist in the expected locations
**Memory issues**: For large datasets, consider reducing the number of samples
**Display issues**: Use `export DISPLAY=:0` for headless systems with X11 forwarding
