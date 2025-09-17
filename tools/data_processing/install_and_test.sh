#!/bin/bash
# Installation and test script for quantization visualizer

echo "Setting up quantization visualizer..."

# Create virtual environment if it doesn't exist
if [ ! -d "venv_visualizer" ]; then
    echo "Creating virtual environment..."
    python3 -m venv venv_visualizer
fi

# Activate virtual environment
echo "Activating virtual environment..."
source venv_visualizer/bin/activate

# Install requirements
echo "Installing requirements..."
pip install -r requirements.txt

# Create plots directory
mkdir -p plots

# Make the script executable
chmod +x quantization_visualizer.py

echo "Setup complete!"
echo ""
echo "Usage examples:"
echo "  python quantization_visualizer.py iris_data"
echo "  python quantization_visualizer.py cancer_data"
echo "  python quantization_visualizer.py digit_data"
echo "  python quantization_visualizer.py walker_fall"
echo ""

# Test with iris dataset if available
if [ -f "data/iris_data.csv" ] && [ -f "data/result/iris_data_nml.csv" ]; then
    echo "Testing with iris dataset..."
    python quantization_visualizer.py iris_data
else
    echo "Iris dataset not found for testing."
fi
