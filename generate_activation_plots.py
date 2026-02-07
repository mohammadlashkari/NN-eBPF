"""
Visualization script for Activation Function improvement.

Usage:
    python generate_activation_plots.py

Requires:
    - results.csv with columns: attack_type, metric, baseline, improved
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# Read results from CSV
df = pd.read_csv('results.csv')

# Filter for activation function comparison
df_activation = df[df['improvement'] == 'activation_function']

# ============================================
# Figure 1: Per-Attack F1-Score Comparison
# ============================================
fig, ax = plt.subplots(figsize=(10, 6))

attack_types = ['Slowloris', 'DDoS', 'PortScan', 'WebAttack', 'Overall']
baseline_f1 = df_activation[df_activation['metric'] == 'f1_score']['baseline'].values
improved_f1 = df_activation[df_activation['metric'] == 'f1_score']['improved'].values

x = np.arange(len(attack_types))
width = 0.35

bars1 = ax.bar(x - width/2, baseline_f1, width, label='ReLU (Baseline)', color='#ff7f0e', alpha=0.8)
bars2 = ax.bar(x + width/2, improved_f1, width, label='LeakyReLU (Improved)', color='#2ca02c', alpha=0.8)

ax.set_xlabel('Attack Type', fontsize=12, fontweight='bold')
ax.set_ylabel('F1-Score', fontsize=12, fontweight='bold')
ax.set_title('Activation Function Impact: F1-Score Comparison', fontsize=14, fontweight='bold')
ax.set_xticks(x)
ax.set_xticklabels(attack_types)
ax.legend()
ax.grid(axis='y', alpha=0.3)
ax.set_ylim([0.85, 1.0])

# Add value labels on bars
for bar in bars1 + bars2:
    height = bar.get_height()
    ax.text(bar.get_x() + bar.get_width()/2., height,
            f'{height:.3f}', ha='center', va='bottom', fontsize=9)

plt.tight_layout()
plt.savefig('activation_f1_comparison.png', dpi=300)
print("Saved: activation_f1_comparison.png")

# ============================================
# Figure 2: Training Loss Curves
# ============================================
fig, ax = plt.subplots(figsize=(10, 6))

# These would come from training history
epochs_data = df_activation[df_activation['metric'] == 'train_loss_epoch'].dropna()
epochs_baseline = epochs_data['epoch'].values
loss_baseline = epochs_data['baseline'].values
loss_improved = epochs_data['improved'].values

ax.plot(epochs_baseline, loss_baseline, label='ReLU (Baseline)',
        color='#ff7f0e', linewidth=2, marker='o', markersize=4)
ax.plot(epochs_baseline, loss_improved, label='LeakyReLU (Improved)',
        color='#2ca02c', linewidth=2, marker='s', markersize=4)

ax.set_xlabel('Epoch', fontsize=12, fontweight='bold')
ax.set_ylabel('Training Loss', fontsize=12, fontweight='bold')
ax.set_title('Training Convergence: ReLU vs LeakyReLU', fontsize=14, fontweight='bold')
ax.legend()
ax.grid(alpha=0.3)

plt.tight_layout()
plt.savefig('activation_training_loss.png', dpi=300)
print("Saved: activation_training_loss.png")

# ============================================
# Figure 3: Metrics Summary Table
# ============================================
fig, ax = plt.subplots(figsize=(12, 4))
ax.axis('tight')
ax.axis('off')

metrics_data = df_activation[df_activation['attack_type'] == 'Overall'][['metric', 'baseline', 'improved']].dropna()
metrics_data['improvement'] = metrics_data['improved'] - metrics_data['baseline']
metrics_data['improvement_%'] = (metrics_data['improvement'] / metrics_data['baseline'] * 100)

table_data = []
for _, row in metrics_data.iterrows():
    table_data.append([
        row['metric'].upper(),
        f"{row['baseline']:.4f}",
        f"{row['improved']:.4f}",
        f"{row['improvement']:+.4f}",
        f"{row['improvement_%']:+.2f}%"
    ])

table = ax.table(cellText=table_data,
                 colLabels=['Metric', 'ReLU (Baseline)', 'LeakyReLU (Improved)', 'Δ Absolute', 'Δ Percent'],
                 cellLoc='center',
                 loc='center',
                 bbox=[0, 0, 1, 1])

table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1, 2)

# Color header
for i in range(5):
    table[(0, i)].set_facecolor('#4CAF50')
    table[(0, i)].set_text_props(weight='bold', color='white')

# Color improvement cells
for i in range(1, len(table_data) + 1):
    improvement = float(table_data[i-1][3])
    if improvement > 0:
        table[(i, 4)].set_facecolor('#90EE90')
    elif improvement < 0:
        table[(i, 4)].set_facecolor('#FFB6C6')

plt.savefig('activation_metrics_table.png', dpi=300, bbox_inches='tight')
print("Saved: activation_metrics_table.png")

print("\n✓ All visualizations generated successfully!")
