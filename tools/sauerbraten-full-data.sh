#!/bin/bash
# sauerbraten-full-data.sh <release.tar.bz2> <out.tar.gz> <topdir>: the orig tarball of sauerbraten-data:
# data/, packages/, docs/ and README.html of Sauerbraten's 2020_12_29 Linux release (not the binaries,
# the engine source or the editing leftovers), as <topdir>/..., made reproducibly (sorted, fixed owner
# and mtime, gzip -n). Scratch space under build/tmp, removed at the end.
set -euo pipefail
{
SRC=$1 OUT=$2 TOP=$3
W=build/tmp/sauer-full-orig
rm -rf "$W"; mkdir -p "$W"
tar -xjf "$SRC" -C "$W" sauerbraten/data sauerbraten/packages sauerbraten/docs sauerbraten/README.html
mv "$W/sauerbraten" "$W/$TOP"
for d in data packages docs; do [ -d "$W/$TOP/$d" ] || { echo "!! $d/ missing in the release"; exit 1; }; done
n=$(find "$W/$TOP" -type f | wc -l)
tar --sort=name --mtime=@1609200000 --owner=0 --group=0 --numeric-owner -C "$W" -cf - "$TOP" | gzip -n6 > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
rm -rf "$W"
echo ">> $OUT ($n files)"
exit 0
}
