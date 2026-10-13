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

# --- Build container ----------------------------------------------------------
# Pinned base image; its apt sources are pointed at the same snapshot date.
BUILDER_BASE        := debian@sha256:2bf225230f05881b6a36d20f484088f408c9a1309edc730c0bf01f15e635d411
BUILDER_SNAPSHOT    := $(DEBIAN_SNAPSHOT)

# Fixed timestamp for kernel and rootfs builds (2026-10-13 00:00:00 UTC, matches the snapshot).
SOURCE_DATE_EPOCH   := 1791849600

# --- Network / netboot --------------------------------------------------------
PS3_IP              := 10.0.0.77
# Petitboot's DHCP client sends a different client ID than Linux, so the router hands it its own
# lease (not the .77 reservation). Reserve this one too, or update it when it drifts.
PETITBOOT_IP        := 10.0.0.51
PC_IP               := 10.0.0.45
NFS_ROOT            := /srv/ps3root
HTTP_PORT           := 8000
KERNEL_CMDLINE      := root=/dev/nfs nfsroot=$(PC_IP):$(NFS_ROOT),v3,tcp ip=dhcp rw rootwait \
                       video=ps3fb:mode:133 console=tty1 netconsole=@/,6666@$(PC_IP)/ loglevel=7
