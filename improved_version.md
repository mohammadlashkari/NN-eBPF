# ML Improvements for NN-eBPF Project

This document lists all possible machine learning improvements that can be implemented **WITHOUT changing the flow attributes** (keeping the same 6 features). This ensures we can use the existing dataset.

## Current Baseline
- **Architecture**: 3-layer MLP (6→32→32→2), no bias, ReLU activation
- **Training**: Adam optimizer, lr=1e-3, batch_size=512, epochs=32
- **Quantization**: Q16.16 fixed-point (enlargement factor s=2^16)
- **Normalization**: StandardScaler (z-score normalization)
- **Loss**: CrossEntropyLoss
- **No regularization, no validation split, no learning rate scheduling**
- **Performance**: F1-score 0.933 (offline), 0.992 (online)

---

## Category 1: Neural Network Architecture Improvements

### 1.1 Network Depth and Width Variations
**Difficulty**: Easy | **Impact**: Medium | **Priority**: High

- [ ] **Experiment with different layer sizes**
  - Current: [6, 32, 32, 2]
  - Try: [6, 64, 64, 2], [6, 128, 128, 2], [6, 16, 16, 2]
  - Try: [6, 64, 32, 16, 2], [6, 128, 64, 32, 2] (deeper networks)
  - Try asymmetric: [6, 128, 64, 2], [6, 256, 64, 2]

- [ ] **Implement bottleneck architecture**
  - Try: [6, 64, 16, 64, 2] (compress and expand)
  - Good for feature learning and regularization

### 1.2 Skip Connections / Residual Networks
**Difficulty**: Medium | **Impact**: High | **Priority**: Medium

- [ ] **Add residual connections**
  - ResNet-style: `output = F.relu(layer(x) + x)`
  - Helps with gradient flow in deeper networks
  - Need to ensure dimensions match for addition
  - **Challenge**: Verify eBPF compatibility with skip connections

- [ ] **Dense connections (DenseNet-style)**
  - Concatenate previous layer outputs
  - Requires more memory but can improve performance

### 1.3 Activation Function Alternatives
**Difficulty**: Easy-Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Try different activation functions** (if eBPF-compatible)
  - LeakyReLU: `max(0.01*x, x)` - avoids dead neurons
  - ELU: `x if x > 0 else alpha*(exp(x)-1)` - smooth, but exp() may be expensive
  - GELU: Used in modern transformers
  - Swish/SiLU: `x * sigmoid(x)` - smooth, non-monotonic
  - **Note**: Test eBPF compatibility for each (some may require approximations)

- [ ] **Parametric ReLU (PReLU)**
  - Learnable slope for negative values: `max(alpha*x, x)`

### 1.4 Add Bias Terms
**Difficulty**: Easy | **Impact**: Low-Medium | **Priority**: Low

- [ ] **Enable bias in linear layers**
  - Current: `bias=False`
  - Try: `bias=True`
  - Adds expressiveness but increases parameters slightly
  - **Check**: eBPF memory constraints with bias

### 1.5 Batch Normalization
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Add BatchNorm layers**
  - Insert after linear layers, before activation
  - Stabilizes training, enables higher learning rates
  - **Challenge**: Implement batch statistics tracking in eBPF (mean, variance)
  - May need to store running statistics in eBPF maps

### 1.6 Layer Normalization
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Add LayerNorm instead of BatchNorm**
  - Normalizes across features instead of batch
  - Easier to implement in eBPF (no batch statistics needed)
  - Works well for small batch sizes

---

## Category 2: Training Optimization Improvements

### 2.1 Advanced Optimizers
**Difficulty**: Easy | **Impact**: Medium-High | **Priority**: High

- [ ] **Try different optimizers**
  - AdamW: Adam with proper weight decay
  - SGD with momentum: Classic, sometimes outperforms Adam
  - RMSprop: Good for non-stationary objectives
  - NAdam: Adam + Nesterov momentum
  - RAdam: Rectified Adam (handles early training instability)
  - AdaBound: Transitions from Adam to SGD

- [ ] **Hyperparameter tuning for each optimizer**
  - Learning rate: [1e-4, 5e-4, 1e-3, 5e-3]
  - Momentum: [0.9, 0.95, 0.99]
  - Weight decay: [1e-5, 1e-4, 1e-3]

### 2.2 Learning Rate Scheduling
**Difficulty**: Easy | **Impact**: High | **Priority**: High

- [ ] **Implement learning rate schedulers**
  - StepLR: Decay LR every N epochs
  - MultiStepLR: Decay at specific milestones
  - ExponentialLR: Exponential decay
  - CosineAnnealingLR: Cosine annealing (popular in modern training)
  - ReduceLROnPlateau: Reduce when validation loss plateaus
  - OneCycleLR: One cycle policy (very effective)
  - CyclicLR: Cyclical learning rates

- [ ] **Learning rate warmup**
  - Gradually increase LR in first few epochs
  - Prevents early instability

### 2.3 Training Duration and Batch Size
**Difficulty**: Easy | **Impact**: Medium | **Priority**: High

- [ ] **Experiment with training epochs**
  - Current: 32 epochs
  - Try: 50, 100, 200 epochs with early stopping

- [ ] **Batch size experiments**
  - Current: 512
  - Try: [128, 256, 1024, 2048]
  - Smaller batches: More noise, better generalization
  - Larger batches: Faster training, more stable gradients

- [ ] **Gradient accumulation**
  - Simulate larger batch sizes on limited memory
  - Accumulate gradients over multiple mini-batches

### 2.4 Loss Function Improvements
**Difficulty**: Easy-Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Try alternative loss functions**
  - Focal Loss: Handles class imbalance better
  - Label Smoothing: Prevents overconfident predictions
  - Weighted CrossEntropyLoss: Give more weight to minority class
  - Symmetric Cross Entropy: Robust to noisy labels

- [ ] **Class weighting**
  - Current: No weighting
  - Compute class weights: `weight = 1 / class_frequency`
  - Helps with imbalanced dataset (more benign than attack)

### 2.5 Early Stopping and Model Checkpointing
**Difficulty**: Easy | **Impact**: Medium | **Priority**: High

- [ ] **Implement early stopping**
  - Monitor validation loss
  - Stop if no improvement for N epochs
  - Prevents overfitting

- [ ] **Save best model (not just last)**
  - Track validation F1-score
  - Keep model with best validation performance
  - Currently only saves last epoch

### 2.6 Gradient Clipping
**Difficulty**: Easy | **Impact**: Low-Medium | **Priority**: Low

- [ ] **Add gradient clipping**
  - `torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm)`
  - Prevents exploding gradients
  - Stabilizes training

---

## Category 3: Regularization Techniques

### 3.1 Weight Regularization
**Difficulty**: Easy | **Impact**: Medium | **Priority**: High

- [ ] **Add L2 regularization (weight decay)**
  - Add to optimizer: `weight_decay=1e-4`
  - Prevents overfitting
  - Try: [1e-5, 1e-4, 1e-3]

- [ ] **Add L1 regularization**
  - Encourages sparsity
  - Add L1 penalty to loss: `loss += lambda * sum(|weights|)`
  - Try: lambda in [1e-5, 1e-4, 1e-3]

- [ ] **Elastic Net (L1 + L2)**
  - Combines both L1 and L2
  - Better generalization

### 3.2 Dropout
**Difficulty**: Easy | **Impact**: Medium-High | **Priority**: High

- [ ] **Add dropout layers**
  - Insert after activation layers
  - Try dropout rates: [0.1, 0.2, 0.3, 0.5]
  - Prevents overfitting by randomly dropping neurons
  - **Note**: Dropout is only during training, disable during inference

- [ ] **Variational Dropout**
  - More principled dropout
  - Better uncertainty estimation

### 3.3 Data Augmentation
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Feature-level augmentation**
  - Add small Gaussian noise: `x + epsilon * N(0, 1)`
  - Random scaling: `x * uniform(0.9, 1.1)`
  - Mixup: `x_mixed = lambda * x1 + (1-lambda) * x2`
  - Cutout: Randomly zero out some features

- [ ] **Synthetic sample generation**
  - SMOTE-like for features (interpolate between samples)
  - Only on training data

---

## Category 4: Data Preprocessing Improvements

### 4.1 Alternative Normalization Methods
**Difficulty**: Easy | **Impact**: Medium | **Priority**: Medium

- [ ] **Try different scalers**
  - Current: StandardScaler (z-score)
  - MinMaxScaler: Scale to [0, 1] or [-1, 1]
  - RobustScaler: Uses median and IQR (robust to outliers)
  - MaxAbsScaler: Scale to [-1, 1] by max absolute value
  - QuantileTransformer: Non-linear, maps to uniform/normal distribution
  - PowerTransformer: Yeo-Johnson or Box-Cox transformation

- [ ] **Compare scaler performance**
  - Evaluate each on validation set
  - Choose best for your data distribution

### 4.2 Outlier Handling
**Difficulty**: Easy | **Impact**: Low-Medium | **Priority**: Low

- [ ] **Outlier clipping**
  - Clip extreme values to percentiles (1st, 99th)
  - Or use IQR method: clip outside [Q1-1.5*IQR, Q3+1.5*IQR]
  - May improve robustness

- [ ] **Winsorization**
  - Replace extreme values with less extreme values
  - Alternative to clipping

### 4.3 Feature Transformation
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Low

- [ ] **Log transformation for skewed features**
  - If features have long-tail distribution
  - `log(x + 1)` to handle zeros

- [ ] **Polynomial features (degree 2)**
  - Create interaction terms: `x1*x2, x1^2, etc.`
  - **Caution**: Increases feature count (may violate "no new features" rule)
  - **Alternative**: Let the network learn these interactions

---

## Category 5: Quantization Improvements

### 5.1 Quantization-Aware Training (QAT)
**Difficulty**: Hard | **Impact**: High | **Priority**: High

- [ ] **Implement Quantization-Aware Training**
  - Current: Post-training quantization
  - QAT: Train with quantization in the loop
  - Model learns to be robust to quantization errors
  - **Implementation**:
    - Add fake quantization nodes during training
    - Simulate fixed-point arithmetic in training
    - Should significantly improve quantized model performance

### 5.2 Different Fixed-Point Formats
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **Experiment with different Q formats**
  - Current: Q16.16 (16 integer bits, 16 fractional bits)
  - Try: Q8.24 (more precision, smaller integer range)
  - Try: Q24.8 (less precision, larger integer range)
  - Try: Q12.20, Q20.12
  - **Note**: Must verify no overflow for your data range

- [ ] **Mixed precision quantization**
  - Use different Q formats for different layers
  - Early layers: More precision
  - Later layers: Less precision

### 5.3 Better Quantization Schemes
**Difficulty**: Hard | **Impact**: Medium-High | **Priority**: Medium

- [ ] **Try symmetric vs asymmetric quantization**
  - Current: Symmetric
  - Asymmetric: Different zero point, may fit data better

- [ ] **Per-channel quantization**
  - Current: Per-tensor
  - Per-channel: Different scale per output channel
  - Better accuracy, slightly more complex

- [ ] **Dynamic quantization**
  - Compute scale dynamically based on activation range
  - **Challenge**: More complex in eBPF

---

## Category 6: Ensemble Methods

### 6.1 Model Ensembling
**Difficulty**: Medium | **Impact**: High | **Priority**: Medium

- [ ] **Train multiple models with different architectures**
  - Ensemble of 3-5 models
  - Voting: Majority vote for final prediction
  - Averaging: Average output probabilities

- [ ] **Bagging (Bootstrap Aggregating)**
  - Train multiple models on different data subsets
  - Reduces variance

- [ ] **Boosting-inspired approaches**
  - Train models sequentially, focusing on hard examples
  - AdaBoost-style weighting

### 6.2 Cross-Validation Ensemble
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **K-Fold cross-validation ensemble**
  - Train K models on K different train/val splits
  - Use all K models for inference (ensemble)
  - More robust than single train/test split

### 6.3 Snapshot Ensemble
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Low

- [ ] **Save multiple checkpoints during training**
  - Use cyclic learning rate
  - Save model at each cycle end
  - Ensemble these snapshots

---

## Category 7: Training Data Strategies

### 7.1 Train/Validation/Test Split
**Difficulty**: Easy | **Impact**: High | **Priority**: High

- [ ] **Implement proper train/val/test split**
  - Current: Only train/test (80/20)
  - Use: 70/15/15 or 60/20/20
  - Validation for hyperparameter tuning
  - Test for final evaluation only

- [ ] **Stratified splitting**
  - Ensure class distribution is preserved
  - Important for imbalanced dataset

### 7.2 Cross-Validation
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Medium

- [ ] **K-Fold Cross-Validation**
  - 5-fold or 10-fold
  - More robust performance estimation
  - Prevents overfitting to specific train/test split

- [ ] **Stratified K-Fold**
  - Maintains class distribution in each fold

### 7.3 Class Imbalance Handling
**Difficulty**: Easy-Medium | **Impact**: High | **Priority**: High

- [ ] **Oversampling minority class**
  - Duplicate attack samples to balance classes
  - Random oversampling

- [ ] **Undersampling majority class**
  - Remove some benign samples
  - Can lose information

- [ ] **SMOTE (Synthetic Minority Over-sampling Technique)**
  - Generate synthetic attack samples
  - Interpolate between existing samples
  - More sophisticated than random oversampling

- [ ] **Class-weighted loss**
  - Give higher weight to minority class
  - `weight_benign = 1.0, weight_attack = N_benign/N_attack`

### 7.4 Hard Negative Mining
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Low

- [ ] **Focus on difficult examples**
  - Identify samples that model gets wrong
  - Oversample these hard examples
  - Or use focal loss to automatically focus on hard examples

---

## Category 8: Model Compression (Beyond Quantization)

### 8.1 Pruning
**Difficulty**: Hard | **Impact**: Medium | **Priority**: Low

- [ ] **Weight pruning**
  - Remove weights with small magnitude
  - Sparse networks can perform similarly to dense
  - Reduces memory and computation in eBPF
  - **Challenge**: eBPF implementation of sparse operations

- [ ] **Structured pruning**
  - Prune entire neurons/channels
  - Easier to implement than unstructured

### 8.2 Knowledge Distillation
**Difficulty**: Hard | **Impact**: Medium-High | **Priority**: Medium

- [ ] **Train larger teacher model, distill to smaller student**
  - Teacher: Large, high-accuracy model (6→128→128→2)
  - Student: Current size or smaller (6→32→32→2)
  - Student learns from teacher's soft outputs
  - Can match or exceed teacher performance with fewer parameters

---

## Category 9: Advanced Training Techniques

### 9.1 Curriculum Learning
**Difficulty**: Medium | **Impact**: Medium | **Priority**: Low

- [ ] **Train on easy examples first, then hard**
  - Order training samples by difficulty
  - Gradually introduce harder samples
  - Can improve convergence

### 9.2 Self-Training / Pseudo-Labeling
**Difficulty**: Medium-Hard | **Impact**: Medium | **Priority**: Low

- [ ] **Use model predictions on unlabeled data**
  - If you have unlabeled traffic data
  - Use high-confidence predictions as pseudo-labels
  - Retrain with pseudo-labeled data

### 9.3 Multi-Task Learning
**Difficulty**: Hard | **Impact**: Medium | **Priority**: Low

- [ ] **Add auxiliary tasks**
  - Main task: Binary classification (benign/attack)
  - Auxiliary: Predict specific attack type
  - Shared representations can improve main task
  - **Note**: Requires attack type labels

### 9.4 Adversarial Training
**Difficulty**: Hard | **Impact**: Medium | **Priority**: Low

- [ ] **Train on adversarial examples**
  - Generate adversarial perturbations
  - Add to training set
  - Improves robustness
  - Relevant for security applications

---

## Category 10: Threshold and Classification Improvements

### 10.1 Adaptive Threshold Optimization
**Difficulty**: Medium | **Impact**: High | **Priority**: High

- [ ] **Learn optimal threshold from validation data**
  - Current: Fixed threshold or adaptive based on margin
  - Find threshold that maximizes F1-score on validation set
  - Or maximize precision at target recall (e.g., 95% recall)

- [ ] **ROC curve analysis**
  - Plot ROC curve
  - Choose operating point based on false positive rate requirements

- [ ] **Precision-Recall curve**
  - Better for imbalanced datasets
  - Choose threshold based on PR curve

### 10.2 Confidence-Based Classification
**Difficulty**: Easy | **Impact**: Medium | **Priority**: Medium

- [ ] **Use output probabilities, not just argmax**
  - Current: `attack if score_attack > score_benign`
  - Better: `attack if P(attack) > threshold` (e.g., 0.7)
  - Allows tuning sensitivity

- [ ] **Uncertainty estimation**
  - Multiple forward passes with dropout (MC Dropout)
  - Measure prediction variance
  - Flag uncertain predictions for manual review

### 10.3 Multi-Class Classification (If Data Available)
**Difficulty**: Medium | **Impact**: High | **Priority**: Medium

- [ ] **Classify attack types, not just binary**
  - Change from binary (benign/attack) to multi-class
  - Outputs: [Benign, DoS, PortScan, BruteForce, XSS, ...]
  - More informative for defense
  - **Note**: Paper does this in offline eval, but not in eBPF

---

## Category 11: Evaluation and Analysis Improvements

### 11.1 Better Metrics
**Difficulty**: Easy | **Impact**: Low | **Priority**: Medium

- [ ] **Track additional metrics during training**
  - Current: Only loss, A, P, R, F1
  - Add: AUC-ROC, AUC-PR, MCC, Specificity, NPV
  - Per-class metrics
  - Confusion matrix

- [ ] **Monitor training dynamics**
  - Learning curves (train vs val loss)
  - Gradient norms
  - Weight distributions

### 11.2 Model Interpretability
**Difficulty**: Medium | **Impact**: Low | **Priority**: Low

- [ ] **Feature importance analysis**
  - Which of the 6 features are most important?
  - Permutation importance
  - SHAP values
  - Helps understand model decisions

- [ ] **Activation analysis**
  - Visualize hidden layer activations
  - Understand what patterns network learns

---

## Category 12: Implementation and Code Quality

### 12.1 Code Improvements
**Difficulty**: Easy | **Impact**: Low | **Priority**: Medium

- [ ] **Fix typo: `totoal_loss` → `total_loss`**

- [ ] **Add validation loop**
  - Currently no validation during training
  - Add validation evaluation each epoch

- [ ] **Progress bars and better logging**
  - Use tqdm for progress tracking
  - Log to file (not just print)
  - TensorBoard integration

- [ ] **Reproducibility**
  - Set random seeds (torch, numpy, random)
  - Log hyperparameters
  - Version control for models

### 12.2 Hyperparameter Search
**Difficulty**: Medium | **Impact**: High | **Priority**: High

- [ ] **Implement hyperparameter search**
  - Grid search over key parameters
  - Random search (often more efficient)
  - Bayesian optimization (Optuna, Ray Tune)
  - Automated hyperparameter tuning

---

## Recommended Implementation Order (Priority-Based)

### Phase 1: Quick Wins (1-2 days)
1. Add train/val/test split with validation
2. Implement early stopping
3. Try AdamW with weight decay
4. Add learning rate scheduling (CosineAnnealingLR)
5. Experiment with dropout (0.2, 0.3)
6. Class-weighted loss for imbalance
7. Increase epochs with early stopping

### Phase 2: Training Improvements (2-3 days)
8. Quantization-Aware Training (QAT)
9. Try different optimizers (SGD+momentum, RAdam)
10. Batch size experiments
11. Different layer sizes ([6,64,64,2], [6,128,64,2])
12. Label smoothing

### Phase 3: Advanced Techniques (3-5 days)
13. Ensemble of 3-5 different architectures
14. SMOTE for class imbalance
15. Try different activation functions (LeakyReLU)
16. Batch/Layer Normalization
17. Knowledge distillation

### Phase 4: Optimization (2-3 days)
18. Hyperparameter search (Optuna)
19. Different quantization formats (Q8.24, Q24.8)
20. Adaptive threshold optimization
21. Try different scalers (RobustScaler, MinMaxScaler)

### Phase 5: Polish and Analysis (1-2 days)
22. Better metrics and evaluation
23. Feature importance analysis
24. Model interpretability
25. Final comparison and visualization

---

## Comparison Methodology

### Metrics to Compare
- **Accuracy, Precision, Recall, F1-score**
- **AUC-ROC, AUC-PR** (better for imbalanced data)
- **MCC** (Matthews Correlation Coefficient)
- **Inference time** (ns per flow)
- **Memory usage** (KB)
- **Model size** (number of parameters)
- **False Positive Rate, False Negative Rate**

### Comparison Tables to Generate
1. **Architecture comparison**: Different layer sizes
2. **Optimizer comparison**: Adam vs AdamW vs SGD vs RAdam
3. **Regularization comparison**: No reg vs L2 vs Dropout vs Both
4. **Quantization comparison**: Different Q formats
5. **Training strategy comparison**: Different schedules, batch sizes
6. **Ensemble vs Single model**
7. **With QAT vs Without QAT**

### Visualizations to Create
1. **ROC curves**: Old vs improved versions
2. **Precision-Recall curves**
3. **Confusion matrices**
4. **Training curves**: Loss and metrics over epochs
5. **Performance vs computational cost trade-offs**
6. **Feature importance comparison**
7. **Reliability analysis** (MTTF comparison)
8. **Bar charts**: F1-score comparison across improvements

---

## Expected Improvements (Estimates)

Based on similar work in literature:

- **QAT**: +2-5% F1-score improvement
- **Better optimizer + LR schedule**: +1-3%
- **Dropout + L2**: +1-2% (better generalization)
- **Optimal architecture**: +1-3%
- **Class balancing**: +2-5% (especially for recall)
- **Ensemble (3-5 models)**: +1-4%
- **Threshold optimization**: +1-2%

**Total potential improvement**: 5-15% F1-score improvement
**From 0.933 → 0.98-1.00 on offline dataset**

---

## Implementation Notes

### Things to Watch Out For:
1. **eBPF Compatibility**: Not all improvements transfer to eBPF
   - Some activation functions may need approximations
   - BatchNorm requires careful implementation
   - Skip connections need verification

2. **Memory Constraints**:
   - Larger models may exceed eBPF memory limits
   - Track memory usage during experiments

3. **Quantization Impact**:
   - Always test quantized performance
   - Some techniques may not survive quantization well

4. **Overfitting**:
   - More complex models may overfit small dataset
   - Always validate on separate data

### Good Practices:
- **Always compare quantized performance**, not just float32
- **Test on real-time reproduction dataset**, not just CIC-IDS-2017
- **Ablation studies**: Change one thing at a time
- **Document everything**: Hyperparameters, results, observations
- **Version control**: Save each model variant

---

## Conclusion

This document provides **60+ improvement ideas** across 12 categories. You can:

1. **Pick 5-10 high-priority items** from Phase 1-2
2. **Implement and evaluate** each
3. **Document results** in comparison tables and graphs
4. **Write your thesis/report** explaining:
   - What you changed
   - Why you changed it
   - What improvement you achieved
   - Trade-offs (accuracy vs speed vs memory)

Focus on **high-impact, feasible** improvements first. Good luck!
