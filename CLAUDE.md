# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NN-eBPF is a network intrusion detection system that runs a neural network directly in eBPF/XDP for real-time packet analysis. It uses a Multi-Layer Perceptron (MLP) to classify TCP flows as benign or malicious based on traffic features.

**Key Architecture:**
- **XDP (eXpress Data Path)**: Processes packets at the kernel level before the network stack
- **Tail Calls**: Chains multiple eBPF programs together to avoid instruction limits
- **Neural Network in Kernel**: 3-layer MLP (6→32→32→2) runs entirely in eBPF
- **Fixed-Point Arithmetic**: All NN computations use 16-bit fixed-point (Q16.16 format) since eBPF doesn't support floating point

## Build Commands

### Initial Setup (First Time Only)
```bash
# Build bpftool and libbpf dependencies
make

# Generate neural network parameters (requires Python environment)
cd src
python3 mlp_train.py  # Train model on dataset
python3 mlp_quant.py  # Quantize to fixed-point format
```

### Building the XDP Programs
```bash
cd src
make                    # Build all XDP programs
make clean             # Clean build artifacts
```

**Build Process:**
1. Generates `vmlinux.h` from kernel BTF (contains all kernel type definitions)
2. Compiles `xdp.bpf.c` to eBPF bytecode (`xdp.bpf.o`)
3. Generates skeleton header (`xdp.skel.h`) with bpftool
4. Compiles userspace loader (`xdp.c`) and links with libbpf

### Running the Intrusion Detection System
```bash
# Run on default interface (wlan0)
sudo ./src/.output/xdp

# Or specify interface
sudo ./src/.output/xdp eth0

# View BPF debug output in separate terminal
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

**Note:** The program automatically tries native XDP mode first, falls back to generic/SKB mode if needed (required for WiFi interfaces).

### Monitoring Detected Attacks
```bash
# Start the attack monitoring program (separate terminal)
sudo ./src/.output/attack_monitor

# View attack logs and statistics
./src/view_attacks.sh           # View last 10 attacks
./src/view_attacks.sh -n 50     # View last 50 attacks
./src/view_attacks.sh -f        # Follow log in real-time
./src/view_attacks.sh -s        # Show statistics
```

**Attack logs are saved to**: `/tmp/nn-ebpf-attacks.log`

Each detected attack includes:
- Source/destination IPs and ports
- Confidence scores and probability
- Flow statistics (packet counts, sizes, durations)
- Performance metrics

See **ATTACK_MONITORING.md** for comprehensive documentation.

### Updating Neural Network Model
```bash
# While xdp is running, hot-update the NN parameters
sudo ./src/.output/hot_updating
```

This swaps between two NN models stored in the `nn_parameters` map without restarting the XDP program.

### Updating Detection Threshold (Zero Downtime)
```bash
# While xdp is running, hot-update the detection threshold

# Manual mode - specify exact threshold value
sudo ./src/.output/hot_threshold 150000  # Default - medium sensitivity
sudo ./src/.output/hot_threshold 100000  # Higher sensitivity (more detections)
sudo ./src/.output/hot_threshold 200000  # Lower sensitivity (fewer false positives)

# Adaptive mode - automatically calculate optimal threshold
sudo ./src/.output/hot_threshold --adaptive  # Analyze traffic and set optimal threshold
```

**Threshold Guide (Q16.16 fixed-point format):**
- **50,000** - VERY HIGH sensitivity (may have false positives)
- **100,000** - HIGH sensitivity (catches more attacks, some false positives)
- **150,000** - MEDIUM sensitivity (balanced, recommended default)
- **200,000** - LOW sensitivity (fewer false positives, may miss subtle attacks)
- **500,000** - VERY LOW sensitivity (only extremely obvious attacks)

The threshold determines the minimum confidence margin (`ATTACK_score - BENIGN_score`) needed to classify traffic as an attack. Updates take effect immediately without restarting XDP. All threshold changes are logged to `/tmp/threshold_updates.log` for audit tracking.

**Adaptive Mode:** Automatically calculates an optimal threshold by analyzing current flow statistics in the `flow_map`. It computes the mean and standard deviation of confidence margins from active flows, then sets the threshold to `mean + 1.0 × std_dev`. This adapts the threshold to your specific network environment and traffic patterns. Requires at least 10 flows with classification data to work; falls back to default (150,000) if insufficient data. Useful for initial calibration or periodic retuning based on real traffic.

## Code Architecture

### eBPF Program Flow (src/xdp.bpf.c)

The system uses **tail calls** to chain 6 eBPF programs together:

```
xdp_prog (entry point)
  ↓ (collects flow stats)
  ↓ (on TCP FIN/RST)
  ├─> xdp_preprocessing (normalizes features)
       ├─> xdp_input_linear (layer 0: 6→32)
            ├─> xdp_input_relu (activation)
                 ├─> xdp_hidden_linear (layer 1: 32→32)
                      ├─> xdp_hidden_relu (activation)
                           └─> xdp_output_linear (layer 2: 32→2, makes decision)
```

**Why Tail Calls?** eBPF programs have a ~1M instruction limit. The full NN would exceed this, so we split it into stages and chain them with `bpf_tail_call()`.

### Key Data Structures

**`struct flow`** (common.h): 5-tuple identifier for TCP connections
- Source/dest IP and port
- Used as key in flow_map

**`struct flow_attribute`** (common.h): Per-flow statistics and NN state
- Traffic features: packet count, min/max length, duration, port, header length
- NN state: `hidden1[32]`, `hidden2[32]` for layer outputs
- Performance metrics: timing information
- Stored as value in flow_map hash table (max 8192 flows)

**`struct Net`** (params.bpf.h): Neural network parameters
- Layer weights: `layer_0_weight[192]`, `layer_1_weight[1024]`, `layer_2_weight[64]`
- Normalization: `mean[6]`, `scale[6]`
- Two models stored in `nn_parameters` map for hot-swapping

### Neural Network Implementation (src/mlp.bpf.h)

All operations use **fixed-point arithmetic** (16-bit fractional part):

**`standard_scaler()`**: Normalizes features using `(x - mean) / scale << 16`
- Converts int64 features to int32 normalized values
- Critical for NN performance (prevents large values from dominating)

**`linear_layer()`**: Matrix multiplication with right-shift for fixed-point
- Computes `y = Wx` with `sum >> 16` to maintain Q16.16 format
- Unrolled loops for performance

**`relu()`**: Activation function `max(x, 0)`
- Introduces non-linearity

### Traffic Feature Extraction (src/handler.bpf.h)

**`update_flow_attribute()`**: Called for every packet in a monitored flow
- Updates packet count, min/max packet length
- Tracks inter-packet duration
- Accumulates header length
- Sets `detection_start_time` when FIN/RST seen

**Features used for classification (6 total):**
1. `max_packet_length`: Largest packet size
2. `max_duration`: Longest gap between packets
3. `min_packet_length`: Smallest packet size
4. `dst_port`: Destination port number
5. `header_length`: Total TCP header bytes
6. `num_packet`: Total packets in flow

These features detect attacks like:
- Slowloris: Many small packets, long duration
- DDoS: Many packets, short duration
- Port scans: Different ports, few packets each

### Userspace Loader (src/xdp.c)

**Key responsibilities:**
1. Loads eBPF programs from skeleton
2. Populates `progs` map with program file descriptors for tail calls
3. Initializes `nn_idx` map to select which NN model to use
4. Attaches XDP program to network interface
5. Handles cleanup on exit (detaches XDP, unpins maps)

**Important:** The loader must populate the `progs` map with all 6 program FDs before the first packet arrives, otherwise tail calls will fail.

## Important Implementation Details

### Fixed-Point Arithmetic
- All NN computations use Q16.16 format (16 integer bits, 16 fractional bits)
- To convert float to fixed-point: `int32_t = (int32_t)(float_value * 65536)`
- Division is expensive in eBPF, so `linear_layer()` uses right-shift instead of division

### IP Address Filtering (xdp.bpf.c:128)
The code currently has `if (1)` which processes ALL TCP traffic. Originally filtered by source IP `33.33.33.73` for testing with dataset-instance scripts.

**To restore filtering:** Change line 128 to:
```c
if (src_ip == 555819337)  // 33.33.33.73
```

### BPF Maps
- `flow_map`: Hash table, stores per-flow statistics (max 8192 entries)
- `nn_parameters`: Array, stores 2 NN models (pinned to `/sys/fs/bpf/nn_parameters`)
- `nn_idx`: Array, index of active NN model (pinned to `/sys/fs/bpf/nn_idx`)
- `threshold_map`: Array, stores detection confidence threshold (pinned to `/sys/fs/bpf/threshold_map`)
- `progs`: Program array for tail calls (max 1024 entries)

**Pinned maps** persist across program restarts, allowing hot updates. The `threshold_map` enables runtime tuning of detection sensitivity via the `hot_threshold` program without any downtime.

### eBPF Verifier Requirements
- All packet accesses must be bounds-checked: `if (ptr + 1 > data_end) return;`
- Loop bounds must be compile-time constants (hence `#pragma clang loop unroll(full)`)
- No unbounded loops allowed

### Debugging
- Use `bpf_printk()` for logging (appears in `/sys/kernel/debug/tracing/trace_pipe`)
- The code has extensive debug prints (some commented out) showing:
  - Flow tuple information
  - Normalized feature values
  - Layer outputs
  - Final classification scores

### Dataset Structure (dataset-instance/)
- `intrusion/`: Attack traffic generators (patator, metasploit, DVMA/xss)
- `normal/`: Benign traffic generators (ftp, mysql, iperf, ssh_exec)
- Scripts use source IP `33.33.33.73` by default to match the filter

## Common Issues

**Tail call failures:** If you see "Tail call to X failed!" in trace_pipe:
- Check that `progs` map is populated before packets arrive (xdp.c:77-103)
- Verify BPF program types are set correctly (must be BPF_PROG_TYPE_XDP)

**XDP attach failures on WiFi:** WiFi drivers often don't support native XDP
- The code automatically falls back to generic/SKB mode (xdp.c:128)

**NN giving wrong results:** Check normalization in trace_pipe
- Normalized values should be between -3 and +3 typically
- Verify `mean` and `scale` arrays match the training data (mlp_params.bpf.h)

**Build errors about vmlinux.h:** Kernel must have BTF enabled
- Check: `ls /sys/kernel/btf/vmlinux` (should exist)
- Requires kernel 5.2+ with CONFIG_DEBUG_INFO_BTF=y
