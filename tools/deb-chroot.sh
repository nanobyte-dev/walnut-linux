#!/bin/bash
# deb-chroot.sh: the ppc64 build chroot for native package builds (Debian's buildd variant at the
# debian-ports snapshot, plus ccache and eatmydata). Runs as root in the deb builder (privileged);
# ppc64 binaries run through the host's qemu-ppc64 binfmt. Apt lists are kept so builds can
# install their build dependencies. Output: build/chroot/sid-ppc64.tar (replaced, ephemeral).
# Env: DEBIAN_SUITE DEBIAN_MIRROR OWNER
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
OUT=build/chroot
CACHE="$PWD/build/apt-cache"
mkdir -p "$OUT" "$CACHE"
mmdebstrap --mode=root --format=tar --arch=ppc64 --variant=buildd \
	--keyring=/usr/share/keyrings/debian-ports-archive-keyring.gpg \
	--aptopt='Acquire::Check-Valid-Until "false"' \
	--aptopt='APT::Install-Recommends "false"' \
	--include=ccache,eatmydata,debian-ports-archive-keyring \
	--skip=cleanup/apt/lists --skip=essential/unlink \
	--setup-hook='mkdir -p "$1"/var/cache/apt/archives/' \
	--setup-hook="sync-in $CACHE /var/cache/apt/archives/" \
	--customize-hook="sync-out /var/cache/apt/archives $CACHE" \
	--customize-hook='rm -f "$1"/var/cache/apt/archives/*.deb' \
	"$DEBIAN_SUITE" "$OUT/sid-ppc64.tar.tmp" "$DEBIAN_MIRROR"
mv "$OUT/sid-ppc64.tar.tmp" "$OUT/sid-ppc64.tar"
chown -R "$OWNER" "$OUT" "$CACHE"
echo ">> $OUT/sid-ppc64.tar"
exit
}
