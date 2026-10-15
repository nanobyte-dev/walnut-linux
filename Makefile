# PS3 Linux build. Settings are in config.mk; run `make help` for targets.
include config.mk

JOBS        ?= $(shell nproc)
UID_GID     := $(shell id -u):$(shell id -g)

BUILD       := build
DOWNLOADS   := $(BUILD)/downloads
KERNEL_TARBALL := $(DOWNLOADS)/linux-$(KERNEL_VERSION).tar.xz
KERNEL_STAGE   := $(BUILD)/kernel/stage
ROOTFS_BASE    := $(BUILD)/rootfs/base.tar
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
ROOTFS_BASE_INPUTS := $(ROOTFS_PACKAGES) tools/build-rootfs.sh $(ROOTFS_VARS)

.PHONY: spu-toolchain spu-tests spu-deploy help all builder kernel rootfs rootfs-base checksums nfs-root usb-root boot-deploy nfs-modules netboot listen shell clean gpu-tools gpu-deploy ps3-reboot \
	mesa-builder mesa-sysroot mesa mesa-tests mesa-deploy classicube classicube-audio classicube-deploy

help:
	@echo "Build:"
	@echo "  make kernel       patched PS3 kernel + modules  -> $(KERNEL_STAGE)"
	@echo "  make rootfs       the PS3's root filesystem image -> $(ROOTFS_TAR)"
	@echo "                    (package layer + our config, kernel modules, RSX driver, Mesa, games)"
	@echo "  make rootfs-base  only its package layer (slow: ~15 min, reruns when packages change)"
	@echo "  make checksums    sha256 of outputs (compare two builds to check reproducibility)"
	@echo "Deploy (sudo):"
	@echo "  make nfs-root     install the image into $(NFS_ROOT) and export it (keeps SSH host"
	@echo "                    keys, Bluetooth pairings and Wi-Fi connections; FRESH=1 doesn't)"
	@echo "  make usb-root     install the image onto the USB disk on the PS3 (FORMAT=1: partition"
	@echo "                    it first, erasing it); boot it with make netboot ROOT=usb"
	@echo "Update the live root in place (NFS; ROOT=usb: the PS3's USB root over SSH; no reboot,"
	@echo "fixes still go into the build):"
	@echo "  make nfs-modules  kernel modules"
	@echo "  make boot-deploy  kernel + Petitboot boot menu (/etc/kboot.conf), for booting from the disk"
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
# The image is built in two layers: the packages (slow, rarely changes), then everything of ours
# on top (fast). `make rootfs` itself is in the Image section below, after its inputs.
rootfs-base: $(ROOTFS_BASE)
$(ROOTFS_BASE): $(ROOTFS_BASE_INPUTS) | $(BUILDER_STAMP)
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) $(BUILDER_TAG) tools/build-rootfs.sh

checksums:
	@cd $(BUILD) && sha256sum kernel/stage/boot/vmlinux-* rootfs/debian-ppc64.tar \
		$$(find kernel/stage/lib/modules -name '*.ko' | sort) > SHA256SUMS
	@head -2 $(BUILD)/SHA256SUMS; echo "($$(wc -l < $(BUILD)/SHA256SUMS) files in $(BUILD)/SHA256SUMS)"

# --- Deploy -------------------------------------------------------------------
# The whole system is in the image (kernel modules, RSX driver, Mesa, games included), so a
# fresh root needs nothing done by hand. The *-deploy targets update the live root in place
# while iterating (tools/install-builds.sh, the same steps the image build runs).
INSTALL_BUILDS := BUILD=$(BUILD) MESA_PREFIX=$(MESA_PREFIX) KBOOT_CMDLINE="$(KBOOT_CMDLINE)" \
	tools/install-builds.sh
# deploy <parts>: into the NFS root on this PC, or with ROOT=usb over SSH into the running PS3's
# USB root (staged here, then unpacked there: system files root-owned, ~tibi's tibi-owned). The
# staged copy has no ClassiCube options.txt, so the one on the PS3 is kept.
ifeq ($(ROOT),usb)
deploy = rm -rf $(BUILD)/deploy && mkdir -p $(BUILD)/deploy && $(INSTALL_BUILDS) $(BUILD)/deploy $(1) && \
	tar -C $(BUILD)/deploy --numeric-owner --owner=0 --group=0 --exclude=./home -cf - . | \
		$(PS3_SSH) 'sudo tar -xpf - --no-overwrite-dir -C /' && \
	if [ -d $(BUILD)/deploy/home ]; then tar -C $(BUILD)/deploy --numeric-owner --owner=1000 \
		--group=1000 --exclude=./home/tibi/classicube/options.txt -cf - ./home | \
		$(PS3_SSH) 'sudo tar -xpf - --no-overwrite-dir -C /'; fi
else
deploy = $(INSTALL_BUILDS) $(NFS_ROOT) $(1)
endif
nfs-root: $(ROOTFS_TAR) $(KERNEL_STAGE)/boot/vmlinux
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) FRESH=$(FRESH) tools/install-nfs-root.sh $(ROOTFS_TAR)

# The image onto the USB disk plugged into the PS3 (running Linux, over SSH); FORMAT=1 partitions
# the disk first (erases it). Boot it with make netboot ROOT=usb.
usb-root: $(ROOTFS_TAR)
	PS3_SSH="$(PS3_SSH)" USB_DISK_ID="$(USB_DISK_ID)" FORMAT=$(FORMAT) FRESH=$(FRESH) \
		tools/install-usb-root.sh $(ROOTFS_TAR)

nfs-modules: $(KERNEL_STAGE)/boot/vmlinux
ifeq ($(ROOT),usb)
	$(call deploy,modules)
else
	sudo $(INSTALL_BUILDS) $(NFS_ROOT) modules
endif

# The kernel and Petitboot's boot menu (/etc/kboot.conf) on the root, for booting from the disk
boot-deploy: $(KERNEL_STAGE)/boot/vmlinux
	$(call deploy,boot)

# --- GPU tools ----------------------------------------------------------------
# Kernel modules (every gpu/*/Kbuild) build against the kernel tree in $(BUILD)/kernel/obj.
# Kbuild writes objects next to the sources, so each directory is copied to $(BUILD)/gpu-<dir>.
GPU_KMODS := $(patsubst gpu/%/Kbuild,%,$(wildcard gpu/*/Kbuild))
GPU_STAMP := $(BUILD)/gpu/.done
gpu-tools: $(GPU_STAMP)
$(GPU_STAMP): $(wildcard gpu/Makefile gpu/*/*) $(KERNEL_STAGE)/boot/vmlinux | $(BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -C gpu OUT=../$(BUILD)/gpu
	for d in $(GPU_KMODS); do \
		mkdir -p $(BUILD)/gpu-$$d && cp gpu/$$d/* $(BUILD)/gpu-$$d/ && \
		$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -s -C $(BUILD)/kernel/obj \
			ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- M=/work/$(BUILD)/gpu-$$d modules \
			|| exit 1; \
	done
	@touch $@

gpu-deploy: $(GPU_STAMP)
	$(call deploy,gpu)

# --- Mesa -------------------------------------------------------------------------------
# Own image on top of the builder (C++ cross compiler, meson), so its tag doesn't change the kernel's.
MESA_BUILDER_TAG := ps3-mesa-builder:$(shell (cat docker/Dockerfile.mesa; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
MESA_BUILDER_STAMP := $(BUILD)/.mesa-builder-$(subst :,-,$(MESA_BUILDER_TAG))
MESA_TARBALL  := $(DOWNLOADS)/mesa-$(MESA_VERSION).tar.xz
MESA_SYSROOT  := $(BUILD)/mesa/sysroot
MESA_INSTALL  := $(BUILD)/mesa/install
MESA_ENV := -e MESA_VERSION=$(MESA_VERSION) -e MESA_TARBALL=$(MESA_TARBALL) -e MESA_FORK=$(MESA_FORK) \
	-e MESA_PREFIX=$(MESA_PREFIX)
MESA_VARS := $(BUILD)/mesa.vars
MESA_VARS_NOW := $(foreach v,MESA_VERSION MESA_SHA256 MESA_FORK MESA_PREFIX SOURCE_DATE_EPOCH MESA_BUILDER_TAG,$(v)=$($(v)))
$(shell [ "$$(cat $(MESA_VARS) 2>/dev/null)" = "$(MESA_VARS_NOW)" ] || echo "$(MESA_VARS_NOW)" > $(MESA_VARS))
MESA_INPUTS := $(MESA_TARBALL) $(shell find $(MESA_FORK) -type f) mesa/cross-ppc64.ini \
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

MESA_TESTS_STAMP := $(BUILD)/mesa/tests/.done
mesa-tests: $(MESA_TESTS_STAMP)
$(MESA_TESTS_STAMP): $(wildcard mesa/tests/*) $(MESA_SYSROOT)/.done | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(MESA_BUILDER_TAG) make -C mesa/tests SYSROOT=/work/$(MESA_SYSROOT) \
		OUT=/work/$(BUILD)/mesa/tests
	@touch $@

mesa-deploy: mesa mesa-tests
	$(call deploy,mesa)

# --- ClassiCube -----------------------------------------------------------------
CC_TARBALL := $(DOWNLOADS)/ClassiCube-$(CLASSICUBE_COMMIT).tar.gz
CC_TEXPACK := $(DOWNLOADS)/classicube-texpack-$(CLASSICUBE_TEXPACK_SHA256).zip
CC_INSTALL := $(BUILD)/classicube/install
$(CC_TARBALL):
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(CLASSICUBE_URL)
	echo "$(CLASSICUBE_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@
$(CC_TEXPACK):
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(CLASSICUBE_TEXPACK_URL)
	echo "$(CLASSICUBE_TEXPACK_SHA256)  $@.tmp" | sha256sum -c --quiet
	mv $@.tmp $@

# Game asset tools (oggdec) on top of the Mesa builder
GAMES_BUILDER_TAG := ps3-games-builder:$(shell (cat docker/Dockerfile.games; echo $(MESA_BUILDER_TAG)) | sha256sum | cut -c1-12)
GAMES_BUILDER_STAMP := $(BUILD)/.games-builder-$(subst :,-,$(GAMES_BUILDER_TAG))
$(GAMES_BUILDER_STAMP): docker/Dockerfile.games | $(MESA_BUILDER_STAMP)
	docker build -q -t $(GAMES_BUILDER_TAG) -f docker/Dockerfile.games --build-arg BUILDER=$(MESA_BUILDER_TAG) docker
	@touch $@

# Sounds and music, fetched and converted as the game's launcher would (games/classicube/sounds.sh)
CC_AUDIO := $(BUILD)/classicube/audio
classicube-audio: $(CC_AUDIO)/default2.zip
$(CC_AUDIO)/default2.zip: $(CC_TARBALL) games/classicube/sounds.sh | $(GAMES_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e CC_TARBALL=$(CC_TARBALL) $(GAMES_BUILDER_TAG) games/classicube/sounds.sh

classicube-deploy: classicube classicube-audio
	$(call deploy,classicube)

classicube: $(CC_INSTALL)/ClassiCube
$(CC_INSTALL)/ClassiCube: $(CC_TARBALL) $(CC_TEXPACK) games/classicube/build.sh $(MESA_SYSROOT)/.done | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e CC_TARBALL=$(CC_TARBALL) -e CC_TEXPACK=$(CC_TEXPACK) \
		$(MESA_BUILDER_TAG) games/classicube/build.sh

# --- SPU toolchain ---------------------------------------------------------------
# spu-elf GCC 9.5 + binutils + newlib for the Cell's SPEs (tools/build-spu-toolchain.sh)
SPU_BUILDER_TAG := ps3-spu-builder:$(shell (cat docker/Dockerfile.spu; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
SPU_BUILDER_STAMP := $(BUILD)/.spu-builder-$(subst :,-,$(SPU_BUILDER_TAG))
SPU_TARBALLS := $(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz $(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz \
	$(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz
SPU_GCC := $(BUILD)/spu/install/opt/spu/bin/spu-elf-gcc
$(SPU_BUILDER_STAMP): docker/Dockerfile.spu | $(BUILDER_STAMP)
	docker build -q -t $(SPU_BUILDER_TAG) -f docker/Dockerfile.spu --build-arg BUILDER=$(BUILDER_TAG) docker
	@touch $@
$(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz:
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(SPU_GCC_URL) && echo "$(SPU_GCC_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
$(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz:
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(SPU_BINUTILS_URL) && echo "$(SPU_BINUTILS_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
$(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz:
	@mkdir -p $(DOWNLOADS)
	curl -fsSL -o $@.tmp $(SPU_NEWLIB_URL) && echo "$(SPU_NEWLIB_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
spu-toolchain: $(SPU_GCC)
$(SPU_GCC): $(SPU_TARBALLS) tools/build-spu-toolchain.sh | $(SPU_BUILDER_STAMP)
	echo "$(SPU_GCC_SHA256)  $(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_BINUTILS_SHA256)  $(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_NEWLIB_SHA256)  $(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) -e SPU_GCC_VERSION=$(SPU_GCC_VERSION) \
		-e SPU_BINUTILS_VERSION=$(SPU_BINUTILS_VERSION) -e SPU_NEWLIB_VERSION=$(SPU_NEWLIB_VERSION) \
		$(SPU_BUILDER_TAG) tools/build-spu-toolchain.sh

# SPU test programs and the spufs runner (spu/), into ~/spu on the root
spu-tests: $(SPU_GCC) | $(SPU_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(SPU_BUILDER_TAG) make -C spu \
		SPU_CC=/work/$(SPU_GCC) OUT=/work/$(BUILD)/spu-tests
spu-deploy: spu-tests
	$(call deploy,spu)

# --- Image --------------------------------------------------------------------
# The package layer plus our config (rootfs/) and everything we build (tools/install-builds.sh)
IMAGE_INPUTS := $(ROOTFS_BASE) $(wildcard rootfs/*) tools/build-image.sh tools/install-builds.sh \
	$(SSH_PUBKEY) $(KERNEL_STAGE)/boot/vmlinux $(GPU_STAMP) $(MESA_INSTALL)$(MESA_PREFIX)/env.sh \
	$(MESA_TESTS_STAMP) $(CC_INSTALL)/ClassiCube $(CC_AUDIO)/default2.zip $(wildcard games/classicube/*)
rootfs: $(ROOTFS_TAR)
IMAGE_VARS := $(BUILD)/image.vars
IMAGE_VARS_NOW := KBOOT_CMDLINE=$(KBOOT_CMDLINE)
$(shell [ "$$(cat $(IMAGE_VARS) 2>/dev/null)" = "$(IMAGE_VARS_NOW)" ] || echo "$(IMAGE_VARS_NOW)" > $(IMAGE_VARS))
$(ROOTFS_TAR): $(IMAGE_INPUTS) $(IMAGE_VARS) | $(BUILDER_STAMP)
	@mkdir -p $(BUILD)/rootfs
	cp $(SSH_PUBKEY) $(BUILD)/rootfs/authorized_keys
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) -e MESA_PREFIX=$(MESA_PREFIX) \
		-e KBOOT_CMDLINE="$(KBOOT_CMDLINE)" $(BUILDER_TAG) tools/build-image.sh
	rm -f $(BUILD)/rootfs/authorized_keys

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
