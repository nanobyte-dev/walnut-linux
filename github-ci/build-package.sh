#!/bin/bash
# build-package.sh PKG OUT [testing|stable]: builds one package on a GitHub runner
# (.github/workflows/walnut-package.yml) into OUT/ (the .debs, debug ones included) and OUT/src/ (the
# source package). No secrets here: this job runs the build, the keys are only in the publish job.
# Our own build dependencies (package.conf LOCAL_DEPS; ps3rsx: the kernel) come from the suite the
# build is for, checked against its signed index. The licensed packages never build in CI: what is
# licensed is tools/licensed.py's answer (its check first: declarations that disagree or can't be read
# stop the build), and a package in a licensed directory is refused by its path too.
set -euo pipefail
cd "$(dirname "$0")/.."
pkg=$1 out=$2 target=${3:-testing}
case $target in
	testing) ring=apt-repo/keyrings/testing-keyring.gpg ;;
	stable) ring=apt-repo/keyrings/archive-keyring.gpg ;;
	*) echo "!! target: testing or stable" >&2; exit 2 ;;
esac
pd=$(tools/pkg-dir.sh "$pkg") || exit 2
case $pd in packages/licensed/*|gamedb/*/*/licensed/*) echo "!! $pkg is licensed ($pd): never built in CI" >&2; exit 2 ;; esac
python3 -I tools/licensed.py check || { echo "!! tools/licensed.py check failed: no build" >&2; exit 2; }
rc=0; python3 -I tools/licensed.py is-licensed "$pkg" || rc=$?
case $rc in
	0) echo "!! $pkg is licensed: never built in CI" >&2; exit 2 ;;
	1) ;;
	*) echo "!! tools/licensed.py is-licensed $pkg failed (exit $rc): no build" >&2; exit 2 ;;
esac
conf() { sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$pd/package.conf"; }
github-ci/runner-prep.sh

fetch() {	# fetch DIR SOURCE: the target suite's newest build of SOURCE into build/debs/DIR
	apt-repo/walnut-repo.py fetch --suite "$target" --keyring "$ring" --source "$2" --to "build/debs/$1"
}
deps=$(conf LOCAL_DEPS)
[ "$pkg" = ps3rsx ] && deps="linux"
for d in $deps; do
	s=$(sed -n 's/^SOURCE=//p' "$(tools/pkg-dir.sh "$d")/package.conf"); s=${s:-$d}
	echo "::group::build dependency $d ($s) from $target"
	fetch "$d" "$s"
	echo "::endgroup::"
done

jobs=$(nproc)
# package.conf CI_FIRST: make targets the build needs that a developer's tree already has (ps3-spu: the
# spu-elf toolchain); a toolchain the cache restored is used as it is
for t in $(conf CI_FIRST); do
	if [ "$t" = spu-toolchain ] && [ -x build/spu/install/opt/spu/bin/spu-elf-gcc ]; then
		echo "-- spu-toolchain: restored from the cache"
		continue
	fi
	echo "::group::make $t"
	make "$t"
	echo "::endgroup::"
done
if [ "$pkg" = ps3rsx ]; then
	make ps3rsx-modules DEB_JOBS="$jobs"
	dirs=$(ls -d build/debs/ps3rsx-*)
else
	make "$pkg-deb" DEB_JOBS="$jobs"
	dirs=build/debs/$pkg
fi

rm -rf "$out"
mkdir -p "$out/src"
for d in $dirs; do
	find "$d" -maxdepth 1 -name '*.deb' -exec cp -t "$out/" {} +
done
for dsc in build/src/"$pkg"/*.dsc build/src/ps3rsx-*/*.dsc; do
	[ -f "$dsc" ] || continue
	cp "$dsc" "$out/src/"
	# the .dsc's files: next to it, or (Debian's orig tarballs) where deb-fetch.sh put them; real files
	# only: deb-prepare.sh links the orig tarball into build/src by its path inside the container
	sed -n '/^Files:/,/^[^ ]/p' "$dsc" | awk 'NF==3 {print $3}' | while read -r f; do
		src=$(find "$(dirname "$dsc")" "build/sources/$pkg" build/downloads -maxdepth 2 -type f -name "$f" -print -quit 2>/dev/null)
		[ -n "$src" ] || { echo "!! $f (from $(basename "$dsc")) not found" >&2; exit 1; }
		cp "$src" "$out/src/"
	done
done
# nothing licensed leaves this job: no licensed binary, no licensed file in the sources
tools/leak-check.py debs "$out"/*.deb
ls "$out"/src/*.dsc >/dev/null 2>&1 && tools/leak-check.py sources "$out"/src/*.dsc
echo ">> $(ls "$out"/*.deb | wc -l) debs, $(ls "$out"/src/*.dsc 2>/dev/null | wc -l) source packages"
ls -la "$out" | tail -n +2
