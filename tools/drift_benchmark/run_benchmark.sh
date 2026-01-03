#!/bin/bash

# Drift Benchmark Runner
# This script compiles the benchmark tool, runs the simulation, and plots the results.

# Configuration
SRC_DIR="src"
BUILD_DIR="build"
OUTPUT_DIR="drift_results"
CONFIG_FILE="drift_config.json"
RESULTS_CSV="drift_results.csv"
EXECUTABLE="./drift_benchmark"
PYTHON_ENV="../../.venv/bin/python"

# Ensure output directory exists
mkdir -p $OUTPUT_DIR

# 1. Compilation Stage
echo "🔨 Compiling Drift Benchmark..."
g++ -std=c++17 $SRC_DIR/drift_benchmark.cpp -o $SRC_DIR/drift_benchmark

if [ $? -ne 0 ]; then
    echo "❌ Compilation failed!"
    exit 1
fi
echo "✅ Compilation successful."

# 2. Benchmark Execution Stage
echo "🚀 Running Benchmark..."
if [ ! -f "$CONFIG_FILE" ]; then
    echo "⚠️  Config file '$CONFIG_FILE' not found!"
    exit 1
fi

$SRC_DIR/drift_benchmark

if [ $? -ne 0 ]; then
    echo "❌ Benchmark execution failed!"
    exit 1
fi

# 3. Visualization Stage
echo "📊 Generating Plots..."

# Extract dataset name from config for naming the output file
DATASET_PATH=$(grep "dataset_path" $CONFIG_FILE | cut -d '"' -f 4)
DATASET_NAME=$(basename "$DATASET_PATH" .csv)
OUTPUT_IMAGE="$OUTPUT_DIR/${DATASET_NAME}_plot.png"
OUTPUT_CSV="$OUTPUT_DIR/${DATASET_NAME}_results.csv"

# Check if python environment exists, otherwise try system python
if [ ! -f "$PYTHON_ENV" ]; then
    PYTHON_ENV="python3"
fi

$PYTHON_ENV $SRC_DIR/plot_drift.py --csv $RESULTS_CSV --config $CONFIG_FILE --output $OUTPUT_IMAGE

if [ $? -eq 0 ]; then
    echo "✅ Plot saved to $OUTPUT_IMAGE"
    
    # Archive the CSV results
    mv $RESULTS_CSV $OUTPUT_CSV
    echo "✅ Results archived to $OUTPUT_CSV"
else
    echo "❌ Plotting failed!"
    exit 1
fi

echo "🎉 All stages completed successfully!"
