# NN-eBPF Improvement Guide

This document lists practical improvements for the intrusion detection system, organized by difficulty and impact. All improvements are designed to be incremental and won't break the existing codebase.

---

## 🧠 Machine Learning Improvements

### 1. Additional Traffic Features (MEDIUM DIFFICULTY, HIGH IMPACT)

**What it does:**
Adds more sophisticated features that capture traffic patterns better, improving detection accuracy.

**New features to add:**
- **Packet size variance**: Detect attacks with irregular packet sizes
- **Bidirectional flow stats**: Track both directions of communication (requests + responses)
- **TCP flags distribution**: Count SYN, ACK, PSH, FIN flags separately (detect SYN floods, port scans)
- **Average packet size**: Better than just min/max
- **Inter-arrival time variance**: Detect irregular timing patterns
- **Bytes per second**: Flow rate metric

**Where to change:**
```
src/common.h (struct flow_attribute):
  - Add: int64_t packet_size_variance
  - Add: int64_t avg_packet_size
  - Add: int64_t syn_count, ack_count, psh_count
  - Add: int64_t bytes_per_second
  - Add: int64_t bidirectional_packets (track reverse flow)

src/handler.bpf.h (update_flow_attribute):
  - Calculate running variance of packet sizes
  - Track TCP flag counts
  - Calculate average packet size
  - Compute flow rate metrics

src/xdp.bpf.c (xdp_preprocessing):
  - Add new features to x[] array (increase from 6 to 10-12 features)

src/mlp_train.py:
  - Add new features to dataset
  - Retrain model with expanded feature set

src/mlp_params.bpf.h (struct Net):
  - Update layer_0_weight size (e.g., 12→32 instead of 6→32)
  - Update mean[] and scale[] arrays to match new feature count
```

**Benefit:** Better detection of sophisticated attacks, fewer false positives

---

### 2. Bidirectional Flow Tracking (MEDIUM DIFFICULTY, HIGH IMPACT)

**What it does:**
Currently tracks outgoing traffic only. This adds tracking of responses, giving a complete picture of communication.

**How it works:**
- Track both source→dest AND dest→source flows
- Calculate features like: response time, request/response ratio, asymmetry
- Detect: Port scans (many requests, no responses), C2 beaconing (periodic bidirectional)

**Where to change:**
```
src/xdp.bpf.c (xdp_prog):
  - When packet arrives, look up BOTH forward and reverse flow keys
  - Forward: (src_ip, src_port, dst_ip, dst_port)
  - Reverse: (dst_ip, dst_port, src_ip, src_port)
  - Link them together with a flow_id field

src/common.h (struct flow_attribute):
  - Add: int64_t reverse_flow_packets
  - Add: int64_t request_response_ratio
  - Add: u64 first_response_time

src/handler.bpf.h:
  - Modify update_flow_attribute to track direction
  - Calculate bidirectional metrics
```

**Benefit:** Detect attacks that look normal in one direction but suspicious overall

---

### 3. Multi-Class Classification (MEDIUM DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Instead of binary (benign/attack), classify specific attack types: DDoS, port scan, slowloris, brute force, etc.

**Where to change:**
```
src/mlp_params.bpf.h (struct Net):
  - Change layer_2_weight from [64] (32→2) to [160] (32→5) for 5 classes
  - Update output layer size

src/xdp.bpf.c (xdp_output_linear):
  - Change linear_layer call: output 5 classes instead of 2
  - Find class with max score: argmax(hidden1[0..4])
  - Map to attack types: 0=benign, 1=DDoS, 2=port_scan, 3=slowloris, 4=brute_force

src/mlp_train.py:
  - Label training data with attack types
  - Change output layer: nn.Linear(32, 5)
  - Use CrossEntropyLoss for multi-class

Dataset:
  - Label each attack script with its type
  - Collect more diverse attack samples
```

**Benefit:** More actionable intelligence - know WHAT kind of attack you're facing

---

### 4. Improved Quantization (LOW DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Better fixed-point conversion to reduce quantization error and improve accuracy.

**Techniques:**
- **Per-layer scaling**: Different Q-format for each layer (e.g., Q8.24 for first layer if values are small)
- **Dynamic range analysis**: Find min/max values per layer during training, optimize bit allocation
- **Quantization-aware training**: Train model knowing it will be quantized

**Where to change:**
```
src/mlp_quant.py:
  - Add per-layer scale factors
  - Analyze activation ranges: print(layer_output.min(), layer_output.max())
  - Adjust fixed-point format per layer if needed
  - Implement quantization-aware training (QAT)

src/mlp.bpf.h (linear_layer):
  - Support per-layer right-shift amounts
  - Add scale[] parameter to linear_layer function

src/mlp_params.bpf.h (struct Net):
  - Add: int32_t layer_0_shift, layer_1_shift, layer_2_shift
```

**Benefit:** Higher accuracy with same model architecture

---

### 5. Model Ensemble (HIGH DIFFICULTY, HIGH IMPACT)

**What it does:**
Run multiple models in parallel and vote on classification (like a committee of experts).

**Where to change:**
```
src/mlp_params.bpf.h (struct Net):
  - Already supports multiple models in nn_parameters map
  - Add 2-3 models trained with different random seeds or architectures

src/xdp.bpf.c (xdp_output_linear):
  - Run inference with model 0, store result
  - Run inference with model 1, store result
  - Majority vote or weighted average
  - Requires duplicating flow_attribute or using temporary storage

Alternative (simpler):
  - Train 3 models
  - Hot-swap between them based on network conditions
  - Use the model that performs best for your traffic
```

**Benefit:** More robust, fewer false positives, better generalization

---

### 6. Adaptive Threshold Based on Context (LOW DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Instead of fixed threshold (150,000), adjust based on flow characteristics.

**Where to change:**
```
src/xdp.bpf.c (xdp_output_linear):
  - If num_packets < 10: threshold = 200000 (stricter - short flows less reliable)
  - If num_packets > 100: threshold = 100000 (more lenient - long flows more data)
  - If dst_port in [80, 443]: threshold = 180000 (web traffic, expect more variance)
  - If dst_port in [22, 3306]: threshold = 120000 (SSH/DB, less tolerance)
```

**Benefit:** Context-aware detection, fewer false positives

---

## 🔧 eBPF Code Improvements

### 7. Action Control: DROP vs PASS (LOW DIFFICULTY, HIGH IMPACT)

**What it does:**
Currently all packets are passed (XDP_PASS). Add ability to actually block attacks.

**Where to change:**
```
src/params.bpf.h:
  - Add new map: action_mode (0=monitor, 1=block, 2=rate_limit)

src/xdp.c (userspace loader):
  - Create action_mode map
  - Initialize to 0 (monitor mode for safety)

src/xdp.bpf.c (xdp_output_linear):
  - Read action_mode from map
  - If mode == 1 (block) and label == 1 (attack):
      return XDP_DROP;
  - If mode == 0 (monitor):
      return XDP_PASS;

Create new utility:
  src/set_action_mode.c:
    - Update action_mode map
    - sudo ./set_action_mode monitor|block|rate_limit
```

**Benefit:** Actually prevent attacks, not just detect them

---

### 8. Attack Statistics & Counters (LOW DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Track how many attacks detected, types, rates, etc. for monitoring.

**Where to change:**
```
src/params.bpf.h:
  - Add map: struct stats_map {
      u64 total_flows;
      u64 attacks_detected;
      u64 benign_flows;
      u64 false_positives; // (user-marked)
      u64 packets_dropped;
    }

src/xdp.bpf.c (xdp_output_linear):
  - Increment stats based on classification
  - Update counters atomically: __sync_fetch_and_add()

Create new utility:
  src/show_stats.c:
    - Read stats_map
    - Display: "Attacks: 45, Benign: 1230, Drop rate: 3.5%"
    - Reset counters option
```

**Benefit:** Real-time monitoring, performance metrics, debugging aid

---

### 9. Connection Rate Limiting (MEDIUM DIFFICULTY, HIGH IMPACT)

**What it does:**
Limit how many connections per second from a single IP, preventing DDoS and brute force.

**Where to change:**
```
src/params.bpf.h:
  - Add map: rate_limit_map (key: IP, value: struct {u64 last_seen, u32 count})

src/xdp.bpf.c (xdp_prog):
  - Before processing flow, check rate_limit_map
  - If same IP seen > 100 times in last second:
      bpf_printk("Rate limit exceeded");
      return XDP_DROP;
  - Update counter

Cleanup:
  - Use BPF_MAP_TYPE_LRU_HASH for automatic old entry removal
```

**Benefit:** Stop floods before they even reach the NN, reduce CPU usage

---

### 10. Per-Protocol Handling (MEDIUM DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Different handling for HTTP, SSH, FTP, etc. Specialized detection per protocol.

**Where to change:**
```
src/xdp.bpf.c (xdp_prog):
  - After extracting flow tuple, identify protocol by port
  - If dst_port == 80 || dst_port == 443:
      update_http_flow_attribute(); // Track HTTP-specific features
  - If dst_port == 22:
      update_ssh_flow_attribute(); // Track SSH-specific features

src/handler.bpf.h:
  - Add protocol-specific handlers
  - Parse application layer headers (limited in eBPF)
  - Extract: HTTP method, user-agent, SSH version, etc.

src/common.h:
  - Add protocol-specific fields to flow_attribute
  - Union for different protocols to save space
```

**Benefit:** Better detection for protocol-specific attacks

---

### 11. Flow Timeout & Cleanup (LOW DIFFICULTY, LOW IMPACT)

**What it does:**
Remove stale flows from flow_map to prevent memory exhaustion.

**Where to change:**
```
Option 1: Use LRU map (EASIEST)
src/params.bpf.h (flow_map):
  - Change type: BPF_MAP_TYPE_LRU_HASH
  - Automatically evicts oldest entries when full

Option 2: Manual cleanup
src/xdp.bpf.c (xdp_prog):
  - Check attr_ptr->last_seen_time
  - If > 60 seconds old and no activity:
      bpf_map_delete_elem(&flow_map, &f);

Option 3: Userspace cleanup
src/xdp.c:
  - Add cleanup thread
  - Periodically iterate flow_map
  - Delete entries older than threshold
```

**Benefit:** Prevent map exhaustion, stable long-term operation

---

### 12. Export Metrics via Perf Events (MEDIUM DIFFICULTY, HIGH IMPACT)

**What it does:**
Send detection results to userspace for logging, alerting, dashboards (Prometheus, Grafana).

**Where to change:**
```
src/params.bpf.h:
  - Add: BPF_MAP_TYPE_PERF_EVENT_ARRAY

src/xdp.bpf.c (xdp_output_linear):
  - struct detection_event {
      u32 src_ip, dst_ip;
      u16 src_port, dst_port;
      u8 label; // 0=benign, 1=attack
      int32_t confidence;
      u64 timestamp;
    };
  - bpf_perf_event_output(ctx, &events, BPF_F_CURRENT_CPU, &event, sizeof(event));

src/xdp.c (userspace):
  - Set up perf buffer: perf_buffer__new()
  - Callback function to handle events
  - Write to log file: /var/log/nn-ebpf/detections.log
  - Send to syslog or external system
```

**Benefit:** Integration with monitoring systems, persistent logs, alerts

---

### 13. IPv6 Support (MEDIUM DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Currently only processes IPv4. Add support for IPv6 traffic.

**Where to change:**
```
src/xdp.bpf.c (flow_tuple):
  - Check eth->h_proto for ETH_P_IPV6 (0x86DD)
  - Parse struct ipv6hdr instead of iphdr
  - Extract 128-bit addresses

src/common.h (struct flow):
  - Change: __u32 saddr → __u8 saddr[16]
  - Change: __u32 daddr → __u8 daddr[16]
  - Add: __u8 ip_version; // 4 or 6

src/xdp.bpf.c (is_private_ip):
  - Add IPv6 private ranges:
    - fc00::/7 (unique local)
    - fe80::/10 (link-local)
```

**Benefit:** Future-proof, support modern networks

---

### 14. Whitelist/Blacklist Support (LOW DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Always allow/block certain IPs regardless of NN decision.

**Where to change:**
```
src/params.bpf.h:
  - Add map: whitelist_map (key: IP, value: 1)
  - Add map: blacklist_map (key: IP, value: 1)

src/xdp.bpf.c (xdp_prog):
  - Before flow processing:
    if (bpf_map_lookup_elem(&whitelist_map, &src_ip)):
        return XDP_PASS; // Always allow
    if (bpf_map_lookup_elem(&blacklist_map, &src_ip)):
        return XDP_DROP; // Always block

Create utilities:
  src/whitelist.c: Add/remove whitelist entries
  src/blacklist.c: Add/remove blacklist entries
```

**Benefit:** Manual control, trust known IPs, block known attackers

---

### 15. Optimized Matrix Multiplication (HIGH DIFFICULTY, MEDIUM IMPACT)

**What it does:**
Speed up linear_layer() using eBPF-specific optimizations.

**Where to change:**
```
src/mlp.bpf.h (linear_layer):
  - Use BPF loop unrolling more aggressively
  - SIMD-like operations where possible
  - Reduce intermediate variables
  - Cache optimization (minimize memory accesses)

Technique 1: Blocked matrix multiplication
  - Process 4x4 blocks at a time
  - Better cache locality

Technique 2: Sparse weights
  - Prune near-zero weights in training
  - Skip multiplication if weight < threshold
  - Reduces computation by 30-50%

Technique 3: Low-rank approximation
  - Decompose weight matrix: W = U·V
  - Two smaller multiplications instead of one large
```

**Benefit:** Lower latency, handle more traffic

---

## 📊 Priority Recommendations

### Quick Wins (Start Here)
1. **Action Control (DROP vs PASS)** - #7
   - Effort: 1 day
   - Impact: Makes system actually useful for defense

2. **Attack Statistics** - #8
   - Effort: 1 day
   - Impact: Essential for monitoring

3. **Adaptive Threshold** - #6
   - Effort: 2 hours
   - Impact: Fewer false positives immediately

### High Impact (Do Next)
4. **Additional Traffic Features** - #1
   - Effort: 1 week
   - Impact: Significantly better detection

5. **Bidirectional Flow Tracking** - #2
   - Effort: 3 days
   - Impact: Detect more attack types

6. **Connection Rate Limiting** - #9
   - Effort: 2 days
   - Impact: Stop floods early

### Advanced (Later)
7. **Multi-Class Classification** - #3
8. **Export Metrics via Perf Events** - #12
9. **Model Ensemble** - #5

---

## 🧪 Testing Each Improvement

After implementing any improvement:

```bash
# 1. Rebuild
cd src && make clean && make

# 2. Test with benign traffic
curl http://your-laptop-ip:8000

# 3. Test with attack (from another device)
nmap -sS your-laptop-ip
hping3 -S -p 80 --flood your-laptop-ip

# 4. Check logs
sudo cat /sys/kernel/debug/tracing/trace_pipe | grep -A 10 "DETECTION RESULT"

# 5. Verify metrics
sudo ./src/.output/show_stats  # (if you implemented #8)
```

---

## 📝 Notes

- **Always test in monitor mode first** before enabling DROP
- **Keep old threshold values** for comparison
- **Collect dataset** before and after each improvement
- **Document false positives/negatives** to measure improvement
- **Version control** - commit after each working improvement

---

**Remember:** Small, incremental changes are better than big rewrites. Test each improvement thoroughly before moving to the next!
