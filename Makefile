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

# Kernel release stamp: the date of the last commit that changed the kernel's inputs (linux/patches
# and the fragments, before 2026-10-16 kernel/; pure renames don't count), reproducible: the same
# checkout gives the same name. The build time while those have uncommitted changes. (The rest of
# linux/, the Debian packaging of milestone 4, doesn't change this kernel.)
KERNEL_STAMP_PATHS := linux/patches $(KERNEL_FRAGMENTS)
KERNEL_STAMP := $(shell if [ -n "$$(git -C $(CURDIR) status --porcelain $(KERNEL_STAMP_PATHS) 2>/dev/null)" ]; then \
	date +%Y%m%d%H%M; else git -C $(CURDIR) log -1 -M --diff-filter=ACDMT --format=%cd \
	--date=format-local:%Y%m%d%H%M -- kernel/ $(KERNEL_STAMP_PATHS); fi)
KERNEL_RELEASE := $(KERNEL_VERSION)$(KERNEL_LOCALVERSION)-$(KERNEL_STAMP)

KERNEL_ENV := -e KERNEL_VERSION=$(KERNEL_VERSION) -e KERNEL_TARBALL=$(KERNEL_TARBALL) \
	-e KERNEL_SERIES=$(KERNEL_SERIES) -e KERNEL_DEFCONFIG=$(KERNEL_DEFCONFIG) \
	-e "KERNEL_FRAGMENTS=$(KERNEL_FRAGMENTS)" -e KERNEL_LOCALVERSION=$(KERNEL_LOCALVERSION)-$(KERNEL_STAMP)

ROOTFS_ENV := -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) \
	-e ROOTFS_PACKAGES=$(ROOTFS_PACKAGES) -e ROOTFS_CUSTOMIZE=$(ROOTFS_CUSTOMIZE) -e OWNER=$(UID_GID)

# The kernel depends on the KERNEL_* settings only, not on all of config.mk: this file is rewritten
# only when those values change, so e.g. an IP change doesn't trigger a kernel rebuild.
KERNEL_VARS := $(BUILD)/kernel.vars
KERNEL_VARS_NOW := $(foreach v,KERNEL_VERSION KERNEL_SHA256 KERNEL_LOCALVERSION KERNEL_STAMP KERNEL_DEFCONFIG \
	KERNEL_FRAGMENTS KERNEL_SERIES SOURCE_DATE_EPOCH BUILDER_TAG,$(v)=$($(v)))
$(shell mkdir -p $(BUILD); [ "$$(cat $(KERNEL_VARS) 2>/dev/null)" = "$(KERNEL_VARS_NOW)" ] || \
	echo "$(KERNEL_VARS_NOW)" > $(KERNEL_VARS))

KERNEL_INPUTS := $(KERNEL_TARBALL) $(KERNEL_SERIES) $(wildcard linux/patches/*/*.patch) \
	$(KERNEL_FRAGMENTS) tools/build-kernel.sh $(KERNEL_VARS)
ROOTFS_VARS := $(BUILD)/rootfs.vars
ROOTFS_VARS_NOW := $(foreach v,DEBIAN_SNAPSHOT DEBIAN_SUITE DEBIAN_MIRROR SOURCE_DATE_EPOCH BUILDER_TAG,$(v)=$($(v)))
$(shell [ "$$(cat $(ROOTFS_VARS) 2>/dev/null)" = "$(ROOTFS_VARS_NOW)" ] || echo "$(ROOTFS_VARS_NOW)" > $(ROOTFS_VARS))
ROOTFS_BASE_INPUTS := $(ROOTFS_PACKAGES) tools/build-rootfs.sh $(ROOTFS_VARS)

.PHONY: repo-snapshot publish deps du clean-ephemeral chroot packages ps3rsx-modules netboot-deb spu-toolchain spu-tests spu-deploy help all builder kernel rootfs rootfs-base checksums nfs-root usb-root usb-root-petitboot debs deploy kernel-deploy netboot listen shell clean gpu-tools gpu-deploy ps3-reboot \
	mesa-builder mesa-sysroot mesa-tests classicube classicube-audio classicube-deploy

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
	@echo "Update the running PS3 without reinstalling (fixes still go into the build):"
	@echo "  make deploy       install our packages on the running PS3 (PKGS=\"ps3-mesa ...\"; all by default)"
	@echo "  make kernel-deploy | gpu-deploy | classicube-deploy | spu-deploy"
	@echo "  make debs         build our packages into $(BUILD)/repo (an apt repository)"
	@echo "Run:"
	@echo "  make netboot      boot our kernel on the PS3 from Petitboot (NFS root)"
	@echo "  make listen       print PS3 early-debug + netconsole output"
	@echo "GPU:"
	@echo "  make gpu-tools    cross-compile ps3-gpu-tools/src/*.c -> $(BUILD)/gpu"
	@echo "  make gpu-deploy   install them (ps3rsx, ps3-gpu-tools packages) on the running PS3"
	@echo "  make ps3-reboot   reboot the running PS3 Linux (lands in GameOS/XMB, not Petitboot)"
	@echo "Mesa:"
	@echo "  make mesa-sysroot Debian ppc64 sysroot to build against -> $(MESA_SYSROOT)"
	@echo "  make mesa-deb     our Mesa: Debian's source + mesa/patches, built natively (qemu) -> $(BUILD)/debs/mesa"
	@echo "  make mesa-tests   offscreen EGL/GLES2 tests (ps3-gpu-tools/mesa-tests) -> $(BUILD)/mesa/tests"
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
# The whole system is in the image (our packages included), so a fresh root needs nothing done by
# hand. The *-deploy targets install freshly built packages on the running PS3 (see Packages).
nfs-root: $(ROOTFS_TAR) $(KERNEL_STAGE)/boot/vmlinux
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) FRESH=$(FRESH) tools/install-nfs-root.sh $(ROOTFS_TAR)

# The image onto the USB disk from Petitboot (the disk isn't in use there, so this replaces even the
# root the PS3 normally runs from); the disk must have been set up with make usb-root FORMAT=1
usb-root-petitboot: $(ROOTFS_TAR)
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) FRESH=$(FRESH) \
		tools/install-usb-root-petitboot.sh $(ROOTFS_TAR)

# The image onto the USB disk plugged into the PS3 (running Linux, over SSH); FORMAT=1 partitions
# the disk first (erases it). Boot it with make netboot ROOT=usb.
usb-root: $(ROOTFS_TAR)
	PS3_SSH="$(PS3_SSH)" USB_DISK_ID="$(USB_DISK_ID)" FORMAT=$(FORMAT) FRESH=$(FRESH) \
		tools/install-usb-root.sh $(ROOTFS_TAR)

# --- GPU tools ----------------------------------------------------------------
# Kernel modules build against the kernel tree in $(BUILD)/kernel/obj. Kbuild writes objects next to
# the sources, so each source directory is copied to $(BUILD)/gpu-<name> (<name>:<source dir>).
GPU_KMODS := ps3rsx:ps3rsx/src kmod:ps3rsx/probes
GPU_STAMP := $(BUILD)/gpu/.done
gpu-tools: $(GPU_STAMP)
$(GPU_STAMP): $(wildcard ps3-gpu-tools/Makefile ps3-gpu-tools/src/* ps3rsx/src/* ps3rsx/probes/*) \
		$(KERNEL_STAGE)/boot/vmlinux | $(BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -C ps3-gpu-tools OUT=../$(BUILD)/gpu
	for m in $(GPU_KMODS); do d=$${m%%:*}; s=$${m#*:}; [ -e $$s/Kbuild ] || { echo "no $$s/Kbuild"; exit 1; }; \
		mkdir -p $(BUILD)/gpu-$$d && cp $$s/* $(BUILD)/gpu-$$d/ && \
		$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) make -s -C $(BUILD)/kernel/obj \
			ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- M=/work/$(BUILD)/gpu-$$d modules \
			|| exit 1; \
	done
	@touch $@

gpu-deploy:
	$(MAKE) deploy PKGS="ps3rsx-modules-ps3 ps3-gpu-tools"

# --- Mesa cross toolchain (the Debian sysroot for ClassiCube and the GL tests; Mesa itself is mesa-deb) -------------------------------------------------------------------------------
# Own image on top of the builder (C++ cross compiler, meson), so its tag doesn't change the kernel's.
MESA_BUILDER_TAG := ps3-mesa-builder:$(shell (cat docker/Dockerfile.mesa; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
MESA_BUILDER_STAMP := $(BUILD)/.mesa-builder-$(subst :,-,$(MESA_BUILDER_TAG))
MESA_SYSROOT  := $(BUILD)/mesa/sysroot

mesa-builder: $(MESA_BUILDER_STAMP)
$(MESA_BUILDER_STAMP): docker/Dockerfile.mesa | $(BUILDER_STAMP)
	docker build -q -t $(MESA_BUILDER_TAG) -f docker/Dockerfile.mesa --build-arg BUILDER=$(BUILDER_TAG) docker
	@touch $@

mesa-sysroot: $(MESA_SYSROOT)/.done
$(MESA_SYSROOT)/.done: $(MESA_SYSROOT_PACKAGES) tools/build-mesa-sysroot.sh $(ROOTFS_VARS) | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) $(ROOTFS_ENV) -e MESA_SYSROOT_PACKAGES=$(MESA_SYSROOT_PACKAGES) $(MESA_BUILDER_TAG) \
		tools/build-mesa-sysroot.sh

MESA_TESTS_STAMP := $(BUILD)/mesa/tests/.done
mesa-tests: $(MESA_TESTS_STAMP)
$(MESA_TESTS_STAMP): $(wildcard ps3-gpu-tools/mesa-tests/*) $(MESA_SYSROOT)/.done | $(MESA_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(MESA_BUILDER_TAG) make -C ps3-gpu-tools/mesa-tests SYSROOT=/work/$(MESA_SYSROOT) \
		OUT=/work/$(BUILD)/mesa/tests
	@touch $@

# --- ClassiCube -----------------------------------------------------------------
CC_TARBALL := $(DOWNLOADS)/ClassiCube-$(CLASSICUBE_COMMIT).tar.gz
CC_TEXPACK := $(DOWNLOADS)/classicube-texpack-$(CLASSICUBE_TEXPACK_SHA256).zip
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

# Sounds and music, fetched and converted as the game's launcher would (classicube/sounds.sh)
CC_AUDIO := $(BUILD)/classicube/audio
classicube-audio: $(CC_AUDIO)/default2.zip
$(CC_AUDIO)/default2.zip: $(CC_TARBALL) classicube/sounds.sh | $(GAMES_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e CC_TARBALL=$(CC_TARBALL) $(GAMES_BUILDER_TAG) classicube/sounds.sh

classicube-deploy:
	$(MAKE) deploy PKGS=classicube

classicube: classicube-deb

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
SPU_VARS := $(BUILD)/spu.vars
SPU_VARS_NOW := $(foreach v,SPU_GCC_SHA256 SPU_BINUTILS_SHA256 SPU_NEWLIB_SHA256 SOURCE_DATE_EPOCH SPU_BUILDER_TAG,$(v)=$($(v)))
$(shell [ "$$(cat $(SPU_VARS) 2>/dev/null)" = "$(SPU_VARS_NOW)" ] || echo "$(SPU_VARS_NOW)" > $(SPU_VARS))
spu-toolchain: $(SPU_GCC)
$(SPU_GCC): $(SPU_TARBALLS) $(SPU_VARS) tools/build-spu-toolchain.sh $(wildcard spu-toolchain/gcc/*.patch spu-toolchain/binutils/*.patch spu-toolchain/newlib/*.patch spu-toolchain/newlib-*/*.patch) | $(SPU_BUILDER_STAMP)
	echo "$(SPU_GCC_SHA256)  $(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_BINUTILS_SHA256)  $(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_NEWLIB_SHA256)  $(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) -e SPU_GCC_VERSION=$(SPU_GCC_VERSION) \
		-e SPU_BINUTILS_VERSION=$(SPU_BINUTILS_VERSION) -e SPU_NEWLIB_VERSION=$(SPU_NEWLIB_VERSION) \
		$(SPU_BUILDER_TAG) tools/build-spu-toolchain.sh

# SPU test programs and the spufs runner (ps3-spu/src): the ps3-spu package
SPU_TESTS_STAMP := $(BUILD)/spu-tests/.done
spu-tests: $(SPU_TESTS_STAMP)
$(SPU_TESTS_STAMP): $(SPU_GCC) $(wildcard ps3-spu/src/*.c ps3-spu/src/Makefile) | $(SPU_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(SPU_BUILDER_TAG) make -C ps3-spu/src \
		SPU_CC=/work/$(SPU_GCC) OUT=/work/$(BUILD)/spu-tests
	@touch $@
spu-deploy:
	$(MAKE) deploy PKGS="ps3-spu ps3-spu-programs"

# --- Packages (our own apt repository, docs/PLAN.md item 7) -------------------
# Each package is built from its build outputs (tools/stage-package.sh, tools/build-debs.sh) into
# build/repo, which is indexed as an apt repository. N in the versions is the commit count.
# (ps3-boot, ps3-gpu-tools, ps3-spu, classicube, mesa, ps3rsx: source packages, make <pkg>-deb)
PACKAGES := ps3-kernel ps3-mesa
PKG_N := $(shell git -C $(CURDIR) rev-list --count HEAD 2>/dev/null || echo 0)
# Debian packaging tools and aptly (docker/Dockerfile.deb); also indexes the old pipeline's repo
REPO_BUILDER_TAG := ps3-deb-builder:$(shell (cat docker/Dockerfile.deb; echo $(BUILDER_TAG) $(DEBIAN_SNAPSHOT)) | sha256sum | cut -c1-12)
REPO_BUILDER_STAMP := $(BUILD)/.repo-builder-$(subst :,-,$(REPO_BUILDER_TAG))
$(REPO_BUILDER_STAMP): docker/Dockerfile.deb | $(BUILDER_STAMP)
	docker build -q -t $(REPO_BUILDER_TAG) -f docker/Dockerfile.deb --build-arg BUILDER=$(BUILDER_TAG) \
		--build-arg SNAPSHOT=$(DEBIAN_SNAPSHOT) docker
	@touch $@
PKG_INPUTS_ps3-kernel := $(KERNEL_STAGE)/boot/vmlinux
PKG_INPUTS_ps3-mesa :=
PKG_STAMPS := $(PACKAGES:%=$(BUILD)/repo/.%.stamp)
debs: $(PKG_STAMPS)
.SECONDEXPANSION:
$(BUILD)/repo/.%.stamp: $$(PKG_INPUTS_$$*) tools/stage-package.sh tools/build-debs.sh | $(REPO_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e MESA_PREFIX=$(MESA_PREFIX) -e KERNEL_VERSION=$(KERNEL_VERSION) \
		-e KERNEL_RELEASE=$(KERNEL_RELEASE) -e KERNEL_ARGS="$(KERNEL_ARGS)" -e MESA_VERSION=$(MESA_VERSION) \
		-e CLASSICUBE_COMMIT=$(CLASSICUBE_COMMIT) -e PKG_N=$(PKG_N) $(REPO_BUILDER_TAG) tools/build-debs.sh $*
	@touch $@

# Deploy to the running PS3 through the dev suite (tools/deploy.sh): make deploy [PKGS="..."]
# [AUTOREMOVE=1]. Upgrades everything of ours that is installed (Mesa included), then installs the
# named top-level packages; simulated first. dpkg replaces files by renaming, so even Mesa under
# a running X is safe on the USB root (on NFS, stop X first).
DEPLOY_TOP := linux-image-ps3 linux-image-ps3-release ps3rsx-modules-ps3 ps3rsx-modules-ps3-release ps3-boot ps3-system ps3-desktop ps3-gpu-tools ps3-spu ps3-spu-programs classicube
PKGS ?= $(DEPLOY_TOP)
AUTOREMOVE ?= 0
STOP_DESKTOP ?= auto
PS3_SCP = scp -q -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts
deploy: $(BUILD)/repo/.ps3-mesa.stamp | $(REPO_BUILDER_STAMP)
	PS3=tibi@$(PS3_IP) SSH="ssh -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts" \
		SCP="$(PS3_SCP)" REPO_BUILDER_TAG=$(REPO_BUILDER_TAG) REPO_HOST=$(REPO_HOST) REPO_DIR=$(REPO_DIR) \
		AUTOREMOVE=$(AUTOREMOVE) STOP_DESKTOP=$(STOP_DESKTOP) tools/deploy.sh $(PKGS)
kernel-deploy:
	$(MAKE) deploy PKGS=ps3-kernel-$(KERNEL_RELEASE)

# --- Image --------------------------------------------------------------------
# The package layer plus our config (image/) and our packages (build/repo)
IMAGE_INPUTS := $(ROOTFS_BASE) $(wildcard image/*) tools/build-image.sh $(SSH_PUBKEY) $(BUILD)/repo/.ps3-mesa.stamp \
	$(wildcard $(BUILD)/debs/*/*.deb)
rootfs: $(ROOTFS_TAR)
IMAGE_VARS := $(BUILD)/image.vars
BOOT_ROOT_ARGS := $(ROOT_ARGS_usb) rootwait
IMAGE_VARS_NOW := $(BOOT_ROOT_ARGS) $(DEBUG_ARGS) $(LEGACY_KERNEL_ARGS) $(REPO_SUITE)
$(shell [ "$$(cat $(IMAGE_VARS) 2>/dev/null)" = "$(IMAGE_VARS_NOW)" ] || echo "$(IMAGE_VARS_NOW)" > $(IMAGE_VARS))
$(ROOTFS_TAR): $(IMAGE_INPUTS) $(IMAGE_VARS) | $(REPO_BUILDER_STAMP)
	@mkdir -p $(BUILD)/rootfs
	cp $(SSH_PUBKEY) $(BUILD)/rootfs/authorized_keys
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) -e BOOT_ROOT_ARGS="$(BOOT_ROOT_ARGS)" \
		-e BOOT_EXTRA_ARGS="$(DEBUG_ARGS)" -e BOOT_LEGACY_ARGS="$(LEGACY_KERNEL_ARGS)" \
		-e KERNEL_RELEASE=$(KERNEL_RELEASE) -e REPO_SUITE=$(REPO_SUITE) -e REPO_URL=$(REPO_URL) $(REPO_BUILDER_TAG) tools/build-image.sh
	rm -f $(BUILD)/rootfs/authorized_keys

# --- Run ----------------------------------------------------------------------
netboot: $(KERNEL_STAGE)/boot/vmlinux
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$(KERNEL_STAGE)/boot/vmlinux \
		KERNEL_CMDLINE="$(KERNEL_CMDLINE)" tools/netboot.sh

# Netboot a kernel from the Debian kernel packages (make linux-deb) before installing it:
# make netboot-deb [FLAVOUR=ps3|ps3-release] [ROOT=nfs|usb]; its own arguments (linux/flavours/)
FLAVOUR ?= ps3
netboot-deb: | $(REPO_BUILDER_STAMP)
	@set -e; d=$$(ls $(BUILD)/debs/linux/linux-image-*-$(FLAVOUR)_*.deb | grep -v dbg | head -1); \
	[ -n "$$d" ] || { echo "no linux-image for $(FLAVOUR): make linux-deb"; exit 1; }; \
	rm -rf $(BUILD)/tmp/netboot && mkdir -p $(BUILD)/tmp/netboot; \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) $(REPO_BUILDER_TAG) dpkg-deb -x $$d $(BUILD)/tmp/netboot; \
	k=$$(ls $(BUILD)/tmp/netboot/boot/vmlinux-*); echo ">> netbooting $$k"; \
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$$k \
		KERNEL_CMDLINE="$(ROOT_ARGS_$(ROOT)) rootwait $(DEBUG_ARGS) $$(cat linux/flavours/$(FLAVOUR).args)" tools/netboot.sh

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

# --- Repository and workspace ------------------------------------------------------------
# Publish packages to the repository server (tools/repo-publish.sh). DEBS: the files to publish
# (default: the current debs of the package pipeline); DIST=dev for throwaway test builds.
DEBS ?= $(wildcard $(BUILD)/repo/*.deb)
DIST ?= ps3
publish: | $(REPO_BUILDER_STAMP)
	@tools/disk-check.sh
	REPO_BUILDER_TAG=$(REPO_BUILDER_TAG) REPO_HOST=$(REPO_HOST) REPO_DIR=$(REPO_DIR) DIST=$(DIST) \
		tools/repo-publish.sh $(DEBS)

# Freeze the stable suite as img-<stamp> for reproducible images (REPO_SUITE=img-<stamp>)
repo-snapshot: | $(REPO_BUILDER_STAMP)
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e HOME=/tmp $(REPO_BUILDER_TAG) tools/repo-snapshot.sh
	$(MAKE) --no-print-directory publish DEBS=

# The repository's `deps` component: Debian packages listed in image/deps.txt, from the snapshot
deps: | $(REPO_BUILDER_STAMP)
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e HOME=/tmp -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) \
		$(REPO_BUILDER_TAG) tools/deps-update.sh
	$(MAKE) --no-print-directory publish DEBS=

# Disk use of build/ (on /home, nearly full) and what may be deleted (CLAUDE.md: everything but
# build/aptly and patch work trees with unexported commits)
du:
	@df -h $(BUILD) | tail -1; du -sh $(BUILD)/* 2>/dev/null | sort -rh | head -25
clean-ephemeral:
	@echo "deleting build/old, build/tmp, superseded experiment trees (keeping aptly, work trees, current outputs)"
	docker run --rm -v $(CURDIR)/$(BUILD):/b alpine:3.20 sh -c 'cd /b && rm -rf old tmp/* spu-port spu-*-ref 2>/dev/null; true'
	docker image prune -f >/dev/null; docker builder prune -f >/dev/null
	@$(MAKE) --no-print-directory du

# A builder stamp is only as good as its image: if Docker no longer has the image (pruned), drop the
# stamp so the image is rebuilt rather than the build failing
$(foreach b,BUILDER MESA_BUILDER GAMES_BUILDER SPU_BUILDER REPO_BUILDER,\
	$(if $(wildcard $($(b)_STAMP)),$(shell docker image inspect $($(b)_TAG) >/dev/null 2>&1 || rm -f $($(b)_STAMP))))

# --- Debian source packages (deb-src + our patches; docs/PLAN.md, packaging plan) ---------------
DEB_ENV := -e OWNER=$(UID_GID) -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) -e HOME=/tmp \
	-e CC_TARBALL=$(CC_TARBALL) -e CC_TEXPACK=$(CC_TEXPACK) -e CC_AUDIO=$(CC_AUDIO) -e KERNEL_RELEASE=$(KERNEL_RELEASE) -e KERNEL_FLAVOUR=$(KERNEL_FLAVOUR)
# upstream tarballs a package's source is made from
classicube-src: $(CC_TARBALL) $(CC_TEXPACK) $(CC_AUDIO)/default2.zip
# ps3rsx for each kernel flavour built by linux-deb (build/debs/linux): build/debs/ps3rsx-<flavour>
ps3rsx-modules:
	@set -e; for f in ps3 ps3-release; do \
		d=$$(ls $(BUILD)/debs/linux/linux-image-*-$${f}_*.deb | grep -v dbg | head -1); \
		[ -n "$$d" ] || { echo "no linux-image for $$f: make linux-deb"; exit 1; }; \
		rel=$$(basename $$d | sed 's/^linux-image-//; s/_.*//'); echo ">> ps3rsx for $$rel"; \
		$(MAKE) --no-print-directory ps3rsx-src ps3rsx-deb KERNEL_RELEASE=$$rel KERNEL_FLAVOUR=$$f; \
	done
chroot: | $(REPO_BUILDER_STAMP)
	@tools/disk-check.sh
	docker run --rm --privileged -v $(CURDIR):/work -w /work $(DEB_ENV) $(REPO_BUILDER_TAG) tools/deb-chroot.sh
$(BUILD)/chroot/sid-ppc64.tar:
	$(MAKE) --no-print-directory chroot
# make <pkg>-src: fetch Debian's source and make ours; make <pkg>-deb: build it natively (qemu)
%-src: | $(REPO_BUILDER_STAMP)
	@if grep -q '^TYPE=debsrc' $*/package.conf; then \
		docker run --rm -v $(CURDIR):/work -w /work $(DEB_ENV) $(REPO_BUILDER_TAG) tools/deb-fetch.sh $*; fi
	@set -e; v=$$(tools/deb-version.sh $*); \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) $(DEB_ENV) $(REPO_BUILDER_TAG) \
		tools/deb-prepare.sh $* "$$(echo "$$v" | sed -n 1p)" "$$(echo "$$v" | sed -n 2p)"
%-deb: %-src | $(BUILD)/chroot/sid-ppc64.tar
	@tools/disk-check.sh
	docker run --rm --privileged -v $(CURDIR):/work -w /work $(DEB_ENV) -e JOBS=$(DEB_JOBS) -e KEEP_TREE=$(KEEP_TREE) \
		$(REPO_BUILDER_TAG) tools/deb-build.sh $*
DEB_JOBS ?= 16
# Changing a Debian-source package's patches (docs/PACKAGING.md): make <pkg>-work makes a git tree
# in build/work/<pkg> (Debian's tree, then one commit per patch of ours; FORCE=1 discards unsaved
# commits), make <pkg>-patches writes the commits back to <pkg>/patches and <pkg>/packaging,
# make <pkg>-rebase VERSION=<debver> moves our commits onto a newer Debian version
%-work: | $(REPO_BUILDER_STAMP)
	docker run --rm -v $(CURDIR):/work -w /work $(DEB_ENV) $(REPO_BUILDER_TAG) tools/deb-fetch.sh $*
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e HOME=/tmp -e FORCE=$(FORCE) \
		$(REPO_BUILDER_TAG) tools/pkg-work.sh $*
%-patches: | $(REPO_BUILDER_STAMP)
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e HOME=/tmp $(REPO_BUILDER_TAG) tools/pkg-export.sh $*
%-rebase: | $(REPO_BUILDER_STAMP)
	@[ -n "$(VERSION)" ] || { echo "make $*-rebase VERSION=<new Debian version>"; exit 1; }
	docker run --rm -v $(CURDIR):/work -w /work $(DEB_ENV) -e NEWVERSION=$(VERSION) $(REPO_BUILDER_TAG) tools/pkg-rebase.sh fetch $*
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e HOME=/tmp -e NEWVERSION=$(VERSION) \
		$(REPO_BUILDER_TAG) tools/pkg-rebase.sh rebase $*
# KEEP_TREE=1: make linux-deb keeps the cross build tree (build/chroot/cross-linux)
KEEP_TREE ?= 0
# All our packages: the Debian source packages and the two still made by the old pipeline
# (the kernel until milestone 4; the transitional ps3-mesa)
# the kernel first: ps3rsx builds against its headers
SRC_PACKAGES := linux mesa ps3-boot ps3-gpu-tools ps3-spu classicube ps3-system ps3-desktop
packages: $(SRC_PACKAGES:%=%-deb) debs
	$(MAKE) --no-print-directory ps3rsx-modules
