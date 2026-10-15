#!/bin/sh
# Runs ClassiCube (the classicube package) from a per-user directory: the game keeps options,
# maps and screenshots next to itself, and downloads into texpacks/ and audio/ (e.g. default.zip
# on first run). Those are real per-user directories, with links to the packaged textures and
# sounds in /usr/lib/classicube.
D="${XDG_DATA_HOME:-$HOME/.local/share}/classicube"
mkdir -p "$D"
for d in texpacks audio; do
	# earlier versions linked the whole directory to the read-only system one
	[ -L "$D/$d" ] && rm "$D/$d"
	mkdir -p "$D/$d"
	for f in /usr/lib/classicube/$d/*; do
		[ -e "$D/$d/${f##*/}" ] || ln -s "$f" "$D/$d/${f##*/}"
	done
done
[ -e "$D/options.txt" ] || cp /usr/lib/classicube/options.txt "$D/"
cd "$D" && exec /usr/lib/classicube/ClassiCube "$@"
