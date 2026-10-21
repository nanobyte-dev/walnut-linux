#!/bin/bash
# Installs the PS3 root filesystem image as an NFS export. Run via `make nfs-root` (uses sudo).
#   tools/install-nfs-root.sh <rootfs.tar>
# Env: NFS_ROOT, PS3_IP, FRESH. An existing NFS_ROOT is moved aside to NFS_ROOT.bak-<date>, never
# deleted. The machine's own state comes over from it unless FRESH=1: SSH host keys (no new
# known_hosts entry), Bluetooth pairings (the DualShock needn't be plugged in again) and saved
# Wi-Fi connections. Everything else is the image as built.
set -euo pipefail

TAR="$(realpath "$1")"
: "${NFS_ROOT:?}" "${PS3_IP:?}"
KEEP=('etc/ssh/ssh_host_*' var/lib/bluetooth etc/NetworkManager/system-connections)

[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
[ -s "$TAR" ] || { echo "missing or empty tarball: $TAR"; exit 1; }

OLD=
if [ -e "$NFS_ROOT" ]; then
	OLD="$NFS_ROOT.bak-$(date +%Y%m%d-%H%M%S)"
	mv "$NFS_ROOT" "$OLD"
	echo "Previous root moved to $OLD"
fi
mkdir -p "$NFS_ROOT"
tar -xpf "$TAR" --numeric-owner --xattrs --xattrs-include='*' -C "$NFS_ROOT"
# an NFS root's DNS comes from the kernel's DHCP (ip=dhcp): NetworkManager leaves the wired port alone
# on it (ps3-system's ps3-nfsroot-network.service)
ln -sfn /proc/net/pnp "$NFS_ROOT/etc/resolv.conf"

if [ -n "$OLD" ] && [ "${FRESH:-}" != 1 ]; then
	for p in "${KEEP[@]}"; do
		for f in "$OLD"/$p; do
			[ -e "$f" ] || continue
			rel="${f#"$OLD"/}"
			mkdir -p "$NFS_ROOT/$(dirname "$rel")"
			cp -a "$f" "$NFS_ROOT/$(dirname "$rel")/"
			echo "kept /$rel"
		done
	done
fi

mkdir -p /etc/exports.d
echo "$NFS_ROOT $PS3_IP(rw,sync,no_root_squash,no_subtree_check)" > /etc/exports.d/ps3.exports
systemctl enable --now nfs-server
exportfs -ra
exportfs -v | grep -F "$NFS_ROOT"
echo "Installed $TAR into $NFS_ROOT"
