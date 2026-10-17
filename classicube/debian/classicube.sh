#!/bin/sh
# Runs ClassiCube (the classicube package) from a per-user directory: the game keeps options,
# maps and screenshots next to itself, and downloads into texpacks/ and audio/ (e.g. default.zip
# on first run). Those are real per-user directories with copies of the packaged textures and
# sounds from /usr/lib/classicube (classicube-data, when installed; without it the launcher offers to
# download them, see README.Debian): the game also rewrites them (texpacks/classicube.zip), which
# failed through links to the read-only originals.
D="${XDG_DATA_HOME:-$HOME/.local/share}/classicube"
mkdir -p "$D"
for d in texpacks audio; do
	# earlier versions linked the whole directory to the read-only system one
	[ -L "$D/$d" ] && rm "$D/$d"
	mkdir -p "$D/$d"
	for f in /usr/lib/classicube/$d/*; do
		[ -e "$f" ] || continue
		t="$D/$d/${f##*/}"
		[ -L "$t" ] && rm "$t"
		[ -e "$t" ] || cp "$f" "$t"
	done
done
[ -e "$D/options.txt" ] || cp /usr/lib/classicube/options.txt "$D/"
cd "$D" && exec /usr/lib/classicube/ClassiCube "$@"
