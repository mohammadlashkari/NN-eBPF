#include "xdp.skel.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h> // Add this for XDP_FLAGS_SKB_MODE
#include <net/if.h>
#include <signal.h>
#include <unistd.h>

static volatile bool exiting = false;

static int libbpf_print_fn(enum libbpf_print_level level, const char *format, va_list args) {
	return vfprintf(stderr, format, args);
}

static void sig_handler(int sig) {
	exiting = true;
}

struct bpf_progs_desc {
	char name[256];
	enum bpf_prog_type type;
	int map_prog_idx;
	struct bpf_program *prog;
};

static struct bpf_progs_desc progs[] = {
	{"xdp_preprocessing", BPF_PROG_TYPE_XDP, 0, NULL},
	{"xdp_input_linear", BPF_PROG_TYPE_XDP, 1, NULL},
	{"xdp_input_relu", BPF_PROG_TYPE_XDP, 2, NULL},
	{"xdp_hidden_linear", BPF_PROG_TYPE_XDP, 3, NULL},
	{"xdp_hidden_relu", BPF_PROG_TYPE_XDP, 4, NULL},
	{"xdp_output_linear", BPF_PROG_TYPE_XDP, 5, NULL},
};

int main(int argc, char **argv) {
	// FIXED: Accept interface from command line or use default
	const char *interface = "wlan0"; // Default to wlan0
	if (argc > 1) {
		interface = argv[1];
	}

	printf("Using interface: %s\n", interface);

	unsigned int ifindex = if_nametoindex(interface);
	if (ifindex == 0) {
		fprintf(stderr, "Error: Interface %s not found\n", interface);
		return 1;
	}

	printf("Interface index: %u\n", ifindex);

	/* Set up libbpf errors and debug info callback */
	libbpf_set_print(libbpf_print_fn);

	/* Cleaner handling of Ctrl-C */
	signal(SIGINT, sig_handler);

	struct xdp_bpf *skel;
	int map_progs_fd, prog_count;
	int err;

	skel = xdp_bpf__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	err = xdp_bpf__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load and verify BPF skeleton\n");
		goto cleanup;
	}

	map_progs_fd = bpf_object__find_map_fd_by_name(skel->obj, "progs");
	prog_count = sizeof(progs) / sizeof(progs[0]);

	for (int i = 0; i < prog_count; i++) {
		progs[i].prog = bpf_object__find_program_by_name(skel->obj, progs[i].name);
		if (!progs[i].prog) {
			fprintf(stderr, "Error: bpf_object__find_program_by_name failed\n");
			return 1;
		}
		bpf_program__set_type(progs[i].prog, progs[i].type);
	}

	for (int i = 0; i < prog_count; i++) {
		int prog_fd = bpf_program__fd(progs[i].prog);
		if (prog_fd < 0) {
			fprintf(stderr, "Error: Couldn't get file descriptor for program %s\n", progs[i].name);
			return 1;
		}
		unsigned int map_prog_idx = progs[i].map_prog_idx;
		if (map_prog_idx < 0) {
			fprintf(stderr, "Error: Cannot get prog fd for bpf program %s\n", progs[i].name);
			return 1;
		}
		// 给 progs map 的 map_prog_idx 插入 prog_fd
		err = bpf_map_update_elem(map_progs_fd, &map_prog_idx, &prog_fd, 0);
		if (err) {
			fprintf(stderr, "Error: bpf_map_update_elem failed for prog array map\n");
			return 1;
		}
	}

	//  update nn index
	int32_t nn_idx = 0;
	int32_t nn_idx_value = 1;
	err = bpf_map__update_elem(skel->maps.nn_idx, &nn_idx, sizeof(int32_t), &nn_idx_value, sizeof(int32_t), BPF_ANY);
	if (err) {
		fprintf(stderr, "Error: updating nn index\n");
		return 1;
	}

	// Initialize confidence threshold map
	// Default threshold: 150000 (Q16.16 fixed-point) ≈ 2.3 in float
	// This provides medium sensitivity - tested to filter benign curl while catching attacks
	int32_t threshold_key = 0;
	int32_t threshold_value = 150000;
	err = bpf_map__update_elem(skel->maps.threshold_map, &threshold_key, sizeof(int32_t), &threshold_value, sizeof(int32_t), BPF_ANY);
	if (err) {
		fprintf(stderr, "Error: initializing threshold map\n");
		return 1;
	}
	printf("✓ Initialized detection threshold: %d (default)\n", threshold_value);

	// FIXED: Use generic/SKB mode for WiFi compatibility
	LIBBPF_OPTS(bpf_xdp_attach_opts, attach_opts);
	attach_opts.old_prog_fd = -1;

	// Try native mode first, fall back to generic mode
	int prog_fd = bpf_program__fd(skel->progs.xdp_prog);

	// First try native mode
	printf("Attempting to attach XDP in native mode...\n");
	err = bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_DRV_MODE, &attach_opts);

	if (err) {
		// If native fails, try generic/SKB mode (works on all interfaces including WiFi)
		printf("Native mode failed, trying generic/SKB mode (compatible with WiFi)...\n");
		err = bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_SKB_MODE, &attach_opts);

		if (err) {
			fprintf(stderr, "Error: Failed to attach XDP program in any mode: %s\n", strerror(-err));
			goto cleanup;
		}
		printf("✓ XDP attached successfully in GENERIC/SKB mode\n");
	} else {
		printf("✓ XDP attached successfully in NATIVE mode\n");
	}

	printf("\nSuccessfully started! Please run `sudo cat /sys/kernel/debug/tracing/trace_pipe` "
		   "to see output of the BPF programs.\n");
	printf("Interface: %s (index: %u)\n", interface, ifindex);
	printf("PID: %d\n", getpid());
	printf("\nPress Ctrl+C to stop...\n\n");

	while (!exiting) {
		fprintf(stderr, ".");
		sleep(60);
	}

cleanup:
	printf("\nCleaning up...\n");

	// Detach XDP program
	bpf_xdp_detach(ifindex, XDP_FLAGS_SKB_MODE | XDP_FLAGS_DRV_MODE, NULL);

	// delete pinned map
	remove("/sys/fs/bpf/nn_parameters");
	remove("/sys/fs/bpf/nn_idx");
	remove("/sys/fs/bpf/threshold_map");

	xdp_bpf__destroy(skel);

	printf("Done.\n");
	return err < 0 ? -err : 0;
}
