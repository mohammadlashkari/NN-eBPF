/*
 * common.h - Shared data structures for NN-eBPF intrusion detection
 * 
 * This file defines:
 * 1. flow_attribute: Statistics collected for each TCP connection
 * 2. flow: 5-tuple identifier for a network flow
 * 3. flow_map: Hash table storing flow statistics
 */

#ifndef COMMON_H
#define COMMON_H

/*
 * flow_attribute: Per-flow statistics and neural network state
 * 
 * This structure stores all the information about a TCP connection:
 * - Traffic statistics (packet counts, sizes, timings)
 * - Neural network inference state (intermediate layer outputs)
 * - Performance metrics (timing information)
 * 
 * Stored in flow_map, keyed by flow 5-tuple
 */
struct flow_attribute
{
    /* === TRAFFIC STATISTICS (Features for Neural Network) === */
    
    s64 num_packet;        // Total number of packets seen in this flow
                          // Used to: Identify flows with abnormal packet counts
                          // Normal: HTTP ~10-50, Attack: DDoS 1000+
    
    s64 min_packet_length; // Smallest packet size in bytes
                          // Used to: Detect fragmentation attacks
                          // Normal: Usually 60+ bytes, Attack: Could be tiny
    
    s64 max_packet_length; // Largest packet size in bytes (including headers)
                          // Used to: Detect data exfiltration or flooding
                          // Normal: ~1500 bytes (MTU), Attack: Could be unusual
    
    s64 max_duration;      // Maximum time gap between consecutive packets (nanoseconds)
                          // Used to: Detect slow attacks (slowloris, slow read)
                          // Normal: Milliseconds, Attack: Could be seconds
    
    s64 dst_port;          // Destination port number
                          // Used to: Identify service being targeted
                          // Common: 80 (HTTP), 443 (HTTPS), 22 (SSH)
    
    s64 header_length;     // Total TCP header bytes across all packets
                          // Used to: Detect header manipulation or overhead attacks
                          // Normal: 20-60 bytes/packet, Attack: Could vary
    
    /* === INTERNAL STATE (Not used as features) === */
    
    s64 last_packet_time;  // Timestamp of most recent packet (nanoseconds)
                          // Used to: Calculate inter-packet delays
    
    s64 status;            // Flow state (unused currently, reserved for future)
                          // Could track: NEW, ESTABLISHED, CLOSING, etc.
    
    /* === PERFORMANCE METRICS === */
    
    u64 total_feature_extraction_time;  // Cumulative time spent extracting features (ns)
                                       // Used to: Measure performance overhead
                                       // Divided by num_packet for average
    
    u64 detection_start_time;          // When neural network inference began (ns)
                                      // Used to: Measure NN inference latency
    
    /* === NEURAL NETWORK STATE === */
    
    s32 nn_idx;            // Which neural network model to use (0 or 1)
                          // Allows multiple models: normal vs high-security
                          // Set from nn_idx map
    
    s32 hidden1[32];       // First hidden layer output (32 neurons)
                          // After input layer: linear_layer() output
                          // After ReLU: non-negative values only
                          // Also stores final output (2 values):
                          //   hidden1[0] = BENIGN score
                          //   hidden1[1] = ATTACK score
    
    s32 hidden2[32];       // Second hidden layer output (32 neurons) 
                          // After normalization: stores normalized features (6 values)
                          // After hidden layer: stores intermediate results
                          // After ReLU: non-negative values only
};

/*
 * flow: Network flow identifier (5-tuple)
 * 
 * Uniquely identifies a TCP connection using:
 * - Source IP + Source Port
 * - Destination IP + Destination Port
 * - Protocol (always TCP in our case)
 * 
 * This structure is used as the KEY in flow_map
 */
struct flow
{
    int saddr;  // Source IP address (network byte order)
               // Example: 192.168.1.100 stored as integer
    
    int sport;  // Source port number (network byte order)
               // Example: 54321 (ephemeral port)
    
    int daddr;  // Destination IP address (network byte order)
               // Example: 93.184.216.34 (example.com)
    
    int dport;  // Destination port number (network byte order)
               // Example: 80 (HTTP) or 443 (HTTPS)
};

/*
 * flow_map: Hash table storing per-flow statistics
 * 
 * Type: BPF_MAP_TYPE_HASH
 * - Efficient lookup: O(1) average case
 * - Key: struct flow (5-tuple)
 * - Value: struct flow_attribute (statistics + NN state)
 * 
 * How it works:
 * 1. When packet arrives, extract flow tuple
 * 2. Look up flow in map: bpf_map_lookup_elem(&flow_map, &flow_key)
 * 3. If found: Update statistics (packet count, max length, etc.)
 * 4. If not found: Create new entry with initial values
 * 5. When FIN/RST: Use accumulated stats for NN classification
 * 
 * Why 8192 entries?
 * - Typical server handles 100-1000 concurrent connections
 * - 8192 allows headroom for burst traffic
 * - Each entry ~400 bytes: 8192 * 400 = ~3.2 MB total
 * - If map fills up, oldest entries evicted (LRU)
 */
#define MAX_PACKET_REACORD 8192

struct
{
    __uint(type, BPF_MAP_TYPE_HASH);        // Hash table for O(1) lookup
    __uint(max_entries, MAX_PACKET_REACORD); // Maximum concurrent flows
    __type(key, struct flow);                // 5-tuple identifier
    __type(value, struct flow_attribute);    // Statistics + NN state
} flow_map SEC(".maps");

/*
 * USAGE EXAMPLE:
 * 
 * // 1. Extract flow from packet
 * struct flow f = {.saddr = ip->saddr, .daddr = ip->daddr, ...};
 * 
 * // 2. Look up or create flow statistics
 * struct flow_attribute *attr = bpf_map_lookup_elem(&flow_map, &f);
 * if (!attr) {
 *     // New flow - initialize
 *     struct flow_attribute new_attr = {0};
 *     bpf_map_update_elem(&flow_map, &f, &new_attr, BPF_ANY);
 *     attr = bpf_map_lookup_elem(&flow_map, &f);
 * }
 * 
 * // 3. Update statistics
 * attr->num_packet++;
 * attr->max_packet_length = max(attr->max_packet_length, packet_len);
 * 
 * // 4. When flow ends, use stats for classification
 * if (tcp->fin || tcp->rst) {
 *     // attr now contains complete flow statistics
 *     // Pass to neural network for classification
 * }
 */

#endif // COMMON_H
