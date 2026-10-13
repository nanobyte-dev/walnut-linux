# PS3 Linux build. Settings are in config.mk; run `make help` for targets.
include config.mk

JOBS        ?= $(shell nproc)
UID_GID     := $(shell id -u):$(shell id -g)

BUILD       := build
DOWNLOADS   := $(BUILD)/downloads
KERNEL_TARBALL := $(DOWNLOADS)/linux-$(KERNEL_VERSION).tar.xz
KERNEL_STAGE   := $(BUILD)/kernel/stage
ROOTFS_TAR     := $(BUILD)/rootfs/debian-ppc64.tar

# Builder image tag changes whenever its inputs change
BUILDER_TAG := ps3-builder:$(shell cat docker/Dockerfile | sha256sum | cut -c1-12)-$(BUILDER_SNAPSHOT)
BUILDER_STAMP := $(BUILD)/.builder-$(subst :,-,$(BUILDER_TAG))

DOCKER_RUN := docker run --rm -v $(CURDIR):/work -w /work \
	-e SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) -e JOBS=$(JOBS)

KERNEL_ENV := -e KERNEL_VERSION=$(KERNEL_VERSION) -e KERNEL_TARBALL=$(KERNEL_TARBALL) \
	-e KERNEL_SERIES=$(KERNEL_SERIES) -e KERNEL_DEFCONFIG=$(KERNEL_DEFCONFIG) \
	-e "KERNEL_FRAGMENTS=$(KERNEL_FRAGMENTS)" -e KERNEL_LOCALVERSION=$(KERNEL_LOCALVERSION)

ROOTFS_ENV := -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) \
	-e ROOTFS_PACKAGES=$(ROOTFS_PACKAGES) -e ROOTFS_CUSTOMIZE=$(ROOTFS_CUSTOMIZE) -e OWNER=$(UID_GID)

# The kernel depends on the KERNEL_* settings only, not on all of config.mk: this file is rewritten
# only when those values change, so e.g. an IP change doesn't trigger a kernel rebuild.
KERNEL_VARS := $(BUILD)/kernel.vars
KERNEL_VARS_NOW := $(foreach v,KERNEL_VERSION KERNEL_SHA256 KERNEL_LOCALVERSION KERNEL_DEFCONFIG \
	KERNEL_FRAGMENTS KERNEL_SERIES SOURCE_DATE_EPOCH BUILDER_BASE,$(v)=$($(v)))
$(shell mkdir -p $(BUILD); [ "$$(cat $(KERNEL_VARS) 2>/dev/null)" = "$(KERNEL_VARS_NOW)" ] || \
	echo "$(KERNEL_VARS_NOW)" > $(KERNEL_VARS))

KERNEL_INPUTS := $(KERNEL_TARBALL) $(KERNEL_SERIES) $(wildcard kernel/patches/*/*.patch) \
	$(KERNEL_FRAGMENTS) tools/build-kernel.sh $(KERNEL_VARS)
ROOTFS_INPUTS := $(ROOTFS_PACKAGES) $(ROOTFS_CUSTOMIZE) tools/build-rootfs.sh $(SSH_PUBKEY) config.mk

.PHONY: help all builder kernel rootfs checksums nfs-root nfs-modules netboot listen shell clean gpu-tools gpu-deploy ps3-reboot ps3-kexec

help:
	@echo "Build:"
	@echo "  make kernel       patched PS3 kernel + modules  -> $(KERNEL_STAGE)"
	@echo "  make rootfs       Debian ppc64 root filesystem  -> $(ROOTFS_TAR)"
	@echo "  make checksums    sha256 of outputs (compare two builds to check reproducibility)"
	@echo "Deploy (sudo):"
	@echo "  make nfs-root     install rootfs + modules into $(NFS_ROOT) and export it"
	@echo "  make nfs-modules  refresh only the kernel modules in $(NFS_ROOT)"
	@echo "Run:"
	@echo "  make netboot      boot our kernel on the PS3 from Petitboot (NFS root)"
	@echo "  make listen       print PS3 early-debug + netconsole output"
	@echo "GPU:"
	@echo "  make gpu-tools    cross-compile gpu/tools/*.c       -> $(BUILD)/gpu"
	@echo "  make gpu-deploy   copy them to ~/bin on the PS3's NFS root"
	@echo "  make ps3-reboot   reboot the running PS3 Linux (lands in GameOS/XMB, not Petitboot)"
	@echo "  make ps3-kexec    running PS3 Linux -> freshly built kernel, no XMB trip"
	@echo "Other:"
	@echo "  make shell        shell in the builder container"
	@echo "  make clean        move build outputs to $(BUILD)/old/ (never deletes)"

all: kernel rootfs

# --- Builder container --------------------------------------------------------
builder: $(BUILDER_STAMP)
$(BUILDER_STAMP): docker/Dockerfile config.mk
	docker build -q -t $(BUILDER_TAG) \
		--build-arg BASE=$(BUILDER_BASE) --build-arg SNAPSHOT=$(BUILDER_SNAPSHOT) docker
	@mkdir -p $(BUILD) && touch $@

# --- Kernel -------------------------------------------------------------------
$(KERNEL_TARBALL):
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(KERNEL_URL)
	echo "$(KERNEL_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

kernel: $(KERNEL_STAGE)/boot/vmlinux
$(KERNEL_STAGE)/boot/vmlinux: $(KERNEL_INPUTS) $(BUILDER_STAMP)
	echo "$(KERNEL_SHA256)  $(KERNEL_TARBALL)" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) $(KERNEL_ENV) $(BUILDER_TAG) tools/build-kernel.sh

# --- Root filesystem ----------------------------------------------------------
rootfs: $(ROOTFS_TAR)
$(ROOTFS_TAR): $(ROOTFS_INPUTS) $(BUILDER_STAMP)
	@mkdir -p $(BUILD)/rootfs
	cp $(SSH_PUBKEY) $(BUILD)/rootfs/authorized_keys
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) $(BUILDER_TAG) tools/build-rootfs.sh
	rm -f $(BUILD)/rootfs/authorized_keys

checksums:
	@cd $(BUILD) && sha256sum kernel/stage/boot/vmlinux-* rootfs/debian-ppc64.tar \
		$$(find kernel/stage/lib/modules -name '*.ko' | sort) > SHA256SUMS
	@head -2 $(BUILD)/SHA256SUMS; echo "($$(wc -l < $(BUILD)/SHA256SUMS) files in $(BUILD)/SHA256SUMS)"

# --- Deploy -------------------------------------------------------------------
nfs-root: $(ROOTFS_TAR) $(KERNEL_STAGE)/boot/vmlinux
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) tools/install-nfs-root.sh $(ROOTFS_TAR) $(KERNEL_STAGE)

nfs-modules: $(KERNEL_STAGE)/boot/vmlinux
	sudo rsync -a --delete --chown=root:root $(KERNEL_STAGE)/lib/modules/ $(NFS_ROOT)/lib/modules/

# --- GPU tools ----------------------------------------------------------------
PS3_HOME := $(NFS_ROOT)/home/tibi
# Test kernel modules build against the kernel tree in $(BUILD)/kernel/obj. Kbuild writes objects
# next to the sources, so the sources are copied into $(BUILD)/gpu-kmod first.
gpu-tools: $(BUILDER_STAMP) | $(KERNEL_STAGE)/boot/vmlinux
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -C gpu OUT=../$(BUILD)/gpu
	mkdir -p $(BUILD)/gpu-kmod && cp gpu/kmod/* $(BUILD)/gpu-kmod/
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -C $(BUILD)/kernel/obj \
		ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- M=/work/$(BUILD)/gpu-kmod modules

gpu-deploy: gpu-tools
	install -d $(PS3_HOME)/bin
	install -m 755 $(BUILD)/gpu/* $(BUILD)/gpu-kmod/*.ko $(PS3_HOME)/bin/

# --- Run ----------------------------------------------------------------------
netboot: $(KERNEL_STAGE)/boot/vmlinux
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$(KERNEL_STAGE)/boot/vmlinux \
		KERNEL_CMDLINE="$(KERNEL_CMDLINE)" tools/netboot.sh

listen:
	tools/listen-debug.sh

PS3_SSH := ssh -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts tibi@$(PS3_IP)
ps3-reboot:
	-$(PS3_SSH) sudo systemctl reboot

# Running PS3 Linux -> new kernel without the firmware (a reboot lands in GameOS, see ps3-reboot).
# The kernel goes to ~/kernel on the NFS root; systemd shuts down cleanly, then kexecs.
ps3-kexec: $(KERNEL_STAGE)/boot/vmlinux
	install -d $(PS3_HOME)/kernel
	install -m 644 $(KERNEL_STAGE)/boot/vmlinux-* $(PS3_HOME)/kernel/vmlinux
	$(PS3_SSH) 'sudo kexec -l ~/kernel/vmlinux --command-line="$(KERNEL_CMDLINE)" && sudo systemctl kexec'

shell: $(BUILDER_STAMP)
	$(DOCKER_RUN) -it --user $(UID_GID) $(BUILDER_TAG) bash

clean:
	@ts=$$(date +%Y%m%d-%H%M%S); mkdir -p $(BUILD)/old/$$ts; \
	for d in kernel rootfs; do if [ -e $(BUILD)/$$d ]; then mv $(BUILD)/$$d $(BUILD)/old/$$ts/; fi; done; \
	echo "moved outputs to $(BUILD)/old/$$ts"
