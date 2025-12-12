// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2022 Hengqi Chen */
#include <vmlinux.h>
#include "handler.bpf.h"
#include "mlp.bpf.h"
#include "params.bpf.h"
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "common.h"

#define ETH_P_IP 0x0800 /* Internet Protocol packet	*/

struct
{
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1024);
	__type(key, u32);
	__type(value, u32);
} progs SEC(".maps");

static inline int flow_tuple(struct xdp_md *ctx, struct flow *f) {
	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;
	struct ethhdr *eth = data;
	if (eth + 1 > (struct ethhdr *)data_end) {
		return -1;
	}
	if (eth->h_proto != bpf_htons(ETH_P_IP)) {
		return -1;
	}
	struct iphdr *ip = data + sizeof(struct ethhdr);
	if (ip + 1 > (struct iphdr *)data_end) {
		return -1;
	}
	if (ip->protocol != IPPROTO_TCP) {
		return -1;
	}
	struct tcphdr *tcp = data + sizeof(struct ethhdr) + sizeof(struct iphdr);
	if (tcp + 1 > (struct tcphdr *)data_end) {
		return -1;
	}
	f->saddr = ip->saddr;
	f->daddr = ip->daddr;
	f->sport = tcp->source;
	f->dport = tcp->dest;
	return 0;
}

SEC("xdp")
int xdp_prog(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}

	void *data = (void *)(long)ctx->data;
	void *data_end = (void *)(long)ctx->data_end;

	// DEBUG: Print source IP of all TCP packets
	u32 saddr = bpf_ntohl(f.saddr);
	bpf_printk("TCP packet from: %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d",
			   (saddr >> 24) & 0xFF,
			   (saddr >> 16) & 0xFF,
			   (saddr >> 8) & 0xFF,
			   saddr & 0xFF,
			   bpf_ntohs(f.sport),
			   (bpf_ntohl(f.daddr) >> 24) & 0xFF,
			   (bpf_ntohl(f.daddr) >> 16) & 0xFF,
			   (bpf_ntohl(f.daddr) >> 8) & 0xFF,
			   bpf_ntohl(f.daddr) & 0xFF,
			   bpf_ntohs(f.dport));

	// MODIFIED: Removed IP filter - now processes ALL TCP traffic
	// Original filter was: if ((bpf_ntohl(f.saddr) == 555819337))
	// Now processes all TCP packets for testing
	{
		struct iphdr *ip = data + sizeof(struct ethhdr);
		if (ip + 1 > (struct iphdr *)data_end) {
			return XDP_PASS;
		}
		struct tcphdr *tcp = data + sizeof(struct ethhdr) + sizeof(struct iphdr);
		if (tcp + 1 > (struct tcphdr *)data_end) {
			return XDP_PASS;
		}
		u64 packet_length = bpf_ntohs(ip->tot_len) - ip->ihl * 4;
		update_flow_attribute(&f, tcp, packet_length);

		if (tcp->fin || tcp->rst) {
			bpf_printk("Flow ending (FIN/RST), starting NN inference...");

			int32_t idx = 0;
			int32_t *idx_ptr = bpf_map_lookup_elem(&nn_idx, &idx);
			struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
			if (idx_ptr && attr_ptr) {
				attr_ptr->nn_idx = *idx_ptr;
				bpf_printk("Tail calling to preprocessing (idx=%d)", *idx_ptr);
				bpf_tail_call(ctx, &progs, 0);
			} else {
				bpf_printk("ERROR: Could not find nn_idx or flow_attribute");
			}
		}
	}
	return XDP_PASS;
}

SEC("xdp")
int xdp_preprocessing(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;

	bpf_printk("[PREPROCESSING] Flow stats: max_len=%lld, min_len=%lld, packets=%lld",
			   attr_ptr->max_packet_length,
			   attr_ptr->min_packet_length,
			   attr_ptr->num_packet);

	struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
	if (!net) {
		bpf_printk("ERROR: Could not find NN parameters");
		return XDP_PASS;
	}

	int64_t x[6] = {attr_ptr->max_packet_length,
					attr_ptr->max_duration,
					attr_ptr->min_packet_length,
					attr_ptr->dst_port,
					attr_ptr->header_length,
					attr_ptr->num_packet};

	standard_scaler(x, attr_ptr->hidden2, net->mean, net->scale, 6);

	bpf_printk("[PREPROCESSING] Normalized features ready");
	bpf_tail_call(ctx, &progs, 1);
	return XDP_PASS;
}

SEC("xdp")
int xdp_test(struct xdp_md *ctx) {
	bpf_printk("Hello XDP! Packet received");
	return XDP_PASS;
}

SEC("xdp")
int xdp_input_linear(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;
	struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
	if (!net)
		return XDP_PASS;

	bpf_printk("[INPUT LINEAR] Computing input layer...");
	linear_layer(net->layer_0_weight, attr_ptr->hidden2, attr_ptr->hidden1, 32, 6);

	bpf_tail_call(ctx, &progs, 2);
	return XDP_PASS;
}

SEC("xdp")
int xdp_input_relu(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;

	bpf_printk("[INPUT RELU] Applying ReLU activation...");
	relu(attr_ptr->hidden1, 32);

	bpf_tail_call(ctx, &progs, 3);
	return XDP_PASS;
}

SEC("xdp")
int xdp_hidden_linear(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;
	struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
	if (!net)
		return XDP_PASS;

	bpf_printk("[HIDDEN LINEAR] Computing hidden layer...");
	linear_layer(net->layer_1_weight, attr_ptr->hidden1, attr_ptr->hidden2, 32, 32);

	bpf_tail_call(ctx, &progs, 4);
	return XDP_PASS;
}

SEC("xdp")
int xdp_hidden_relu(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;

	bpf_printk("[HIDDEN RELU] Applying ReLU activation...");
	relu(attr_ptr->hidden2, 32);

	bpf_tail_call(ctx, &progs, 5);
	return XDP_PASS;
}

SEC("xdp")
int xdp_output_linear(struct xdp_md *ctx) {
	struct flow f = {};
	if (flow_tuple(ctx, &f) < 0) {
		return XDP_PASS;
	}
	struct flow_attribute *attr_ptr = (struct flow_attribute *)bpf_map_lookup_elem(&flow_map, &f);
	if (!attr_ptr)
		return XDP_PASS;
	struct Net *net = bpf_map_lookup_elem(&nn_parameters, &(attr_ptr->nn_idx));
	if (!net)
		return XDP_PASS;

	bpf_printk("[OUTPUT LINEAR] Computing output layer...");
	linear_layer(net->layer_2_weight, attr_ptr->hidden2, attr_ptr->hidden1, 2, 32);

	// Determine the prediction
	int label = (attr_ptr->hidden1[0] > attr_ptr->hidden1[1]) ? 0 : 1;
	const char *result = label ? "⚠️  ATTACK DETECTED!" : "✓ BENIGN";

	bpf_printk("========================================");
	bpf_printk("INTRUSION DETECTION RESULT:");
	bpf_printk("  Score[BENIGN]: %d", attr_ptr->hidden1[0]);
	bpf_printk("  Score[ATTACK]: %d", attr_ptr->hidden1[1]);
	bpf_printk("  Classification: %s", result);
	bpf_printk("  Avg feature extraction time: %lld ns",
			   attr_ptr->total_feature_extraction_time / attr_ptr->num_packet);
	bpf_printk("  Detection time: %lld ns",
			   bpf_ktime_get_ns() - attr_ptr->detection_start_time);
	bpf_printk("========================================");

	return XDP_PASS;
}

char __license[] SEC("license") = "GPL";
