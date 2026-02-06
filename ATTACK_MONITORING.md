# Attack Monitoring System

NN-eBPF includes a comprehensive attack monitoring system that logs all detected attacks with detailed metrics to help you analyze and respond to security threats.

## Overview

When the XDP program detects an attack, it sends detailed event information through an eBPF ring buffer to a userspace monitoring program. The monitor logs all attacks to a file with comprehensive metrics including:

- **Network Flow**: Source/destination IPs and ports
- **Detection Metrics**: Confidence scores, margins, probability
- **Flow Statistics**: Packet counts, sizes, durations
- **Performance**: Detection timing information

## Quick Start

### 1. Build the monitoring program

```bash
cd src
make
```

This builds `attack_monitor` along with other XDP programs.

### 2. Start the XDP program (Terminal 1)

```bash
sudo ./src/.output/xdp
```

This loads the XDP program and creates the ring buffer map.

### 3. Start the attack monitor (Terminal 2)

```bash
# Use default log file (/tmp/nn-ebpf-attacks.log)
sudo ./src/.output/attack_monitor

# Or specify custom log file
sudo ./src/.output/attack_monitor /var/log/my-attacks.log
```

The monitor will:
- Connect to the eBPF ring buffer
- Wait for attack events
- Log attacks to file
- Display summaries to console

### 4. View attack logs (Terminal 3)

```bash
# View last 10 attacks
./src/view_attacks.sh

# View last 50 attacks
./src/view_attacks.sh -n 50

# Follow log in real-time
./src/view_attacks.sh -f

# Show statistics
./src/view_attacks.sh -s
```

## Attack Log Format

Each detected attack is logged with the following structure:

```
========================================
ATTACK DETECTED #1
========================================
Timestamp:       2026-02-06 14:32:15
System Time:     1234567890123 ns

--- NETWORK FLOW ---
Source IP:       192.168.1.100
Source Port:     54321
Destination IP:  192.168.1.50
Destination Port: 80

--- DETECTION METRICS ---
Attack Score:    500000
Benign Score:    -200000
Confidence Margin: 700000
Threshold Used:  150000
Attack Probability: 95%
Confidence Level: VERY_HIGH (5/5)

--- FLOW STATISTICS ---
Total Packets:   1523
Max Packet Size: 1500 bytes
Min Packet Size: 60 bytes
Max Duration:    5000000 ns (5.000 ms)
Header Length:   30460 bytes

--- PERFORMANCE ---
Detection Time:  12345 ns (12.345 μs)
```

## Monitoring Commands

### View Recent Attacks

```bash
# Last 10 attacks (default)
./src/view_attacks.sh

# Last N attacks
./src/view_attacks.sh -n 20
```

### Follow Log in Real-Time

```bash
./src/view_attacks.sh -f
```

Press `Ctrl+C` to stop following.

### View Statistics

```bash
./src/view_attacks.sh -s
```

Shows:
- Total attacks detected
- Top 10 attacking IP addresses
- Top 10 targeted ports
- Confidence distribution
- Average attack probability
- Time range of attacks

Example output:
```
NN-eBPF Attack Statistics
=========================

Total Attacks Detected: 45

Top 10 Attacking IPs:
   15 attacks - 192.168.1.100
    8 attacks - 192.168.1.101
    5 attacks - 10.0.0.50
    ...

Top 10 Targeted Ports:
   20 attacks - Port 80
   12 attacks - Port 443
    8 attacks - Port 22
    ...

Confidence Distribution:
   25 attacks - VERY_HIGH
   12 attacks - HIGH
    8 attacks - MEDIUM
```

### Clear Logs

```bash
./src/view_attacks.sh -c
```

Prompts for confirmation before deleting all logs.

## Log File Locations

**Default log file:**
```
/tmp/nn-ebpf-attacks.log
```

**Custom log file:**
Specify when starting attack_monitor:
```bash
sudo ./src/.output/attack_monitor /var/log/my-custom-log.log
```

**Ring buffer map:**
```
/sys/fs/bpf/attack_events
```

## Integration with External Tools

### Export to JSON

Parse the log file to extract data:

```bash
grep -A 20 "^ATTACK DETECTED" /tmp/nn-ebpf-attacks.log
```

### Alert on High-Confidence Attacks

```bash
# Monitor for VERY_HIGH confidence attacks
tail -f /tmp/nn-ebpf-attacks.log | grep -A 5 "VERY_HIGH" | \
    while read line; do
        # Send alert (email, Slack, etc.)
        echo "ALERT: High-confidence attack detected!" | mail -s "Security Alert" admin@example.com
    done
```

### Block Attacking IPs with iptables

```bash
# Extract attacking IPs and block them
grep "^Source IP:" /tmp/nn-ebpf-attacks.log | \
    awk '{print $NF}' | sort -u | \
    while read ip; do
        sudo iptables -A INPUT -s $ip -j DROP
        echo "Blocked $ip"
    done
```

## Monitoring Best Practices

### 1. Run in Background

Use systemd service or screen/tmux:

```bash
# Using screen
screen -S attack-monitor sudo ./src/.output/attack_monitor
# Detach with Ctrl+A, D

# Reattach later
screen -r attack-monitor
```

### 2. Log Rotation

Prevent log files from growing too large:

```bash
# Rotate logs daily using logrotate
cat > /etc/logrotate.d/nn-ebpf << EOF
/tmp/nn-ebpf-attacks.log {
    daily
    rotate 7
    compress
    missingok
    notifempty
}
EOF
```

### 3. Monitor Performance

Check ring buffer status:

```bash
# View ring buffer info
sudo bpftool map show name attack_events

# Check if events are being dropped
# (monitor program should print warnings)
```

### 4. Correlate with Other Logs

Combine with system logs for comprehensive analysis:

```bash
# Find attacks during specific time window
grep "2026-02-06 14:" /tmp/nn-ebpf-attacks.log

# Cross-reference with auth logs
grep "Failed password" /var/log/auth.log
```

## Metrics Explanation

### Confidence Margin

The difference between attack and benign scores. Higher margin = higher confidence.

- **< 0**: Classified as benign
- **0-150k**: Low confidence, near threshold
- **150k-500k**: Medium to high confidence
- **> 500k**: Very high confidence

### Confidence Levels

Scaled based on detection threshold:

| Level | Name | Margin Range |
|-------|------|-------------|
| 5 | VERY_HIGH | > 3.33 × threshold |
| 4 | HIGH | > 1.33 × threshold |
| 3 | MEDIUM | > 0.67 × threshold |
| 2 | LOW | > 0.33 × threshold |
| 1 | VERY_LOW | ≤ 0.33 × threshold |

### Attack Probability

Approximate percentage probability that traffic is malicious (piecewise linear approximation of softmax).

- **< 60%**: Uncertain, borderline case
- **60-85%**: Likely attack
- **85-95%**: Very likely attack
- **> 95%**: Almost certain attack

## Troubleshooting

### Monitor Can't Connect to Ring Buffer

**Error**: `Failed to open ring buffer map`

**Solution**: Make sure XDP program is running first. The ring buffer is created by the XDP loader.

```bash
# Terminal 1: Start XDP first
sudo ./src/.output/xdp

# Terminal 2: Then start monitor
sudo ./src/.output/attack_monitor
```

### No Attacks Being Logged

**Check**:
1. Is traffic being processed? (Check XDP console output)
2. Is threshold too high? (Lower with `hot_threshold`)
3. Is ring buffer full? (Check for warnings in monitor output)

```bash
# Lower threshold for more sensitive detection
sudo ./src/.output/hot_threshold 100000
```

### Log File Permission Denied

**Solution**: Run monitor with sudo or change log file location:

```bash
# Use writable location
sudo ./src/.output/attack_monitor /tmp/attacks.log
```

## Advanced Usage

### Custom Event Processing

You can modify `attack_monitor.c` to:
- Send events to a database
- Trigger automated responses
- Export to external monitoring systems (Prometheus, Grafana)
- Send real-time alerts (email, Slack, PagerDuty)

Example: Send to syslog:

```c
#include <syslog.h>

// In handle_event():
syslog(LOG_WARNING, "Attack from %s:%u -> %s:%u (confidence: %u%%)",
       src_ip_str, event->src_port, dst_ip_str, event->dst_port,
       event->probability);
```

### Filter Events in eBPF

Modify `xdp_output_linear()` to only log certain attacks:

```c
// Only log high-confidence attacks
if (label == 1 && confidence_level >= 4) {
    // Submit to ring buffer
}
```

## Performance Considerations

**Ring Buffer Overhead**:
- Event size: ~100 bytes
- Buffer size: 256 KB (default)
- Can store ~2500 events before overflow

**CPU Impact**:
- Minimal: Event submission is lock-free
- Monitor consumes < 1% CPU in typical scenarios

**Memory Usage**:
- Ring buffer: 256 KB kernel memory
- Monitor: ~5 MB userspace memory

## Summary

The attack monitoring system provides:
- ✅ Real-time attack detection and logging
- ✅ Detailed metrics for analysis
- ✅ Low overhead (ring buffer, zero-copy)
- ✅ Flexible log viewing and statistics
- ✅ Integration-friendly format
- ✅ Production-ready reliability

Start monitoring your network today!
