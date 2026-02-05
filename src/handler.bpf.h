#ifndef HANDLER_BPF_H
#define HANDLER_BPF_H

#include <vmlinux.h>
#include "common.h"
#include "mlp.bpf.h"

static inline void init_flow_attribute(struct flow_attribute *attr)
{
    attr->num_packet = 0;
    attr->min_packet_length = 0x3fffffffffffffffULL;
    attr->max_duration = 0;
    attr->max_packet_length = 0;
    attr->dst_port = 0;
    attr->header_length = 0;

    attr->last_packet_time = 0;
    attr->status = 0;
}

/*
 * update_flow_attribute: Update statistics for a TCP flow
 *
 * This function is called FOR EVERY PACKET in a monitored flow.
 * It incrementally builds up statistics that will be fed to the
 * neural network when the connection ends (FIN/RST).
 *
 * Parameters:
 *   - f: Flow 5-tuple (src_ip, src_port, dst_ip, dst_port)
 *   - tcp: TCP header from the packet
 *   - packet_length: Total packet size (including headers)
 *
 * What it tracks (6 features for ML):
 *   1. num_packet: Count of packets in this flow
 *   2. max_packet_length: Largest packet seen
 *   3. min_packet_length: Smallest packet seen
 *   4. max_duration: Longest gap between consecutive packets
 *   5. dst_port: Destination port (set once, first packet)
 *   6. header_length: Total TCP header bytes accumulated
 *
 * These features help identify attacks:
 *   - Slowloris: Many packets, long durations, small sizes
 *   - DDoS: Many packets, short durations
 *   - Port scan: Many flows to different ports, few packets each
 */
static inline void update_flow_attribute(struct flow *f, struct tcphdr *tcp, u64 packet_length)
{
    // Get current timestamp for duration calculations
    u64 packet_time = bpf_ktime_get_ns();

    // Start performance timer (measures feature extraction overhead)
    u64 extraction_start_time = bpf_ktime_get_ns();

    // Look up this flow's statistics in the hash map
    // Key = flow 5-tuple, Value = flow_attribute struct
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, f);

    // First packet in this flow? Initialize a new entry
    if (!attr_ptr)
    {
        struct flow_attribute attr = {};
        init_flow_attribute(&attr);

        // Set initial values
        attr.last_packet_time = packet_time;
        attr.dst_port = bpf_ntohs(tcp->dest);  // Convert from network to host byte order

        // Insert into map (BPF_NOEXIST = fail if already exists)
        bpf_map_update_elem(&flow_map, f, &attr, BPF_NOEXIST);

        // Re-lookup to get pointer to the inserted entry
        attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, f);
        if (!attr_ptr)
            return;  // Map full or other error - skip this packet
    }

    /*
     * ═══════════════════════════════════════════════════════════════
     *            UPDATE FLOW STATISTICS (Features for ML)
     * ═══════════════════════════════════════════════════════════════
     */

    // Feature 1: Increment packet count
    // Normal: 5-100 packets, Attack: Could be 1000+ (DDoS)
    attr_ptr->num_packet += 1;

    // Feature 2: Track minimum packet length
    // Normal: Usually 40+ bytes, Attack: Could be tiny (fragmentation)
    // Example: Kimiya's curl had min_len=20 (TCP ACK without payload)
    if (packet_length < attr_ptr->min_packet_length)
        attr_ptr->min_packet_length = packet_length;

    // Feature 3: Track maximum packet length
    // Normal: Up to MTU (~1500 bytes), Attack: Could be unusual
    // Example: Mohammad's curl had max_len=134 (small HTTP response)
    if (packet_length > attr_ptr->max_packet_length)
        attr_ptr->max_packet_length = packet_length;

    // Feature 4: Track maximum inter-packet duration
    // Normal: Milliseconds, Attack: Could be seconds (slowloris)
    // Example: Mohammad's curl had max_dur=7.9ms (fast, normal)
    u64 duration = packet_time - attr_ptr->last_packet_time;
    if (duration > attr_ptr->max_duration)
        attr_ptr->max_duration = duration;
    attr_ptr->last_packet_time = packet_time;  // Update for next packet

    // Feature 5: Accumulate TCP header length
    // tcp->doff = Data Offset (header length in 32-bit words)
    // Multiply by 4 to get bytes
    // Normal: 20-60 bytes per packet, Attack: Could vary
    // Example: Mohammad had hdr_len=168 over 5 packets = 33.6 bytes/packet
    attr_ptr->header_length += tcp->doff * 4;

    // Track performance: How much time spent extracting features
    attr_ptr->total_feature_extraction_time += bpf_ktime_get_ns() - extraction_start_time;

    /*
     * ═══════════════════════════════════════════════════════════════
     *              CONNECTION CLOSING - TRIGGER NN ANALYSIS
     * ═══════════════════════════════════════════════════════════════
     *
     * When we see FIN (normal close) or RST (abrupt close), the
     * connection is ending. This is the perfect time to analyze it:
     *   1. We have complete flow statistics
     *   2. Connection is done - no more packets expected
     *   3. Attacks often complete in one connection
     *
     * Setting detection_start_time triggers the neural network
     * inference in xdp_prog() via tail call to preprocessing.
     */
    if (tcp->fin || tcp->rst)
    {
        attr_ptr->detection_start_time = bpf_ktime_get_ns();
    }
}

#endif