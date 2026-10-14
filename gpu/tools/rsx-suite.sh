#!/bin/bash
# Full test run after a fresh boot (run as tibi; uses sudo for the module). Logs to ~/suite.log
# on the NFS root, so a wedge or crash still leaves the record. Stops at the first failure.
#   rsx-suite.sh [smoke|mesa|multi|all]   (default all)
L=$HOME/suite.log
B=$HOME/bin
what=${1:-all}
exec > >(tee -a "$L") 2>&1
echo "=== $(date +%T) rsx-suite $what"
sudo dmesg -n 7
sudo modprobe drm_kms_helper || exit 1
# always a fresh load with default parameters (trace=1 floods netconsole and hangs the box)
lsmod | grep -q ps3rsx && sudo rmmod ps3rsx
sudo insmod $B/ps3rsx.ko || exit 1

run() { echo "--- $*"; timeout 300 "$@"; local rc=$?; [ $rc = 0 ] || { echo "!!! FAILED ($rc): $*"; sudo dmesg | tail -20; exit 1; }; }

if [ "$what" = smoke ] || [ "$what" = all ]; then
	for t in reject nop m2mf clear tri fence twochan; do run $B/rsxdrm-smoke $t; done
fi
if [ "$what" = mesa ] || [ "$what" = all ]; then
	. $HOME/mesa/env.sh
	for t in egl-clear egl-tri egl-tex egl-gears; do
		echo "--- $t (softpipe reference)"
		LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=softpipe timeout 600 $t /tmp/$t-ref.ppm | grep -E "RENDERER|checksum|PASS|FAIL"
		echo "--- $t (RSX)"
		out=$(timeout 600 $t /tmp/$t-nv.ppm 2>&1); rc=$?
		echo "$out" | grep -vE "^MESA-EGL"
		# Mesa silently falls back to softpipe when the nouveau screen fails: that's a failure
		echo "$out" | grep -q "RENDERER: .*NV" || { echo "!!! not on the RSX"; exit 1; }
		[ $rc = 0 ] || { echo "!!! FAILED ($rc): $t"; sudo dmesg | tail -20; exit 1; }
		cmp -s /tmp/$t-ref.ppm /tmp/$t-nv.ppm && echo "    identical to softpipe" || echo "    differs from softpipe (compare /tmp/$t-*.ppm)"
	done
fi
if [ "$what" = multi ] || [ "$what" = all ]; then
	. $HOME/mesa/env.sh
	echo "--- two GL processes at once"
	egl-gears /tmp/g1.ppm > /tmp/g1.out 2>&1 & egl-tri /tmp/t2.ppm > /tmp/t2.out 2>&1 & egl-gears /tmp/g3.ppm > /tmp/g3.out 2>&1
	wait
	grep -h checksum /tmp/g1.out /tmp/t2.out /tmp/g3.out
	cmp -s /tmp/g1.ppm /tmp/egl-gears-nv.ppm && cmp -s /tmp/g3.ppm /tmp/egl-gears-nv.ppm && cmp -s /tmp/t2.ppm /tmp/egl-tri-nv.ppm \
		&& echo "    concurrent results identical to the single runs" || echo "!!! concurrent results differ"
fi
echo "=== $(date +%T) done"
