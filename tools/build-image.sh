#!/bin/bash
# Builds the PS3 root filesystem image: the package layer (build/rootfs/base.tar, from
# build-rootfs.sh) plus our configuration (rootfs/customize.sh) and our own packages from
# build/repo (kernel, RSX driver, Mesa, games, SPU tools: tools/build-debs.sh). Takes a minute or
# two, so a config change doesn't reinstall the Debian packages. Runs as root in the builder
# container. Inputs (env): ROOTFS_CUSTOMIZE SOURCE_DATE_EPOCH KERNEL_RELEASE KBOOT_CMDLINE OWNER
# Output: build/rootfs/debian-ppc64.tar, byte-for-byte the same from the same inputs
set -euo pipefail
{

OUT=build/rootfs
R="$(mktemp -d /tmp/ps3root.XXXXXX)"
trap 'umount "$R/proc" "$R/sys" 2>/dev/null || true; rm -rf --one-file-system "$R"' EXIT

# The build host's SELinux labels (Docker's overlay on Fedora) ride along in base.tar's xattrs:
# they mean nothing on the PS3 and make the image depend on the host, so they're dropped
tar -xpf "$OUT/base.tar" --numeric-owner --xattrs --xattrs-include='*' \
	--xattrs-exclude='security.selinux' -C "$R"
mount -t proc proc "$R/proc"
mount -t sysfs sysfs "$R/sys"

export SOURCE_DATE_EPOCH
PUBKEY_FILE="$PWD/$OUT/authorized_keys" bash "$ROOTFS_CUSTOMIZE" "$R"
# Our packages (docs/PLAN.md item 7), the way apt would install them
mkdir -p "$R/tmp/ps3-repo"
cp build/repo/*.deb "$R/tmp/ps3-repo/"
chroot "$R" sh -c 'dpkg -i /tmp/ps3-repo/*.deb' > /dev/null
rm -r "$R/tmp/ps3-repo"
chroot "$R" ldconfig
# Petitboot's boot menu for a disk root: it reads /etc/kboot.conf on the partitions it can mount
# (ext4 included) and boots the default entry after its countdown, without the network
printf '%s\n' "# Petitboot boot menu (tools/build-image.sh)" "default=debian" \
	"debian='/boot/vmlinux-$KERNEL_RELEASE $KBOOT_CMDLINE'" > "$R/etc/kboot.conf"

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
	--xattrs-exclude='security.selinux' \
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
