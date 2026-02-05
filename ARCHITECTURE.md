# NN-eBPF ARCHITECTURE - VISUAL GUIDE

## Quick Reference Diagrams

### Neural Network Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        NEURAL NETWORK LAYERS                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  INPUT (6 features)                                                  │
│  ┌──────────────────┐                                               │
│  │ max_packet_len   │  ─┐                                           │
│  │ max_duration     │  ─┤                                           │
│  │ min_packet_len   │  ─┤                                           │
│  │ dst_port         │  ─┼─→ [NORMALIZE] ─→ Q16.16 fixed-point     │
│  │ header_length    │  ─┤                                           │
│  │ num_packet       │  ─┘                                           │
│  └──────────────────┘                                               │
│          │                                                            │
│          │ 6 normalized values                                       │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │            LAYER 0: Linear (6 → 32)                     │       │
│  │  Matrix: 6×32 weights = 192 parameters                  │       │
│  │  Operation: hidden1 = W0 × input                        │       │
│  └─────────────────────────────────────────────────────────┘       │
│          │                                                            │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │            ACTIVATION: ReLU                              │       │
│  │  hidden1[i] = max(0, hidden1[i])                        │       │
│  └─────────────────────────────────────────────────────────┘       │
│          │                                                            │
│          │ 32 activated values                                       │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │            LAYER 1: Linear (32 → 32)                    │       │
│  │  Matrix: 32×32 weights = 1024 parameters                │       │
│  │  Operation: hidden2 = W1 × hidden1                      │       │
│  └─────────────────────────────────────────────────────────┘       │
│          │                                                            │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │            ACTIVATION: ReLU                              │       │
│  │  hidden2[i] = max(0, hidden2[i])                        │       │
│  └─────────────────────────────────────────────────────────┘       │
│          │                                                            │
│          │ 32 activated values                                       │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │            LAYER 2: Linear (32 → 2)                     │       │
│  │  Matrix: 32×2 weights = 64 parameters                   │       │
│  │  Operation: output = W2 × hidden2                       │       │
│  └─────────────────────────────────────────────────────────┘       │
│          │                                                            │
│          ▼                                                            │
│  ┌──────────────────┐                                               │
│  │ output[0] = -27452 │  ─→ BENIGN score (logit)                   │
│  │ output[1] = 19569  │  ─→ ATTACK score (logit)                   │
│  └──────────────────┘                                               │
│          │                                                            │
│          ▼                                                            │
│  ┌─────────────────────────────────────────────────────────┐       │
│  │  DECISION: if (output[1] - output[0] > threshold)       │       │
│  │            then ATTACK else BENIGN                       │       │
│  └─────────────────────────────────────────────────────────┘       │
│                                                                       │
│  Total parameters: 192 + 1024 + 64 = 1280 weights                  │
│  Plus: 6 means + 6 scales = 12 normalization params                │
│  Total: 1292 parameters                                             │
└─────────────────────────────────────────────────────────────────────┘
```

### eBPF Map Layout

```
┌──────────────────────────────────────────────────────────────────────┐
│                           eBPF MAPS                                   │
├──────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  MAP 1: flow_map (BPF_MAP_TYPE_HASH)                                 │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ Key: struct flow (5-tuple)                                      │ │
│  │  ┌────────────────────────────────────────┐                    │ │
│  │  │ saddr: 10.127.132.33                   │                    │ │
│  │  │ sport: 45808                            │                    │ │
│  │  │ daddr: 10.127.132.177                  │                    │ │
│  │  │ dport: 2080                             │                    │ │
│  │  └────────────────────────────────────────┘                    │ │
│  │                                                                  │ │
│  │ Value: struct flow_attribute                                    │ │
│  │  ┌────────────────────────────────────────┐                    │ │
│  │  │ [Traffic Features]                      │                    │ │
│  │  │ num_packet: 5                           │                    │ │
│  │  │ max_packet_length: 134                  │                    │ │
│  │  │ min_packet_length: 32                   │                    │ │
│  │  │ max_duration: 9022688                   │                    │ │
│  │  │ dst_port: 2080                          │                    │ │
│  │  │ header_length: 168                      │                    │ │
│  │  │                                          │                    │ │
│  │  │ [Neural Network State]                  │                    │ │
│  │  │ hidden1[32]: Layer outputs              │                    │ │
│  │  │ hidden2[32]: Layer outputs              │                    │ │
│  │  │ nn_idx: 1 (which model)                 │                    │ │
│  │  │                                          │                    │ │
│  │  │ [Performance Metrics]                   │                    │ │
│  │  │ total_feature_extraction_time: 8920 ns  │                    │ │
│  │  │ detection_start_time: 2028429251000 ns  │                    │ │
│  │  └────────────────────────────────────────┘                    │ │
│  │                                                                  │ │
│  │ Max entries: 8192 flows                                         │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                        │
│  MAP 2: nn_parameters (BPF_MAP_TYPE_ARRAY)  [PINNED]                │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ Index 0: struct Net (Model A)                                  │ │
│  │  ┌───────────────────────────────────┐                        │ │
│  │  │ layer_0_weight[192]                │                        │ │
│  │  │ layer_1_weight[1024]               │                        │ │
│  │  │ layer_2_weight[64]                 │                        │ │
│  │  │ mean[6]: [572, 11161066668, ...]  │                        │ │
│  │  │ scale[6]: [451, 24027934608, ...] │                        │ │
│  │  └───────────────────────────────────┘                        │ │
│  │                                                                  │ │
│  │ Index 1: struct Net (Model B)  ← Currently active              │ │
│  │  ┌───────────────────────────────────┐                        │ │
│  │  │ layer_0_weight[192]                │                        │ │
│  │  │ layer_1_weight[1024]               │                        │ │
│  │  │ layer_2_weight[64]                 │                        │ │
│  │  │ mean[6]                             │                        │ │
│  │  │ scale[6]                            │                        │ │
│  │  └───────────────────────────────────┘                        │ │
│  │                                                                  │ │
│  │ Pinned to: /sys/fs/bpf/nn_parameters                           │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                        │
│  MAP 3: nn_idx (BPF_MAP_TYPE_ARRAY)  [PINNED]                       │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ Index 0: value = 1  ← Which model is active (0 or 1)          │ │
│  │                                                                  │ │
│  │ Pinned to: /sys/fs/bpf/nn_idx                                  │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                        │
│  MAP 4: progs (BPF_MAP_TYPE_PROG_ARRAY)                             │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │ Index 0: FD of xdp_preprocessing                               │ │
│  │ Index 1: FD of xdp_input_linear                                │ │
│  │ Index 2: FD of xdp_input_relu                                  │ │
│  │ Index 3: FD of xdp_hidden_linear                               │ │
│  │ Index 4: FD of xdp_hidden_relu                                 │ │
│  │ Index 5: FD of xdp_output_linear                               │ │
│  └────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────┘
```

### Packet Processing Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                    PACKET ARRIVES at NIC                             │
│                                                                       │
│         ┌───┬───┬───┬───┬───┬───┬───┬───┬───┐                     │
│   HEX:  │45 │00 │00 │3c │... │... │06 │... │...│                  │
│         └───┴───┴───┴───┴───┴───┴───┴───┴───┘                     │
│          │   │                   │                                   │
│          │   └─ IP header        └─ Protocol (6=TCP)               │
│          └─ IPv4                                                     │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│                    XDP HOOK (Before netstack)                        │
│                                                                       │
│  User decides:                                                       │
│  • XDP_PASS:    Continue to network stack                           │
│  • XDP_DROP:    Discard packet immediately                          │
│  • XDP_TX:      Bounce packet back to same NIC                      │
│  • XDP_REDIRECT: Send to another NIC                                │
│  • XDP_ABORTED: Error occurred                                      │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│                         xdp_prog (ENTRY)                             │
│                                                                       │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 1. Validate packet boundaries                        │           │
│  │    if (eth + 1 > data_end) return XDP_PASS;        │           │
│  │    if (ip + 1 > data_end) return XDP_PASS;         │           │
│  │    if (tcp + 1 > data_end) return XDP_PASS;        │           │
│  └─────────────────────────────────────────────────────┘           │
│                              ↓                                        │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 2. Filter packets                                    │           │
│  │    Is IPv4? Is TCP? Matches IP filter?             │           │
│  │    → No: return XDP_PASS                            │           │
│  └─────────────────────────────────────────────────────┘           │
│                              ↓                                        │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 3. Extract flow 5-tuple                             │           │
│  │    struct flow f = {                                 │           │
│  │      .saddr = ip->saddr,  // 10.127.132.33         │           │
│  │      .sport = tcp->source, // 45808                 │           │
│  │      .daddr = ip->daddr,  // 10.127.132.177        │           │
│  │      .dport = tcp->dest   // 2080                   │           │
│  │    };                                                │           │
│  └─────────────────────────────────────────────────────┘           │
│                              ↓                                        │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 4. Lookup/create flow in flow_map                   │           │
│  │    attr = bpf_map_lookup_elem(&flow_map, &f);      │           │
│  │    if (!attr) {                                      │           │
│  │      // New flow - initialize                       │           │
│  │      init_flow_attribute(&new_attr);                │           │
│  │      bpf_map_update_elem(&flow_map, &f, &new_attr);│           │
│  │    }                                                 │           │
│  └─────────────────────────────────────────────────────┘           │
│                              ↓                                        │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 5. Update flow statistics                           │           │
│  │    attr->num_packet++;                              │           │
│  │    attr->max_packet_length = max(...);             │           │
│  │    attr->min_packet_length = min(...);             │           │
│  │    attr->max_duration = max(...);                  │           │
│  │    attr->header_length += tcp->doff * 4;           │           │
│  └─────────────────────────────────────────────────────┘           │
│                              ↓                                        │
│  ┌─────────────────────────────────────────────────────┐           │
│  │ 6. Check if connection ending                       │           │
│  │    if (tcp->fin || tcp->rst) {                      │           │
│  │      // Trigger neural network                      │           │
│  │      bpf_tail_call(ctx, &progs, 0);                │           │
│  │    }                                                 │           │
│  │    return XDP_PASS;                                 │           │
│  └─────────────────────────────────────────────────────┘           │
└─────────────────────────────────────────────────────────────────────┘
                              ↓ (if FIN/RST)
          ┌────────────────────────────────────────┐
          │  TAIL CALL CHAIN (Neural Network)      │
          └────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [0] xdp_preprocessing                             │
    │     • Normalize 6 features                        │
    │     • Convert to Q16.16 fixed-point              │
    │     bpf_tail_call(ctx, &progs, 1) ──────────────→│
    └──────────────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [1] xdp_input_linear                              │
    │     • Layer 0: 6→32 (192 multiplications)        │
    │     bpf_tail_call(ctx, &progs, 2) ──────────────→│
    └──────────────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [2] xdp_input_relu                                │
    │     • Apply ReLU (32 comparisons)                │
    │     bpf_tail_call(ctx, &progs, 3) ──────────────→│
    └──────────────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [3] xdp_hidden_linear                             │
    │     • Layer 1: 32→32 (1024 multiplications)      │
    │     bpf_tail_call(ctx, &progs, 4) ──────────────→│
    └──────────────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [4] xdp_hidden_relu                               │
    │     • Apply ReLU (32 comparisons)                │
    │     bpf_tail_call(ctx, &progs, 5) ──────────────→│
    └──────────────────────────────────────────────────┘
                              ↓
    ┌──────────────────────────────────────────────────┐
    │ [5] xdp_output_linear                             │
    │     • Layer 2: 32→2 (64 multiplications)         │
    │     • Decision: BENIGN vs ATTACK                 │
    │     • Log result                                  │
    │     • return XDP_PASS (or XDP_DROP)              │
    └──────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────────┐
│              Packet continues to network stack                       │
│                   (or dropped if XDP_DROP)                           │
└─────────────────────────────────────────────────────────────────────┘
```

### Fixed-Point Arithmetic (Q16.16)

```
┌──────────────────────────────────────────────────────────────────┐
│              FIXED-POINT REPRESENTATION (Q16.16)                  │
├──────────────────────────────────────────────────────────────────┤
│                                                                    │
│  32-bit integer divided into two parts:                          │
│                                                                    │
│  ┌────────────────────────┬────────────────────────┐            │
│  │  Integer Part (16 bit)  │ Fractional Part (16 bit)│            │
│  │     Sign + 15 bits      │      16 bits            │            │
│  └────────────────────────┴────────────────────────┘            │
│                                                                    │
│  Example: 1.5 in Q16.16                                          │
│  ┌────────────────────────┬────────────────────────┐            │
│  │         0x0001          │       0x8000            │            │
│  │    (1 in decimal)       │    (0.5 in decimal)     │            │
│  └────────────────────────┴────────────────────────┘            │
│                                                                    │
│  Combined: 0x00018000 = 98304 in decimal                         │
│  To convert: 98304 / 65536 = 1.5                                 │
│                                                                    │
│  ───────────────────────────────────────────────────────────    │
│                                                                    │
│  OPERATIONS:                                                      │
│                                                                    │
│  1. Float to Fixed-Point:                                        │
│     fixed = (int32_t)(float_value × 65536)                       │
│                                                                    │
│  2. Fixed-Point to Float:                                        │
│     float = (float)(fixed_value) / 65536.0                       │
│                                                                    │
│  3. Addition/Subtraction (same as integers):                     │
│     result = a + b  (no adjustment needed)                       │
│                                                                    │
│  4. Multiplication (need to adjust):                             │
│     int64_t product = ((int64_t)a) * ((int64_t)b);              │
│     int32_t result = product >> 16;  // Right-shift by 16       │
│                                                                    │
│  5. Division (need to adjust):                                   │
│     int32_t result = (((int64_t)a) << 16) / b;                  │
│                                                                    │
│  ───────────────────────────────────────────────────────────    │
│                                                                    │
│  YOUR LOG EXAMPLES:                                               │
│                                                                    │
│  norm[0] = -63646                                                 │
│    → Float: -63646 / 65536 ≈ -0.971                             │
│                                                                    │
│  norm[1] = -30417                                                 │
│    → Float: -30417 / 65536 ≈ -0.464                             │
│                                                                    │
│  Score [BENIGN] = -27452                                          │
│    → Float: -27452 / 65536 ≈ -0.419                             │
│                                                                    │
│  Score [ATTACK] = 19569                                           │
│    → Float: 19569 / 65536 ≈ +0.299                              │
│                                                                    │
│  ───────────────────────────────────────────────────────────    │
│                                                                    │
│  RANGE & PRECISION:                                               │
│                                                                    │
│  • Minimum value: -32768.0                                        │
│  • Maximum value: +32767.99998                                    │
│  • Precision: 1/65536 ≈ 0.0000152587890625                       │
│  • Good for: Values between -1000 and +1000                      │
│  • Neural network outputs: Usually -10 to +10                    │
│                                                                    │
└──────────────────────────────────────────────────────────────────┘
```

### Hot Update Process

```
┌────────────────────────────────────────────────────────────────────┐
│                    HOT UPDATE MECHANISM                             │
├────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  INITIAL STATE:                                                     │
│  ┌──────────────────────────────────────────────────────┐         │
│  │ nn_parameters[0] = Old Model (inactive)              │         │
│  │ nn_parameters[1] = Current Model (ACTIVE) ←───┐     │         │
│  │ nn_idx[0] = 1  ─────────────────────────────────┘     │         │
│  └──────────────────────────────────────────────────────┘         │
│         │                                                           │
│         │ XDP program reads nn_idx[0] → 1                         │
│         │ Uses nn_parameters[1] for classification                │
│         │                                                           │
│  ┌──────▼─────────────────────────────────────────────┐          │
│  │  PACKETS FLOWING: Using Model 1                     │          │
│  │  10.127.132.33:45808 → BENIGN (using model 1)      │          │
│  │  10.127.132.33:45809 → ATTACK (using model 1)      │          │
│  │  ...                                                 │          │
│  └──────────────────────────────────────────────────────┘          │
│                                                                      │
│  ═══════════════════════════════════════════════════════          │
│                                                                      │
│  HOT UPDATE STARTS:                                                │
│  ┌──────────────────────────────────────────────────────┐         │
│  │ Terminal 1: XDP keeps running                        │         │
│  │ Terminal 2: $ sudo ./hot_updating                    │         │
│  └──────────────────────────────────────────────────────┘         │
│         │                                                           │
│         ├─ 1. Read current index:                                 │
│         │     bpf_map_lookup_elem(nn_idx, ...) → 1                │
│         │                                                           │
│         ├─ 2. Calculate next slot:                                │
│         │     new_idx = (1 + 1) % 2 = 0                           │
│         │                                                           │
│         ├─ 3. Load new model into slot 0:                         │
│         │     struct Net new_model = {...new weights...};         │
│         │     bpf_map_update_elem(nn_parameters, &0, &new_model); │
│         │                                                           │
│  ┌──────▼─────────────────────────────────────────────┐          │
│  │ nn_parameters[0] = NEW Model (staged) ←──┐         │          │
│  │ nn_parameters[1] = Old Model (ACTIVE)    │         │          │
│  │ nn_idx[0] = 1 ───────────────────────────┘         │          │
│  └──────────────────────────────────────────────────────┘          │
│         │                                                           │
│         │ XDP still using model 1 (no interruption)               │
│         │                                                           │
│         ├─ 4. ATOMIC SWITCH:                                      │
│         │     bpf_map_update_elem(nn_idx, &0, &0);  // 1 → 0     │
│         │                                                           │
│  ┌──────▼─────────────────────────────────────────────┐          │
│  │ nn_parameters[0] = NEW Model (ACTIVE NOW!) ←───┐   │          │
│  │ nn_parameters[1] = Old Model (inactive)         │   │          │
│  │ nn_idx[0] = 0  ─────────────────────────────────┘   │          │
│  └──────────────────────────────────────────────────────┘          │
│         │                                                           │
│         │ Next packet reads nn_idx[0] → 0                         │
│         │ Now uses nn_parameters[0] (new model)                   │
│         │                                                           │
│  ┌──────▼─────────────────────────────────────────────┐          │
│  │  PACKETS NOW FLOWING: Using NEW Model 0             │          │
│  │  10.127.132.33:45810 → BENIGN (using NEW model 0)  │          │
│  │  10.127.132.33:45811 → ATTACK (using NEW model 0)  │          │
│  │  ...                                                 │          │
│  └──────────────────────────────────────────────────────┘          │
│                                                                      │
│  RESULT: Zero downtime, instant switch!                           │
│  • No packets dropped                                              │
│  • No XDP restart needed                                           │
│  • Atomic operation (no race conditions)                          │
│                                                                      │
└────────────────────────────────────────────────────────────────────┘
```

### Training Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                        TRAINING PIPELINE                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  STAGE 1: Data Collection                                           │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ Benign Traffic:          Attack Traffic:                    │   │
│  │ • HTTP requests          • Slowloris (slow POST)            │   │
│  │ • SSH sessions           • DDoS (SYN flood)                 │   │
│  │ • FTP transfers          • Port scans                       │   │
│  │ • MySQL queries          • SQL injection attempts           │   │
│  │ • Normal browsing        • XSS attacks                      │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 2: Feature Extraction                                        │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ For each TCP flow:                                          │   │
│  │ [134, 9022688, 32, 2080, 168, 5] ← Sample flow             │   │
│  │  │     │       │    │     │    │                            │   │
│  │  │     │       │    │     │    └─ num_packet               │   │
│  │  │     │       │    │     └────── header_length            │   │
│  │  │     │       │    └──────────── dst_port                 │   │
│  │  │     │       └───────────────── min_packet_length        │   │
│  │  │     └───────────────────────── max_duration             │   │
│  │  └─────────────────────────────── max_packet_length        │   │
│  │                                                              │   │
│  │ Labels: 0 = Benign, 1 = Attack                             │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 3: Normalization (StandardScaler)                           │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ Calculate mean and scale for each feature:                 │   │
│  │                                                              │   │
│  │ mean[0] = 572 (average max_packet_length)                  │   │
│  │ scale[0] = 451 (std dev of max_packet_length)              │   │
│  │                                                              │   │
│  │ Transform: X_norm = (X - mean) / scale                     │   │
│  │                                                              │   │
│  │ Example:                                                     │   │
│  │   Raw: [134, 9022688, 32, 2080, 168, 5]                   │   │
│  │   →                                                          │   │
│  │   Normalized: [-0.97, -0.46, -0.07, 0.95, -0.24, -0.24]   │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 4: Neural Network Training (mlp_train.py)                   │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ for epoch in range(32):                                     │   │
│  │   for batch in train_loader:                               │   │
│  │     1. Forward pass: prediction = model(X)                 │   │
│  │     2. Calculate loss: loss = CrossEntropy(pred, label)    │   │
│  │     3. Backward pass: loss.backward()                      │   │
│  │     4. Update weights: optimizer.step()                    │   │
│  │                                                              │   │
│  │ Optimizer: Adam (lr=0.001)                                 │   │
│  │ Loss function: CrossEntropyLoss                            │   │
│  │ Batch size: 512                                             │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 5: Testing                                                   │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ Accuracy: (TP + TN) / Total                                │   │
│  │ Precision: TP / (TP + FP)                                  │   │
│  │ Recall: TP / (TP + FN)                                     │   │
│  │ F1 Score: 2 × (P × R) / (P + R)                           │   │
│  │                                                              │   │
│  │ Typical results: 90-95% accuracy                           │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 6: Quantization (mlp_quant.py)                              │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ Convert float32 → int32 (Q16.16):                          │   │
│  │                                                              │   │
│  │ weight_fixed = round(weight_float × 65536)                 │   │
│  │                                                              │   │
│  │ Example:                                                     │   │
│  │   Float: 0.007 → Fixed: 459                                │   │
│  │   Float: -0.018 → Fixed: -1179                             │   │
│  │                                                              │   │
│  │ Output: mlp_params.bpf.h (C header file)                   │   │
│  └────────────────────────────────────────────────────────────┘   │
│                              ↓                                        │
│  STAGE 7: Compile & Deploy                                         │
│  ┌────────────────────────────────────────────────────────────┐   │
│  │ $ make                                                      │   │
│  │   → Compiles xdp.bpf.c (includes mlp_params.bpf.h)        │   │
│  │   → Generates xdp.bpf.o (eBPF bytecode)                    │   │
│  │   → Creates xdp (userspace loader)                         │   │
│  │                                                              │   │
│  │ $ sudo ./xdp wlan0                                         │   │
│  │   → Loads NN into kernel                                   │   │
│  │   → Starts packet analysis                                 │   │
│  └────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Summary

This NN-eBPF system is an elegant fusion of:

- **Machine Learning:** Multi-layer perceptron for pattern recognition
- **Systems Programming:** Low-level C code running in kernel space
- **Network Security:** Real-time intrusion detection at line rate
- **Fixed-Point Math:** Efficient computation without floating point
- **eBPF Technology:** Safe, fast, and flexible kernel extensions

The entire neural network runs in **~60 microseconds** per flow, making it one of the fastest intrusion detection systems possible. By running in XDP, it processes packets before they even reach the Linux network stack, providing unparalleled protection.
