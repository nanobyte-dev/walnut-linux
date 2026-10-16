#!/bin/bash
# Builds the PS3 root filesystem image: the package layer (build/rootfs/base.tar, from
# build-rootfs.sh) plus our configuration (image/customize.sh) and our own packages from
# build/repo (kernel, RSX driver, Mesa, games, SPU tools: tools/build-debs.sh). Takes a minute or
# two, so a config change doesn't reinstall the Debian packages. Runs as root in the builder
# container. Inputs (env): ROOTFS_CUSTOMIZE SOURCE_DATE_EPOCH BOOT_ROOT_ARGS BOOT_EXTRA_ARGS
# BOOT_LEGACY_ARGS KERNEL_RELEASE OWNER
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
# The installation's settings our packages' scripts read: the boot menu (ps3-boot writes
# /etc/kboot.conf from it when the kernel package goes in) and the desktop's autologin user
cat > "$R/etc/default/ps3-boot" <<EOF
# Petitboot boot menu settings (ps3-update-kboot writes /etc/kboot.conf from them)
PS3_ROOT_ARGS="$BOOT_ROOT_ARGS"
PS3_EXTRA_ARGS="$BOOT_EXTRA_ARGS"
PS3_LEGACY_ARGS="$BOOT_LEGACY_ARGS"
# the debug flavour boots by default until the release flavour has proven itself
PS3_DEFAULT_FLAVOUR=ps3
EOF
printf '%s\n' '# The user that gets the desktop on tty1 (ps3-desktop; empty: a normal login prompt)' \
	'PS3_DESKTOP_USER=tibi' > "$R/etc/default/ps3-desktop"
# DNS for apt while the packages go in (customize.sh points it at the kernel's DHCP afterwards)
rm -f "$R/etc/resolv.conf" && cp /etc/resolv.conf "$R/etc/resolv.conf"

# Our packages, installed by apt from a local flat repository of our debs (build/repo, and the
# Debian-source packages in build/debs/*), pinned above Debian's, with the base's snapshot source
# for anything new. Our Mesa replaces Debian's as an upgrade (auto marks kept); then whatever only
# Debian's Mesa needed (LLVM, z3, sensors) is autoremoved.
mkdir -p "$R/tmp/ps3-repo"
cp build/repo/*.deb "$R/tmp/ps3-repo/"
find build/debs -mindepth 2 -maxdepth 2 -name "*.deb" -exec cp -t "$R/tmp/ps3-repo/" {} +
rm -f "$R"/tmp/ps3-repo/ps3-mesa_*.deb "$R"/tmp/ps3-repo/*-dbgsym_*.deb	# transitional only / debug symbols
(cd "$R/tmp/ps3-repo" && apt-ftparchive packages . > Packages)
echo 'deb [trusted=yes] file:/tmp/ps3-repo ./' > "$R/etc/apt/sources.list.d/zz-image.list"
printf 'Package: *\nPin: origin ""\nPin-Priority: 1001\n' > "$R/etc/apt/preferences.d/zz-image"
PKGS=$(grep -v -e '^\s*#' -e '^\s*$' image/packages-ours.txt | sed "s/@KERNEL_RELEASE@/$KERNEL_RELEASE/")
APT="chroot $R env DEBIAN_FRONTEND=noninteractive apt-get -q -y -o Dpkg::Use-Pty=0"
$APT update > /dev/null
$APT --no-remove upgrade > /dev/null
$APT --no-remove install $PKGS > /dev/null
$APT autoremove --purge | grep "^Removing " | cut -d" " -f2 | paste -sd' ' | sed 's/^/>> autoremoved: /'
chroot "$R" dpkg-query -W -f='${Package} ${Version}\n' mesa-libgallium libgbm1 | sed 's/^/>> /'
if chroot "$R" dpkg-query -W -f='${Package}\n' 'libllvm*' 'libz3-*' 2>/dev/null | grep -q .; then
	echo "!! LLVM or z3 still installed"; exit 1
fi
# The PS3's own apt configuration (Debian live plus ours, pinned) is ps3-system's; the build's
# temporary sources go (ps3-system set the snapshot sources.list aside)
rm -r "$R/tmp/ps3-repo" "$R/etc/apt/sources.list.d/zz-image.list" "$R/etc/apt/preferences.d/zz-image"
rm -f "$R/etc/apt/sources.list" "$R/etc/apt/sources.list.ps3-snapshot"
chroot "$R" apt-get clean
rm -rf "$R"/var/lib/apt/lists/* && mkdir -p "$R/var/lib/apt/lists/partial"
chroot "$R" ldconfig

# The installation's own settings: hostname, user, SSH key, locale (image/customize.sh)
PUBKEY_FILE="$PWD/$OUT/authorized_keys" bash "$ROOTFS_CUSTOMIZE" "$R"

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

# The previous image is kept as one backup (images are ephemeral: CLAUDE.md)
[ ! -e "$OUT/debian-ppc64.tar" ] || mv "$OUT/debian-ppc64.tar" "$OUT/debian-ppc64.prev.tar"
mv "$OUT/debian-ppc64.tar.tmp" "$OUT/debian-ppc64.tar"
chown "$OWNER" "$OUT/debian-ppc64.tar"
exit
}
