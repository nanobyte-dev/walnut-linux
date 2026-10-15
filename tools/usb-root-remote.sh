#!/bin/sh
# Runs on the PS3 (as root), driven by tools/install-usb-root.sh:
#   usb-root-remote.sh prepare   check the disk, partition it if FORMAT=1, mount the root, move an
#                                existing installation aside
#   usb-root-remote.sh unpack    the image tarball on stdin -> the root partition
#   usb-root-remote.sh finish    fstab, the machine's own state, unmount
# Env: USB_DISK_ID (name in /dev/disk/by-id), FORMAT, FRESH.
# Only that USB disk is ever written: the PS3's own disks (ps3da/db/dc) belong to GameOS.
set -eu
DISK_LABEL_ID=0x50533301	# our partition table's disk id: PARTUUID=50533301-0N
SWAP=/dev/disk/by-partuuid/50533301-01
ROOT=/dev/disk/by-partuuid/50533301-02
MNT=/mnt/ps3usb
STATE=/mnt/ps3usb/.install-old	# where prepare left the previous installation (a symlink)

fail() { echo "usb-root: $*" >&2; exit 1; }

case "$1" in
prepare)
	link="/dev/disk/by-id/${USB_DISK_ID:?}"
	[ -b "$link" ] || fail "no disk $link"
	dev=$(readlink -f "$link")
	case "$dev" in /dev/sd[a-z]) ;; *) fail "$link is $dev, not a USB disk" ;; esac
	[ "$(lsblk -dno TRAN "$dev")" = usb ] || fail "$dev is not on USB"
	if [ "$(sfdisk --disk-id "$dev")" != "$DISK_LABEL_ID" ]; then
		[ "${FORMAT:-}" = 1 ] || fail "$dev doesn't have our partition table; FORMAT=1 erases it and partitions it"
		echo "Partitioning $dev ($(lsblk -dno SIZE,MODEL "$dev")): 4 GiB swap, the rest root"
		grep -q "^$dev" /proc/mounts && fail "$dev is mounted"
		wipefs -a "$dev"
		sfdisk -q "$dev" <<-EOF
		label: dos
		label-id: $DISK_LABEL_ID
		size=4GiB, type=82
		type=83
		EOF
		udevadm settle
		mkswap -L ps3swap "$SWAP"
		# no features newer than Petitboot's kernel knows, so it may read /boot from here later
		mkfs.ext4 -q -L ps3root -O ^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file "$ROOT"
	fi
	mkdir -p "$MNT"
	mountpoint -q "$MNT" || mount "$ROOT" "$MNT"
	# A previous installation is moved aside (into .old-<date> on the same disk), never deleted
	rm -f "$STATE"
	if [ -e "$MNT/etc" ]; then
		old=".old-$(date +%Y%m%d-%H%M%S)"
		mkdir "$MNT/$old"
		for f in "$MNT"/* "$MNT"/.[!.]*; do
			case "${f#"$MNT"/}" in .old-*|lost+found|'*'|'.[!.]*') continue ;; esac
			mv "$f" "$MNT/$old/"
		done
		ln -s "$old" "$STATE"
		echo "Previous installation moved to $MNT/$old"
	fi
	;;
unpack)
	mountpoint -q "$MNT" || fail "$MNT not mounted (run prepare)"
	tar -xp --numeric-owner --xattrs --xattrs-include='*' -C "$MNT"
	;;
finish)
	mountpoint -q "$MNT" || fail "$MNT not mounted (run prepare)"
	cat > "$MNT/etc/fstab" <<-EOF
	# Root on the USB disk (tools/install-usb-root.sh). The kernel mounts it read-only for the
	# boot-time fsck; systemd remounts it read-write.
	PARTUUID=50533301-02  /     ext4   defaults,noatime  0 1
	# Before zram (priority 100): idle pages go to the disk, which leaves the RAM to the programs
	PARTUUID=50533301-01  none  swap   sw,pri=200        0 0
	tmpfs                 /tmp  tmpfs  defaults,size=64M 0 0
	EOF
	# The machine's own state: SSH host keys, Bluetooth pairings, Wi-Fi connections. From the
	# previous installation on this disk, or else from the running system.
	if [ "${FRESH:-}" != 1 ]; then
		if [ -L "$STATE" ]; then from="$MNT/$(readlink "$STATE")"; else from=; fi
		for p in 'etc/ssh/ssh_host_*' var/lib/bluetooth etc/NetworkManager/system-connections; do
			for f in $from/$p; do
				[ -e "$f" ] || continue
				rel="${f#"$from"/}"
				mkdir -p "$MNT/$(dirname "$rel")"
				cp -a "$f" "$MNT/$(dirname "$rel")/"
				echo "kept /$rel"
			done
		done
	fi
	rm -f "$STATE"
	sync
	umount "$MNT"
	echo "USB root installed: boot it with make netboot ROOT=usb"
	;;
*)
	fail "usage: $0 prepare|unpack|finish"
	;;
esac
