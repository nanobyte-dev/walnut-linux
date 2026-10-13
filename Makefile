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

KERNEL_INPUTS := $(KERNEL_TARBALL) $(KERNEL_SERIES) $(wildcard kernel/patches/*/*.patch) \
	$(KERNEL_FRAGMENTS) tools/build-kernel.sh config.mk
ROOTFS_INPUTS := $(ROOTFS_PACKAGES) $(ROOTFS_CUSTOMIZE) tools/build-rootfs.sh $(SSH_PUBKEY) config.mk

.PHONY: help all builder kernel rootfs checksums nfs-root nfs-modules netboot listen shell clean

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
		$$(find kernel/stage/lib/modules -name '*.ko' | sort) | tee SHA256SUMS | head -2

# --- Deploy -------------------------------------------------------------------
nfs-root: $(ROOTFS_TAR) $(KERNEL_STAGE)/boot/vmlinux
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) tools/install-nfs-root.sh $(ROOTFS_TAR) $(KERNEL_STAGE)

nfs-modules: $(KERNEL_STAGE)/boot/vmlinux
	sudo rsync -a --delete --chown=root:root $(KERNEL_STAGE)/lib/modules/ $(NFS_ROOT)/lib/modules/

# --- Run ----------------------------------------------------------------------
netboot: $(KERNEL_STAGE)/boot/vmlinux
	PS3_IP=$(PS3_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$(KERNEL_STAGE)/boot/vmlinux \
		KERNEL_CMDLINE="$(KERNEL_CMDLINE)" tools/netboot.sh

listen:
	tools/listen-debug.sh

shell: $(BUILDER_STAMP)
	$(DOCKER_RUN) -it --user $(UID_GID) $(BUILDER_TAG) bash

clean:
	@ts=$$(date +%Y%m%d-%H%M%S); mkdir -p $(BUILD)/old/$$ts; \
	for d in kernel rootfs; do if [ -e $(BUILD)/$$d ]; then mv $(BUILD)/$$d $(BUILD)/old/$$ts/; fi; done; \
	echo "moved outputs to $(BUILD)/old/$$ts"
