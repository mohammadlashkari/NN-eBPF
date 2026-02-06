#ifndef PARAMS_BPF_H
#define PARAMS_BPF_H

struct Net
{
	int32_t layer_0_weight[192];
	int32_t layer_1_weight[1024];
	int32_t layer_2_weight[64];
    int64_t mean[6];
    int64_t scale[6];
};

struct
{
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int32_t);
	__type(value, struct Net);
	__uint(max_entries, 2);
	__uint(pinning, LIBBPF_PIN_BY_NAME); // <- pin
} nn_parameters SEC(".maps");

struct
{
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int32_t);
	__type(value, int32_t);
	__uint(max_entries, 1);
	__uint(pinning, LIBBPF_PIN_BY_NAME); // <- pin
} nn_idx SEC(".maps");

/*
 * threshold_map: Stores the dynamic confidence threshold for attack detection
 *
 * This map allows runtime adjustment of detection sensitivity without restarting XDP.
 * - Key: 0 (single entry)
 * - Value: Confidence threshold in Q16.16 fixed-point format
 *
 * Usage:
 *   threshold = 150000 (default) → Medium sensitivity
 *   threshold = 100000 (lower)   → Higher sensitivity, more detections
 *   threshold = 200000 (higher)  → Lower sensitivity, fewer false positives
 *
 * Updated via hot_threshold userspace program without downtime.
 */
struct
{
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int32_t);
	__type(value, int32_t);
	__uint(max_entries, 1);
	__uint(pinning, LIBBPF_PIN_BY_NAME); // <- pin to /sys/fs/bpf/threshold_map
} threshold_map SEC(".maps");

#endif