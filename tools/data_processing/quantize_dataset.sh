#!/bin/bash

# ESP32 Dataset Quantization and Visualization Script
# This script quantizes CSV datasets to 2-bit categories and optionally generates visualizations

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default values
CSV_PATH=""
HEADER_MODE=""  # Empty means auto-detect
MAX_FEATURES=""  # Empty means use default (1023)
QUANT_BITS=""  # Empty uses processing tool default (2 bits)
RUN_VISUALIZATION="yes"  # Default to yes
HELP=false

# Function to show usage
show_usage() {
    echo -e "${CYAN}ESP32 Dataset Quantization Tool${NC}"
    echo -e "${BLUE}================================${NC}"
    echo ""
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  -p, --path <file>         Path to input CSV file (required)"
    echo "  -he, --header <yes/no>    Skip header if 'yes', process all lines if 'no' (auto-detect if not specified)"
    echo "  -f, --features <number>   Maximum number of features (default: 1023, range: 1-65535)"
    echo "  -q, --bits <1-8>          Quantization coefficient in bits per feature (default: 2)"
    echo "  -v, --visualize           Run quantization visualization after processing (default: yes)"
    echo "  -nv, --no-visualize       Skip visualization"
    echo "  -h, --help                Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 -p data/iris_data.csv                           # Auto-detect header, default max features, visualize"
    echo "  $0 -p data/iris_data.csv --header no               # Process all lines (no header)"
    echo "  $0 -p data/iris_data.csv --header yes              # Skip first line (has header)"
    echo "  $0 -p data/iris_data.csv -f 512                    # Limit to 512 features max"
    echo "  $0 -p data/iris_data.csv -nv                       # Skip visualization"
    echo "  $0 -p data/iris_data.csv -q 3                     # Quantize with 3 bits per feature"
    echo "  $0 -p data/iris_data.csv --header yes -f 256       # Skip header + 256 features + visualize (default)"
    echo ""
    echo -e "${YELLOW}Note: Header detection analyzes first two rows to determine if dataset has headers${NC}"
    echo -e "${YELLOW}      --header yes: Skip first line (treat as header)${NC}"
    echo -e "${YELLOW}      --header no:  Process all lines (no header present)${NC}"
    echo -e "${YELLOW}      (no --header): Automatically detect header presence${NC}"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -p|--path)
            CSV_PATH="$2"
            shift 2
            ;;
        -he|--header)
            HEADER_MODE="$2"
            shift 2
            ;;
        -f|--features)
            MAX_FEATURES="$2"
            shift 2
            ;;
        -q|--bits|--quant|--quantization)
            QUANT_BITS="$2"
            shift 2
            ;;
        -v|--visualize)
            RUN_VISUALIZATION="yes"
            shift
            ;;
        -nv|--no-visualize)
            RUN_VISUALIZATION="no"
            shift
            ;;
        -h|--help)
            HELP=true
            shift
            ;;
        *)
            if [[ -z "$CSV_PATH" && "$1" != -* ]]; then
                # Backward compatibility: treat first non-flag argument as CSV path
                CSV_PATH="$1"
            else
                echo -e "${RED}Error: Unknown option $1${NC}"
                show_usage
                exit 1
            fi
            shift
            ;;
    esac
done

# Show help and exit if requested
if [ "$HELP" = true ]; then
    show_usage
    exit 0
fi

# Validate required arguments
if [[ -z "$CSV_PATH" ]]; then
    echo -e "${RED}Error: CSV file path is required${NC}"
    echo "Use -p <path> or --path <path> to specify the input file"
    echo "Use -h or --help for usage information"
    exit 1
fi

# Validate quantization bits if provided
if [[ -n "$QUANT_BITS" ]]; then
    if ! [[ "$QUANT_BITS" =~ ^[1-8]$ ]]; then
        echo -e "${RED}Error: Quantization bits must be an integer between 1 and 8${NC}"
        exit 1
    fi
fi

# Check if file exists
if [[ ! -f "$CSV_PATH" ]]; then
    echo -e "${RED}Error: File '$CSV_PATH' not found${NC}"
    exit 1
fi

echo -e "${CYAN}=== ESP32 Dataset Processing and Visualization ===${NC}"
echo -e "${BLUE}Configuration:${NC}"
echo -e "  📁 Input file: ${GREEN}$CSV_PATH${NC}"
if [[ -n "$HEADER_MODE" ]]; then
    echo -e "  📋 Header mode: ${GREEN}$HEADER_MODE${NC} (user-specified)"
else
    echo -e "  📋 Header mode: ${GREEN}auto-detect${NC}"
fi
if [[ -n "$MAX_FEATURES" ]]; then
    echo -e "  📊 Max features: ${GREEN}$MAX_FEATURES${NC} (user-specified)"
else
    echo -e "  📊 Max features: ${GREEN}1023${NC} (default)"
fi
if [[ -n "$QUANT_BITS" ]]; then
    echo -e "  🧮 Quantization bits: ${GREEN}$QUANT_BITS${NC} (user-specified)"
else
    echo -e "  🧮 Quantization bits: ${GREEN}2${NC} (default)"
fi
echo -e "  📊 Run visualization: ${GREEN}$RUN_VISUALIZATION${NC}"
echo ""

# Step 1: Compile processing program if needed
echo -e "${PURPLE}Step 1: Checking processing program...${NC}"
if [[ ! -f "processing_data" || "processing_data.cpp" -nt "processing_data" ]]; then
    echo -e "${YELLOW}🔧 Compiling processing program...${NC}"
    if ! g++ -std=c++17 -I../../src -o processing_data processing_data.cpp; then
        echo -e "${RED}❌ Failed to compile processing program${NC}"
        exit 1
    fi
    echo -e "${GREEN}✅ Processing program compiled successfully${NC}"
else
    echo -e "${GREEN}✅ Processing program is up to date${NC}"
fi

# Step 2: Run dataset processing
echo -e "\n${PURPLE}Step 2: Processing dataset...${NC}"
PROCESS_ARGS="-p $CSV_PATH"

# Add header argument only if user specified it
if [[ -n "$HEADER_MODE" ]]; then
    PROCESS_ARGS="$PROCESS_ARGS -he $HEADER_MODE"
fi

# Add max features argument if user specified it
if [[ -n "$MAX_FEATURES" ]]; then
    PROCESS_ARGS="$PROCESS_ARGS -f $MAX_FEATURES"
fi

# Add quantization bits if user specified it
if [[ -n "$QUANT_BITS" ]]; then
    PROCESS_ARGS="$PROCESS_ARGS -q $QUANT_BITS"
fi

if [[ "$RUN_VISUALIZATION" == "yes" ]]; then
    PROCESS_ARGS="$PROCESS_ARGS -v"
fi

echo -e "${YELLOW}🚀 Running: ./processing_data $PROCESS_ARGS${NC}"
if ! ./processing_data $PROCESS_ARGS; then
    echo -e "${RED}❌ Dataset processing failed${NC}"
    exit 1
fi

echo -e "\n${GREEN}✅ ESP32 Dataset Processing and Visualization completed successfully!${NC}"

# Extract base name for further operations
BASENAME=$(basename "$CSV_PATH" .csv)
DIRNAME=$(dirname "$CSV_PATH")
RESULT_DIR="$DIRNAME/result"

echo -e "\n${CYAN}Generated Files:${NC}"
if [[ -f "$RESULT_DIR/${BASENAME}_nml.csv" ]]; then
    echo -e "  📊 Normalized CSV: ${GREEN}$RESULT_DIR/${BASENAME}_nml.csv${NC}"
fi
if [[ -f "$RESULT_DIR/${BASENAME}_nml.bin" ]]; then
    echo -e "  💾 Binary dataset: ${GREEN}$RESULT_DIR/${BASENAME}_nml.bin${NC}"
fi
if [[ -f "$RESULT_DIR/${BASENAME}_ctg.csv" ]]; then
    echo -e "  📋 Categorizer: ${GREEN}$RESULT_DIR/${BASENAME}_ctg.csv${NC}"
fi
if [[ -f "$RESULT_DIR/${BASENAME}_dp.csv" ]]; then
    echo -e "  ⚙️  Parameters: ${GREEN}$RESULT_DIR/${BASENAME}_dp.csv${NC}"
fi

if [[ "$RUN_VISUALIZATION" == "yes" && -d "plots" ]]; then
    echo -e "  📊 Visualizations: ${GREEN}plots/${NC}"
fi

echo -e "\n${YELLOW}💡 Next steps:${NC}"
echo -e "   • Review generated files in the result/ directory"
if [[ "$RUN_VISUALIZATION" == "yes" ]]; then
    echo -e "   • Check visualization plots in the plots/ directory"
fi
echo -e "   • Transfer files to ESP32 using unified_transfer.py"
echo ""
