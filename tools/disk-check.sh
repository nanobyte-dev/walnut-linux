#!/bin/sh
# disk-check.sh: stops a heavy build when the filesystem holding build/ has less than 10% free
# (/home is shared and nearly full; `make du` shows usage, `make clean-ephemeral` reclaims space)
set -eu
mkdir -p build
used=$(df --output=pcent build | tail -1 | tr -dc 0-9)
if [ "$used" -gt 90 ]; then
	echo "!! build/ is on a filesystem $used% full (less than 10% free): stopping."
	echo "   make du   shows what uses space; make clean-ephemeral reclaims it."
	exit 1
fi
