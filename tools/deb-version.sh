#!/bin/bash
# deb-version.sh <pkg>: prints N for <pkg>'s "+ps3.N" version, the date and the short hash of that commit:
# N = the commit count up to the last commit that touched the package's directory or the shared
# packaging tooling (so versions only grow); a dirty tree gives "<N+1>~dev<stamp>" (make deploy only).
set -euo pipefail
PKG=$1
paths=("$PKG/" tools/deb-prepare.sh tools/deb-build.sh tools/deb-chroot.sh docker/Dockerfile.deb)
# package.conf VERSION_PATHS: more sources the package carries (e.g. ps3-boot's initramfs, tools/ssdimg)
VERSION_PATHS=$(sed -n 's/^VERSION_PATHS="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' "$PKG/package.conf" 2>/dev/null)
for v in $VERSION_PATHS; do paths+=("$v"); done
last=$(git log -1 --format=%H -- "${paths[@]}")
n=$(git rev-list --count "$last")
date=$(git log -1 --format=%cD "$last")
if [ -n "$(git status --porcelain -- "${paths[@]}")" ]; then
	n="$((n + 1))~dev$(date -u +%Y%m%d%H%M%S)"
	date=$(date -uR)
fi
echo "$n"
echo "$date"
git log -1 --format=%h "$last"	# the commit the version stands for (dirty trees: plus changes)
