#!/bin/sh
# Runs on the PS3 (as root), driven by tools/install-usb-root.sh:
#   usb-root-remote.sh prepare   check the disk, partition it if FORMAT=1, mount the root, move an
#                                existing installation aside
#   usb-root-remote.sh unpack    the image tarball on stdin -> the root partition
#   usb-root-remote.sh finish    fstab, the machine's own state, unmount
# Env: USB_DISK_ID (name in /dev/disk/by-id), FORMAT, FRESH.
# Only that USB disk is ever written: the PS3's own disks (ps3da/db/dc) belong to GameOS.
# Our partition table: DOS, partition 1 swap (label ps3swap), partition 2 ext4 (label ps3root).
# FORMAT=1 gives the disk a random disk id, so two installed disks never share a PARTUUID
# (<id>-01, <id>-02); the root's fstab and boot menu use the disk's own id. Disks partitioned
# before that keep their fixed id 0x50533301 and work as they are.
set -eu
MNT=/mnt/ps3usb
STATE=/mnt/ps3usb/.install-old	# where prepare left the previous installation (a symlink)

fail() { echo "usb-root: $*" >&2; exit 1; }

# the USB disk, checked: a whole sd* disk on the USB bus
usb_disk() {
	link="/dev/disk/by-id/${USB_DISK_ID:?}"
	[ -b "$link" ] || fail "no disk $link"
	dev=$(readlink -f "$link")
	case "$dev" in /dev/sd[a-z]) ;; *) fail "$link is $dev, not a USB disk" ;; esac
	[ "$(lsblk -dno TRAN "$dev")" = usb ] || fail "$dev is not on USB"
}
# ours: a DOS table with our two labelled partitions; sets ID (8 hex digits, the PARTUUIDs' prefix)
our_table() {
	[ "$(blkid -p -o value -s PTTYPE "$dev" 2>/dev/null)" = dos ] || return 1
	[ "$(blkid -p -o value -s TYPE "${dev}1" 2>/dev/null)" = swap ] || return 1
	[ "$(blkid -p -o value -s TYPE "${dev}2" 2>/dev/null)" = ext4 ] || return 1
	[ "$(blkid -p -o value -s LABEL "${dev}2" 2>/dev/null)" = ps3root ] || return 1
	ID=$(sfdisk --disk-id "$dev" | sed 's/^0x//' | tr A-F a-f)
	case "$ID" in [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]) ;; *) return 1 ;; esac
	[ "$ID" != 00000000 ]
}
# a random disk id, never 0 nor the fixed id of the first installs
new_id() {
	while :; do
		id=$(od -An -N4 -tx1 /dev/urandom | tr -d ' \n')
		case "$id" in 00000000|50533301) continue ;; esac
		echo "$id"; return
	done
}

# A running system older than the image lacks sfdisk/mke2fs: use the ones install-usb-root.sh
# sent from the image
if ! command -v sfdisk >/dev/null && [ -x /tmp/usb-tools/usr/sbin/sfdisk ]; then
	PATH=/tmp/usb-tools/usr/sbin:$PATH
	LD_LIBRARY_PATH=/tmp/usb-tools/usr/lib/powerpc64-linux-gnu
	MKE2FS_CONFIG=/tmp/usb-tools/etc/mke2fs.conf
	export PATH LD_LIBRARY_PATH MKE2FS_CONFIG
fi

case "$1" in
prepare)
	usb_disk
	if ! our_table; then
		[ "${FORMAT:-}" = 1 ] || fail "$dev doesn't have our partition table; FORMAT=1 erases it and partitions it"
		ID=$(new_id)
		echo "Partitioning $dev ($(lsblk -dno SIZE,MODEL "$dev")): 4 GiB swap, the rest root; disk id 0x$ID"
		grep -q "^$dev" /proc/mounts && fail "$dev is mounted"
		wipefs -a "$dev"
		sfdisk -q "$dev" <<-EOF
		label: dos
		label-id: 0x$ID
		size=4GiB, type=82
		type=83
		EOF
		udevadm settle
		mkswap -L ps3swap "/dev/disk/by-partuuid/$ID-01"
		# no features newer than Petitboot's kernel knows, so it may read /boot from here later
		mke2fs -t ext4 -q -L ps3root -O ^metadata_csum,^metadata_csum_seed,^64bit,^orphan_file "/dev/disk/by-partuuid/$ID-02"
		udevadm settle
		our_table || fail "$dev: the new partition table doesn't check out"
	fi
	mkdir -p "$MNT"
	mountpoint -q "$MNT" || mount "/dev/disk/by-partuuid/$ID-02" "$MNT"
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
	usb_disk
	our_table || fail "$dev: not our partition table any more"
	cat > "$MNT/etc/fstab" <<-EOF
	# Root on the USB disk (tools/install-usb-root.sh). The kernel mounts it read-only for the
	# boot-time fsck; systemd remounts it read-write.
	PARTUUID=$ID-02  /     ext4   defaults,noatime  0 1
	# Before zram (priority 100): idle pages go to the disk, which leaves the RAM to the programs
	PARTUUID=$ID-01  none  swap   sw,pri=200,noauto 0 0
	tmpfs                 /tmp  tmpfs  defaults,size=64M 0 0
	EOF
	# The boot menu boots this disk's root: its PARTUUID into /etc/default/ps3-boot, then
	# Petitboot's kboot.conf written again from it (ps3-update-kboot from the image, on its root)
	if [ -e "$MNT/etc/default/ps3-boot" ]; then
		if grep -q '^PS3_ROOT_ARGS=.*root=PARTUUID=' "$MNT/etc/default/ps3-boot"; then
			sed -i "/^PS3_ROOT_ARGS=/s/root=PARTUUID=[0-9A-Fa-f-]*/root=PARTUUID=$ID-02/" "$MNT/etc/default/ps3-boot"
		else
			sed -i "s/^PS3_ROOT_ARGS=\"/PS3_ROOT_ARGS=\"root=PARTUUID=$ID-02 /" "$MNT/etc/default/ps3-boot"
		fi
		grep -q "^PS3_ROOT_ARGS=.*root=PARTUUID=$ID-02" "$MNT/etc/default/ps3-boot" ||
			fail "couldn't set the root in $MNT/etc/default/ps3-boot"
		if [ -x "$MNT/usr/sbin/ps3-update-kboot" ]; then
			PS3_BOOT_ROOT="$MNT" sh "$MNT/usr/sbin/ps3-update-kboot"
		fi
	fi
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
	echo "USB root installed on PARTUUID=$ID-02: Petitboot lists its kernels; netboot with"
	echo "make netboot-deb ROOT=usb USB_ROOT_PARTUUID=$ID-02 (or set it in config.local.mk)"
	;;
*)
	fail "usage: $0 prepare|unpack|finish"
	;;
esac
