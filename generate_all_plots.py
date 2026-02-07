#!/usr/bin/env python3
"""
Master script to generate all improvement visualizations.

Usage:
    python generate_all_plots.py

This will generate all plots for all four improvements:
1. Activation Function (ReLU → LeakyReLU)
2. AdamW + Weight Decay
3. Loss Function (Cross-Entropy → Label Smoothing)
4. Early Stopping & Checkpointing
"""

import os
import sys

def run_script(script_name):
    """Run a visualization script and report status."""
    print(f"\n{'='*60}")
    print(f"Running: {script_name}")
    print('='*60)

    try:
        with open(script_name) as f:
            code = f.read()
        exec(code, {'__name__': '__main__'})
        print(f"✓ {script_name} completed successfully!")
        return True
    except FileNotFoundError:
        print(f"✗ Error: {script_name} not found!")
        return False
    except Exception as e:
        print(f"✗ Error running {script_name}: {e}")
        import traceback
        traceback.print_exc()
        return False

def main():
    print("=" * 60)
    print("NN-eBPF Improvement Visualizations Generator")
    print("=" * 60)

    # Check if results.csv exists
    if not os.path.exists('results.csv'):
        print("✗ Error: results.csv not found!")
        print("Please ensure results.csv is in the current directory.")
        sys.exit(1)

    print("✓ Found results.csv")

    # List of visualization scripts to run
    scripts = [
        'generate_activation_plots.py',
        'generate_adamw_plots.py',
        'generate_loss_plots.py',
        'generate_earlystop_plots.py'
    ]

    # Track results
    results = {}

    # Run each script
    for script in scripts:
        results[script] = run_script(script)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)

    success_count = sum(results.values())
    total_count = len(results)

    for script, success in results.items():
        status = "✓" if success else "✗"
        print(f"{status} {script}")

    print(f"\nCompleted: {success_count}/{total_count} scripts successful")

    if success_count == total_count:
        print("\n✓ All visualizations generated successfully!")
        print("\nGenerated files:")
        print("  - activation_f1_comparison.png")
        print("  - activation_training_loss.png")
        print("  - activation_metrics_table.png")
        print("  - adamw_f1_comparison.png")
        print("  - adamw_weights_robustness.png")
        print("  - adamw_training_curves.png")
        print("  - adamw_metrics_table.png")
        print("  - loss_f1_comparison.png")
        print("  - loss_training_curves.png")
        print("  - loss_calibration.png")
        print("  - loss_metrics_table.png")
        print("  - earlystop_training_curves.png")
        print("  - earlystop_efficiency.png")
        print("  - earlystop_metrics_table.png")
    else:
        print("\n⚠ Some visualizations failed to generate.")
        print("Check the error messages above for details.")
        sys.exit(1)

if __name__ == '__main__':
    main()
