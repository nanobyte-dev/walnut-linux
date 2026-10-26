#!/bin/bash
# pkg-dir.sh NAME: prints the directory of our source package NAME (the one with NAME/package.conf
# under packages/: walnut/<area>/, patched/<area>/, repackaged/, licensed/; docs/LAYOUT.md), relative
# to the repository's top; exit 1 when there is none. Packages are named by their directory alone:
# make NAME-deb, build/src/NAME, build/debs/NAME.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ ${1:-} =~ ^[a-z0-9][a-z0-9.+-]*$ ]] || { echo "usage: $0 NAME" >&2; exit 2; }
for d in packages/*/"$1" packages/*/*/"$1" packages/*/*/*/"$1"; do
	[ -f "$d/package.conf" ] && { echo "$d"; exit 0; }
done
echo "!! no package $1 (packages/*/$1/package.conf)" >&2
exit 1
