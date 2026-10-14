# PS3 Linux build. Settings are in config.mk; run `make help` for targets.
include config.mk

JOBS        ?= $(shell nproc)
UID_GID     := $(shell id -u):$(shell id -g)

BUILD       := build
DOWNLOADS   := $(BUILD)/downloads
KERNEL_TARBALL := $(DOWNLOADS)/linux-$(KERNEL_VERSION).tar.xz
KERNEL_STAGE   := $(BUILD)/kernel/stage
ROOTFS_TAR     := $(BUILD)/rootfs/debian-ppc64.tar

# Builder image tag changes whenever its inputs change (Dockerfile, base image, snapshot)
BUILDER_TAG := ps3-builder:$(shell (cat docker/Dockerfile; echo $(BUILDER_BASE)) | sha256sum | cut -c1-12)-$(BUILDER_SNAPSHOT)
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
	KERNEL_FRAGMENTS KERNEL_SERIES SOURCE_DATE_EPOCH BUILDER_TAG,$(v)=$($(v)))
$(shell mkdir -p $(BUILD); [ "$$(cat $(KERNEL_VARS) 2>/dev/null)" = "$(KERNEL_VARS_NOW)" ] || \
	echo "$(KERNEL_VARS_NOW)" > $(KERNEL_VARS))

KERNEL_INPUTS := $(KERNEL_TARBALL) $(KERNEL_SERIES) $(wildcard kernel/patches/*/*.patch) \
	$(KERNEL_FRAGMENTS) tools/build-kernel.sh $(KERNEL_VARS)
ROOTFS_VARS := $(BUILD)/rootfs.vars
ROOTFS_VARS_NOW := $(foreach v,DEBIAN_SNAPSHOT DEBIAN_SUITE DEBIAN_MIRROR SOURCE_DATE_EPOCH BUILDER_TAG,$(v)=$($(v)))
$(shell [ "$$(cat $(ROOTFS_VARS) 2>/dev/null)" = "$(ROOTFS_VARS_NOW)" ] || echo "$(ROOTFS_VARS_NOW)" > $(ROOTFS_VARS))
ROOTFS_INPUTS := $(ROOTFS_PACKAGES) $(ROOTFS_CUSTOMIZE) tools/build-rootfs.sh $(SSH_PUBKEY) $(ROOTFS_VARS)

.PHONY: help all builder kernel rootfs checksums nfs-root nfs-modules netboot listen shell clean gpu-tools gpu-deploy ps3-reboot \
	mesa-builder mesa-sysroot mesa mesa-tests mesa-deploy

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
	@echo "Mesa:"
	@echo "  make mesa-sysroot Debian ppc64 sysroot to build against -> $(MESA_SYSROOT)"
	@echo "  make mesa         patched Mesa (nouveau nv30 + softpipe) -> $(MESA_INSTALL)$(MESA_PREFIX)"
	@echo "  make mesa-tests   offscreen EGL/GLES2 tests (mesa/tests) -> $(BUILD)/mesa/tests"
	@echo "  make mesa-deploy  copy both to $(MESA_PREFIX) on the PS3's NFS root"
	@echo "Other:"
	@echo "  make shell        shell in the builder container"
	@echo "  make clean        move build outputs to $(BUILD)/old/ (never deletes)"

all: kernel rootfs

# --- Builder container --------------------------------------------------------
builder: $(BUILDER_STAMP)
$(BUILDER_STAMP): docker/Dockerfile
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
$(KERNEL_STAGE)/boot/vmlinux: $(KERNEL_INPUTS) | $(BUILDER_STAMP)
	echo "$(KERNEL_SHA256)  $(KERNEL_TARBALL)" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) $(KERNEL_ENV) $(BUILDER_TAG) tools/build-kernel.sh

# --- Root filesystem ----------------------------------------------------------
rootfs: $(ROOTFS_TAR)
$(ROOTFS_TAR): $(ROOTFS_INPUTS) | $(BUILDER_STAMP)
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
# Kernel modules (every gpu/*/Kbuild) build against the kernel tree in $(BUILD)/kernel/obj.
# Kbuild writes objects next to the sources, so each directory is copied to $(BUILD)/gpu-<dir>.
GPU_KMODS := $(patsubst gpu/%/Kbuild,%,$(wildcard gpu/*/Kbuild))
gpu-tools: $(BUILDER_STAMP) | $(KERNEL_STAGE)/boot/vmlinux
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -C gpu OUT=../$(BUILD)/gpu
	for d in $(GPU_KMODS); do \
		mkdir -p $(BUILD)/gpu-$$d && cp gpu/$$d/* $(BUILD)/gpu-$$d/ && \
		$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -s -C $(BUILD)/kernel/obj \
			ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- M=/work/$(BUILD)/gpu-$$d modules \
			|| exit 1; \
	done

gpu-deploy: gpu-tools
	install -d $(PS3_HOME)/bin
	install -m 755 $(BUILD)/gpu/* $(BUILD)/gpu-*/*.ko gpu/tools/*.sh $(PS3_HOME)/bin/

# --- Mesa -------------------------------------------------------------------------------
# Own image on top of the builder (C++ cross compiler, meson), so its tag doesn't change the kernel's.
MESA_BUILDER_TAG := ps3-mesa-builder:$(shell (cat docker/Dockerfile.mesa; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
MESA_BUILDER_STAMP := $(BUILD)/.mesa-builder-$(subst :,-,$(MESA_BUILDER_TAG))
MESA_TARBALL  := $(DOWNLOADS)/mesa-$(MESA_VERSION).tar.xz
MESA_SYSROOT  := $(BUILD)/mesa/sysroot
MESA_INSTALL  := $(BUILD)/mesa/install
MESA_ENV := -e MESA_VERSION=$(MESA_VERSION) -e MESA_TARBALL=$(MESA_TARBALL) -e MESA_SERIES=$(MESA_SERIES) \
	-e MESA_PREFIX=$(MESA_PREFIX)
MESA_VARS := $(BUILD)/mesa.vars
MESA_VARS_NOW := $(foreach v,MESA_VERSION MESA_SHA256 MESA_SERIES MESA_PREFIX SOURCE_DATE_EPOCH MESA_BUILDER_TAG,$(v)=$($(v)))
$(shell [ "$$(cat $(MESA_VARS) 2>/dev/null)" = "$(MESA_VARS_NOW)" ] || echo "$(MESA_VARS_NOW)" > $(MESA_VARS))
MESA_INPUTS := $(MESA_TARBALL) $(MESA_SERIES) $(wildcard mesa/patches/*.patch) mesa/cross-ppc64.ini \
	tools/build-mesa.sh $(MESA_VARS) $(MESA_SYSROOT)/.done

mesa-builder: $(MESA_BUILDER_STAMP)
$(MESA_BUILDER_STAMP): docker/Dockerfile.mesa | $(BUILDER_STAMP)
	docker build -q -t $(MESA_BUILDER_TAG) -f docker/Dockerfile.mesa --build-arg BUILDER=$(BUILDER_TAG) docker
	@touch $@

$(MESA_TARBALL):
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(MESA_URL)
	echo "$(MESA_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

mesa-sysroot: $(MESA_SYSROOT)/.done
$(MESA_SYSROOT)/.done: $(MESA_SYSROOT_PACKAGES) tools/build-mesa-sysroot.sh $(ROOTFS_VARS) | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) $(ROOTFS_ENV) -e MESA_SYSROOT_PACKAGES=$(MESA_SYSROOT_PACKAGES) $(MESA_BUILDER_TAG) \
		tools/build-mesa-sysroot.sh

mesa: $(MESA_INSTALL)$(MESA_PREFIX)/env.sh
$(MESA_INSTALL)$(MESA_PREFIX)/env.sh: $(MESA_INPUTS) | $(MESA_BUILDER_STAMP)
	echo "$(MESA_SHA256)  $(MESA_TARBALL)" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) $(MESA_ENV) $(MESA_BUILDER_TAG) tools/build-mesa.sh

mesa-tests: $(MESA_SYSROOT)/.done | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(MESA_BUILDER_TAG) make -C mesa/tests SYSROOT=/work/$(MESA_SYSROOT) \
		OUT=/work/$(BUILD)/mesa/tests

mesa-deploy: mesa mesa-tests
	install -d $(PS3_HOME)/mesa/bin
	rsync -a $(MESA_INSTALL)$(MESA_PREFIX)/ $(PS3_HOME)/mesa/
	install -m 755 $(BUILD)/mesa/tests/* $(PS3_HOME)/mesa/bin/

# --- Run ----------------------------------------------------------------------
netboot: $(KERNEL_STAGE)/boot/vmlinux
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$(KERNEL_STAGE)/boot/vmlinux \
		KERNEL_CMDLINE="$(KERNEL_CMDLINE)" tools/netboot.sh

listen:
	tools/listen-debug.sh

PS3_SSH := ssh -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts tibi@$(PS3_IP)
ps3-reboot:
	-$(PS3_SSH) sudo systemctl reboot

shell: $(BUILDER_STAMP)
	$(DOCKER_RUN) -it --user $(UID_GID) $(BUILDER_TAG) bash

clean:
	@ts=$$(date +%Y%m%d-%H%M%S); mkdir -p $(BUILD)/old/$$ts; \
	for d in kernel rootfs mesa; do if [ -e $(BUILD)/$$d ]; then mv $(BUILD)/$$d $(BUILD)/old/$$ts/; fi; done; \
	echo "moved outputs to $(BUILD)/old/$$ts"
