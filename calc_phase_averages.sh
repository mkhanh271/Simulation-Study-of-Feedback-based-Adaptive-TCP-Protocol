#!/bin/bash
# ==============================================================================
# Calculate phase averages for all metrics
# Output: phase_data.txt for gnuplot
# ==============================================================================

echo "Calculating phase averages..."

# Function to calculate average for a phase
calc_avg() {
    local file=$1
    local start=$2
    local end=$3
    
    awk -v s="$start" -v e="$end" '
    $1 >= s && $1 <= e {
        sum += $2
        n++
    }
    END {
        if (n > 0) printf "%.2f", sum/n
        else print 0
    }' "$file"
}

# Phases
PHASE1_START=2
PHASE1_END=30
PHASE2_START=30
PHASE2_END=50
OVERALL_START=2
OVERALL_END=80

# ===== THROUGHPUT =====
echo "# Throughput data" > phase_throughput.dat
echo "# Phase Adaptive Reno" >> phase_throughput.dat

# Phase 1 - from analyze output
echo "1 12.90 9.59" >> phase_throughput.dat
# Phase 2
echo "2 1.12 1.06" >> phase_throughput.dat
# Overall
echo "3 5.29 4.05" >> phase_throughput.dat

# ===== PACKET LOSS =====
echo "# Loss data" > phase_loss.dat
echo "# Phase Adaptive Reno" >> phase_loss.dat

echo "1 0.02 0.19" >> phase_loss.dat
echo "2 1.68 2.05" >> phase_loss.dat
echo "3 0.27 0.50" >> phase_loss.dat

# ===== RTT (calculate from data files) =====
echo "# RTT data" > phase_rtt.dat
echo "# Phase Adaptive Reno" >> phase_rtt.dat

RTT_A_P1=$(calc_avg rtt_adaptive.dat $PHASE1_START $PHASE1_END)
RTT_R_P1=$(calc_avg rtt_reno.dat $PHASE1_START $PHASE1_END)
RTT_A_P2=$(calc_avg rtt_adaptive.dat $PHASE2_START $PHASE2_END)
RTT_R_P2=$(calc_avg rtt_reno.dat $PHASE2_START $PHASE2_END)
RTT_A_ALL=$(calc_avg rtt_adaptive.dat $OVERALL_START $OVERALL_END)
RTT_R_ALL=$(calc_avg rtt_reno.dat $OVERALL_START $OVERALL_END)

echo "1 $RTT_A_P1 $RTT_R_P1" >> phase_rtt.dat
echo "2 $RTT_A_P2 $RTT_R_P2" >> phase_rtt.dat
echo "3 $RTT_A_ALL $RTT_R_ALL" >> phase_rtt.dat

# ===== DELAY (calculate from data files) =====
echo "# Delay data" > phase_delay.dat
echo "# Phase Adaptive Reno" >> phase_delay.dat

DELAY_A_P1=$(calc_avg delay_adaptive.dat $PHASE1_START $PHASE1_END)
DELAY_R_P1=$(calc_avg delay_reno.dat $PHASE1_START $PHASE1_END)
DELAY_A_P2=$(calc_avg delay_adaptive.dat $PHASE2_START $PHASE2_END)
DELAY_R_P2=$(calc_avg delay_reno.dat $PHASE2_START $PHASE2_END)
DELAY_A_ALL=$(calc_avg delay_adaptive.dat $OVERALL_START $OVERALL_END)
DELAY_R_ALL=$(calc_avg delay_reno.dat $OVERALL_START $OVERALL_END)

echo "1 $DELAY_A_P1 $DELAY_R_P1" >> phase_delay.dat
echo "2 $DELAY_A_P2 $DELAY_R_P2" >> phase_delay.dat
echo "3 $DELAY_A_ALL $DELAY_R_ALL" >> phase_delay.dat

# ===== CWND (from analyze output) =====
echo "# CWND data" > phase_cwnd.dat
echo "# Phase Adaptive Reno" >> phase_cwnd.dat

CWND_A_P1=$(calc_avg cwnd_adaptive_opt.dat $PHASE1_START $PHASE1_END)
CWND_R_P1=$(calc_avg cwnd_reno_opt.dat $PHASE1_START $PHASE1_END)
CWND_A_P2=$(calc_avg cwnd_adaptive_opt.dat $PHASE2_START $PHASE2_END)
CWND_R_P2=$(calc_avg cwnd_reno_opt.dat $PHASE2_START $PHASE2_END)
CWND_A_ALL=$(calc_avg cwnd_adaptive_opt.dat $OVERALL_START $OVERALL_END)
CWND_R_ALL=$(calc_avg cwnd_reno_opt.dat $OVERALL_START $OVERALL_END)

echo "1 $CWND_A_P1 $CWND_R_P1" >> phase_cwnd.dat
echo "2 $CWND_A_P2 $CWND_R_P2" >> phase_cwnd.dat
echo "3 $CWND_A_ALL $CWND_R_ALL" >> phase_cwnd.dat

# ===== SUMMARY =====
echo ""
echo "=========================================="
echo " PHASE AVERAGES CALCULATED"
echo "=========================================="
echo ""

echo "=== PHASE 1: Clean (2-30s) ==="
echo "Throughput: Adaptive=$RTT_A_P1 | Reno=$RTT_R_P1"
echo "RTT: Adaptive=$RTT_A_P1 ms | Reno=$RTT_R_P1 ms"
echo "Delay: Adaptive=$DELAY_A_P1 ms | Reno=$DELAY_R_P1 ms"
echo "CWND: Adaptive=$CWND_A_P1 | Reno=$CWND_R_P1"
echo ""

echo "=== PHASE 2: Lossy (30-50s) ==="
echo "Throughput: Adaptive=1.12 | Reno=1.06"
echo "RTT: Adaptive=$RTT_A_P2 ms | Reno=$RTT_R_P2 ms"
echo "Delay: Adaptive=$DELAY_A_P2 ms | Reno=$DELAY_R_P2 ms"
echo "CWND: Adaptive=$CWND_A_P2 | Reno=$CWND_R_P2"
echo ""

echo "=== OVERALL (2-80s) ==="
echo "Throughput: Adaptive=5.29 | Reno=4.05"
echo "RTT: Adaptive=$RTT_A_ALL ms | Reno=$RTT_R_ALL ms"
echo "Delay: Adaptive=$DELAY_A_ALL ms | Reno=$DELAY_R_ALL ms"
echo "CWND: Adaptive=$CWND_A_ALL | Reno=$CWND_R_ALL"
echo ""

echo "Phase data files created:"
echo "  phase_throughput.dat"
echo "  phase_loss.dat"
echo "  phase_rtt.dat"
echo "  phase_delay.dat"
echo "  phase_cwnd.dat"
echo ""
echo "Now update plot_optimal_all.gnu to use these files"