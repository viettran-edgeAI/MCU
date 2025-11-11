# Makefile for HOG Feature Extraction Tool
# ==========================================

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
INCLUDES = -I../../src -I/usr/include/opencv4
LIBS = $(shell pkg-config --libs opencv4)

# Target
PROCESSOR = hog_processor

# Default configuration
CONFIG = hog_config.json

# Build targets
.PHONY: all clean run run-custom test help

all: $(PROCESSOR)

# Build the HOG processor
$(PROCESSOR): hog_processor.cpp
	@echo "🔨 Building HOG processor..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LIBS)
	@echo "✅ Build complete: ./$(PROCESSOR)"

# Run with default config
run: $(PROCESSOR)
	@echo "▶️  Running HOG processor with $(CONFIG)..."
	@echo ""
	./$(PROCESSOR) $(CONFIG)

# Run with custom config
run-custom: $(PROCESSOR)
	@echo "▶️  Running HOG processor with $(CONFIG)..."
	@echo ""
	./$(PROCESSOR) $(CONFIG)

# Quick test with limited images
test: $(PROCESSOR)
	@echo "🧪 Running quick test (3 images per class)..."
	@echo ""
	@if [ -f $(CONFIG) ]; then \
		sed 's/"max_images_per_class": *[0-9-]*/"max_images_per_class": 3/' $(CONFIG) > test_config.json; \
		./$(PROCESSOR) test_config.json; \
		rm -f test_config.json; \
	else \
		echo "❌ Config file $(CONFIG) not found!"; \
	fi

# Clean build artifacts
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -f $(PROCESSOR)
	rm -f test_config.json
	@echo "✅ Clean complete"

# Show help
help:
	@echo "HOG Feature Extraction Makefile"
	@echo "==============================="
	@echo ""
	@echo "Usage:"
	@echo "  make                    Build hog_processor (default)"
	@echo "  make run                Run with default config (hog_config.json)"
	@echo "  make run-custom CONFIG=my.json  Run with custom config"
	@echo "  make test               Quick test with 3 images per class"
	@echo "  make clean              Remove build artifacts"
	@echo "  make help               Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make && make run"
	@echo "  make run-custom CONFIG=custom_config.json"
	@echo "  make test"
	@echo ""
	@echo "Requirements:"
	@echo "  - g++ with C++17 support"
	@echo "  - OpenCV 4.x"
	@echo "  - pkg-config"
	@echo ""
	@echo "Install OpenCV:"
	@echo "  Ubuntu/Debian: sudo apt-get install libopencv-dev"
