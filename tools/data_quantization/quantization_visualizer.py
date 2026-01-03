#!/usr/bin/env python3
"""
Quantization Data Visualizer

This program creates PCA scatter plots to visualize the data dispersion 
before and after quantization for classification datasets.

Usage:
    python quantization_visualizer.py <model_name>

The program will look for:
    - Original data: data/<model_name>.csv
    - Quantized data: data/result/<model_name>_nml.csv

Both files should have no header and the class label in the first column.
"""

import argparse
import os
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import mpl_toolkits.mplot3d  # Required for 3D projections
from sklearn.decomposition import PCA
from sklearn.preprocessing import LabelEncoder


def load_data(filepath):
    """
    Load CSV data with or without header and class labels in first column.
    
    Returns:
        features: numpy array of feature data
        labels: numpy array of class labels
        label_mapping: dict mapping numeric labels to original string labels (or None)
    """
    try:
        # First, try to read as numbers (no header), with low_memory=False to suppress warnings
        data = pd.read_csv(filepath, header=None, low_memory=False)
        
        # Check if the first row contains strings (indicating a header)
        first_row = data.iloc[0]
        if any(isinstance(val, str) for val in first_row):
            # Reload with header
            data = pd.read_csv(filepath, header=0, low_memory=False)
        
        labels = data.iloc[:, 0].values  # First column is the class label
        features = data.iloc[:, 1:].values  # Rest are features
        
        # Convert string labels to numeric if needed
        label_mapping = None
        if not np.issubdtype(labels.dtype, np.number):
            from sklearn.preprocessing import LabelEncoder
            le = LabelEncoder()
            labels = le.fit_transform(labels)
            # Create mapping from numeric to original string labels
            label_mapping = {i: label for i, label in enumerate(le.classes_)}
            
        return features, labels, label_mapping
    except FileNotFoundError:
        print(f"Error: File {filepath} not found!")
        return None, None, None
    except Exception as e:
        print(f"Error loading {filepath}: {e}")
        return None, None, None


def sample_data_for_visualization(features, labels, max_samples_per_class=50, max_classes=5):
    """
    Sample data to reduce density while maintaining class representation.
    
    Args:
        features: Feature data array
        labels: Class labels array
        max_samples_per_class: Maximum samples per class for visualization
        max_classes: Maximum number of classes to display
    
    Returns:
        sampled_features, sampled_labels, sampling_info
    """
    unique_labels = np.unique(labels)
    
    # Limit to max_classes if there are too many
    if len(unique_labels) > max_classes:
        # Select the most frequent classes
        label_counts = [(label, np.sum(labels == label)) for label in unique_labels]
        label_counts.sort(key=lambda x: x[1], reverse=True)
        selected_labels = [label for label, _ in label_counts[:max_classes]]
        
        # Filter data to only include selected classes
        mask = np.isin(labels, selected_labels)
        features = features[mask]
        labels = labels[mask]
        unique_labels = np.array(selected_labels)
    
    sampled_indices = []
    
    for label in unique_labels:
        class_indices = np.where(labels == label)[0]
        if len(class_indices) > max_samples_per_class:
            # Random sampling for better representation
            np.random.seed(42)  # For reproducible results
            selected_indices = np.random.choice(class_indices, max_samples_per_class, replace=False)
        else:
            selected_indices = class_indices
        sampled_indices.extend(selected_indices)
    
    sampled_indices = np.array(sampled_indices)
    
    sampling_info = {
        'original_size': len(features),
        'sampled_size': len(sampled_indices),
        'samples_per_class': max_samples_per_class,
        'total_classes': len(unique_labels),
        'classes_shown': len(unique_labels),
        'classes_filtered': len(np.unique(labels)) > max_classes
    }
    
    return features[sampled_indices], labels[sampled_indices], sampling_info


def create_pca_plot(features, labels, title, ax, class_names=None, view_angle=None, max_samples=500):
    """
    Create a 3D PCA scatter plot with sampling for better visualization.
    
    Args:
        features: Feature data array
        labels: Class labels array
        title: Plot title
        ax: Matplotlib 3D axis
        class_names: Optional list of class names for legend
        view_angle: Tuple of (elev, azim) for viewing angle
        max_samples: Maximum total samples to display
    """
    # Sample data if too dense
    if len(features) > max_samples:
        max_per_class = max(10, max_samples // min(5, len(np.unique(labels))))
        features_viz, labels_viz, sampling_info = sample_data_for_visualization(
            features, labels, max_per_class
        )
        if sampling_info['classes_filtered']:
            title += f" (top 5 classes, {sampling_info['sampled_size']}/{sampling_info['original_size']} samples)"
        else:
            title += f" (sampled: {sampling_info['sampled_size']}/{sampling_info['original_size']})"
    else:
        features_viz, labels_viz, sampling_info = sample_data_for_visualization(features, labels, max_classes=5)
        if sampling_info['classes_filtered']:
            title += " (showing top 5 classes)"
    
    # Perform PCA
    pca = PCA(n_components=3)
    features_pca = pca.fit_transform(features)  # Use full data for PCA
    features_viz_pca = pca.transform(features_viz)  # Transform sampled data
    
    # Create scatter plot with better styling
    scatter = ax.scatter(
        features_viz_pca[:, 0],
        features_viz_pca[:, 1],
        features_viz_pca[:, 2],
        c=labels_viz,
        s=60,  # Slightly larger points
        cmap='tab10',  # Better color map for classification
        alpha=0.8,
        edgecolors='white',
        linewidth=0.5
    )
    
    # Set labels and title
    ax.set_title(title, fontsize=10, pad=10)
    ax.set_xlabel(f'PC1 ({pca.explained_variance_ratio_[0]*100:.1f}%)', fontsize=9)
    ax.set_ylabel(f'PC2 ({pca.explained_variance_ratio_[1]*100:.1f}%)', fontsize=9)
    ax.set_zlabel(f'PC3 ({pca.explained_variance_ratio_[2]*100:.1f}%)', fontsize=9)
    
    # Set viewing angle
    if view_angle:
        ax.view_init(elev=view_angle[0], azim=view_angle[1])
    else:
        ax.view_init(elev=-150, azim=110)
    
    # Remove tick labels for cleaner look
    ax.set_xticklabels([])
    ax.set_yticklabels([])
    ax.set_zticklabels([])
    
    # Add legend (smaller for compact layout)
    unique_labels = np.unique(labels_viz)
    if class_names is None:
        class_names = [f'Class {int(label)}' for label in unique_labels]
    
    # Create legend elements with better colors
    legend_elements = []
    colors = plt.cm.tab10(np.linspace(0, 1, len(unique_labels)))
    for i, (label, color) in enumerate(zip(unique_labels, colors)):
        legend_elements.append(plt.Line2D([0], [0], marker='o', color='w', 
                                        markerfacecolor=color, markersize=5,
                                        label=class_names[i] if i < len(class_names) else f'Class {int(label)}'))
    
    ax.legend(handles=legend_elements, loc='upper right', title='Classes', fontsize=7, title_fontsize=8)
    
    return pca.explained_variance_ratio_


def create_quantization_assessment_plots(original_var, quantized_var, original_features, quantized_features, 
                                       original_labels, quantized_labels, fig, model_name):
    """
    Create quantization impact assessment plots in the bottom row.
    """
    # Bottom left: Variance retention bar chart
    ax1 = fig.add_subplot(3, 3, 7)
    
    pc_names = ['PC1', 'PC2', 'PC3']
    original_percentages = original_var[:3] * 100
    quantized_percentages = quantized_var[:3] * 100
    
    x = np.arange(len(pc_names))
    width = 0.35
    
    bars1 = ax1.bar(x - width/2, original_percentages, width, label='Original', color='skyblue', alpha=0.8)
    bars2 = ax1.bar(x + width/2, quantized_percentages, width, label='Quantized', color='lightcoral', alpha=0.8)
    
    ax1.set_xlabel('Principal Components')
    ax1.set_ylabel('Explained Variance (%)')
    ax1.set_title('Variance Comparison', fontsize=10)
    ax1.set_xticks(x)
    ax1.set_xticklabels(pc_names)
    ax1.legend(fontsize=8)
    ax1.grid(True, alpha=0.3)
    
    # Add value labels on bars
    for bar in bars1:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height + 0.5,
                f'{height:.1f}%', ha='center', va='bottom', fontsize=8)
    for bar in bars2:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height + 0.5,
                f'{height:.1f}%', ha='center', va='bottom', fontsize=8)
    
    # Bottom middle: Retention percentages
    ax2 = fig.add_subplot(3, 3, 8)
    
    retention_percentages = []
    for i in range(3):
        retention = (quantized_var[i] / original_var[i]) * 100 if original_var[i] > 0 else 100
        retention_percentages.append(min(retention, 200))  # Cap at 200% for visualization
    
    colors = ['green' if r >= 90 else 'orange' if r >= 80 else 'red' for r in retention_percentages]
    bars = ax2.bar(pc_names, retention_percentages, color=colors, alpha=0.7)
    
    ax2.set_ylabel('Retention (%)')
    ax2.set_title('Variance Retention', fontsize=10)
    ax2.axhline(y=100, color='black', linestyle='--', alpha=0.5, label='Perfect Retention')
    ax2.axhline(y=90, color='green', linestyle='--', alpha=0.5, label='Excellent (90%)')
    ax2.axhline(y=80, color='orange', linestyle='--', alpha=0.5, label='Good (80%)')
    ax2.legend(fontsize=7, loc='upper right')
    ax2.grid(True, alpha=0.3)
    
    # Add value labels on bars
    for bar, retention in zip(bars, retention_percentages):
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height + 2,
                f'{retention:.0f}%', ha='center', va='bottom', fontsize=8)
    
    # Bottom right: Summary metrics and recommendation
    ax3 = fig.add_subplot(3, 3, 9)
    ax3.axis('off')
    
    # Calculate summary metrics
    total_var_retention = quantized_var[:3].sum() / original_var[:3].sum() * 100
    original_separation = calculate_class_separation_score(original_features, original_labels)
    quantized_separation = calculate_class_separation_score(quantized_features, quantized_labels)
    separation_retention = (quantized_separation / original_separation * 100) if original_separation > 0 else 100
    
    # Determine recommendation
    if separation_retention >= 90 and total_var_retention >= 90:
        recommendation = "✓ QUANTIZED\nRECOMMENDED"
        rec_color = 'green'
    elif separation_retention >= 80 and total_var_retention >= 80:
        recommendation = "⚠ QUANTIZED\nACCEPTABLE"
        rec_color = 'orange'
    elif separation_retention >= 70 or total_var_retention >= 70:
        recommendation = "⚠ CONSIDER\nTRADE-OFFS"
        rec_color = 'orange'
    else:
        recommendation = "✗ ORIGINAL\nRECOMMENDED"
        rec_color = 'red'
    
    # Create text summary
    summary_text = f"""QUANTIZATION IMPACT SUMMARY
    
Dataset: {model_name.upper()}
Classes: {len(np.unique(original_labels))}
Features: {original_features.shape[1]}

KEY METRICS:
• Total Variance Retention: {total_var_retention:.1f}%
• Class Separation Retention: {separation_retention:.1f}%
• PC1 Retention: {retention_percentages[0]:.1f}%

RECOMMENDATION:
"""
    
    ax3.text(0.05, 0.95, summary_text, transform=ax3.transAxes, fontsize=9,
             verticalalignment='top', fontfamily='monospace')
    
    # Add recommendation with color
    ax3.text(0.5, 0.15, recommendation, transform=ax3.transAxes, fontsize=12,
             verticalalignment='center', horizontalalignment='center',
             bbox=dict(boxstyle="round,pad=0.3", facecolor=rec_color, alpha=0.3),
             weight='bold')
    
    return ax1, ax2, ax3


def calculate_class_separation_score(features, labels):
    """
    Calculate a score indicating how well-separated the classes are.
    Uses the ratio of between-class to within-class scatter.
    
    Returns:
        separation_score: Higher values indicate better separation
    """
    from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
    
    try:
        # Use LDA to calculate separation score
        lda = LinearDiscriminantAnalysis()
        lda.fit(features, labels)
        
        # Calculate the score based on explained variance ratios
        explained_variance_ratio = lda.explained_variance_ratio_
        separation_score = np.sum(explained_variance_ratio)
        
        return separation_score
    except:
        # Fallback: calculate simple inter/intra class distance ratio
        unique_labels = np.unique(labels)
        if len(unique_labels) < 2:
            return 0.0
        
        # Calculate centroids
        centroids = []
        for label in unique_labels:
            class_data = features[labels == label]
            centroids.append(np.mean(class_data, axis=0))
        centroids = np.array(centroids)
        
        # Inter-class distances
        inter_distances = []
        for i in range(len(centroids)):
            for j in range(i+1, len(centroids)):
                inter_distances.append(np.linalg.norm(centroids[i] - centroids[j]))
        mean_inter_distance = np.mean(inter_distances) if inter_distances else 0
        
        # Intra-class distances
        intra_distances = []
        for label in unique_labels:
            class_data = features[labels == label]
            if len(class_data) > 1:
                centroid = np.mean(class_data, axis=0)
                for point in class_data:
                    intra_distances.append(np.linalg.norm(point - centroid))
        mean_intra_distance = np.mean(intra_distances) if intra_distances else 1
        
        # Return ratio (higher is better for classification)
        separation_score = mean_inter_distance / (mean_intra_distance + 1e-10)
        return separation_score
    """
    Calculate a score indicating how well-separated the classes are.
    Uses the ratio of between-class to within-class scatter.
    
    Returns:
        separation_score: Higher values indicate better separation
    """
    from sklearn.discriminant_analysis import LinearDiscriminantAnalysis
    
    try:
        # Use LDA to calculate separation score
        lda = LinearDiscriminantAnalysis()
        lda.fit(features, labels)
        
        # Calculate the score based on explained variance ratios
        explained_variance_ratio = lda.explained_variance_ratio_
        separation_score = np.sum(explained_variance_ratio)
        
        return separation_score
    except:
        # Fallback: calculate simple inter/intra class distance ratio
        unique_labels = np.unique(labels)
        if len(unique_labels) < 2:
            return 0.0
        
        # Calculate centroids
        centroids = []
        for label in unique_labels:
            class_data = features[labels == label]
            centroids.append(np.mean(class_data, axis=0))
        centroids = np.array(centroids)
        
        # Inter-class distances
        inter_distances = []
        for i in range(len(centroids)):
            for j in range(i+1, len(centroids)):
                inter_distances.append(np.linalg.norm(centroids[i] - centroids[j]))
        mean_inter_distance = np.mean(inter_distances) if inter_distances else 0
        
        # Intra-class distances
        intra_distances = []
        for label in unique_labels:
            class_data = features[labels == label]
            if len(class_data) > 1:
                centroid = np.mean(class_data, axis=0)
                for point in class_data:
                    intra_distances.append(np.linalg.norm(point - centroid))
        mean_intra_distance = np.mean(intra_distances) if intra_distances else 1
        
        # Return ratio (higher is better for classification)
        separation_score = mean_inter_distance / (mean_intra_distance + 1e-10)
        return separation_score


def print_pca_info(original_var, quantized_var, original_features, quantized_features, original_labels, quantized_labels):
    """Print comprehensive PCA variance information and classification analysis."""
    print("\n" + "="*80)
    print("PCA ANALYSIS & CLASSIFICATION QUALITY ASSESSMENT")
    print("="*80)
    
    # Explained variance analysis
    print("EXPLAINED VARIANCE ANALYSIS:")
    print("-" * 40)
    print("Explained variance represents the proportion of the dataset's total variation")
    print("captured by each principal component. Higher percentages mean more information")
    print("is preserved in fewer dimensions.\n")
    
    print(f"Original Data - Explained Variance:")
    print(f"  PC1: {original_var[0]*100:.1f}% (most important variation direction)")
    print(f"  PC2: {original_var[1]*100:.1f}% (second most important direction)")
    print(f"  PC3: {original_var[2]*100:.1f}% (third most important direction)")
    print(f"  Total (3 PCs): {original_var[:3].sum()*100:.1f}% (total info preserved in 3D)")
    
    print(f"\nQuantized Data - Explained Variance:")
    print(f"  PC1: {quantized_var[0]*100:.1f}%")
    print(f"  PC2: {quantized_var[1]*100:.1f}%")
    print(f"  PC3: {quantized_var[2]*100:.1f}%")
    print(f"  Total (3 PCs): {quantized_var[:3].sum()*100:.1f}%")
    
    print(f"\nVariance Retention after Quantization:")
    for i in range(3):
        retention = (quantized_var[i] / original_var[i]) * 100 if original_var[i] > 0 else 100
        print(f"  PC{i+1}: {retention:.1f}%")
    
    # Classification quality assessment
    print("\n" + "-" * 40)
    print("CLASSIFICATION QUALITY ASSESSMENT:")
    print("-" * 40)
    
    original_separation = calculate_class_separation_score(original_features, original_labels)
    quantized_separation = calculate_class_separation_score(quantized_features, quantized_labels)
    
    print(f"Class Separation Score (higher = better for classification):")
    print(f"  Original Data:  {original_separation:.3f}")
    print(f"  Quantized Data: {quantized_separation:.3f}")
    
    separation_retention = (quantized_separation / original_separation * 100) if original_separation > 0 else 100
    print(f"  Separation Retention: {separation_retention:.1f}%")
    
    # Recommendation
    print("\n" + "-" * 40)
    print("RECOMMENDATION FOR CLASSIFICATION:")
    print("-" * 40)
    
    total_var_retention = quantized_var[:3].sum() / original_var[:3].sum() * 100
    
    if separation_retention >= 90 and total_var_retention >= 90:
        recommendation = "✓ QUANTIZED version recommended - minimal quality loss"
    elif separation_retention >= 80 and total_var_retention >= 80:
        recommendation = "⚠ QUANTIZED version acceptable - moderate quality loss"
    elif separation_retention >= 70 or total_var_retention >= 70:
        recommendation = "⚠ Consider trade-offs - significant quality loss but still usable"
    else:
        recommendation = "✗ ORIGINAL version recommended - substantial quality loss in quantized"
    
    print(recommendation)
    
    # Interpretation guide
    print("\n" + "-" * 40)
    print("INTERPRETATION GUIDE:")
    print("-" * 40)
    print("• Explained Variance: Shows how much information each PC captures")
    print("• Higher PC1 percentage = data varies mainly along one direction")
    print("• More balanced PC percentages = data varies in multiple directions")
    print("• Class Separation Score: Measures how distinguishable classes are")
    print("• Retention > 90% = excellent preservation")
    print("• Retention 80-90% = good preservation")
    print("• Retention < 80% = significant information loss")
    
    print("="*80)


def get_class_names(model_name, label_mapping=None):
    """
    Get appropriate class names based on the dataset.
    
    Args:
        model_name: Name of the dataset/model
        label_mapping: Optional dict mapping numeric labels to original string labels
    
    Returns:
        List of class names, or None if not available
    """
    # If we have a label mapping from the original data, use it
    if label_mapping is not None:
        # Sort by numeric label to maintain order
        sorted_labels = sorted(label_mapping.items())
        return [label for _, label in sorted_labels]
    
    # Otherwise, use predefined mappings
    class_name_mapping = {
        'iris': ['Setosa', 'Versicolor', 'Virginica'],
        'cancer': ['Malignant', 'Benign'],
        'digit': [f'Digit {i}' for i in range(10)],
        'walker_fall': ['Idle', 'Fall', 'Step', 'Motion']  # Updated to 4 classes
    }
    return class_name_mapping.get(model_name, None)


def main():
    parser = argparse.ArgumentParser(description='Visualize data quantization effects using PCA')
    parser.add_argument('model_name', help='Name of the model/dataset (without .csv extension)')
    parser.add_argument('--original', help='Path to original CSV file')
    parser.add_argument('--quantized', help='Path to quantized CSV file')
    args = parser.parse_args()
    
    model_name = args.model_name
    
    # Define file paths
    original_file = args.original if args.original else f'data/{model_name}.csv'
    quantized_file = args.quantized if args.quantized else f'data/result/{model_name}_nml.csv'
    
    print(f"Loading original data from: {original_file}")
    print(f"Loading quantized data from: {quantized_file}")
    
    # Load data - only the original file may have string labels
    original_features, original_labels, label_mapping = load_data(original_file)
    quantized_features, quantized_labels, _ = load_data(quantized_file)
    
    if original_features is None or quantized_features is None:
        sys.exit(1)
    
    # Verify data consistency
    if len(original_features) != len(quantized_features):
        print("Warning: Original and quantized datasets have different sizes!")
    
    print(f"\nDataset: {model_name}")
    print(f"Original data shape: {original_features.shape}")
    print(f"Quantized data shape: {quantized_features.shape}")
    print(f"Number of classes: {len(np.unique(original_labels))}")
    
    # Get class names - prefer label mapping from original data
    class_names = get_class_names(model_name, label_mapping)
    if class_names:
        print(f"Class names: {class_names}")
    else:
        print("Class names: Using default numbering")
    
    # Create visualization with 3x3 layout
    fig = plt.figure(figsize=(18, 14))
    fig.suptitle(f'PCA Visualization: {model_name.title()} Dataset - Quantization Impact Analysis', 
                 fontsize=16, y=0.95)
    
    # Define viewing angles for better perspective (reduced to 3 views)
    view_angles = [
        (-150, 110, "Standard View"),
        (20, 45, "Side View"),
        (-90, 0, "Top-front View")
    ]
    
    # Row 1: Original data (3 different views)
    original_vars = []
    for i, (elev, azim, view_name) in enumerate(view_angles):
        ax = fig.add_subplot(3, 3, i+1, projection='3d')
        original_var = create_pca_plot(original_features, original_labels, 
                                      f'Original Data - {view_name}', ax, class_names, 
                                      view_angle=(elev, azim))
        original_vars.append(original_var)
    
    # Row 2: Quantized data (3 matching views)
    quantized_vars = []
    for i, (elev, azim, view_name) in enumerate(view_angles):
        ax = fig.add_subplot(3, 3, i+4, projection='3d')
        quantized_var = create_pca_plot(quantized_features, quantized_labels, 
                                       f'Quantized Data - {view_name}', ax, class_names,
                                       view_angle=(elev, azim))
        quantized_vars.append(quantized_var)
    
    # Row 3: Quantization impact assessment (3 assessment plots)
    create_quantization_assessment_plots(original_vars[0], quantized_vars[0], 
                                       original_features, quantized_features,
                                       original_labels, quantized_labels, fig, model_name)
    
    # Adjust layout
    plt.tight_layout(rect=[0, 0.02, 1, 0.93])
    
    # Print comprehensive analysis (using first view's variance data)
    print_pca_info(original_vars[0], quantized_vars[0], original_features, quantized_features,
                   original_labels, quantized_labels)
    
    # Save plot
    output_dir = 'plots'
    os.makedirs(output_dir, exist_ok=True)
    output_file = f'{output_dir}/{model_name}_pca_comparison.png'
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nPlot saved as: {output_file}")
    
    # Show plot
    plt.show()


if __name__ == '__main__':
    main()
