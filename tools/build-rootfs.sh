#!/bin/bash
# Builds the Debian ppc64 root filesystem tarball. Runs as root inside the builder container
# (privileged, for mounts in the chroot); foreign binaries run through the host's qemu-ppc64 binfmt.
# Inputs (env): DEBIAN_SUITE DEBIAN_MIRROR ROOTFS_PACKAGES ROOTFS_CUSTOMIZE SOURCE_DATE_EPOCH OWNER
# Output: build/rootfs/debian-ppc64.tar
set -euo pipefail
# The body is one block, parsed whole before it runs: editing this file during a build (bash
# otherwise reads scripts as it goes) can't make it run a shifted fragment.
{

OUT=build/rootfs
mkdir -p "$OUT"
PACKAGES="$(grep -v -e '^\s*#' -e '^\s*$' "$ROOTFS_PACKAGES" | paste -sd,)"

export SOURCE_DATE_EPOCH
export PUBKEY_FILE="$PWD/$OUT/authorized_keys"

mmdebstrap --mode=root --format=tar --arch=ppc64 --variant=minbase \
	--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
	--aptopt='Acquire::Check-Valid-Until "false"' \
	--aptopt='APT::Install-Recommends "false"' \
	--include="$PACKAGES" \
	--customize-hook="bash $PWD/$ROOTFS_CUSTOMIZE \"\$1\"" \
	"$DEBIAN_SUITE" "$OUT/debian-ppc64.tar.tmp" "$DEBIAN_MIRROR"

# The previous image is moved aside, never overwritten
if [ -e "$OUT/debian-ppc64.tar" ]; then
	OLD="build/old/$(date +%Y%m%d-%H%M%S)-rootfs"
	mkdir -p "$OLD" && mv "$OUT/debian-ppc64.tar" "$OLD/"
	echo ">> Previous rootfs moved to $OLD"
fi
mv "$OUT/debian-ppc64.tar.tmp" "$OUT/debian-ppc64.tar"
chown "$OWNER" "$OUT/debian-ppc64.tar"
exit
}
