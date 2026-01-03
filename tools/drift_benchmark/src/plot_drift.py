import pandas as pd
import matplotlib.pyplot as plt
import argparse
import json

def plot_drift(csv_path, config_path):
    # Load results
    df = pd.read_csv(csv_path)
    
    # Load config to get drift point
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    drift_point = config.get('drift_point', 1000)
    metric = config.get('metric', 'accuracy')
    
    plt.figure(figsize=(12, 6))
    
    # Plot metrics
    if metric == 'all':
        plt.plot(df['window_start'], df['accuracy'], label='Accuracy')
        plt.plot(df['window_start'], df['f1'], label='F1 Score')
    else:
        if metric in df.columns:
            plt.plot(df['window_start'], df[metric], label=metric.capitalize())
        else:
            plt.plot(df['window_start'], df['accuracy'], label='Accuracy')
            
    # Add vertical line for drift point (if relevant, though here we start eval AFTER drift point)
    # Actually, the drift might happen LATER in the stream.
    # The "drift_point" in config was used for initial training.
    # Real concept drift might happen at sample 2000, 3000 etc.
    
    plt.axvline(x=drift_point, color='r', linestyle='--', label='End of Initial Training')
    
    # Plot retraining events
    if 'retrained' in df.columns:
        retrain_events = df[df['retrained'] == 1]
        for idx, row in retrain_events.iterrows():
            # Only label the first one to avoid cluttering the legend
            label = 'Full Retraining' if idx == retrain_events.index[0] else ""
            plt.axvline(x=row['window_start'], color='purple', linestyle='-', alpha=0.6, linewidth=2, label=label)

    plt.title(f'Concept Drift Analysis - {metric.capitalize()}')
    plt.xlabel('Sample Index')
    plt.ylabel('Score')
    plt.legend()
    plt.grid(True)
    plt.ylim(0, 1.1)
    
    output_file = args.output if args.output else csv_path.replace('.csv', '.png')
    plt.savefig(output_file)
    print(f"Plot saved to {output_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default='drift_results.csv', help='Path to results CSV')
    parser.add_argument('--config', default='drift_config.json', help='Path to config JSON')
    parser.add_argument('--output', help='Path to save output image')
    args = parser.parse_args()
    
    plot_drift(args.csv, args.config)
    args = parser.parse_args()
    
    plot_drift(args.csv, args.config)
