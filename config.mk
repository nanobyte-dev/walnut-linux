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

# --- Package upstreams --------------------------------------------------------
# Each package's pinned upstream versions, URLs and sha256s are in its own directory: <pkg>/upstream.mk
# (warzone2100, openrct2, zlib-ng, darkplaces-gl2, classicube, classicube-data, sauerbraten-nonfree-data,
# spu-toolchain) and tools/ssdimg/upstream.mk (the initramfs's busybox, e2fsprogs, GNU tar).

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
