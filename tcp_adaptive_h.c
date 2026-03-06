#ifndef ns_tcp_adaptive_h
#define ns_tcp_adaptive_h

#include "tcp.h"
#include <math.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>

/* =================== TCP Mode Selection =================== */
enum TCPMode {
    PCWND_MODE,     // Partial Congestion Window (lossy networks)
    FCWND_MODE      // Full Congestion Window (high-speed networks)
};

/* =================== Window State for FCWND =================== */
enum WindowState {
    WINDOW_OPEN,     // Low variation, increase aggressively
    WINDOW_VARIANT,  // Medium variation, moderate adjustment
    WINDOW_CLOSE     // High variation, reduce window
};

/* =================== Feedback Types =================== */
enum FeedbackType {
    NO_FEEDBACK,
    ROUTE_FAILURE,
    ROUTE_REESTABLISHED
};

/* =================== RTT Table Entry (Section 4.1) =================== */
struct RTTTableEntry {
    double timestamp;
    double rtt_value;
    int seq_num;
    
    RTTTableEntry(double ts, double rtt, int seq) 
        : timestamp(ts), rtt_value(rtt), seq_num(seq) {}
};

/* =================== RTT Statistics =================== */
struct RTTStats {
    std::vector<RTTTableEntry> rtt_table;
    double rtt_min;
    double rtt_max;
    double rtt_avg;
    double rtt_current;
    int sample_count;
    
    RTTStats() {
        reset();
    }
    
    void update(double rtt, double timestamp, int seq) {
        rtt_current = rtt;
        rtt_table.push_back(RTTTableEntry(timestamp, rtt, seq));
        sample_count++;
        
        // Keep only last 100 samples
        if (rtt_table.size() > 100) {
            rtt_table.erase(rtt_table.begin());
        }
        
        if (rtt_table.size() > 0) {
            rtt_min = rtt_table[0].rtt_value;
            rtt_max = rtt_table[0].rtt_value;
            double sum = 0.0;
            
            for (size_t i = 0; i < rtt_table.size(); i++) {
                if (rtt_table[i].rtt_value < rtt_min) 
                    rtt_min = rtt_table[i].rtt_value;
                if (rtt_table[i].rtt_value > rtt_max) 
                    rtt_max = rtt_table[i].rtt_value;
                sum += rtt_table[i].rtt_value;
            }
            
            rtt_avg = sum / rtt_table.size();
        }
    }
    
    void reset() {
        rtt_table.clear();
        rtt_min = rtt_max = rtt_avg = rtt_current = 0.0;
        sample_count = 0;
    }
    
    bool has_enough_samples() {
        return sample_count >= 5;
    }
};

/* =================== Bandwidth Estimator =================== */
struct BandwidthEstimator {
    double estimated_bw;      // Estimated link bandwidth (Mbps)
    double available_bw;      // Available bandwidth (Mbps)
    double last_update_time;
    std::vector<double> throughput_samples;
    double packet_size_bytes; // Packet size for conversion
    
    BandwidthEstimator() {
        estimated_bw = 5.0;   // Default 5 Mbps
        available_bw = 0.0;
        last_update_time = 0.0;
        packet_size_bytes = 1000.0; // Default 1000 bytes
    }
    
    void set_packet_size(double size) {
        packet_size_bytes = size;
    }
    
    void update(double throughput, double current_time) {
        throughput_samples.push_back(throughput);
        
        if (throughput_samples.size() > 20) {
            throughput_samples.erase(throughput_samples.begin());
        }
        
        if (throughput_samples.size() > 0) {
            estimated_bw = *std::max_element(
                throughput_samples.begin(), 
                throughput_samples.end()
            );
        }
        
        last_update_time = current_time;
    }
    
    // FIXED: Correct formula from Section 4.2 using rtt_current
    void calculate_available(double rtt_min, double rtt_current, double cwnd) {
        if (rtt_min > 0 && rtt_current > 0 && packet_size_bytes > 0) {
            // Expected rate = maximum possible throughput (packets/sec)
            double expected_rate = cwnd / rtt_min;
            
            // Current rate = actual throughput (packets/sec)
            double current_rate = cwnd / rtt_current;
            
            // Available = unused capacity (packets/sec)
            double unused_pkts_per_sec = expected_rate - current_rate;
            
            if (unused_pkts_per_sec < 0) unused_pkts_per_sec = 0;
            
            // Convert to Mbps
            available_bw = (unused_pkts_per_sec * packet_size_bytes * 8.0) / 1e6;
        } else {
            available_bw = 0.0;
        }
    }
};

/* =================== Main TCP Adaptive Agent =================== */
class AdaptiveTcpAgent : public TcpAgent {
public:
    AdaptiveTcpAgent();
    virtual ~AdaptiveTcpAgent();

    virtual void recv(Packet *pkt, Handler *);
    virtual void timeout(int tno);
    
    // Routing layer interface (call from AODV)
    void notify_route_failure();
    void notify_route_reestablished();

protected:
    /* ===== Core TCP functions ===== */
    virtual void opencwnd();
    virtual void slowdown(int how);
    
    /* ===== Mode selection ===== */
    TCPMode select_mode();
    void switch_mode(TCPMode new_mode);
    bool should_switch_mode();
    
    /* ===== FCWND-TCP specific ===== */
    void fcwnd_opencwnd();
    void fcwnd_slowdown();
    WindowState get_window_state();
    double calculate_arrival_rate();
    double calculate_expected_rate();
    double calculate_variation();
    void update_window_state_history();
    double get_fcwnd_increment();
    double calculate_avg_variation_since_open();  // NEW: Phase 1A
    
    /* ===== PCWND-TCP specific ===== */
    void pcwnd_opencwnd();
    void pcwnd_slowdown();
    double calculate_bandwidth_availability();
    double calculate_frame_setting_value();
    double calculate_unused_bandwidth();
    bool is_dynamic_window_mode();
    bool is_desired_hint_mode();
    bool is_under_usage();
    bool is_full_usage();
    
    /* ===== Feedback mechanism ===== */
    void handle_feedback(FeedbackType feedback);
    void freeze_connection();
    void unfreeze_connection();
    bool is_route_failure_loss();
    
    /* ===== RTT tracking ===== */
    void update_rtt_stats(double rtt, int seq);
    
    /* ===== Bandwidth estimation ===== */
    void update_bandwidth_estimate();
    double calculate_current_throughput();
    double get_bandwidth_utilization();
    
    /* ===== Paper formulas (Section 5) ===== */
    double p_cwnd();      // Loss rate
    double B_cwnd();      // Adaptive decrease factor
    double A_cwnd();      // Adaptive increase factor

    /* =================== Parameters (bind to TCL) =================== */
    
    // Paper parameters (Section 5)
    double Wlow_;         // Low window threshold (default: 16)
    double Whigh_;        // High window threshold (default: 1024)
    double bhigh_;        // High decrease factor (default: 0.9)
    
    // Mode selection thresholds
    double loss_threshold_;      // Threshold to switch to PCWND (default: 0.05)
    double delay_threshold_;     // RTT variation threshold (default: 0.3)
    double bw_util_high_;        // High bandwidth utilization (default: 0.8)
    double bw_util_low_;         // Low bandwidth utilization (default: 0.3)
    
    // PCWND parameters
    double slice_ratio_;         // Window slicing ratio (default: 0.7)
    double medium_cwnd_;         // Medium window size (default: 100)
    double unused_bw_threshold_; // Threshold for unused bandwidth
    double under_usage_threshold_;   // Threshold for under-usage (default: 0.3)
    double full_usage_threshold_;    // Threshold for full usage (default: 0.9)
    
    // FCWND parameters
    double variation_low_;       // Low variation threshold (default: 0.1)
    double variation_high_;      // High variation threshold (default: 0.3)
    double aggressive_increment_factor_;  // Factor for aggressive increment
    
    /* =================== State variables =================== */
    
    // Current mode
    TCPMode current_mode_;
    TCPMode previous_mode_;
    
    // Window state (for FCWND)
    WindowState window_state_;
    WindowState previous_window_state_;
    double last_open_point_;
    double last_variant_point_;
    double last_open_time_;      // NEW: Phase 1A - timestamp when entered OPEN
    double last_open_cwnd_;      // NEW: Phase 1A - cwnd snapshot when entered OPEN
    int consecutive_open_count_;
    int consecutive_variant_count_;
    int consecutive_close_count_;
    
    // Loss statistics
    int pkt_sent_;
    int pkt_lost_;
    int pkt_delivered_;
    double last_loss_time_;
    
    // RTT tracking
    RTTStats rtt_stats_;
    
    // Feedback state
    bool connection_frozen_;
    double frozen_cwnd_;
    double frozen_ssthresh_;
    double freeze_time_;
    bool route_failure_detected_;
    
    // Bandwidth estimation
    BandwidthEstimator bw_estimator_;
    double last_throughput_update_;
    double throughput_update_interval_;
    int bytes_sent_since_update_;
    
    // Timing
    double last_mode_switch_time_;
    double mode_switch_interval_;  // Minimum interval between mode switches
    double last_ack_time_;
    
    // Statistics for debugging
    int mode_switches_;
    int route_failures_;
    double total_freeze_time_;
};

#endif /* ns_tcp_adaptive_h */