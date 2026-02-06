Ggg# NN-eBPF Test Cases

This document provides test cases for validating the intrusion detection system and threshold tuning.

## Prerequisites
0. if want train the model again
```sh
   source .venv/bin/activate  # -> activate libarariesrun in root
   python3 mlp_train.py       # -> train + normalize -> mlp.th output
   python3 mlp_quant.py mlp.th 16
```

1. XDP program must be running:
   ```bash
   sudo ./src/.output/xdp wlan0
   sudo ./src/.output/hot_updating # -> register initial weights
   ```

2. Monitor detection logs in separate terminal:
   ```bash
   sudo cat /sys/kernel/debug/tracing/trace_pipe
   ```

3. (Optional) Filter logs for results only:
   ```bash
   sudo cat /sys/kernel/debug/tracing/trace_pipe | grep -E "INTRUSION|ATTACK|BENIGN|Threshold|margin"
   ```

---

## Test Case Categories

### 1. NORMAL TRAFFIC (Should be classified as BENIGN)

#### Test 1.1: Simple HTTP Request
**Purpose:** Baseline test for normal web traffic

```bash
# Simple GET request to a web server
curl http://example.com

# Alternative: Local test (requires web server running)
curl http://localhost:80

# With explicit connection close
curl -H "Connection: close" http://example.com
```

**Expected Result:**
- Classification: BENIGN
- Attack probability: < 55%
- Confidence margin: < 150,000 (default threshold)

---

#### Test 1.2: HTTP with Large Response
**Purpose:** Test normal traffic with larger data transfer

```bash
# Download a large file
curl -o /dev/null http://example.com/large-file.zip

# Test with streaming data
curl http://example.com/api/data
```

**Expected Result:**
- Classification: BENIGN
- Margin should be negative or low positive

---

#### Test 1.3: HTTPS Traffic
**Purpose:** Validate TLS/SSL encrypted normal traffic

```bash
# HTTPS GET request
curl https://example.com

# With verbose output
curl -v https://www.google.com
```

**Expected Result:**
- Classification: BENIGN
- Low confidence margin

---

#### Test 1.4: Multiple Sequential Requests
**Purpose:** Test multiple legitimate connections

```bash
# Send 10 normal requests
for i in {1..10}; do
    curl http://example.com
    sleep 1
done
```

**Expected Result:**
- All flows classified as BENIGN
- Consistent low margins

---

#### Test 1.5: Normal SSH Session
**Purpose:** Validate SSH traffic is recognized as benign

```bash
# SSH connection and command execution
ssh user@remote-host "ls -la"

# Keep-alive session
ssh -o ServerAliveInterval=5 user@remote-host
```

**Expected Result:**
- Classification: BENIGN
- SSH typically has small packets with regular intervals

---

#### Test 1.6: FTP Transfer
**Purpose:** Validate FTP protocol traffic

```bash
# Upload file via FTP
curl -T local-file.txt ftp://ftp.example.com --user username:password

# Download via FTP
curl ftp://ftp.example.com/file.txt --user username:password
```

**Expected Result:**
- Classification: BENIGN
- Data connection should show normal traffic patterns

---

#### Test 1.7: DNS Queries
**Purpose:** Validate DNS traffic (if monitoring DNS)

```bash
# Multiple DNS lookups
for domain in google.com facebook.com github.com; do
    nslookup $domain
    sleep 0.5
done
```

**Expected Result:**
- Classification: BENIGN
- Very small packets, short duration

---

### 2. PORT SCANNING ATTACKS (Should be DETECTED)

#### Test 2.1: Basic Port Scan with Nmap
**Purpose:** Detect reconnaissance via port scanning

```bash
# TCP SYN scan (requires root)
sudo nmap -sS -p 1-1000 target-ip

# TCP connect scan (no root needed)
nmap -sT -p 1-100 target-ip

# Fast scan (top 100 ports)
nmap -F target-ip
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Attack probability: > 85%
- Confidence margin: > 200,000
- Reason: Many connections to different ports, minimal data per flow

---

#### Test 2.2: Aggressive Nmap Scan
**Purpose:** Test detection of intensive reconnaissance

```bash
# Aggressive scan with OS detection
sudo nmap -A -T4 target-ip

# Version detection scan
nmap -sV -p 1-1000 target-ip

# All TCP ports
nmap -p- target-ip
```

**Expected Result:**
- Classification: ATTACK DETECTED
- High confidence (margin > 500,000)
- Very suspicious packet patterns

---

#### Test 2.3: Stealth Scan
**Purpose:** Detect stealthy reconnaissance attempts

```bash
# SYN stealth scan
sudo nmap -sS -T2 -p 1-1000 target-ip

# FIN scan (avoids some firewalls)
sudo nmap -sF -p 80,443,22 target-ip

# NULL scan
sudo nmap -sN target-ip
```

**Expected Result:**
- Classification: ATTACK DETECTED
- May have lower margin than aggressive s/cans
- Multiple incomplete connections

---

#### Test 2.4: UDP Port Scan
**Purpose:** Validate UDP scanning detection

```bash
# UDP scan
sudo nmap -sU -p 53,161,1900 target-ip
```

**Expected Result:**
- Depends on whether UDP is monitored (current system is TCP-only)
- TCP-only systems will not detect this

---

### 3. DENIAL OF SERVICE ATTACKS (Should be DETECTED)

#### Test 3.1: Slowloris Attack
**Purpose:** Detect slow HTTP DoS attack

```bash
# Using slowloris tool (install: pip install slowloris)
slowloris target-ip -p 80 -s 200

# Alternative with hping3
sudo hping3 -S -p 80 --flood --rand-source target-ip
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Attack probability: > 90%
- Reason: Many connections with very long durations and small packets

---

#### Test 3.2: SYN Flood Attack
**Purpose:** Detect TCP SYN flooding

```bash
# Using hping3 for SYN flood
sudo hping3 -S -p 80 --flood target-ip

# With random source IPs
sudo hping3 -S -p 80 --flood --rand-source target-ip

# Slower rate (1000 packets/sec)
sudo hping3 -S -p 80 -i u1000 target-ip
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Very high packet count, short duration
- Margin >> 1,000,000

---

#### Test 3.3: HTTP Flood
**Purpose:** Detect application-layer DoS

```bash
# Using ab (Apache Bench) - high request rate
ab -n 10000 -c 100 http://target-ip/

# Using curl in loop (simpler)
for i in {1..1000}; do curl http://target-ip/ & done; wait

# Using siege tool
siege -c 100 -t 60S http://target-ip/

# Using wrk
wrk -t4 -c100 -d30s http://192.168.70.191:2080/
```

**Expected Result:**
- Classification: ATTACK DETECTED
- High number of connections in short time
- Different from normal browsing patterns

---

#### Test 3.4: Connection Exhaustion
**Purpose:** Test detection of connection exhaustion attack

```bash
# Open many connections and hold them
for i in {1..100}; do
    (exec 3<>/dev/tcp/target-ip/80; sleep 300; exec 3>&-) &
done
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Long-lived connections with minimal data transfer

---

### 4. WEB APPLICATION ATTACKS (May be DETECTED)

#### Test 4.1: SQL Injection Attempts
**Purpose:** Detect malicious SQL injection traffic patterns

```bash
# SQL injection attempts via curl
curl "http://target-ip/page.php?id=1' OR '1'='1"
curl "http://target-ip/login.php?user=admin'--&pass=x"

# Using sqlmap (automated tool)
sqlmap -u "http://target-ip/page.php?id=1" --batch
```

**Expected Result:**
- May or may not be detected (depends on traffic pattern)
- Single requests look like normal HTTP
- Repeated attempts may trigger detection

---

#### Test 4.2: Cross-Site Scripting (XSS)
**Purpose:** Test XSS payload delivery detection

```bash
# XSS attempts
curl "http://target-ip/search.php?q=<script>alert('XSS')</script>"
curl "http://target-ip/comment.php" -d "text=<img src=x onerror=alert('XSS')>"
```

**Expected Result:**
- Likely classified as BENIGN (payload in data, not traffic pattern)
- NN-eBPF focuses on traffic patterns, not payload content

---

#### Test 4.3: Directory Brute-Force
**Purpose:** Detect web directory enumeration

```bash
# Using gobuster
gobuster dir -u http://target-ip -w /usr/share/wordlists/dirb/common.txt

# Using dirbuster or manual curl loop
for path in admin backup test config; do
    curl http://target-ip/$path
done
```

**Expected Result:**
- Classification: Likely ATTACK
- Many rapid requests to different URLs
- Pattern similar to port scanning

---

### 5. CREDENTIAL ATTACKS (Should be DETECTED)

#### Test 5.1: SSH Brute Force
**Purpose:** Detect SSH credential stuffing

```bash
# Using hydra
hydra -l admin -P /usr/share/wordlists/rockyou.txt ssh://target-ip

# Manual attempts
for pass in password admin 123456; do
    sshpass -p "$pass" ssh -o StrictHostKeyChecking=no user@target-ip 2>/dev/null
done
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Many failed connection attempts
- Short connections with authentication failures

---

#### Test 5.2: HTTP Basic Auth Brute Force
**Purpose:** Detect HTTP authentication attacks

```bash
# Using hydra for HTTP
hydra -l admin -P passwords.txt target-ip http-get /admin

# Using curl loop
for pass in password admin test123; do
    curl -u admin:$pass http://target-ip/admin
done
```

**Expected Result:**
- Classification: ATTACK DETECTED
- Rapid successive requests with 401 responses

---

### 6. MIXED TRAFFIC SCENARIOS

#### Test 6.1: Normal Traffic During Attack
**Purpose:** Validate system can distinguish concurrent traffic

```bash
# Terminal 1: Normal browsing
curl http://example.com

# Terminal 2: Port scan
nmap -sS -p 1-1000 target-ip

# Terminal 3: Normal SSH
ssh user@legitimate-host
```

**Expected Result:**
- Port scan flows: ATTACK DETECTED
- curl/SSH flows: BENIGN
- Each flow classified independently

---

#### Test 6.2: False Positive Testing
**Purpose:** Find edge cases that trigger false positives

```bash
# Large legitimate file download (may look unusual)
curl -o /dev/null http://cdn.example.com/large-video.mp4

# Automated legitimate traffic (CI/CD, monitoring)
for i in {1..100}; do curl http://api.example.com/health; sleep 1; done

# VPN or tunnel traffic (if tunneling TCP over TCP)
# (Requires VPN client)
```

**Expected Result:**
- Should be BENIGN
- Use to tune threshold if false positives occur

---

## Threshold Tuning Guide

### Finding the Right Threshold

1. **Collect Baseline Data:**
   ```bash
   # Run normal traffic tests and record margins
   curl http://example.com
   # Check trace_pipe for: "Confidence margin: <value>"
   ```

2. **Collect Attack Data:**
   ```bash
   # Run attack tests and record margins
   nmap -sS -p 1-1000 target-ip
   # Check trace_pipe for: "Confidence margin: <value>"
   ```

3. **Calculate Threshold:**
   ```
   threshold = max(benign_margins) + safety_buffer

   Example:
   - Max benign margin: 113,000
   - Min attack margin: 1,991,000
   - Good threshold: 150,000 (between them)
   ```

### Testing Different Thresholds

```bash
# Start with default
sudo ./src/.output/xdp wlan0

# Run test traffic, then adjust
sudo ./src/.output/hot_threshold 100000  # More sensitive
# Run tests again, check results

sudo ./src/.output/hot_threshold 200000  # Less sensitive
# Run tests again, check results

# Find sweet spot that minimizes false positives AND false negatives
```

### Threshold Recommendations by Use Case

**High Security Environment (DMZ, Production Servers):**
```bash
sudo ./src/.output/hot_threshold 100000
```
- Catches more attacks, may have false positives
- Review logs frequently

**Balanced (Recommended Default):**
```bash
sudo ./src/.output/hot_threshold 150000
```
- Good balance of detection and false positive rate

**Low Noise (Development, Internal Network):**
```bash
sudo ./src/.output/hot_threshold 200000
```
- Fewer false positives, may miss subtle attacks
- Good for environments with trusted users

**Very Conservative (Testing/Research):**
```bash
sudo ./src/.output/hot_threshold 500000
```
- Only extremely obvious attacks
- Use when learning system behavior

---

## Interpreting Results

### Log Output Explanation

```
*** INTRUSION DETECTION RESULT ***
Flow: 192.168.1.100:54321 -> 93.184.216.34:80
Packets in flow: 42
Score [BENIGN]: -123456
Score [ATTACK]: 234567
Confidence margin: 358023 (threshold: 150000)
Attack probability: ~85%
Confidence: HIGH
Classification: *** ATTACK DETECTED ***
```

**What each field means:**
- **Packets in flow:** Total TCP packets in this connection
- **Score [BENIGN/ATTACK]:** Raw neural network outputs (logits)
- **Confidence margin:** `ATTACK - BENIGN` (decision metric)
- **Threshold:** Current detection threshold (from threshold_map)
- **Attack probability:** Approximate % (not exact softmax)
- **Classification:** Final decision (margin > threshold = ATTACK)

### Performance Metrics

```
Avg feature extraction: 1234 ns
Total detection time: 56789 ns
```

- **Feature extraction:** Time per packet to update flow statistics
- **Detection time:** Time from FIN/RST to classification result
- Lower is better (typically µs to low ms range)

---

## Automated Test Script

Create `test_suite.sh`:

```bash
#!/bin/bash

# Automated test suite for NN-eBPF
# Usage: sudo ./test_suite.sh <target-ip>

TARGET=$1

if [ -z "$TARGET" ]; then
    echo "Usage: $0 <target-ip>"
    exit 1
fi

echo "=== NN-eBPF Automated Test Suite ==="
echo "Target: $TARGET"
echo ""

# Test 1: Normal traffic
echo "[1/5] Testing normal HTTP request..."
curl -s http://$TARGET > /dev/null
sleep 2

# Test 2: Port scan
echo "[2/5] Testing port scan (should detect)..."
nmap -sS -p 1-100 $TARGET > /dev/null 2>&1
sleep 2

# Test 3: SYN flood (short burst)
echo "[3/5] Testing SYN flood (should detect)..."
timeout 5 sudo hping3 -S -p 80 --flood $TARGET > /dev/null 2>&1
sleep 2

# Test 4: Multiple normal requests
echo "[4/5] Testing multiple normal requests..."
for i in {1..5}; do curl -s http://$TARGET > /dev/null; sleep 1; done
sleep 2

# Test 5: Aggressive scan
echo "[5/5] Testing aggressive scan (should detect)..."
nmap -A -T4 -p 80,443 $TARGET > /dev/null 2>&1
sleep 2

echo ""
echo "=== Test Suite Complete ==="
echo "Check trace_pipe output for results:"
echo "  sudo cat /sys/kernel/debug/tracing/trace_pipe | grep 'Classification'"
```

---

## Troubleshooting

### Issue: All traffic detected as BENIGN
**Solution:** Lower the threshold
```bash
sudo ./src/.output/hot_threshold 50000
```

### Issue: Too many false positives
**Solution:** Raise the threshold
```bash
sudo ./src/.output/hot_threshold 250000
```

### Issue: No detection results in trace_pipe
**Possible causes:**
1. Source IP filter enabled (check xdp.bpf.c:128)
2. Traffic not TCP (XDP only monitors TCP)
3. No FIN/RST received (connection still open)

### Issue: Threshold update not taking effect
**Solution:**
1. Verify XDP is running: `ps aux | grep xdp`
2. Check map exists: `ls /sys/fs/bpf/threshold_map`
3. Check logs: `cat /tmp/threshold_updates.log`

---

## Advanced Testing

### Statistical Validation

Run each test 10+ times and calculate:
- **True Positive Rate (TPR):** Attacks correctly detected / Total attacks
- **False Positive Rate (FPR):** Benign traffic flagged as attack / Total benign
- **Accuracy:** (TP + TN) / Total tests

Target metrics:
- TPR > 95% (catch most attacks)
- FPR < 5% (few false alarms)

### Load Testing

```bash
# Simulate realistic load
ab -n 10000 -c 50 http://target-ip/  # 50 concurrent connections
# Verify system maintains performance under load
```

### Long-Duration Testing

```bash
# Leave XDP running for hours/days
# Monitor for:
# - Memory leaks (check flow_map size)
# - Performance degradation
# - Threshold stability
```

---

## Notes

- Always run tests in a controlled environment (your own servers)
- Some attacks (SYN flood, port scans) may be illegal against others' systems
- Adjust test parameters based on your network capacity
- Document threshold changes and results for future reference

**Last Updated:** 2026-02-06
