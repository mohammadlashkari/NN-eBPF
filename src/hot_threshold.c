/*
 * hot_threshold.c - Runtime threshold updater for NN-eBPF intrusion detection
 *
 * This program allows updating the confidence threshold for attack detection
 * WITHOUT restarting the XDP program (zero downtime).
 *
 * USAGE:
 *   sudo ./hot_threshold <new_threshold>
 *
 * EXAMPLES:
 *   sudo ./hot_threshold 150000  # Default - medium sensitivity
 *   sudo ./hot_threshold 100000  # Lower threshold - more sensitive (more detections)
 *   sudo ./hot_threshold 200000  # Higher threshold - less sensitive (fewer false positives)
 *   sudo ./hot_threshold 50000   # Very sensitive - catches borderline cases
 *   sudo ./hot_threshold 500000  # Very conservative - only obvious attacks
 *
 * THRESHOLD GUIDE (Q16.16 fixed-point format):
 *   50,000  - VERY HIGH sensitivity (may have false positives)
 *   100,000 - HIGH sensitivity (catches more attacks, some false positives)
 *   150,000 - MEDIUM sensitivity (balanced, recommended default)
 *   200,000 - LOW sensitivity (fewer false positives, may miss subtle attacks)
 *   500,000 - VERY LOW sensitivity (only extremely obvious attacks)
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

int main(int argc, char **argv)
{
    // Check command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <threshold_value>\n", argv[0]);
        fprintf(stderr, "\nExamples:\n");
        fprintf(stderr, "  %s 150000  # Medium sensitivity (default)\n", argv[0]);
        fprintf(stderr, "  %s 100000  # Higher sensitivity\n", argv[0]);
        fprintf(stderr, "  %s 200000  # Lower sensitivity\n", argv[0]);
        fprintf(stderr, "\nThreshold Guide:\n");
        fprintf(stderr, "  50,000  - VERY HIGH sensitivity\n");
        fprintf(stderr, "  100,000 - HIGH sensitivity\n");
        fprintf(stderr, "  150,000 - MEDIUM sensitivity (recommended)\n");
        fprintf(stderr, "  200,000 - LOW sensitivity\n");
        fprintf(stderr, "  500,000 - VERY LOW sensitivity\n");
        return 1;
    }

    // Parse new threshold value
    int32_t new_threshold = atoi(argv[1]);

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
        fprintf(log, "[%s] Threshold updated: %d -> %d (change: %+d)\n",
                timestamp, current_threshold, new_threshold,
                new_threshold - current_threshold);
        fclose(log);
        printf("✓ Logged update to /tmp/threshold_updates.log\n");
    }

    return 0;
}
