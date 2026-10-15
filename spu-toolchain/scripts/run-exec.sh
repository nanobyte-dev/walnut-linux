#!/bin/sh
# run-exec.sh <dir>: runs every SPU program in <dir> on an SPE; PASS = exit 0
for f in "$1"/*.spu; do
	timeout 10 ~/spu/spurun "$f" > /dev/null 2>&1
	rc=$?
	case $rc in 0) r=PASS ;; 124) r=TIMEOUT ;; 125) r=NOEXIT ;; *) r=FAIL ;; esac
	echo "$r $(basename "$f" .spu)"
done
