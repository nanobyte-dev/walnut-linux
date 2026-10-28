# PS3 Linux build. Settings are in config.mk; run `make help` for targets.
include config.mk
# the shared ccache's login (config.local.mk, or the CI secret) reaches tools/ccache-remote.sh through
# the environment, never on a command line make prints
export WALNUT_CCACHE_AUTH
# Our source packages: every directory with a package.conf where tools/pkg-dir.sh looks (packages/<group>/
# [<area>/]<name>/, a game's or engine's packages/<name>/ and licensed/<name>/ in gamedb/; docs/LAYOUT.md),
# named by their directory alone (make <name>-deb, build/debs/<name>); two of one name stop make here.
# $(call pkgdir,<name>) is the directory
PKG_DIRS := $(shell tools/pkg-dir.sh --all)
$(if $(filter 0,$(.SHELLSTATUS)),,$(error tools/pkg-dir.sh --all failed (above)))
pkgdir = $(filter %/$(1),$(PKG_DIRS))
# each package's upstream pins (<pkg dir>/upstream.mk; config.mk says which), the SPU toolchain's
include $(sort $(wildcard $(PKG_DIRS:%=%/upstream.mk))) packages/patched/toolchain/spu/upstream.mk tools/ssdimg/upstream.mk

JOBS        ?= $(shell nproc)
UID_GID     := $(shell id -u):$(shell id -g)
# $(call need,VAR...) in a recipe: stops with a message when a config.local.mk setting is empty
need = $(foreach v,$1,$(if $($v),,$(error $v is empty: set it in config.local.mk (see config.local.mk.example))))

BUILD       := build
DOWNLOADS   := $(BUILD)/downloads
ROOTFS_BASE    := $(BUILD)/rootfs/base.tar
ROOTFS_TAR     := $(BUILD)/rootfs/debian-ppc64.tar

# Builder image tag changes whenever its inputs change (Dockerfile, base image, snapshot)
BUILDER_TAG := ps3-builder:$(shell (cat build-containers/Dockerfile; echo $(BUILDER_BASE)) | sha256sum | cut -c1-12)-$(BUILDER_SNAPSHOT)
BUILDER_STAMP := $(BUILD)/.builder-$(subst :,-,$(BUILDER_TAG))

DOCKER_RUN := docker run --rm -v $(CURDIR):/work -w /work \
	-e SOURCE_DATE_EPOCH=$(SOURCE_DATE_EPOCH) -e JOBS=$(JOBS)

# the games' packages in the image's package layer (gamedb build.toml image_base): system/image/packages.txt
# names only generic ones
ROOTFS_GAMES := $(shell python3 -I tools/gamedb/build-info.py image-base)
$(if $(filter 0,$(.SHELLSTATUS)),,$(error tools/gamedb/build-info.py image-base failed (above)))
ROOTFS_ENV := -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) \
	-e ROOTFS_PACKAGES=$(ROOTFS_PACKAGES) -e ROOTFS_GAMES="$(ROOTFS_GAMES)" -e ROOTFS_CUSTOMIZE=$(ROOTFS_CUSTOMIZE) \
	-e OWNER=$(UID_GID)

# The base layer depends on these settings only, not on all of config.mk: this file is rewritten
# only when they change, so e.g. an IP change doesn't trigger a rebuild.
ROOTFS_VARS := $(BUILD)/rootfs.vars
ROOTFS_VARS_NOW := $(foreach v,DEBIAN_SNAPSHOT DEBIAN_SUITE DEBIAN_MIRROR SOURCE_DATE_EPOCH BUILDER_TAG ROOTFS_GAMES,$(v)=$($(v)))
$(shell mkdir -p $(BUILD); [ "$$(cat $(ROOTFS_VARS) 2>/dev/null)" = "$(ROOTFS_VARS_NOW)" ] || echo "$(ROOTFS_VARS_NOW)" > $(ROOTFS_VARS))
ROOTFS_BASE_INPUTS := $(ROOTFS_PACKAGES) tools/build-rootfs.sh $(ROOTFS_VARS)

.PHONY: xcc-toolchain gamedb-catalog gamedb-app-pages walnut-server walnut-site walnut-server-test public-push initrd-ssd initrd-walnut initrd-busybox publish du clean-ephemeral chroot packages ps3rsx-modules snd-ps3-ko netboot-deb spu-toolchain spu-tests spu-deploy help all builder rootfs rootfs-base checksums nfs-root usb-root usb-root-petitboot deploy listen shell clean gpu-deploy test test-slow ps3-reboot bench-push bench-pull bench-shots bench-ref bench-matrix \
	classicube-audio game-data gdp-index gdp-manifest licensed-data game-debs game-deploy gamedb-integration

help:
	@echo "Packages (docs/PACKAGING.md):"
	@echo "  make packages     all our packages -> $(BUILD)/debs/<pkg> (kernel first, ps3rsx last)"
	@echo "  make <pkg>-deb    one source package (linux, mesa, ps3-desktop, ...); make ps3rsx-modules; make snd-ps3-ko REL=..."
	@echo "                    the RSX driver for both kernels in $(BUILD)/debs/linux"
	@echo "  make game-debs GAME=<id>  every source package of one game's gamedb bundle (packages/, licensed/)"
	@echo "  make game-data    the licensed game data debs game-data-packager makes (Quake shareware, Quake II"
	@echo "                    demo: gamedb build.toml; the published files when the licensed suite has them)"
	@echo "                    -> $(BUILD)/debs/game-data"
	@echo "  make gdp-index    tools/gamedb/gdp-packages.json: the games and packages game-data-packager knows (check.py)"
	@echo "  make gdp-manifest tools/gamedb/gdp-manifest.json: the files of every [data_setup] package (walnut-games-helper import-data)"
	@echo "  make licensed-data PKG=<pkg> INPUT=<file>  a licensed data deb from your own copy"
	@echo "                    (roller-coaster-tycoon2-data: the RCT2 CD image or a folder, by game-data-packager;"
	@echo "                    tyrian-data: tyrian21.zip) -> $(BUILD)/debs/<pkg>"
	@echo "  make publish      licensed packages built here -> walnut.nanobyte.dev's licensed suite (DEBS=...;"
	@echo "                    free packages are published by CI only)"
	@echo "Image:"
	@echo "  make rootfs [PROFILE=public|dev]  the PS3's root filesystem image -> $(ROOTFS_TAR)"
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
	@echo "  make gpu-deploy | spu-deploy | game-deploy GAME=<gamedb id> (its build.toml packages)"
	@echo "Tests (no PS3):"
	@echo "  make test         every quick host test suite, a summary table (tools/run-tests.sh)"
	@echo "  make test-slow    the same plus the container-based ones (autogm); ONLY=<suite> runs one"
	@echo "Run:"
	@echo "  make netboot-deb  boot a kernel from $(BUILD)/debs/linux from Petitboot (FLAVOUR=release|debug,"
	@echo "                    ROOT=nfs|usb)"
	@echo "  make listen       print PS3 early-debug + netconsole output"
	@echo "  make ps3-reboot   reboot the running PS3 Linux (lands in GameOS/XMB, not Petitboot)"
	@echo "  make bench-push   the game suite runner to the PS3 (/var/tmp/bench/harness/run.sh, docs/games/SUITE.md)"
	@echo "  make bench-pull   its results into $(BUILD)/bench (PERFDATA=1: with perf.data and perf-syms.tar), screenshots as PNGs + PS3 | PC sheets"
	@echo "  make bench-shots  the PS3 | PC screenshot sheets again, and the ones not reviewed yet"
	@echo "  make bench-ref GAME=x [VARIANT=y]  the PC reference screenshots of a recipe (build/bench-ref)"
	@echo "  make bench-matrix the benchmark records as CSV matrices, one per metric ($(BUILD)/bench-matrix, gamedb/benchmarks/README.md)"
	@echo "  make initrd-ssd   the SSD root's initramfs (IMGMAP=<copy of the PS3's map.json>) -> $(BUILD)/initrd"
	@echo "  make initrd-walnut  the internal install's generic initramfs -> $(BUILD)/initrd/initrd-walnut.img"
	@echo "  make bench-assets  the benchmarks' built assets (gamedb/games/*/benchmark/assets-src/assets.mk;"
	@echo "                     make classicube-bench: ClassiCube's plugin)"
	@echo "Other:"
	@echo "  make shell        shell in the builder container"
	@echo "  make clean        move build outputs to $(BUILD)/old/ (never deletes)"

all:
	$(MAKE) --no-print-directory packages
	$(MAKE) --no-print-directory rootfs

# --- Builder container --------------------------------------------------------
builder: $(BUILDER_STAMP)
$(BUILDER_STAMP): build-containers/Dockerfile
	docker build -q -t $(BUILDER_TAG) \
		--build-arg BASE=$(BUILDER_BASE) --build-arg SNAPSHOT=$(BUILDER_SNAPSHOT) --build-arg SNAPSHOT_URL=$(SNAPSHOT_URL) build-containers
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
	$(call need,PS3_IP)
	sudo NFS_ROOT=$(NFS_ROOT) PS3_IP=$(PS3_IP) FRESH=$(FRESH) tools/install-nfs-root.sh $(ROOTFS_TAR)

# The image onto the USB disk from Petitboot (the disk isn't in use there, so this replaces even the
# root the PS3 normally runs from); the disk must have been set up with make usb-root FORMAT=1
usb-root-petitboot: $(ROOTFS_TAR)
	$(call need,PETITBOOT_IP PC_IP)
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) FRESH=$(FRESH) \
		tools/install-usb-root-petitboot.sh $(ROOTFS_TAR)

# The image onto the USB disk plugged into the PS3 (running Linux, over SSH); FORMAT=1 partitions
# the disk first (erases it). Boot it with make netboot-deb ROOT=usb.
usb-root: $(ROOTFS_TAR)
	$(call need,PS3_IP PS3_USER USB_DISK_ID)
	PS3_SSH="$(PS3_SSH)" USB_DISK_ID="$(USB_DISK_ID)" FORMAT=$(FORMAT) FRESH=$(FRESH) \
		tools/install-usb-root.sh $(ROOTFS_TAR)

# --- GPU tools (ps3-gpu-tools and ps3rsx are source packages: make ps3-gpu-tools-deb ps3rsx-modules) ---
gpu-deploy:
	$(MAKE) deploy PKGS="ps3rsx-modules-ps3 ps3-gpu-tools"

# Game asset tools (oggdec) on top of the builder
GAMES_BUILDER_TAG := ps3-games-builder:$(shell (cat build-containers/Dockerfile.games; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
GAMES_BUILDER_STAMP := $(BUILD)/.games-builder-$(subst :,-,$(GAMES_BUILDER_TAG))
$(GAMES_BUILDER_STAMP): build-containers/Dockerfile.games | $(BUILDER_STAMP)
	docker build -q -t $(GAMES_BUILDER_TAG) -f build-containers/Dockerfile.games --build-arg BUILDER=$(BUILDER_TAG) build-containers
	@touch $@

# Each package's orig tarball recipe (<pkg dir>/orig.mk, docs/PACKAGING.md "Upstream tarballs"): the rules that
# fetch or make the upstream tarballs its source is made from, its <pkg>-src prerequisites, and
# ORIG_VARS += the variables naming them (DEB_ENV passes each to the package tools: package.conf
# ORIG=$X_ORIG). $(call fetch,URL,SHA256) is the recipe of a plain download. Two passes: every free
# package's first, then the licensed ones' (gamedb/*/*/licensed/*: they may use the free ones'
# variables, never the reverse; the public copy has none)
ORIG_VARS :=
define fetch
@mkdir -p $(@D)
curl -fsSL -o $@.tmp $(1)
echo "$(2)  $@.tmp" | sha256sum -c --quiet
mv $@.tmp $@
endef
LICENSED_PKG_DIRS := $(filter gamedb/%,$(foreach d,$(PKG_DIRS),$(if $(findstring /licensed/,$(d)),$(d))))
include $(sort $(wildcard $(addsuffix /orig.mk,$(filter-out $(LICENSED_PKG_DIRS),$(PKG_DIRS)))))
include $(sort $(wildcard $(addsuffix /orig.mk,$(LICENSED_PKG_DIRS))))

# One game's packages (gamedb/games/<GAME>/): make game-debs builds every source package of ours in its
# bundle (packages/ and licensed/: tools/gamedb/build-info.py own-packages), make game-deploy installs its
# build.toml packages on the running PS3
game-debs:
	@[ -n "$(GAME)" ] || { echo "make game-debs GAME=<gamedb id>"; exit 2; }
	@set -e; p=$$(python3 -I tools/gamedb/build-info.py own-packages $(GAME)); \
	[ -n "$$p" ] || { echo "$(GAME): no source package of ours in its bundle"; exit 1; }; \
	$(MAKE) --no-print-directory $$(printf '%s-deb ' $$p)
game-deploy:
	@[ -n "$(GAME)" ] || { echo "make game-deploy GAME=<gamedb id>"; exit 2; }
	@set -e; p=$$(python3 -I tools/gamedb/build-info.py packages $(GAME)); \
	[ -n "$$p" ] || { echo "$(GAME): no packages in its build.toml"; exit 1; }; \
	$(MAKE) --no-print-directory deploy PKGS="$$p"

# The games and package names our game-data-packager knows (build/debs/game-data-packager, make
# game-data-packager-deb; installed over Debian's in a throwaway games builder container):
# tools/gamedb/gdp-packages.json (committed; check.py checks build.toml [data_setup] against it offline).
# Regenerate when our gdp changes
gdp-index: | $(GAMES_BUILDER_STAMP)
	python3 -I tools/gamedb/gdp-index.py --image $(GAMES_BUILDER_TAG)
# The files of every [data_setup] package (path, size, checksum), as our gdp builds them:
# tools/gamedb/gdp-manifest.json (committed; walnut-games ships it, walnut-games-helper import-data checks
# a player's data stream against it). Regenerate when that gdp changes or a [data_setup] names a new package
gdp-manifest: | $(GAMES_BUILDER_STAMP)
	python3 -I tools/gamedb/gdp-manifest.py --image $(GAMES_BUILDER_TAG)

# The licensed game data made by game-data-packager (tools/game-data.sh): every gamedb build.toml
# [[licensed]] with builder = "game-data-packager:<game>" (tools/gamedb/build-info.py game-data; today
# Quake's shareware and Quake II's demo data; one made from the owner's copy, input = true, only when
# published) -> build/debs/game-data. Non-free, the licensed suite only. A version already published
# there is downloaded, never made again (its output isn't reproducible); REGEN=1 makes it anyway. A file
# made gets X-Walnut-Edition/X-Walnut-Release (tools/gamedb/stamp-data-deb.py). The suite's login reaches
# the container as a netrc file under build/tmp, deleted afterwards.
# $(call game-data-run,OUT DIR,SPECS,MORE DOCKER ARGS)
game-data-run = mkdir -p $(BUILD)/tmp; n=$(BUILD)/tmp/licensed.netrc; rm -f $$n; \
	apt-repo/licensed-auth.sh $$n netrc || [ "$(REGEN)" = 1 ] || exit 1; \
	auth=; [ ! -e $$n ] || auth="-e LICENSED_NETRC=/work/$$n"; set +e; \
	$(DOCKER_RUN) --user $(UID_GID) -e HOME=/tmp $$auth -e REGEN=$(REGEN) $(3) $(GAMES_BUILDER_TAG) \
		tools/game-data.sh $(1) $(2); rc=$$?; rm -f $$n
game-data: | $(GAMES_BUILDER_STAMP)
	@set -e; pairs=$$(python3 -I tools/gamedb/build-info.py game-data); [ -n "$$pairs" ]; \
	$(call game-data-run,$(BUILD)/debs/game-data,$$pairs,); exit $$rc
# The licensed game data made from the owner's own copy (build.toml [[licensed]]): builder = "script"
# runs gamedb/games/<id>/licensed/<pkg>/<pkg>.sh on this PC (PKG=tyrian-data INPUT=<tyrian21.zip>); a
# game-data-packager one with input = true runs tools/game-data.sh on INPUT in the games builder
# (PKG=roller-coaster-tycoon2-data INPUT=<the RCT2 CD image, a folder or an installer>; an .iso is
# unpacked with 7z into build/tmp/licensed-input first, deleted afterwards) -> build/debs/<pkg>
licensed-data: | $(GAMES_BUILDER_STAMP)
	@[ -n "$(PKG)" ] && [ -n "$(INPUT)" ] || { echo "make licensed-data PKG=<pkg> INPUT=<file>: $$(python3 -I tools/gamedb/build-info.py scripts | paste -sd' ')"; exit 2; }
	@set -e; b=$$(python3 -I tools/gamedb/build-info.py builder $(PKG)); \
	case $$b in \
	script) s=$$(python3 -I tools/gamedb/build-info.py script $(PKG)); $$s "$(INPUT)" ;; \
	game-data-packager:*) \
		spec=$$(python3 -I tools/gamedb/build-info.py game-data | grep -x "[a-z0-9-]*:$(PKG):input") \
			|| { echo "$(PKG): no input = true in its [[licensed]]: make game-data makes it"; exit 2; }; \
		in=$$(realpath -e "$(INPUT)"); x=$(BUILD)/tmp/licensed-input; rm -rf $$x; \
		case $$in in \
		*.iso|*.ISO) mkdir -p $$x; 7z x -y -o$$x "$$in" > /dev/null; mnt="-v $(CURDIR)/$$x:/input:ro"; arg=/input ;; \
		*) if [ -d "$$in" ]; then mnt="-v $$in:/input:ro"; arg=/input; \
		   else mnt="-v $$in:/input/$${in##*/}:ro"; arg=/input/$${in##*/}; fi ;; \
		esac; \
		$(call game-data-run,$(BUILD)/debs/$(PKG),$$spec,$$mnt -e INPUT=$$arg); rm -rf $$x; exit $$rc ;; \
	*) echo "$(PKG): builder $$b, not one make licensed-data runs"; exit 2 ;; \
	esac

# The benchmarks' built assets (gamedb/README.md "benchmark/assets-src/"): each assets.mk has its
# rules and adds its targets to BENCH_ASSETS (ClassiCube's plugin: make classicube-bench)
BENCH_ASSETS :=
include $(sort $(wildcard gamedb/games/*/benchmark/assets-src/assets.mk))
.PHONY: bench-assets
bench-assets: $(BENCH_ASSETS)

# --- SPU toolchain ---------------------------------------------------------------
# spu-elf GCC 9.5 + binutils + newlib for the Cell's SPEs (tools/build-spu-toolchain.sh)
SPU_BUILDER_TAG := ps3-spu-builder:$(shell (cat build-containers/Dockerfile.spu; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
SPU_BUILDER_STAMP := $(BUILD)/.spu-builder-$(subst :,-,$(SPU_BUILDER_TAG))
SPU_TARBALLS := $(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz $(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz \
	$(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz
SPU_GCC := $(BUILD)/spu/install/opt/spu/bin/spu-elf-gcc
$(SPU_BUILDER_STAMP): build-containers/Dockerfile.spu | $(BUILDER_STAMP)
	docker build -q -t $(SPU_BUILDER_TAG) -f build-containers/Dockerfile.spu --build-arg BUILDER=$(BUILDER_TAG) build-containers
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
$(SPU_GCC): $(SPU_TARBALLS) $(SPU_VARS) tools/build-spu-toolchain.sh $(wildcard packages/patched/toolchain/spu/gcc/*.patch packages/patched/toolchain/spu/binutils/*.patch packages/patched/toolchain/spu/newlib/*.patch packages/patched/toolchain/spu/newlib-*/*.patch) | $(SPU_BUILDER_STAMP)
	echo "$(SPU_GCC_SHA256)  $(DOWNLOADS)/gcc-$(SPU_GCC_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_BINUTILS_SHA256)  $(DOWNLOADS)/binutils-$(SPU_BINUTILS_VERSION).tar.xz" | sha256sum -c --quiet
	echo "$(SPU_NEWLIB_SHA256)  $(DOWNLOADS)/newlib-$(SPU_NEWLIB_VERSION).tar.gz" | sha256sum -c --quiet
	$(DOCKER_RUN) --user $(UID_GID) -e SPU_GCC_VERSION=$(SPU_GCC_VERSION) \
		-e SPU_BINUTILS_VERSION=$(SPU_BINUTILS_VERSION) -e SPU_NEWLIB_VERSION=$(SPU_NEWLIB_VERSION) \
		$(SPU_BUILDER_TAG) tools/build-spu-toolchain.sh

# SPU test programs (packages/walnut/tools/ps3-spu/spu), spurun, libspeq and speq-dxt (cross-built for the PPE), outside
# the package build: the ps3-spu package
SPU_TESTS_STAMP := $(BUILD)/spu-tests/.done
spu-tests: $(SPU_TESTS_STAMP)
$(SPU_TESTS_STAMP): $(SPU_GCC) $(wildcard packages/walnut/tools/ps3-spu/Makefile packages/walnut/tools/ps3-spu/*/*.[ch] packages/walnut/tools/ps3-spu/lib/*.map) | $(SPU_BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) $(SPU_BUILDER_TAG) make -C packages/walnut/tools/ps3-spu ppe spu \
		CC=powerpc64-linux-gnu-gcc CFLAGS="-O2 -mcpu=cell" SPU_CC=/work/$(SPU_GCC) OUT=/work/$(BUILD)/spu-tests
	@touch $@
# The SSD root's initramfs (docs/GAMEOS-HDD.md "Phase B", tools/ssdimg/build-initrd.sh):
# IMGMAP=<a copy of the PS3's /var/lib/ps3-imgmap/<name>.map.json, its verified/created records next to
# it> (or BOOTMAP=<boot maps made by ps3-imgmap export-boot>), INITRD_RELEASE (default: the newest
# release kernel in build/debs/linux) -> build/initrd/initrd-ssd-<release>.img (+ .manifest)
INITRD_DL := $(DOWNLOADS)/initrd
BB_PPC64 := $(INITRD_DL)/$(BUSYBOX_PPC64_DEB)
BB_AMD64 := $(INITRD_DL)/$(BUSYBOX_AMD64_DEB)
$(BB_PPC64):
	mkdir -p $(@D)
	curl -fsSL -o $@.tmp $(BUSYBOX_PPC64_URL) && echo "$(BUSYBOX_PPC64_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
$(BB_AMD64):
	mkdir -p $(@D)
	curl -fsSL -o $@.tmp $(BUSYBOX_AMD64_URL) && echo "$(BUSYBOX_AMD64_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
E2FS_TB := $(INITRD_DL)/$(E2FSPROGS_TARBALL)
GTAR_TB := $(INITRD_DL)/$(GNUTAR_TARBALL)
$(E2FS_TB):
	mkdir -p $(@D)
	curl -fsSL -o $@.tmp $(E2FSPROGS_URL) && echo "$(E2FSPROGS_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
$(GTAR_TB):
	mkdir -p $(@D)
	curl -fsSL -o $@.tmp $(GNUTAR_URL) && echo "$(GNUTAR_SHA256)  $@.tmp" | sha256sum -c --quiet && mv $@.tmp $@
initrd-busybox: $(BB_PPC64) $(BB_AMD64) $(E2FS_TB) $(GTAR_TB)
# The internal install's generic initramfs (docs/GAMEOS-HDD.md "The internal install", docs/release/ALPHA.md
# M1): no map, no kernel release, for every kernel with ufs and dm built in -> build/initrd/initrd-walnut.img
initrd-walnut: $(BB_PPC64) $(E2FS_TB) $(GTAR_TB) | $(BUILDER_STAMP)
	$(DOCKER_RUN) --user $(UID_GID) -e ARCH=ppc64 -e BUSYBOX_DEB=$(BB_PPC64) -e E2FSPROGS_TAR=$(E2FS_TB) \
		-e GNUTAR_TAR=$(GTAR_TB) -e INITRD_BUILT=$(shell git describe --always --dirty) \
		$(BUILDER_TAG) tools/ssdimg/build-initrd.sh
initrd-ssd: $(BB_PPC64) | $(BUILDER_STAMP)
	@[ -n "$(IMGMAP)$(BOOTMAP)" ] || { echo "make initrd-ssd IMGMAP=<copy of the PS3's map.json, with its records> (docs/GAMEOS-HDD.md Phase B)"; exit 2; }
	$(DOCKER_RUN) --user $(UID_GID) -e ARCH=ppc64 -e BUSYBOX_DEB=$(BB_PPC64) -e IMGMAP=$(IMGMAP) \
		-e BOOTMAP="$(BOOTMAP)" -e RELEASE=$(INITRD_RELEASE) -e INITRD_BUILT=$(shell git describe --always --dirty) \
		$(BUILDER_TAG) tools/ssdimg/build-initrd.sh

test:
	tools/run-tests.sh $(if $(ONLY),--only $(ONLY))
test-slow:
	tools/run-tests.sh --slow $(if $(ONLY),--only $(ONLY))
spu-deploy:
	$(MAKE) deploy PKGS="ps3-spu ps3-spu-programs"

# --- Packages (Debian source packages, docs/PACKAGING.md; the targets are at the end) -------------
# Debian packaging tools and aptly (build-containers/Dockerfile.deb): builds, publishes, extracts packages
REPO_BUILDER_TAG := ps3-deb-builder:$(shell (cat build-containers/Dockerfile.deb; echo $(BUILDER_TAG) $(DEBIAN_SNAPSHOT)) | sha256sum | cut -c1-12)
REPO_BUILDER_STAMP := $(BUILD)/.repo-builder-$(subst :,-,$(REPO_BUILDER_TAG))
$(REPO_BUILDER_STAMP): build-containers/Dockerfile.deb | $(BUILDER_STAMP)
	docker build -q -t $(REPO_BUILDER_TAG) -f build-containers/Dockerfile.deb --build-arg BUILDER=$(BUILDER_TAG) \
		--build-arg SNAPSHOT=$(DEBIAN_SNAPSHOT) --build-arg SNAPSHOT_URL=$(SNAPSHOT_URL) build-containers
	@touch $@

# Deploy to the running PS3 from this PC's builds (tools/deploy.sh: a flat repository of build/debs,
# reached by the PS3 through an SSH reverse tunnel): make deploy [PKGS="..."]
# [AUTOREMOVE=1] [KERNELS=1] [DRY_RUN=1]. Upgrades everything of ours that is installed (Mesa
# included; kernels and ps3rsx modules only with KERNELS=1 or when named), then installs the named
# top-level packages; simulated first. dpkg replaces files by renaming, so even Mesa under a
# running X is safe on the USB root (on NFS, stop X first).
DEPLOY_TOP := linux-image-ps3 linux-image-ps3-debug ps3rsx-modules-ps3 ps3rsx-modules-ps3-debug ps3-boot ps3-system ps3-desktop ps3-splash ps3-gpu-tools ps3-spu ps3-spu-programs libspeq0 ps3-apps walnut-games ps3-games-integration
PKGS ?= $(DEPLOY_TOP)
AUTOREMOVE ?= 0
STOP_DESKTOP ?= auto
PS3_SCP = scp -q -i $(PS3_SSH_KEY) -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts
deploy: | $(REPO_BUILDER_STAMP)
	$(call need,PS3_IP PS3_USER)
	PS3=$(PS3_USER)@$(PS3_IP) SSH="ssh -i $(PS3_SSH_KEY) -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts" \
		SCP="$(PS3_SCP)" REPO_BUILDER_TAG=$(REPO_BUILDER_TAG) \
		AUTOREMOVE=$(AUTOREMOVE) STOP_DESKTOP=$(STOP_DESKTOP) DRY_RUN=$(DRY_RUN) KERNELS=$(KERNELS) tools/deploy.sh $(PKGS)

# --- Image --------------------------------------------------------------------
# The package layer plus our config (system/image/) and our packages (build/debs/*)
IMAGE_INPUTS := $(ROOTFS_BASE) $(wildcard system/image/*) tools/build-image.sh tools/image-packages.sh \
	tools/licensed.py tools/gamedb/build-info.py $(wildcard gamedb/*/*/build.toml) $(wildcard $(SSH_PUBKEY)) \
	$(wildcard $(BUILD)/debs/*/*.deb)
PROFILE ?= dev
rootfs: $(ROOTFS_TAR)
IMAGE_VARS := $(BUILD)/image.vars
BOOT_ROOT_ARGS := $(ROOT_ARGS_usb) rootwait
# WALNUT_SUITE=stable|testing: our packages from that suite on walnut.nanobyte.dev, checked against
# its key (the release image, .github/workflows/release-image.yml); else build/debs
WALNUT_SUITE ?=
WALNUT_APT ?= https://walnut.nanobyte.dev/apt
# the release this image becomes (github-ci/release-id.sh: VERSION's A.B and the next C, or a nightly's
# id): /etc/walnut-image's BUILD_ID; empty for images that aren't releases
WALNUT_RELEASE ?=
WALNUT_KEYRING = apt-repo/keyrings/$(if $(filter testing,$(WALNUT_SUITE)),testing,archive)-keyring.gpg
IMAGE_VARS_NOW := $(BOOT_ROOT_ARGS) $(DEBUG_ARGS) $(PROFILE) $(WALNUT_SUITE) $(WALNUT_APT) $(WALNUT_RELEASE)
$(shell [ "$$(cat $(IMAGE_VARS) 2>/dev/null)" = "$(IMAGE_VARS_NOW)" ] || echo "$(IMAGE_VARS_NOW)" > $(IMAGE_VARS))
$(ROOTFS_TAR): $(IMAGE_INPUTS) $(IMAGE_VARS) | $(REPO_BUILDER_STAMP)
	@mkdir -p $(BUILD)/rootfs
	[ -f "$(SSH_PUBKEY)" ] && cp $(SSH_PUBKEY) $(BUILD)/rootfs/authorized_keys || touch $(BUILD)/rootfs/authorized_keys
	$(DOCKER_RUN) --privileged $(ROOTFS_ENV) -e BOOT_ROOT_ARGS="$(BOOT_ROOT_ARGS)" \
		-e BOOT_EXTRA_ARGS="$(DEBUG_ARGS)" -e PROFILE=$(PROFILE) \
		-e WALNUT_SUITE=$(WALNUT_SUITE) \
		-e WALNUT_APT=$(WALNUT_APT) -e WALNUT_KEYRING=$(WALNUT_KEYRING) -e WALNUT_RELEASE=$(WALNUT_RELEASE) \
		$(REPO_BUILDER_TAG) tools/build-image.sh
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
	$(call need,PETITBOOT_IP PC_IP)
	@set -e; $(KERNEL_DEB_SH); d=$$(kernel_deb $(FLAVOUR)); \
	[ -n "$$d" ] || { echo "no linux-image for $(FLAVOUR): make linux-deb"; exit 1; }; \
	rm -rf $(BUILD)/tmp/netboot && mkdir -p $(BUILD)/tmp/netboot; \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) $(REPO_BUILDER_TAG) dpkg-deb -x $$d $(BUILD)/tmp/netboot; \
	k=$$(ls $(BUILD)/tmp/netboot/boot/vmlinux-*); echo ">> netbooting $$k"; \
	PS3_IP=$(PETITBOOT_IP) PC_IP=$(PC_IP) HTTP_PORT=$(HTTP_PORT) KERNEL=$$k \
		KERNEL_CMDLINE="$(ROOT_ARGS_$(ROOT)) rootwait $(DEBUG_ARGS) $$(cat $(BUILD)/tmp/netboot/usr/lib/ps3/boot/*.args)" tools/netboot.sh

listen:
	tools/listen-debug.sh

PS3_SSH := ssh -i $(PS3_SSH_KEY) -o UserKnownHostsFile=$(CURDIR)/$(BUILD)/ps3_known_hosts $(PS3_USER)@$(PS3_IP)
ps3-reboot:
	$(call need,PS3_IP PS3_USER)
	-$(PS3_SSH) sudo systemctl reboot

# The game suite runner (bench, docs/games/SUITE.md) to /var/tmp/bench on the PS3: unpacked
# beside the old copy and swapped in with a rename, so a run in progress keeps reading its files.
# bench-pull: the PS3's run directories (/var/tmp/ps3-bench) into $(BUILD)/bench; the raw profiles
# (perf.data ~20 MB a run, perf-load.data, perf-syms.tar) only with PERFDATA=1 (docs/PROFILING.md)
# The tree: bench/ as it is (harness/, lib/, analysis/, tests/; __pycache__ left out) and every game's benchmark
# (gamedb/games/<id>/benchmark/: recipe.sh, analysis.toml, lib/, assets/; not assets-src/, legacy/, results/, runs/)
# as games/<id>/ (bench/harness/games.sh), its played test (gamedb/games/<id>/rating/test.toml) as
# games/<id>/rating/test.toml (playdef.py, play-test.sh stop).
BENCH_GAME_FILES = $(wildcard $(foreach f,recipe.sh analysis.toml lib assets,gamedb/games/*/benchmark/$(f)) \
	gamedb/games/*/rating/test.toml)
# Each game's data packages with their edition and release, games/<id>/data.txt (tools/gamedb/build-info.py
# bench-data, made under $(BUILD)/tmp/bench-data): the harness records the installed ones in versions.
BENCH_DATA = $(BUILD)/tmp/bench-data
BENCH_TAR = tar -c --exclude=__pycache__ --transform='s,^gamedb/games/\([^/]*\)/benchmark,bench/games/\1,' \
	--transform='s,^gamedb/games/\([^/]*\)/rating/,bench/games/\1/rating/,' bench $(sort $(BENCH_GAME_FILES)) \
	-C $(BENCH_DATA) bench
bench-push:
	$(call need,PS3_IP PS3_USER)
	rm -rf $(BENCH_DATA); python3 -I tools/gamedb/build-info.py bench-data $(BENCH_DATA)/bench/games
	$(BENCH_TAR) | $(PS3_SSH) 'set -e; rm -rf /var/tmp/bench.new /var/tmp/bench.old; \
		mkdir /var/tmp/bench.new; tar -C /var/tmp/bench.new --strip-components=1 -x; \
		[ ! -e /var/tmp/bench ] || mv /var/tmp/bench /var/tmp/bench.old; mv /var/tmp/bench.new /var/tmp/bench'
# The screenshots (docs/games/SUITE.md "Screenshots"): bench-pull leaves out the .xwd captures it already
# turned into PNGs, then shots.py post converts the new ones (the .xwd removed) and makes each run's
# PS3 | PC sheet (<run>/shots-vs-pc.png) against build/bench-ref; bench-shots does that step alone and
# lists the sheets nobody has reviewed (no REVIEW.md). bench-ref GAME=x [VARIANT=y] [SHOTS="30 90"]:
# the PC reference of a recipe (tools/bench-ref.sh: an amd64 container, Xvfb, llvmpipe at GL 2.1).
bench-pull:
	$(call need,PS3_IP PS3_USER)
	mkdir -p $(BUILD)/bench
	cd $(BUILD)/bench && find . -path '*/shots/*.png' | sed 's/\.png$$/.xwd/' > .pulled-shots
	$(PS3_SSH) 'tar -C /var/tmp/ps3-bench -X - -c $(if $(PERFDATA),,--exclude=perf.data --exclude=perf-load.data --exclude=perf-sys.data --exclude=perf-syms.tar) .' \
		< $(BUILD)/bench/.pulled-shots | tar -C $(BUILD)/bench -x
	-python3 bench/analysis/shots.py post $(BUILD)/bench
	@echo ">> $(BUILD)/bench; summaries: bench/analysis/report.py $(BUILD)/bench/<run dirs>"
bench-shots:
	python3 bench/analysis/shots.py post $(BUILD)/bench
	@echo ">> sheets not reviewed yet (no REVIEW.md):"; python3 bench/analysis/shots.py pending $(BUILD)/bench
bench-ref:
	tools/bench-ref.sh $(GAME) $(or $(VARIANT),base)
# The CSV matrices (one per metric, one row per batch, a column per game-variant) made from gamedb's
# benchmark records (gamedb/benchmarks/README.md; bench/analysis/matrix.py writes the records, --csv the files)
bench-matrix:
	python3 bench/analysis/matrix.py --csv $(BUILD)/bench-matrix

shell: $(BUILDER_STAMP)
	$(DOCKER_RUN) -it --user $(UID_GID) $(BUILDER_TAG) bash

clean:
	@ts=$$(date +%Y%m%d-%H%M%S); mkdir -p $(BUILD)/old/$$ts; \
	for d in rootfs debs; do if [ -e $(BUILD)/$$d ]; then mv $(BUILD)/$$d $(BUILD)/old/$$ts/; fi; done; \
	echo "moved outputs to $(BUILD)/old/$$ts"

# walnut.nanobyte.dev's server side (servers/walnut.nanobyte.dev/: the walnut-repo and walnut-upload containers,
# the walnut-watch publisher, the main proxy's site config, the licensed suite's login from
# config.local.mk); idempotent
walnut-server:
	servers/walnut.nanobyte.dev/deploy.sh

# publish website/site/ from here (GitHub: the "Walnut site" workflow does the same)
walnut-site:
	servers/walnut.nanobyte.dev/publish-site.sh

walnut-server-test:
	python3 servers/walnut.nanobyte.dev/test_walnut_watch.py

# The public GitHub copy (docs/release/ALPHA.md M8): re-export (trailers removed, the strip and
# replace lists applied, the history scan) and push; deterministic, so the public main fast-forwards
# The export clone has the Git LFS pointers only (Walnut Games' screenshots): the objects are copied
# from this repository's LFS store and pushed with the branch (filter-repo leaves pointer files alone).
public-push:
	tools/public-export.sh
	git -C $${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public} remote add origin git@github.com:nanobyte-dev/walnut-linux.git
	git lfs fetch --all origin >/dev/null 2>&1 || true
	[ ! -d .git/lfs/objects ] || { mkdir -p $${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public}/.git/lfs && \
		rsync -a .git/lfs/objects $${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public}/.git/lfs/ && \
		git -C $${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public} lfs push --all origin; }
	git -C $${WALNUT_PUBLIC_DIR:-/mnt/data_linux/Temp/ps3/walnut-public} push origin main

# --- Repository and workspace ------------------------------------------------------------
# Publish licensed packages built here to walnut.nanobyte.dev's password-protected licensed suite
# (apt-repo/licensed-publish.sh; docs/PACKAGING.md "The licensed suite"). DEBS: the .deb/.dsc files
# (default: every licensed build in build/debs with its source). Free packages are refused: CI
# publishes them (testing on every push, stable by hand). DRY_RUN=1 makes the bundle only.
DEBS ?=
publish:
	DRY_RUN=$(DRY_RUN) apt-repo/licensed-publish.sh $(DEBS)

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
DEB_ENV := -e OWNER=$(UID_GID) -e DEBIAN_SUITE=$(DEBIAN_SUITE) -e DEBIAN_MIRROR=$(DEBIAN_MIRROR) -e HOME=/tmp -e CCACHE_REMOTE_STORAGE \
	$(foreach v,$(ORIG_VARS),-e $(v)=$($(v))) -e KERNEL_RELEASE=$(KERNEL_RELEASE) -e KERNEL_FLAVOUR=$(KERNEL_FLAVOUR)
# (the upstream tarballs a package's source is made from: <pkg>-src prerequisites in its orig.mk)
# KERNEL_RELEASE and KERNEL_FLAVOUR are set only by ps3rsx-modules (below), per kernel.
# ps3rsx for both kernels built by linux-deb (build/debs/linux), release and debug: modules
# ps3rsx-modules-<release>, metas ps3rsx-modules-ps3 and ps3rsx-modules-ps3-debug (named like the
# kernel's: KERNEL_FLAVOUR is the Debian flavour), in build/debs/ps3rsx-<Debian flavour>
ps3rsx-modules:
	@set -e; $(KERNEL_DEB_SH); count=0; for f in release:ps3 debug:ps3-debug; do \
		d=$$(kernel_deb $${f%%:*}); \
		[ -n "$$d" ] || continue; \
		count=$$((count + 1)); \
		rel=$$(basename $$d | sed 's/^linux-image-//; s/_.*//'); echo ">> ps3rsx for $$rel"; \
		$(MAKE) --no-print-directory ps3rsx-src ps3rsx-deb KERNEL_RELEASE=$$rel KERNEL_FLAVOUR=$${f#*:}; \
	done; \
	[ $$count -gt 0 ] || { echo "no linux-image in $(BUILD)/debs/linux: make linux-deb" >&2; exit 1; }
# snd_ps3.ko out of tree with packages/patched/kernel/linux/patches' sound/ppc patches, for the kernel REL (default: the newest
# release kernel in build/debs/linux), to try them without a new kernel: build/debs/snd-ps3/<REL>
snd-ps3-ko: | $(REPO_BUILDER_STAMP)
	@set -e; rel='$(REL)'; if [ -z "$$rel" ]; then $(KERNEL_DEB_SH); d=$$(kernel_deb release); \
		rel=$$(basename "$$d" | sed 's/^linux-image-//; s/_.*//'); fi; \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) -e JOBS=$(JOBS) $(REPO_BUILDER_TAG) \
		tools/snd-ps3-ko.sh "$$rel"
chroot: | $(REPO_BUILDER_STAMP)
	@tools/disk-check.sh
	docker run --rm --privileged -v $(CURDIR):/work -w /work $(DEB_ENV) $(REPO_BUILDER_TAG) tools/deb-chroot.sh
$(BUILD)/chroot/sid-ppc64.tar:
	$(MAKE) --no-print-directory chroot
# make <pkg>-src: fetch Debian's source and make ours; make <pkg>-deb: build it natively (qemu)
%-src: | $(REPO_BUILDER_STAMP)
	@[ -n '$(call pkgdir,$*)' ] || { echo 'no package $* (packages/**/$*/package.conf)'; exit 1; }
	@if grep -q '^TYPE=debsrc' $(call pkgdir,$*)/package.conf; then \
		docker run --rm -v $(CURDIR):/work -w /work $(DEB_ENV) $(REPO_BUILDER_TAG) tools/deb-fetch.sh $*; fi
	@set -e; v=$$(tools/deb-version.sh $*); \
	docker run --rm -v $(CURDIR):/work -w /work --user $(UID_GID) $(DEB_ENV) $(REPO_BUILDER_TAG) \
		tools/deb-prepare.sh $* "$$(echo "$$v" | sed -n 1p)" "$$(echo "$$v" | sed -n 2p)" "$$(echo "$$v" | sed -n 3p)"
# ps3-boot ships the generic initramfs (package.conf COPY_IN): built first
ps3-boot-src: initrd-walnut
# walnut-games ships the games catalog (package.conf COPY_IN): packed first, on the host (python3 only;
# the screenshots are Git LFS files: a checkout without them stops the pack)
gamedb-catalog:
	python3 -I tools/gamedb/pack-catalog.py
# and the site's pages for its Details view (tools/gamedb/build.sh --app: Hugo runs here, on the host or
# the CI runner, not in the PS3 build chroot); the catalog's pictures go in as links to it
gamedb-app-pages: gamedb-catalog
	python3 -I tools/gamedb/pack-app-pages.py
walnut-games-src: gamedb-catalog gamedb-app-pages
# ps3-games ships the games' integration (ps3-games-integration: launchers, config files, divert and
# migrate-home lists; package.conf COPY_IN): packed from gamedb first
gamedb-integration:
	python3 -I tools/gamedb/pack-integration.py
ps3-games-src: gamedb-integration
%-deb: %-src | $(BUILD)/chroot/sid-ppc64.tar
	@tools/disk-check.sh
	@if grep -q '^BUILD=xcc' $(call pkgdir,$*)/package.conf; then \
		$(MAKE) --no-print-directory xcc-toolchain && \
		used=$$(df --output=pcent $(XCC_ROOT) | tail -1 | tr -dc 0-9) && \
		{ [ "$$used" -le 90 ] || { echo "!! $(XCC_ROOT) is $$used% full (less than 10% free): stopping."; exit 1; }; } && \
		echo "docker run ... tools/xcc-build.sh $* (in $(XCC_ROOT))" && \
		docker run --rm --privileged -v $(CURDIR):/work -v $(XCC_ROOT):/xcc -w /work $(DEB_ENV) -e JOBS=$(XCC_JOBS) \
			-e XCC_TREE=/xcc/$(notdir $(XCC_TREE)) $(REPO_BUILDER_TAG) tools/xcc-build.sh $*; \
	else \
		echo "docker run ... tools/deb-build.sh $*"; \
		tools/ccache-remote.sh docker run --rm --privileged -v $(CURDIR):/work -w /work $(DEB_ENV) -e JOBS=$(DEB_JOBS) -e KEEP_TREE=$(KEEP_TREE) \
			$(REPO_BUILDER_TAG) tools/deb-build.sh $*; \
	fi
# BUILD=xcc packages (tools/xcc-build.sh: the ppc64 chroot with the x86-64 cross compiler run natively
# inside; WebKitGTK): their chroot, deps cache and ccache live in XCC_ROOT (config.local.mk may move it
# to a bigger disk), the exported toolchain (build-containers/Dockerfile.xcc) in XCC_ROOT/x86-<tag>
XCC_ROOT ?= $(CURDIR)/$(BUILD)/xcc
# GCC needs 2-3 GB per job on WebKit's unified sources: 14 jobs leave room on a 62 GB PC
XCC_JOBS ?= 14
XCC_TAG := ps3-xcc:$(shell (cat build-containers/Dockerfile.xcc; echo $(BUILDER_TAG)) | sha256sum | cut -c1-12)
XCC_TREE := $(XCC_ROOT)/x86-$(lastword $(subst :, ,$(XCC_TAG)))
xcc-toolchain: $(XCC_TREE)/.done
$(XCC_TREE)/.done: build-containers/Dockerfile.xcc | $(BUILDER_STAMP)
	docker build -q -t $(XCC_TAG) -f build-containers/Dockerfile.xcc --build-arg BUILDER=$(BUILDER_TAG) build-containers
	rm -rf $(XCC_TREE).tmp && mkdir -p $(XCC_TREE).tmp
	cid=$$(docker create $(XCC_TAG)) && docker export $$cid | tar -x -C $(XCC_TREE).tmp --anchored --exclude=dev --exclude=proc --exclude=sys; \
		rc=$$?; docker rm $$cid >/dev/null; [ $$rc = 0 ]
	rm -rf $(XCC_TREE) && mv $(XCC_TREE).tmp $(XCC_TREE) && touch $@
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
# All our packages in build order (tools/pkg-dir.sh --order: the kernel first, then each package after the
# ones its LOCAL_DEPS names); ps3rsx is built by ps3rsx-modules, for every kernel, last. The public copy
# has no licensed package, so none is here
SRC_PACKAGES := $(filter-out ps3rsx,$(shell tools/pkg-dir.sh --order))
$(if $(filter 0,$(.SHELLSTATUS)),,$(error tools/pkg-dir.sh --order failed (above)))
packages: $(SRC_PACKAGES:%=%-deb)
	$(MAKE) --no-print-directory ps3rsx-modules
