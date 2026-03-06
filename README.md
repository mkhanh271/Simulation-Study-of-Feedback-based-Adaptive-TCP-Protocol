# Simulation Study of Feedback-based Adaptive TCP Protocol

> An NS-2 simulation study implementing and evaluating **TCP Adaptive** — a feedback-based congestion control protocol that dynamically switches between two operating modes (FCWND and PCWND) based on real-time network conditions — benchmarked against **TCP Reno**.

---
## First Words : TCP Adaptive will perform best in MANET network with AODV routing protocol , this project only works with traditional network and simple routing protocol , you can run all the code peacafully without any bugs or errors :) , I really happy and appreciate all your contribution ~~~ Thanks yall !

## 📋 Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Requirements](#requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Test Scenarios & Phases](#test-scenarios--phases)
- [Results & Visualization](#results--visualization)
- [Key Parameters](#key-parameters)
- [References](#references)

---

## Overview

Standard TCP variants like Reno treat all packet loss as congestion signals, leading to aggressive window reduction even during route failures or temporary link disruptions. **TCP Adaptive** addresses this by:

- Maintaining an **RTT table** and **bandwidth estimator** to distinguish congestion from route failure
- Dynamically switching between **FCWND-TCP** (Full Congestion Window, for stable high-speed links) and **PCWND-TCP** (Partial Congestion Window, for lossy/variable links)
- Using a **feedback mechanism** from the routing layer to freeze the connection during route failures and restore it gracefully upon route re-establishment

This implementation is based on:
> *"Feedback based Adaptive TCP Protocol for improving Performance"*  
> International Journal of Grid and Distributed Computing, Vol. 11, No. 1 (2018)

---

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                   TCP Adaptive Agent                       │
│              (AdaptiveTcpAgent : TcpAgent)                 │
│                                                            │
│  ┌──────────────┐        ┌──────────────────────────────┐  │
│  │  Mode Switch │        │        RTT Tracker           │  │
│  │              │        │  - RTT table (last 100 pts)  │  │
│  │  FCWND ←──→ PCWND    │  - rtt_min, rtt_max, rtt_avg │  │
│  │              │        │  - EWMA smoothing            │  │
│  └──────┬───────┘        └──────────────────────────────┘  │
│         │                                                  │
│  ┌──────▼───────┐        ┌──────────────────────────────┐  │
│  │  FCWND Mode  │        │     Bandwidth Estimator      │  │
│  │              │        │  - Max throughput (20 wins)  │  │
│  │  WINDOW_OPEN │        │  - Available BW calculation  │  │
│  │  WINDOW_VAR  │        └──────────────────────────────┘  │
│  │  WINDOW_CLOSE│                                          │
│  └──────────────┘        ┌──────────────────────────────┐  │
│                          │    Feedback Mechanism        │  │
│  ┌───────────────┐       │  - route_failure → freeze   │  │
│  │  PCWND Mode   │       │  - route_reestablished       │  │
│  │               │       │    → restore cwnd/ssthresh  │  │
│  │  Window slice │       └──────────────────────────────┘  │
│  │  BW-aware adj │                                         │
│  └───────────────┘                                         │
└────────────────────────────────────────────────────────────┘
                            │
              ┌─────────────▼─────────────┐
              │      NS-2 Simulation      │
              │   optimal_test_adaptive   │
              │   .tcl  (via Tcl script)  │
              └─────────────┬─────────────┘
                            │
              ┌─────────────▼─────────────┐
              │     Trace Analysis        │
              │   analyze_optimal.sh      │
              │   calc_phase_averages.sh  │
              └─────────────┬─────────────┘
                            │
              ┌─────────────▼─────────────┐
              │      Visualization        │
              │   plot_optimal_all.gnu    │
              └───────────────────────────┘
```

### Two Operating Modes

| Mode | Trigger Condition | Behavior |
|------|------------------|----------|
| **FCWND** (Full CWnd) | Low loss, stable RTT, high BW utilization | Aggressive window growth; 3-state machine: OPEN → VARIANT → CLOSE |
| **PCWND** (Partial CWnd) | Loss > 5%, RTT variation > 30%, low utilization | Conservative window slicing (70%), bandwidth-aware adjustment |

---

## Project Structure

```
.
├── tcp_adaptive_h.c               # Header: class definitions, structs, enums
├── tcp_adaptive_cc.c              # Implementation: FCWND, PCWND, feedback logic
├── optimal_test_adaptive_tcl.ps1  # NS-2 Tcl simulation script
├── run_test.sh                    # Full pipeline automation script
├── analyze_optimal.sh             # Trace file parser (throughput, RTT, delay, loss)
├── calc_phase_averages.sh         # Per-phase metric aggregator
├── plot_optimal_all.gnu           # Gnuplot visualization script
├── cwnd_comparison.png            # Result: CWND evolution plot
├── metrics_vs_time.png            # Result: 4-metric time-series comparison
├── phase_comparison.png           # Result: Performance by phase
└── README.md
```

| File | Role |
|---|---|
| `tcp_adaptive_h.c` | Defines `AdaptiveTcpAgent`, `RTTStats`, `BandwidthEstimator`, enums for modes/states |
| `tcp_adaptive_cc.c` | Full implementation of FCWND/PCWND logic, mode switching, feedback handler |
| `optimal_test_adaptive_tcl.ps1` | NS-2 Tcl topology and simulation configuration |
| `run_test.sh` | One-command pipeline: simulate → analyze → plot |
| `analyze_optimal.sh` | AWK-based trace parser generating `.dat` files for gnuplot |
| `calc_phase_averages.sh` | Computes per-phase averages for phase comparison plots |
| `plot_optimal_all.gnu` | Generates all 4 PNG result plots via gnuplot |

---

## Requirements

### System
- Linux (Ubuntu 18.04+ recommended)
- [NS-2](https://sourceforge.net/projects/nsnam/) ≥ 2.35
- GCC / G++ (for compiling TCP Adaptive into NS-2)
- [Gnuplot](http://www.gnuplot.info/) ≥ 5.0
- Bash, AWK (standard Unix tools)

### Install NS-2 (Ubuntu)
```bash
sudo apt-get install ns2 nam
```

### Install Gnuplot
```bash
sudo apt-get install gnuplot
```

---

## Installation

### Step 1 — Clone the repository
```bash
git clone https://github.com/<your-username>/<repo-name>.git
cd <repo-name>
```

### Step 2 — Integrate TCP Adaptive into NS-2

Copy the source files into the NS-2 source tree and recompile:

```bash
# Copy to NS-2 TCP directory (adjust path to your NS-2 install)
cp tcp_adaptive_h.c  /path/to/ns-2.xx/tcp/tcp-adaptive.h
cp tcp_adaptive_cc.c /path/to/ns-2.xx/tcp/tcp-adaptive.cc

# Rebuild NS-2
cd /path/to/ns-2.xx
make clean && make
```

> The agent registers itself as `Agent/TCP/Adaptive` in the Tcl namespace, so it can be instantiated directly in simulation scripts.

---

## Usage

### Option A — Run Full Pipeline (Recommended)

```bash
chmod +x run_test.sh
sudo ./run_test.sh
```

This script automatically runs all 8 steps:
1. Checks required files
2. Cleans old results
3. Runs the NS-2 simulation
4. Verifies the trace file
5. Analyzes results (`analyze_optimal.sh`)
6. Calculates phase averages (`calc_phase_averages.sh`)
7. Generates plots (`gnuplot`)
8. Verifies output PNG files

---

### Option B — Step by Step

```bash
# Step 1: Run NS-2 simulation
ns optimal_test_adaptive.tcl

# Step 2: Analyze trace file
chmod +x analyze_optimal.sh
./analyze_optimal.sh

# Step 3: Calculate phase averages
chmod +x calc_phase_averages.sh
./calc_phase_averages.sh

# Step 4: Generate plots
gnuplot plot_optimal_all.gnu
```

---

## Test Scenarios & Phases

The simulation runs for **120 seconds** with two distinct network phases, comparing TCP Adaptive against TCP Reno on the same topology.

### Network Topology

```
Node 0 (Adaptive src) ──┐
                         ├──► Node 2 ──[bottleneck]──► Node 3 ──┬──► Node 4 (Adaptive dst)
Node 1 (Reno src)     ──┘                                        └──► Node 5 (Reno dst)
```

- Bottleneck link: **10 Mbps, 40ms delay**
- Access links: **100 Mbps, 2ms delay**
- Queue: **DropTail**, 50 packets

---

### Phase 1 — Clean Link (t = 2–30s)

Both flows transmit over a stable, loss-free link.

| Metric | TCP Adaptive | TCP Reno |
|--------|-------------|----------|
| Throughput | **12.90 Mbps** | 9.59 Mbps |
| Packet Loss | **0.02%** | 0.19% |
| Avg CWND | **~46 pkts** | ~36 pkts |

**Expected behavior:** TCP Adaptive stays in FCWND mode, with the WINDOW_OPEN state allowing aggressive growth. Reno is more conservative due to its standard AIMD.

---

### Phase 2 — Lossy Link (t = 30–50s)

Random packet loss is injected to simulate a degraded wireless or congested link.

| Metric | TCP Adaptive | TCP Reno |
|--------|-------------|----------|
| Throughput | 1.12 Mbps | 1.06 Mbps |
| Packet Loss | **1.68%** | 2.05% |
| Avg CWND | **~5 pkts** | ~5 pkts |

**Expected behavior:** TCP Adaptive detects rising loss and RTT variation, switches to PCWND mode, slices the window (×0.7) and adjusts conservatively — recovering faster than Reno.

---

### Overall Performance (t = 2–80s)

| Metric | TCP Adaptive | TCP Reno | Improvement |
|--------|-------------|----------|-------------|
| Throughput | **5.29 Mbps** | 4.05 Mbps | **+30.6%** |
| Packet Loss | **0.27%** | 0.50% | **−46%** |

---

## Results & Visualization

Four PNG plots are generated after running the pipeline:

### `metrics_vs_time.png`
Four-panel time-series comparison (0–120s):
- **Throughput (Mbps):** TCP Adaptive consistently higher in Phase 1
- **RTT (ms):** Adaptive maintains lower RTT spikes during lossy phase
- **End-to-End Delay (ms):** One-way delay from source to destination
- **Packet Loss Rate (%):** Adaptive recovers faster after the lossy phase

### `cwnd_comparison.png`
Side-by-side CWND evolution for both protocols:
- TCP Adaptive (green): Avg CWND = **19.01 pkts**
- TCP Reno (red): Avg CWND = **15.73 pkts**
- Shows aggressive growth in Phase 1 and controlled reduction in Phase 2

### `phase_comparison.png`
Four-panel bar/line chart showing per-phase averages:
- Average Throughput by Phase
- Packet Loss Rate by Phase
- Average RTT by Phase
- Average CWND by Phase

### Generated Data Files

| File | Description |
|------|-------------|
| `throughput_adaptive.dat` / `throughput_reno.dat` | Per-second throughput (Mbps) |
| `rtt_adaptive.dat` / `rtt_reno.dat` | RTT samples from seq/ACK tracking (ms) |
| `delay_adaptive.dat` / `delay_reno.dat` | One-way end-to-end delay (ms) |
| `loss_adaptive.dat` / `loss_reno.dat` | Per-second packet loss rate (%) |
| `cwnd_adaptive_opt.dat` / `cwnd_reno_opt.dat` | Congestion window over time |
| `phase_throughput.dat`, `phase_rtt.dat`, etc. | Phase-averaged metrics for comparison plot |

---

## Key Parameters

### Paper Parameters (Section 5)

| Parameter | Default | Description |
|-----------|---------|-------------|
| `Wlow_` | 16 | Low window threshold |
| `Whigh_` | 1024 | High window threshold |
| `bhigh_` | 0.9 | High decrease factor for PCWND |

### Mode Selection Thresholds

| Parameter | Default | Description |
|-----------|---------|-------------|
| `loss_threshold_` | 0.05 | Loss rate (5%) → switch to PCWND |
| `delay_threshold_` | 0.3 | RTT variation (30%) → switch to PCWND |
| `bw_util_high_` | 0.8 | Bandwidth utilization (80%) → stay FCWND |
| `bw_util_low_` | 0.3 | Bandwidth utilization (30%) lower bound |

### FCWND Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `variation_low_` | 0.1 | Low variation threshold → WINDOW_OPEN |
| `variation_high_` | 0.3 | High variation threshold → WINDOW_CLOSE |
| `aggressive_increment_factor_` | 2.0 | Multiplier for aggressive cwnd growth |

### PCWND Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `slice_ratio_` | 0.7 | Window slicing ratio (reduce to 70%) |
| `medium_cwnd_` | 100 | Medium window size reference |
| `under_usage_threshold_` | 0.3 | Under-utilization threshold (30%) |
| `full_usage_threshold_` | 0.9 | Full-utilization threshold (90%) |

---

## References

- *"Feedback based Adaptive TCP Protocol for improving Performance"* — International Journal of Grid and Distributed Computing, Vol. 11, No. 1, 2018
- [NS-2 Network Simulator](https://sourceforge.net/projects/nsnam/)
- [The NS Manual](https://www.isi.edu/nsnam/ns/doc/)
- Jain, R. — *"A Delay-based Approach for Congestion Avoidance in Interconnected Heterogeneous Computer Networks"*, ACM SIGCOMM, 1989

---

## License

MIT License — see [LICENSE](LICENSE) for details.
