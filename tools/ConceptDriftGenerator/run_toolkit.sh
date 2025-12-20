#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory where the script is located
BASE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "--------------------------------------------------"
echo "Concept Drift Toolkit: Generate & Visualize"
echo "--------------------------------------------------"

# Step 1: Compile the C++ Generator if it doesn't exist or source is newer
if [ ! -f "$BASE_DIR/generate_data" ] || [ "$BASE_DIR/generate_data.cpp" -nt "$BASE_DIR/generate_data" ]; then
    echo "[1/3] Compiling C++ Data Generator..."
    g++ -std=c++17 "$BASE_DIR/generate_data.cpp" -o "$BASE_DIR/generate_data"
else
    echo "[1/3] C++ Generator is up to date."
fi

# Step 2: Run the Generator
echo "[2/3] Generating datasets from parameters.json..."
"$BASE_DIR/generate_data"

# Step 3: Run the Visualization
echo "[3/3] Running advanced drift visualization..."
python3 "$BASE_DIR/visualize_drift.py"

echo "--------------------------------------------------"
echo "Done! Check the following folders for results:"
echo " - Datasets: $BASE_DIR/datasets/"
echo " - Visualizations: $BASE_DIR/drift_concept_visualize/"
echo "--------------------------------------------------"
