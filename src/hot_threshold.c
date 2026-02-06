/*
 * hot_threshold.c - Runtime threshold updater for NN-eBPF intrusion detection
 *
 * This program allows updating the confidence threshold for attack detection
 * WITHOUT restarting the XDP program (zero downtime).
 *
 * USAGE:
 *   sudo ./hot_threshold <new_threshold>
 *   sudo ./hot_threshold --adaptive
 *
 * EXAMPLES:
 *   sudo ./hot_threshold 150000     # Default - medium sensitivity
 *   sudo ./hot_threshold 100000     # Lower threshold - more sensitive (more detections)
 *   sudo ./hot_threshold 200000     # Higher threshold - less sensitive (fewer false positives)
 *   sudo ./hot_threshold --adaptive # Automatically calculate optimal threshold from traffic
 *
 * THRESHOLD GUIDE (Q16.16 fixed-point format):
 *   50,000  - VERY HIGH sensitivity (may have false positives)
 *   100,000 - HIGH sensitivity (catches more attacks, some false positives)
 *   150,000 - MEDIUM sensitivity (balanced, recommended default)
 *   200,000 - LOW sensitivity (fewer false positives, may miss subtle attacks)
 *   500,000 - VERY LOW sensitivity (only extremely obvious attacks)
 *
 * ADAPTIVE MODE:
 * - Analyzes recent attack detections and benign traffic patterns
 * - Calculates threshold based on statistical analysis (mean + k*stddev)
 * - Adapts to your specific network environment
 * - Requires flow_map data from running XDP program
 *
 * HOW IT WORKS:
 * 1. Opens the pinned threshold_map from /sys/fs/bpf/threshold_map
 * 2. Reads the current threshold value
 * 3. Updates the map with the new threshold value
 * 4. All running XDP programs immediately use the new threshold
 *
 * WHY THIS IS USEFUL:
 * - Tune detection sensitivity based on real-time traffic patterns
 * - React to false positive/negative rates without downtime
 * - A/B test different thresholds in production
 * - Adjust based on time of day or threat level
 *
 * NOTE: The XDP program must be running for this to work (threshold_map must exist)
 */

#include <bpf/bpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include "common.h"

#define FLOW_MAP_PATH "/sys/fs/bpf/flow_map"
#define DEFAULT_THRESHOLD 150000

/*
 * Calculate optimal threshold using adaptive algorithm
 * Returns: calculated threshold value, or -1 on error
 */
static int32_t calculate_adaptive_threshold(void)
{
    int flow_map_fd;
    struct flow flow_key = {0};
    struct flow flow_next_key;
    struct flow_attribute attr;

    // Arrays to store margin values from flows
    #define MAX_SAMPLES 1000
    int32_t margins[MAX_SAMPLES];
    int sample_count = 0;

    // Open flow_map to analyze current flows
    printf("Opening flow map for adaptive analysis...\n");
    flow_map_fd = bpf_obj_get(FLOW_MAP_PATH);
    if (flow_map_fd < 0) {
        fprintf(stderr, "Warning: Could not open flow_map: %s\n", strerror(errno));
        fprintf(stderr, "Make sure XDP program is running.\n");
        fprintf(stderr, "Falling back to default threshold.\n");
        return DEFAULT_THRESHOLD;
    }

    printf("Analyzing flow statistics...\n");

    // Iterate through all flows in the map
    while (bpf_map_get_next_key(flow_map_fd, &flow_key, &flow_next_key) == 0) {
        // Look up flow attributes
        if (bpf_map_lookup_elem(flow_map_fd, &flow_next_key, &attr) == 0) {
            // Only consider flows that have been processed by NN
            // (check if hidden1 has meaningful values)
            if (attr.num_packet > 0 && sample_count < MAX_SAMPLES) {
                // Calculate margin: attack_score - benign_score
                // In the NN output: hidden1[1] = attack, hidden1[0] = benign
                int32_t benign_score = attr.hidden1[0];
                int32_t attack_score = attr.hidden1[1];
                int32_t margin = attack_score - benign_score;

                // Only include flows with meaningful scores (not all zeros)
                if (benign_score != 0 || attack_score != 0) {
                    margins[sample_count++] = margin;
                }
            }
        }

        flow_key = flow_next_key;
    }

    close(flow_map_fd);

    if (sample_count < 10) {
        printf("Not enough flow data for adaptive mode (found %d flows)\n", sample_count);
        printf("Recommendation: Run XDP for a while to collect traffic data,\n");
        printf("                or use a fixed threshold value.\n");
        printf("Falling back to default threshold: %d\n", DEFAULT_THRESHOLD);
        return DEFAULT_THRESHOLD;
    }

    // Calculate statistics
    printf("Analyzed %d flows with classification data\n", sample_count);

    // Calculate mean
    double sum = 0;
    for (int i = 0; i < sample_count; i++) {
        sum += margins[i];
    }
    double mean = sum / sample_count;

    // Calculate standard deviation
    double variance_sum = 0;
    for (int i = 0; i < sample_count; i++) {
        double diff = margins[i] - mean;
        variance_sum += diff * diff;
    }
    double std_dev = sqrt(variance_sum / sample_count);

    // Find min and max for reference
    int32_t min_margin = margins[0];
    int32_t max_margin = margins[0];
    for (int i = 1; i < sample_count; i++) {
        if (margins[i] < min_margin) min_margin = margins[i];
        if (margins[i] > max_margin) max_margin = margins[i];
    }

    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("              ADAPTIVE THRESHOLD ANALYSIS\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Flow samples analyzed:  %d\n", sample_count);
    printf("Mean margin:            %.0f\n", mean);
    printf("Std deviation:          %.0f\n", std_dev);
    printf("Min margin:             %d\n", min_margin);
    printf("Max margin:             %d\n", max_margin);
    printf("\n");

    // Calculate adaptive threshold
    // Strategy: Use mean + 1.0 * std_dev for balanced sensitivity
    // This catches flows that are 1 standard deviation above mean
    double k_factor = 1.0;  // Adjust this for sensitivity
    int32_t adaptive_threshold = (int32_t)(mean + k_factor * std_dev);

    // Clamp to reasonable range
    if (adaptive_threshold < 50000) {
        printf("Calculated threshold too low (%d), clamping to 50,000\n", adaptive_threshold);
        adaptive_threshold = 50000;
    } else if (adaptive_threshold > 1000000) {
        printf("Calculated threshold too high (%d), clamping to 1,000,000\n", adaptive_threshold);
        adaptive_threshold = 1000000;
    }

    printf("Adaptive algorithm:     mean + %.1f × std_dev\n", k_factor);
    printf("Calculated threshold:   %d\n", adaptive_threshold);
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    return adaptive_threshold;
}

int main(int argc, char **argv)
{
    int32_t new_threshold;
    int adaptive_mode = 0;

    // Check command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <threshold_value|--adaptive>\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s 150000     # Medium sensitivity (default)\n", argv[0]);
        fprintf(stderr, "  %s 100000     # Higher sensitivity\n", argv[0]);
        fprintf(stderr, "  %s 200000     # Lower sensitivity\n", argv[0]);
        fprintf(stderr, "  %s --adaptive # Auto-calculate from traffic patterns\n", argv[0]);
        fprintf(stderr, "\nThreshold Guide:\n");
        fprintf(stderr, "  50,000  - VERY HIGH sensitivity\n");
        fprintf(stderr, "  100,000 - HIGH sensitivity\n");
        fprintf(stderr, "  150,000 - MEDIUM sensitivity (recommended)\n");
        fprintf(stderr, "  200,000 - LOW sensitivity\n");
        fprintf(stderr, "  500,000 - VERY LOW sensitivity\n");
        fprintf(stderr, "\nAdaptive Mode:\n");
        fprintf(stderr, "  Analyzes current flow statistics and calculates optimal threshold\n");
        fprintf(stderr, "  based on mean + std_dev of confidence margins.\n");
        return 1;
    }

    // Check if adaptive mode is requested
    if (strcmp(argv[1], "--adaptive") == 0 || strcmp(argv[1], "-a") == 0) {
        adaptive_mode = 1;
        new_threshold = calculate_adaptive_threshold();
        if (new_threshold < 0) {
            return 1;  // Error already reported
        }
    } else {
        // Parse new threshold value
        new_threshold = atoi(argv[1]);
    }

    // Validate threshold range (sanity check)
    if (new_threshold < 0) {
        fprintf(stderr, "Error: Threshold must be positive (got %d)\n", new_threshold);
        return 1;
    }

    if (new_threshold > 10000000) {
        fprintf(stderr, "Warning: Threshold %d is extremely high (>10M)\n", new_threshold);
        fprintf(stderr, "This will likely block ALL attack detection. Continue? [y/N] ");
        char response;
        scanf("%c", &response);
        if (response != 'y' && response != 'Y') {
            printf("Aborted.\n");
            return 1;
        }
    }

    // Path to the pinned threshold map
    const char *threshold_map_path = "/sys/fs/bpf/threshold_map";

    // Open the pinned BPF map
    printf("Opening threshold map at %s...\n", threshold_map_path);
    int threshold_map_fd = bpf_obj_get(threshold_map_path);
    if (threshold_map_fd < 0) {
        fprintf(stderr, "Error: Failed to open threshold_map: %s\n", strerror(errno));
        fprintf(stderr, "Make sure the XDP program is running (it creates this map).\n");
        return 1;
    }
    printf("✓ Successfully opened threshold map (fd=%d)\n", threshold_map_fd);

    // Read current threshold value
    int32_t key = 0;
    int32_t current_threshold;
    int err = bpf_map_lookup_elem(threshold_map_fd, &key, &current_threshold);
    if (err) {
        fprintf(stderr, "Error: Failed to read current threshold: %s\n", strerror(errno));
        return 1;
    }

    // Display current vs new threshold
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("                 THRESHOLD UPDATE SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════\n");
    if (adaptive_mode) {
        printf("Mode:               ADAPTIVE (auto-calculated)\n");
    } else {
        printf("Mode:               MANUAL (user-specified)\n");
    }
    printf("Current threshold:  %d\n", current_threshold);
    printf("New threshold:      %d\n", new_threshold);
    printf("Change:             %+d (%s%.1f%%)\n",
           new_threshold - current_threshold,
           new_threshold > current_threshold ? "+" : "",
           ((float)(new_threshold - current_threshold) / current_threshold) * 100);
    printf("\n");

    // Explain impact
    if (new_threshold < current_threshold) {
        printf("Impact: HIGHER sensitivity (more detections, potential false positives)\n");
    } else if (new_threshold > current_threshold) {
        printf("Impact: LOWER sensitivity (fewer false positives, may miss subtle attacks)\n");
    } else {
        printf("No change (new threshold equals current threshold)\n");
    }
    printf("═══════════════════════════════════════════════════════════\n");
    printf("\n");

    // Update the threshold in the map
    printf("Updating threshold map...\n");
    err = bpf_map_update_elem(threshold_map_fd, &key, &new_threshold, BPF_ANY);
    if (err) {
        fprintf(stderr, "Error: Failed to update threshold: %s\n", strerror(errno));
        return 1;
    }

    // Log timestamp for tracking
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    printf("✓ Successfully updated threshold to %d\n", new_threshold);
    printf("✓ Timestamp: %s\n", timestamp);
    printf("\n");
    printf("The new threshold is now active!\n");
    printf("All flows will use threshold=%d starting NOW (no restart needed).\n", new_threshold);
    printf("\n");
    printf("Monitor detection results with:\n");
    printf("  sudo cat /sys/kernel/debug/tracing/trace_pipe\n");
    printf("\n");
    printf("Look for these log lines:\n");
    printf("  [THRESHOLD] Current detection threshold: %d\n", new_threshold);
    printf("  Confidence margin: <value> (threshold: %d)\n", new_threshold);
    printf("\n");

    // Optional: Log to file for audit trail
    FILE *log = fopen("/tmp/threshold_updates.log", "a");
    if (log) {
        fprintf(log, "[%s] Threshold updated: %d -> %d (change: %+d) [%s]\n",
                timestamp, current_threshold, new_threshold,
                new_threshold - current_threshold,
                adaptive_mode ? "ADAPTIVE" : "MANUAL");
        fclose(log);
        printf("✓ Logged update to /tmp/threshold_updates.log\n");
    }

    return 0;
}
