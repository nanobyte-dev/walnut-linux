#!/bin/bash
# ssd-install-image.sh: Phase B on the PS3 (docs/GAMEOS-HDD.md "Phase B: the whole system on the SSD"),
# run as root from the USB root with Phase A's devices in place (ps3-imgmap create ... ps3ssd
# --swap-mib 1024 in this boot). FROM is a copy of the PC's build/initrd (the initramfs, its
# .manifest, maps/, ps3-imgmap-boot.ppc64). --map MAP: the map in /var/lib/ps3-imgmap when there are several.
#   ssd-install-image.sh check --from FROM
#       Read-only: the USB root is the running root; /dev/mapper/ps3ssd and ps3ssd-swap are the
#       devices ps3-imgmap create made from a verified map (state in /var/lib/ps3-imgmap: records,
#       the tables it wrote, the kernel's live tables); the initramfs's boot map makes exactly those
#       tables (ps3-imgmap-boot tables) and is the one inside the initramfs (manifest sha256s); the
#       initramfs's kernel release is installed here with the same ufs and dm-mod modules.
#   ssd-install-image.sh install --from FROM --tar TAR|- [--reuse] [--no-home] [--yes] [--dry-run]
#       check, then the root file system on /dev/mapper/ps3ssd: a new ext4 (label ps3root; asks
#       before replacing a file system that is there, e.g. Phase A's game copies, label ps3ssd), or
#       with --reuse the existing ps3root, its old contents moved into .old-<date>. TAR (the PC's
#       build/rootfs/debian-ppc64.tar; - reads it from stdin and needs --yes) is extracted with
#       owners, modes and xattrs, then: /etc/fstab (root /dev/mapper/ps3root, swap /dev/mapper/ps3swap
#       noauto for ps3-vswap, the USB root at /mnt/usbroot noauto, no USB swap), hostname and hosts,
#       SSH host keys, Bluetooth pairings, NetworkManager connections, /home/tibi (unless --no-home),
#       /var/tmp/bench and /var/tmp/ps3-bench's state (not its run directories), /var/lib/ps3-imgmap
#       (for reference), a oneshot unit that gives udev the initramfs's dm devices (ps3ssd-udev),
#       and /var/lib/ps3ssd/packages-from-usb.txt: pkg=version of every package installed here
#       that the new root lacks or has in another version (kernels apart), for apt after the first
#       boot. Writes only to /dev/mapper/ps3ssd (and the new root on it).
#   ssd-install-image.sh boot --from FROM [--dry-run]
#       check, then on the USB root: /boot/initrd-ssd-<release>.img and /etc/kboot.d/ssd.conf (the
#       entry "ssd-<release>": the release's kernel and arguments, PS3_EXTRA_ARGS, root=/dev/mapper/ps3root
#       rootfstype=ext4 ro, ps3ssd.image=<the map's image>), then ps3-update-kboot (needs ps3-boot with
#       /etc/kboot.d support). The USB entries are unchanged.
# --dry-run: every check, then the writes printed instead of done. Never touches /dev/ps3da, ps3db
# or ps3dc, the boot flag, Petitboot's settings or the fan.
# Host tests only (tools/ssdimg/vm-e2e.sh; sudo drops them on the PS3): PS3SSD_TEST_ROOT, a prefix for
# the USB root's files (and no check that / is the USB root), PS3SSD_STATE (/var/lib/ps3-imgmap),
# PS3SSD_UPDATE_KBOOT (ps3-update-kboot).
{
set -euo pipefail
export LC_ALL=C
R=${PS3SSD_TEST_ROOT:-}
STATE=${PS3SSD_STATE:-/var/lib/ps3-imgmap}
UPDATE_KBOOT=${PS3SSD_UPDATE_KBOOT:-/usr/sbin/ps3-update-kboot}
DEV=/dev/mapper/ps3ssd
SWAPDEV=/dev/mapper/ps3ssd-swap
MNT=$R/mnt/ps3root-new
case $(uname -m) in ppc64*) ARCH=ppc64 SUF= ;; x86_64) ARCH=amd64 SUF=-amd64 ;; *) ARCH=$(uname -m) SUF=-$ARCH ;; esac
USBROOT_UUID=50533301-02
die() { echo "!! ssd-install-image: $*" >&2; exit 1; }
say() { echo ">> $*"; }
DRY=0 YES=0 REUSE=0 HOME_COPY=1 FROM= TAR= MAP=
cmd=${1:-}; shift || true
while [ $# -gt 0 ]; do
	case $1 in
	--from) FROM=$2; shift ;;
	--tar) TAR=$2; shift ;;
	--map) MAP=$2; shift ;;
	--reuse) REUSE=1 ;;
	--no-home) HOME_COPY=0 ;;
	--yes) YES=1 ;;
	--dry-run) DRY=1 ;;
	*) die "unknown option $1 (see the top of $0)" ;;
	esac
	shift
done
do_() {	# do_ CMD...: run it, or print it with --dry-run
	if [ $DRY = 1 ]; then echo "[dry-run] $*"; else "$@"; fi
}
ask() {	# ask QUESTION: yes with --yes or a typed "yes" on the terminal (a dry run only says it would ask)
	[ $DRY = 1 ] && { echo "[dry-run] would ask: $1"; return 0; }
	[ $YES = 1 ] && return 0
	[ -t 0 ] || die "$1: not a terminal, --yes after the user's go"
	read -r -p ">> $1 Type yes: " a
	[ "$a" = yes ]
}
devnum() { printf '%d:%d' "0x$(stat -L -c %t "$1")" "0x$(stat -L -c %T "$1")"; }

# --- check: everything read-only ------------------------------------------------------------------
check() {
	[ "$(id -u)" = 0 ] || die "run as root"
	[ -n "$FROM" ] && [ -d "$FROM" ] || die "--from <copy of the PC's build/initrd>"
	local usb src
	if [ -z "$R" ]; then
		usb=$(readlink -f /dev/disk/by-partuuid/$USBROOT_UUID 2>/dev/null) || die "no USB root partition (PARTUUID $USBROOT_UUID)"
		src=$(readlink -f "$(findmnt -n -o SOURCE /)")
		[ "$src" = "$usb" ] || die "/ is $src, not the USB root $usb: run this from the USB root"
		say "running from the USB root ($usb)"
	fi
	[ -b $DEV ] && [ -b $SWAPDEV ] || die "no $DEV / $SWAPDEV: ps3-imgmap create <map> ps3ssd --swap-mib 1024 first (Phase A)"
	# the map, its records, the tables ps3-imgmap create wrote
	if [ -z "$MAP" ]; then
		MAP=$(ls $STATE/*.map.json 2>/dev/null | head -n 1)
		[ -n "$MAP" ] && [ "$(ls $STATE/*.map.json | wc -l)" = 1 ] || die "not exactly one map in $STATE: --map <map.json>"
	fi
	[ -e "$MAP" ] || die "no $MAP"
	SHA=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["extents_sha256"])' "$MAP")
	[ -e $STATE/verified-$SHA.json ] || die "no verify pass of $MAP"
	[ -e $STATE/created-$SHA.json ] || die "no created record of $MAP"
	local bid rb
	bid=$(cat /proc/sys/kernel/random/boot_id)
	rb=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("boot_id",""))' $STATE/rechecked-$SHA.json 2>/dev/null || true)
	[ "$rb" = "$bid" ] || die "no recheck of $MAP in this boot"
	local dbn
	dbn=$(devnum /dev/ps3db)
	for pair in "ps3ssd $DEV" "ps3ssd-swap $SWAPDEV"; do
		set -- $pair
		[ -e $STATE/$1.table ] || die "no $STATE/$1.table"
		[ "$(sed "s| /dev/ps3db | $dbn |" $STATE/$1.table)" = "$(dmsetup table $1)" ] ||
			die "$2's live table is not the one ps3-imgmap create wrote ($STATE/$1.table)"
	done
	say "$DEV and $SWAPDEV: the live tables are ps3-imgmap create's (map sha256 ${SHA:0:12}...)"
	# the initramfs: its manifest, its map, the same tables, the kernel here
	local imgs
	imgs=$(ls "$FROM"/initrd-ssd-*.img 2>/dev/null | grep -- "$SUF\.img\$" | { [ -n "$SUF" ] && cat || grep -v -- '-amd64\.img$'; } || true)
	[ -n "$imgs" ] && [ "$(echo "$imgs" | wc -l)" = 1 ] || die "want one initrd-ssd-<release>$SUF.img in $FROM, found: $(echo $imgs)"
	IMG=$imgs
	[ -e "$IMG.manifest" ] || die "no $IMG.manifest"
	[ "$(sed -n 's/^arch=//p' "$IMG.manifest")" = $ARCH ] || die "$IMG is not for $ARCH"
	REL=$(sed -n 's/^release=//p' "$IMG.manifest")
	[ "$(sed -n 's/^image_sha256=//p' "$IMG.manifest")" = "$(sha256sum "$IMG" | cut -d' ' -f1)" ] ||
		die "$IMG doesn't match its manifest"
	local name bm tool
	name=$(basename "$MAP" .map.json)
	bm=$FROM/maps/$name.map
	tool=$FROM/ps3-imgmap-boot.$ARCH
	[ -e "$bm" ] || die "no $bm"
	grep -qx "$(sha256sum "$bm" | cut -d' ' -f1)  ./ps3ssd/maps/$name.map" "$IMG.manifest" ||
		die "$bm is not the map inside $IMG"
	[ -x "$tool" ] || chmod 755 "$tool" 2>/dev/null || true
	"$tool" check "$bm" >/dev/null || die "the boot map is refused by ps3-imgmap-boot"
	grep -qx "extents_sha256 $SHA" "$bm" || die "the boot map is of another map than $MAP"
	"$tool" tables "$bm" root | cmp -s - $STATE/ps3ssd.table || die "the boot map's root table differs from ps3ssd's"
	"$tool" tables "$bm" swap | cmp -s - $STATE/ps3ssd-swap.table || die "the boot map's swap table differs from ps3ssd-swap's"
	IMAGE=$(sed -n 's/^image //p' "$bm")
	say "the initramfs ($(basename "$IMG"), $(stat -c %s "$IMG") bytes) makes the same two devices from $IMAGE"
	[ -e $R/boot/vmlinux-$REL ] || die "no /boot/vmlinux-$REL on the USB root"
	[ -e $R/usr/lib/ps3/boot/$REL.args ] || die "no /usr/lib/ps3/boot/$REL.args"
	while read -r _ path sum; do
		[ -n "$path" ] || continue
		[ "$(sha256sum "$R/$path" 2>/dev/null | cut -d' ' -f1)" = "$sum" ] || die "/$path is not the initramfs's module source"
	done <<< "$(grep '^module_src ' "$IMG.manifest" || true)"
	say "kernel $REL here, its ufs and dm-mod are the initramfs's"
}

# --- install ----------------------------------------------------------------------------------------
copy_tree() {	# copy_tree SRC DSTDIR [tar options]: SRC (a path) into DSTDIR with owners, modes, xattrs
	local src=$1 dst=$2; shift 2
	[ -e "$src" ] || return 0
	do_ mkdir -p "$dst"
	if [ $DRY = 1 ]; then echo "[dry-run] copy $src -> $dst/ ($(du -sh "$src" 2>/dev/null | cut -f1))"; return 0; fi
	tar -C "$(dirname "$src")" --xattrs --xattrs-include='*' "$@" -cf - "$(basename "$src")" |
		tar -C "$dst" -xp --numeric-owner --xattrs --xattrs-include='*' -f -
	say "copied $src ($(du -sh "$src" 2>/dev/null | cut -f1))"
}

write() {	# write FILE: stdin into FILE (printed with --dry-run)
	if [ $DRY = 1 ]; then echo "[dry-run] write $1:"; sed 's/^/    /'; else mkdir -p "$(dirname "$1")"; cat > "$1"; fi
}

MOUNTED=0
cleanup() {	# on any failure after the mount: the new root unmounted again (synced), never left mounted
	if [ $MOUNTED = 1 ] && findmnt -M "$MNT" >/dev/null 2>&1; then
		sync; umount "$MNT" && echo "!! ssd-install-image: stopped; $DEV unmounted again" >&2
	fi
}
trap cleanup EXIT

install_root() {
	[ -n "$TAR" ] || die "--tar <debian-ppc64.tar> (or - for stdin, with --yes)"
	[ "$TAR" = - ] && [ $YES = 0 ] && [ $DRY = 0 ] && die "--tar - reads the image from stdin: no questions possible, --yes after the user's go"
	[ "$TAR" = - ] || [ -s "$TAR" ] || die "no $TAR"
	# the devices must be free: nothing mounted, no swap, not under the VRAM swap's backing
	findmnt -S $DEV >/dev/null && die "$DEV is mounted ($(findmnt -n -o TARGET -S $DEV | xargs)): ssd-arm.sh reset; umount it"
	grep -q "^$(readlink -f $SWAPDEV) \|^$SWAPDEV " /proc/swaps && die "$SWAPDEV is in use as swap"
	if dmsetup table ps3vswap-back 2>/dev/null | grep -q " $(devnum $SWAPDEV) "; then
		die "the VRAM swap's spill is on $SWAPDEV (SWAP=ssd arm): ssd-arm.sh reset"
	fi
	local fs label
	fs=$(blkid -o value -s TYPE $DEV 2>/dev/null || true)
	label=$(blkid -o value -s LABEL $DEV 2>/dev/null || true)
	say "$DEV: $(( $(blockdev --getsz $DEV) / 2048 )) MiB, now ${fs:-no file system}${label:+ (label $label)}"
	do_ mkdir -p $MNT
	if [ $REUSE = 1 ]; then
		[ "$fs" = ext4 ] && [ "$label" = ps3root ] || die "--reuse wants an ext4 labelled ps3root on $DEV"
		do_ mount -o noatime $DEV $MNT
		[ $DRY = 1 ] || MOUNTED=1
		local old=.old-$(date +%Y%m%d-%H%M%S)
		say "the previous installation goes to $MNT/$old"
		if [ $DRY = 0 ]; then
			mkdir "$MNT/$old"
			for f in "$MNT"/* "$MNT"/.[!.]*; do
				case "${f#"$MNT"/}" in .old-*|lost+found|'*'|'.[!.]*') continue ;; esac
				mv "$f" "$MNT/$old/"
			done
		fi
	else
		if [ -n "$fs" ]; then
			local what="$fs${label:+, label $label}"
			[ "$label" = ps3ssd ] && what="$what: Phase A's game copies"
			ask "$DEV holds a file system ($what): erase it for the new root?" || die "nothing done"
		fi
		# the inode tables and journal written now, not by ext4lazyinit in the first SSD boots
		do_ mkfs.ext4 -q -F -L ps3root -E lazy_itable_init=0,lazy_journal_init=0,nodiscard $DEV
		do_ mount -o noatime $DEV $MNT
		[ $DRY = 1 ] || MOUNTED=1
	fi
	say "extracting ${TAR/#-/the image from stdin} (owners, modes, xattrs)"
	if [ $DRY = 1 ]; then echo "[dry-run] tar -xp --numeric-owner --xattrs --xattrs-include='*' -C $MNT -f $TAR"
	else
		tar -xp --numeric-owner --xattrs --xattrs-include='*' -C $MNT -f "$TAR"
		[ -e $MNT/usr/lib/modules/$REL ] || die "the image has no kernel modules for $REL"
		[ -e $MNT/sbin/init ] || [ -L $MNT/sbin/init ] || die "the image has no /sbin/init"
	fi

	say "fixing up the copy"
	write $MNT/etc/fstab <<EOF
# Root on the SSD image (dev_hdd0:$IMAGE), made by the initramfs (ssd-install-image.sh, docs/GAMEOS-HDD.md
# "Phase B"). It mounts root read-only for systemd's fsck, which then remounts it read-write.
/dev/mapper/ps3root   /             ext4   defaults,noatime         0 1
# The VRAM swap's backing (ps3-vswap takes the first swap line; noauto: it activates it)
/dev/mapper/ps3swap   none          swap   sw,pri=200,noauto        0 0
tmpfs                 /tmp          tmpfs  defaults,size=64M        0 0
# The USB root, by hand only (sudo mount /mnt/usbroot)
PARTUUID=$USBROOT_UUID  /mnt/usbroot  ext4   noauto,noatime,nofail    0 0
EOF
	do_ mkdir -p $MNT/mnt/usbroot
	for f in /etc/hostname /etc/hosts; do do_ cp -a $R$f $MNT$f; done
	for p in $R/etc/ssh/ssh_host_* $R/var/lib/bluetooth $R/etc/NetworkManager/system-connections; do
		[ -e "$p" ] || continue
		p=${p#"$R"}
		do_ mkdir -p "$MNT$(dirname "$p")"
		do_ cp -a "$R$p" "$MNT$(dirname "$p")/"
	done
	say "SSH host keys, Bluetooth pairings, Wi-Fi connections copied"
	# /etc/default/ps3-vswap is not copied: PS3_VSWAP_DM is Phase A's swap arm; the SSD root backs the
	# VRAM swap with /dev/mapper/ps3swap directly (fstab)
	if [ $HOME_COPY = 1 ]; then copy_tree $R/home/tibi $MNT/home; fi
	copy_tree $R/var/tmp/bench $MNT/var/tmp
	if [ -d $R/var/tmp/ps3-bench ]; then	# the bench's state, not its run directories (<stamp>-<game>-<variant>)
		copy_tree $R/var/tmp/ps3-bench $MNT/var/tmp --exclude='ps3-bench/[0-9][0-9][0-9][0-9]*-*'
	fi
	if [ $DRY = 1 ]; then echo "[dry-run] copy $STATE -> /var/lib/ps3-imgmap"
	else mkdir -p $MNT/var/lib/ps3-imgmap && tar -C $STATE -cf - . | tar -C $MNT/var/lib/ps3-imgmap -xp --numeric-owner -f -; fi
	write $MNT/etc/systemd/system/ps3ssd-udev.service <<'EOF'
[Unit]
Description=udev learns the SSD root's device-mapper devices (made in the initramfs without udev)
Documentation=file:///run/ps3ssd/init.log
After=systemd-udev-trigger.service
DefaultDependencies=no
ConditionPathExists=/run/ps3ssd/init.log

[Service]
Type=oneshot
RemainAfterExit=yes
# a "change" event per dm device: 55-dm.rules then sets their names and links (/dev/mapper/*,
# /dev/disk/by-*) and systemd sees them ready, as if udev had watched them being made
ExecStart=/usr/bin/udevadm trigger --action=change --subsystem-match=block --sysname-match=dm-*
ExecStart=/usr/bin/udevadm settle --timeout=10

[Install]
WantedBy=sysinit.target
EOF
	do_ mkdir -p $MNT/etc/systemd/system/sysinit.target.wants
	do_ ln -sf ../ps3ssd-udev.service $MNT/etc/systemd/system/sysinit.target.wants/ps3ssd-udev.service
	# what apt needs to give the new root this root's packages (versions), kernels apart
	do_ mkdir -p $MNT/var/lib/ps3ssd
	if ! command -v dpkg-query >/dev/null; then
		say "no dpkg-query: no package list"
	elif [ $DRY = 0 ]; then
		dpkg-query -W -f '${db:Status-Abbrev} ${Package}:${Architecture}=${Version}\n' | awk '$1 ~ /^ii/ {print $2}' | sort > /tmp/ps3ssd-usb.pkgs
		dpkg-query --admindir=$MNT/var/lib/dpkg -W -f '${db:Status-Abbrev} ${Package}:${Architecture}=${Version}\n' |
			awk '$1 ~ /^ii/ {print $2}' | sort > /tmp/ps3ssd-new.pkgs
		comm -23 /tmp/ps3ssd-usb.pkgs /tmp/ps3ssd-new.pkgs | grep -Ev '^(linux-(image|headers|kbuild)|ps3rsx-modules)' \
			> $MNT/var/lib/ps3ssd/packages-from-usb.txt || true
		comm -13 /tmp/ps3ssd-usb.pkgs /tmp/ps3ssd-new.pkgs > $MNT/var/lib/ps3ssd/packages-only-new.txt || true
		say "$(wc -l < $MNT/var/lib/ps3ssd/packages-from-usb.txt) packages to align after the first boot" \
			"(/var/lib/ps3ssd/packages-from-usb.txt), $(wc -l < $MNT/var/lib/ps3ssd/packages-only-new.txt) only in the new root"
		rm -f /tmp/ps3ssd-usb.pkgs /tmp/ps3ssd-new.pkgs
	else
		echo "[dry-run] /var/lib/ps3ssd/packages-from-usb.txt: dpkg-query here against the new root's"
	fi
	do_ sync
	do_ umount $MNT
	MOUNTED=0
	say "the SSD root is installed; next: ssd-install-image.sh boot --from $FROM, then the user boots ssd-$REL"
}

# --- boot: the initramfs and the entry on the USB root ----------------------------------------------
boot_entry() {
	grep -q 'kboot.d' "$UPDATE_KBOOT" || die "this ps3-boot has no /etc/kboot.d: install the new ps3-boot first"
	[ -r $R/etc/default/ps3-boot ] && . $R/etc/default/ps3-boot
	local dst=/boot/initrd-ssd-$REL.img args
	args=$(cat $R/usr/lib/ps3/boot/$REL.args)
	do_ install -m 644 "$IMG" $R$dst
	[ $DRY = 1 ] || [ "$(sha256sum $R$dst | cut -d' ' -f1)" = "$(sha256sum "$IMG" | cut -d' ' -f1)" ] || die "$dst: copy differs"
	do_ mkdir -p $R/etc/kboot.d
	write $R/etc/kboot.d/ssd.conf <<EOF
# The SSD root (docs/GAMEOS-HDD.md "Phase B"; ssd-install-image.sh boot, $(date -u +%Y-%m-%d)): kernel and
# initramfs from this USB disk, root from dev_hdd0:$IMAGE through the initramfs's dm devices.
ssd-$REL='/boot/vmlinux-$REL initrd=$dst root=/dev/mapper/ps3root rootfstype=ext4 ro ps3ssd.image=$IMAGE ${PS3_EXTRA_ARGS:-} $args'
EOF
	do_ env PS3_BOOT_ROOT="$R" sh "$UPDATE_KBOOT"
	[ $DRY = 1 ] || { say "/etc/kboot.conf:"; sed 's/^/    /' $R/etc/kboot.conf; }
	[ $DRY = 1 ] || grep -q "^ssd-$REL=" $R/etc/kboot.conf || die "ps3-update-kboot left the entry out (see above)"
	say "in Petitboot: the entry ssd-$REL (the USB entries are unchanged)"
}

case $cmd in
check) check ;;
install) check; install_root ;;
boot) check; boot_entry ;;
*) die "usage: $0 check|install|boot --from DIR [...] (see the top of the script)" ;;
esac
exit 0
}
