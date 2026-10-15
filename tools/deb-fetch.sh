#!/bin/bash
# deb-fetch.sh <pkg>: downloads the Debian source package named in <pkg>/package.conf from the
# snapshot (inside the deb builder, as root; apt checks it against the snapshot's signed Release)
# into build/sources/<pkg>/, and checks the .dsc against DSC_SHA256 (printed if not pinned yet).
# Env: OWNER (uid:gid for the files)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
PKG=$1
. "$PKG/package.conf"
OUT=build/sources/$PKG
mkdir -p "$OUT"
DSC=$OUT/${SOURCE}_${VERSION#*:}.dsc
if [ ! -e "$DSC" ]; then
	apt-get update -qq
	(cd "$OUT" && apt-get source --download-only -qq "$SOURCE=$VERSION")
fi
sum=$(sha256sum "$DSC" | cut -d' ' -f1)
if [ -z "${DSC_SHA256:-}" ]; then
	echo "!! $PKG/package.conf has no DSC_SHA256; the downloaded .dsc is: DSC_SHA256=$sum"
	exit 1
fi
[ "$sum" = "$DSC_SHA256" ] || { echo "!! $DSC: sha256 $sum, expected $DSC_SHA256"; exit 1; }
chown -R "${OWNER:-0:0}" "$OUT"
echo ">> $DSC"
exit
}
