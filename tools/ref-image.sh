#!/bin/bash
# ref-image.sh PKG [IMAGE]: a recipe's REF_IMAGE (tools/play-pc.sh, tools/bench-ref.sh), the PC side of a game
# we build ourselves: our source package (build/src/PKG/*.dsc, from make PKG-src: the PS3's patches in it)
# built for amd64 in Debian sid at the PS3's snapshot (BUILDER_BASE, DEBIAN_SNAPSHOT), then installed in
# a fresh container of the same base, committed as IMAGE (default PKG-amd64). The build runs in a
# throwaway container; only the .debs and the log come out, to build/ref-image/PKG/ (ephemeral).
#   JOBS=6   parallel build jobs (the PC is shared)
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash execute a
# shifted fragment.
{
cd "$(dirname "$0")/.."
PKG=${1:?usage: ref-image.sh PKG [IMAGE]}; IMG=${2:-$PKG-amd64}
DSC=$(ls build/src/"$PKG"/*.dsc 2> /dev/null | head -1 || :)
[ -n "$DSC" ] || { echo "!! no build/src/$PKG/*.dsc: make $PKG-src first" >&2; exit 2; }
tools/disk-check.sh
BASE=$(sed -n 's/^BUILDER_BASE *:= *//p' config.mk); SNAPSHOT=$(sed -n 's/^DEBIAN_SNAPSHOT *:= *//p' config.mk)
OUT=build/ref-image/$PKG
rm -rf "$OUT"; mkdir -p "$OUT"
apt_src="rm -f /etc/apt/sources.list.d/debian.sources
echo 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/$SNAPSHOT unstable main contrib' > /etc/apt/sources.list
apt-get update -qq"
echo ">> building $(basename "$DSC" .dsc) for amd64 (log: $OUT/build.log)"
docker run --rm -v "$PWD/build/src/$PKG:/src:ro" -v "$PWD/$OUT:/out" -e JOBS="${JOBS:-6}" "$BASE" bash -c "set -e
$apt_src
export DEBIAN_FRONTEND=noninteractive
apt-get install -y -qq --no-install-recommends build-essential dpkg-dev > /dev/null
mkdir /b; cd /b
dpkg-source -x /src/$(basename "$DSC") s > /dev/null
cd s
apt-get build-dep -y -qq --no-install-recommends ./ > /dev/null
DEB_BUILD_OPTIONS=\"nocheck noautodbgsym parallel=\$JOBS\" ionice -c3 nice -n 10 dpkg-buildpackage -b -uc -us > /out/build.log 2>&1 \
	|| { tail -30 /out/build.log; chown -R $(id -u):$(id -g) /out; exit 1; }
cp ../*.deb /out/; chown -R $(id -u):$(id -g) /out"
echo ">> installing $(cd "$OUT" && ls ./*.deb | tr '\n' ' ')in $IMG"
c=ref-image-$PKG-$$
docker rm -f "$c" > /dev/null 2>&1 || :
docker run --name "$c" -v "$PWD/$OUT:/debs:ro" "$BASE" bash -c "set -e
$apt_src
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends /debs/*.deb > /dev/null
rm -rf /var/lib/apt/lists/*" || { docker rm -f "$c" > /dev/null; exit 1; }
docker commit -c 'CMD ["bash"]' "$c" "$IMG" > /dev/null
docker rm "$c" > /dev/null
echo ">> $IMG: $(docker image inspect -f '{{.Id}}' "$IMG" | cut -c8-19)"
}
