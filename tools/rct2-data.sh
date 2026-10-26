#!/bin/bash
# rct2-data.sh <RCT2.iso> [out dir]: the licensed game data deb OpenRCT2 needs, made from the user's own
# RollerCoaster Tycoon 2 CD image (Infogrames/Atari: not redistributable). Package rct2-data (arch all,
# Section local/games), so tools/licensed-publish.sh takes it, for the licensed suite only (also listed in
# tools/licensed-packages.txt). The data is never committed or put in a public suite.
# The CD has the files in InstallShield cabinets (DATA1.CAB/HDR, DATA2.CAB); unshield unpacks the
# "Minimum" group (Data, ObjData, Scenarios, Tracks, Landscapes: what OpenRCT2 reads as its game_path;
# ~115 MB). Nothing from the disc is executed. The disc's DATA/CSS3..46.DAT (ride music, ~480 MB) are left
# out (RCT2_MUSIC=1 adds them). unshield: UNSHIELD=path, else `unshield` on PATH, else built from
# upstream's git into build/tmp/rct2-tools.
# Installs to /usr/share/games/rct2 (OpenRCT2: game_path in ~/.config/OpenRCT2/config.ini).
# Needs: isoinfo, ar, tar, gzip (no dpkg-deb). Env: UNSHIELD RCT2_MUSIC VERSION
set -euo pipefail
{
ISO=${1:?usage: rct2-data.sh RCT2.iso [out dir]}
OUT=${2:-build/debs/rct2-data}
VERSION=${VERSION:-1.0-1}
W=build/tmp/rct2-data-work
U=${UNSHIELD:-$(command -v unshield || true)}
if [ -z "$U" ]; then
	T=build/tmp/rct2-tools
	if [ ! -x $T/b3/src/unshield ]; then
		mkdir -p $T
		[ -d $T/src ] || git clone -q --depth 1 https://github.com/twogood/unshield.git $T/src
		cmake -S $T/src -B $T/b3 -DUSE_OUR_OWN_MD5=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_BUILD_TYPE=Release >/dev/null
		cmake --build $T/b3 -j4 >/dev/null
	fi
	U=$T/b3/src/unshield
fi
rm -rf "$W"; mkdir -p "$W/iso" "$W/x" "$OUT"
for f in DATA1.CAB DATA1.HDR DATA2.CAB; do
	isoinfo -i "$ISO" -x "/$f;1" > "$W/iso/$(echo $f | tr A-Z a-z)"
done
"$U" -g Minimum -d "$W/x" x "$W/iso/data1.cab" > "$W/unshield.log"
S=$W/pkg/usr/share/games/rct2
mkdir -p "$S" "$W/pkg/DEBIAN"
for d in Data ObjData Scenarios Tracks Landscapes; do cp -a "$W/x/Minimum/$d" "$S/$d"; done
if [ "${RCT2_MUSIC:-0}" = 1 ]; then
	for f in $(isoinfo -i "$ISO" -f | grep -E '^/DATA/CSS[0-9]+\.DAT' | sed 's/;1$//'); do
		isoinfo -i "$ISO" -x "$f;1" > "$S/Data/$(basename "$f" | tr A-Z a-z)"
	done
fi
test -s "$S/Data/g1.dat" || { echo "!! no Data/g1.dat: the cabinet did not unpack"; exit 1; }
SZ=$(du -sk "$W/pkg/usr" | cut -f1)
cat > "$W/pkg/DEBIAN/control" <<EOC
Package: rct2-data
Version: $VERSION
Architecture: all
Section: local/games
Priority: optional
Installed-Size: $SZ
Maintainer: Tiberiu Chibici <chibicitiberiu@gmail.com>
Description: RollerCoaster Tycoon 2 game data for OpenRCT2 (from the owner's CD)
 Data, ObjData, Scenarios, Tracks and Landscapes of the original game, unpacked from the user's own
 disc image. Licensed content we may not redistribute: our own licensed suite only.
 Point OpenRCT2's game_path at /usr/share/games/rct2.
EOC
# unshield extracts with the CD's modes (directories 0700): readable by everyone, as the game runs as a user
find "$W/pkg/usr" -type d -exec chmod 755 {} +
find "$W/pkg/usr" -type f -exec chmod 644 {} +
( cd "$W/pkg" && find usr -type f -exec md5sum {} + > DEBIAN/md5sums )
D=$W/deb; mkdir -p "$D"
echo 2.0 > "$D/debian-binary"
( cd "$W/pkg/DEBIAN" && tar --owner=0 --group=0 -czf "../../deb/control.tar.gz" . )
( cd "$W/pkg" && tar --owner=0 --group=0 --exclude=./DEBIAN -czf "../deb/data.tar.gz" ./usr )
rm -f "$OUT/rct2-data_${VERSION}_all.deb"
( cd "$D" && ar rc "$OLDPWD/$OUT/rct2-data_${VERSION}_all.deb" debian-binary control.tar.gz data.tar.gz )
ls -l "$OUT"
exit
}
