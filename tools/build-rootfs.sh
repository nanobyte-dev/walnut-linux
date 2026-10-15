#!/bin/bash
# Builds the package layer of the PS3 root filesystem: Debian ppc64 with every package from
# rootfs/packages.txt installed, nothing of ours yet (build-image.sh adds that). This is the slow
# part (dpkg under qemu, about 15 minutes), so it only reruns when the package list or the
# snapshot changes. Runs as root inside the builder container (privileged, for mounts in the
# chroot); foreign binaries run through the host's qemu-ppc64 binfmt.
# Inputs (env): DEBIAN_SUITE DEBIAN_MIRROR ROOTFS_PACKAGES SOURCE_DATE_EPOCH OWNER
# Output: build/rootfs/base.tar. Downloaded packages are kept in build/apt-cache.
set -euo pipefail
# The body is one block, parsed whole before it runs: editing this file during a build (bash
# otherwise reads scripts as it goes) can't make it run a shifted fragment.
{

OUT=build/rootfs
CACHE="$PWD/build/apt-cache"
mkdir -p "$OUT" "$CACHE"
PACKAGES="$(grep -v -e '^\s*#' -e '^\s*$' "$ROOTFS_PACKAGES" | paste -sd,)"

export SOURCE_DATE_EPOCH

# The .deb cache goes in before the download and comes back out after the install
# (mmdebstrap's sync-in/sync-out recipe); the snapshot pins the versions, apt checks the hashes
mmdebstrap --mode=root --format=tar --arch=ppc64 --variant=minbase \
	--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
	--aptopt='Acquire::Check-Valid-Until "false"' \
	--aptopt='APT::Install-Recommends "false"' \
	--include="$PACKAGES" \
	--skip=essential/unlink \
	--setup-hook='mkdir -p "$1"/var/cache/apt/archives/' \
	--setup-hook="sync-in $CACHE /var/cache/apt/archives/" \
	--customize-hook="sync-out /var/cache/apt/archives $CACHE" \
	"$DEBIAN_SUITE" "$OUT/base.tar.tmp" "$DEBIAN_MIRROR"
chown -R "$OWNER" "$CACHE"

# The previous layer is moved aside, never overwritten
if [ -e "$OUT/base.tar" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-rootfs-base"
	mkdir -p "$OLD" && mv "$OUT/base.tar" "$OLD/"
	echo ">> Previous package layer moved to $OLD"
fi
mv "$OUT/base.tar.tmp" "$OUT/base.tar"
chown "$OWNER" "$OUT/base.tar"
exit
}
