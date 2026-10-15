#!/bin/sh
# egl-thin over sizes, least suspicious first; each size logged (synced) before it runs
. /usr/lib/ps3-mesa/env.sh
for s in "64 64" "64 1" "1427 1" "2 64" "1 64" "1 2" "1 3" "1 1"; do
	echo "run $s" >> ~/thin.log; sync
	timeout 20 egl-thin $s >> ~/thin.log 2>&1; echo "rc $?" >> ~/thin.log; sync
	sudo dmesg | grep -q "graph exception" && { echo "EXCEPTION after $s" >> ~/thin.log; sync; break; }
done
echo end >> ~/thin.log
