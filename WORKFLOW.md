# NN-eBPF INTRUSION DETECTION SYSTEM - COMPLETE WORKFLOW

## TABLE OF CONTENTS
1. [High-Level Overview](#high-level-overview)
2. [Machine Learning Algorithm](#machine-learning-algorithm)
3. [Training Process](#training-process)
4. [Normalization Explained](#normalization-explained)
5. [Python Files Explained](#python-files-explained)
6. [Tail Calls Explained](#tail-calls-explained)
7. [Hot Updating](#hot-updating)
8. [eBPF Maps](#ebpf-maps)
9. [Flow & Features](#flow--features)
10. [Retraining the Model](#retraining-the-model)
11. [Activation Functions (ReLU)](#activation-functions-relu)
12. [Understanding Scores](#understanding-scores)
13. [Step-by-Step Workflow Diagram](#step-by-step-workflow-diagram)
14. [Your Log Analysis](#your-log-analysis)

---

## HIGH-LEVEL OVERVIEW

This system runs a **Neural Network directly inside the Linux kernel** to detect network attacks in real-time. Think of it as having a brain inside your network card that can instantly decide if incoming traffic is malicious.

**Why is this special?**
- Normal intrusion detection systems run in userspace (slow, can be bypassed)
- This runs in XDP (eXpress Data Path) - processes packets BEFORE they reach the network stack
- Decisions made in microseconds (~60μs in your logs)
- Can't be bypassed by root users or malware

**What it does:**
1. Watches every TCP packet
2. Collects statistics per connection (packet count, size, timing)
3. When connection ends (FIN/RST flag), runs neural network
4. Neural network decides: BENIGN or ATTACK
5. Can block attack (currently just logs it)

---

## MACHINE LEARNING ALGORITHM

### What Algorithm?
**Multi-Layer Perceptron (MLP)** - A type of artificial neural network

**Architecture:**
```
INPUT LAYER      HIDDEN LAYER 1    HIDDEN LAYER 2    OUTPUT LAYER
   6 neurons  →     32 neurons   →    32 neurons   →   2 neurons
   (features)       (learning)         (learning)      (decision)
```

**Layer breakdown:**
- **Layer 0 (Input):** 6 features → 32 neurons
  - Takes 6 traffic features
  - Multiplies by 192 weights (6×32)
  - Produces 32 intermediate values

- **Layer 1 (Hidden):** 32 → 32 neurons
  - Takes 32 values from layer 0
  - Multiplies by 1024 weights (32×32)
  - Produces 32 new intermediate values

- **Layer 2 (Output):** 32 → 2 neurons
  - Takes 32 values from layer 1
  - Multiplies by 64 weights (32×2)
  - Produces 2 final scores:
    - `score[0]` = confidence this is BENIGN traffic
    - `score[1]` = confidence this is ATTACK traffic

### Why MLP?
- **Fast:** Simple matrix multiplications (good for kernel)
- **Compact:** No floating point needed (eBPF doesn't support it)
- **Effective:** 90%+ accuracy on detecting attacks
- **Small:** Fits in eBPF instruction limit (with tail calls)

---

## TRAINING PROCESS

### Where Training Happens: `mlp_train.py`

**Step-by-step training:**

#### 1. Load Dataset
```python
data = np.load('../dataset/reproduction-xdp-data.npy')    # Traffic features
label = np.load('../dataset/reproduction-xdp-label.npy')  # 0=benign, 1=attack
```

Dataset contains real traffic captures:
- **Benign traffic:** Normal web browsing, SSH, FTP, MySQL
- **Attack traffic:** Slowloris, DDoS, SQL injection, XSS

#### 2. Split Data (80/20 train/test)
```python
X_train, X_test, y_train, y_test = model_selection.train_test_split(
    data, label, train_size=0.8, test_size=0.2
)
```

#### 3. Normalize Features (CRITICAL!)
```python
standard_scaler = preprocessing.StandardScaler()
standard_scaler.fit(data)  # Calculate mean and scale for each feature
X_train = standard_scaler.transform(X_train)  # Normalize training data
X_test = standard_scaler.transform(X_test)    # Normalize test data
```

**Why normalize?**
- Feature values have different ranges:
  - `num_packet`: 1 to 10,000
  - `dst_port`: 1 to 65535
  - `max_duration`: nanoseconds (billions)

- Without normalization, large values dominate
- Neural networks learn better with values around -3 to +3

**What it does:**
```
normalized_value = (original_value - mean) / standard_deviation
```

#### 4. Train Neural Network
```python
model = Net(6, 2)  # 6 inputs, 2 outputs (benign/attack)
train(num_epoch=32, train_loader, model, device, learning_rate=0.001)
```

**Training process (32 epochs):**
- Each epoch: Show network all training examples
- Network makes predictions
- Calculate error (how wrong it was)
- Adjust weights to reduce error
- Repeat until error is minimized

**What are weights?**
- Numbers that control how much each input matters
- Start random, get adjusted during training
- Final weights encode the "knowledge" of what attacks look like

#### 5. Save Model + Normalization Parameters
```python
torch.save({
    'state_dict': model.state_dict(),  # Neural network weights
    'mean': standard_scaler.mean_,     # Mean for each feature
    'scale': standard_scaler.scale_    # Scale for each feature
}, 'mlp.th')
```

**Why save mean/scale?**
- Kernel must normalize incoming traffic THE SAME WAY as training
- If normalization differs, network will give garbage results

---

## NORMALIZATION EXPLAINED

### What is Standard Scaler?

**Formula:**
```
normalized_feature = (raw_feature - mean) / scale
```

**Example from your logs:**

```
Raw features BEFORE normalization:
  max_len=134, max_dur=9022688, min_len=32
  dst_port=2080, hdr_len=168, num_pkt=5

Normalization params:
  mean: [572, 11161066668, 36, ?, ?, ?]
  scale: [451, 24027934608, 54, ?, ?, ?]

Normalized features AFTER scaling:
  norm[0]=-63646   (max_len: (134-572)/451 = -0.97 in float)
  norm[1]=-30417   (max_dur: (9022688-11161066668)/24027934608 = -0.46)
  norm[2]=-4854    (min_len: (32-36)/54 = -0.07)
```

### Why Fixed-Point (Q16.16 Format)?

eBPF doesn't support floating point, so we use **fixed-point arithmetic:**

**Q16.16 = 16 bits integer + 16 bits fractional**

```
Float value: 1.5
Fixed-point: 1.5 × 65536 = 98304

Float value: -0.97
Fixed-point: -0.97 × 65536 ≈ -63646 (your norm[0])
```

**How it works in code:**
```c
// Normalization (mlp.bpf.h)
if (mean[i] > x[i])
    y[i] = -((int32_t)(((uint64_t)mean[i] - (uint64_t)x[i]) * (1 << 16) / (uint64_t)scale[i]));
else
    y[i] = (int32_t)(((uint64_t)x[i] - (uint64_t)mean[i]) * (1 << 16) / (uint64_t)scale[i]);
```

**Matrix multiplication (mlp.bpf.h):**
```c
sum += ((int64_t)(W[i * N + j])) * ((int64_t)(x[j]));
y[i] = sum >> 16;  // Right-shift divides by 65536 (keeps fixed-point format)
```

---

## PYTHON FILES EXPLAINED

### 1. `mlp.py` - Neural Network Definition
**Purpose:** Defines the neural network architecture

```python
class Net(nn.Module):
    def __init__(self, in_dim, out_dim):
        self.model = nn.Sequential(
            nn.Linear(in_dim, 32, bias=False),  # Layer 0: 6→32
            nn.ReLU(),                          # Activation
            nn.Linear(32, 32, bias=False),      # Layer 1: 32→32
            nn.ReLU(),                          # Activation
            nn.Linear(32, out_dim, bias=False), # Layer 2: 32→2
        )
```

**Key points:**
- `bias=False`: No bias terms (simpler for eBPF)
- 3 linear layers + 2 ReLU activations
- This Python definition matches the C implementation in xdp.bpf.c

### 2. `mlp_train.py` - Training Script
**Purpose:** Train the model and save parameters

**What it does:**
1. Loads traffic dataset from `.npy` files
2. Splits into training (80%) and test (20%)
3. Normalizes features using StandardScaler
4. Trains neural network for 32 epochs
5. Tests accuracy on test set
6. Saves trained model + normalization params to `mlp.th`

**When to run:**
- When you get new attack data
- When you want to improve accuracy
- After collecting benign traffic from your network

### 3. `mlp_quant.py` - Quantization Script
**Purpose:** Convert floating-point model to fixed-point for eBPF

**What it does:**
```python
params = params * (2 ** 16)  # Multiply by 65536
params = params.round().int() # Convert to integers
```

**Converts:**
- Float weights → int32_t weights (Q16.16)
- Float mean → int64_t mean (rounded)
- Float scale → int64_t scale (rounded)

**Outputs:** `mlp_params.bpf.h` (C header file)

**Example output:**
```c
#define FIX_POINT 16
#define N0 6
#define N1 32
#define N2 32
#define N3 2

static const int32_t layer_0_weight[192] = {459, -1234, 5678, ...};
static const int32_t layer_1_weight[1024] = {...};
static const int32_t layer_2_weight[64] = {...};
static const int64_t mean[6] = {572, 11161066668, 36, ...};
static const int64_t scale[6] = {451, 24027934608, 54, ...};
```

**When to run:**
- After training (`mlp_train.py`)
- Before compiling eBPF program

**Command:**
```bash
python3 mlp_quant.py mlp.th 16
#                     ↑      ↑
#                model file  fix point bits
```

---

## TAIL CALLS EXPLAINED

### What are Tail Calls?

**Problem:** eBPF programs have a ~1 million instruction limit
**Solution:** Split program into smaller pieces and chain them together

**Tail call = Jump to another eBPF program (never returns!)**

```c
bpf_tail_call(ctx, &progs, index);
// Execution continues in the program at progs[index]
// This line NEVER executes unless tail call fails!
```

### Why Needed?

The full neural network would exceed the instruction limit:
- 6→32 layer: 192 multiplications
- 32→32 layer: 1024 multiplications
- 32→2 layer: 64 multiplications
- Plus ReLU, normalization, logging
- Total: Too many instructions!

### The Chain (6 Programs)

```
PACKET ARRIVES
    ↓
[0] xdp_prog
    - Entry point (SEC("xdp"))
    - Extracts flow tuple
    - Updates flow statistics
    - Waits for TCP FIN/RST
    ↓ (when connection ends)
[1] xdp_preprocessing (tail call index 0)
    - Normalizes 6 features
    - Converts to Q16.16 format
    ↓
[2] xdp_input_linear (tail call index 1)
    - Layer 0: 6→32 (matrix multiply)
    ↓
[3] xdp_input_relu (tail call index 2)
    - Apply ReLU to layer 0
    ↓
[4] xdp_hidden_linear (tail call index 3)
    - Layer 1: 32→32 (matrix multiply)
    ↓
[5] xdp_hidden_relu (tail call index 4)
    - Apply ReLU to layer 1
    ↓
[6] xdp_output_linear (tail call index 5)
    - Layer 2: 32→2 (matrix multiply)
    - Makes final decision
    - Logs result
    - Returns XDP_PASS or XDP_DROP
```

### How Tail Calls Work (Implementation)

**Step 1: Define program array map (`xdp.bpf.c`)**
```c
struct {
    __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
    __uint(max_entries, 1024);
} progs SEC(".maps");
```

**Step 2: Populate map in userspace (`xdp.c`)**
```c
struct bpf_progs_desc progs[] = {
    {"xdp_preprocessing", BPF_PROG_TYPE_XDP, 0, NULL},
    {"xdp_input_linear", BPF_PROG_TYPE_XDP, 1, NULL},
    {"xdp_input_relu", BPF_PROG_TYPE_XDP, 2, NULL},
    // ... etc
};

// Get file descriptor for each program
int prog_fd = bpf_program__fd(progs[i].prog);

// Store in progs map: progs[index] = prog_fd
bpf_map_update_elem(map_progs_fd, &map_prog_idx, &prog_fd, 0);
```

**Step 3: Call in kernel (`xdp.bpf.c`)**
```c
bpf_tail_call(ctx, &progs, 0);  // Jump to preprocessing
```

### Why "Tail" Call?

- **Tail position:** Last thing function does (no code after)
- **No return:** Calling function never gets control back
- **Stack reuse:** New program uses same stack (saves memory)

---

## HOT UPDATING

### What is `hot_updating.c`?

A userspace program that **updates the neural network WITHOUT restarting the XDP program**.

### How It Works

**Step 1: Two model slots in `nn_parameters` map**
```c
struct {
    __uint(max_entries, 2);  // Slot 0 and Slot 1
} nn_parameters SEC(".maps");
```

**Step 2: `nn_idx` map tracks active model**
```c
struct {
    __uint(max_entries, 1);
    __type(value, int32_t);  // Current model: 0 or 1
} nn_idx SEC(".maps");
```

**Step 3: Hot update process**
```c
// Read current model index (e.g., 0)
bpf_map_lookup_elem(nn_idx_fd, &zero, &nn_idx);

// Calculate next slot (1)
int new_nn_idx = (nn_idx + 1) % 2;

// Load new weights into slot 1
struct Net new_net;
memcpy(new_net.mean, mean, ...);
memcpy(new_net.layer_0_weight, layer_0_weight, ...);
bpf_map_update_elem(nn_parameters_fd, &new_nn_idx, &new_net, BPF_ANY);

// Switch active model (atomic operation)
bpf_map_update_elem(nn_idx_fd, &zero, &new_nn_idx, BPF_ANY);
```

**Step 4: Kernel uses new model automatically**
```c
// In xdp.bpf.c
int32_t *idx_ptr = bpf_map_lookup_elem(&nn_idx, &idx);  // Gets 1 now!
struct Net *net = bpf_map_lookup_elem(&nn_parameters, idx_ptr);  // Uses new weights
```

### How Frequently to Run?

**Depends on your needs:**

- **Never:** If you trained once and model works well
- **Daily:** If you collect new attack samples and retrain nightly
- **On-demand:** When you detect false positives/negatives
- **A/B testing:** Switch between models every hour to compare performance

**Can it run while XDP is running?**
- **YES!** That's the whole point
- The maps are **pinned** to `/sys/fs/bpf/` (persistent)
- Update happens instantly (atomic map update)
- No packet drops, no downtime

**Example workflow:**
```bash
# Terminal 1: XDP running
sudo ./src/.output/xdp

# Terminal 2: Update model (while XDP still running)
cd src
python3 mlp_train.py    # Train new model
python3 mlp_quant.py mlp.th 16  # Quantize
make                    # Recompile hot_updating
sudo ./src/.output/hot_updating  # Swap models
```

---

## eBPF MAPS

### What are eBPF Maps?

**Maps = Shared memory between kernel and userspace**

Think of them as hash tables / arrays that both eBPF programs and userspace programs can access.

### Map 1: `flow_map` (Hash Table)

**Purpose:** Store statistics for each active TCP connection

**Type:** `BPF_MAP_TYPE_HASH`
**Key:** `struct flow` (5-tuple: src_ip, src_port, dst_ip, dst_port, protocol)
**Value:** `struct flow_attribute` (statistics + NN state)
**Max entries:** 8192 connections

**What's stored:**
```c
struct flow_attribute {
    // Traffic features (6 features for ML)
    s64 num_packet;        // Total packets in flow
    s64 min_packet_length; // Smallest packet
    s64 max_packet_length; // Largest packet
    s64 max_duration;      // Longest gap between packets
    s64 dst_port;          // Destination port
    s64 header_length;     // Total TCP header bytes

    // Neural network state
    s32 hidden1[32];       // Layer 0/2 output
    s32 hidden2[32];       // Layer 1 output + normalized inputs
    s32 nn_idx;            // Which model (0 or 1)

    // Performance tracking
    u64 total_feature_extraction_time;
    u64 detection_start_time;
    s64 last_packet_time;
};
```

**Lifecycle:**
1. First packet in flow → Create new entry
2. Each packet → Update statistics (packet count, max length, etc.)
3. FIN/RST packet → Trigger neural network classification
4. After classification → Entry can be removed (or kept for logging)

### Map 2: `nn_parameters` (Array)

**Purpose:** Store neural network models

**Type:** `BPF_MAP_TYPE_ARRAY`
**Key:** `int32_t` (0 or 1 - model index)
**Value:** `struct Net` (all weights + normalization params)
**Max entries:** 2 (two models for hot swapping)
**Special:** **PINNED** to `/sys/fs/bpf/nn_parameters` (persists after program exits)

**What's stored:**
```c
struct Net {
    int32_t layer_0_weight[192];   // 6×32 weights
    int32_t layer_1_weight[1024];  // 32×32 weights
    int32_t layer_2_weight[64];    // 32×2 weights
    int64_t mean[6];               // Normalization means
    int64_t scale[6];              // Normalization scales
};
```

**Why 2 slots?**
- Slot 0: Current active model
- Slot 1: Staging area for new model
- Hot update: Load new model into slot 1, then flip `nn_idx`

### Map 3: `nn_idx` (Array)

**Purpose:** Track which model is currently active

**Type:** `BPF_MAP_TYPE_ARRAY`
**Key:** `int32_t` (always 0 - single value)
**Value:** `int32_t` (0 or 1 - which model to use)
**Max entries:** 1
**Special:** **PINNED** to `/sys/fs/bpf/nn_idx` (persists after program exits)

**How it's used:**
```c
// Get active model index
int32_t idx = 0;
int32_t *active_model = bpf_map_lookup_elem(&nn_idx, &idx);
// *active_model = 0 or 1

// Get that model's parameters
struct Net *net = bpf_map_lookup_elem(&nn_parameters, active_model);
```

### Map 4: `progs` (Program Array)

**Purpose:** Store file descriptors for tail calls

**Type:** `BPF_MAP_TYPE_PROG_ARRAY`
**Key:** `u32` (program index 0-5)
**Value:** `u32` (program file descriptor)
**Max entries:** 1024 (only 6 used)

**Contents:**
```
progs[0] = fd of xdp_preprocessing
progs[1] = fd of xdp_input_linear
progs[2] = fd of xdp_input_relu
progs[3] = fd of xdp_hidden_linear
progs[4] = fd of xdp_hidden_relu
progs[5] = fd of xdp_output_linear
```

**How tail calls work:**
```c
bpf_tail_call(ctx, &progs, 2);  // Jump to progs[2] = xdp_input_relu
```

### What is `hidden1` and `hidden2`?

**They're reused arrays for different purposes at different stages:**

**`hidden2[32]`:**
- **Stage 1 (preprocessing):** Stores 6 normalized input features (indices 0-5 used)
- **Stage 2 (hidden layer):** Stores 32 hidden layer outputs (all 32 indices used)

**`hidden1[32]`:**
- **Stage 1 (input layer):** Stores 32 input layer outputs
- **Stage 2 (output layer):** Stores 2 final scores (indices 0-1 used)
  - `hidden1[0]` = BENIGN score
  - `hidden1[1]` = ATTACK score

**Why reuse?**
- Save memory (only 64 int32_t instead of 128)
- eBPF has strict stack size limits
- Values from previous stages aren't needed anymore

### What is `nn_idx`?

**Two meanings (confusing but intentional):**

1. **Map name:** `nn_idx` = the eBPF map that stores active model index
2. **Field name:** `attr->nn_idx` = which model this flow should use (copied from map)

**Flow:**
```c
// Read from map
int32_t *map_nn_idx = bpf_map_lookup_elem(&nn_idx, &zero);

// Store in flow attribute
attr->nn_idx = *map_nn_idx;  // Copy value (0 or 1)

// Later, use it
struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr->nn_idx));
```

---

## FLOW & FEATURES

### What is a Flow?

**Flow = A unique TCP connection**

Identified by **5-tuple:**
```c
struct flow {
    int saddr;  // Source IP (e.g., 192.168.1.100)
    int sport;  // Source port (e.g., 45808)
    int daddr;  // Destination IP (e.g., 10.127.132.177)
    int dport;  // Destination port (e.g., 2080)
    // protocol is always TCP (implied)
};
```

**Example from your logs:**
```
Flow: 10.127.132.33:45808 -> 10.127.132.177:2080
      └─ source IP:port   └─ destination IP:port
```

All packets with this 5-tuple belong to the same flow.

### The 6 Features (What Goes Into the Neural Network)

| Feature # | Name | Description | Example | Why Important |
|-----------|------|-------------|---------|---------------|
| 0 | `max_packet_length` | Largest packet in flow (bytes) | 134 | DDoS: Large packets, Exfiltration: Unusual sizes |
| 1 | `max_duration` | Longest gap between packets (ns) | 9,022,688 | Slowloris: Very long gaps, Normal: Milliseconds |
| 2 | `min_packet_length` | Smallest packet in flow (bytes) | 32 | Fragmentation attacks: Tiny packets |
| 3 | `dst_port` | Destination port number | 2080 | Identifies service (80=HTTP, 22=SSH) |
| 4 | `header_length` | Total TCP header bytes | 168 | Header manipulation attacks |
| 5 | `num_packet` | Total packets in flow | 5 | DDoS: Many packets, Normal: Varies |

### Features in Python Training vs C Code

**BOTH USE THE SAME 6 FEATURES!**

**In Python (`mlp_train.py`):**
```python
data = np.load('reproduction-xdp-data.npy')  # Shape: (N, 6)
# data[i] = [max_len, max_dur, min_len, dst_port, hdr_len, num_pkt]
```

**In C (`xdp.bpf.c`):**
```c
int64_t x[6] = {
    attr->max_packet_length,  // Feature 0
    attr->max_duration,       // Feature 1
    attr->min_packet_length,  // Feature 2
    attr->dst_port,           // Feature 3
    attr->header_length,      // Feature 4
    attr->num_packet          // Feature 5
};
standard_scaler(x, attr->hidden2, net->mean, net->scale, 6);
```

**Critical:** Order must match exactly! Otherwise, the model will give wrong results.

### How Features are Collected

**Every packet triggers `update_flow_attribute()` (`handler.bpf.h`):**

```c
static inline void update_flow_attribute(struct flow *f, struct tcphdr *tcp, u64 packet_length) {
    struct flow_attribute *attr = bpf_map_lookup_elem(&flow_map, f);

    // First packet? Initialize
    if (!attr) {
        struct flow_attribute new_attr = {0};
        init_flow_attribute(&new_attr);
        new_attr.dst_port = bpf_ntohs(tcp->dest);
        bpf_map_update_elem(&flow_map, f, &new_attr, BPF_NOEXIST);
        attr = bpf_map_lookup_elem(&flow_map, f);
    }

    // Update features
    attr->num_packet += 1;
    attr->min_packet_length = MIN(attr->min_packet_length, packet_length);
    attr->max_packet_length = MAX(attr->max_packet_length, packet_length);

    u64 duration = packet_time - attr->last_packet_time;
    attr->max_duration = MAX(attr->max_duration, duration);
    attr->last_packet_time = packet_time;

    attr->header_length += tcp->doff * 4;  // doff = header length in 32-bit words

    // Connection ending? Trigger classification
    if (tcp->fin || tcp->rst) {
        attr->detection_start_time = bpf_ktime_get_ns();
    }
}
```

---

## RETRAINING THE MODEL

### Can You Train the Model Again?
**YES! Absolutely.**

### Can You Change Parameters?
**YES! You can change:**
- Neural network architecture (layers, neurons)
- Training hyperparameters (learning rate, epochs, batch size)
- Dataset (add new attacks, remove attacks, add your own traffic)

### How to Retrain

#### Option 1: Use Existing Dataset (Quick)
```bash
cd src
python3 mlp_train.py           # Train model (takes 1-5 minutes)
python3 mlp_quant.py mlp.th 16 # Convert to fixed-point
make                           # Recompile C programs
sudo ./src/.output/xdp         # Run with new model
```

#### Option 2: Collect Your Own Data (Recommended)

**Step 1: Collect benign traffic**
```bash
# Run XDP in monitoring mode (collect features, don't classify)
sudo ./src/.output/xdp

# Generate normal traffic
curl http://example.com
ssh user@server
mysql -h database.com
# ... use your applications normally

# Features are logged in trace_pipe, or you can modify code to save to file
```

**Step 2: Collect attack traffic**
```bash
# Use dataset-instance/ scripts or your own attack tools
cd dataset-instance/intrusion
./slowloris.sh   # Generate slowloris attack
./ddos.sh        # Generate DDoS attack

# Or use real attack captures (PCAP files)
```

**Step 3: Format dataset**
Create numpy arrays:
```python
# benign_data.shape = (N_benign, 6)  # 6 features per sample
# attack_data.shape = (N_attack, 6)

data = np.concatenate([benign_data, attack_data])
labels = np.array([0]*N_benign + [1]*N_attack)  # 0=benign, 1=attack

np.save('my-data.npy', data)
np.save('my-label.npy', labels)
```

**Step 4: Update `mlp_train.py`**
```python
data_path = 'my-data.npy'   # Change these paths
label_path = 'my-label.npy'
```

**Step 5: Train**
```bash
python3 mlp_train.py
python3 mlp_quant.py mlp.th 16
```

### Changing Architecture

**Example: Make network bigger (more accurate but slower)**

**Edit `mlp.py`:**
```python
class Net(nn.Module):
    def __init__(self, in_dim, out_dim):
        super().__init__()
        self.model = nn.Sequential(
            nn.Linear(in_dim, 64, bias=False),   # Changed: 32→64
            nn.ReLU(),
            nn.Linear(64, 64, bias=False),       # Changed: 32→64
            nn.ReLU(),
            nn.Linear(64, out_dim, bias=False),  # Changed: 32→64
        )
```

**Edit `params.bpf.h`:**
```c
struct Net {
    int32_t layer_0_weight[384];   // 6×64 (was 192)
    int32_t layer_1_weight[4096];  // 64×64 (was 1024)
    int32_t layer_2_weight[128];   // 64×2 (was 64)
    int64_t mean[6];
    int64_t scale[6];
};
```

**Edit `common.h`:**
```c
struct flow_attribute {
    // ...
    s32 hidden1[64];  // Changed: 32→64
    s32 hidden2[64];  // Changed: 32→64
};
```

**Update all eBPF functions to use 64 instead of 32.**

**Retrain and recompile.**

---

## ACTIVATION FUNCTIONS (ReLU)

### What is an Activation Function?

**Purpose:** Introduce non-linearity into neural networks

**Why needed?**
Without activation functions, stacking layers is pointless:
```
Linear(A) + Linear(B) = Linear(A+B)
3 layers without activation = 1 layer
```

With activation functions:
```
Linear(A) + ReLU + Linear(B) ≠ Linear(anything)
Can learn complex, non-linear patterns
```

### ReLU (Rectified Linear Unit)

**Name:** ReLU = Rectified Linear Unit
**Formula:** `f(x) = max(0, x)`

**What it does:**
- If input is positive: Keep it unchanged
- If input is negative: Set it to zero

**Graphically:**
```
      │
    5 │         ╱
    4 │        ╱
    3 │       ╱
    2 │      ╱
    1 │     ╱
    0 │────┘─────
      │-5 -3 -1 1 3 5
```

**Example:**
```
Input:  [-5, -2, 0, 3, 7]
Output: [ 0,  0, 0, 3, 7]
```

**Implementation in C:**
```c
static inline void relu(int32_t *tensor, const int32_t size) {
    for (int32_t i = 0; i < size; i++)
        tensor[i] = MAX(tensor[i], 0);  // MAX(tensor[i], 0)
}
```

### Why ReLU is Popular

**Advantages:**
1. **Fast:** Simple comparison and assignment
2. **No gradient vanishing:** Unlike sigmoid/tanh, gradients don't shrink
3. **Sparse activation:** Many neurons output 0 (efficient)
4. **Works well in practice:** Despite simplicity, very effective

**Disadvantages:**
- **Dying ReLU:** If a neuron always outputs 0, it stops learning
- Solution: Use variants (Leaky ReLU, PReLU)

### Where ReLU is Applied

**In your logs, you can see ReLU twice:**
```
[INPUT LINEAR] Computing layer 0: 6 inputs -> 32 neurons
[INPUT RELU] Applying activation function (ReLU)...     <-- ReLU #1
[HIDDEN LINEAR] Computing layer 1: 32 -> 32 neurons
[HIDDEN RELU] Applying activation function (ReLU)...    <-- ReLU #2
[OUTPUT LINEAR] Computing layer 2: 32 -> 2 outputs (BENIGN vs ATTACK)
```

**No ReLU after output layer!** Because:
- Output scores can be negative
- We compare scores (difference matters, not absolute value)
- ReLU would change the meaning of the scores

---

## UNDERSTANDING SCORES

### Why Scores Aren't [0,1]?

**Your question: "shouldn't scores be between [0,1]?"**

**Answer: NO, not in this implementation!**

Here's why:

### What are Logits?

The output layer produces **logits** (raw scores), not probabilities.

**Logits:**
- Raw output from neural network
- Can be any value: -∞ to +∞
- Used directly for classification (argmax)

**Probabilities:**
- Converted from logits using softmax
- Always between 0 and 1
- Sum to 1 across all classes

**This system uses LOGITS, not probabilities!**

### From Logits to Probabilities

**In Python (`mlp.py`):**
```python
y_pred = F.softmax(output, dim=1).argmax(dim=1)
```

**Softmax formula:**
```
prob[benign] = e^(score_benign) / (e^(score_benign) + e^(score_attack))
prob[attack] = e^(score_attack) / (e^(score_benign) + e^(score_attack))
```

**But in eBPF, softmax is expensive (exponential function)!**
So we skip it and just compare raw scores.

### Why This Works

**Key insight:** `argmax(softmax(x)) = argmax(x)`

Softmax doesn't change which score is highest, it just converts to probabilities.

**Example:**
```
Logits: [BENIGN=-27452, ATTACK=19569]
Softmax: [BENIGN≈0.0000...01, ATTACK≈0.9999...99]
Decision: ATTACK wins (same in both cases)
```

**So the eBPF code just compares logits directly:**
```c
int label = (attr->hidden1[1] > attr->hidden1[0]) ? 1 : 0;
// If ATTACK score > BENIGN score, classify as ATTACK
```

### Your Log Analysis

```
Score [BENIGN]: -27452
Score [ATTACK]: 19569
```

**What this means:**
- **BENIGN score:** -27452 (in Q16.16 fixed-point)
  - As float: -27452 / 65536 ≈ -0.42
- **ATTACK score:** 19569 (in Q16.16 fixed-point)
  - As float: 19569 / 65536 ≈ +0.30

**Difference:** 19569 - (-27452) = 47021

**Decision:**
```c
// Original code (commented out):
// int label = (attr->hidden1[1] > attr->hidden1[0]) ? 1 : 0;
// This would classify as ATTACK (19569 > -27452)

// Current code (with confidence threshold):
int32_t confidence_threshold = 100000;
int32_t attack_margin = attr->hidden1[1] - attr->hidden1[0];
int label = (attack_margin > confidence_threshold) ? 1 : 0;
// attack_margin = 47021, threshold = 100000
// 47021 < 100000, so classify as BENIGN
```

**Why confidence threshold?**
- Reduces false positives
- Requires neural network to be "sure" before flagging attack
- Your traffic (5 packets, normal HTTP): Too short to be confident it's attack

### Converting to Probabilities (Optional)

If you want to see probabilities, you can calculate them:

**From your scores:**
```python
import math

benign_score = -27452 / 65536  # -0.42
attack_score = 19569 / 65536   # +0.30

benign_exp = math.exp(benign_score)  # e^(-0.42) ≈ 0.657
attack_exp = math.exp(attack_score)  # e^(0.30) ≈ 1.350

total = benign_exp + attack_exp  # 0.657 + 1.350 = 2.007

prob_benign = benign_exp / total  # 0.657 / 2.007 ≈ 0.327 (32.7%)
prob_attack = attack_exp / total  # 1.350 / 2.007 ≈ 0.673 (67.3%)
```

**Interpretation:**
- 67.3% confident it's attack
- Not high enough confidence (threshold requires higher margin)
- Classified as BENIGN (to be safe)

---

## STEP-BY-STEP WORKFLOW DIAGRAM

### Phase 1: Initial Setup (One-Time)

```
┌─────────────────────────────────────────────────────────────┐
│ STEP 1: BUILD DEPENDENCIES                                  │
├─────────────────────────────────────────────────────────────┤
│ $ make  (in project root)                                   │
│   ├─> Builds bpftool                                        │
│   ├─> Builds libbpf                                         │
│   └─> Creates tools for compiling eBPF programs             │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ STEP 2: TRAIN NEURAL NETWORK                                │
├─────────────────────────────────────────────────────────────┤
│ $ cd src                                                     │
│ $ python3 mlp_train.py                                      │
│   ├─> Loads dataset (reproduction-xdp-data.npy)            │
│   ├─> Trains MLP (6→32→32→2) for 32 epochs                │
│   ├─> Tests accuracy on test set                            │
│   └─> Saves model + normalization params to mlp.th         │
│                                                              │
│ OUTPUT: mlp.th (trained model)                              │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ STEP 3: QUANTIZE MODEL (Float → Fixed-Point)               │
├─────────────────────────────────────────────────────────────┤
│ $ python3 mlp_quant.py mlp.th 16                           │
│   ├─> Converts float32 weights to int32 (Q16.16)          │
│   ├─> Rounds mean/scale to int64                           │
│   └─> Generates C header file                              │
│                                                              │
│ OUTPUT: mlp_params.bpf.h                                    │
│   static const int32_t layer_0_weight[192] = {...};        │
│   static const int32_t layer_1_weight[1024] = {...};       │
│   static const int32_t layer_2_weight[64] = {...};         │
│   static const int64_t mean[6] = {...};                    │
│   static const int64_t scale[6] = {...};                   │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ STEP 4: COMPILE eBPF PROGRAMS                               │
├─────────────────────────────────────────────────────────────┤
│ $ cd src                                                     │
│ $ make                                                       │
│   ├─> Generates vmlinux.h (kernel types from BTF)          │
│   ├─> Compiles xdp.bpf.c → xdp.bpf.o (eBPF bytecode)      │
│   ├─> Generates xdp.skel.h (skeleton header)               │
│   ├─> Compiles xdp.c (userspace loader)                    │
│   └─> Compiles hot_updating.c (model updater)              │
│                                                              │
│ OUTPUT: ./src/.output/xdp                                   │
│         ./src/.output/hot_updating                          │
└─────────────────────────────────────────────────────────────┘
```

### Phase 2: Loading and Running

```
┌─────────────────────────────────────────────────────────────┐
│ STEP 5: LOAD XDP PROGRAM TO KERNEL                         │
├─────────────────────────────────────────────────────────────┤
│ $ sudo ./src/.output/xdp [interface]                       │
│                                                              │
│ 5.1) Open skeleton                                          │
│      xdp_bpf__open() → Loads .o file                       │
│                                                              │
│ 5.2) Load eBPF programs to kernel                          │
│      xdp_bpf__load()                                        │
│      ├─> Kernel verifies bytecode (safety checks)          │
│      ├─> JIT compiles to native machine code               │
│      └─> Programs loaded but not active yet                │
│                                                              │
│ 5.3) Populate tail call map (progs)                        │
│      ├─> progs[0] = FD of xdp_preprocessing               │
│      ├─> progs[1] = FD of xdp_input_linear                │
│      ├─> progs[2] = FD of xdp_input_relu                  │
│      ├─> progs[3] = FD of xdp_hidden_linear               │
│      ├─> progs[4] = FD of xdp_hidden_relu                 │
│      └─> progs[5] = FD of xdp_output_linear               │
│                                                              │
│ 5.4) Initialize nn_idx map                                  │
│      nn_idx[0] = 1  (use model slot 1)                     │
│                                                              │
│ 5.5) Attach XDP program to network interface               │
│      bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS)           │
│      ├─> Try native mode (XDP_FLAGS_DRV_MODE)             │
│      └─> Fallback to generic mode (XDP_FLAGS_SKB_MODE)    │
│                                                              │
│ 5.6) Pin maps to filesystem                                │
│      /sys/fs/bpf/nn_parameters (persists after exit)       │
│      /sys/fs/bpf/nn_idx (persists after exit)              │
└─────────────────────────────────────────────────────────────┘
                          │
                    XDP NOW ACTIVE
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│ STEP 6: PACKET PROCESSING (For Every TCP Packet)           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ PACKET ARRIVES at Network Interface                  │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 0] xdp_prog (Entry Point)                     │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Validate packet boundaries                        │   │
│ │ ✓ Check: Is IPv4?  → No: XDP_PASS                  │   │
│ │ ✓ Check: Is TCP?   → No: XDP_PASS                  │   │
│ │ ✓ Extract flow tuple (src_ip, dst_ip, ports)       │   │
│ │ ✓ Check IP filter (if enabled)                      │   │
│ │ ✓ Lookup/create flow in flow_map                    │   │
│ │ ✓ Update statistics:                                │   │
│ │   ├─ num_packet++                                   │   │
│ │   ├─ max_packet_length = max(...)                  │   │
│ │   ├─ min_packet_length = min(...)                  │   │
│ │   ├─ max_duration = max(...)                       │   │
│ │   └─ header_length += tcp->doff * 4                │   │
│ │ ✓ Check: TCP FIN or RST?                           │   │
│ │   → No: XDP_PASS (done for now)                    │   │
│ │   → Yes: Continue to neural network ↓              │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 0)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 1] xdp_preprocessing                          │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Get active model: nn_idx[0] → 1                  │   │
│ │ ✓ Get flow stats from flow_map                     │   │
│ │ ✓ Get NN params: nn_parameters[1]                  │   │
│ │ ✓ Prepare features array:                          │   │
│ │   x[0] = max_packet_length                         │   │
│ │   x[1] = max_duration                              │   │
│ │   x[2] = min_packet_length                         │   │
│ │   x[3] = dst_port                                  │   │
│ │   x[4] = header_length                             │   │
│ │   x[5] = num_packet                                │   │
│ │ ✓ Normalize features:                              │   │
│ │   hidden2[i] = (x[i] - mean[i]) / scale[i] << 16  │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 1)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 2] xdp_input_linear (Layer 0: 6→32)          │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Matrix multiply: hidden1 = layer_0_weight × hidden2│  │
│ │   for (i = 0; i < 32; i++)                         │   │
│ │     for (j = 0; j < 6; j++)                        │   │
│ │       sum += W[i*6+j] * hidden2[j]                 │   │
│ │     hidden1[i] = sum >> 16  (fixed-point adjust)   │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 2)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 3] xdp_input_relu (Activation)               │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Apply ReLU: hidden1[i] = max(0, hidden1[i])     │   │
│ │   (Sets negative values to zero)                    │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 3)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 4] xdp_hidden_linear (Layer 1: 32→32)        │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Matrix multiply: hidden2 = layer_1_weight × hidden1│  │
│ │   for (i = 0; i < 32; i++)                         │   │
│ │     for (j = 0; j < 32; j++)                       │   │
│ │       sum += W[i*32+j] * hidden1[j]                │   │
│ │     hidden2[i] = sum >> 16                         │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 4)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 5] xdp_hidden_relu (Activation)              │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Apply ReLU: hidden2[i] = max(0, hidden2[i])     │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   bpf_tail_call(ctx, &progs, 5)            │
│                          │                                   │
│                          ▼                                   │
│ ┌─────────────────────────────────────────────────────┐   │
│ │ [PROG 6] xdp_output_linear (Layer 2: 32→2)         │   │
│ │ ─────────────────────────────────────────────────── │   │
│ │ ✓ Matrix multiply: hidden1 = layer_2_weight × hidden2│  │
│ │   for (i = 0; i < 2; i++)                          │   │
│ │     for (j = 0; j < 32; j++)                       │   │
│ │       sum += W[i*32+j] * hidden2[j]                │   │
│ │     hidden1[i] = sum >> 16                         │   │
│ │                                                      │   │
│ │ ✓ Make decision:                                    │   │
│ │   benign_score = hidden1[0]                        │   │
│ │   attack_score = hidden1[1]                        │   │
│ │   margin = attack_score - benign_score             │   │
│ │   if (margin > threshold) → ATTACK                 │   │
│ │   else → BENIGN                                    │   │
│ │                                                      │   │
│ │ ✓ Log result to trace_pipe                         │   │
│ │ ✓ return XDP_PASS (or XDP_DROP for attacks)       │   │
│ └─────────────────────────────────────────────────────┘   │
│                          │                                   │
│                   Packet continues to network stack         │
└─────────────────────────────────────────────────────────────┘
```

### Phase 3: Hot Update (Optional)

```
┌─────────────────────────────────────────────────────────────┐
│ STEP 7: HOT UPDATE NEURAL NETWORK (While XDP Running)      │
├─────────────────────────────────────────────────────────────┤
│ Terminal 1: XDP running                                     │
│ $ sudo ./src/.output/xdp                                   │
│ (keeps running, processing packets)                         │
│                                                              │
│ Terminal 2: Train new model                                │
│ $ cd src                                                    │
│ $ python3 mlp_train.py                                     │
│ $ python3 mlp_quant.py mlp.th 16                           │
│ $ make                                                      │
│ $ sudo ./src/.output/hot_updating                          │
│   ├─> Read current nn_idx: 1                              │
│   ├─> Calculate new slot: (1+1) % 2 = 0                   │
│   ├─> Load new weights into nn_parameters[0]              │
│   ├─> Atomically update nn_idx[0] = 0                     │
│   └─> Next packet uses new model!                         │
│                                                              │
│ No downtime, no packet drops                               │
└─────────────────────────────────────────────────────────────┘
```

### Phase 4: Monitoring

```
┌─────────────────────────────────────────────────────────────┐
│ STEP 8: VIEW DEBUG OUTPUT                                   │
├─────────────────────────────────────────────────────────────┤
│ $ sudo cat /sys/kernel/debug/tracing/trace_pipe           │
│                                                              │
│ Shows bpf_printk() output from kernel:                     │
│ - Flow information (IPs, ports)                             │
│ - Feature values (raw and normalized)                       │
│ - Layer outputs                                             │
│ - Final classification (BENIGN / ATTACK)                    │
│ - Performance metrics (nanoseconds)                         │
└─────────────────────────────────────────────────────────────┘
```

---

## YOUR LOG ANALYSIS

Let's break down your curl request logs in detail:

```
irq/160-iwlwifi-525  [001] ..s21  2028.428862: bpf_trace_printk:
==> Packet from monitored IP: 10.127.132.33:45808
```
**Meaning:**
- Packet captured from source IP `10.127.132.33`, port `45808`
- This matches the IP filter (or filter disabled with `if (1)`)
- First packet in the TCP connection

---

```
irq/160-iwlwifi-525  [001] ..s21  2028.429231: bpf_trace_printk:
==> Packet from monitored IP: 10.127.132.33:45808
```
**Meaning:**
- Second packet from same flow
- Flow statistics being updated (packet count, sizes, durations)

---

```
irq/160-iwlwifi-525  [001] ..s21  2028.429251: bpf_trace_printk:
==> Flow ending (FIN/RST) - Starting NN inference...
```
**Meaning:**
- TCP FIN or RST flag detected → Connection closing
- Trigger neural network analysis
- Total flow: 5 packets collected

---

```
==> Packets in flow: 5, Starting preprocessing...
[PREPROCESSING] Flow: 45808->2080, packets=5
```
**Meaning:**
- Flow summary: client port 45808 → server port 2080
- Total 5 packets captured
- Now entering preprocessing (tail call to `xdp_preprocessing`)

---

```
[PREPROCESSING] Features: max_len=134, min_len=32, duration=9022688
[PREPROCESSING] port=2080, hdr_len=168
```
**Meaning:**
- **max_len=134:** Largest packet was 134 bytes
- **min_len=32:** Smallest packet was 32 bytes
- **duration=9022688:** Longest gap between packets: 9,022,688 nanoseconds ≈ 9 milliseconds
- **port=2080:** Destination port 2080 (custom service)
- **hdr_len=168:** Total TCP header bytes: 168 bytes ÷ 5 packets = 33.6 bytes/packet

**Analysis:**
- Small packet sizes (32-134 bytes) → Typical for HTTP request/response
- Short duration (9ms) → Normal interactive traffic
- Port 2080 → Non-standard (maybe dev server?)

---

```
NN params loaded - first weight: 459, mean[0]: 572
```
**Meaning:**
- Neural network parameters successfully loaded from `nn_parameters` map
- `layer_0_weight[0] = 459` (sanity check)
- `mean[0] = 572` (used for normalizing feature 0 = max_packet_length)

---

```
[DEBUG] Raw features BEFORE normalization:
  max_len=134, max_dur=9022688, min_len=32
  dst_port=2080, hdr_len=168, num_pkt=5
```
**Meaning:**
- The 6 raw features extracted from the flow:
  1. max_len = 134
  2. max_dur = 9022688
  3. min_len = 32
  4. dst_port = 2080
  5. hdr_len = 168
  6. num_pkt = 5

---

```
[DEBUG] Normalized features AFTER scaling:
  norm[0]=-63646, norm[1]=-30417, norm[2]=-4854
  norm[3]=62086, norm[4]=-15719, norm[5]=-15711
```
**Meaning:**
- Features converted to Q16.16 fixed-point format
- **norm[0] = -63646:** max_len normalized (134 is below average of 572)
- **norm[1] = -30417:** max_dur normalized (9ms is below average)
- **norm[2] = -4854:** min_len normalized (32 is slightly below average of 36)
- **norm[3] = 62086:** dst_port normalized (2080 is unusual port)
- **norm[4] = -15719:** hdr_len normalized (168 is below average)
- **norm[5] = -15711:** num_pkt normalized (5 packets is below average)

**Convert to float (divide by 65536):**
- norm[0] ≈ -0.97
- norm[1] ≈ -0.46
- norm[2] ≈ -0.07
- norm[3] ≈ +0.95
- norm[4] ≈ -0.24
- norm[5] ≈ -0.24

**All values are reasonable (between -1 and +1), indicating good normalization.**

---

```
[DEBUG] Normalization params:
  mean: 572, 11161066668, 36
  scale: 451, 24027934608, 54
```
**Meaning:**
- **mean[0] = 572:** Average max_packet_length in training data
- **mean[1] = 11161066668:** Average max_duration (11.16 seconds!)
- **mean[2] = 36:** Average min_packet_length
- **scale[0] = 451:** Standard deviation of max_packet_length
- **scale[1] = 24027934608:** Standard deviation of max_duration
- **scale[2] = 54:** Standard deviation of min_packet_length

**Note:** Training data includes slowloris attacks (very long durations), which skews the mean duration to 11 seconds. Your flow (9ms) is much faster, hence the large negative normalized value.

---

```
[PREPROCESSING] Normalization complete, chaining to input layer...
[INPUT LINEAR] Computing layer 0: 6 inputs -> 32 neurons
```
**Meaning:**
- Tail call to `xdp_input_linear`
- Matrix multiplication: 6 features × 192 weights = 32 outputs

---

```
[INPUT RELU] Applying activation function (ReLU)...
[HIDDEN LINEAR] Computing layer 1: 32 -> 32 neurons
[HIDDEN RELU] Applying activation function (ReLU)...
[OUTPUT LINEAR] Computing layer 2: 32 -> 2 outputs (BENIGN vs ATTACK)
```
**Meaning:**
- Neural network forward pass completing
- Each tail call executes successfully
- No errors (good!)

---

```
========================================
*** INTRUSION DETECTION RESULT ***
Flow: 10.127.132.33:45808 -> 10.127.132.177:2080
Packets in flow: 5
```
**Meaning:**
- Final decision ready
- Flow from client (10.127.132.33:45808) to server (10.127.132.177:2080)
- Based on 5 packets

---

```
Score [BENIGN]: -27452
Score [ATTACK]: 19569
```
**Meaning:**
- Raw logit scores (Q16.16 fixed-point)
- **BENIGN score:** -27452 / 65536 ≈ -0.42
- **ATTACK score:** 19569 / 65536 ≈ +0.30

**Interpretation:**
- Network leans slightly towards attack (0.30 > -0.42)
- But confidence is low (small difference)

---

```
Confidence margin: 47021 (threshold: 100000)
```
**Meaning:**
- Margin = 19569 - (-27452) = 47021
- Threshold = 100000 (configurable)
- **47021 < 100000:** Not confident enough to classify as attack

**Why confidence threshold?**
- Reduces false positives
- Only flag attacks when neural network is very confident
- Your flow (5 packets, normal HTTP) is ambiguous → Default to BENIGN

---

```
Classification: BENIGN (Normal Traffic)
```
**Decision:** Flow classified as BENIGN

**Why?**
- Short flow (5 packets) → Not enough data for high confidence
- Fast duration (9ms) → Not a slow attack
- Small packets (32-134 bytes) → Typical HTTP
- Attack score higher, but not by enough

**This is correct! Your curl request is legitimate traffic.**

---

```
Avg feature extraction: 1784 ns
Total detection time: 61086 ns
```
**Performance:**
- **Feature extraction:** 1.78 microseconds per packet (very fast!)
- **Total detection:** 61 microseconds for entire neural network
- **Overhead:** Minimal impact on throughput

**For comparison:**
- Network latency: ~10-100 milliseconds
- 61 microseconds = 0.061 milliseconds (negligible!)

---

### Summary of Your Log

**Your curl request:**
- 5 packets
- 32-134 bytes per packet
- 9ms maximum duration
- Port 2080 (custom service)

**Neural network decision:**
- Slightly suspicious (attack score > benign score)
- But not confident enough (margin 47021 < threshold 100000)
- **Final: BENIGN** (correct classification)

**Performance:**
- Feature extraction: 1.78 μs per packet
- Neural network inference: 61 μs total
- Extremely fast (no noticeable impact)

---

## CONCLUSION

You now have a complete understanding of how NN-eBPF works:

1. **ML Algorithm:** Multi-Layer Perceptron (6→32→32→2)
2. **Training:** Python scripts train on labeled traffic dataset
3. **Weights:** Learned parameters that encode attack patterns
4. **Normalization:** Converts features to uniform scale using mean/scale
5. **Python files:** Train model, convert to fixed-point, generate C headers
6. **Tail calls:** Chain eBPF programs to avoid instruction limits
7. **Hot updating:** Swap models without restarting (uses dual-slot design)
8. **eBPF maps:** Store flow stats, NN parameters, program FDs
9. **Features:** 6 traffic statistics per flow (same in Python and C)
10. **ReLU:** Activation function that adds non-linearity
11. **Scores:** Raw logits (not probabilities), compared directly
12. **Workflow:** Train → Quantize → Compile → Load → Monitor → (Optional) Hot Update

The system runs a neural network entirely in the Linux kernel, making real-time decisions on every TCP connection in under 100 microseconds. It's a fascinating blend of machine learning and low-level systems programming!
