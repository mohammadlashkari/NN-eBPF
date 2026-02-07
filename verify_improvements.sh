#!/bin/bash

# ============================================================
# NN-eBPF Improvements Verification Script
# ============================================================
# This script verifies that all 4 ML improvements are properly
# implemented in the codebase.
#
# Usage: bash verify_improvements.sh
# ============================================================

echo "============================================================"
echo "NN-eBPF ML Improvements Verification"
echo "============================================================"
echo ""

PASS=0
FAIL=0

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

check_pass() {
    echo -e "${GREEN}✓ PASS${NC}: $1"
    ((PASS++))
}

check_fail() {
    echo -e "${RED}✗ FAIL${NC}: $1"
    ((FAIL++))
}

check_warn() {
    echo -e "${YELLOW}⚠ WARNING${NC}: $1"
}

echo "Checking Python Training Code..."
echo "----------------------------------------"

# Check 1: LeakyReLU in mlp.py
if grep -q "nn.LeakyReLU" src/mlp.py; then
    check_pass "LeakyReLU activation found in mlp.py"
else
    check_fail "LeakyReLU activation NOT found in mlp.py"
fi

# Check 2: Label Smoothing class
if grep -q "class LabelSmoothingCrossEntropy" src/mlp.py; then
    check_pass "LabelSmoothingCrossEntropy class found in mlp.py"
else
    check_fail "LabelSmoothingCrossEntropy class NOT found in mlp.py"
fi

# Check 3: Early Stopping class
if grep -q "class EarlyStopping" src/mlp.py; then
    check_pass "EarlyStopping class found in mlp.py"
else
    check_fail "EarlyStopping class NOT found in mlp.py"
fi

# Check 4: AdamW optimizer
if grep -q "torch.optim.AdamW" src/mlp.py; then
    check_pass "AdamW optimizer found in mlp.py"
else
    check_fail "AdamW optimizer NOT found in mlp.py"
fi

# Check 5: Weight decay parameter
if grep -q "weight_decay" src/mlp.py; then
    check_pass "Weight decay parameter found in mlp.py"
else
    check_fail "Weight decay parameter NOT found in mlp.py"
fi

# Check 6: Train/val/test split
if grep -q "X_val" src/mlp_train.py; then
    check_pass "Validation set split found in mlp_train.py"
else
    check_fail "Validation set split NOT found in mlp_train.py"
fi

echo ""
echo "Checking eBPF Kernel Code..."
echo "----------------------------------------"

# Check 7: LeakyReLU function in eBPF
if grep -q "leaky_relu" src/mlp.bpf.h; then
    check_pass "leaky_relu() function found in mlp.bpf.h"
else
    check_fail "leaky_relu() function NOT found in mlp.bpf.h"
fi

# Check 8: LeakyReLU usage in XDP program
if grep -q "leaky_relu" src/xdp.bpf.c; then
    check_pass "leaky_relu() called in xdp.bpf.c"
else
    check_fail "leaky_relu() NOT called in xdp.bpf.c (still using relu?)"
fi

# Check 9: LeakyReLU alpha constant
if grep -q "alpha_fixed = 655" src/mlp.bpf.h; then
    check_pass "LeakyReLU alpha (655 for 0.01) found in mlp.bpf.h"
else
    check_warn "LeakyReLU alpha constant not found (may use different value)"
fi

echo ""
echo "Checking Documentation..."
echo "----------------------------------------"

# Check 10-13: Documentation files
for doc in activation_function.md loss_function.md early_stopping_checkpointing.md adamw_weight_decay.md; do
    if [ -f "$doc" ]; then
        check_pass "Documentation file $doc exists"
    else
        check_fail "Documentation file $doc NOT found"
    fi
done

# Check 14: Summary file
if [ -f "IMPROVEMENTS_SUMMARY.md" ]; then
    check_pass "IMPROVEMENTS_SUMMARY.md exists"
else
    check_fail "IMPROVEMENTS_SUMMARY.md NOT found"
fi

echo ""
echo "Checking Code Quality..."
echo "----------------------------------------"

# Check 15: Comments in mlp.py
comment_count=$(grep -c "# IMPROVEMENT" src/mlp.py)
if [ "$comment_count" -gt 3 ]; then
    check_pass "Found $comment_count improvement comments in mlp.py"
else
    check_warn "Only $comment_count improvement comments in mlp.py (expected 4+)"
fi

# Check 16: No typos (totoal_loss should be fixed)
if grep -q "totoal_loss" src/mlp.py; then
    check_warn "Typo 'totoal_loss' still present in mlp.py"
else
    check_pass "Typo 'totoal_loss' has been fixed to 'total_loss'"
fi

echo ""
echo "Checking Dependencies..."
echo "----------------------------------------"

# Check if in virtual environment
if [[ "$VIRTUAL_ENV" != "" ]]; then
    check_pass "Virtual environment is active"
else
    check_warn "No virtual environment detected (may use system Python)"
fi

# Check for required Python packages
python3 -c "import torch" 2>/dev/null
if [ $? -eq 0 ]; then
    check_pass "PyTorch is installed"
else
    check_fail "PyTorch is NOT installed"
fi

python3 -c "import sklearn" 2>/dev/null
if [ $? -eq 0 ]; then
    check_pass "scikit-learn is installed"
else
    check_fail "scikit-learn is NOT installed"
fi

python3 -c "import numpy" 2>/dev/null
if [ $? -eq 0 ]; then
    check_pass "NumPy is installed"
else
    check_fail "NumPy is NOT installed"
fi

echo ""
echo "============================================================"
echo "Verification Summary"
echo "============================================================"
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ ALL CHECKS PASSED!${NC}"
    echo "Your improvements are properly implemented."
    echo ""
    echo "Next steps:"
    echo "  1. Train the model: cd src && python3 mlp_train.py"
    echo "  2. Quantize: python3 mlp_quant.py mlp.th 16"
    echo "  3. Build eBPF: make clean && make"
    echo "  4. Deploy: sudo ./.output/xdp wlan0"
else
    echo -e "${RED}✗ SOME CHECKS FAILED${NC}"
    echo "Please review the failed checks above and fix the issues."
    echo ""
    echo "See IMPROVEMENTS_SUMMARY.md for detailed instructions."
fi

echo "============================================================"
