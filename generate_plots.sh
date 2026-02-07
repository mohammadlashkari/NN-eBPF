#!/bin/bash
# Master script to generate all improvement visualizations

echo "=========================================="
echo "NN-eBPF Visualization Generator"
echo "=========================================="
echo ""

# Check if results.csv exists
if [ ! -f "results.csv" ]; then
    echo "❌ Error: results.csv not found!"
    echo "Please ensure results.csv is in the current directory."
    exit 1
fi

echo "✓ Found results.csv"
echo ""

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "❌ Error: python3 not found!"
    echo "Please install Python 3 to continue."
    exit 1
fi

echo "✓ Python 3 is available"
echo ""

# Check if required packages are installed
echo "Checking Python dependencies..."
python3 -c "import pandas, matplotlib, numpy" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "⚠️  Warning: Some Python packages may be missing."
    echo "Installing required packages..."
    pip3 install pandas matplotlib numpy
    if [ $? -ne 0 ]; then
        echo "❌ Failed to install packages. Please install manually:"
        echo "   pip3 install pandas matplotlib numpy"
        exit 1
    fi
fi

echo "✓ All dependencies installed"
echo ""

# Run the master visualization script
echo "Generating all visualizations..."
echo ""
python3 generate_all_plots.py

if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "✓ Success! All visualizations generated."
    echo "=========================================="
    echo ""
    echo "Generated files:"
    ls -1 *.png 2>/dev/null | sed 's/^/  - /'
    echo ""
else
    echo ""
    echo "=========================================="
    echo "❌ Error generating visualizations."
    echo "=========================================="
    exit 1
fi
