/*
 * TCP Adaptive Implementation
 * Based on: "Feedback based Adaptive TCP Protocol for improving Performance"
 * Paper: International Journal of Grid and Distributed Computing Vol. 11, No. 1 (2018)
 * 
 * Implements FCWND-TCP and PCWND-TCP with adaptive mode switching
 */

#include "tcp-adaptive.h"
#include "flags.h"
#include "random.h"
#include "template.h"

/* =================== Tcl registration =================== */
static class AdaptiveTcpClass : public TclClass {
public:
    AdaptiveTcpClass() : TclClass("Agent/TCP/Adaptive") {}
    TclObject* create(int, const char*const*) {
        return (new AdaptiveTcpAgent());
    }
} class_adaptive_tcp;
int AdaptiveTcpAgentClass_initialized = 0;

/* =================== Constructor =================== */
AdaptiveTcpAgent::AdaptiveTcpAgent() : TcpAgent()
{
    /* ===== BIND FIRST (trước khi gán giá trị) ===== */
    bind("Wlow_", &Wlow_);
    bind("Whigh_", &Whigh_);
    bind("bhigh_", &bhigh_);
    bind("loss_threshold_", &loss_threshold_);
    bind("delay_threshold_", &delay_threshold_);
    bind("bw_util_high_", &bw_util_high_);
    bind("bw_util_low_", &bw_util_low_);
    bind("slice_ratio_", &slice_ratio_);
    bind("medium_cwnd_", &medium_cwnd_);
    bind("variation_low_", &variation_low_);
    bind("variation_high_", &variation_high_);
    bind("unused_bw_threshold_", &unused_bw_threshold_);
    bind("under_usage_threshold_", &under_usage_threshold_);
    bind("full_usage_threshold_", &full_usage_threshold_);
    bind("aggressive_increment_factor_", &aggressive_increment_factor_);

    /* Paper default values  */
    Wlow_  = 16.0;
    Whigh_ = 1024.0;
    bhigh_ = 0.9;
    
    /* Mode selection thresholds */
    loss_threshold_ = 0.05;      // 5% loss → switch to PCWND
    delay_threshold_ = 0.3;      // 30% RTT variation → PCWND
    bw_util_high_ = 0.8;         // 80% utilization → FCWND
    bw_util_low_ = 0.3;          // 30% utilization
    
    /* PCWND parameters */
    slice_ratio_ = 0.7;          // Reduce to 70% of cwnd
    medium_cwnd_ = 100.0;
    unused_bw_threshold_ = 0.5;  // 50% of link bandwidth
    under_usage_threshold_ = 0.3; // 30% usage
    full_usage_threshold_ = 0.9;  // 90% usage
    
    /* FCWND parameters */
    variation_low_ = 0.1;        // 10% variation
    variation_high_ = 0.3;       // 30% variation
    aggressive_increment_factor_ = 2.0;
    
    /* Initialize state */
    current_mode_ = FCWND_MODE;   // Start with FCWND (high-speed)
    previous_mode_ = FCWND_MODE;
    window_state_ = WINDOW_OPEN;
    previous_window_state_ = WINDOW_OPEN;
    last_open_point_ = 0.0;
    last_variant_point_ = 0.0;
    last_open_time_ = 0.0;        // Phase 1A
    last_open_cwnd_ = 0.0;        // Phase 1A
    
    consecutive_open_count_ = 0;
    consecutive_variant_count_ = 0;
    consecutive_close_count_ = 0;
    
    pkt_sent_ = 1;               // Avoid division by zero
    pkt_lost_ = 0;
    pkt_delivered_ = 0;
    last_loss_time_ = 0.0;
    
    connection_frozen_ = false;
    frozen_cwnd_ = 0.0;
    frozen_ssthresh_ = 0.0;
    freeze_time_ = 0.0;
    route_failure_detected_ = false;
    
    last_throughput_update_ = 0.0;
    throughput_update_interval_ = 0.5;  // Update every 0.5 seconds
    bytes_sent_since_update_ = 0;
    
    last_mode_switch_time_ = 0.0;
    mode_switch_interval_ = 1.0; // Switch mode at most once per second
    last_ack_time_ = 0.0;
    
    mode_switches_ = 0;
    route_failures_ = 0;
    total_freeze_time_ = 0.0;
    
    rtt_stats_.reset();

}

AdaptiveTcpAgent::~AdaptiveTcpAgent() {}

/* =================== Paper formulas (Section 5) =================== */

double AdaptiveTcpAgent::p_cwnd()
{
    if (pkt_sent_ <= 0) return 0.0;
    return (double)pkt_lost_ / (double)pkt_sent_;
}

double AdaptiveTcpAgent::B_cwnd()
{
    // Formula from Section 5: Adaptive decrease factor
    // B(cwnd) = 0.5 for cwnd <= Wlow
    // B(cwnd) = bhigh for cwnd >= Whigh
    // B(cwnd) = 0.5 + [(log(cwnd) - log(Wlow)) / (log(Whigh) - log(Wlow))] × (bhigh - 0.5)
    
    if (cwnd_ <= Wlow_)  return 0.5;
    if (cwnd_ >= Whigh_) return bhigh_;

    double log_cwnd = log(cwnd_);
    double log_wlow = log(Wlow_);
    double log_whigh = log(Whigh_);
    
    double x = (log_cwnd - log_wlow) / (log_whigh - log_wlow);
    return 0.5 + x * (bhigh_ - 0.5);
}

double AdaptiveTcpAgent::A_cwnd()
{
    // Formula from Section 5: Adaptive increase factor
    // A(cwnd) = (2 × cwnd² × B(cwnd) × p(cwnd)) / (2 - B(cwnd))
    
    double p = p_cwnd();
    if (p <= 0.0) p = 1e-6;  // Avoid division by zero

    double B = B_cwnd();
    if (B >= 2.0) B = 1.99;  // Avoid division by zero
    
    double numerator = 2.0 * cwnd_ * cwnd_ * B * p;
    double denominator = 2.0 - B;
    
    return numerator / denominator;
}

/* =================== RTT tracking =================== */

void AdaptiveTcpAgent::update_rtt_stats(double rtt, int seq)
{
    double current_time = Scheduler::instance().clock();
    rtt_stats_.update(rtt, current_time, seq);
    
    // Set packet size for bandwidth calculation
    bw_estimator_.set_packet_size((double)size_);
    
    // Calculate bandwidth availability (Section 4.2) - FIXED Phase 1B
    if (rtt_stats_.rtt_min > 0 && rtt_stats_.rtt_current > 0) {
        bw_estimator_.calculate_available(rtt_stats_.rtt_min, 
                                          rtt_stats_.rtt_current,  // Use rtt_current
                                          cwnd_);
    }
}

/* =================== Bandwidth estimation =================== */

double AdaptiveTcpAgent::calculate_current_throughput()
{
    double now = Scheduler::instance().clock();
    double time_elapsed = now - last_throughput_update_;
    
    if (time_elapsed > 0 && bytes_sent_since_update_ > 0) {
        // Throughput in Mbps
        return (bytes_sent_since_update_ * 8.0) / (time_elapsed * 1e6);
    }
    
    return 0.0;
}

double AdaptiveTcpAgent::get_bandwidth_utilization()
{
    if (bw_estimator_.estimated_bw <= 0) return 0.0;
    
    double current_throughput = calculate_current_throughput();
    return current_throughput / bw_estimator_.estimated_bw;
}

void AdaptiveTcpAgent::update_bandwidth_estimate()
{
    double now = Scheduler::instance().clock();
    
    if (now - last_throughput_update_ >= throughput_update_interval_) {
        double throughput = calculate_current_throughput();
        
        if (throughput > 0) {
            bw_estimator_.update(throughput, now);
        }
        
        last_throughput_update_ = now;
        bytes_sent_since_update_ = 0;
    }
}

/* =================== Mode selection (Section 4) - Phase 1E SIMPLIFIED =================== */

bool AdaptiveTcpAgent::should_switch_mode()
{
    // Check if enough time has passed since last switch
    double now = Scheduler::instance().clock();
    if (now - last_mode_switch_time_ < mode_switch_interval_) {
        return false;
    }
    
    // Need enough RTT samples to make decision
    if (!rtt_stats_.has_enough_samples()) {
        return false;
    }
    
    return true;
}

TCPMode AdaptiveTcpAgent::select_mode()
{
    if (!should_switch_mode()) {
        return current_mode_;
    }
    
    double loss_rate = p_cwnd();
    double rtt_variation = 0.0;
    
    // Calculate RTT variation
    if (rtt_stats_.rtt_avg > 0) {
        rtt_variation = (rtt_stats_.rtt_max - rtt_stats_.rtt_min) 
                        / rtt_stats_.rtt_avg;
    }
    
    // SIMPLIFIED decision logic from Section 4 (Phase 1E)
    // PCWND: for lossy links (high loss OR high delay variation)
    // FCWND: for stable high-speed networks (low loss AND low variation)
    
    if (loss_rate > loss_threshold_ || rtt_variation > delay_threshold_) {
        return PCWND_MODE;
    }
    
    return FCWND_MODE;
}

void AdaptiveTcpAgent::switch_mode(TCPMode new_mode)
{
    if (new_mode != current_mode_) {
        previous_mode_ = current_mode_;
        current_mode_ = new_mode;
        last_mode_switch_time_ = Scheduler::instance().clock();
        mode_switches_++;
        
        // Reset mode-specific state
        if (new_mode == FCWND_MODE) {
            window_state_ = WINDOW_OPEN;
            last_open_point_ = cwnd_;
            last_open_time_ = Scheduler::instance().clock();  // Phase 1A
            last_open_cwnd_ = cwnd_;                          // Phase 1A
            consecutive_open_count_ = 0;
            consecutive_variant_count_ = 0;
            consecutive_close_count_ = 0;
        }
        
        if (1) {
            printf("%.6f TCP Adaptive: Mode switch %s -> %s (loss=%.4f, rtt_var=%.4f)\n",
                   Scheduler::instance().clock(),
                   (previous_mode_ == FCWND_MODE ? "FCWND" : "PCWND"),
                   (current_mode_ == FCWND_MODE ? "FCWND" : "PCWND"),
                   p_cwnd(),
                   rtt_stats_.rtt_avg > 0 ? 
                       (rtt_stats_.rtt_max - rtt_stats_.rtt_min) / rtt_stats_.rtt_avg : 0.0);
        }
    }
}

/* =================== FCWND-TCP Implementation (Section 4.1) - Phase 1A =================== */

double AdaptiveTcpAgent::calculate_arrival_rate()
{
    // Arrival rate = cwnd / RTT_current
    if (rtt_stats_.rtt_current > 0) {
        return cwnd_ / rtt_stats_.rtt_current;
    }
    return 0.0;
}

double AdaptiveTcpAgent::calculate_expected_rate()
{
    // Expected rate = cwnd / RTT_min
    if (rtt_stats_.rtt_min > 0) {
        return cwnd_ / rtt_stats_.rtt_min;
    }
    return 0.0;
}

double AdaptiveTcpAgent::calculate_variation()
{
    double arrival = calculate_arrival_rate();
    double expected = calculate_expected_rate();
    
    if (expected > 0) {
        return fabs(arrival - expected) / expected;
    }
    return 0.0;
}

// NEW Phase 1A: Calculate average variation from last open point
double AdaptiveTcpAgent::calculate_avg_variation_since_open()
{
    if (last_open_time_ <= 0.0 || rtt_stats_.rtt_table.empty()) {
        return calculate_variation(); // Fallback to instant variation
    }
    
    double sum = 0.0;
    int count = 0;
    
    for (const auto &entry : rtt_stats_.rtt_table) {
        if (entry.timestamp >= last_open_time_) {
            // Calculate variation using window at open point
            double arrival_rate = 0.0;
            double expected_rate = 0.0;
            
            if (entry.rtt_value > 0) {
                arrival_rate = last_open_cwnd_ / entry.rtt_value;
            }
            
            if (rtt_stats_.rtt_min > 0) {
                expected_rate = last_open_cwnd_ / rtt_stats_.rtt_min;
            }
            
            if (expected_rate > 0) {
                double var = fabs(arrival_rate - expected_rate) / expected_rate;
                sum += var;
                count++;
            }
        }
    }
    
    return (count > 0) ? (sum / count) : calculate_variation();
}

void AdaptiveTcpAgent::update_window_state_history()
{
    WindowState new_state = get_window_state();
    
    // Phase 1A: Capture timestamp when entering OPEN state
    if (new_state == WINDOW_OPEN && window_state_ != WINDOW_OPEN) {
        last_open_time_ = Scheduler::instance().clock();
        last_open_cwnd_ = cwnd_;
    }
    
    // Track consecutive state occurrences
    if (new_state == window_state_) {
        if (new_state == WINDOW_OPEN) consecutive_open_count_++;
        else if (new_state == WINDOW_VARIANT) consecutive_variant_count_++;
        else consecutive_close_count_++;
    } else {
        // State changed, reset counters
        consecutive_open_count_ = (new_state == WINDOW_OPEN) ? 1 : 0;
        consecutive_variant_count_ = (new_state == WINDOW_VARIANT) ? 1 : 0;
        consecutive_close_count_ = (new_state == WINDOW_CLOSE) ? 1 : 0;
    }
    
    previous_window_state_ = window_state_;
    window_state_ = new_state;
}

WindowState AdaptiveTcpAgent::get_window_state()
{
    double variation = calculate_variation();
    
    // State machine from Section 4.1
    if (variation < variation_low_) {
        return WINDOW_OPEN;
    } else if (variation < variation_high_) {
        return WINDOW_VARIANT;
    } else {
        return WINDOW_CLOSE;
    }
}

double AdaptiveTcpAgent::get_fcwnd_increment()
{
    double increment;
    
    // "If window size is small, FCWND works similar to basic TCP"
    if (cwnd_ < Wlow_) {
        increment = 1.0 / cwnd_;
    }
    // "If window size is large, FCWND increments by choosing value based on exact window size"
    else if (cwnd_ >= Whigh_) {
        increment = A_cwnd() / cwnd_;
        
        // Apply aggressive factor for sustained OPEN state
        if (window_state_ == WINDOW_OPEN && consecutive_open_count_ > 5) {
            increment *= aggressive_increment_factor_;
        }
    }
    // Medium window: interpolate
    else {
        double alpha = (log(cwnd_) - log(Wlow_)) / (log(Whigh_) - log(Wlow_));
        double std_increment = 1.0 / cwnd_;
        double adaptive_increment = A_cwnd() / cwnd_;
        increment = std_increment * (1 - alpha) + adaptive_increment * alpha;
        
        // Apply window state modulation
        switch (window_state_) {
            case WINDOW_OPEN:
                increment *= 1.5;
                break;
            case WINDOW_VARIANT:
                break;
            case WINDOW_CLOSE:
                increment *= 0.5;
                break;
        }
    }
    
    return increment;
}

void AdaptiveTcpAgent::fcwnd_opencwnd()
{
    pkt_sent_++;
    
    // Update window state based on network conditions
    update_window_state_history();
    
    if (cwnd_ < ssthresh_) {
        // Slow start phase
        if (cwnd_ >= Wlow_) {
            cwnd_ += 2.0;
        } else {
            cwnd_ += 1.0;
        }
    } else {
        // Congestion avoidance with PROPER average variation (Phase 1A)
        double avg_variation = calculate_avg_variation_since_open();
        double increment;
        
        // Paper logic (Section 4.1):
        // "If variation is little, use normal TCP"
        if (avg_variation < variation_low_) {
            increment = 1.0 / cwnd_;  // Normal TCP increment
        }
        // "If variation is more, adjust window to highest size"
        else if (avg_variation > variation_high_) {
            // Aggressive increase towards Whigh
            increment = A_cwnd() / cwnd_;
            increment *= aggressive_increment_factor_;
        }
        // Medium variation
        else {
            increment = get_fcwnd_increment();
        }
        
        cwnd_ += increment;
        
        // Track state change points
        if (window_state_ == WINDOW_OPEN) {
            last_open_point_ = cwnd_;
        } else if (window_state_ == WINDOW_VARIANT) {
            last_variant_point_ = cwnd_;
        }
    }
    
    // Bounds checking
    if (cwnd_ > Whigh_ * 2) cwnd_ = Whigh_ * 2;
    if (cwnd_ < 1.0) cwnd_ = 1.0;
}

void AdaptiveTcpAgent::fcwnd_slowdown()
{
    pkt_lost_++;
    last_loss_time_ = Scheduler::instance().clock();
    
    double B = B_cwnd();
    int new_ssthresh;
    
    // Phase 1D: "Increase LARGER size, decrease SMALLER size"
    // Decrease differently based on window magnitude
    
    if (cwnd_ <= Wlow_) {
        // Small window: behave like standard TCP
        new_ssthresh = (int)(cwnd_ * 0.5);
    }
    else if (cwnd_ >= Whigh_) {
        // Large window: use full adaptive decrease
        new_ssthresh = (int)(cwnd_ * (1.0 - B));
    }
    else {
        // Medium window: interpolate between standard and adaptive
        double alpha = (log(cwnd_) - log(Wlow_)) / (log(Whigh_) - log(Wlow_));
        double std_decrease = cwnd_ * 0.5;
        double adaptive_decrease = cwnd_ * (1.0 - B);
        new_ssthresh = (int)(std_decrease * (1.0 - alpha) + adaptive_decrease * alpha);
    }
    
    if (new_ssthresh < 2) new_ssthresh = 2;
    
    ssthresh_ = new_ssthresh;
    cwnd_ = ssthresh_;
    
    // Reset window state to CLOSE after packet loss
    window_state_ = WINDOW_CLOSE;
    consecutive_open_count_ = 0;
    consecutive_variant_count_ = 0;
    consecutive_close_count_ = 1;
}

/* =================== PCWND-TCP Implementation (Section 4.2) - Phase 1C =================== */

double AdaptiveTcpAgent::calculate_bandwidth_availability()
{
    return bw_estimator_.available_bw;
}

double AdaptiveTcpAgent::calculate_unused_bandwidth()
{
    double bw_avail = calculate_bandwidth_availability();
    double current_usage = calculate_current_throughput();
    
    return bw_avail - current_usage;
}

bool AdaptiveTcpAgent::is_under_usage()
{
    double bw_util = get_bandwidth_utilization();
    return (bw_util < under_usage_threshold_);
}

bool AdaptiveTcpAgent::is_full_usage()
{
    double bw_util = get_bandwidth_utilization();
    return (bw_util > full_usage_threshold_);
}

bool AdaptiveTcpAgent::is_dynamic_window_mode()
{
    double unused_bw = calculate_unused_bandwidth();
    double threshold = unused_bw_threshold_ * bw_estimator_.estimated_bw;
    
    return (cwnd_ > medium_cwnd_ && unused_bw > threshold);
}

bool AdaptiveTcpAgent::is_desired_hint_mode()
{
    return (cwnd_ < medium_cwnd_);
}

// Phase 1C: CORRECTED frame setting calculation
double AdaptiveTcpAgent::calculate_frame_setting_value()
{
    double pkt_bytes = (double)size_;
    if (pkt_bytes <= 0) pkt_bytes = 1000.0;  // Default
    
    // Get unused bandwidth (Mbps)
    double unused_bw_mbps = bw_estimator_.available_bw;
    
    // Convert to packets per second
    double extra_pkts_per_sec = 0.0;
    if (unused_bw_mbps > 0) {
        extra_pkts_per_sec = (unused_bw_mbps * 1e6) / (pkt_bytes * 8.0);
    }
    
    // Calculate extra inflight packets based on RTT
    double rtt = (rtt_stats_.rtt_avg > 0) ? rtt_stats_.rtt_avg : rtt_stats_.rtt_current;
    if (rtt <= 0) rtt = 0.1;  // Default 100ms
    
    double extra_inflight = extra_pkts_per_sec * rtt;
    double dynamic_window = cwnd_ + extra_inflight;
    
    // Paper logic (Section 4.2):
    double unused_threshold = unused_bw_threshold_ * bw_estimator_.estimated_bw;
    
    // 1. Dynamic window mode
    if (cwnd_ > medium_cwnd_ && unused_bw_mbps > unused_threshold) {
        return dynamic_window;
    }
    // 2. Desired hint window mode
    else if (cwnd_ < medium_cwnd_) {
        return medium_cwnd_;
    }
    // 3. Another mode
    else {
        return cwnd_;
    }
}

void AdaptiveTcpAgent::pcwnd_opencwnd()
{
    pkt_sent_++;
    
    if (cwnd_ < ssthresh_) {
        // Slow start - conservative for PCWND
        cwnd_ += 1.0;
    } else {
        // Congestion avoidance - Phase 1C
        double base_increment = 1.0 / cwnd_;
        double frame_value = calculate_frame_setting_value();
        
        // Paper: "As per value of class, TCP was checked of under-usage or full usage"
        bool under_use = is_under_usage();
        bool full_use = is_full_usage();
        
        if (under_use) {
            // Under-utilized: increase aggressively toward frame_value
            if (frame_value > cwnd_) {
                double diff = frame_value - cwnd_;
                cwnd_ += fmax(base_increment, diff * 0.3);  // 30% of difference
            } else {
                cwnd_ += base_increment;
            }
        }
        else if (full_use) {
            // Fully utilized: be conservative
            cwnd_ = fmin(cwnd_ + base_increment * 0.5, frame_value);
        }
        else {
            // Normal: move gradually toward frame_value
            if (frame_value > cwnd_) {
                double diff = frame_value - cwnd_;
                cwnd_ += fmax(base_increment, diff * 0.1);  // 10% of difference
            } else if (frame_value < cwnd_) {
                cwnd_ = frame_value;  // Reduce immediately
            } else {
                cwnd_ += base_increment;
            }
        }
    }
    
    // Bounds checking
    if (cwnd_ < 1.0) cwnd_ = 1.0;
}

void AdaptiveTcpAgent::pcwnd_slowdown()
{
    pkt_lost_++;
    last_loss_time_ = Scheduler::instance().clock();
    
    // Partial window reduction (Section 4.2)
    ssthresh_ = (int)(cwnd_ * slice_ratio_);
    
    if (ssthresh_ < 2) {
        ssthresh_ = 2;
    }
    
    cwnd_ = ssthresh_;
}

/* =================== Feedback mechanism (CRITICAL FEATURE) =================== */

bool AdaptiveTcpAgent::is_route_failure_loss()
{
    return route_failure_detected_;
}

void AdaptiveTcpAgent::notify_route_failure()
{
    route_failure_detected_ = true;
    route_failures_++;
    
    if (1) {
        printf("%.6f TCP Adaptive: Route failure detected\n",
               Scheduler::instance().clock());
    }
    
    handle_feedback(ROUTE_FAILURE);
}

void AdaptiveTcpAgent::notify_route_reestablished()
{
    route_failure_detected_ = false;
    
    if (1) {
        printf("%.6f TCP Adaptive: Route re-established\n",
               Scheduler::instance().clock());
    }
    
    handle_feedback(ROUTE_REESTABLISHED);
}

void AdaptiveTcpAgent::handle_feedback(FeedbackType feedback)
{
    switch (feedback) {
        case ROUTE_FAILURE:
            freeze_connection();
            break;
            
        case ROUTE_REESTABLISHED:
            unfreeze_connection();
            break;
            
        case NO_FEEDBACK:
        default:
            break;
    }
}

void AdaptiveTcpAgent::freeze_connection()
{
    if (!connection_frozen_) {
        connection_frozen_ = true;
        frozen_cwnd_ = cwnd_;
        frozen_ssthresh_ = ssthresh_;
        freeze_time_ = Scheduler::instance().clock();
        
        // Stop retransmission timers (Chandran paper, Section 2)
        if (rtx_timer_.status() == TIMER_PENDING) {
            rtx_timer_.cancel();
        }
        
        if (1) {
            printf("%.6f TCP Adaptive: Connection frozen (cwnd=%.1f, ssthresh=%.1f)\n",
                   freeze_time_, frozen_cwnd_, frozen_ssthresh_);
        }
    }
}

void AdaptiveTcpAgent::unfreeze_connection()
{
    if (connection_frozen_) {
        double unfreeze_time = Scheduler::instance().clock();
        double freeze_duration = unfreeze_time - freeze_time_;
        total_freeze_time_ += freeze_duration;
        
        // Restore window sizes (Chandran paper, Section 2)
        cwnd_ = frozen_cwnd_;
        ssthresh_ = frozen_ssthresh_;
        
        connection_frozen_ = false;
        route_failure_detected_ = false;
        
        if (1) {
            printf("%.6f TCP Adaptive: Connection unfrozen (duration=%.3f, cwnd=%.1f)\n",
                   Scheduler::instance().clock(), freeze_time_, double(cwnd_));
        }
        
        // Resume transmission
        send_much(0, 0, maxburst_);
    }
}

/* =================== Main congestion control functions =================== */

void AdaptiveTcpAgent::opencwnd()
{
    // If connection is frozen due to route failure, do nothing
    if (connection_frozen_) {
        return;
    }
    
    // Update bandwidth estimation
    update_bandwidth_estimate();
    
    // Select appropriate mode based on network conditions
    TCPMode new_mode = select_mode();
    switch_mode(new_mode);
    
    // Apply mode-specific window increase
    if (current_mode_ == FCWND_MODE) {
        fcwnd_opencwnd();
    } else {
        pcwnd_opencwnd();
    }
}

void AdaptiveTcpAgent::slowdown(int how)
{
    // Check if this is a route failure or congestion loss
    if (is_route_failure_loss()) {
        handle_feedback(ROUTE_FAILURE);
        return;
    }
    
    // Normal congestion loss - apply mode-specific decrease
    if (current_mode_ == FCWND_MODE) {
        fcwnd_slowdown();
    } else {
        pcwnd_slowdown();
    }
    
    // Apply standard TCP behavior for different loss types
    if (how & CLOSE_CWND_HALF) {
        if (cwnd_ > ssthresh_) {
            cwnd_ = ssthresh_;
        }
    } else if (how & CLOSE_CWND_RESTART) {
        cwnd_ = (int)wnd_init_;
    } else if (how & CLOSE_CWND_ONE) {
        cwnd_ = 1;
    }
}

/* =================== Packet reception =================== */

void AdaptiveTcpAgent::recv(Packet *pkt, Handler *h)
{
    hdr_tcp *tcph = hdr_tcp::access(pkt);
    
    // Update RTT statistics when new ACK is received
    if (tcph->seqno() > last_ack_) {
        double current_time = Scheduler::instance().clock();
        double rtt = current_time - tcph->ts();
        
        if (rtt > 0) {
            update_rtt_stats(rtt, tcph->seqno());
            last_ack_time_ = current_time;
            pkt_delivered_++;
        }
    }
    
    // Track bytes sent for throughput calculation
    bytes_sent_since_update_ += size_;
    
    // Call parent class implementation
    TcpAgent::recv(pkt, h);
}

/* =================== Timeout handling =================== */

void AdaptiveTcpAgent::timeout(int tno)
{
    // If connection is frozen, don't process timeout normally
    if (connection_frozen_) {
        return;
    }
    
    // Normal timeout processing
    TcpAgent::timeout(tno);
}