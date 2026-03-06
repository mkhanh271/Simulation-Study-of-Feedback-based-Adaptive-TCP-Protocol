#!/bin/bash
# ==============================================================================
# FIXED: Correct RTT, Delay, and Packet Loss calculation
# ==============================================================================

TRACE="optimal_adaptive.tr"

echo "=========================================="
echo " TCP Adaptive vs Reno - FIXED ANALYSIS"
echo "=========================================="
echo ""

if [ ! -f "$TRACE" ]; then
    echo "ERROR: $TRACE not found. Run simulation first."
    exit 1
fi

# -------------------------------------------------------------------
# Calculate throughput (Mbps) - UNCHANGED, already correct
# -------------------------------------------------------------------
calc_phase_throughput() {
    local fid=$1
    local start=$2
    local end=$3

    awk -v fid="$fid" -v s="$start" -v e="$end" '
    $1=="r" && $8==fid && $5=="tcp" && $6>=1000 && $2>=s && $2<=e {
        bytes += $6
    }
    END {
        duration = e - s
        if (duration > 0)
            printf "%.4f", (bytes*8)/(duration*1e6)
        else
            print 0
    }' "$TRACE"
}

# -------------------------------------------------------------------
# Average CWND - UNCHANGED
# -------------------------------------------------------------------
calc_avg_cwnd() {
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

# -------------------------------------------------------------------
# FIXED: Packet Loss Rate
# Count only DATA packets (size >= 1000)
# -------------------------------------------------------------------
calc_loss_phase() {
    local fid=$1
    local start=$2
    local end=$3

    awk -v fid="$fid" -v s="$start" -v e="$end" '
    # Count DATA packets sent from source (node 0 for fid=1, node 1 for fid=2)
    $1=="+" && $5=="tcp" && $6>=1000 && $8==fid && $2>=s && $2<=e {
        # Only count from source nodes
        if ((fid==1 && $3==0) || (fid==2 && $3==1)) {
            sent++
        }
    }

    # Count dropped DATA packets
    ($1=="d" || $1=="D") && $5=="tcp" && $6>=1000 && $8==fid && $2>=s && $2<=e {
        drop++
    }

    END {
        if (sent > 0)
            printf "%.2f", (drop * 100.0 / sent)
        else
            print 0
    }' "$TRACE"
}

# -------------------------------------------------------------------
# FIXED: Generate time-series data
# -------------------------------------------------------------------
generate_trace_data() {

    echo ""
    echo "------------------------------------------"
    echo " Generating time-series data (FIXED)"
    echo "------------------------------------------"

    # ================================
    # 1. THROUGHPUT - UNCHANGED (already correct)
    # ================================
    echo "Generating throughput data..."
    
    # Adaptive
    awk '
    $1=="r" && $5=="tcp" && $6>=1000 && $8==1 {
        sec = int($2)
        bytes[sec] += $6
    }
    END {
        for (sec in bytes) {
            mbps = (bytes[sec] * 8.0) / 1e6
            printf "%.1f %.4f\n", sec, mbps
        }
    }' "$TRACE" | sort -n > throughput_adaptive.dat
    
    # Reno
    awk '
    $1=="r" && $5=="tcp" && $6>=1000 && $8==2 {
        sec = int($2)
        bytes[sec] += $6
    }
    END {
        for (sec in bytes) {
            mbps = (bytes[sec] * 8.0) / 1e6
            printf "%.1f %.4f\n", sec, mbps
        }
    }' "$TRACE" | sort -n > throughput_reno.dat

    # ================================
    # 2. RTT - FIXED (track DATA send + ACK return)
    # ================================
    echo "Generating RTT data..."
    
    # Adaptive (fid=1, source=node 0, dest=node 4)
    awk '
    # Track DATA packet leaving source (dequeue event "-")
    $1=="-" && $3==0 && $5=="tcp" && $6>=1000 && $8==1 {
        data_send[$11] = $2  # $11 is sequence number
    }
    
    # Track ACK arriving back at source
    $1=="r" && $4==0 && $5=="ack" && $8==1 {
        seq = $11
        if (seq in data_send) {
            rtt = ($2 - data_send[seq]) * 1000
            if (rtt > 0 && rtt < 10000)
                printf "%.3f %.3f\n", $2, rtt
            delete data_send[seq]
        }
    }' "$TRACE" | sort -n > rtt_adaptive.dat
    
    # Reno (fid=2, source=node 1, dest=node 5)
    awk '
    # Track DATA packet leaving source
    $1=="-" && $3==1 && $5=="tcp" && $6>=1000 && $8==2 {
        data_send[$11] = $2
    }
    
    # Track ACK arriving back at source
    $1=="r" && $4==1 && $5=="ack" && $8==2 {
        seq = $11
        if (seq in data_send) {
            rtt = ($2 - data_send[seq]) * 1000
            if (rtt > 0 && rtt < 10000)
                printf "%.3f %.3f\n", $2, rtt
            delete data_send[seq]
        }
    }' "$TRACE" | sort -n > rtt_reno.dat

    # ================================
    # 3. ONE-WAY DELAY - FIXED (source to destination)
    # ================================
    echo "Generating delay data..."
    
    # Adaptive (node 0 → node 4)
    awk '
    # DATA leaves source
    $1=="-" && $3==0 && $5=="tcp" && $6>=1000 && $8==1 {
        send[$12] = $2  # $12 is unique packet ID
    }
    
    # DATA arrives at destination
    $1=="r" && $4==4 && $5=="tcp" && $6>=1000 && $8==1 {
        if ($12 in send) {
            delay = ($2 - send[$12]) * 1000
            if (delay > 0 && delay < 10000)
                printf "%.3f %.3f\n", $2, delay
            delete send[$12]
        }
    }' "$TRACE" | sort -n > delay_adaptive.dat
    
    # Reno (node 1 → node 5)
    awk '
    # DATA leaves source
    $1=="-" && $3==1 && $5=="tcp" && $6>=1000 && $8==2 {
        send[$12] = $2
    }
    
    # DATA arrives at destination
    $1=="r" && $4==5 && $5=="tcp" && $6>=1000 && $8==2 {
        if ($12 in send) {
            delay = ($2 - send[$12]) * 1000
            if (delay > 0 && delay < 10000)
                printf "%.3f %.3f\n", $2, delay
            delete send[$12]
        }
    }' "$TRACE" | sort -n > delay_reno.dat

    # ================================
    # 4. PACKET LOSS - FIXED (per second, only from source)
    # ================================
    echo "Generating packet loss data..."
    
    # Adaptive
    awk '
    # Count DATA sent from source
    $1=="+" && $3==0 && $5=="tcp" && $6>=1000 && $8==1 {
        sec = int($2)
        sent[sec]++
    }
    
    # Count DATA dropped
    ($1=="d" || $1=="D") && $5=="tcp" && $6>=1000 && $8==1 {
        sec = int($2)
        drop[sec]++
    }
    
    END {
        for (sec in sent) {
            s = sent[sec]
            d = (sec in drop) ? drop[sec] : 0
            loss = (s > 0) ? (d * 100.0 / s) : 0
            printf "%.1f %.3f\n", sec, loss
        }
    }' "$TRACE" | sort -n > loss_adaptive.dat
    
    # Reno
    awk '
    # Count DATA sent from source
    $1=="+" && $3==1 && $5=="tcp" && $6>=1000 && $8==2 {
        sec = int($2)
        sent[sec]++
    }
    
    # Count DATA dropped
    ($1=="d" || $1=="D") && $5=="tcp" && $6>=1000 && $8==2 {
        sec = int($2)
        drop[sec]++
    }
    
    END {
        for (sec in sent) {
            s = sent[sec]
            d = (sec in drop) ? drop[sec] : 0
            loss = (s > 0) ? (d * 100.0 / s) : 0
            printf "%.1f %.3f\n", sec, loss
        }
    }' "$TRACE" | sort -n > loss_reno.dat

    echo ""
    echo "Generated files:"
    echo "  throughput_adaptive.dat, throughput_reno.dat"
    echo "  rtt_adaptive.dat, rtt_reno.dat"
    echo "  delay_adaptive.dat, delay_reno.dat"
    echo "  loss_adaptive.dat, loss_reno.dat"
    
    # Check if files have data
    for f in throughput_adaptive.dat throughput_reno.dat rtt_adaptive.dat rtt_reno.dat; do
        if [ ! -s "$f" ]; then
            echo "WARNING: $f is empty!"
        else
            lines=$(wc -l < "$f")
            echo "  $f: $lines lines"
        fi
    done
    
    # Verify RTT values
    echo ""
    echo "=== RTT Verification ==="
    echo "Expected RTT: ~44-50ms (40ms bottleneck + 4ms access + queuing)"
    echo ""
    echo "Sample Adaptive RTT:"
    head -5 rtt_adaptive.dat | awk '{printf "  Time: %.3f  RTT: %.3f ms\n", $1, $2}'
    echo ""
    echo "Sample Reno RTT:"
    head -5 rtt_reno.dat | awk '{printf "  Time: %.3f  RTT: %.3f ms\n", $1, $2}'
}

# -------------------------------------------------------------------
# OVERALL RESULTS
# -------------------------------------------------------------------
echo "=== OVERALL RESULTS (2–80s) ==="

TP_A=$(calc_phase_throughput 1 2 80)
TP_R=$(calc_phase_throughput 2 2 80)

echo "Throughput:"
echo "  Adaptive: $TP_A Mbps"
echo "  Reno:     $TP_R Mbps"

IMPROVEMENT=$(awk "BEGIN{if($TP_R>0) printf \"%.2f\",(($TP_A-$TP_R)/$TP_R)*100; else print 0}")
echo "  Improvement: $IMPROVEMENT %"

LOSS_A=$(calc_loss_phase 1 2 80)
LOSS_R=$(calc_loss_phase 2 2 80)

echo ""
echo "Packet Loss:"
echo "  Adaptive: $LOSS_A %"
echo "  Reno:     $LOSS_R %"

# -------------------------------------------------------------------
# Phase Analysis
# -------------------------------------------------------------------
echo ""
echo "=========================================="
echo " PHASE ANALYSIS"
echo "=========================================="

echo ""
echo "--- Phase 1: CLEAN LINK (2–30s) ---"
TP_A_P1=$(calc_phase_throughput 1 2 30)
TP_R_P1=$(calc_phase_throughput 2 2 30)
LOSS_A_P1=$(calc_loss_phase 1 2 30)
LOSS_R_P1=$(calc_loss_phase 2 2 30)

[ -f cwnd_adaptive_opt.dat ] && CWND_A_P1=$(calc_avg_cwnd cwnd_adaptive_opt.dat 2 30)
[ -f cwnd_reno_opt.dat ] && CWND_R_P1=$(calc_avg_cwnd cwnd_reno_opt.dat 2 30)

echo "Throughput: Adaptive=$TP_A_P1 | Reno=$TP_R_P1"
echo "Loss: Adaptive=$LOSS_A_P1 | Reno=$LOSS_R_P1"
echo "CWND: Adaptive=$CWND_A_P1 | Reno=$CWND_R_P1"

echo ""
echo "--- Phase 2: LOSSY LINK (30–50s) ---"
TP_A_P2=$(calc_phase_throughput 1 30 50)
TP_R_P2=$(calc_phase_throughput 2 30 50)
LOSS_A_P2=$(calc_loss_phase 1 30 50)
LOSS_R_P2=$(calc_loss_phase 2 30 50)

[ -f cwnd_adaptive_opt.dat ] && CWND_A_P2=$(calc_avg_cwnd cwnd_adaptive_opt.dat 30 50)
[ -f cwnd_reno_opt.dat ] && CWND_R_P2=$(calc_avg_cwnd cwnd_reno_opt.dat 30 50)

echo "Throughput: Adaptive=$TP_A_P2 | Reno=$TP_R_P2"
echo "Loss: Adaptive=$LOSS_A_P2 | Reno=$LOSS_R_P2"
echo "CWND: Adaptive=$CWND_A_P2 | Reno=$CWND_R_P2"

# -------------------------------------------------------------------
echo ""
echo "=========================================="
echo " Generating time-series data..."
echo "=========================================="

generate_trace_data

echo ""
echo "DONE. Now run: gnuplot plot_optimal_all.gnu"