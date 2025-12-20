import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import json
import os
import numpy as np
from sklearn.decomposition import PCA
from scipy.stats import wasserstein_distance, skew
from scipy.spatial.distance import euclidean

def load_config(config_path):
    with open(config_path, 'r') as f:
        return json.load(f)

def hellinger_distance(p, q):
    """Calculates Hellinger distance between two discrete distributions."""
    return np.sqrt(1 - np.sum(np.sqrt(p * q)))

def plot_distance_over_time(df, config, output_dir):
    """2. Distribution Distance / Divergence over Time"""
    name = config['name']
    burn_in = config['burn_in']
    window_size = 200
    step = 50
    
    features = df.columns[:-1]
    ref_len = max(100, burn_in)
    ref_data = df.iloc[:ref_len][features]
    
    distances = []
    indices = []
    
    for start in range(0, len(df) - window_size, step):
        current_window = df.iloc[start : start + window_size][features]
        
        # Wasserstein Distance (average over features)
        w_dist = np.mean([wasserstein_distance(ref_data[col], current_window[col]) for col in features])
        
        # Hellinger Distance (proxy using histograms)
        h_dists = []
        for col in features:
            bins = np.histogram_bin_edges(df[col], bins=20)
            p, _ = np.histogram(ref_data[col], bins=bins, density=True)
            q, _ = np.histogram(current_window[col], bins=bins, density=True)
            # Normalize to sum to 1 for Hellinger
            p = p / (p.sum() + 1e-10)
            q = q / (q.sum() + 1e-10)
            h_dists.append(hellinger_distance(p, q))
        h_dist = np.mean(h_dists)
        
        distances.append({'wasserstein': w_dist, 'hellinger': h_dist})
        indices.append(start + window_size // 2)
    
    dist_df = pd.DataFrame(distances, index=indices)
    
    plt.figure(figsize=(12, 6))
    plt.plot(dist_df['wasserstein'], label='Wasserstein Distance', marker='o', markersize=4)
    plt.plot(dist_df['hellinger'], label='Hellinger Distance', marker='s', markersize=4)
    
    plt.axvline(x=burn_in, color='r', linestyle='--', label='Drift Point')
    if config.get('type') == 'gradual':
        plt.axvspan(burn_in, burn_in + config.get('drift_width', 100), color='gray', alpha=0.2, label='Drift Window')
    
    plt.title(f'Distribution Divergence over Time - {name}')
    plt.xlabel('Instance Index')
    plt.ylabel('Distance Metric')
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, f'{name}_dist_divergence.png'))
    plt.close()

def plot_geometry_drift(df, config, output_dir):
    """3. Feature Space Geometry Drift"""
    name = config['name']
    burn_in = config['burn_in']
    features = df.columns[:-1]
    
    # Centroid Distance over Time
    ref_len = max(100, burn_in)
    ref_centroid = df.iloc[:ref_len][features].mean().values
    window_size = 100
    centroid_dists = []
    indices = []
    
    for start in range(0, len(df) - window_size, 20):
        cur_centroid = df.iloc[start : start + window_size][features].mean().values
        dist = euclidean(ref_centroid, cur_centroid)
        centroid_dists.append(dist)
        indices.append(start + window_size // 2)
        
    plt.figure(figsize=(12, 5))
    plt.plot(indices, centroid_dists, color='purple', label='||μ_t - μ_ref||')
    plt.axvline(x=burn_in, color='r', linestyle='--')
    plt.title(f'Centroid Drift - {name}')
    plt.xlabel('Time (Instance Index)')
    plt.ylabel('Euclidean Distance')
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, f'{name}_centroid_drift.png'))
    plt.close()

    # PCA Projection with Time Color (3D)
    n_comp = min(3, len(features))
    pca = PCA(n_components=n_comp)
    pca_result = pca.fit_transform(df[features])
    
    fig = plt.figure(figsize=(12, 10))
    if n_comp == 3:
        ax = fig.add_subplot(111, projection='3d')
        sc = ax.scatter(pca_result[:, 0], pca_result[:, 1], pca_result[:, 2], 
                        c=range(len(df)), cmap='viridis', s=10, alpha=0.6)
        ax.set_zlabel('PC3')
    else:
        ax = fig.add_subplot(111)
        sc = ax.scatter(pca_result[:, 0], pca_result[:, 1], 
                        c=range(len(df)), cmap='viridis', s=10, alpha=0.6)
    
    plt.colorbar(sc, ax=ax, label='Time (Instance Index)')
    
    # Mark Centroids
    ref_pca = pca.transform(ref_centroid.reshape(1, -1))
    after_centroid = df.iloc[burn_in:][features].mean().values
    after_pca = pca.transform(after_centroid.reshape(1, -1))
    
    if n_comp == 3:
        ax.scatter(ref_pca[0, 0], ref_pca[0, 1], ref_pca[0, 2], 
                   color='red', marker='X', s=200, label='Ref Centroid', edgecolors='black')
        ax.scatter(after_pca[0, 0], after_pca[0, 1], after_pca[0, 2], 
                   color='cyan', marker='X', s=200, label='Post-Drift Centroid', edgecolors='black')
    else:
        ax.scatter(ref_pca[0, 0], ref_pca[0, 1], 
                   color='red', marker='X', s=200, label='Ref Centroid', edgecolors='black')
        ax.scatter(after_pca[0, 0], after_pca[0, 1], 
                   color='cyan', marker='X', s=200, label='Post-Drift Centroid', edgecolors='black')
    
    ax.set_title(f'{n_comp}D PCA Projection (Color=Time) - {name}')
    ax.set_xlabel('PC1')
    ax.set_ylabel('PC2')
    ax.legend()
    plt.savefig(os.path.join(output_dir, f'{name}_pca_time.png'))
    plt.close()

def plot_label_distribution_over_time(df, config, output_dir):
    """4. Label Distribution over Time (Line Graph)"""
    name = config['name']
    burn_in = config['burn_in']
    window_size = 200
    step = 50
    
    labels = sorted(df['class'].unique())
    history = {label: [] for label in labels}
    indices = []
    
    for start in range(0, len(df) - window_size, step):
        window = df.iloc[start : start + window_size]['class']
        counts = window.value_counts(normalize=True)
        for label in labels:
            history[label].append(counts.get(label, 0.0))
        indices.append(start + window_size // 2)
        
    plt.figure(figsize=(12, 6))
    for label in labels:
        plt.plot(indices, history[label], label=f'Class {label}', marker='o', markersize=3)
        
    plt.axvline(x=burn_in, color='r', linestyle='--', label='Drift Point')
    if config.get('type') == 'gradual':
        plt.axvspan(burn_in, burn_in + config.get('drift_width', 100), color='gray', alpha=0.2, label='Drift Window')
        
    plt.title(f'Label Distribution Shift over Time - {name}')
    plt.xlabel('Instance Index')
    plt.ylabel('Relative Frequency')
    plt.ylim(-0.05, 1.05)
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(os.path.join(output_dir, f'{name}_label_dist_time.png'))
    plt.close()

def plot_drift(csv_path, config, output_dir):
    df = pd.read_csv(csv_path)
    name = config['name']
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"Generating advanced plots for {name}...")
    plot_distance_over_time(df, config, output_dir)
    plot_geometry_drift(df, config, output_dir)
    plot_label_distribution_over_time(df, config, output_dir)
    
    print(f"Visualizations saved to {output_dir}")

if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(base_dir, "parameters.json")
    configs = load_config(config_path)
    
    if isinstance(configs, dict):
        configs = [configs]
        
    output_dir = os.path.join(base_dir, "drift_concept_visualize")
    
    for config in configs:
        csv_path = os.path.join(base_dir, "datasets", f"{config['name']}.csv")
        if os.path.exists(csv_path):
            plot_drift(csv_path, config, output_dir)
        else:
            print(f"Warning: Dataset {csv_path} not found.")
