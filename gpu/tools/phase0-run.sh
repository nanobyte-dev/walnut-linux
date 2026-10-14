#!/bin/sh
# Runs Phase 0 probe tests on the PS3, one insmod each, logging to ~/phase0.log (on the NFS
# root, so the log survives a wedge or crash).   phase0-run.sh test [test ...]
L=/home/tibi/phase0.log
for t in "$@"; do
	echo "=== $t $(date +%T)" >>"$L"
	dmesg -C
	if [ "$t" = flip ]; then
		/home/tibi/bin/rsx-hold 8 &
		sleep 1
	fi
	insmod /home/tibi/bin/rsx_ctx_probe2.ko test="$t"
	rmmod rsx_ctx_probe2
	dmesg | grep -v fb_blit >>"$L"
	sync
	wait
done
