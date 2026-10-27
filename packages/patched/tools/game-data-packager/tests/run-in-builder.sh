#!/bin/bash
# run-in-builder.sh: test_gdp_patches.py (our game-data-packager patches) against the deb that
# make game-data-packager-deb built (build/debs/game-data-packager), installed in a throwaway
# container of the deb builder image (its apt sources: the Debian snapshot, for python3-yaml,
# python3-debian and the unpackers its Recommends name, without the GTK runtime). Exit 77 (skipped, tools/run-tests.sh) without docker, the image or the deb.
set -eu
cd "$(dirname "$0")/../../../../.."
deb=$(ls build/debs/game-data-packager/game-data-packager_*_all.deb 2>/dev/null | sort -V | tail -1)
[ -n "$deb" ] || { echo "skip: no build/debs/game-data-packager (make game-data-packager-deb)"; exit 77; }
command -v docker >/dev/null || { echo "skip: no docker"; exit 77; }
img=$(docker images --format '{{.Repository}}:{{.Tag}}' ps3-deb-builder 2>/dev/null | head -1)
[ -n "$img" ] || { echo "skip: no ps3-deb-builder image"; exit 77; }
echo ">> $deb in $img"
docker run --rm -v "$PWD:/work:ro" -w /work "$img" sh -ec "
	apt-get update -qq
	apt-get install -y -qq --no-install-recommends ./$deb 7zip cabextract innoextract lhasa unshield unzip >/dev/null
	python3 -I packages/patched/tools/game-data-packager/tests/test_gdp_patches.py -v"
