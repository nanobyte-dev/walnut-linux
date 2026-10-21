#!/bin/bash
# build-package.sh PKG OUT: builds one package on a GitHub runner (.github/workflows/walnut-package.yml)
# into OUT/ (the .debs, debug ones included) and OUT/src/ (the source package). No secrets here:
# this job runs the build, the keys are only in the publish jobs. Our own build dependencies
# (package.conf LOCAL_DEPS; ps3rsx: the kernel) come from the testing suite, checked against its
# signed index. The licensed packages never build in CI (tools/licensed-packages.txt).
set -euo pipefail
cd "$(dirname "$0")/../.."
pkg=$1 out=$2
[ -f "$pkg/package.conf" ] || { echo "!! no package $pkg" >&2; exit 2; }
! grep -qx "$pkg" tools/licensed-packages.txt || { echo "!! $pkg is licensed: never built in CI" >&2; exit 2; }
conf() { sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$pkg/package.conf"; }

echo "::group::runner: disk, qemu"
# the runner's preinstalled SDKs: ~30 GB we don't use
sudo rm -rf /usr/share/dotnet /usr/local/lib/android /opt/ghc /usr/local/.ghcup /opt/hostedtoolcache/CodeQL /usr/local/share/boost
sudo docker image prune -af >/dev/null || true
df -h / | tail -1
# ppc64 (big-endian) binaries through qemu: native builds run in a ppc64 chroot
sudo apt-get update -qq && sudo apt-get install -y -qq qemu-user-static >/dev/null
[ -e /proc/sys/fs/binfmt_misc/qemu-ppc64 ] || { echo "!! no qemu-ppc64 binfmt" >&2; exit 1; }
echo "::endgroup::"

fetch() {	# fetch DIR SOURCE: the newest testing build of SOURCE into build/debs/DIR
	tools/walnut-repo.py fetch --suite testing --keyring tools/walnut-server/testing-keyring.gpg \
		--source "$2" --to "build/debs/$1"
}
deps=$(conf LOCAL_DEPS)
[ "$pkg" = ps3rsx ] && deps="linux"
for d in $deps; do
	s=$(sed -n 's/^SOURCE=//p' "$d/package.conf"); s=${s:-$d}
	echo "::group::build dependency $d ($s) from testing"
	fetch "$d" "$s"
	echo "::endgroup::"
done

jobs=$(nproc)
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
	sed -n '/^Files:/,/^[^ ]/p' "$dsc" | awk 'NF==3 {print $3}' | while read -r f; do
		cp "$(dirname "$dsc")/$f" "$out/src/"
	done
done
# nothing licensed leaves this job: no licensed binary, no licensed file in the sources
tools/leak-check.py debs "$out"/*.deb
ls "$out"/src/*.dsc >/dev/null 2>&1 && tools/leak-check.py sources "$out"/src/*.dsc
echo ">> $(ls "$out"/*.deb | wc -l) debs, $(ls "$out"/src/*.dsc 2>/dev/null | wc -l) source packages"
ls -la "$out" | tail -n +2
