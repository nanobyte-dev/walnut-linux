#!/bin/sh
# Runs ClassiCube (the classicube package) from a per-user directory: the game keeps options,
# maps and screenshots next to itself. Textures and sounds stay in /usr/lib/classicube.
D="${XDG_DATA_HOME:-$HOME/.local/share}/classicube"
mkdir -p "$D"
for f in texpacks audio; do
	[ -e "$D/$f" ] || ln -s "/usr/lib/classicube/$f" "$D/$f"
done
[ -e "$D/options.txt" ] || cp /usr/lib/classicube/options.txt "$D/"
cd "$D" && exec /usr/lib/classicube/ClassiCube "$@"
