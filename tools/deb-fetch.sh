#!/bin/bash
# deb-fetch.sh <pkg>: downloads the Debian source package named in <pkg>/package.conf from the
# snapshot (inside the deb builder, as root; apt checks it against the snapshot's signed Release)
# into build/sources/<pkg>/, and checks the .dsc against DSC_SHA256 (printed if not pinned yet).
# package.conf COMPONENT (e.g. contrib): the archive component the source is in, when it isn't main;
# the builder's apt sources are main only, so its deb-src lines are added for this fetch (the
# container is thrown away after it; docker/Dockerfile.deb stays as it is).
# package.conf SOURCE_SNAPSHOT: a Debian source newer than the build snapshot, fetched from the
# snapshot it first appears in (its unstable deb-src line added the same way); DSC_SHA256 still pins it.
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
	if [ -n "${COMPONENT:-}" ] && [ "$COMPONENT" != main ]; then
		sed -n "s/^\(deb-src .*\) main\$/\1 $COMPONENT/p" /etc/apt/sources.list > /etc/apt/sources.list.d/ps3-$COMPONENT.list
	fi
	if [ -n "${SOURCE_SNAPSHOT:-}" ]; then
		sed -n "s|^\(deb-src .*/archive/debian/\)[0-9TZ]*\( unstable main\)\$|\1$SOURCE_SNAPSHOT\2|p" /etc/apt/sources.list \
			> /etc/apt/sources.list.d/ps3-source-snapshot.list
		[ -s /etc/apt/sources.list.d/ps3-source-snapshot.list ] || { echo "!! no unstable deb-src line to point at $SOURCE_SNAPSHOT"; exit 1; }
	fi
	apt-get update -qq
	(cd "$OUT" && apt-get source --download-only -qq "$SOURCE=$VERSION")
fi
chown -R "${OWNER:-0:0}" "$OUT"
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
