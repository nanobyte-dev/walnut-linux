#!/bin/bash
# ssd-arm.sh: the SSD arms of the bench (docs/GAMEOS-HDD.md "Test drive, Phase A"), as tibi with sudo -n.
# run.sh calls `set` before every run (before ps3-bench.sh's quiet, which then drops the caches and
# empties the swaps as always) and `reset` at its end; by hand: copy, status, reset.
#   ssd-arm.sh set GAME VARIANT GAMEDATA SWAP   GAMEDATA, SWAP: usb or ssd. Puts this run's arm in place:
#        GAMEDATA=ssd bind-mounts the SSD copy $SSD/<dir> over each of the recipe's DATA_DIRS
#        (read-write, as the originals: the copy keeps their owners), usb unmounts every bind this
#        script made; SWAP=ssd points the VRAM swap's spill (its backing, the device-mapper device
#        ps3vswap-back that ps3-vswap makes with PS3_VSWAP_DM=1) at the SSD's swap device
#        ($SSD_SWAP, ps3-imgmap create --swap-mib), usb back at the USB partition: VRAM swap off
#        (its pages come back), the table swapped (dmsetup suspend/load/resume), mkswap, on again
#        at its priority. Nothing to do: nothing done (no sudo). The VRAM swap (300) and zram (100)
#        keep their priorities; there is no third swap.
#   ssd-arm.sh reset                            both back to usb
#   ssd-arm.sh copy GAME [VARIANT]              the recipe's DATA_DIRS copied to $SSD (cp -a as root:
#        owners and modes kept), then compared file by file (diff -rq: same names, same contents); a
#        copy already there is only compared
#   ssd-arm.sh status                           binds, the swap arm, /proc/swaps
# State in /dev/shm (gone with the boot, as the binds and the table are): ps3-ssdarm.binds (the
# binds made), ps3-ssdarm.swap (ssd or usb), ps3-ssdarm.usb-table (the boot's table, put back for usb).
# Environment: BENCH_SSD (default /mnt/ps3ssd/games; must be on BENCH_SSD_DEV's mount,
# /dev/mapper/ps3ssd), BENCH_SSD_SWAP (/dev/mapper/ps3ssd-swap), BENCH_SSDARM_STATE (/dev/shm),
# BENCH_RECIPES; SSDARM_DRY=1 prints the root commands to stderr instead of running them (selftest.py).
{
set -u
here=$(cd "$(dirname "$0")" && pwd)
SSD=${BENCH_SSD:-/mnt/ps3ssd/games}
SSD_DEV=${BENCH_SSD_DEV:-/dev/mapper/ps3ssd}
SSD_SWAP=${BENCH_SSD_SWAP:-/dev/mapper/ps3ssd-swap}
ST=${BENCH_SSDARM_STATE:-/dev/shm}
BINDS=$ST/ps3-ssdarm.binds SWAPST=$ST/ps3-ssdarm.swap USBTAB=$ST/ps3-ssdarm.usb-table
VS=ps3vswap-back VSDEV=/dev/ps3vswap0
die() { echo "!! ssd-arm: $*"; exit 1; }
root() {	# root CMD...: as root (sudo -n), or printed with SSDARM_DRY=1
	if [ "${SSDARM_DRY:-0}" = 1 ]; then echo "root: $*" >&2; return 0; fi
	sudo -n "$@"
}
kb() { awk -v k="$1:" '$1 == k { print $2 }' /proc/meminfo; }

data_dirs() {	# data_dirs GAME VARIANT: the recipe's DATA_DIRS, one a line
	(CMD=(); ENV=(); BACKUP=(); DATA_DIRS=(); ASSETS=$here/assets
	 . "${BENCH_RECIPES:-$here/recipes}/$1.sh" || exit 1
	 declare -F "variant_$2" >/dev/null && "variant_$2"
	 for d in "${DATA_DIRS[@]}"; do printf '%s\n' "$d"; done)
}

ssd_ready() {	# the SSD's file system mounted where $SSD is
	[ "${SSDARM_DRY:-0}" = 1 ] && return 0
	local s
	s=$(findmnt -n -o SOURCE --target "$SSD" 2>/dev/null)
	[ "$s" = "$SSD_DEV" ] || [ "$(readlink -f "$s" 2>/dev/null)" = "$(readlink -f "$SSD_DEV" 2>/dev/null)" ] ||
		die "$SSD is not on $SSD_DEV (got '${s:-nothing}'): ps3-imgmap create, then mount it"
}

unbind_all() {	# every bind this script made, except those in $1 (newline list)
	local d keep=${1:-} left=()
	[ -s "$BINDS" ] || return 0
	while IFS= read -r d; do
		[ -n "$d" ] || continue
		if [ -n "$keep" ] && grep -qxF -- "$d" <<< "$keep"; then left+=("$d"); continue; fi
		if [ "${SSDARM_DRY:-0}" = 1 ] || mountpoint -q -- "$d"; then
			root umount -- "$d" || die "umount $d failed (a game still running?)"
		fi
		echo ">> GAMEDATA: $d back on the USB disk"
	done < "$BINDS"
	if [ ${#left[@]} -gt 0 ]; then printf '%s\n' "${left[@]}" > "$BINDS"; else rm -f "$BINDS"; fi
}

bind_game() {	# bind_game GAME VARIANT
	local dirs d s
	dirs=$(data_dirs "$1" "$2") || die "recipe $1 failed to source"
	[ -n "$dirs" ] || die "$1 has no DATA_DIRS in its recipe: nothing to put on the SSD"
	ssd_ready
	unbind_all "$dirs"
	while IFS= read -r d; do
		s=$SSD$d
		[ -d "$s" ] || [ "${SSDARM_DRY:-0}" = 1 ] || die "no SSD copy $s: ssd-arm.sh copy $1 $2"
		grep -qxF -- "$d" "$BINDS" 2>/dev/null && continue	# already bound
		if [ "${SSDARM_DRY:-0}" != 1 ] && mountpoint -q -- "$d"; then die "$d is a mount point already (not ours)"; fi
		root mount --bind -- "$s" "$d" || die "mount --bind $s $d failed"
		printf '%s\n' "$d" >> "$BINDS"
		echo ">> GAMEDATA: $d from $s"
	done <<< "$dirs"
}

swap_now() { cat "$SWAPST" 2>/dev/null || echo usb; }

swap_to() {	# swap_to usb|ssd: the VRAM swap's backing table pointed at the USB partition or the SSD
	local want=$1 cur tab size prio used avail target
	[ "$(swap_now)" = "$want" ] && return 0
	if [ "${SSDARM_DRY:-0}" != 1 ]; then
		[ "$(cat /sys/block/ps3vswap0/backing 2>/dev/null)" = "/dev/mapper/$VS" ] ||
			die "the VRAM swap's backing is '$(cat /sys/block/ps3vswap0/backing 2>/dev/null)', not /dev/mapper/$VS: PS3_VSWAP_DM=1 in /etc/default/ps3-vswap and a reboot (docs/GAMEOS-HDD.md)"
		tab=$(sudo -n dmsetup table "$VS") || die "dmsetup table $VS failed"
	else
		tab="0 2097152 linear 8:3 0"
	fi
	[ -s "$USBTAB" ] || printf '%s\n' "$tab" > "$USBTAB"	# the boot's table: the USB partition
	size=$(awk '{ print $2; exit }' "$USBTAB")
	if [ "$want" = ssd ]; then
		target=$SSD_SWAP
		if [ "${SSDARM_DRY:-0}" != 1 ]; then
			[ -b "$target" ] || die "no $target: ps3-imgmap create MAP ps3ssd --swap-mib 1024"
			[ "$(sudo -n blockdev --getsz "$target")" -ge "$size" ] || die "$target is smaller than the VRAM swap ($size sectors)"
		fi
		newtab="0 $size linear $target 0"
	else
		newtab=$(cat "$USBTAB")
	fi
	prio=$(awk -v d=$VSDEV '$1 == d { print $5 }' /proc/swaps); prio=${prio:-300}
	if [ "${SSDARM_DRY:-0}" != 1 ] && grep -q "^$VSDEV " /proc/swaps; then
		used=$(awk -v d=$VSDEV '$1 == d { print $4 }' /proc/swaps); avail=$(kb MemAvailable)
		[ "$used" -lt $(( avail - 49152 )) ] || die "swapoff $VSDEV: $used kB to bring back, $avail kB available"
	fi
	echo ">> SWAP: VRAM swap's spill to ${want^^} ($newtab)"
	if [ "${SSDARM_DRY:-0}" = 1 ] || grep -q "^$VSDEV " /proc/swaps; then
		root timeout -s INT 180 swapoff $VSDEV || die "swapoff $VSDEV failed"
	fi
	if ! { root dmsetup suspend $VS && root dmsetup load $VS --table "$newtab" && root dmsetup resume $VS; }; then
		root dmsetup resume $VS; root swapon -p "$prio" --discard=pages $VSDEV
		die "dmsetup table switch failed: VRAM swap left on its old table"
	fi
	root mkswap $VSDEV > /dev/null && root swapon -p "$prio" --discard=pages $VSDEV ||
		die "mkswap/swapon $VSDEV failed: no VRAM swap now (ssd-arm.sh reset, or ps3-bench.sh desktop)"
	echo "$want" > "$SWAPST"
}

case ${1:-} in
set)
	g=${2:?game}; v=${3:?variant}; gd=${4:-usb}; sw=${5:-usb}
	case $gd in usb|ssd) ;; *) die "GAMEDATA=$gd: usb or ssd" ;; esac
	case $sw in usb|ssd) ;; *) die "SWAP=$sw: usb or ssd" ;; esac
	if [ "$gd" = ssd ]; then bind_game "$g" "$v"; else unbind_all; fi
	swap_to "$sw"
	;;
reset) unbind_all; swap_to usb ;;
copy)
	g=${2:?game}; v=${3:-base}
	dirs=$(data_dirs "$g" "$v") || die "recipe $g failed to source"
	[ -n "$dirs" ] || die "$g has no DATA_DIRS"
	ssd_ready
	while IFS= read -r d; do
		grep -qxF -- "$d" "$BINDS" 2>/dev/null && die "$d is bound to the SSD now: ssd-arm.sh reset first"
		if root test -e "$SSD$d"; then
			root diff -rq --no-dereference -- "$d" "$SSD$d" > /dev/null 2>&1 &&
				{ echo ">> $SSD$d: there already, same files and contents"; continue; }
			[ "${SSDARM_DRY:-0}" = 1 ] ||
				die "$SSD$d exists and differs from $d: remove it (sudo rm -rf '$SSD$d') and copy again"
		fi
		echo ">> copy $d -> $SSD$d ($(du -sh -- "$d" 2>/dev/null | cut -f1))"
		root mkdir -p -- "$(dirname -- "$SSD$d")" && root cp -a -- "$d" "$SSD$d" || die "copy of $d failed"
		root diff -rq --no-dereference -- "$d" "$SSD$d" || die "$SSD$d differs from $d after the copy"
		echo ">> $SSD$d: same files, same contents"
	done <<< "$dirs"
	;;
status)
	echo "SSD copies: $SSD ($(findmnt -n -o SOURCE --target "$SSD" 2>/dev/null || echo 'not mounted'))"
	echo "GAMEDATA binds: $(tr '\n' ' ' < "$BINDS" 2>/dev/null)"
	echo "SWAP: $(swap_now) (backing $(cat /sys/block/ps3vswap0/backing 2>/dev/null || echo -))"
	cat /proc/swaps
	;;
*) sed -n '2,/^[^#]/s/^# \{0,1\}//p' "$0"; exit 2 ;;
esac
exit 0
}
