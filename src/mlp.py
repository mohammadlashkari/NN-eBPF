import torch.nn.functional as F
import torch.nn as nn
import torch
import numpy as np

# ============================================
# IMPROVEMENT 1: Better Activation Function
# ============================================
# Changed from ReLU to LeakyReLU to avoid "dying ReLU" problem
# LeakyReLU allows small negative gradients (0.01*x for x<0)
# This prevents neurons from becoming permanently inactive

class Net(nn.Module):
    def __init__(self, in_dim, out_dim, activation='leakyrelu'):
        super().__init__()
        # LeakyReLU with negative_slope=0.01 (default)
        # Still eBPF-compatible: max(0.01*x, x) can be implemented in fixed-point
        self.model = nn.Sequential(
            nn.Linear(in_dim, 32, bias=False),
            nn.LeakyReLU(negative_slope=0.01),  # Changed from ReLU
            nn.Linear(32, 32, bias=False),
            nn.LeakyReLU(negative_slope=0.01),  # Changed from ReLU
            nn.Linear(32, out_dim, bias=False),
        )

    def forward(self, X):
        return self.model(X)

# ============================================
# IMPROVEMENT 2: Better Loss Function
# ============================================
# Label Smoothing Cross Entropy Loss
# Prevents overconfident predictions and improves generalization
# Formula: y_smooth = y * (1 - smoothing) + smoothing / num_classes

class LabelSmoothingCrossEntropy(nn.Module):
    """
    Label Smoothing Cross Entropy Loss

    Smoothing factor controls how much we soften the labels:
    - smoothing=0.0 → standard cross-entropy
    - smoothing=0.1 → 90% confidence in true class, 10% spread to other class

    Benefits:
    - Prevents overconfidence in predictions
    - Better calibrated probabilities
    - Improved generalization to unseen attacks
    """
    def __init__(self, smoothing=0.1):
        super().__init__()
        self.smoothing = smoothing
        self.confidence = 1.0 - smoothing

    def forward(self, pred, target):
        # pred: [batch_size, num_classes] (logits)
        # target: [batch_size] (class indices)

        num_classes = pred.size(-1)
        log_probs = F.log_softmax(pred, dim=-1)

        # Create smooth labels
        with torch.no_grad():
            true_dist = torch.zeros_like(log_probs)
            true_dist.fill_(self.smoothing / (num_classes - 1))
            true_dist.scatter_(1, target.unsqueeze(1), self.confidence)

        # Compute loss
        loss = torch.sum(-true_dist * log_probs, dim=-1)
        return loss.mean()

# ============================================
# IMPROVEMENT 3: Early Stopping Handler
# ============================================
# Monitors validation loss and stops training if no improvement
# Saves best model checkpoint based on validation F1-score

class EarlyStopping:
    """
    Early stopping to stop training when validation loss doesn't improve.

    Args:
        patience: Number of epochs to wait before stopping
        min_delta: Minimum change to qualify as improvement
        mode: 'min' for loss, 'max' for metrics like F1
    """
    def __init__(self, patience=7, min_delta=0.0, mode='min', verbose=True):
        self.patience = patience
        self.min_delta = min_delta
        self.mode = mode
        self.verbose = verbose
        self.counter = 0
        self.best_score = None
        self.early_stop = False
        self.best_epoch = 0

    def __call__(self, score, epoch):
        if self.best_score is None:
            self.best_score = score
            self.best_epoch = epoch
            return False

        # Check if improved
        if self.mode == 'min':
            improved = score < (self.best_score - self.min_delta)
        else:  # mode == 'max'
            improved = score > (self.best_score + self.min_delta)

        if improved:
            self.best_score = score
            self.best_epoch = epoch
            self.counter = 0
            if self.verbose:
                print(f'[EarlyStopping] Improvement detected at epoch {epoch}')
        else:
            self.counter += 1
            if self.verbose:
                print(f'[EarlyStopping] No improvement for {self.counter}/{self.patience} epochs')

            if self.counter >= self.patience:
                self.early_stop = True
                if self.verbose:
                    print(f'[EarlyStopping] Stopping early at epoch {epoch}. Best epoch was {self.best_epoch}')

        return self.early_stop

# ============================================
# IMPROVEMENT 4: Training with AdamW + Early Stopping
# ============================================
# AdamW: Adam with decoupled weight decay (proper L2 regularization)
# Weight decay prevents overfitting by penalizing large weights

def train(num_epoch, train_loader, model, device, lr, model_path=None,
          val_loader=None, weight_decay=1e-4, patience=7, label_smoothing=0.1):
    """
    Enhanced training loop with 4 improvements:
    1. LeakyReLU activation (in model)
    2. Label smoothing loss
    3. Early stopping + model checkpointing
    4. AdamW optimizer with weight decay

    Args:
        num_epoch: Maximum number of training epochs
        train_loader: DataLoader for training data
        model: Neural network model
        device: 'cuda' or 'cpu'
        lr: Learning rate
        model_path: Path to save final model
        val_loader: DataLoader for validation (for early stopping)
        weight_decay: L2 regularization strength (default: 1e-4)
        patience: Early stopping patience (default: 7 epochs)
        label_smoothing: Label smoothing factor (default: 0.1)
    """

    # IMPROVEMENT 2: Label Smoothing Cross Entropy
    criterion = LabelSmoothingCrossEntropy(smoothing=label_smoothing)

    # IMPROVEMENT 4: AdamW optimizer with weight decay
    # AdamW properly decouples weight decay from gradient update
    # weight_decay=1e-4 provides moderate regularization
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay)

    # IMPROVEMENT 3: Early stopping setup
    early_stopping = EarlyStopping(patience=patience, mode='min', verbose=True)

    # Track best model state
    best_model_state = None
    best_val_loss = float('inf')
    best_val_f1 = 0.0

    model.to(device)

    # Training history for logging
    history = {
        'train_loss': [],
        'val_loss': [],
        'val_f1': []
    }

    for epoch in range(num_epoch):
        # ===== Training Phase =====
        model.train()
        total_loss = 0  # Track total loss for epoch

        for X, y in train_loader:
            X, y = X.to(device), y.to(device)
            output = model(X)
            loss = criterion(output, y)
            total_loss += loss.item()

            optimizer.zero_grad()
            loss.backward()
            optimizer.step()

        avg_train_loss = total_loss / len(train_loader)
        history['train_loss'].append(avg_train_loss)

        # ===== Validation Phase =====
        val_metrics = None
        if val_loader is not None:
            val_loss, val_precision, val_recall, val_f1 = test(val_loader, model, device, criterion)
            history['val_loss'].append(val_loss)
            history['val_f1'].append(val_f1)

            print(f'Epoch {epoch:3d}: Train Loss={avg_train_loss:.4f} | '
                  f'Val Loss={val_loss:.4f} Val F1={val_f1:.4f}')

            # Save best model based on F1 score
            if val_f1 > best_val_f1:
                best_val_f1 = val_f1
                best_val_loss = val_loss
                best_model_state = model.state_dict().copy()
                print(f'  → New best model! F1={val_f1:.4f}')

            # Check early stopping (based on validation loss)
            if early_stopping(val_loss, epoch):
                print(f'\n[Early Stopping] Training stopped at epoch {epoch}')
                print(f'Best validation: F1={best_val_f1:.4f}, Loss={best_val_loss:.4f}')
                break
        else:
            # No validation set - just print training loss
            print(f'Epoch {epoch:3d}: Train Loss={avg_train_loss:.4f}')

    # Restore best model if we had validation
    if best_model_state is not None:
        model.load_state_dict(best_model_state)
        print(f'\n[Model Checkpoint] Restored best model from epoch {early_stopping.best_epoch}')

    # Save final/best model
    if model_path is not None:
        torch.save(model, model_path)
        print(f'[Model Save] Model saved to {model_path}')

    return model, history

def test(test_loader, model, device, criterion=None):
    """
    Enhanced test function with proper metrics calculation.

    Returns:
        (loss, precision, recall, f1): Evaluation metrics
    """
    model.to(device)
    model.eval()  # Set to evaluation mode

    if criterion is None:
        criterion = nn.CrossEntropyLoss()

    with torch.no_grad():
        TP = 0  # True Positives (attack correctly identified)
        FP = 0  # False Positives (benign classified as attack)
        TN = 0  # True Negatives (benign correctly identified)
        FN = 0  # False Negatives (attack classified as benign)
        total_loss = 0

        for X, y in test_loader:
            X, y = X.to(device), y.to(device)
            output = model(X)
            total_loss += criterion(output, y).item()

            # Get predictions
            y_pred = F.softmax(output, dim=1).argmax(dim=1)

            # Calculate confusion matrix elements
            # Class 0 = Benign, Class 1 = Attack
            TP += torch.logical_and(y == 1, y_pred == 1).sum().item()
            FP += torch.logical_and(y == 0, y_pred == 1).sum().item()
            TN += torch.logical_and(y == 0, y_pred == 0).sum().item()
            FN += torch.logical_and(y == 1, y_pred == 0).sum().item()

        # Calculate metrics
        A = 0.0  # Accuracy
        P = 0.0  # Precision
        R = 0.0  # Recall
        F1 = 0.0  # F1-score

        try:
            A = (TP + TN) / (TP + FP + TN + FN)
            P = TP / (TP + FP) if (TP + FP) > 0 else 0.0
            R = TP / (TP + FN) if (TP + FN) > 0 else 0.0
            F1 = 2 * P * R / (P + R) if (P + R) > 0 else 0.0
            print(f'Test Results - Accuracy:{A:.3f} Precision:{P:.3f} Recall:{R:.3f} F1:{F1:.3f}')
        except ZeroDivisionError:
            print(f'Error in metrics calculation: TP={TP}, FP={FP}, TN={TN}, FN={FN}')

        avg_loss = total_loss / len(test_loader) if len(test_loader) > 0 else 0.0
        return (avg_loss, P, R, F1)