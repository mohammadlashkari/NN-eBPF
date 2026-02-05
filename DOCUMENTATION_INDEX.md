# NN-eBPF Documentation Index

## 📚 Documentation Overview

I've created comprehensive documentation that answers all your questions about how this neural network intrusion detection system works.

### 📖 Documentation Files

1. **[WORKFLOW.md](./WORKFLOW.md)** - **START HERE!** (17,000+ words)
   - Complete explanation of how everything works
   - Answers ALL your questions in detail
   - Machine learning algorithm explained
   - Training process step-by-step
   - Normalization explained with examples
   - Python files explained
   - Tail calls explained
   - Hot updating mechanism
   - eBPF maps deep dive
   - Flow and features breakdown
   - Retraining guide
   - ReLU activation function
   - Score interpretation
   - **Your log analysis with detailed breakdown**
   - Full workflow diagram from start to finish

2. **[ARCHITECTURE.md](./ARCHITECTURE.md)** - Visual Reference
   - Neural network architecture diagram
   - eBPF map layout visualization
   - Packet processing flow chart
   - Fixed-point arithmetic explained
   - Hot update mechanism diagram
   - Training pipeline visualization

3. **[QUICK_REFERENCE.md](./QUICK_REFERENCE.md)** - Quick Commands
   - Common commands and operations
   - File structure overview
   - Troubleshooting guide
   - Performance tuning tips
   - FAQ section

4. **[CLAUDE.md](./CLAUDE.md)** - Project Instructions
   - Original project overview
   - Build commands
   - Code architecture summary

---

## ✅ Your Questions Answered

Here's where to find answers to each of your questions:

### 1. **Explain the ML algorithm**
→ **WORKFLOW.md** - Section: "MACHINE LEARNING ALGORITHM"
- Multi-Layer Perceptron (MLP) architecture: 6→32→32→2
- Layer breakdown with visualization
- Why MLP is used for this application

### 2. **What are the weights?**
→ **WORKFLOW.md** - Section: "TRAINING PROCESS" → "Step 4: Train Neural Network"
- Weights are learned parameters (1,280 total)
- Start random, get adjusted during training
- Encode "knowledge" of what attacks look like

### 3. **How the model is trained?**
→ **WORKFLOW.md** - Section: "TRAINING PROCESS"
- Complete step-by-step breakdown
- Data loading, splitting, normalization
- Training for 32 epochs with Adam optimizer
- Testing and accuracy metrics

### 4. **How does normalization work? What does it do?**
→ **WORKFLOW.md** - Section: "NORMALIZATION EXPLAINED"
- StandardScaler formula: `(x - mean) / scale`
- Why it's necessary (uniform feature scales)
- Fixed-point representation (Q16.16)
- Example with your actual log values

### 5. **Explain the Python files - what do they do?**
→ **WORKFLOW.md** - Section: "PYTHON FILES EXPLAINED"
- `mlp.py`: Neural network definition
- `mlp_train.py`: Training script
- `mlp_quant.py`: Float to fixed-point conversion
- When to run each file

### 6. **What is the tail call?**
→ **WORKFLOW.md** - Section: "TAIL CALLS EXPLAINED"
- Jump to another eBPF program (never returns)
- Why needed: eBPF instruction limit
- The 6-program chain diagram
- Implementation details

### 7. **What does hot_updating do? How frequently? During runtime?**
→ **WORKFLOW.md** - Section: "HOT UPDATING"
- Swaps neural network models without restart
- Uses dual-slot design (slot 0 and slot 1)
- **YES, runs while XDP is running** (zero downtime)
- Frequency: On-demand or daily (your choice)
- Complete step-by-step process

### 8. **What's stored in eBPF maps? What is hidden[0] and hidden[1]? What is nn_index?**
→ **WORKFLOW.md** - Section: "eBPF MAPS"
- 4 maps explained: flow_map, nn_parameters, nn_idx, progs
- `hidden1[32]`: Layer outputs / final scores
- `hidden2[32]`: Normalized features / layer outputs
- `nn_idx`: Tracks which model is active (0 or 1)
- Complete data structure breakdown

### 9. **What are flow and feature_flow? Features in ML vs C code?**
→ **WORKFLOW.md** - Section: "FLOW & FEATURES"
- Flow: TCP connection identified by 5-tuple
- 6 features used (same in Python and C!)
- Table showing all features with examples
- How features are collected per packet

### 10. **Can I train the model again? Can I change params?**
→ **WORKFLOW.md** - Section: "RETRAINING THE MODEL"
- **YES to both!**
- Option 1: Use existing dataset (quick)
- Option 2: Collect your own data (recommended)
- Changing architecture guide
- Step-by-step instructions

### 11. **What is the activation layer name? What does ReLU do?**
→ **WORKFLOW.md** - Section: "ACTIVATION FUNCTIONS (ReLU)"
- Name: **ReLU** (Rectified Linear Unit)
- Formula: `f(x) = max(0, x)`
- Introduces non-linearity
- Why it's needed (prevents layer collapse)
- Implementation in C

### 12. **Score should be [0,1] but logs show different values - explain**
→ **WORKFLOW.md** - Section: "UNDERSTANDING SCORES"
- **Scores are LOGITS, not probabilities!**
- Can be any value (-∞ to +∞)
- Why eBPF uses logits (softmax is expensive)
- Your log values converted to float
- How to convert to probabilities (optional)

### 13. **Explain the logs**
→ **WORKFLOW.md** - Section: "YOUR LOG ANALYSIS"
- **Line-by-line breakdown of your entire log**
- Every value explained in detail
- What each number means
- Why classified as BENIGN
- Performance metrics explained

### 14. **Step-by-step diagram from loading to running**
→ **WORKFLOW.md** - Section: "STEP-BY-STEP WORKFLOW DIAGRAM"
- Phase 1: Initial Setup (train, quantize, compile)
- Phase 2: Loading and Running (6-step process)
- Phase 3: Hot Update (optional)
- Phase 4: Monitoring
- Complete ASCII diagrams with annotations

---

## 🎯 Quick Start

### If you want to understand the system:
1. Read **WORKFLOW.md** from top to bottom
2. Reference **ARCHITECTURE.md** for visual diagrams
3. Use **QUICK_REFERENCE.md** for commands

### If you want to run it:
```bash
# Build and train (first time)
make
cd src
python3 mlp_train.py
python3 mlp_quant.py mlp.th 16
make
sudo ./xdp wlan0

# View output
sudo cat /sys/kernel/debug/tracing/trace_pipe
```

### If you want to modify it:
- **Change architecture:** See WORKFLOW.md → "Retraining the Model" → "Changing Architecture"
- **Change threshold:** See QUICK_REFERENCE.md → "Adjusting Confidence Threshold"
- **Add features:** See QUICK_REFERENCE.md → "Custom Attack Detection"

---

## 🔍 Key Insights from Your Logs

Your curl request generated this flow:
- **5 packets**, 32-134 bytes each
- **9ms** max duration (very fast - normal)
- **Port 2080** (custom service)
- **Scores:** BENIGN=-27452, ATTACK=19569 (in Q16.16 format)
- **Decision:** BENIGN (correct! It was legitimate traffic)
- **Performance:** 61 microseconds total (extremely fast)

The neural network slightly favored "attack" but not confidently enough (margin 47,021 < threshold 100,000), so it defaulted to BENIGN. This is good - it means the confidence threshold is working to reduce false positives.

---

## 📊 System Summary

**What it does:**
- Runs neural network in Linux kernel (XDP/eBPF)
- Analyzes every TCP connection in real-time
- Classifies as BENIGN or ATTACK
- Makes decision in ~60 microseconds
- Zero network stack overhead

**Architecture:**
- 6 input features → 32 neurons → 32 neurons → 2 outputs
- 1,280 weight parameters + 12 normalization params
- Split into 6 eBPF programs via tail calls
- Fixed-point arithmetic (Q16.16 format)

**Performance:**
- Feature extraction: ~2 μs per packet
- NN inference: ~60 μs per flow
- Negligible overhead (<0.1% for 1 Gbps)

**Capabilities:**
- Hot model updates (zero downtime)
- Detects: Slowloris, DDoS, port scans, injection attacks
- Handles: 8,192 concurrent flows
- Supports: IPv4 TCP (WiFi and Ethernet)

---

## 🛠️ File Structure

```
src/
├── xdp.bpf.c          ← 6 eBPF programs (kernel space) ⭐
├── xdp.c              ← Userspace loader
├── hot_updating.c     ← Model swap utility
├── common.h           ← Data structures
├── params.bpf.h       ← NN map definitions
├── handler.bpf.h      ← Flow tracking
├── mlp.bpf.h          ← NN math (linear, relu, normalize)
├── mlp.py             ← NN architecture ⭐
├── mlp_train.py       ← Training script ⭐
├── mlp_quant.py       ← Quantization script ⭐
└── mlp_params.bpf.h   ← Generated weights (after quantize)
```

⭐ = Main files you'll interact with

---

## 🎓 Learning Path

**Beginner** → Read WORKFLOW.md sections:
1. High-Level Overview
2. Machine Learning Algorithm
3. Flow & Features
4. Understanding Scores

**Intermediate** → Read WORKFLOW.md sections:
1. Training Process
2. Normalization Explained
3. Python Files Explained
4. Your Log Analysis

**Advanced** → Read WORKFLOW.md sections:
1. Tail Calls Explained
2. eBPF Maps
3. Hot Updating
4. Retraining the Model

**Expert** → Study:
1. Source code with comments in xdp.bpf.c
2. ARCHITECTURE.md diagrams
3. Fixed-point arithmetic details
4. Modify architecture and retrain

---

## 💡 Tips

1. **Always read WORKFLOW.md first** - it answers everything
2. **Your logs are explained in detail** - see "YOUR LOG ANALYSIS" section
3. **To monitor all traffic** - change `if (1)` in xdp.bpf.c:128
4. **To block attacks** - change `XDP_PASS` to conditional `XDP_DROP`
5. **Test before production** - false positives can break legitimate traffic

---

## 🤝 Contributing

To improve the model:
1. Collect more diverse traffic (benign + attacks)
2. Update dataset files
3. Retrain: `python3 mlp_train.py`
4. Quantize: `python3 mlp_quant.py mlp.th 16`
5. Test accuracy before deploying

---

## 📝 Notes

- All documentation created by Claude Code
- Comprehensive coverage of ML + eBPF concepts
- Includes your actual log analysis
- Diagrams use ASCII art for terminal viewing
- Code comments added to key files

**Estimated reading time:**
- WORKFLOW.md: 45-60 minutes (comprehensive)
- ARCHITECTURE.md: 15-20 minutes (visual)
- QUICK_REFERENCE.md: 10-15 minutes (practical)

---

## 🚀 Next Steps

1. **Read WORKFLOW.md** (start to finish)
2. **Run the system** (follow commands in QUICK_REFERENCE.md)
3. **Generate traffic** (curl, wget, browse)
4. **Watch the logs** (sudo cat /sys/kernel/debug/tracing/trace_pipe)
5. **Experiment** (change threshold, retrain, modify architecture)

Happy learning! 🎉

---

*Documentation generated: 2026-02-05*
*System: NN-eBPF Intrusion Detection*
*Author: Claude Code (Anthropic)*
