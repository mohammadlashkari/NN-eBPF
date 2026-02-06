// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/*
 * attack_monitor.c - Userspace program for monitoring detected attacks
 *
 * This program reads attack events from the eBPF ring buffer and logs them
 * to a file with detailed metrics. Useful for security monitoring and analysis.
 *
 * Usage:
 *   sudo ./attack_monitor [log_file]
 *
 * Default log file: /tmp/nn-ebpf-attacks.log
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include "common.h"

#define DEFAULT_LOG_FILE "/tmp/nn-ebpf-attacks.log"
#define RINGBUF_MAP_PATH "/sys/fs/bpf/attack_events"

static volatile bool running = true;
static FILE *log_file = NULL;
static unsigned long attack_count = 0;

/*
 * Signal handler for graceful shutdown (Ctrl+C)
 */
static void sig_handler(int sig)
{
    running = false;
    printf("\n[!] Received signal %d, shutting down...\n", sig);
}

/*
 * Convert nanoseconds to human-readable time
 */
static void format_timestamp(char *buf, size_t bufsize)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, bufsize, "%Y-%m-%d %H:%M:%S", tm_info);
}

/*
 * Convert IP address to string (xxx.xxx.xxx.xxx)
 */
static void ip_to_str(uint32_t ip, char *buf, size_t bufsize)
{
    snprintf(buf, bufsize, "%u.%u.%u.%u",
             (ip >> 24) & 0xFF,
             (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF,
             ip & 0xFF);
}

/*
 * Get confidence level name
 */
static const char *confidence_str(uint8_t level)
{
    switch (level) {
        case 5: return "VERY_HIGH";
        case 4: return "HIGH";
        case 3: return "MEDIUM";
        case 2: return "LOW";
        case 1: return "VERY_LOW";
        default: return "UNKNOWN";
    }
}

/*
 * Callback function invoked for each attack event from ring buffer
 */
static int handle_event(void *ctx, void *data, size_t data_sz)
{
    struct attack_event *event = data;
    char timestamp[64];
    char src_ip_str[16], dst_ip_str[16];

    // Increment attack counter
    attack_count++;

    // Format timestamp and IP addresses
    format_timestamp(timestamp, sizeof(timestamp));
    ip_to_str(event->src_ip, src_ip_str, sizeof(src_ip_str));
    ip_to_str(event->dst_ip, dst_ip_str, sizeof(dst_ip_str));

    // Print to console (short summary)
    printf("\n[ATTACK #%lu] %s\n", attack_count, timestamp);
    printf("  Source:      %s:%u\n", src_ip_str, event->src_port);
    printf("  Target:      %s:%u\n", dst_ip_str, event->dst_port);
    printf("  Confidence:  %s (%u%%)\n",
           confidence_str(event->confidence_level), event->probability);
    printf("  Margin:      %d (threshold: %d)\n", event->margin, event->threshold);
    printf("  Packets:     %llu\n", (unsigned long long)event->num_packets);

    // Write detailed log to file
    if (log_file) {
        fprintf(log_file, "\n========================================\n");
        fprintf(log_file, "ATTACK DETECTED #%lu\n", attack_count);
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "Timestamp:       %s\n", timestamp);
        fprintf(log_file, "System Time:     %llu ns\n", (unsigned long long)event->timestamp);
        fprintf(log_file, "\n");

        fprintf(log_file, "--- NETWORK FLOW ---\n");
        fprintf(log_file, "Source IP:       %s\n", src_ip_str);
        fprintf(log_file, "Source Port:     %u\n", event->src_port);
        fprintf(log_file, "Destination IP:  %s\n", dst_ip_str);
        fprintf(log_file, "Destination Port: %u\n", event->dst_port);
        fprintf(log_file, "\n");

        fprintf(log_file, "--- DETECTION METRICS ---\n");
        fprintf(log_file, "Attack Score:    %d\n", event->attack_score);
        fprintf(log_file, "Benign Score:    %d\n", event->benign_score);
        fprintf(log_file, "Confidence Margin: %d\n", event->margin);
        fprintf(log_file, "Threshold Used:  %d\n", event->threshold);
        fprintf(log_file, "Attack Probability: %u%%\n", event->probability);
        fprintf(log_file, "Confidence Level: %s (%u/5)\n",
                confidence_str(event->confidence_level), event->confidence_level);
        fprintf(log_file, "\n");

        fprintf(log_file, "--- FLOW STATISTICS ---\n");
        fprintf(log_file, "Total Packets:   %llu\n", (unsigned long long)event->num_packets);
        fprintf(log_file, "Max Packet Size: %llu bytes\n", (unsigned long long)event->max_pkt_len);
        fprintf(log_file, "Min Packet Size: %llu bytes\n", (unsigned long long)event->min_pkt_len);
        fprintf(log_file, "Max Duration:    %llu ns (%.3f ms)\n",
                (unsigned long long)event->max_duration,
                event->max_duration / 1000000.0);
        fprintf(log_file, "Header Length:   %llu bytes\n", (unsigned long long)event->header_length);
        fprintf(log_file, "\n");

        fprintf(log_file, "--- PERFORMANCE ---\n");
        fprintf(log_file, "Detection Time:  %llu ns (%.3f μs)\n",
                (unsigned long long)event->detection_time,
                event->detection_time / 1000.0);
        fprintf(log_file, "\n");

        // Flush to ensure data is written immediately
        fflush(log_file);
    }

    return 0;
}

int main(int argc, char **argv)
{
    const char *log_path = DEFAULT_LOG_FILE;
    struct ring_buffer *rb = NULL;
    int ringbuf_fd;
    int err;

    // Parse command-line arguments
    if (argc > 1) {
        log_path = argv[1];
    }

    // Set up signal handlers
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("NN-eBPF Attack Monitor\n");
    printf("======================\n");
    printf("Log file: %s\n", log_path);
    printf("Press Ctrl+C to stop\n\n");

    // Open log file
    log_file = fopen(log_path, "a");
    if (!log_file) {
        fprintf(stderr, "ERROR: Failed to open log file '%s': %s\n",
                log_path, strerror(errno));
        fprintf(stderr, "Will print to console only.\n");
    } else {
        // Write startup header to log
        char timestamp[64];
        format_timestamp(timestamp, sizeof(timestamp));
        fprintf(log_file, "\n\n");
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "NN-eBPF ATTACK MONITOR STARTED\n");
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "Time: %s\n", timestamp);
        fprintf(log_file, "PID: %d\n", getpid());
        fprintf(log_file, "\n");
        fflush(log_file);
    }

    // Open the pinned ring buffer map
    ringbuf_fd = bpf_obj_get(RINGBUF_MAP_PATH);
    if (ringbuf_fd < 0) {
        fprintf(stderr, "ERROR: Failed to open ring buffer map at %s: %s\n",
                RINGBUF_MAP_PATH, strerror(-ringbuf_fd));
        fprintf(stderr, "\n");
        fprintf(stderr, "Make sure the XDP program is running first!\n");
        fprintf(stderr, "The ring buffer map should be created by the XDP loader.\n");
        err = 1;
        goto cleanup;
    }

    printf("[*] Connected to ring buffer map\n");
    printf("[*] Waiting for attack events...\n\n");

    // Create ring buffer consumer
    rb = ring_buffer__new(ringbuf_fd, handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "ERROR: Failed to create ring buffer\n");
        err = 1;
        goto cleanup;
    }

    // Main event loop: poll ring buffer for events
    while (running) {
        err = ring_buffer__poll(rb, 100 /* timeout_ms */);
        if (err == -EINTR) {
            // Interrupted by signal, continue
            err = 0;
            break;
        }
        if (err < 0) {
            fprintf(stderr, "ERROR: Polling ring buffer: %s\n", strerror(-err));
            break;
        }
    }

cleanup:
    // Clean up resources
    printf("\n[*] Shutting down...\n");
    printf("[*] Total attacks detected: %lu\n", attack_count);

    if (rb) {
        ring_buffer__free(rb);
    }

    if (log_file) {
        char timestamp[64];
        format_timestamp(timestamp, sizeof(timestamp));
        fprintf(log_file, "\n");
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "NN-eBPF ATTACK MONITOR STOPPED\n");
        fprintf(log_file, "========================================\n");
        fprintf(log_file, "Time: %s\n", timestamp);
        fprintf(log_file, "Total attacks logged: %lu\n", attack_count);
        fprintf(log_file, "\n");
        fclose(log_file);
    }

    return err != 0;
}
