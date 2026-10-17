#!/bin/bash
# sauerbraten-data.sh <release.tar.bz2> <files.txt> <out.tar.gz> <topdir>: the orig tarball of
# sauerbraten-nonfree-data: the files listed in files.txt (paths below the release's packages/)
# out of Sauerbraten's 2020_12_29 Linux release, as <topdir>/packages/..., made reproducibly
# (sorted, fixed owner and mtime, gzip -n). Scratch space under build/tmp, removed at the end.
set -euo pipefail
{
SRC=$1 LIST=$2 OUT=$3 TOP=$4
W=build/tmp/sauer-data-orig
rm -rf "$W"; mkdir -p "$W/$TOP"
sed 's|^|sauerbraten/packages/|' "$LIST" > "$W/members"
tar -xjf "$SRC" -C "$W" --files-from="$W/members"
mv "$W/sauerbraten/packages" "$W/$TOP/packages"
n=$(find "$W/$TOP" -type f | wc -l)
[ "$n" = "$(wc -l < "$LIST")" ] || { echo "!! $n files extracted, $(wc -l < "$LIST") listed"; exit 1; }
tar --sort=name --mtime=@1609200000 --owner=0 --group=0 --numeric-owner -C "$W" -cf - "$TOP" | gzip -n9 > "$OUT.tmp"
mv "$OUT.tmp" "$OUT"
rm -rf "$W"
echo ">> $OUT ($n files)"
exit 0
}
