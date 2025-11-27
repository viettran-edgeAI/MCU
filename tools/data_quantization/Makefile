# ESP32 Dataset Processing and Visualization Makefile
# Provides targets for compilation, testing, and data processing

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
INCLUDES = -I../../src
TARGET = processing_data
SOURCE = processing_data.cpp

# Python settings
PYTHON = python3
VENV_DIR = venv_quantizer
PIP = $(VENV_DIR)/bin/pip
PYTHON_VENV = $(VENV_DIR)/bin/python

# Directories
DATA_DIR = data
RESULT_DIR = $(DATA_DIR)/result
PLOTS_DIR = plots

# Default target
.PHONY: all
all: build setup-python

# Build the C++ processing program
.PHONY: build
build: $(TARGET)

$(TARGET): $(SOURCE)
	@echo "🔧 Compiling $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SOURCE)
	@echo "✅ Compilation successful!"

# Set up Python environment and dependencies
.PHONY: setup-python
setup-python: $(VENV_DIR)/bin/activate requirements.txt
	@echo "🐍 Setting up Python environment..."
	$(PIP) install -r requirements.txt
	@echo "✅ Python setup complete!"

$(VENV_DIR)/bin/activate:
	@echo "🔧 Creating Python virtual environment..."
	$(PYTHON) -m venv $(VENV_DIR)

# Create necessary directories
.PHONY: setup-dirs
setup-dirs:
	@echo "📁 Creating directories..."
	mkdir -p $(DATA_DIR) $(RESULT_DIR) $(PLOTS_DIR)
	@echo "✅ Directories created!"

# Complete setup (C++ + Python + directories)
.PHONY: setup
setup: build setup-python setup-dirs
	@echo "🚀 Complete setup finished!"
	@echo "💡 Run 'make test' to verify installation"

# Test with sample data
.PHONY: test
test: build setup-dirs
	@echo "🧪 Running test with sample data..."
	@if [ ! -f "$(DATA_DIR)/test_data.csv" ]; then \
		echo "📄 Creating test dataset..."; \
		echo "class,f1,f2,f3" > $(DATA_DIR)/test_data.csv; \
		echo "A,1.1,2.2,3.3" >> $(DATA_DIR)/test_data.csv; \
		echo "B,4.4,5.5,6.6" >> $(DATA_DIR)/test_data.csv; \
		echo "A,7.7,8.8,9.9" >> $(DATA_DIR)/test_data.csv; \
		echo "B,10.0,11.1,12.2" >> $(DATA_DIR)/test_data.csv; \
	fi
	./$(TARGET) -p $(DATA_DIR)/test_data.csv -he yes
	@echo "✅ Test completed successfully!"

# Test with visualization
.PHONY: test-viz
test-viz: build setup-python setup-dirs
	@echo "🧪 Running test with visualization..."
	@if [ ! -f "$(DATA_DIR)/test_data.csv" ]; then \
		echo "📄 Creating test dataset..."; \
		echo "class,f1,f2,f3" > $(DATA_DIR)/test_data.csv; \
		echo "A,1.1,2.2,3.3" >> $(DATA_DIR)/test_data.csv; \
		echo "B,4.4,5.5,6.6" >> $(DATA_DIR)/test_data.csv; \
		echo "A,7.7,8.8,9.9" >> $(DATA_DIR)/test_data.csv; \
		echo "B,10.0,11.1,12.2" >> $(DATA_DIR)/test_data.csv; \
	fi
	./$(TARGET) -p $(DATA_DIR)/test_data.csv -he yes -v
	@echo "✅ Test with visualization completed!"

# Process a specific dataset
.PHONY: process
process: build
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Error: FILE parameter required"; \
		echo "💡 Usage: make process FILE=data/mydata.csv"; \
		exit 1; \
	fi
	@if [ ! -f "$(FILE)" ]; then \
		echo "❌ Error: File $(FILE) not found"; \
		exit 1; \
	fi
	@echo "🔄 Processing $(FILE)..."
	./$(TARGET) -p $(FILE) -he yes
	@echo "✅ Processing completed!"

# Process with visualization
.PHONY: process-viz
process-viz: build setup-python
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Error: FILE parameter required"; \
		echo "💡 Usage: make process-viz FILE=data/mydata.csv"; \
		exit 1; \
	fi
	@if [ ! -f "$(FILE)" ]; then \
		echo "❌ Error: File $(FILE) not found"; \
		exit 1; \
	fi
	@echo "🔄 Processing $(FILE) with visualization..."
	./$(TARGET) -p $(FILE) -he yes -v
	@echo "✅ Processing with visualization completed!"

# Use unified script (recommended method)
.PHONY: unified
unified: 
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Error: FILE parameter required"; \
		echo "💡 Usage: make unified FILE=data/mydata.csv [HEADER=yes] [VIZ=yes]"; \
		exit 1; \
	fi
	@HEADER_OPT=""; \
	if [ "$(HEADER)" = "no" ]; then \
		HEADER_OPT="--header no"; \
	fi; \
	VIZ_OPT=""; \
	if [ "$(VIZ)" = "yes" ]; then \
		VIZ_OPT="--visualize"; \
	fi; \
	echo "🚀 Running unified script..."; \
	chmod +x quantize_dataset.sh; \
	./quantize_dataset.sh -p $(FILE) $$HEADER_OPT $$VIZ_OPT

# Visualization only (requires processed data)
.PHONY: visualize
visualize: setup-python
	@if [ -z "$(NAME)" ]; then \
		echo "❌ Error: NAME parameter required"; \
		echo "💡 Usage: make visualize NAME=dataset_name"; \
		echo "   (This expects data/dataset_name.csv and data/result/dataset_name_nml.csv)"; \
		exit 1; \
	fi
	@echo "📊 Generating visualization for $(NAME)..."
	$(PYTHON_VENV) quantization_visualizer.py $(NAME)
	@echo "✅ Visualization completed!"

# Clean build artifacts
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -f $(TARGET)
	@echo "✅ Clean completed!"

# Clean all generated files
.PHONY: clean-all
clean-all: clean
	@echo "🧹 Cleaning all generated files..."
	rm -rf $(RESULT_DIR)/* $(PLOTS_DIR)/*
	rm -f $(DATA_DIR)/test_data.csv
	@echo "✅ Deep clean completed!"

# Remove Python environment
.PHONY: clean-python
clean-python:
	@echo "🧹 Removing Python environment..."
	rm -rf $(VENV_DIR)
	@echo "✅ Python environment removed!"

# Complete cleanup
.PHONY: distclean
distclean: clean-all clean-python
	@echo "🧹 Complete cleanup finished!"

# Show help
.PHONY: help
help:
	@echo "ESP32 Dataset Processing and Visualization Makefile"
	@echo "=================================================="
	@echo ""
	@echo "Setup targets:"
	@echo "  setup          - Complete setup (C++ + Python + directories)"
	@echo "  build          - Compile C++ processing program"
	@echo "  setup-python   - Set up Python environment and dependencies"
	@echo "  setup-dirs     - Create necessary directories"
	@echo ""
	@echo "Testing targets:"
	@echo "  test           - Run basic test with sample data"
	@echo "  test-viz       - Run test with visualization"
	@echo ""
	@echo "Processing targets:"
	@echo "  process        - Process dataset: make process FILE=data/file.csv"
	@echo "  process-viz    - Process with visualization: make process-viz FILE=data/file.csv"
	@echo "  unified        - Use unified script: make unified FILE=data/file.csv [HEADER=yes] [VIZ=yes]"
	@echo "  visualize      - Visualization only: make visualize NAME=dataset_name"
	@echo ""
	@echo "Cleanup targets:"
	@echo "  clean          - Remove build artifacts"
	@echo "  clean-all      - Remove all generated files"
	@echo "  clean-python   - Remove Python environment"
	@echo "  distclean      - Complete cleanup"
	@echo ""
	@echo "Examples:"
	@echo "  make setup                                    # Initial setup"
	@echo "  make test                                     # Quick test"
	@echo "  make unified FILE=data/iris.csv VIZ=yes       # Full workflow"
	@echo "  make process FILE=data/mydata.csv             # Process only"
	@echo "  make visualize NAME=iris                      # Visualize existing"
	@echo ""

# Check dependencies
.PHONY: check-deps
check-deps:
	@echo "🔍 Checking dependencies..."
	@which $(CXX) > /dev/null || (echo "❌ g++ not found" && exit 1)
	@which $(PYTHON) > /dev/null || (echo "❌ python3 not found" && exit 1)
	@echo "✅ Basic dependencies found"
	@if [ -f "$(VENV_DIR)/bin/activate" ]; then \
		echo "✅ Python virtual environment exists"; \
		$(PIP) check || echo "⚠️  Python package conflicts detected"; \
	else \
		echo "⚠️  Python virtual environment not set up (run 'make setup-python')"; \
	fi

# Show status
.PHONY: status
status: check-deps
	@echo ""
	@echo "📊 Project Status:"
	@echo "=================="
	@if [ -f "$(TARGET)" ]; then \
		echo "✅ C++ program: $(TARGET) (compiled)"; \
	else \
		echo "❌ C++ program: Not compiled"; \
	fi
	@if [ -d "$(VENV_DIR)" ]; then \
		echo "✅ Python environment: $(VENV_DIR)"; \
	else \
		echo "❌ Python environment: Not set up"; \
	fi
	@if [ -d "$(DATA_DIR)" ]; then \
		echo "✅ Data directory: $(DATA_DIR)"; \
		DATA_COUNT=$$(find $(DATA_DIR) -name "*.csv" -not -path "$(RESULT_DIR)/*" 2>/dev/null | wc -l); \
		echo "   📄 CSV files: $$DATA_COUNT"; \
	else \
		echo "❌ Data directory: Not created"; \
	fi
	@if [ -d "$(RESULT_DIR)" ]; then \
		RESULT_COUNT=$$(find $(RESULT_DIR) -name "*" -type f 2>/dev/null | wc -l); \
		echo "✅ Result directory: $$RESULT_COUNT files"; \
	else \
		echo "❌ Result directory: Not created"; \
	fi
	@if [ -d "$(PLOTS_DIR)" ]; then \
		PLOT_COUNT=$$(find $(PLOTS_DIR) -name "*.png" 2>/dev/null | wc -l); \
		echo "✅ Plots directory: $$PLOT_COUNT images"; \
	else \
		echo "❌ Plots directory: Not created"; \
	fi
	@echo ""

# Default help when no target specified
.DEFAULT_GOAL := help
