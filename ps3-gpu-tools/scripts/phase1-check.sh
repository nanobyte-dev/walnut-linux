#!/bin/sh
# Phase 1 checks after booting the new kernel (docs/DRIVER-PLAN.md). Run as root on the PS3.
B=/usr/lib/ps3/bin
echo "== kernel: $(uname -r), cmdline: $(cat /proc/cmdline | grep -o 'ps3rsx_gart=[^ ]*')"
dmesg | grep -E "ps3rsx gart|ps3fb videomemory" | sed 's/^/   /'
echo "== swap / zram / drm"
swapon --show
modprobe drm && echo "   drm module loads"
echo "== existing tools still draw"
$B/rsx-probe ref | grep ref || echo "   rsx-probe FAILED"
echo "== takeover"
dmesg -C
modprobe ps3fb_share_test || { echo "   insmod FAILED"; dmesg; exit 1; }
dmesg | sed 's/^/   /'
if $B/rsx-probe ref >/dev/null 2>&1; then echo "   BAD: direct GPU access worked while owned"; else echo "   ok: direct GPU access refused while owned"; fi
sleep 2
rmmod ps3fb_share_test
dmesg | grep release | sed 's/^/   /'
echo "== after release"
$B/rsx-probe ref | grep ref || echo "   rsx-probe FAILED after release"
