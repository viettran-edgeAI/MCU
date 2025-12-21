import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import json
import os
import numpy as np
from sklearn.decomposition import PCA
from scipy.stats import wasserstein_distance, skew
from scipy.spatial.distance import euclidean
import glob

def load_config(config_path):
    with open(config_path, 'r') as f:
        configs = json.load(f)
        # Filter out comment-only entries (where name starts with '_')
        if isinstance(configs, list):
            return [c for c in configs if not c.get('name', '').startswith('_')]
        return configs

def hellinger_distance(p, q):
    """Calculates Hellinger distance between two discrete distributions."""
    return np.sqrt(1 - np.sum(np.sqrt(p * q)))

def plot_distance_over_time(ax, df, config):
    """1. Distribution Distance / Divergence over Time"""
    name = config['name']
    burn_in = config.get('burn_in', 0)
    drift_type = config.get('type', 'abrupt')
    
    # Get feature columns (all except 'label' or 'class')
    label_col = 'label' if 'label' in df.columns else 'class'
    features = [col for col in df.columns if col != label_col]
    
    # Adjust window size based on dataset size
    window_size = min(200, len(df) // 10)
    step = max(20, window_size // 4)
    
    # Reference data (before drift)
    if burn_in > 0:
        ref_len = min(burn_in, len(df) // 4)
    else:
        ref_len = min(500, len(df) // 4)
    ref_data = df.iloc[:ref_len][features]
    
    distances = []
    indices = []
    
    for start in range(0, len(df) - window_size, step):
        current_window = df.iloc[start : start + window_size][features]
        
        # Wasserstein Distance (average over features)
        try:
            w_dist = np.mean([wasserstein_distance(ref_data[col], current_window[col]) for col in features])
        except:
            w_dist = 0.0
        
        # Hellinger Distance (proxy using histograms)
        h_dists = []
        for col in features:
            try:
                bins = np.histogram_bin_edges(df[col], bins=20)
                p, _ = np.histogram(ref_data[col], bins=bins, density=True)
                q, _ = np.histogram(current_window[col], bins=bins, density=True)
                # Normalize to sum to 1 for Hellinger
                p = p / (p.sum() + 1e-10)
                q = q / (q.sum() + 1e-10)
                h_dists.append(hellinger_distance(p, q))
            except:
                h_dists.append(0.0)
        h_dist = np.mean(h_dists) if h_dists else 0.0
        
        distances.append({'wasserstein': w_dist, 'hellinger': h_dist})
        indices.append(start + window_size // 2)
    
    if not distances:
        ax.text(0.5, 0.5, 'Insufficient data', ha='center', va='center', transform=ax.transAxes)
        return
    
    dist_df = pd.DataFrame(distances, index=indices)
    
    ax.plot(dist_df['wasserstein'], label='Wasserstein Distance', marker='o', markersize=3)
    ax.plot(dist_df['hellinger'], label='Hellinger Distance', marker='s', markersize=3)
    
    # Mark drift points based on type
    if drift_type == 'gradual':
        drift_width = config.get('drift_width', 500)
        drift_end = min(burn_in + drift_width, len(df))
        
        # Create gradient effect for gradual drift using multiple overlapping spans
        n_segments = 20
        segment_width = (drift_end - burn_in) / n_segments
        for i in range(n_segments):
            segment_start = burn_in + i * segment_width
            segment_end = segment_start + segment_width
            # Alpha increases from 0.05 to 0.3 across the drift region
            alpha = 0.05 + (i / n_segments) * 0.25
            ax.axvspan(segment_start, segment_end, color='orange', alpha=alpha, linewidth=0)
        
        # Mark start and end of gradual drift
        ax.axvline(x=burn_in, color='darkorange', linestyle='--', linewidth=2, 
                   label='Drift Start', alpha=0.8)
        ax.axvline(x=drift_end, color='darkred', linestyle='--', linewidth=2, 
                   label='Drift End', alpha=0.8)
    elif drift_type == 'abrupt':
        if burn_in > 0:
            ax.axvline(x=burn_in, color='r', linestyle='--', linewidth=2, label='Drift Point')
        else:
            # Mark start for datasets with immediate drift
            ax.axvline(x=0, color='r', linestyle='--', linewidth=2, label='Drift at Start')
    elif drift_type == 'sudden':
        num_points = config.get('num_drift_points', 5)
        step_size = max(1, (len(df) - burn_in) // num_points)
        for i in range(num_points):
            drift_point = burn_in + i * step_size
            if drift_point < len(df):
                if i == 0:
                    ax.axvline(x=drift_point, color='darkorange', linestyle='--', 
                              linewidth=2, label='Sudden Drifts', alpha=0.8)
                else:
                    ax.axvline(x=drift_point, color='orange', linestyle='--', 
                              linewidth=1.5, alpha=0.6)
    elif drift_type == 'recurrent':
        num_points = config.get('num_drift_points', 10)
        cycle_length = max(1, (len(df) - burn_in) // num_points)
        for i in range(num_points + 1):
            drift_point = burn_in + i * cycle_length
            if drift_point < len(df):
                if i == 0:
                    ax.axvline(x=drift_point, color='purple', linestyle='--', 
                              linewidth=2, label='Recurrent Drifts', alpha=0.8)
                else:
                    # Alternate colors for forward/backward phases
                    color = 'mediumpurple' if i % 2 == 0 else 'plum'
                    ax.axvline(x=drift_point, color=color, linestyle=':', 
                              linewidth=1.5, alpha=0.5)
    
    ax.set_title(f'Distribution Divergence ({drift_type.capitalize()})')
    ax.set_xlabel('Instance Index')
    ax.set_ylabel('Distance Metric')
    ax.legend(fontsize=8, loc='best')
    ax.grid(True, alpha=0.3)

def plot_centroid_drift(ax, df, config):
    """2. Centroid Distance over Time"""
    name = config['name']
    burn_in = config.get('burn_in', 0)
    
    # Get feature columns
    label_col = 'label' if 'label' in df.columns else 'class'
    features = [col for col in df.columns if col != label_col]
    
    # Reference centroid
    if burn_in > 0:
        ref_len = min(burn_in, len(df) // 4)
    else:
        ref_len = min(500, len(df) // 4)
    ref_centroid = df.iloc[:ref_len][features].mean().values
    
    window_size = min(100, len(df) // 10)
    step = max(10, window_size // 5)
    centroid_dists = []
    indices = []
    
    for start in range(0, len(df) - window_size, step):
        try:
            cur_centroid = df.iloc[start : start + window_size][features].mean().values
            dist = euclidean(ref_centroid, cur_centroid)
            centroid_dists.append(dist)
            indices.append(start + window_size // 2)
        except:
            pass
    
    if not centroid_dists:
        ax.text(0.5, 0.5, 'Insufficient data', ha='center', va='center', transform=ax.transAxes)
        return
        
    ax.plot(indices, centroid_dists, color='purple', linewidth=2, label='||μ_t - μ_ref||')
    
    # Mark drift regions
    drift_type = config.get('type', 'abrupt')
    if drift_type == 'gradual':
        drift_width = config.get('drift_width', 500)
        drift_end = min(burn_in + drift_width, len(df))
        # Gradient shading for gradual drift
        n_segments = 15
        segment_width = (drift_end - burn_in) / n_segments
        for i in range(n_segments):
            segment_start = burn_in + i * segment_width
            segment_end = segment_start + segment_width
            alpha = 0.05 + (i / n_segments) * 0.2
            ax.axvspan(segment_start, segment_end, color='orange', alpha=alpha, linewidth=0)
        ax.axvline(x=burn_in, color='darkorange', linestyle='--', linewidth=1.5, alpha=0.7)
        ax.axvline(x=drift_end, color='darkred', linestyle='--', linewidth=1.5, alpha=0.7)
    elif burn_in > 0:
        ax.axvline(x=burn_in, color='r', linestyle='--', linewidth=2, label='Drift Start')
    elif burn_in == 0:
        ax.axvline(x=0, color='r', linestyle='--', linewidth=2, label='Drift at Start')
    
    boundary = config.get('boundary_type', 'unknown')
    ax.set_title(f'Centroid Drift ({boundary.capitalize()})')
    ax.set_xlabel('Time (Instance Index)')
    ax.set_ylabel('Euclidean Distance')
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

def plot_pca_projection(ax, df, config):
    """3. PCA Projection with Time Color"""
    name = config['name']
    burn_in = config.get('burn_in', 0)
    
    # Get feature columns
    label_col = 'label' if 'label' in df.columns else 'class'
    features = [col for col in df.columns if col != label_col]
    
    # Subsample for large datasets to improve performance
    if len(df) > 2000:
        df_sample = df.sample(n=2000, random_state=42).sort_index()
    else:
        df_sample = df
    
    if burn_in > 0:
        ref_len = min(burn_in, len(df) // 4)
    else:
        ref_len = min(500, len(df) // 4)
    ref_centroid = df.iloc[:ref_len][features].mean().values

    # PCA Projection with Time Color (2D for combined plot)
    try:
        n_comp = min(2, len(features))
        pca = PCA(n_components=n_comp)
        pca_result = pca.fit_transform(df_sample[features])
        
        # Create time-based colors
        time_colors = np.array(df_sample.index) / len(df)
        
        sc = ax.scatter(pca_result[:, 0], pca_result[:, 1], 
                        c=time_colors, cmap='viridis', s=15, alpha=0.6, edgecolors='none')
        
        cbar = plt.colorbar(sc, ax=ax, label='Time Progress')
        cbar.ax.tick_params(labelsize=8)
        
        # Mark Centroids
        ref_pca = pca.transform(ref_centroid.reshape(1, -1))
        
        if burn_in > 0 and burn_in < len(df):
            after_centroid = df.iloc[burn_in:][features].mean().values
            after_pca = pca.transform(after_centroid.reshape(1, -1))
            
            ax.scatter(ref_pca[0, 0], ref_pca[0, 1], 
                       color='red', marker='X', s=150, label='Pre-Drift μ', 
                       edgecolors='white', linewidths=1.5, zorder=5)
            ax.scatter(after_pca[0, 0], after_pca[0, 1], 
                       color='cyan', marker='X', s=150, label='Post-Drift μ', 
                       edgecolors='white', linewidths=1.5, zorder=5)
        else:
            ax.scatter(ref_pca[0, 0], ref_pca[0, 1], 
                       color='red', marker='X', s=150, label='Initial μ', 
                       edgecolors='white', linewidths=1.5, zorder=5)
        
        var_exp = pca.explained_variance_ratio_
        ax.set_xlabel(f'PC1 ({var_exp[0]:.1%} var)', fontsize=9)
        if n_comp > 1:
            ax.set_ylabel(f'PC2 ({var_exp[1]:.1%} var)', fontsize=9)
        else:
            ax.set_ylabel('PC2', fontsize=9)
        
        ax.set_title(f'PCA Feature Space (n={len(features)})')
        ax.legend(fontsize=8, loc='best')
        ax.grid(True, alpha=0.2)
        
        # Add drift region visualization in PCA space
        drift_type = config.get('type', 'abrupt')
        if drift_type == 'gradual':
            drift_width = config.get('drift_width', 500)
            drift_end = min(burn_in + drift_width, len(df))
            # Subtle background gradient
            ax.axvspan(burn_in, drift_end, color='yellow', alpha=0.08, linewidth=0)
        
    except Exception as e:
        ax.text(0.5, 0.5, f'PCA Error: {str(e)[:50]}', 
                ha='center', va='center', transform=ax.transAxes, fontsize=8)

def plot_label_distribution_over_time(ax, df, config):
    """4. Label Distribution over Time (Line Graph)"""
    name = config['name']
    burn_in = config.get('burn_in', 0)
    drift_type = config.get('type', 'abrupt')
    
    # Determine label column
    label_col = 'label' if 'label' in df.columns else 'class'
    
    window_size = min(200, len(df) // 10)
    step = max(20, window_size // 4)
    
    labels = sorted(df[label_col].unique())
    num_labels = len(labels)
    
    # Use distinct colors for up to 10 classes
    colors = plt.cm.tab10(np.linspace(0, 1, min(10, num_labels)))
    
    history = {label: [] for label in labels}
    indices = []
    
    for start in range(0, len(df) - window_size, step):
        window = df.iloc[start : start + window_size][label_col]
        counts = window.value_counts(normalize=True)
        for label in labels:
            history[label].append(counts.get(label, 0.0))
        indices.append(start + window_size // 2)
    
    if not indices:
        ax.text(0.5, 0.5, 'Insufficient data', ha='center', va='center', transform=ax.transAxes)
        return
        
    for i, label in enumerate(labels):
        color = colors[i % len(colors)]
        ax.plot(indices, history[label], label=f'Class {label}', 
                marker='o', markersize=2, linewidth=2, color=color)
    
    # Mark drift points with gradient for gradual drift
    if drift_type == 'gradual':
        drift_width = config.get('drift_width', 500)
        drift_end = min(burn_in + drift_width, len(df))
        # Gradient shading
        n_segments = 15
        segment_width = (drift_end - burn_in) / n_segments
        for i in range(n_segments):
            segment_start = burn_in + i * segment_width
            segment_end = segment_start + segment_width
            alpha = 0.05 + (i / n_segments) * 0.2
            ax.axvspan(segment_start, segment_end, color='orange', alpha=alpha, linewidth=0)
        ax.axvline(x=burn_in, color='darkorange', linestyle='--', linewidth=1.5, 
                   label='Drift Start', alpha=0.7)
        ax.axvline(x=drift_end, color='darkred', linestyle='--', linewidth=1.5, 
                   label='Drift End', alpha=0.7)
    elif burn_in > 0:
        ax.axvline(x=burn_in, color='r', linestyle='--', linewidth=2, label='Drift Start')
    elif burn_in == 0:
        ax.axvline(x=0, color='r', linestyle='--', linewidth=2, label='Drift at Start')
        
    ax.set_title(f'Label Distribution Over Time (n={num_labels} classes)')
    ax.set_xlabel('Instance Index')
    ax.set_ylabel('Relative Frequency')
    ax.set_ylim(-0.05, 1.05)
    ax.legend(fontsize=8, loc='best', ncol=max(1, num_labels // 4))
    ax.grid(True, alpha=0.3)

def plot_drift(csv_path, config, output_dir):
    """Generate combined 4-panel drift analysis plot"""
    try:
        df = pd.read_csv(csv_path)
        name = config['name']
        os.makedirs(output_dir, exist_ok=True)
        
        # Detect label column
        if 'label' not in df.columns and 'class' not in df.columns:
            # Try to find last column as label
            df.rename(columns={df.columns[-1]: 'label'}, inplace=True)
        
        print(f"Generating combined drift plot for {name}...")
        print(f"  Shape: {df.shape}, Features: {len([c for c in df.columns if c not in ['label', 'class']])}, "
              f"Labels: {df['label' if 'label' in df.columns else 'class'].nunique()}")
        
        # Create figure with 4 subplots
        fig, axes = plt.subplots(2, 2, figsize=(18, 12))
        
        # Generate title with configuration info
        drift_type = config.get('type', 'unknown')
        boundary = config.get('boundary_type', 'unknown')
        title = f"Concept Drift Analysis: {name}\n"
        title += f"Type: {drift_type.capitalize()}, Boundary: {boundary.capitalize()}, "
        title += f"Samples: {config.get('n_instances', len(df))}, Burn-in: {config.get('burn_in', 0)}"
        fig.suptitle(title, fontsize=16, fontweight='bold')
        
        # Generate all 4 plots
        plot_distance_over_time(axes[0, 0], df, config)
        plot_centroid_drift(axes[0, 1], df, config)
        plot_pca_projection(axes[1, 0], df, config)
        plot_label_distribution_over_time(axes[1, 1], df, config)
        
        plt.tight_layout(rect=[0, 0.03, 1, 0.96])
        
        # Save with descriptive filename
        output_file = os.path.join(output_dir, f'{name}_combined_analysis.png')
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        plt.close()
        
        print(f"  ✓ Saved: {output_file}")
        
    except Exception as e:
        print(f"  ✗ Error processing {csv_path}: {str(e)}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    base_dir = os.path.dirname(os.path.abspath(__file__))
    config_path = os.path.join(base_dir, "parameters.json")
    
    print("=" * 60)
    print("Concept Drift Visualization Tool")
    print("=" * 60)
    
    configs = load_config(config_path)
    
    if isinstance(configs, dict):
        configs = [configs]
    
    print(f"Loaded {len(configs)} dataset configuration(s)")
    print()
        
    output_dir = os.path.join(base_dir, "drift_concept_visualize")
    os.makedirs(output_dir, exist_ok=True)
    
    datasets_dir = os.path.join(base_dir, "datasets")
    
    # Process each configuration
    processed = 0
    skipped = 0
    
    for config in configs:
        name = config.get('name', 'unknown')
        
        # Find matching CSV file (with various suffixes)
        csv_candidates = [
            os.path.join(datasets_dir, f"{name}.csv"),
            os.path.join(datasets_dir, f"{name}_{config.get('type', 'abrupt')}_noise.csv"),
            os.path.join(datasets_dir, f"{name}_{config.get('type', 'abrupt')}_noise_redundant.csv"),
        ]
        
        # Also check for any file starting with the name
        pattern = os.path.join(datasets_dir, f"{name}*.csv")
        csv_candidates.extend(glob.glob(pattern))
        
        csv_path = None
        for candidate in csv_candidates:
            if os.path.exists(candidate):
                csv_path = candidate
                break
        
        if csv_path:
            plot_drift(csv_path, config, output_dir)
            processed += 1
        else:
            print(f"⚠ Warning: No dataset found for '{name}'")
            print(f"  Tried: {name}.csv, {name}_*.csv")
            skipped += 1
    
    print()
    print("=" * 60)
    print(f"Complete: {processed} visualizations generated, {skipped} skipped")
    print(f"Output directory: {output_dir}")
    print("=" * 60)
