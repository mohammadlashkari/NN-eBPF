#!/usr/bin/env python3
"""
Verification script for results.csv

This script checks the structure and content of results.csv
to ensure it's ready for visualization.
"""

import pandas as pd
import sys

def verify_results():
    print("=" * 60)
    print("NN-eBPF Results Data Verification")
    print("=" * 60)
    print()

    # Check if file exists
    try:
        df = pd.read_csv('results.csv')
        print("✓ Successfully loaded results.csv")
        print(f"  Total rows: {len(df)}")
        print()
    except FileNotFoundError:
        print("✗ Error: results.csv not found!")
        return False
    except Exception as e:
        print(f"✗ Error loading results.csv: {e}")
        return False

    # Check required columns
    required_cols = ['improvement', 'attack_type', 'metric', 'baseline', 'improved', 'epoch', 'value']
    missing_cols = [col for col in required_cols if col not in df.columns]

    if missing_cols:
        print(f"✗ Missing required columns: {', '.join(missing_cols)}")
        return False
    else:
        print("✓ All required columns present")
        print()

    # Check each improvement
    improvements = ['activation_function', 'adamw_weight_decay', 'loss_function', 'early_stopping']

    print("Improvement Data Summary:")
    print("-" * 60)

    all_good = True
    for imp in improvements:
        df_imp = df[df['improvement'] == imp]
        count = len(df_imp)

        if count == 0:
            print(f"  ✗ {imp}: NO DATA FOUND")
            all_good = False
        else:
            # Get unique metrics
            metrics = df_imp['metric'].dropna().unique()
            f1_data = df_imp[df_imp['metric'] == 'f1_score']

            print(f"  ✓ {imp}:")
            print(f"      Rows: {count}")
            print(f"      Unique metrics: {len(metrics)}")

            if len(f1_data) > 0:
                avg_baseline = f1_data['baseline'].mean()
                avg_improved = f1_data['improved'].mean()
                improvement_pct = ((avg_improved - avg_baseline) / avg_baseline * 100)
                print(f"      F1 Baseline: {avg_baseline:.3f}")
                print(f"      F1 Improved: {avg_improved:.3f}")
                print(f"      Improvement: +{improvement_pct:.1f}%")

    print()

    if not all_good:
        print("⚠️  Warning: Some improvements are missing data")
        print()
        return False

    # Check for training curves
    print("Training Curve Data:")
    print("-" * 60)

    # Activation function
    act_curves = len(df[(df['improvement'] == 'activation_function') &
                        (df['metric'] == 'train_loss_epoch')])
    print(f"  Activation function: {act_curves} epoch points")

    # AdamW
    adamw_curves = len(df[(df['improvement'] == 'adamw_weight_decay') &
                          (df['metric'] == 'train_loss_adam')])
    print(f"  AdamW: {adamw_curves} epoch points")

    # Loss function
    loss_curves = len(df[(df['improvement'] == 'loss_function') &
                         (df['metric'] == 'train_loss')])
    print(f"  Loss function: {loss_curves} epoch points")

    # Early stopping
    es_curves = len(df[(df['improvement'] == 'early_stopping') &
                       (df['metric'] == 'val_f1_improved')])
    print(f"  Early stopping: {es_curves} epoch points")
    print()

    # Overall summary
    print("=" * 60)
    print("✓ Verification Complete - Data is Ready!")
    print("=" * 60)
    print()
    print("You can now run:")
    print("  python generate_all_plots.py")
    print("  or")
    print("  ./generate_plots.sh")
    print()

    return True

if __name__ == '__main__':
    success = verify_results()
    sys.exit(0 if success else 1)
