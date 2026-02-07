#!/usr/bin/env python3
"""
Collect benign traffic data to retrain the model.

This script helps you collect real HTTP/HTTPS traffic from normal usage
to add to the training dataset, making the model work with real-world traffic.
"""

import numpy as np
import time

def collect_traffic_features():
    """
    Parse BPF trace output to extract flow features.

    Instructions:
    1. Run your XDP program: sudo ./src/.output/xdp
    2. Generate normal traffic from your phone (browse websites, etc)
    3. Capture the trace output: sudo cat /sys/kernel/debug/tracing/trace_pipe > trace.log
    4. Run this script to extract features from the log
    5. Add these as benign samples to the training data
    """

    print("To collect benign traffic data:")
    print("1. Start XDP program: sudo ./src/.output/xdp")
    print("2. Generate NORMAL traffic (web browsing, API calls, etc)")
    print("3. Capture logs: sudo cat /sys/kernel/debug/tracing/trace_pipe > benign_trace.log")
    print("4. Press Ctrl+C after collecting ~100 flows")
    print()
    print("Then run: python3 parse_trace_to_dataset.py benign_trace.log")
    print()
    print("This will create benign_data.npy which you can merge with the existing dataset")

def merge_datasets():
    """
    Merge new benign data with existing training data.
    """
    # Load original dataset
    original_data = np.load('../dataset/reproduction-xdp-data.npy')
    original_labels = np.load('../dataset/reproduction-xdp-label.npy')

    # Load new benign data
    benign_data = np.load('../dataset/benign_data.npy')
    benign_labels = np.zeros(len(benign_data), dtype=int)  # Label 0 = benign

    # Merge
    combined_data = np.vstack([original_data, benign_data])
    combined_labels = np.concatenate([original_labels, benign_labels])

    # Shuffle
    indices = np.random.permutation(len(combined_data))
    combined_data = combined_data[indices]
    combined_labels = combined_labels[indices]

    # Save
    np.save('../dataset/combined-data.npy', combined_data)
    np.save('../dataset/combined-label.npy', combined_labels)

    print(f"Original dataset: {len(original_data)} samples")
    print(f"Added benign samples: {len(benign_data)}")
    print(f"Combined dataset: {len(combined_data)} samples")
    print(f"Saved to: ../dataset/combined-data.npy")

if __name__ == '__main__':
    collect_traffic_features()
