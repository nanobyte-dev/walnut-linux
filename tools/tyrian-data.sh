#!/bin/bash
# tyrian-data.sh <tyrian21.zip> [out dir]: the licensed game data deb OpenTyrian needs, from the
# freeware Tyrian 2.1 zip (Epic MegaGames / Daniel Cook, camanis.net; its license.doc is Epic's
# "Limited Use Software License Agreement" and file_id.diz says all copying and distribution is
# prohibited, so we do not redistribute it). Package tyrian-data (the name OpenTyrian's Depends
# asks for; arch all, Section local/games), so tools/repo-publish.sh sends it to the ps3-licensed
# suite only (also listed in tools/licensed-packages.txt). The data is never committed or put in a
# public suite. Installs to /usr/share/games/tyrian (OpenTyrian's TYRIAN_DIR).
# The DOS executables, the setup and order programs and the manuals' installers are left out; the
# license text becomes the package's copyright file.
# Needs: unzip, ar, tar, gzip (no dpkg-deb). Env: VERSION
set -euo pipefail
{
ZIP=${1:?usage: tyrian-data.sh tyrian21.zip [out dir]}
OUT=${2:-build/debs/tyrian-data}
VERSION=${VERSION:-2.1-1}
W=build/tmp/tyrian-data-work
rm -rf "$W"; mkdir -p "$W/x" "$OUT"
unzip -q "$ZIP" -d "$W/x"
SRC=$(dirname "$(find "$W/x" -name tyrian1.lvl | head -1)")
test -s "$SRC/tyrian1.lvl" || { echo "!! no tyrian1.lvl in $ZIP"; exit 1; }
S=$W/pkg/usr/share/games/tyrian
DOC=$W/pkg/usr/share/doc/tyrian-data
mkdir -p "$S" "$DOC" "$W/pkg/DEBIAN"
for f in "$SRC"/*; do
	b=$(basename "$f")
	case "$(echo "$b" | tr A-Z a-z)" in
		*.exe|*.com|*.bat|*.diz|*.doc|*.txt|*.box|*.int|*.ini|*.tfp) ;;
		*) cp "$f" "$S/$(echo "$b" | tr A-Z a-z)" ;;
	esac
done
cp "$SRC/license.doc" "$DOC/copyright"
find "$W/pkg" -exec touch -d @1609200000 {} +
find "$W/pkg" -type d -exec chmod 755 {} +
find "$W/pkg" -type f -exec chmod 644 {} +
test -s "$S/tyrian1.lvl" -a -s "$S/tyrian.hdt" -a -s "$S/palette.dat" -a -s "$S/voices.snd" || { echo "!! data files missing"; exit 1; }
SZ=$(du -sk "$W/pkg/usr" | cut -f1)
cat > "$W/pkg/DEBIAN/control" <<EOC
Package: tyrian-data
Version: $VERSION
Architecture: all
Section: local/games
Priority: optional
Installed-Size: $SZ
Maintainer: PS3 Linux project <ps3@localhost>
Description: Tyrian 2.1 game data for OpenTyrian (freeware release, not redistributable)
 The data files of the freeware Tyrian 2.1 (Epic MegaGames, 2004; levels, graphics, music and
 sound), from the zip on camanis.net. The license is Epic's limited-use agreement, so this package
 is for our LAN repository only (the ps3-licensed suite). OpenTyrian finds it in
 /usr/share/games/tyrian.
EOC
( cd "$W/pkg" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums )
D=$W/deb; mkdir -p "$D"
echo 2.0 > "$D/debian-binary"
( cd "$W/pkg/DEBIAN" && tar --owner=0 --group=0 --numeric-owner --sort=name --mtime=@1609200000 -czf "../../deb/control.tar.gz" . )
( cd "$W/pkg" && tar --owner=0 --group=0 --numeric-owner --sort=name --mtime=@1609200000 --exclude=./DEBIAN -czf "../deb/data.tar.gz" ./usr )
rm -f "$OUT/tyrian-data_${VERSION}_all.deb"
( cd "$D" && ar rc "$OLDPWD/$OUT/tyrian-data_${VERSION}_all.deb" debian-binary control.tar.gz data.tar.gz )
rm -rf "$W"
ls -l "$OUT"
exit
}
