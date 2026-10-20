# Build settings. Everything that decides what ends up on the PS3 lives here or in the files it points to.

# The kernel and Mesa are Debian source packages: their versions, hashes, configs and boot
# arguments are in linux/package.conf and mesa/package.conf (docs/PACKAGING.md).

# --- Debian root filesystem ---------------------------------------------------
# Packages come from a frozen snapshot of debian-ports, so a rebuild gets the same versions.
DEBIAN_SNAPSHOT     := 20261013T000000Z
DEBIAN_SUITE        := unstable
DEBIAN_MIRROR       := http://snapshot.debian.org/archive/debian-ports/$(DEBIAN_SNAPSHOT)
ROOTFS_PACKAGES     := image/packages.txt
ROOTFS_CUSTOMIZE    := image/customize.sh

# --- DarkPlaces for Nexuiz (darkplaces-gl2/) -----------------------------------
# Debian's buster orig tarball (snapshot.debian.org), the last DarkPlaces with the GL 1.x/2.0 renderpaths
DARKPLACES_VERSION  := 0~20180412~beta1
DARKPLACES_SHA256   := 02cb41e64a467c2570086d3e746e64917c1abe06270e05875640b6ab0567b5ea
DARKPLACES_URL      := https://snapshot.debian.org/archive/debian/20180612T152409Z/pool/main/d/darkplaces/darkplaces_0~20180412~beta1.orig.tar.xz

# --- zlib-ng in zlib-compat mode (zlib-ng/) ------------------------------------
# A libz.so.1 in a directory of its own that a game opts into (LD_LIBRARY_PATH); not in Debian
ZLIBNG_VERSION      := 2.3.3
ZLIBNG_SHA256       := f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1
ZLIBNG_URL          := https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$(ZLIBNG_VERSION).tar.gz

# --- Warzone 2100 (warzone2100/): release 4.3.5, the last with an OpenGL 2.1 / GLES 2 renderer ---
WARZONE_VERSION     := 4.3.5
WARZONE_SHA256      := 01d608f6f9638e14d7c857df40ad339c1bfc207a05daafe7157ad8652a3405c3
WARZONE_URL         := https://github.com/Warzone2100/warzone2100/releases/download/$(WARZONE_VERSION)/warzone2100_src.tar.xz

# --- OpenRCT2 (openrct2/): release 0.5.6 with our big-endian patches, and the asset zips its CMake would fetch ---
# (upstream's assets.json at v0.5.6: objects, title sequences, OpenSoundEffects, OpenMusic; same sha256)
OPENRCT2_VERSION    := 0.5.6
OPENRCT2_SHA256     := f4732724c1a709cfaff02ddff7fe3b6d027ad7827b343bf6f2a254114d32128b
OPENRCT2_URL        := https://github.com/OpenRCT2/OpenRCT2/archive/refs/tags/v$(OPENRCT2_VERSION).tar.gz
OPENRCT2_OBJECTS_VERSION  := 1.8.0
OPENRCT2_OBJECTS_SHA256   := 949bee5cac9e941be63bb923b792738a507c155a6fea73edab0b23bede21d302
OPENRCT2_OBJECTS_URL      := https://github.com/OpenRCT2/objects/releases/download/v$(OPENRCT2_OBJECTS_VERSION)/objects.zip
OPENRCT2_TITLESEQ_VERSION := 0.5.6
OPENRCT2_TITLESEQ_SHA256  := 37ac28985baf183bae27af72710613595f683dc24892c05989fb6bfd016bdb5d
OPENRCT2_TITLESEQ_URL     := https://github.com/OpenRCT2/title-sequences/releases/download/v$(OPENRCT2_TITLESEQ_VERSION)/title-sequences.zip
OPENRCT2_OPENSFX_VERSION  := 1.0.6
OPENRCT2_OPENSFX_SHA256   := 06b90f3e19c216752df441d551b26a9e3e1ba7755bdd2102504b73bf993608be
OPENRCT2_OPENSFX_URL      := https://github.com/OpenRCT2/OpenSoundEffects/releases/download/v$(OPENRCT2_OPENSFX_VERSION)/opensound.zip
OPENRCT2_OPENMUSIC_VERSION := 1.6.1
OPENRCT2_OPENMUSIC_SHA256 := 994b350d3b180ee1cb9619fe27f7ebae3a1a5232840c4bd47a89f33fa89de1a1
OPENRCT2_OPENMUSIC_URL    := https://github.com/OpenRCT2/OpenMusic/releases/download/v$(OPENRCT2_OPENMUSIC_VERSION)/openmusic.zip

# --- ClassiCube (classicube/) ------------------------------------------------
# A pinned commit (SDL3 backend), plus the texture pack it needs (plain HTTP, so checksummed)
CLASSICUBE_COMMIT   := aa47929825a4647354588818fb0b002b2f808279
CLASSICUBE_SHA256   := 911e2b179728cbbb3c5f6d90f324f38ebbe6f68c57cb96dd66741fb15e5ab488
CLASSICUBE_URL      := https://github.com/ClassiCube/ClassiCube/archive/$(CLASSICUBE_COMMIT).tar.gz
CLASSICUBE_TEXPACK_SHA256 := 55eac12a9727d209b42694d07dfdca8d50349779ab77ffc11a31d2cb66b45635
CLASSICUBE_TEXPACK_URL := http://static.classicube.net/default.zip

# --- Sauerbraten's models and sounds missing from cube2-data (sauerbraten-nonfree-data/) --------
# The 2020_12_29 Linux release (SourceForge publishes the MD5 only; the SHA256 is of our download)
SAUER_RELEASE_FILE   := sauerbraten_2020_12_29_linux.tar.bz2
SAUER_RELEASE_MD5    := b50d018f7dd6259f1fc12b56028a51b5
SAUER_RELEASE_SHA256 := cdba7c4a47cefd30d0afdd6a912199a1384319cf1619923cb7189e72e468be70
SAUER_RELEASE_URL    := https://sourceforge.net/projects/sauerbraten/files/sauerbraten/2020_11_29/$(SAUER_RELEASE_FILE)/download

# --- SPU toolchain (spu-elf, for the Cell's SPEs) ---------------------------------
# The same GCC and binutils versions as the PPE (docs/PLAN.md): GCC 16.2 with our port of the SPU
# backend (removed upstream in GCC 10; spu-toolchain/gcc/*.patch, docs/SPU-GCC.md), binutils 2.47 (SPU still
# upstream), newlib 4.6 with spu-toolchain/newlib-4.6/*.patch. GNU tarballs' signatures checked against
# gnu-keyring.gpg, newlib against sourceware's sha512.sum (2026-10-15).
SPU_GCC_VERSION     := 16.2.0
SPU_GCC_SHA256      := e6738e29597f733270731aa90600f37ffdc045079dfc27ec7e8192cc81085c3e
SPU_GCC_URL         := https://ftp.gnu.org/gnu/gcc/gcc-$(SPU_GCC_VERSION)/gcc-$(SPU_GCC_VERSION).tar.xz
# binutils 2.47 needs spu-toolchain/binutils/*.patch: since 2.36 gas reads GCC's "nop 127" as the generic
# ".nop 127" directive (32 nops), which pushed branches out of hint range ("Relocation doesn't
# fit" on hbrr). 2.33.1 (sha256 ab66fc2d1c3ec0359b8e08843c9f33b63e8707efdff5e4cc5c200eae24722cbf)
# needs no patch: to go back, set it and empty spu-toolchain/binutils/.
SPU_BINUTILS_VERSION := 2.47
SPU_BINUTILS_SHA256 := 154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff
SPU_BINUTILS_URL    := https://ftp.gnu.org/gnu/binutils/binutils-$(SPU_BINUTILS_VERSION).tar.xz
# newlib's SPU support is unmaintained since ~2012; 4.6 builds with spu-toolchain/newlib-4.6/*.patch (sys/sched.h,
# math.h's isinf/isnan, fenv signatures, include paths, SPU stdio on the 4.x reent, ...). 1.20.0
# (needs -std=gnu17 -fpermissive, no patches) still builds: set the version back, with
# sha256 c644b2847244278c57bec2ddda69d8fab5a7c767f3b9af69aa7aa3da823ff692.
SPU_NEWLIB_VERSION  := 4.6.0.20260123
SPU_NEWLIB_SHA256   := 6ff27e3bf022666f43f7802255be680eeff722ac181b1725d21e2e8318604ee3
SPU_NEWLIB_URL      := https://sourceware.org/pub/newlib/newlib-$(SPU_NEWLIB_VERSION).tar.gz

# --- The SSD root's initramfs (tools/ssdimg/build-initrd.sh, docs/GAMEOS-HDD.md "Phase B") -----------
# Debian's busybox-static, pinned to its own snapshot and sha256: ppc64 for the PS3 (debian-ports),
# amd64 for the KVM test (tools/ssdimg/vm-e2e.sh); the same version and configuration
BUSYBOX_SNAPSHOT    := 20261013T000000Z
BUSYBOX_PPC64_DEB   := busybox-static_1.38.0-3_ppc64.deb
BUSYBOX_PPC64_URL   := http://snapshot.debian.org/archive/debian-ports/$(BUSYBOX_SNAPSHOT)/pool-ppc64/main/b/busybox/$(BUSYBOX_PPC64_DEB)
BUSYBOX_PPC64_SHA256 := cd762eee99372ac2bfa0d0c32cc981a76d35cf3debb6b7c66a3b53b5f7d8cc30
BUSYBOX_AMD64_DEB   := busybox-static_1.38.0-3+b1_amd64.deb
BUSYBOX_AMD64_URL   := http://snapshot.debian.org/archive/debian/$(BUSYBOX_SNAPSHOT)/pool/main/b/busybox/$(BUSYBOX_AMD64_DEB)
BUSYBOX_AMD64_SHA256 := 26801f17e6c88e813be104effc0ea3b43d912bd59ca6295fcd1260528ebb4d41

# --- Build container ----------------------------------------------------------
# Pinned base image; its apt sources are pointed at the same snapshot date.
# Debian sid (amd64), upgraded to the same snapshot as the PS3's root, so the cross compiler is the
# snapshot's GCC 16.2 (gcc-16-powerpc64-linux-gnu 16.2.0-3cross1), as on the PS3 (docs/PLAN.md)
BUILDER_BASE        := debian@sha256:c53e050391cca6c8517c4a075230b798a0f980faa5d3ed0a912249ea882e40b5
BUILDER_SNAPSHOT    := $(DEBIAN_SNAPSHOT)

# Fixed timestamp for rootfs builds (2026-10-13 00:00:00 UTC, matches the snapshot).
SOURCE_DATE_EPOCH   := 1791849600

# --- This installation's LAN: config.local.mk (not tracked) ---------------------
# The values that belong to one developer's machines (addresses, the user and key on the PS3, the
# repository server, the USB disk's serial) live in config.local.mk next to this file, which git
# ignores; config.local.mk.example lists them. Without it the defaults below are empty, and the
# targets that need one stop and say which (the image build, the packages and the tests don't).
-include $(dir $(lastword $(MAKEFILE_LIST)))config.local.mk

# --- Network / netboot --------------------------------------------------------
# The PS3 running Linux, and the user the PC's tools log in as there (make deploy, bench, usb-root)
PS3_IP              ?=
PS3_USER            ?=
# The SSH key for that user (private key; the image authorizes SSH_PUBKEY in the dev profile)
PS3_SSH_KEY         ?= $(HOME)/.ssh/id_ps3
SSH_PUBKEY          ?= $(PS3_SSH_KEY).pub
# Petitboot's address (it may differ from Linux's: Petitboot asks DHCP itself)
PETITBOOT_IP        ?= $(PS3_IP)
# This PC: netboot's HTTP server, the NFS root's server, netconsole's target (its MAC too)
PC_IP               ?=
PC_MAC              ?=
NFS_ROOT            ?= /srv/ps3root

# --- apt repository (tools/repo-server/, published with aptly from build/aptly) -----------------
# REPO_HOST: user@host that make publish rsyncs to; REPO_DIR: the repository's directory there;
# REPO_URL: what the PS3 and the image build fetch from
REPO_HOST           ?=
REPO_DIR            ?=
REPO_URL            ?=
# The image's packages: empty = this checkout's build outputs; img-<stamp> = that frozen snapshot of
# the stable suite (make repo-snapshot), for an image anyone can rebuild from the server
REPO_SUITE          ?=
HTTP_PORT           := 8000
# Boot arguments (linux/flavours/*.args and the ones below). ps3rsx_cma=64M: the GPU's GART comes
# on demand from a 64 MiB CMA area, which holds ordinary movable pages while the GPU doesn't use it (ps3rsx/src/gart.c); ps3rsx_gart=0 turns off the
# fixed reservation that was used before (ps3rsx_gart=<size> still works as a fallback).
# netconsole spells out source IP, device and target MAC: built-in netconsole starts before
# ip=dhcp has configured eth0 and gives up without them.
# loglevel=4: two boots hung in early userspace while systemd flooded the kernel log through
# netconsole; raise it with `dmesg -n 7` once the system is up.
# rsize/wsize: the kernel's nfsroot default is 4 KB per request, which capped reads at 8 MB/s and
# kept the PPE at ~95% system time during package installs (2026-10-14).
# ROOT=nfs (default): the root filesystem is NFS from this PC. ROOT=usb: the USB disk
# (make usb-root), mounted read-only first for the boot-time fsck.
ROOT                ?= nfs
ROOT_ARGS_nfs       := root=/dev/nfs nfsroot=$(PC_IP):$(NFS_ROOT),v3,tcp,rsize=65536,wsize=65536 rw
# The USB root's partition (PARTUUID=<disk id>-02). Installs with FORMAT=1 give each disk a random
# id (tools/usb-root-remote.sh) and write the boot menu from it; this is the one netboot-deb
# ROOT=usb and the image's default boot menu use. 50533301-02: the fixed id of the first installs.
USB_ROOT_PARTUUID   ?= 50533301-02
ROOT_ARGS_usb       := root=PARTUUID=$(USB_ROOT_PARTUUID) rootfstype=ext4 ro
# The USB disk for the root filesystem (its /dev/disk/by-id name on the PS3, config.local.mk)
USB_DISK_ID         ?=
# The kernel's own arguments are shipped in its package (/usr/lib/ps3/boot/<release>.args, from
# linux/flavours/<flavour>.args), so each kernel in the boot menu gets the ones it understands;
# make netboot-deb uses them too.
# This installation's development extras: DHCP for netconsole, netconsole to this PC
# (netconsole only with all three addresses in config.local.mk). The public image has none of
# these (PROFILE=public, tools/build-image.sh).
NETCONSOLE_ARG      := netconsole=6665@$(PS3_IP)/eth0,6666@$(PC_IP)/$(PC_MAC)
DEBUG_ARGS          := ip=dhcp $(if $(and $(PS3_IP),$(PC_IP),$(PC_MAC)),$(NETCONSOLE_ARG))
