// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2022 Hengqi Chen */
#include <vmlinux.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "params.bpf.h"
#include "common.h"
#include "handler.bpf.h"
#include "mlp.bpf.h"

#define ETH_P_IP 0x0800 /* Internet Protocol packet */

/* 
 * BPF_MAP_TYPE_PROG_ARRAY: Tail call map for chaining eBPF programs
 * This allows breaking the neural network inference into smaller programs
 * to avoid eBPF's instruction limit (max ~1 million instructions per program)
 */
struct
{
    __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
    __uint(max_entries, 1024);
    __type(key, u32);
    __type(value, u32);
} progs SEC(".maps");

/*
 * flow_tuple: Extract network flow information from packet
 * Returns: 0 on success, -1 if packet doesn't match criteria
 * 
 * This function:
 * 1. Validates packet boundaries (required by eBPF verifier)
 * 2. Checks if packet is IPv4
 * 3. Checks if packet is TCP
 * 4. Extracts source/dest IP and ports into flow structure
 */
static inline int flow_tuple(struct xdp_md *ctx, struct flow *f)
{
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    
    // Parse Ethernet header
    struct ethhdr *eth = data;
    if (eth + 1 > (struct ethhdr *)data_end)
    {
        return -1; // Packet too small
    }
    
    // Only process IPv4 packets (skip IPv6, ARP, etc.)
    if (eth->h_proto != bpf_htons(ETH_P_IP))
    {
        return -1;
    }
    
    // Parse IP header
    struct iphdr *ip = data + sizeof(struct ethhdr);
    if (ip + 1 > (struct iphdr *)data_end)
    {
        return -1; // IP header incomplete
    }
    
    // Only process TCP packets (skip UDP, ICMP, etc.)
    if (ip->protocol != IPPROTO_TCP)
    {
        return -1;
    }
    
    // Parse TCP header
    struct tcphdr *tcp = data + sizeof(struct ethhdr) + sizeof(struct iphdr);
    if (tcp + 1 > (struct tcphdr *)data_end)
    {
        return -1; // TCP header incomplete
    }
    
    // Extract flow tuple (5-tuple: src_ip, src_port, dst_ip, dst_port, protocol)
    f->saddr = ip->saddr;
    f->daddr = ip->daddr;
    f->sport = tcp->source;
    f->dport = tcp->dest;
    
    return 0;
}

/*
 * xdp_prog: Main entry point for XDP packet processing
 * 
 * This is the FIRST function called for every packet.
 * It decides whether to analyze the packet with the neural network.
 * 
 * Flow:
 * 1. Extract flow information
 * 2. Check if packet matches criteria (currently filtered by source IP)
 * 3. Update flow statistics in flow_map
 * 4. If connection is closing (FIN/RST), trigger neural network analysis
 */
SEC("xdp")
int xdp_prog(struct xdp_md *ctx)
{
    struct flow f = {};
    
    // Extract flow tuple from packet
    if (flow_tuple(ctx, &f) < 0)
    {
        // Not TCP or malformed packet - allow it through
        return XDP_PASS;
    }
    
    void *data = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;
    
    /* 
     * FILTER: Only process packets from specific source IP
     * 33.33.33.73 (decimal) == 555819337 (integer representation)
     * 
     * WHY THIS FILTER EXISTS:
     * - In testing/research environments, you control both client and server
     * - The test scripts in dataset-instance/ are configured to use this IP
     * - This prevents analyzing random internet traffic during testing
     * 
     * TO REMOVE FILTER: Change this line to: if (1) {
     * This will analyze ALL TCP traffic
     */
    
    // Convert source IP to human-readable format for logging
    u32 src_ip = bpf_ntohl(f.saddr);
    
    // OPTION 1: Filter by specific IP (current behavior)
    if (1) // 33.33.33.73
    {
        bpf_printk("==> Packet from monitored IP: %d.%d.%d.%d:%d",
                   (src_ip >> 24) & 0xFF,
                   (src_ip >> 16) & 0xFF,
                   (src_ip >> 8) & 0xFF,
                   src_ip & 0xFF,
                   bpf_ntohs(f.sport));
        
        // Re-validate packet boundaries (required after any operations)
        struct iphdr *ip = data + sizeof(struct ethhdr);
        if (ip + 1 > (struct iphdr *)data_end)
        {
            return XDP_PASS;
        }
        
        struct tcphdr *tcp = data + sizeof(struct ethhdr) + sizeof(struct iphdr);
        if (tcp + 1 > (struct tcphdr *)data_end)
        {
            return XDP_PASS;
        }
        
        // Calculate packet payload length (excluding headers)
        u64 packet_length = bpf_ntohs(ip->tot_len) - ip->ihl * 4;
        
        // Update flow statistics in the flow_map
        // This tracks: packet count, min/max lengths, timings, etc.
        update_flow_attribute(&f, tcp, packet_length);
        
        /*
         * Check if TCP connection is closing (FIN or RST flag set)
         * This is when we trigger neural network analysis because:
         * 1. We have complete flow statistics
         * 2. Connection is ending - good time to classify it
         * 3. Attacks often complete in one connection
         */
        if (tcp->fin || tcp->rst)
        {
            bpf_printk("==> Flow ending (FIN/RST) - Starting NN inference...");
            
            // Get neural network index (which model to use)
            int32_t idx = 0;
            int32_t *idx_ptr = bpf_map_lookup_elem(&nn_idx, &idx);
            
            // Get flow statistics we've been collecting
            struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
            
            if (idx_ptr && attr_ptr)
            {
                // Store which NN model to use
                attr_ptr->nn_idx = *idx_ptr;
                
                bpf_printk("==> Packets in flow: %lld, Starting preprocessing...", attr_ptr->num_packet);
                
                /*
                 * TAIL CALL to xdp_preprocessing
                 * This jumps to the next program in the chain
                 * Index 0 = xdp_preprocessing (defined in xdp.c loader)
                 * 
                 * NOTE: After tail_call, this function NEVER returns!
                 * Execution continues in the called program
                 */
                bpf_tail_call(ctx, &progs, 0);
                
                // This line only executes if tail call fails
                bpf_printk("ERROR: Tail call to preprocessing failed!");
            }
            else
            {
                bpf_printk("ERROR: Could not find nn_idx or flow_attribute in maps");
            }
        }
    }
    // OPTION 2: To monitor ALL TCP traffic, uncomment these lines and comment the above if block:
    /*
    else
    {
        // Log every 100th packet to avoid spam
        static u32 packet_count = 0;
        packet_count++;
        if (packet_count % 100 == 0)
        {
            bpf_printk("Processed %u packets (not from monitored IP)", packet_count);
        }
    }
    */
    
    return XDP_PASS; // Allow packet to continue to network stack
}

/*
 * xdp_preprocessing: Prepare flow data for neural network
 * 
 * This function is called via tail_call from xdp_prog when a flow ends.
 * 
 * Steps:
 * 1. Retrieve flow statistics from flow_map
 * 2. Retrieve neural network parameters (mean, scale)
 * 3. Normalize input features using standard scaler
 * 4. Chain to next stage (input layer)
 */
SEC("xdp")
int xdp_preprocessing(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    // Get flow statistics
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in preprocessing");
        return XDP_PASS;
    }
    
    bpf_printk("[PREPROCESSING] Flow: %d->%d, packets=%lld",
               bpf_ntohs(f.sport),
               bpf_ntohs(f.dport),
               attr_ptr->num_packet);
    
    /* COMMENTED PRINTS were for detailed debugging of individual features
     * They show the exact values being fed into the neural network
     * Uncomment them if you need to debug why classification is wrong
     */
    bpf_printk("[PREPROCESSING] Features: max_len=%lld, min_len=%lld, duration=%lld",
               attr_ptr->max_packet_length,
               attr_ptr->min_packet_length,
               attr_ptr->max_duration);
    
    bpf_printk("[PREPROCESSING] port=%lld, hdr_len=%lld",
               attr_ptr->dst_port,
               attr_ptr->header_length);
    
    // Get neural network parameters (weights, mean, scale)
    struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
    if (!net)
    {
        bpf_printk("ERROR: NN parameters not found (idx=%d)", attr_ptr->nn_idx);
        return XDP_PASS;
    }
    
    /*
     * Prepare input features (6 features total):
     * 1. max_packet_length: Largest packet in flow
     * 2. max_duration: Longest time between packets
     * 3. min_packet_length: Smallest packet in flow  
     * 4. dst_port: Destination port number
     * 5. header_length: Total TCP header bytes
     * 6. num_packet: Total packets in flow
     * 
     * These features help identify attacks:
     * - Slowloris: Many small packets, long duration
     * - DDoS: Many packets, short duration
     * - Port scan: Many different ports, few packets each
     */
    int64_t x[6] = {
        attr_ptr->max_packet_length,
        attr_ptr->max_duration,
        attr_ptr->min_packet_length,
        attr_ptr->dst_port,
        attr_ptr->header_length,
        attr_ptr->num_packet
    };
    
    /*
     * Standard Scaler: Normalize features
     * Formula: x_normalized = (x - mean) / scale
     * 
     * Why? Neural networks work best when inputs are normalized
     * Without this, large values (like num_packet=10000) would dominate
     * small values (like dst_port=80)
     */
    standard_scaler(x, attr_ptr->hidden2, net->mean, net->scale, 6);
    
    bpf_printk("[PREPROCESSING] Normalization complete, chaining to input layer...");
    
    /* COMMENTED PRINT shows normalized values
     * Useful for debugging if NN gives unexpected results
     * Values should typically be between -3 and +3 after normalization
     */
    // bpf_printk("Normalized: %d %d %d %d %d %d",
    //            attr_ptr->hidden2[0], attr_ptr->hidden2[1], attr_ptr->hidden2[2],
    //            attr_ptr->hidden2[3], attr_ptr->hidden2[4], attr_ptr->hidden2[5]);
    
    // Chain to input layer (index 1 = xdp_input_linear)
    bpf_tail_call(ctx, &progs, 1);
    
    bpf_printk("ERROR: Tail call to input_linear failed!");
    return XDP_PASS;
}

/*
 * xdp_input_linear: First layer of neural network (6 -> 32 neurons)
 * 
 * Performs matrix multiplication: output = weights * input + bias
 * Takes 6 normalized features and produces 32 intermediate values
 */
SEC("xdp")
int xdp_input_linear(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in input_linear");
        return XDP_PASS;
    }
    
    struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
    if (!net)
    {
        bpf_printk("ERROR: NN parameters not found in input_linear");
        return XDP_PASS;
    }
    
    bpf_printk("[INPUT LINEAR] Computing layer 0: 6 inputs -> 32 neurons");
    
    /*
     * Linear transformation: y = Wx + b
     * - W (weights): 6x32 matrix from layer_0_weight
     * - x (input): 6 normalized features in hidden2
     * - y (output): 32 values in hidden1
     */
    linear_layer(net->layer_0_weight, attr_ptr->hidden2, attr_ptr->hidden1, 32, 6);
    
    /* COMMENTED PRINT shows first 6 output values
     * Useful for debugging if this layer produces all zeros or NaN
     */
    // bpf_printk("Layer 0 output: %d %d %d %d %d %d",
    //            attr_ptr->hidden1[0], attr_ptr->hidden1[1], attr_ptr->hidden1[2],
    //            attr_ptr->hidden1[3], attr_ptr->hidden1[4], attr_ptr->hidden1[5]);
    
    // Chain to ReLU activation (index 2 = xdp_input_relu)
    bpf_tail_call(ctx, &progs, 2);
    
    bpf_printk("ERROR: Tail call to input_relu failed!");
    return XDP_PASS;
}

/*
 * xdp_input_relu: Apply ReLU activation to first layer
 * 
 * ReLU (Rectified Linear Unit): f(x) = max(0, x)
 * This introduces non-linearity, allowing NN to learn complex patterns
 * Without activation functions, multiple layers = single layer
 */
SEC("xdp")
int xdp_input_relu(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in input_relu");
        return XDP_PASS;
    }
    
    bpf_printk("[INPUT RELU] Applying activation function (ReLU)...");
    
    /*
     * Apply ReLU: Replace all negative values with 0
     * Positive values stay the same
     * This helps NN focus on relevant features and ignore noise
     */
    relu(attr_ptr->hidden1, 32);
    
    /* COMMENTED PRINT shows values after ReLU
     * All values should be >= 0 after this
     * If you see negative values, there's a bug in relu()
     */
    // bpf_printk("After ReLU: %d %d %d %d %d %d",
    //            attr_ptr->hidden1[0], attr_ptr->hidden1[1], attr_ptr->hidden1[2],
    //            attr_ptr->hidden1[3], attr_ptr->hidden1[4], attr_ptr->hidden1[5]);
    
    // Chain to hidden layer (index 3 = xdp_hidden_linear)
    bpf_tail_call(ctx, &progs, 3);
    
    bpf_printk("ERROR: Tail call to hidden_linear failed!");
    return XDP_PASS;
}

/*
 * xdp_hidden_linear: Second layer of neural network (32 -> 32 neurons)
 * 
 * This is the "thinking" layer that learns complex attack patterns
 * Takes 32 values from previous layer, produces 32 new values
 */
SEC("xdp")
int xdp_hidden_linear(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in hidden_linear");
        return XDP_PASS;
    }
    
    struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
    if (!net)
    {
        bpf_printk("ERROR: NN parameters not found in hidden_linear");
        return XDP_PASS;
    }
    
    bpf_printk("[HIDDEN LINEAR] Computing layer 1: 32 -> 32 neurons");
    
    /*
     * Linear transformation: y = Wx + b
     * - W (weights): 32x32 matrix from layer_1_weight
     * - x (input): 32 values in hidden1
     * - y (output): 32 values in hidden2
     */
    linear_layer(net->layer_1_weight, attr_ptr->hidden1, attr_ptr->hidden2, 32, 32);
    
    /* COMMENTED PRINT shows first 6 values after hidden layer */
    // bpf_printk("Layer 1 output: %d %d %d %d %d %d",
    //            attr_ptr->hidden2[0], attr_ptr->hidden2[1], attr_ptr->hidden2[2],
    //            attr_ptr->hidden2[3], attr_ptr->hidden2[4], attr_ptr->hidden2[5]);
    
    // Chain to ReLU activation (index 4 = xdp_hidden_relu)
    bpf_tail_call(ctx, &progs, 4);
    
    bpf_printk("ERROR: Tail call to hidden_relu failed!");
    return XDP_PASS;
}

/*
 * xdp_hidden_relu: Apply ReLU activation to hidden layer
 */
SEC("xdp")
int xdp_hidden_relu(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in hidden_relu");
        return XDP_PASS;
    }
    
    bpf_printk("[HIDDEN RELU] Applying activation function (ReLU)...");
    
    relu(attr_ptr->hidden2, 32);
    
    /* COMMENTED PRINT shows values after second ReLU */
    // bpf_printk("After ReLU: %d %d %d %d %d %d",
    //            attr_ptr->hidden2[0], attr_ptr->hidden2[1], attr_ptr->hidden2[2],
    //            attr_ptr->hidden2[3], attr_ptr->hidden2[4], attr_ptr->hidden2[5]);
    
    // Chain to output layer (index 5 = xdp_output_linear)
    bpf_tail_call(ctx, &progs, 5);
    
    bpf_printk("ERROR: Tail call to output_linear failed!");
    return XDP_PASS;
}

/*
 * xdp_output_linear: Final layer - makes the decision (32 -> 2 neurons)
 * 
 * This is the FINAL stage that produces the classification:
 * - Output[0] = confidence that traffic is BENIGN
 * - Output[1] = confidence that traffic is ATTACK
 * 
 * Whichever score is higher determines the classification
 */
SEC("xdp")
int xdp_output_linear(struct xdp_md *ctx)
{
    struct flow f = {};
    if (flow_tuple(ctx, &f) < 0)
    {
        return XDP_PASS;
    }
    
    struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
    if (!attr_ptr)
    {
        bpf_printk("ERROR: Flow not found in output_linear");
        return XDP_PASS;
    }
    
    struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
    if (!net)
    {
        bpf_printk("ERROR: NN parameters not found in output_linear");
        return XDP_PASS;
    }
    
    bpf_printk("[OUTPUT LINEAR] Computing layer 2: 32 -> 2 outputs (BENIGN vs ATTACK)");
    
    /*
     * Final linear layer: y = Wx + b
     * - W (weights): 32x2 matrix from layer_2_weight
     * - x (input): 32 values in hidden2
     * - y (output): 2 values in hidden1
     *   - hidden1[0] = score for BENIGN
     *   - hidden1[1] = score for ATTACK
     */
    linear_layer(net->layer_2_weight, attr_ptr->hidden2, attr_ptr->hidden1, 2, 32);
    
    /*
     * Make classification decision
     * If hidden1[0] > hidden1[1] -> BENIGN (label = 0)
     * If hidden1[1] > hidden1[0] -> ATTACK (label = 1)
     */
    int label = (attr_ptr->hidden1[0] > attr_ptr->hidden1[1]) ? 0 : 1;
    
    // Calculate performance metrics
    u64 avg_feature_time = attr_ptr->total_feature_extraction_time / attr_ptr->num_packet;
    u64 detection_time = bpf_ktime_get_ns() - attr_ptr->detection_start_time;
    
    /*
     * MAIN OUTPUT - This is what you want to see!
     * Shows the classification result and confidence scores
     */
    bpf_printk("========================================");
    bpf_printk("*** INTRUSION DETECTION RESULT ***");
    bpf_printk("Flow: %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d",
               (bpf_ntohl(f.saddr) >> 24) & 0xFF,
               (bpf_ntohl(f.saddr) >> 16) & 0xFF,
               (bpf_ntohl(f.saddr) >> 8) & 0xFF,
               bpf_ntohl(f.saddr) & 0xFF,
               bpf_ntohs(f.sport),
               (bpf_ntohl(f.daddr) >> 24) & 0xFF,
               (bpf_ntohl(f.daddr) >> 16) & 0xFF,
               (bpf_ntohl(f.daddr) >> 8) & 0xFF,
               bpf_ntohl(f.daddr) & 0xFF,
               bpf_ntohs(f.dport));
    bpf_printk("Packets in flow: %lld", attr_ptr->num_packet);
    bpf_printk("Score [BENIGN]: %d", attr_ptr->hidden1[0]);
    bpf_printk("Score [ATTACK]: %d", attr_ptr->hidden1[1]);
    bpf_printk("Classification: %s", label ? "*** ATTACK DETECTED ***" : "BENIGN (Normal Traffic)");
    bpf_printk("Avg feature extraction: %lld ns", avg_feature_time);
    bpf_printk("Total detection time: %lld ns", detection_time);
    bpf_printk("========================================");
    
    /*
     * NOTE: This currently returns XDP_PASS (allows packet)
     * In production, you might want to:
     * - return XDP_DROP if label == 1 (block attacks)
     * - return XDP_PASS if label == 0 (allow benign)
     * 
     * For testing, we always PASS to see all results
     */
    return XDP_PASS;
}

char __license[] SEC("license") = "GPL";
