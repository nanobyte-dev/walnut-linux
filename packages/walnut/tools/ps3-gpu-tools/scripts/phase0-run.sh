#!/bin/sh
# Runs Phase 0 probe tests on the PS3, one insmod each, logging to ~/phase0.log (on the NFS
# root, so the log survives a wedge or crash). After every test it checks that ps3fb's own
# channel still runs (rsx-probe ref) and stops at the first wedge.
#   phase0-run.sh test[:n] [test[:n] ...]      e.g. phase0-run.sh ctxcount:1 iomap
L=/home/tibi/phase0.log
for arg in "$@"; do
	t=${arg%%:*}; n=${arg#*:}; [ "$n" = "$arg" ] && n=8
	echo "=== $arg $(date +%T)" >>"$L"
	dmesg -C
	if [ "$t" = flip ]; then
		/usr/lib/ps3/bin/rsx-hold 8 &
		sleep 1
	fi
	modprobe rsx_ctx_probe2 test="$t" n="$n"
	rmmod rsx_ctx_probe2
	wait
	sleep 1
	dmesg | grep -v fb_blit >>"$L"
	if timeout 10 /usr/lib/ps3/bin/rsx-probe ref >>"$L" 2>&1; then
		echo "--- GPU alive after $arg" >>"$L"
	else
		echo "--- GPU WEDGED after $arg, stopping" >>"$L"
		sync
		exit 1
	fi
	sync
done
