#!/bin/bash
# Builds the PS3 root filesystem image: the package layer (build/rootfs/base.tar, from
# build-rootfs.sh) plus our configuration (image/customize.sh) and our own packages from
# build/debs/* (kernels, RSX driver, Mesa, games, SPU tools: make packages), REPO_SUITE or WALNUT_SUITE. Takes a
# minute or two, so a config change doesn't reinstall the Debian packages. Runs as root in the
# builder container. Inputs (env): ROOTFS_CUSTOMIZE SOURCE_DATE_EPOCH BOOT_ROOT_ARGS
# BOOT_EXTRA_ARGS REPO_SUITE REPO_URL WALNUT_SUITE WALNUT_APT WALNUT_KEYRING OWNER
# Output: build/rootfs/debian-ppc64.tar, byte-for-byte the same from the same inputs
set -euo pipefail
{

OUT=build/rootfs
PROFILE="${PROFILE:-dev}"
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
if [ "$PROFILE" = "public" ]; then
	BOOT_EXTRA_ARGS_CLEAN=$(echo "${BOOT_EXTRA_ARGS:-}" | sed -E 's/netconsole=[^ ]*//g; s/video=[^ ]*//g; s/ip=dhcp//g' | xargs)
	cat > "$R/etc/default/ps3-boot" <<EOF
# Petitboot boot menu settings (ps3-update-kboot writes /etc/kboot.conf from them)
PS3_ROOT_ARGS="$BOOT_ROOT_ARGS"
PS3_EXTRA_ARGS="$BOOT_EXTRA_ARGS_CLEAN"
# the release kernel boots by default; the debug one is in the menu next to it
PS3_DEFAULT_FLAVOUR=release
EOF
	# Public image: no default desktop user (walnut-firstboot creates the user on first boot)
	printf '%s\n' '# The user that gets the desktop on tty1 (ps3-desktop; empty: a normal login prompt)' \
		'PS3_DESKTOP_USER=' > "$R/etc/default/ps3-desktop"
else
	cat > "$R/etc/default/ps3-boot" <<EOF
# Petitboot boot menu settings (ps3-update-kboot writes /etc/kboot.conf from them)
PS3_ROOT_ARGS="$BOOT_ROOT_ARGS"
PS3_EXTRA_ARGS="$BOOT_EXTRA_ARGS"
# the release kernel boots by default; the debug one is in the menu next to it
PS3_DEFAULT_FLAVOUR=release
EOF
	printf '%s\n' '# The user that gets the desktop on tty1 (ps3-desktop; empty: a normal login prompt)' \
		'PS3_DESKTOP_USER=tibi' > "$R/etc/default/ps3-desktop"
fi
# DNS for apt while the packages go in (customize.sh points it at the kernel's DHCP afterwards)
rm -f "$R/etc/resolv.conf" && cp /etc/resolv.conf "$R/etc/resolv.conf"

# Our packages, installed by apt from a local flat repository of our debs (build/debs/*) or,
# with REPO_SUITE, a frozen snapshot of the stable suite on the server; pinned above Debian's, with
# the base's snapshot source for anything new. Our Mesa replaces Debian's as an upgrade (auto marks kept); then whatever only
# Debian's Mesa needed (LLVM, z3, sensors) is autoremoved.
if [ -n "${WALNUT_SUITE:-}" ]; then
	# a suite on walnut.nanobyte.dev (the release image: stable), checked against its key
	# (WALNUT_KEYRING); the exact versions go in the release's manifest
	echo ">> our packages from $WALNUT_APT, suite $WALNUT_SUITE"
	mkdir -p "$R/tmp/ps3-repo"
	cp "$WALNUT_KEYRING" "$R/tmp/ps3-repo/keyring.gpg"
	echo "deb [signed-by=/tmp/ps3-repo/keyring.gpg] $WALNUT_APT $WALNUT_SUITE main" > "$R/etc/apt/sources.list.d/zz-image.list"
	printf 'Package: *\nPin: release o=Walnut, n=%s\nPin-Priority: 1001\n' "$WALNUT_SUITE" > "$R/etc/apt/preferences.d/zz-image"
elif [ -n "${REPO_SUITE:-}" ]; then
	# a frozen snapshot of our stable suite on the server (make repo-snapshot)
	echo ">> our packages from $REPO_URL/ps3, suite $REPO_SUITE"
	mkdir -p "$R/tmp/ps3-repo"
	echo "deb [trusted=yes] $REPO_URL/ps3 $REPO_SUITE main deps" > "$R/etc/apt/sources.list.d/zz-image.list"
	if [ "$PROFILE" != "public" ]; then
		# the licensed suite's snapshot of the same name (classicube-data, game data), when there is
		# one: snapshots from before the ps3-licensed split have their licensed packages in ps3 itself
		if python3 -c 'import sys, urllib.request; urllib.request.urlopen(sys.argv[1], timeout=30)' \
				"$REPO_URL/ps3-licensed/dists/$REPO_SUITE/Release" 2>/dev/null; then
			echo ">> licensed packages from $REPO_URL/ps3-licensed, suite $REPO_SUITE"
			echo "deb [trusted=yes] $REPO_URL/ps3-licensed $REPO_SUITE main" >> "$R/etc/apt/sources.list.d/zz-image.list"
		fi
	fi
	printf 'Package: *\nPin: release n=%s\nPin-Priority: 1001\n' "$REPO_SUITE" > "$R/etc/apt/preferences.d/zz-image"
else
	# this checkout's build outputs, as a flat repository
	mkdir -p "$R/tmp/ps3-repo"
	find build/debs -mindepth 2 -maxdepth 2 -name "*.deb" -exec cp -t "$R/tmp/ps3-repo/" {} +
	rm -f "$R"/tmp/ps3-repo/*-dbgsym_*.deb	# debug symbols
	if [ "$PROFILE" = "public" ]; then
		# No debug kernel or debug ps3rsx in public image
		rm -f "$R"/tmp/ps3-repo/*-debug*.deb
		# No licensed debs in the flat repo: tools/leak-check.py verifies each package
		for d in "$R"/tmp/ps3-repo/*.deb; do
			[ -e "$d" ] || continue
			if ! python3 tools/leak-check.py debs "$d" >/dev/null 2>&1; then
				rm -f "$d"
			fi
		done
	fi
	(cd "$R/tmp/ps3-repo" && apt-ftparchive packages . > Packages)
	echo 'deb [trusted=yes] file:/tmp/ps3-repo ./' > "$R/etc/apt/sources.list.d/zz-image.list"
	printf 'Package: *\nPin: origin ""\nPin-Priority: 1001\n' > "$R/etc/apt/preferences.d/zz-image"
fi
# man-db: no database rebuild on every package install (slow and RAM-hungry on the PS3; its daily
# timer still updates it), and none in the image (index.db isn't reproducible)
echo "man-db man-db/auto-update boolean false" | chroot "$R" debconf-set-selections
if [ "$PROFILE" = "public" ]; then
	PKGS=$(grep -v -e '^\s*#' -e '^\s*$' image/packages-ours.txt | \
		grep -v -E '^(linux-image-ps3-debug|ps3rsx-modules-ps3-debug|ps3-games-licensed|classicube)$' | \
		sed 's/^ps3-games$/ps3-games-free/')
else
	PKGS=$(grep -v -e '^\s*#' -e '^\s*$' image/packages-ours.txt)
fi
APT="chroot $R env DEBIAN_FRONTEND=noninteractive apt-get -q -y -o Dpkg::Use-Pty=0 -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold"
$APT update > /dev/null
$APT --no-remove upgrade > /dev/null
$APT --no-remove install $PKGS > /dev/null
$APT autoremove --purge | grep "^Removing " | cut -d" " -f2 | paste -sd' ' | sed 's/^/>> autoremoved: /'
chroot "$R" dpkg-query -W -f='${Package} ${Version}\n' mesa-libgallium libgbm1 | sed 's/^/>> /'
if chroot "$R" dpkg-query -W -f='${Package}\n' 'libllvm*' 'libz3-*' 2>/dev/null | grep -q .; then
	echo "!! LLVM or z3 still installed"; exit 1
fi
# Public profile: strip video= from kernel boot arguments and update kboot.conf
if [ "$PROFILE" = "public" ]; then
	for argf in "$R"/usr/lib/ps3/boot/*.args; do
		[ -e "$argf" ] || continue
		sed -i -E 's/video=ps3fb:mode:[0-9]+ *//g' "$argf"
	done
	chroot "$R" ps3-update-kboot > /dev/null 2>&1 || true
fi
# The PS3's own apt configuration (Debian live plus ours, pinned) is ps3-system's; the build's
# temporary sources go (ps3-system set the snapshot sources.list aside)
rm -r "$R/tmp/ps3-repo" "$R/etc/apt/sources.list.d/zz-image.list" "$R/etc/apt/preferences.d/zz-image"
rm -f "$R/etc/apt/sources.list" "$R/etc/apt/sources.list.ps3-snapshot"
chroot "$R" apt-get clean
rm -rf "$R"/var/lib/apt/lists/* && mkdir -p "$R/var/lib/apt/lists/partial"
chroot "$R" ldconfig

# The installation's own settings: hostname, user, SSH key, locale (image/customize.sh)
PROFILE="$PROFILE" PUBKEY_FILE="$PWD/$OUT/authorized_keys" bash "$ROOTFS_CUSTOMIZE" "$R"

umount "$R/proc" "$R/sys"
# What mmdebstrap's cleanup removes for reproducibility; the chroot commands above write some of
# it again
rm -f "$R"/var/log/dpkg.log "$R"/var/log/apt/history.log "$R"/var/log/apt/term.log \
	"$R"/var/log/alternatives.log "$R"/var/cache/ldconfig/aux-cache "$R"/var/log/apt/eipp.log.xz \
	"$R"/var/lib/dbus/machine-id
[ ! -e "$R/etc/machine-id" ] || : > "$R/etc/machine-id"
rm -rf "$R"/var/cache/man/*
find "$R/run" "$R/tmp" -mindepth 1 -delete

# Packed the way mmdebstrap packs its tarballs
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --clamp-mtime --numeric-owner --one-file-system \
	--format=pax --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime --xattrs \
	--xattrs-exclude='security.selinux' \
	-C "$R" -cf "$OUT/debian-ppc64.tar.tmp" .

# The previous image is kept as one backup (images are ephemeral: CLAUDE.md)
[ ! -e "$OUT/debian-ppc64.tar" ] || mv "$OUT/debian-ppc64.tar" "$OUT/debian-ppc64.prev.tar"
mv "$OUT/debian-ppc64.tar.tmp" "$OUT/debian-ppc64.tar"
chown "$OWNER" "$OUT" "$OUT/debian-ppc64.tar"
exit
}
