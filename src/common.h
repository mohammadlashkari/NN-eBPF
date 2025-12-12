
#ifndef COMMON_H
#define COMMON_H

#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>
#include <vmlinux.h>

/* -----------------------------
 * Flow attribute structure
 * ----------------------------- */
struct flow_attribute {
	__s64 num_packet;
	__s64 min_packet_length;
	__s64 max_packet_length;
	__s64 max_duration;
	__s64 dst_port;
	__s64 header_length;

	__s64 last_packet_time;
	__s64 status;

	__u64 total_feature_extraction_time;
	__u64 detection_start_time;

	__s32 nn_idx;
	__s32 hidden1[32];
	__s32 hidden2[32];
};

/* -----------------------------
 * Flow key (tuple)
 * ----------------------------- */
struct flow {
	__u32 saddr;
	__u32 sport;
	__u32 daddr;
	__u32 dport;
};

/* -----------------------------
 * Flow map
 * ----------------------------- */
#define MAX_PACKET_REACORD 8192

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, MAX_PACKET_REACORD);
	__type(key, struct flow);
	__type(value, struct flow_attribute);
} flow_map SEC(".maps");

#endif /* COMMON_H */
