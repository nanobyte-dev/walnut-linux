#!/bin/bash
# rsx-arm.sh: switches the PS3 between ps3rsx builds within one boot, for same-boot A/B runs (run as
# tibi on the PS3; uses sudo). The modules come from arms-prep.sh on the PC, copied to
# /var/tmp/rsx-ab/arms/{A,B,C}/ps3rsx.ko with arms.txt.
#   rsx-arm.sh ARM [PARAM=VALUE...]   load arm A, B or C (C: the test-switch build) with those module
#                                     parameters, e.g. rsx-arm.sh C test_clear_off=1
#   rsx-arm.sh unload                 the desktop stopped, the VRAM swap off, ps3rsx unloaded
#   rsx-arm.sh status                 the loaded arm (by the module file's sha256) and its parameters
# A switch: `ps3-bench.sh quiet` (desktop stopped, caches dropped, swaps emptied), the VRAM swap off
# (swapoff, its pages come back to RAM first: refused when they wouldn't fit), rmmod, the arm's file
# put where the package keeps it (/lib/modules/<release>/updates/ps3rsx.ko; the package's own file
# comes back with `sudo apt-get install --reinstall ps3rsx-modules-$(uname -r)` or a dpkg -i), modprobe,
# then the VRAM swap back (udev's 71-ps3-vswap.rules restarts ps3-vswap.service for the new device).
# Every switch is logged with a UTC stamp to /var/tmp/ps3-bench/rsx-arms.log, which abba-report.py
# uses to tell the runs' arms apart.
# Refused after a GPU recovery or in display-only mode in this boot: a module reload after a recovery
# is the untested path behind the 3-beep shutdown (docs/RSX.md), so the boot ends there instead.
set -euo pipefail
D=/var/tmp/rsx-ab/arms
LOG=/var/tmp/ps3-bench/rsx-arms.log
REL=$(uname -r)
KO=/lib/modules/$REL/updates/ps3rsx.ko
U=/sys/devices/platform/ps3rsx
die() { echo "rsx-arm: $*" >&2; exit 1; }

# the sysfs counters start again at each load, the kernel log doesn't
recovered() {
	[ "$(cat $U/recoveries 2>/dev/null || echo 0)" != 0 ] || [ "$(cat $U/recovery_failed 2>/dev/null || echo 0)" != 0 ] ||
		[ "$(cat $U/display_only 2>/dev/null || echo 0)" != 0 ] ||
		sudo dmesg | grep -q -e 'graph exception' -e 'in-place recovery' -e 'GPU stopped'
}

which_arm() {
	local s a
	s=$(sha256sum "$KO" | cut -c1-16)
	for a in A B C; do
		[ -e "$D/$a/ps3rsx.ko" ] && [ "$(sha256sum "$D/$a/ps3rsx.ko" | cut -c1-16)" = "$s" ] && { echo "$a"; return; }
	done
	echo "package ($s)"
}

unload() {
	recovered && die "a GPU recovery (or display-only mode) in this boot: no module reload; reboot first"
	ps3-bench.sh quiet
	if grep -q '^/dev/ps3vswap0 ' /proc/swaps; then
		local used avail
		used=$(awk '$1 == "/dev/ps3vswap0" { print $4 }' /proc/swaps)
		avail=$(awk '/^MemAvailable:/ { print $2 }' /proc/meminfo)
		[ "$used" -lt $(( avail - 49152 )) ] || die "swapoff ps3vswap0: $used kB to bring back, $avail kB available"
		sudo timeout -s INT 180 swapoff /dev/ps3vswap0 || die "swapoff /dev/ps3vswap0 failed"
	fi
	if lsmod | grep -q '^ps3rsx '; then
		sudo rmmod ps3rsx || die "rmmod ps3rsx failed (something still has the device open: lsof /dev/dri/*)"
	fi
}

case ${1:-} in
unload)
	unload
	;;
status)
	echo "arm: $(which_arm); loaded: $(lsmod | grep -q '^ps3rsx ' && echo yes || echo no)"
	for f in /sys/module/ps3rsx/parameters/*; do [ -r "$f" ] && echo "  ${f##*/}=$(cat "$f")"; done 2>/dev/null | grep -E 'test_clear_off|vswap_vram_mb|ctxsw|dry_run' || true
	;;
A|B|C)
	arm=$1; shift
	[ -e "$D/$arm/ps3rsx.ko" ] || die "no $D/$arm/ps3rsx.ko (arms-prep.sh on the PC, then copy)"
	grep -q "^$arm\s.*\s$REL\$" "$D/arms.txt" || die "arm $arm isn't built for the running kernel $REL ($D/arms.txt)"
	unload
	sudo install -m 644 "$D/$arm/ps3rsx.ko" "$KO"
	sudo modprobe ps3rsx "$@" || die "modprobe ps3rsx $* failed"
	udevadm settle
	for _ in $(seq 30); do grep -q '^/dev/ps3vswap0 ' /proc/swaps && break; sleep 1; done
	grep -q '^/dev/ps3vswap0 ' /proc/swaps || echo "rsx-arm: the VRAM swap isn't back on (systemctl status ps3-vswap)" >&2
	[ "$(which_arm)" = "$arm" ] || die "the module file isn't arm $arm's"
	mkdir -p "${LOG%/*}"
	printf '%s\t%s\t%s\t%s\n' "$(date -u +%Y%m%dT%H%M%SZ)" "$arm" "$(grep "^$arm\s" "$D/arms.txt" | cut -f3)" "$*" >> "$LOG"
	echo "rsx-arm: arm $arm loaded ($(grep "^$arm\s" "$D/arms.txt" | cut -f3)) $*"
	;;
*)
	sed -n '2,8p' "$0" >&2
	exit 2
	;;
esac
