#!/bin/bash
# pkg-export.sh <pkg>: writes the commits of build/work/<pkg> (tools/pkg-work.sh) back as our
# patch series, inside the deb builder. Commits touching only debian/ become <pkg>/packaging/,
# the others <pkg>/patches/[<Gbp-Pq topic>/]<Gbp-Pq name or format-patch name>; a commit that
# touches both is refused. Patches are git format-patch output without the parts that change on
# every export (index lines, stats, signatures, commit ids). Both series files are rewritten, the
# old patch files replaced. Review with git diff; tag "exported" then marks the tree as saved.
set -euo pipefail
{
PKG=$1
. "$PKG/package.conf"
W=build/work/$PKG
[ -d "$W/.git" ] || { echo "!! no work tree: make $PKG-work"; exit 1; }
cd "$W"
[ -z "$(git status --porcelain)" ] || { echo "!! $W has uncommitted changes: commit them first"; exit 1; }
# a tree pkg-work.sh didn't finish has no "exported" tag: exporting it would drop patches
git rev-parse -q --verify exported >/dev/null || { echo "!! $W is incomplete (pkg-work failed?): not exporting"; exit 1; }
base=$(git describe --tags --abbrev=0 --match 'base/*' HEAD)
OUT=/work/build/tmp/export-$PKG
rm -rf "$OUT"; mkdir -p "$OUT/packaging" "$OUT/patches"
: > "$OUT/packaging/series"; : > "$OUT/patches/series"
n=0
for c in $(git rev-list --reverse "$base"..HEAD); do
	n=$((n + 1))
	files=$(git diff-tree --no-commit-id --name-only -r "$c")
	deb=$(echo "$files" | grep -c '^debian/' || true)
	all=$(echo "$files" | grep -c . || true)
	msg=$(git log -1 --format=%B "$c")
	name=$(echo "$msg" | sed -n 's/^Gbp-Pq: Name //p' | tail -1)
	topic=$(echo "$msg" | sed -n 's/^Gbp-Pq: Topic //p' | tail -1)
	[ -n "$name" ] || name=$(git format-patch -1 --stdout --start-number "$n" "$c" >/dev/null; \
		printf '%04d-%s.patch' "$n" "$(git log -1 --format=%f "$c" | cut -c1-52)")
	if [ "$deb" = "$all" ]; then kind=packaging; topic=
	elif [ "$deb" = 0 ]; then kind=patches; [ "$topic" = packaging ] && topic=
	else echo "!! $(git log -1 --format='%h %s' "$c"): touches debian/ and the source: split it"; exit 1; fi
	rel=${topic:+$topic/}$name
	mkdir -p "$OUT/$kind/$(dirname "$rel")"
	git format-patch -1 --stdout --zero-commit --no-signature --no-stat -k "$c" |
		grep -v '^index [0-9a-f]*\.\.[0-9a-f]*' |
		sed '/^Gbp-Pq: /d' |
		awk '/^diff --git /{body=1} !body && /^$/ {if (blank) next; blank=1; print; next} {blank=0; print}' > "$OUT/$kind/$rel"
	grep -q '^Subject: .' "$OUT/$kind/$rel" || { echo "!! $rel has no subject"; exit 1; }
	echo "$rel" >> "$OUT/$kind/series"
done
# replace the series in the repository (only patch files and series; review with git diff)
for kind in packaging patches; do
	D=/work/$PKG/$kind
	if [ -s "$OUT/$kind/series" ]; then
		mkdir -p "$D"
		# the series file's leading comment block (where the patches come from) is kept
		{ [ ! -e "$D/series" ] || sed -n '/^\s*#/!q; p' "$D/series"; cat "$OUT/$kind/series"; } > "$OUT/$kind/series.new"
		mv "$OUT/$kind/series.new" "$OUT/$kind/series"
		find "$D" -name '*.patch' -delete
		find "$D" -mindepth 1 -type d -empty -delete
		cp -r "$OUT/$kind/." "$D/"
	elif [ -d "$D" ]; then
		find "$D" -name '*.patch' -delete; rm -f "$D/series"
		find "$D" -depth -type d -empty -delete
	fi
done
rm -rf "$OUT"
git tag -f exported >/dev/null
count() { grep -cv -e '^\s*#' -e '^\s*$' "$1" 2>/dev/null || echo 0; }
echo ">> $PKG: $(count /work/$PKG/packaging/series) packaging and $(count /work/$PKG/patches/series) source patches exported; review with git diff -- $PKG"
exit
}
