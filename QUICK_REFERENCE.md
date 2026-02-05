# NN-eBPF QUICK REFERENCE

## Common Commands

### First-Time Setup
```bash
# 1. Build dependencies (one-time)
make

# 2. Train neural network
cd src
python3 mlp_train.py

# 3. Convert to fixed-point
python3 mlp_quant.py mlp.th 16

# 4. Compile eBPF programs
make

# 5. Run XDP program
sudo ./src/.output/xdp wlan0
```

### View Debug Output
```bash
# In separate terminal
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

### Hot Update Model (While Running)
```bash
# Terminal 1: Keep XDP running
sudo ./src/.output/xdp

# Terminal 2: Update model
cd src
python3 mlp_train.py          # Train new model
python3 mlp_quant.py mlp.th 16 # Quantize
make                           # Recompile
sudo ./.output/hot_updating    # Swap models (instant!)
```

### Cleanup
```bash
# Stop XDP (Ctrl+C in terminal running xdp)

# Manual cleanup (if needed)
sudo ip link set dev wlan0 xdp off
sudo rm /sys/fs/bpf/nn_parameters
sudo rm /sys/fs/bpf/nn_idx
```

---

## File Structure

```
NN-eBPF/
├── src/
│   ├── xdp.bpf.c            ← eBPF kernel programs (6 programs)
│   ├── xdp.c                ← Userspace loader
│   ├── hot_updating.c       ← Model swap utility
│   ├── common.h             ← Data structures (flow, flow_attribute)
│   ├── params.bpf.h         ← NN parameter maps
│   ├── handler.bpf.h        ← Flow tracking functions
│   ├── mlp.bpf.h            ← NN math functions (linear, relu, normalize)
│   ├── mlp.py               ← NN architecture definition
│   ├── mlp_train.py         ← Training script
│   ├── mlp_quant.py         ← Quantization script
│   └── mlp_params.bpf.h     ← Generated NN weights (after quantization)
├── dataset/
│   └── reproduction-xdp-*.npy ← Training data
├── WORKFLOW.md              ← Complete explanation (this file!)
├── ARCHITECTURE.md          ← Visual diagrams
└── QUICK_REFERENCE.md       ← Common commands
```

---

## Understanding Your Logs

### Example Log Breakdown

```
Score [BENIGN]: -27452
Score [ATTACK]: 19569
Confidence margin: 47021 (threshold: 100000)
Classification: BENIGN
```

**What this means:**
- **Raw scores (logits):** Not probabilities! Can be any value.
- **BENIGN = -27452:** In fixed-point (÷65536 = -0.42 in float)
- **ATTACK = 19569:** In fixed-point (÷65536 = +0.30 in float)
- **Margin = 47021:** Difference between scores
- **Threshold = 100000:** Minimum margin to classify as ATTACK
- **Decision:** BENIGN (margin too small)

**To convert to probabilities (optional):**
```python
import math
benign_float = -27452 / 65536  # -0.42
attack_float = 19569 / 65536   # +0.30

benign_prob = math.exp(benign_float) / (math.exp(benign_float) + math.exp(attack_float))
attack_prob = math.exp(attack_float) / (math.exp(benign_float) + math.exp(attack_float))

print(f"BENIGN: {benign_prob:.1%}")  # ~32.7%
print(f"ATTACK: {attack_prob:.1%}")  # ~67.3%
```

---

## Adjusting Confidence Threshold

**Location:** `src/xdp.bpf.c:583`

```c
// Current setting
int32_t confidence_threshold = 100000;

// More aggressive (detect more attacks, more false positives)
int32_t confidence_threshold = 50000;

// More conservative (fewer false positives, might miss attacks)
int32_t confidence_threshold = 200000;
```

**After changing, recompile:**
```bash
cd src
make
sudo ./xdp
```

---

## Features Explained

| Feature | Description | Normal Value | Attack Pattern |
|---------|-------------|--------------|----------------|
| `max_packet_length` | Largest packet in flow | 500-1500 bytes | Varies |
| `max_duration` | Longest gap between packets | 1-100 ms | Slowloris: >1s |
| `min_packet_length` | Smallest packet in flow | 40-100 bytes | Fragmentation: <40 |
| `dst_port` | Destination port | 80, 443, 22, etc | Scans: unusual |
| `header_length` | Total TCP header bytes | 100-500 bytes | Varies |
| `num_packet` | Total packets in flow | 10-100 | DDoS: 1000+ |

---

## Troubleshooting

### Problem: XDP fails to attach
```
Error: Failed to attach XDP program
```
**Solution:**
- Check interface name: `ip link show`
- Try generic mode (automatic fallback)
- Check permissions: Must run as root
- Verify kernel version: Needs 4.8+ with XDP support

### Problem: Tail call failed
```
ERROR: Tail call to preprocessing failed!
```
**Solution:**
- Check that `progs` map is populated before packets arrive
- Verify all 6 programs are loaded: `bpftool prog show`
- Check BPF logs: `sudo dmesg | grep bpf`

### Problem: Wrong classifications
```
Normal traffic flagged as attack
```
**Solution:**
1. Check normalization parameters match training:
   - Look at `mean` and `scale` in logs
   - Should match values in `mlp_params.bpf.h`
2. Adjust confidence threshold (see above)
3. Retrain model with more diverse benign traffic
4. Collect your own traffic for training

### Problem: Build errors
```
error: 'for' loop initial declarations are only allowed in C99 mode
```
**Solution:**
```bash
cd src
make clean
make
```

### Problem: No output in trace_pipe
```
$ sudo cat /sys/kernel/debug/tracing/trace_pipe
(nothing appears)
```
**Solution:**
- Check IP filter in `xdp.bpf.c:128`
  - Should be `if (1)` to monitor all traffic
  - Or change to match your IP
- Verify XDP is attached: `sudo ip link show wlan0`
  - Should show `xdp` in output
- Generate traffic: `curl http://example.com`

---

## Modifying the Network Architecture

### Make Network Bigger (More Accurate)

**1. Edit `src/mlp.py`:**
```python
class Net(nn.Module):
    def __init__(self, in_dim, out_dim):
        super().__init__()
        self.model = nn.Sequential(
            nn.Linear(in_dim, 64, bias=False),   # 32→64
            nn.ReLU(),
            nn.Linear(64, 64, bias=False),       # 32→64
            nn.ReLU(),
            nn.Linear(64, out_dim, bias=False),  # 32→64
        )
```

**2. Edit `src/params.bpf.h`:**
```c
struct Net {
    int32_t layer_0_weight[384];   // 6×64
    int32_t layer_1_weight[4096];  // 64×64
    int32_t layer_2_weight[128];   // 64×2
    int64_t mean[6];
    int64_t scale[6];
};
```

**3. Edit `src/common.h`:**
```c
struct flow_attribute {
    // ... other fields ...
    s32 hidden1[64];  // 32→64
    s32 hidden2[64];  // 32→64
};
```

**4. Update all references to 32 → 64 in:**
- `src/xdp.bpf.c` (all 6 programs)
- Function calls to `linear_layer()` and `relu()`

**5. Retrain and recompile:**
```bash
cd src
python3 mlp_train.py
python3 mlp_quant.py mlp.th 16
make
```

---

## Performance Tuning

### Measure Latency
Your logs already show timing:
```
Avg feature extraction: 1784 ns    ← Per-packet overhead
Total detection time: 61086 ns     ← Full NN inference
```

**Typical values:**
- Feature extraction: 1-5 μs per packet
- NN inference: 50-200 μs total
- Total overhead: <0.1% for 1 Gbps traffic

### Reduce Latency
1. **Smaller network:** 6→16→16→2 instead of 6→32→32→2
2. **Disable debug prints:** Comment out `bpf_printk()` calls
3. **Native XDP mode:** Better than generic (automatic if supported)

### Increase Accuracy
1. **Bigger network:** 6→64→64→2 or add more layers
2. **More training data:** Collect diverse benign and attack samples
3. **More epochs:** Increase from 32 to 64 or 128
4. **Better features:** Add more packet statistics

---

## Disabling IP Filter (Monitor All Traffic)

**Location:** `src/xdp.bpf.c:128`

**Current (filtered):**
```c
if (src_ip == 555819337)  // Only 33.33.33.73
{
    // Process packet
}
```

**Change to (monitor all):**
```c
if (1)  // Process ALL TCP packets
{
    // Process packet
}
```

**Recompile:**
```bash
cd src
make
sudo ./xdp
```

---

## Dataset Format

If you want to create your own dataset:

**Structure:**
```python
# Data: numpy array of shape (N, 6)
# Each row = one flow with 6 features
data = np.array([
    [134, 9022688, 32, 2080, 168, 5],  # Flow 1 (benign)
    [1500, 50000, 60, 80, 400, 100],   # Flow 2 (benign)
    [64, 5000000000, 64, 443, 2000, 5000],  # Flow 3 (slowloris attack)
    # ... more flows ...
])

# Labels: 0 = benign, 1 = attack
labels = np.array([0, 0, 1, ...])

# Save
np.save('my_data.npy', data)
np.save('my_labels.npy', labels)
```

**Update paths in `mlp_train.py`:**
```python
data_path = 'my_data.npy'
label_path = 'my_labels.npy'
```

---

## Production Deployment

### Block Attacks (Currently Just Logs)

**Location:** `src/xdp.bpf.c:624`

**Current (passive):**
```c
return XDP_PASS;  // Allow all packets
```

**Change to (active blocking):**
```c
if (label == 1) {
    bpf_printk("!!! BLOCKING ATTACK !!!");
    return XDP_DROP;  // Drop attack packets
}
return XDP_PASS;  // Allow benign packets
```

**⚠️ Warning:** Test thoroughly before deploying! False positives will break legitimate traffic.

### Monitor Multiple Interfaces

**Option 1: Run multiple instances**
```bash
# Terminal 1
sudo ./xdp wlan0

# Terminal 2
sudo ./xdp eth0
```

**Option 2: Use XDP multi-attach (requires kernel 5.9+)**
(More complex, requires code modifications)

---

## Advanced: Custom Attack Detection

### Add New Feature

**Example: Add average packet size**

**1. Update `struct flow_attribute` in `src/common.h`:**
```c
struct flow_attribute {
    // ... existing fields ...
    s64 avg_packet_length;  // New feature
};
```

**2. Update `update_flow_attribute()` in `src/handler.bpf.h`:**
```c
attr->avg_packet_length = (attr->max_packet_length + attr->min_packet_length) / 2;
```

**3. Update preprocessing in `src/xdp.bpf.c`:**
```c
int64_t x[7] = {  // 6→7 features
    attr->max_packet_length,
    attr->max_duration,
    attr->min_packet_length,
    attr->dst_port,
    attr->header_length,
    attr->num_packet,
    attr->avg_packet_length  // New feature
};
```

**4. Update Python files:**
- Change `in_dim` from 6 to 7
- Update dataset to include new feature
- Retrain model

**5. Update all arrays:**
- `mean[7]`, `scale[7]` instead of `[6]`
- Update `struct Net` in `params.bpf.h`

---

## FAQ

### Q: Can I run this on WiFi?
**A:** Yes! The code automatically falls back to generic/SKB mode which works on WiFi interfaces.

### Q: Does this work on IPv6?
**A:** No, currently only IPv4. To add IPv6, modify `flow_tuple()` function.

### Q: Can I use a GPU for training?
**A:** Yes! PyTorch will automatically use CUDA if available:
```python
device = 'cuda' if torch.cuda.is_available() else 'cpu'
```

### Q: How much memory does this use?
**A:**
- eBPF maps: ~3-4 MB (8192 flows × 400 bytes)
- NN parameters: ~7 KB (1280 weights × 4 bytes + metadata)
- Total kernel memory: <5 MB

### Q: Can this detect zero-day attacks?
**A:** Only if they have similar traffic patterns to known attacks in training data. Neural networks learn patterns, not specific exploits.

### Q: What attacks does it detect?
**A:** Based on training data:
- Slowloris (slow HTTP)
- DDoS (SYN flood, UDP flood)
- Port scans
- SQL injection (by traffic pattern)
- XSS attacks (by traffic pattern)

### Q: False positive rate?
**A:** Depends on:
- Training data quality
- Confidence threshold
- Network diversity
- Typical range: 1-10% with proper tuning

---

## Resources

- **eBPF Documentation:** https://ebpf.io
- **XDP Tutorial:** https://github.com/xdp-project/xdp-tutorial
- **libbpf:** https://github.com/libbpf/libbpf
- **PyTorch:** https://pytorch.org/docs/stable/index.html

---

## Getting Help

If you encounter issues:

1. **Check logs:**
   ```bash
   sudo cat /sys/kernel/debug/tracing/trace_pipe
   sudo dmesg | tail -50
   ```

2. **Verify setup:**
   ```bash
   # Check kernel version
   uname -r

   # Check BTF support
   ls /sys/kernel/btf/vmlinux

   # Check XDP attachment
   sudo ip link show
   ```

3. **Debug mode:**
   Uncomment all `bpf_printk()` statements in `xdp.bpf.c` for detailed output

4. **Test with known traffic:**
   ```bash
   # Generate benign traffic
   curl http://example.com

   # Should see classification in trace_pipe
   ```

---

## Summary

**To get started:**
```bash
make && cd src && python3 mlp_train.py && python3 mlp_quant.py mlp.th 16 && make && sudo ./xdp
```

**To update model:**
```bash
cd src && python3 mlp_train.py && python3 mlp_quant.py mlp.th 16 && make && sudo ./hot_updating
```

**To monitor:**
```bash
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

That's it! You're now running a neural network directly in your Linux kernel for real-time intrusion detection. 🚀
