# $(BPFTOOL) $(LIBBPF_INCLUDE) $(LIBBPF_OBJ) from outside

OUTPUT ?= $(abspath .output)
CLANG = clang
LIBBPF_INCLUDE ?= $(abspath ../.output/libbpf/usr/include)
LIBBPF_SRC ?= $(abspath ../libbpf/include)
LIBBPF_OBJ ?= $(abspath ../.output/libbpf/libbpf.a)
BPFTOOL ?= $(abspath ../.output/bpftool/bpftool)

INCLUDES = -I$(OUTPUT) -I$(LIBBPF_INCLUDE) -I$(LIBBPF_SRC) -I/usr/include/x86_64-linux-gnu/
TARGET = $(OUTPUT)/xdp $(OUTPUT)/hot_updating $(OUTPUT)/hot_threshold
SRC = xdp.bpf.c $(OUTPUT)/vmlinux.h handler.bpf.h common.h mlp.bpf.h params.bpf.h

all: $(TARGET)

$(OUTPUT):
	mkdir -p "$(OUTPUT)"

$(OUTPUT)/hot_updating: hot_updating.c mlp_params.bpf.h | $(OUTPUT)
	$(CLANG) -g -Wall -lelf -lz $(INCLUDES) $< $(LIBBPF_OBJ) -o $@

$(OUTPUT)/hot_threshold: hot_threshold.c | $(OUTPUT)
	$(CLANG) -g -Wall -lelf -lz $(INCLUDES) $< $(LIBBPF_OBJ) -o $@

$(OUTPUT)/test-mlp: test-mlp.c mlp.bpf.h params.bpf.h | $(OUTPUT)
	$(CLANG) -O2 -g -Wall $(INCLUDES) $(LIBBPF_OBJ) $< -o $@

$(OUTPUT)/xdp: $(OUTPUT)/xdp.o | $(OUTPUT)
	$(CLANG) -O3 -g -Wall -lelf -lz $^ $(LIBBPF_OBJ) -o $@

$(OUTPUT)/xdp.o: xdp.c $(OUTPUT)/xdp.skel.h | $(OUTPUT)
	$(CLANG) -O3 -g -Wall $(INCLUDES) -c $< -o $@

$(OUTPUT)/xdp.skel.h: $(OUTPUT)/xdp.bpf.o | $(OUTPUT)
	$(BPFTOOL) gen skeleton $< > $@

$(OUTPUT)/xdp.bpf.o: $(SRC) | $(OUTPUT)
	$(CLANG) -g -O3 -target bpf $(INCLUDES) -c $< -o $@

$(OUTPUT)/vmlinux.h: | $(OUTPUT)
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

.PHONY: clean
clean:
	rm -rf $(OUTPUT)/*
