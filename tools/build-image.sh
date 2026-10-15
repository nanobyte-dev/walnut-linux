#!/bin/bash
# Builds the PS3 root filesystem image: the package layer (build/rootfs/base.tar, from
# build-rootfs.sh) plus our configuration (rootfs/customize.sh) and everything we build
# ourselves (kernel modules, RSX driver, Mesa, games; tools/install-builds.sh). Takes a minute or
# two, so a config change doesn't reinstall the packages. Runs as root in the builder container.
# Inputs (env): ROOTFS_CUSTOMIZE SOURCE_DATE_EPOCH MESA_PREFIX KBOOT_CMDLINE OWNER
# Output: build/rootfs/debian-ppc64.tar, byte-for-byte the same from the same inputs
set -euo pipefail
{

OUT=build/rootfs
R="$(mktemp -d /tmp/ps3root.XXXXXX)"
trap 'umount "$R/proc" "$R/sys" 2>/dev/null || true; rm -rf --one-file-system "$R"' EXIT

tar -xpf "$OUT/base.tar" --numeric-owner --xattrs --xattrs-include='*' -C "$R"
mount -t proc proc "$R/proc"
mount -t sysfs sysfs "$R/sys"

export SOURCE_DATE_EPOCH
PUBKEY_FILE="$PWD/$OUT/authorized_keys" bash "$ROOTFS_CUSTOMIZE" "$R"
BUILD=build tools/install-builds.sh "$R" boot modules gpu mesa classicube
# the Mesa path in ld.so.conf.d (setuid Xorg ignores LD_LIBRARY_PATH)
chroot "$R" ldconfig

umount "$R/proc" "$R/sys"
# What mmdebstrap's cleanup removes for reproducibility; the chroot commands above write some of
# it again
rm -f "$R"/var/log/dpkg.log "$R"/var/log/apt/history.log "$R"/var/log/apt/term.log \
	"$R"/var/log/alternatives.log "$R"/var/cache/ldconfig/aux-cache "$R"/var/log/apt/eipp.log.xz \
	"$R"/var/lib/dbus/machine-id
[ ! -e "$R/etc/machine-id" ] || : > "$R/etc/machine-id"
find "$R/run" "$R/tmp" -mindepth 1 -delete

# Packed the way mmdebstrap packs its tarballs
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --clamp-mtime --numeric-owner --one-file-system \
	--format=pax --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime --xattrs \
	-C "$R" -cf "$OUT/debian-ppc64.tar.tmp" .

# The previous image is moved aside, never overwritten
if [ -e "$OUT/debian-ppc64.tar" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-rootfs"
	mkdir -p "$OLD" && mv "$OUT/debian-ppc64.tar" "$OLD/"
	chown -R "$OWNER" "$OLD"	# yours to delete (this script runs as root)
	echo ">> Previous image moved to $OLD"
fi
mv "$OUT/debian-ppc64.tar.tmp" "$OUT/debian-ppc64.tar"
chown "$OWNER" "$OUT/debian-ppc64.tar"
exit
}
