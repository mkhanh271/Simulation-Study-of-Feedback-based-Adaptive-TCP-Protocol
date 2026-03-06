# ==============================================================================
# REALISTIC TEST SCENARIO - TCP Adaptive vs Reno
# FIXED: Remove window_ parameter to avoid initial CWND spike
# ==============================================================================

set ns [new Simulator]

# =================== TRACE FILES ===================
set tf [open optimal_adaptive.tr w]
$ns trace-all $tf

set cwnd_adaptive [open cwnd_adaptive_opt.dat w]
set cwnd_reno     [open cwnd_reno_opt.dat w]
set throughput_file [open throughput_opt.dat w]

puts "============================================"
puts " REALISTIC TCP Adaptive vs Reno (FIXED)"
puts "============================================"

# =================== NODES ===================
set S_adaptive [$ns node]
set S_reno     [$ns node]
set R1         [$ns node]
set R2         [$ns node]
set D_adaptive [$ns node]
set D_reno     [$ns node]

# Background
set BG1 [$ns node]
set BG2 [$ns node]

# =================== LINKS ===================

# Access links
$ns duplex-link $S_adaptive $R1 100Mb 2ms DropTail
$ns duplex-link $S_reno     $R1 100Mb 2ms DropTail
$ns duplex-link $R2 $D_adaptive 100Mb 2ms DropTail
$ns duplex-link $R2 $D_reno     100Mb 2ms DropTail

# Bottleneck
$ns duplex-link $R1 $R2 20Mb 40ms DropTail
$ns queue-limit $R1 $R2 100

# Background
$ns duplex-link $BG1 $R1 10Mb 5ms DropTail
$ns duplex-link $BG2 $R2 10Mb 5ms DropTail

puts "Link R1-R2: 20Mb / 40ms / queue=100"

# =================== LOSS MODEL ===================
set loss_model [new ErrorModel]
$loss_model set unit pkt
$loss_model set rate_ 0.001
$loss_model ranvar [new RandomVariable/Uniform]
$loss_model drop-target [new Agent/Null]

$ns link-lossmodel $loss_model $R1 $R2

# =================== BACKGROUND TRAFFIC ===================
proc create_cbr {ns src dst start stop rate} {
    set udp [new Agent/UDP]
    set null [new Agent/Null]

    $ns attach-agent $src $udp
    $ns attach-agent $dst $null
    $ns connect $udp $null

    set cbr [new Application/Traffic/CBR]
    $cbr attach-agent $udp
    $cbr set rate_ $rate
    $cbr set packet_size_ 500

    $ns at $start "$cbr start"
    $ns at $stop  "$cbr stop"
}

create_cbr $ns $BG1 $D_adaptive 1.0 120.0 2.5Mb
create_cbr $ns $BG2 $D_reno     1.0 120.0 2.5Mb

# =================== TCP ADAPTIVE ===================
set tcp_adaptive [new Agent/TCP/Adaptive]
$tcp_adaptive set fid_ 1
$tcp_adaptive set window_ 200
$tcp_adaptive set maxcwnd_ 400
$tcp_adaptive set packetSize_ 1000

# TCP Adaptive parameters
$tcp_adaptive set Wlow_ 15.0
$tcp_adaptive set Whigh_ 180.0
$tcp_adaptive set bhigh_ 0.85
$tcp_adaptive set loss_threshold_ 0.015
$tcp_adaptive set delay_threshold_ 0.25
$tcp_adaptive set slice_ratio_ 0.75
$tcp_adaptive set variation_low_ 0.05
$tcp_adaptive set variation_high_ 0.2
$tcp_adaptive set aggressive_increment_factor_ 2.5

$ns attach-agent $S_adaptive $tcp_adaptive

set sink_a [new Agent/TCPSink]
$ns attach-agent $D_adaptive $sink_a
$ns connect $tcp_adaptive $sink_a
$sink_a set lastBW_ 0

set ftp_a [new Application/FTP]
$ftp_a attach-agent $tcp_adaptive

# =================== TCP RENO ===================
set tcp_reno [new Agent/TCP/Reno]
$tcp_reno set fid_ 2
$tcp_reno set window_ 200
$tcp_reno set maxcwnd_ 400
$tcp_reno set packetSize_ 1000

$ns attach-agent $S_reno $tcp_reno

set sink_r [new Agent/TCPSink]
$ns attach-agent $D_reno $sink_r
$ns connect $tcp_reno $sink_r
$sink_r set lastBW_ 0

set ftp_r [new Application/FTP]
$ftp_r attach-agent $tcp_reno

# =================== TRACE FUNCTIONS ===================

proc trace_cwnd {tcp file label} {
    global ns
    set t [$ns now]
    set cwnd [$tcp set cwnd_]
    set ssthresh [$tcp set ssthresh_]
    puts $file "$t $cwnd $ssthresh"
    $ns at [expr $t + 0.1] "trace_cwnd $tcp $file $label"
}

proc trace_throughput {sink file label interval} {
    global ns
    set t [$ns now]

    set now [$sink set bytes_]
    set last [$sink set lastBW_]

    set bw [expr ($now - $last) * 8.0 / $interval / 1000000.0]

    puts $file "$t $bw $label"

    $sink set lastBW_ $now
    $ns at [expr $t + $interval] "trace_throughput $sink $file $label $interval"
}

# =================== PHASE CONTROL ===================
proc switch_to_moderate_loss {} {
    global loss_model ns
    puts "[format %.1f [$ns now]]s → MODERATE LOSS (5%)"
    $loss_model set rate_ 0.05
}

proc switch_to_high_loss {} {
    global loss_model ns
    puts "[format %.1f [$ns now]]s → HIGH LOSS (12%)"
    $loss_model set rate_ 0.12
}

proc switch_to_low_loss {} {
    global loss_model ns
    puts "[format %.1f [$ns now]]s → LOW LOSS (2%)"
    $loss_model set rate_ 0.02
}

proc switch_to_recovery {} {
    global loss_model ns
    puts "[format %.1f [$ns now]]s → RECOVERY (0.5%)"
    $loss_model set rate_ 0.005
}

# =================== SCHEDULE ===================
$ns at 0.5 "$ftp_a start"
$ns at 0.5 "$ftp_r start"

$ns at 2.0 "trace_cwnd $tcp_adaptive $cwnd_adaptive Adaptive"
$ns at 2.0 "trace_cwnd $tcp_reno $cwnd_reno Reno"

$ns at 2.0 "trace_throughput $sink_a $throughput_file Adaptive 1.0"
$ns at 2.0 "trace_throughput $sink_r $throughput_file Reno 1.0"

$ns at 30.0 "switch_to_moderate_loss"
$ns at 55.0 "switch_to_high_loss"
$ns at 75.0 "switch_to_low_loss"
$ns at 95.0 "switch_to_recovery"

$ns at 115.0 "$ftp_a stop"
$ns at 115.0 "$ftp_r stop"

# =================== FINISH ===================
proc finish {} {
    global ns tf cwnd_adaptive cwnd_reno throughput_file

    $ns flush-trace
    close $tf
    close $cwnd_adaptive
    close $cwnd_reno
    close $throughput_file

    puts ""
    puts "=========================================="
    puts " Simulation Completed Successfully"
    puts "=========================================="
    exit 0
}

$ns at 116.0 "finish"

# =================== VISUAL ===================
$ns duplex-link-op $R1 $R2 orient right
$ns duplex-link-op $R1 $R2 queuePos 0.5
$ns color 1 Blue
$ns color 2 Red

puts "Starting simulation..."
$ns run