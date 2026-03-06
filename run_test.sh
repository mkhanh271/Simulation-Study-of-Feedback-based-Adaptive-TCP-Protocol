#!/bin/bash
# ==============================================================================
# COMPLETE TEST PIPELINE - TCP Adaptive vs Reno
# ==============================================================================

echo "=============================================="
echo "  TCP ADAPTIVE - FULL TEST PIPELINE"
echo "=============================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check required files
echo "Step 1: Checking required files..."
REQUIRED_FILES=(
    "optimal_test_adaptive.tcl"
    "analyze_optimal.sh"
    "plot_optimal_all.gnu"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo -e "${RED}ERROR: $file not found!${NC}"
        exit 1
    fi
done
echo -e "${GREEN} All required files found${NC}"
echo ""

# Clean old results
echo "Step 2: Cleaning old results..."
rm -f *.tr *.dat *.png
echo -e "${GREEN} Cleaned${NC}"
echo ""

# Run NS-2 simulation
echo "Step 3: Running NS-2 simulation..."
echo "----------------------------------------------"
ns optimal_test_adaptive.tcl
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Simulation failed!${NC}"
    exit 1
fi
echo -e "${GREEN} Simulation completed${NC}"
echo ""

# Check trace file
if [ ! -f "optimal_adaptive.tr" ]; then
    echo -e "${RED}ERROR: Trace file not generated!${NC}"
    exit 1
fi

TRACE_SIZE=$(wc -c < optimal_adaptive.tr)
echo "Trace file size: $TRACE_SIZE bytes"
echo ""

# Analyze results
echo "Step 4: Analyzing results..."
echo "----------------------------------------------"
chmod +x analyze_optimal.sh
./analyze_optimal.sh
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Analysis failed!${NC}"
    exit 1
fi
echo -e "${GREEN} Analysis completed${NC}"
echo ""

# Check generated data files
echo "Step 5: Checking generated data files..."
DATA_FILES=(
    "throughput_adaptive.dat"
    "throughput_reno.dat"
    "rtt_adaptive.dat"
    "rtt_reno.dat"
    "delay_adaptive.dat"
    "delay_reno.dat"
    "loss_adaptive.dat"
    "loss_reno.dat"
    "cwnd_adaptive_opt.dat"
    "cwnd_reno_opt.dat"
)

ALL_OK=true
for file in "${DATA_FILES[@]}"; do
    if [ ! -s "$file" ]; then
        echo -e "${RED} $file is missing or empty${NC}"
        ALL_OK=false
    else
        lines=$(wc -l < "$file")
        echo -e "${GREEN} $file ($lines lines)${NC}"
    fi
done

if [ "$ALL_OK" = false ]; then
    echo -e "${RED}ERROR: Some data files are missing or empty!${NC}"
    exit 1
fi
echo ""

# Calculate phase averages
echo "Step 6: Calculating phase averages..."
echo "----------------------------------------------"
if [ -f "calc_phase_averages.sh" ]; then
    chmod +x calc_phase_averages.sh
    ./calc_phase_averages.sh
    echo -e "${GREEN} Phase averages calculated${NC}"
else
    echo -e "${YELLOW} calc_phase_averages.sh not found, using default values${NC}"
fi
echo ""

# Generate plots
echo "Step 7: Generating plots..."
echo "----------------------------------------------"
gnuplot plot_optimal_all.gnu
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Plotting failed!${NC}"
    exit 1
fi
echo -e "${GREEN} Plots generated${NC}"
echo ""

# Check generated plots
echo "Step 8: Verifying generated plots..."
PLOT_FILES=(
    "metrics_vs_time.png"
    "cwnd_comparison.png"
    "phase_comparison.png"
    "detailed_comparison.png"
)

for file in "${PLOT_FILES[@]}"; do
    if [ -f "$file" ]; then
        size=$(wc -c < "$file")
        echo -e "${GREEN} $file ($size bytes)${NC}"
    else
        echo -e "${RED} $file not generated${NC}"
    fi
done
echo ""

# Summary
echo "=============================================="
echo "  TEST COMPLETED SUCCESSFULLY!"
echo "=============================================="
echo ""
echo "Generated files:"
echo "  Data files: ${#DATA_FILES[@]}"
echo "  Plot files: ${#PLOT_FILES[@]}"
echo ""
echo "You can view the plots:"
echo "  1. metrics_vs_time.png    - Throughput, RTT, Delay, Loss"
echo "  2. cwnd_comparison.png    - Congestion window evolution"
echo "  3. phase_comparison.png   - Performance by phase"
echo ""
