# PS3 Linux build. Settings are in config.mk; run `make help` for targets.
include config.mk

JOBS        ?= $(shell nproc)
UID_GID     := $(shell id -u):$(shell id -g)

BUILD       := build
DOWNLOADS   := $(BUILD)/downloads
ROOTFS_BASE    := $(BUILD)/rootfs/base.tar
ROOTFS_TAR     := $(BUILD)/rootfs/debian-ppc64.tar

# Builder image tag changes whenever its inputs change (Dockerfile, base image, snapshot)
BUILDER_TAG := ps3-builder:$(shell (cat docker/Dockerfile; echo $(BUILDER_BASE)) | sha256sum | cut -c1-12)-$(BUILDER_SNAPSHOT)
BUILDER_STAMP := $(BUILD)/.builder-$(subst :,-,$(BUILDER_TAG))

DOCKER_RUN := docker run --rm -v $(CURDIR):/work -w /work \
	-e SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) -e JOBS=$(JOBS)

ROOTFS_ENV := -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) \
	-e ROOTFS_PACKAGES=$(ROOTFS_PACKAGES) -e ROOTFS_CUSTOMIZE=$(ROOTFS_CUSTOMIZE) -e OWNER=$(UID_GID)

# The base layer depends on these settings only, not on all of config.mk: this file is rewritten
# only when they change, so e.g. an IP change doesn't trigger a rebuild.
ROOTFS_VARS := $(BUILD)/rootfs.vars
ROOTFS_VARS_NOW := $(foreach v,DEBIAN_SNAPSHOT DEBIAN_SUITE DEBIAN_MIRROR SOURCE_DATE_EPOCH BUILDER_TAG,$(v)=$($(v)))
$(shell mkdir -p $(BUILD); [ "$$(cat $(ROOTFS_VARS) 2>/dev/null)" = "$(ROOTFS_VARS_NOW)" ] || echo "$(ROOTFS_VARS_NOW)" > $(ROOTFS_VARS))
ROOTFS_BASE_INPUTS := $(ROOTFS_PACKAGES) tools/build-rootfs.sh $(ROOTFS_VARS)

.PHONY: repo-snapshot publish deps du clean-ephemeral chroot packages ps3rsx-modules netboot-deb spu-toolchain spu-tests spu-deploy help all builder rootfs rootfs-base checksums nfs-root usb-root usb-root-petitboot deploy listen shell clean gpu-deploy ps3-reboot bench-push bench-pull \
	classicube classicube-audio classicube-deploy classicube-bench quake-data

help:
	@echo "Packages (docs/PACKAGING.md):"
	@echo "  make packages     all our packages -> $(BUILD)/debs/<pkg> (kernel first, ps3rsx last)"
	@echo "  make <pkg>-deb    one source package (linux, mesa, ps3-desktop, ...); make ps3rsx-modules"
	@echo "                    the RSX driver for both kernels in $(BUILD)/debs/linux"
	@echo "  make quake-data   Quake shareware and Quake II demo data debs (game-data-packager; the published"
	@echo "                    files when the ps3 suite has them) -> $(BUILD)/debs/quake-data"
	@echo "Image:"
	@echo "  make rootfs       the PS3's root filesystem image -> $(ROOTFS_TAR)"
	@echo "                    (package layer + our config and packages: kernels, RSX driver, Mesa, games)"
	@echo "  make rootfs-base  only its package layer (slow: ~15 min, reruns when packages change)"
	@echo "  make checksums    sha256 of the image and packages (compare two builds for reproducibility)"
	@echo "Install the image (sudo):"
	@echo "  make nfs-root     install the image into $(NFS_ROOT) and export it (keeps SSH host"
	@echo "                    keys, Bluetooth pairings and Wi-Fi connections; FRESH=1 doesn't)"
	@echo "  make usb-root     install the image onto the USB disk on the PS3 (FORMAT=1: partition"
	@echo "                    it first, erasing it); boot it with make netboot-deb ROOT=usb"
	@echo "Update the running PS3 without reinstalling (fixes still go into the build):"
	@echo "  make deploy       install our packages on the running PS3 (PKGS=\"ps3-desktop ...\"; KERNELS=1"
	@echo "                    upgrades kernels too; DRY_RUN=1 simulates)"
	@echo "  make gpu-deploy | classicube-deploy | spu-deploy"
	@echo "Run:"
	@echo "  make netboot-deb  boot a kernel from $(BUILD)/debs/linux from Petitboot (FLAVOUR=release|debug,"
	@echo "                    ROOT=nfs|usb)"
	@echo "  make listen       print PS3 early-debug + netconsole output"
	@echo "  make ps3-reboot   reboot the running PS3 Linux (lands in GameOS/XMB, not Petitboot)"
	@echo "  make bench-push   the game suite runner to the PS3 (/var/tmp/bench/run.sh, docs/GAME-PLAN.md)"
	@echo "  make bench-pull   its results into $(BUILD)/bench"
	@echo "  make classicube-bench  ClassiCube's benchmark plugin -> games/bench/assets/classicube-bench.so"
	@echo "Other:"
	@echo "  make shell        shell in the builder container"
	@echo "  make clean        move build outputs to $(BUILD)/old/ (never deletes)"

all:
	$(MAKE) --no-print-directory packages
	$(MAKE) --no-print-directory rootfs

# --- Builder container --------------------------------------------------------
builder: $(BUILDER_STAMP)
$(BUILDER_STAMP): docker/Dockerfile
	docker build -q -t $(BUILDER_TAG) \
		--build-arg BASE=$(BUILDER_BASE) --build-arg SNAPSHOT=$(BUILDER_SNAPSHOT) docker
	@mkdir -p $(BUILD) && touch $@

# --- Root filesystem ----------------------------------------------------------
# The image is built in two layers: the packages (slow, rarely changes), then everything of ours
# on top (fast). `make rootfs` itself is in the Image section below, after its inputs.
rootfs-base: $(ROOTFS_BASE)
$(ROOTFS_BASE): $(ROOTFS_BASE_INPUTS) | $(BUILDER_STAMP)
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) $(BUILDER_TAG) tools/build-rootfs.sh

# The image and every package we built (the kernels' vmlinux is in the linux-image debs)
checksums:
	@cd $(BUILD) && sha256sum rootfs/debian-ppc64.tar \
		$$(find debs -mindepth 2 -maxdepth 2 -name '*.deb' | sort) > SHA256SUMS
	@head -2 $(BUILD)/SHA256SUMS; echo "($$(wc -l < $(BUILD)/SHA256SUMS) files in $(BUILD)/SHA256SUMS)"

# --- Deploy -------------------------------------------------------------------
# The whole system is in the image (our packages, kernels included), so a fresh root needs nothing
# done by hand; boot it with make netboot-deb. The *-deploy targets install freshly built packages
# on the running PS3 (see Packages).
nfs-root: $(ROOTFS_TAR)
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) FRESH=$(FRESH) tools/install-nfs-root.sh $(ROOTFS_TAR)

# The image onto the USB disk from Petitboot (the disk isn't in use there, so this replaces even the
# root the PS3 normally runs from); the disk must have been set up with make usb-root FORMAT=1
usb-root-petitboot: $(ROOTFS_TAR)
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) FRESH=$(FRESH) \
		tools/install-usb-root-petitboot.sh $(ROOTFS_TAR)

# The image onto the USB disk plugged into the PS3 (running Linux, over SSH); FORMAT=1 partitions
# the disk first (erases it). Boot it with make netboot-deb ROOT=usb.
usb-root: $(ROOTFS_TAR)
	PS3_SSH="$(PS3_SSH)" USB_DISK_ID="$(USB_DISK_ID)" FORMAT=$(FORMAT) FRESH=$(FRESH) \
		tools/install-usb-root.sh $(ROOTFS_TAR)

# --- GPU tools (ps3-gpu-tools and ps3rsx are source packages: make ps3-gpu-tools-deb ps3rsx-modules) ---
gpu-deploy:
	$(MAKE) deploy PKGS="ps3rsx-modules-ps3 ps3-gpu-tools"

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

# Game asset tools (oggdec) on top of the builder
GAMES_BUILDER_TAG := ps3-games-builder:$(shell (cat docker/Dockerfile.games; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
GAMES_BUILDER_STAMP := $(BUILD)/.games-builder-$(subst :,-,$(GAMES_BUILDER_TAG))
$(GAMES_BUILDER_STAMP): docker/Dockerfile.games | $(BUILDER_STAMP)
	docker build -q -t $(GAMES_BUILDER_TAG) -f docker/Dockerfile.games --build-arg BUILDER=$(BUILDER_TAG) docker
	@touch $@

# Sounds and music, fetched and converted as the game's launcher would (classicube/sounds.sh)
CC_AUDIO := $(BUILD)/classicube/audio
classicube-audio: $(CC_AUDIO)/default2.zip
$(CC_AUDIO)/default2.zip: $(CC_TARBALL) classicube/sounds.sh | $(GAMES_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e CC_TARBALL=$(CC_TARBALL) $(GAMES_BUILDER_TAG) classicube/sounds.sh

classicube-deploy:
	$(MAKE) deploy PKGS=classicube

# Quake's shareware and Quake II's demo data (QuakeSpasm, Yamagi Quake II) from game-data-packager
# (tools/game-data.sh): non-free, our LAN repository only. A version already in the ps3 suite is
# downloaded from there, never made again (its output isn't reproducible); REGEN=1 makes it anyway.
quake-data: | $(GAMES_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e HOME=/tmp -e REPO_URL=$(REPO_URL) -e REGEN=$(REGEN) $(GAMES_BUILDER_TAG) \
		tools/game-data.sh $(BUILD)/debs/quake-data quake:quake-shareware quake2:quake2-demo-data

classicube: classicube-deb

# ClassiCube's benchmark plugin (games/bench/classicube-bench: a camera path, then the frame rate),
# against the pinned source's headers, cross-built in the builder (the snapshot's GCC, as the
# package) -> games/bench/assets/classicube-bench.so, tracked so that make bench-push carries it
CC_BENCH_SRC := $(BUILD)/classicube/bench-src
classicube-bench: games/bench/assets/classicube-bench.so
games/bench/assets/classicube-bench.so: games/bench/classicube-bench/ccbench.c games/bench/classicube-bench/Makefile \
		$(CC_TARBALL) | $(BUILDER_STAMP)
	rm -rf $(CC_BENCH_SRC); mkdir -p $(CC_BENCH_SRC)
	tar -xzf $(CC_TARBALL) -C $(CC_BENCH_SRC) --strip-components=1
	$(DOCKER_RUN) --user $(UID_GID) $(BUILDER_TAG) \
		make -C games/bench/classicube-bench CC_SRC=/work/$(CC_BENCH_SRC) OUT=/work/$@
	rm -rf $(CC_BENCH_SRC)

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

# SPU test programs (ps3-spu/spu), spurun, libspeq and speq-dxt (cross-built for the PPE), outside
# the package build: the ps3-spu package
SPU_TESTS_STAMP := $(BUILD)/spu-tests/.done
spu-tests: $(SPU_TESTS_STAMP)
$(SPU_TESTS_STAMP): $(SPU_GCC) $(wildcard ps3-spu/Makefile ps3-spu/*/*.[ch] ps3-spu/lib/*.map) | $(SPU_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(SPU_BUILDER_TAG) make -C ps3-spu ppe spu \
		CC=powerpc64-linux-gnu-gcc CFLAGS="-O2 -mcpu=cell" SPU_CC=/work/$(SPU_GCC) OUT=/work/$(BUILD)/spu-tests
	@touch $@
spu-deploy:
	$(MAKE) deploy PKGS="ps3-spu ps3-spu-programs"

# --- Packages (Debian source packages, docs/PACKAGING.md; the targets are at the end) -------------
# Debian packaging tools and aptly (docker/Dockerfile.deb): builds, publishes, extracts packages
REPO_BUILDER_TAG := ps3-deb-builder:$(shell (cat docker/Dockerfile.deb; echo $(BUILDER_TAG) $(DEBIAN_SNAPSHOT)) | sha256sum | cut -c1-12)
REPO_BUILDER_STAMP := $(BUILD)/.repo-builder-$(subst :,-,$(REPO_BUILDER_TAG))
$(REPO_BUILDER_STAMP): docker/Dockerfile.deb | $(BUILDER_STAMP)
	docker build -q -t $(REPO_BUILDER_TAG) -f docker/Dockerfile.deb --build-arg BUILDER=$(BUILDER_TAG) \
		--build-arg SNAPSHOT=$(DEBIAN_SNAPSHOT) docker
	@touch $@

# Deploy to the running PS3 through the dev suite (tools/deploy.sh): make deploy [PKGS="..."]
# [AUTOREMOVE=1] [KERNELS=1] [DRY_RUN=1]. Upgrades everything of ours that is installed (Mesa
# included; kernels and ps3rsx modules only with KERNELS=1 or when named), then installs the named
# top-level packages; simulated first. dpkg replaces files by renaming, so even Mesa under a
# running X is safe on the USB root (on NFS, stop X first).
DEPLOY_TOP := linux-image-ps3 linux-image-ps3-debug ps3rsx-modules-ps3 ps3rsx-modules-ps3-debug ps3-boot ps3-system ps3-desktop ps3-splash ps3-gpu-tools ps3-spu ps3-spu-programs libspeq0 ps3-apps ps3-games classicube
PKGS ?= $(DEPLOY_TOP)
AUTOREMOVE ?= 0
STOP_DESKTOP ?= auto
PS3_SCP = scp -q -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts
deploy: | $(REPO_BUILDER_STAMP)
	PS3=tibi@$(PS3_IP) SSH="ssh -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts" \
		SCP="$(PS3_SCP)" REPO_BUILDER_TAG=$(REPO_BUILDER_TAG) REPO_HOST=$(REPO_HOST) REPO_DIR=$(REPO_DIR) \
		AUTOREMOVE=$(AUTOREMOVE) STOP_DESKTOP=$(STOP_DESKTOP) DRY_RUN=$(DRY_RUN) KERNELS=$(KERNELS) tools/deploy.sh $(PKGS)

# --- Image --------------------------------------------------------------------
# The package layer plus our config (image/) and our packages (build/debs/*)
IMAGE_INPUTS := $(ROOTFS_BASE) $(wildcard image/*) tools/build-image.sh $(SSH_PUBKEY) \
	$(wildcard $(BUILD)/debs/*/*.deb)
rootfs: $(ROOTFS_TAR)
IMAGE_VARS := $(BUILD)/image.vars
BOOT_ROOT_ARGS := $(ROOT_ARGS_usb) rootwait
IMAGE_VARS_NOW := $(BOOT_ROOT_ARGS) $(DEBUG_ARGS) $(REPO_SUITE)
$(shell [ "$$(cat $(IMAGE_VARS) 2>/dev/null)" = "$(IMAGE_VARS_NOW)" ] || echo "$(IMAGE_VARS_NOW)" > $(IMAGE_VARS))
$(ROOTFS_TAR): $(IMAGE_INPUTS) $(IMAGE_VARS) | $(REPO_BUILDER_STAMP)
	@mkdir -p $(BUILD)/rootfs
	cp $(SSH_PUBKEY) $(BUILD)/rootfs/authorized_keys
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) -e BOOT_ROOT_ARGS="$(BOOT_ROOT_ARGS)" \
		-e BOOT_EXTRA_ARGS="$(DEBUG_ARGS)" \
		-e REPO_SUITE=$(REPO_SUITE) -e REPO_URL=$(REPO_URL) $(REPO_BUILDER_TAG) tools/build-image.sh
	rm -f $(BUILD)/rootfs/authorized_keys

# --- Run ----------------------------------------------------------------------
# The kernel image package of a flavour in build/debs/linux (make linux-deb): kernel_deb release
# (linux-image-6.12.111+ps3.N) or kernel_deb debug (linux-image-6.12.111+ps3.N-debug<stamp>)
KERNEL_DEB_SH = kernel_deb() { ls $(BUILD)/debs/linux/linux-image-[0-9]*_*.deb 2>/dev/null | \
	if [ "$$1" = debug ]; then grep -e '-debug[0-9]*_'; else grep -v -e '-debug[0-9]*_'; fi | head -1; }
# Netboot a kernel from the Debian kernel packages (make linux-deb) before installing it:
# make netboot-deb [FLAVOUR=release|debug] [ROOT=nfs|usb]; with the arguments its package ships
FLAVOUR ?= release
netboot-deb: | $(REPO_BUILDER_STAMP)
	@set -e; $(KERNEL_DEB_SH); d=$$(kernel_deb $(FLAVOUR)); \
	[ -n "$$d" ] || { echo "no linux-image for $(FLAVOUR): make linux-deb"; exit 1; }; \
	rm -rf $(BUILD)/tmp/netboot && mkdir -p $(BUILD)/tmp/netboot; \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) $(REPO_BUILDER_TAG) dpkg-deb -x $$d $(BUILD)/tmp/netboot; \
	k=$$(ls $(BUILD)/tmp/netboot/boot/vmlinux-*); echo ">> netbooting $$k"; \
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$$k \
		KERNEL_CMDLINE="$(ROOT_ARGS_$(ROOT)) rootwait $(DEBUG_ARGS) $$(cat $(BUILD)/tmp/netboot/usr/lib/ps3/boot/*.args)" tools/netboot.sh

listen:
	tools/listen-debug.sh

PS3_SSH := ssh -i $(HOME)/.ssh/id_ps3 -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts tibi@$(PS3_IP)
ps3-reboot:
	-$(PS3_SSH) sudo systemctl reboot

# The game suite runner (games/bench, docs/GAME-PLAN.md) to /var/tmp/bench on the PS3: unpacked
# beside the old copy and swapped in with a rename, so a run in progress keeps reading its files.
# bench-pull: the PS3's run directories (/var/tmp/ps3-bench) into $(BUILD)/bench
bench-push:
	tar -C games -c bench | $(PS3_SSH) 'set -e; rm -rf /var/tmp/bench.new /var/tmp/bench.old; \
		mkdir /var/tmp/bench.new; tar -C /var/tmp/bench.new --strip-components=1 -x; \
		[ ! -e /var/tmp/bench ] || mv /var/tmp/bench /var/tmp/bench.old; mv /var/tmp/bench.new /var/tmp/bench'
bench-pull:
	mkdir -p $(BUILD)/bench
	$(PS3_SSH) 'tar -C /var/tmp/ps3-bench -c --exclude=perf.data .' | tar -C $(BUILD)/bench -x
	@echo ">> $(BUILD)/bench; summaries: games/bench/report.py $(BUILD)/bench/<run dirs>"

shell: $(BUILDER_STAMP)
	$(DOCKER_RUN) -it --user $(UID_GID) $(BUILDER_TAG) bash

clean:
	@ts=$$(date +%Y%m%d-%H%M%S); mkdir -p $(BUILD)/old/$$ts; \
	for d in rootfs debs; do if [ -e $(BUILD)/$$d ]; then mv $(BUILD)/$$d $(BUILD)/old/$$ts/; fi; done; \
	echo "moved outputs to $(BUILD)/old/$$ts"

# --- Repository and workspace ------------------------------------------------------------
# Publish packages to the repository server (tools/repo-publish.sh). DEBS: the files to publish
# (default: every current build in build/debs/*, debug symbols to ps3-dbgsym); DIST=dev for
# throwaway test builds.
DEBS ?= $(wildcard $(BUILD)/debs/*/*.deb)
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
$(foreach b,BUILDER GAMES_BUILDER SPU_BUILDER REPO_BUILDER,\
	$(if $(wildcard $($(b)_STAMP)),$(shell docker image inspect $($(b)_TAG) >/dev/null 2>&1 || rm -f $($(b)_STAMP))))

# --- Debian source packages (deb-src + our patches; docs/PLAN.md, packaging plan) ---------------
DEB_ENV := -e OWNER=$(UID_GID) -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) -e HOME=/tmp \
	-e CC_TARBALL=$(CC_TARBALL) -e CC_TEXPACK=$(CC_TEXPACK) -e CC_AUDIO=$(CC_AUDIO) -e KERNEL_RELEASE=$(KERNEL_RELEASE) -e KERNEL_FLAVOUR=$(KERNEL_FLAVOUR)
# upstream tarballs a package's source is made from
classicube-src: $(CC_TARBALL) $(CC_TEXPACK) $(CC_AUDIO)/default2.zip
# KERNEL_RELEASE and KERNEL_FLAVOUR are set only by ps3rsx-modules (below), per kernel.
# ps3rsx for both kernels built by linux-deb (build/debs/linux), release and debug: modules
# ps3rsx-modules-<release>, metas ps3rsx-modules-ps3 and ps3rsx-modules-ps3-debug (named like the
# kernel's: KERNEL_FLAVOUR is the Debian flavour), in build/debs/ps3rsx-<Debian flavour>
ps3rsx-modules:
	@set -e; $(KERNEL_DEB_SH); for f in release:ps3 debug:ps3-debug; do \
		d=$$(kernel_deb $${f%%:*}); \
		[ -n "$$d" ] || { echo "no linux-image for $${f%%:*}: make linux-deb"; exit 1; }; \
		rel=$$(basename $$d | sed 's/^linux-image-//; s/_.*//'); echo ">> ps3rsx for $$rel"; \
		$(MAKE) --no-print-directory ps3rsx-src ps3rsx-deb KERNEL_RELEASE=$$rel KERNEL_FLAVOUR=$${f#*:}; \
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
		tools/deb-prepare.sh $* "$$(echo "$$v" | sed -n 1p)" "$$(echo "$$v" | sed -n 2p)" "$$(echo "$$v" | sed -n 3p)"
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
# All our packages, the kernel first: ps3rsx builds against its headers
SRC_PACKAGES := linux mesa plymouth ps3-boot ps3-gpu-tools ps3-spu classicube fluidsynth dsda-doom neverball supertuxkart ps3-system ps3-desktop ps3-splash
packages: $(SRC_PACKAGES:%=%-deb)
	$(MAKE) --no-print-directory ps3rsx-modules
