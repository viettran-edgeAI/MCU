import openml
import pandas as pd
import os

# List of datasets mentioned in "Extremely Simple Streaming Random Forests" and common concept drift benchmarks
datasets = {
    "electricity": 151,
    "covertype": 150,
    "airlines": 1169,
    "pokerhand": 155,
    "sea": 161,
    "sine": 162,
    "hyperplane": 163
}

output_dir = "datasets"
if not os.path.exists(output_dir):
    os.makedirs(output_dir)

print(f"Downloading datasets to {os.path.abspath(output_dir)}...")

for name, dataset_id in datasets.items():
    try:
        print(f"Downloading {name} (ID: {dataset_id})...")
        dataset = openml.datasets.get_dataset(dataset_id)
        X, y, categorical_indicator, attribute_names = dataset.get_data(
            target=dataset.default_target_attribute,
            dataset_format="dataframe"
        )
        
        # Combine features and target
        df = pd.concat([X, y], axis=1)
        
        # Save to CSV
        output_path = os.path.join(output_dir, f"{name}.csv")
        df.to_csv(output_path, index=False)
        print(f"Saved {name} to {output_path}")
        
    except Exception as e:
        print(f"Error downloading {name}: {e}")

print("\nDone! You can now use these datasets with the data_quantization tool.")
