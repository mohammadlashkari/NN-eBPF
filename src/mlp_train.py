"""
NN-eBPF Intrusion Detection - Improved Training Script

This script implements 4 key improvements over the baseline:
1. LeakyReLU activation function (prevents dying neurons)
2. Label smoothing cross-entropy loss (better generalization)
3. Early stopping + model checkpointing (prevents overfitting)
4. AdamW optimizer with weight decay (proper L2 regularization)

Original baseline: ReLU + CrossEntropy + Adam + no validation
Improved version: LeakyReLU + LabelSmoothing + AdamW + early stopping
"""

from sklearn import preprocessing
from sklearn import model_selection
import numpy as np
from mlp import Net, train, test
import torch.utils.data as Data
import torch
import json
import os

# ============================================
# Configuration
# ============================================
data_path = '../dataset/reproduction-xdp-data.npy'
label_path = '../dataset/reproduction-xdp-label.npy'
model_path = '../dataset/reproduction-xdp-MLP.pkl'

# Hyperparameters
batch_size = 512
learning_rate = 1e-3
num_epoch = 50  # Increased from 32 (early stopping will prevent overfitting)

# IMPROVEMENT 4: AdamW weight decay
weight_decay = 1e-4  # L2 regularization strength

# IMPROVEMENT 3: Early stopping patience
patience = 7  # Stop if no improvement for 7 epochs

# IMPROVEMENT 2: Label smoothing
label_smoothing = 0.1  # 10% smoothing

device = 'cuda' if torch.cuda.is_available() else 'cpu'
print(f'Using device: {device}')

# ============================================
# Load and Split Data
# ============================================
print('\n[1/5] Loading dataset...')
data = np.load(data_path)
label = np.load(label_path, allow_pickle=True)
print(f'Dataset shape: {data.shape}, Labels shape: {label.shape}')

# IMPROVEMENT 3: Train/Val/Test split (was only Train/Test before)
# Split: 70% train, 15% validation, 15% test
print('\n[2/5] Splitting data into train/val/test...')

# First split: 70% train, 30% temp
X_train, X_temp, y_train, y_temp = model_selection.train_test_split(
    data, label, train_size=0.7, test_size=0.3, stratify=label, random_state=42)

# Second split: Split temp into 50% validation, 50% test (15% each of total)
X_val, X_test, y_val, y_test = model_selection.train_test_split(
    X_temp, y_temp, train_size=0.5, test_size=0.5, stratify=y_temp, random_state=42)

print(f'Train set: {X_train.shape[0]} samples ({X_train.shape[0]/len(data)*100:.1f}%)')
print(f'Val set:   {X_val.shape[0]} samples ({X_val.shape[0]/len(data)*100:.1f}%)')
print(f'Test set:  {X_test.shape[0]} samples ({X_test.shape[0]/len(data)*100:.1f}%)')

# Save raw test data for later use
save_X_test = X_test.copy()

# ============================================
# Data Normalization
# ============================================
print('\n[3/5] Normalizing data (StandardScaler)...')
standard_scaler = preprocessing.StandardScaler()
standard_scaler.fit(X_train)  # Fit only on training data to avoid data leakage

X_train = standard_scaler.transform(X_train)
X_val = standard_scaler.transform(X_val)
X_test = standard_scaler.transform(X_test)

print(f'Feature mean: {standard_scaler.mean_[:3]}...')
print(f'Feature scale: {standard_scaler.scale_[:3]}...')

# ============================================
# Create DataLoaders
# ============================================
print('\n[4/5] Creating DataLoaders...')

train_loader = Data.DataLoader(
    dataset=Data.TensorDataset(
        torch.from_numpy(X_train).float(),
        torch.from_numpy(y_train).long()
    ),
    batch_size=batch_size,
    shuffle=True,
    num_workers=4,
)

# IMPROVEMENT 3: Validation DataLoader (NEW)
val_loader = Data.DataLoader(
    dataset=Data.TensorDataset(
        torch.from_numpy(X_val).float(),
        torch.from_numpy(y_val).long()
    ),
    batch_size=batch_size,
    shuffle=False,  # No need to shuffle validation
    num_workers=4,
)

test_loader = Data.DataLoader(
    dataset=Data.TensorDataset(
        torch.from_numpy(X_test).float(),
        torch.from_numpy(y_test).long()
    ),
    batch_size=batch_size,
    shuffle=False,  # No need to shuffle test
    num_workers=4,
)

# ============================================
# Model Training (with all 4 improvements)
# ============================================
print('\n[5/5] Training model with improvements...')
print('=' * 60)
print('IMPROVEMENTS APPLIED:')
print('  1. LeakyReLU activation (negative_slope=0.01)')
print(f'  2. Label smoothing loss (smoothing={label_smoothing})')
print(f'  3. Early stopping (patience={patience}) + checkpointing')
print(f'  4. AdamW optimizer (weight_decay={weight_decay})')
print('=' * 60)

# Create model with LeakyReLU (IMPROVEMENT 1)
model = Net(X_train.shape[1], 2)
print(f'\nModel architecture:\n{model}')

# Train with all improvements
model, history = train(
    num_epoch=num_epoch,
    train_loader=train_loader,
    model=model,
    device=device,
    lr=learning_rate,
    model_path=model_path,
    val_loader=val_loader,  # IMPROVEMENT 3: Validation
    weight_decay=weight_decay,  # IMPROVEMENT 4: Weight decay
    patience=patience,  # IMPROVEMENT 3: Early stopping
    label_smoothing=label_smoothing  # IMPROVEMENT 2: Label smoothing
)

# ============================================
# Final Evaluation on Test Set
# ============================================
print('\n' + '=' * 60)
print('FINAL EVALUATION ON TEST SET')
print('=' * 60)
test_loss, test_precision, test_recall, test_f1 = test(test_loader, model, device)

# ============================================
# Save Model and Metadata
# ============================================
print('\n[Saving] Saving model and metadata...')

# Save complete model state
torch.save({
    'state_dict': model.state_dict(),
    'train_loader': train_loader,
    'X_test': X_test,
    'y_test': y_test,
    'raw_X_test': save_X_test,
    'mean': standard_scaler.mean_,
    'scale': standard_scaler.scale_,
    'history': history,
    'hyperparameters': {
        'learning_rate': learning_rate,
        'batch_size': batch_size,
        'weight_decay': weight_decay,
        'label_smoothing': label_smoothing,
        'patience': patience,
        'activation': 'LeakyReLU',
        'optimizer': 'AdamW',
        'loss': 'LabelSmoothingCrossEntropy'
    },
    'test_metrics': {
        'loss': test_loss,
        'precision': test_precision,
        'recall': test_recall,
        'f1': test_f1
    }
}, 'mlp.th')

print(f'Model saved to: mlp.th')
print(f'Model checkpoint saved to: {model_path}')

# Save training history to JSON for analysis
history_path = 'training_history.json'
with open(history_path, 'w') as f:
    # Convert numpy types to Python types for JSON serialization
    serializable_history = {
        'train_loss': [float(x) for x in history['train_loss']],
        'val_loss': [float(x) for x in history['val_loss']],
        'val_f1': [float(x) for x in history['val_f1']]
    }
    json.dump(serializable_history, f, indent=2)
print(f'Training history saved to: {history_path}')

print('\n' + '=' * 60)
print('TRAINING COMPLETE!')
print('=' * 60)
print(f'Best Validation F1: {max(history["val_f1"]):.4f}')
print(f'Final Test F1: {test_f1:.4f}')
print(f'Total epochs trained: {len(history["train_loss"])}')
print('=' * 60)
