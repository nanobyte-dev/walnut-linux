# Build settings. Everything that decides what ends up on the PS3 lives here or in the files it points to.

# --- Kernel -------------------------------------------------------------------
KERNEL_VERSION      := 6.12.112
KERNEL_SHA256       := 164dc9d1f6c93c61a15e1f071c48379b467f2b17c469cce7223471968208ed03
KERNEL_URL          := https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$(KERNEL_VERSION).tar.xz
KERNEL_LOCALVERSION := -ps3
KERNEL_DEFCONFIG    := ps3_defconfig
KERNEL_FRAGMENTS    := kernel/config/ps3.config
KERNEL_SERIES       := kernel/patches/series

# --- Debian root filesystem ---------------------------------------------------
# Packages come from a frozen snapshot of debian-ports, so a rebuild gets the same versions.
DEBIAN_SNAPSHOT     := 20261013T000000Z
DEBIAN_SUITE        := unstable
DEBIAN_MIRROR       := http://snapshot.debian.org/archive/debian-ports/$(DEBIAN_SNAPSHOT)
ROOTFS_PACKAGES     := rootfs/packages.txt
ROOTFS_CUSTOMIZE    := rootfs/customize.sh
SSH_PUBKEY          := $(HOME)/.ssh/id_ps3.pub

# --- Mesa (userspace GL for the RSX) ------------------------------------------
# Same Mesa version as the snapshot's Debian packages; built with our nv30 patch.
MESA_VERSION        := 26.2.4
MESA_SHA256         := bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9
MESA_URL            := https://archive.mesa3d.org/mesa-$(MESA_VERSION).tar.xz
# Our fork of the files we change (nouveau driver, loader/dri/gbm bits), laid over the tarball
MESA_FORK           := mesa/fork
MESA_SYSROOT_PACKAGES := mesa/sysroot-packages.txt
# Runtime path on the PS3. /home/tibi is NFS_ROOT/home/tibi (owned by tibi), so no sudo to deploy.
MESA_PREFIX         := /home/tibi/mesa

# --- ClassiCube (games/classicube) ------------------------------------------------
# A pinned commit (SDL3 backend), plus the texture pack it needs (plain HTTP, so checksummed)
CLASSICUBE_COMMIT   := aa47929825a4647354588818fb0b002b2f808279
CLASSICUBE_SHA256   := 911e2b179728cbbb3c5f6d90f324f38ebbe6f68c57cb96dd66741fb15e5ab488
CLASSICUBE_URL      := https://github.com/ClassiCube/ClassiCube/archive/$(CLASSICUBE_COMMIT).tar.gz
CLASSICUBE_TEXPACK_SHA256 := 55eac12a9727d209b42694d07dfdca8d50349779ab77ffc11a31d2cb66b45635
CLASSICUBE_TEXPACK_URL := http://static.classicube.net/default.zip

# --- Build container ----------------------------------------------------------
# Pinned base image; its apt sources are pointed at the same snapshot date.
BUILDER_BASE        := debian@sha256:2bf225230f05881b6a36d20f484088f408c9a1309edc730c0bf01f15e635d411
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
HTTP_PORT           := 8000
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
KERNEL_CMDLINE_COMMON := ip=dhcp rootwait \
                       video=ps3fb:mode:133 consoleblank=0 ps3rsx_gart=16M console=tty1 netconsole=6665@$(PS3_IP)/eth0,6666@$(PC_IP)/$(PC_MAC) loglevel=4
KERNEL_CMDLINE      := $(ROOT_ARGS_$(ROOT)) $(KERNEL_CMDLINE_COMMON)
# What Petitboot boots from the disk on its own (the image's /etc/kboot.conf)
KBOOT_CMDLINE       := $(ROOT_ARGS_usb) $(KERNEL_CMDLINE_COMMON)
