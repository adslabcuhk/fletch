APP      := bench
SRCS     := main.c md5.c

DPDK_DIR   := /home/dell/crq/dpdk_env/dpdk-stable-20.11.6
DPDK_BUILD := $(DPDK_DIR)/build

export PKG_CONFIG_PATH := $(DPDK_BUILD)/meson-private

PKGCFLAGS := $(shell pkg-config --cflags libdpdk)
PKGCFLAGS := $(subst -include rte_config.h,-include $(DPDK_DIR)/config/rte_config.h,$(PKGCFLAGS))

DPDK_INCS := \
  -I$(DPDK_DIR)/lib/librte_eal/include \
  -I$(DPDK_DIR)/lib/librte_eal/include/generic \
  -I$(DPDK_DIR)/lib/librte_eal/linux/include \
  -I$(DPDK_DIR)/lib/librte_eal/x86/include \
  -I$(DPDK_DIR)/lib/librte_ethdev \
  -I$(DPDK_DIR)/lib/librte_mbuf \
  -I$(DPDK_DIR)/lib/librte_mempool \
  -I$(DPDK_DIR)/lib/librte_ring \
  -I$(DPDK_DIR)/lib/librte_net \
  -I$(DPDK_DIR)/lib/librte_pci \
  -I$(DPDK_DIR)/lib/librte_kvargs \
  -I$(DPDK_DIR)/lib/librte_meter \
  -I$(DPDK_DIR)/lib/librte_rcu \
  -I$(DPDK_DIR)/lib/librte_timer \
  -I$(DPDK_DIR)/lib/librte_hash \
  -I$(DPDK_DIR)/lib/librte_telemetry \
  -I$(DPDK_DIR)/drivers/bus/pci \
  -I$(DPDK_DIR)/config \
  -I$(DPDK_BUILD)

CFLAGS := $(DPDK_INCS) $(PKGCFLAGS) \
          -O3 -march=native -Wall -Wno-deprecated-declarations

LDFLAGS := -L$(DPDK_BUILD)/lib \
           -L$(DPDK_BUILD)/drivers \
           -Wl,-rpath,$(DPDK_BUILD)/lib \
           -Wl,-rpath,$(DPDK_BUILD)/drivers \
           -Wl,--whole-archive \
           $(DPDK_BUILD)/drivers/librte_mempool_ring.a \
           $(DPDK_BUILD)/drivers/librte_net_mlx5.a \
           $(DPDK_BUILD)/drivers/librte_common_mlx5.a \
           -Wl,--no-whole-archive \
           $(shell pkg-config --libs libdpdk) \
           -lm -lnuma -libverbs -lmlx5 -lpthread -lrte_pci -lrte_bus_pci

BUILDDIR := build

all: $(BUILDDIR)/$(APP)

$(BUILDDIR)/$(APP): $(SRCS) md5.h | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
