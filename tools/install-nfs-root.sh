#!/bin/bash
# Installs the PS3 root filesystem as an NFS export. Run via `make nfs-root` (uses sudo).
#   tools/install-nfs-root.sh <rootfs.tar> <kernel stage dir>
# Env: NFS_ROOT, PS3_IP. An existing NFS_ROOT is moved aside to NFS_ROOT.bak-<date>, never deleted.
set -euo pipefail

TAR="$(realpath "$1")"
STAGE="$(realpath "$2")"
: "${NFS_ROOT:?}" "${PS3_IP:?}"

[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
[ -s "$TAR" ] || { echo "missing or empty tarball: $TAR"; exit 1; }

if [ -e "$NFS_ROOT" ]; then
	mv "$NFS_ROOT" "$NFS_ROOT.bak-$(date +%Y%m%d-%H%M%S)"
fi
mkdir -p "$NFS_ROOT"
tar -xpf "$TAR" --numeric-owner --xattrs --xattrs-include='*' -C "$NFS_ROOT"
rsync -a --chown=root:root "$STAGE/lib/modules/" "$NFS_ROOT/lib/modules/"

mkdir -p /etc/exports.d
echo "$NFS_ROOT $PS3_IP(rw,sync,no_root_squash,no_subtree_check)" > /etc/exports.d/ps3.exports
systemctl enable --now nfs-server
exportfs -ra
exportfs -v | grep -F "$NFS_ROOT"
echo "Installed $TAR into $NFS_ROOT"
