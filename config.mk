# Build settings. Everything that decides what ends up on the PS3 lives here or in the files it points to.

# --- Kernel -------------------------------------------------------------------
KERNEL_VERSION      := 6.12.112
KERNEL_SHA256       := 164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03
KERNEL_URL          := https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$(KERNEL_VERSION).tar.xz
# The release is <version><KERNEL_LOCALVERSION>-<stamp>, e.g. 6.12.112-ps3-debug-202610151608:
# a new name whenever the kernel changes, so kernels install side by side and Petitboot lists
# them all (Makefile: KERNEL_STAMP)
KERNEL_LOCALVERSION := -ps3-debug
KERNEL_DEFCONFIG    := ps3_defconfig
KERNEL_FRAGMENTS    := linux/config/ps3.config
KERNEL_SERIES       := linux/patches/series

# --- Debian root filesystem ---------------------------------------------------
# Packages come from a frozen snapshot of debian-ports, so a rebuild gets the same versions.
DEBIAN_SNAPSHOT     := 20261013T000000Z
DEBIAN_SUITE        := unstable
DEBIAN_MIRROR       := http://snapshot.debian.org/archive/debian-ports/$(DEBIAN_SNAPSHOT)
ROOTFS_PACKAGES     := image/packages.txt
ROOTFS_CUSTOMIZE    := image/customize.sh
SSH_PUBKEY          := $(HOME)/.ssh/id_ps3.pub

# --- Mesa (userspace GL for the RSX) ------------------------------------------
# Same Mesa version as the snapshot's Debian packages; built with our nv30 patch.
MESA_VERSION        := 26.2.4
MESA_SYSROOT_PACKAGES := sysroot/mesa-sysroot-packages.txt

# --- ClassiCube (classicube/) ------------------------------------------------
# A pinned commit (SDL3 backend), plus the texture pack it needs (plain HTTP, so checksummed)
CLASSICUBE_COMMIT   := aa47929825a4647354588818fb0b002b2f808279
CLASSICUBE_SHA256   := 911e2b179728cbbb3c5f6d90f324f38ebbe6f68c57cb96dd66741fb15e5ab488
CLASSICUBE_URL      := https://github.com/ClassiCube/ClassiCube/archive/$(CLASSICUBE_COMMIT).tar.gz
CLASSICUBE_TEXPACK_SHA256 := 55eac12a9727d209b42694d07dfdca8d50349779ab77ffc11a31d2cb66b45635
CLASSICUBE_TEXPACK_URL := http://static.classicube.net/default.zip

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

# --- Build container ----------------------------------------------------------
# Pinned base image; its apt sources are pointed at the same snapshot date.
# Debian sid (amd64), upgraded to the same snapshot as the PS3's root, so the cross compiler is the
# snapshot's GCC 16.2 (gcc-16-powerpc64-linux-gnu 16.2.0-3cross1), as on the PS3 (docs/PLAN.md)
BUILDER_BASE        := debian@sha256:c53e050391cca6c8517c4a075230b798a0f980faa5d3ed0a912249ea882e40b5
BUILDER_SNAPSHOT    := $(DEBIAN_SNAPSHOT)

# Fixed timestamp for kernel and rootfs builds (2026-10-13 00:00:00 UTC, matches the snapshot).
SOURCE_DATE_EPOCH   := 1791849600

# --- Network / netboot --------------------------------------------------------
PS3_IP              := 10.0.0.77
# Petitboot's address (it has also been given .51 by DHCP at times).
PETITBOOT_IP        := 10.0.0.77
PC_IP               := 10.0.0.45
PC_MAC              := b4:2e:99:f3:12:1a
NFS_ROOT            := /srv/ps3root

# --- apt repository (tools/repo-server/, published with aptly from build/aptly) -----------------
REPO_HOST           := tibi@10.0.0.10
REPO_DIR            := /data/fast/NewServer/Ps3Repo
REPO_URL            := http://10.0.0.10:8070
HTTP_PORT           := 8000
# ps3rsx_cma=64M: the GPU's GART comes on demand from a 64 MiB CMA area, which holds ordinary
# movable pages while the GPU doesn't use it (ps3rsx/src/gart.c); ps3rsx_gart=0 turns off the
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
ROOT_ARGS_usb       := root=PARTUUID=50533301-02 rootfstype=ext4 ro
# The USB disk for the root filesystem (its /dev/disk/by-id name on the PS3): a 500 GB Samsung
# HN-M500MBB in a USB enclosure, given for this (2026-10-15)
USB_DISK_ID         := usb-SAMSUNG_HN-M500MBB_0000AB1234E8-0:0
# The kernel's own arguments: shipped in its package (/usr/lib/ps3/boot/<release>.args), so each
# kernel in the boot menu gets the ones it understands
KERNEL_ARGS         := video=ps3fb:mode:133 consoleblank=0 ps3rsx_gart=0 ps3rsx_cma=64M console=tty1 loglevel=4
# This installation's development extras: DHCP for netconsole, netconsole to this PC
DEBUG_ARGS          := ip=dhcp netconsole=6665@$(PS3_IP)/eth0,6666@$(PC_IP)/$(PC_MAC)
# For kernels whose package has no arguments file (built before 2026-10-15: fixed 32 MiB GART)
LEGACY_KERNEL_ARGS  := video=ps3fb:mode:133 consoleblank=0 ps3rsx_gart=32M console=tty1 loglevel=4
KERNEL_CMDLINE      := $(ROOT_ARGS_$(ROOT)) rootwait $(DEBUG_ARGS) $(KERNEL_ARGS)
