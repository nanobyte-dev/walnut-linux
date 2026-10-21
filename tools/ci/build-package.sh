#!/bin/bash
# build-package.sh PKG OUT [testing|stable]: builds one package on a GitHub runner
# (.github/workflows/walnut-package.yml) into OUT/ (the .debs, debug ones included) and OUT/src/ (the
# source package). No secrets here: this job runs the build, the keys are only in the publish job.
# Our own build dependencies (package.conf LOCAL_DEPS; ps3rsx: the kernel) come from the suite the
# build is for, checked against its signed index. The licensed packages never build in CI
# (tools/licensed-packages.txt).
set -euo pipefail
cd "$(dirname "$0")/../.."
pkg=$1 out=$2 target=${3:-testing}
case $target in
	testing) ring=tools/walnut-server/testing-keyring.gpg ;;
	stable) ring=tools/walnut-server/archive-keyring.gpg ;;
	*) echo "!! target: testing or stable" >&2; exit 2 ;;
esac
[ -f "$pkg/package.conf" ] || { echo "!! no package $pkg" >&2; exit 2; }
! grep -qx "$pkg" tools/licensed-packages.txt || { echo "!! $pkg is licensed: never built in CI" >&2; exit 2; }
conf() { sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}$/\1/p" "$pkg/package.conf"; }
# qemu-user from the build snapshot (relative to SNAPSHOT_URL), pinned
QEMU_DEB_URL=archive/debian/20261013T000000Z/pool/main/q/qemu/qemu-user_11.1.2+ds-3_amd64.deb
QEMU_DEB_SHA256=4bacc2bf647f1756c5f08e7f146161d3eeef9d9f53e5d6412d1b3daca0c93655

if [ "${GITHUB_ACTIONS:-}" = true ]; then	# only on a throwaway runner: this deletes things
	echo "::group::runner: disk, qemu"
	# the runner's preinstalled SDKs and images: ~30 GB we don't use
	sudo rm -rf /usr/share/dotnet /usr/local/lib/android /opt/ghc /usr/local/.ghcup /opt/hostedtoolcache/CodeQL /usr/local/share/boost
	sudo docker image prune -af >/dev/null || true
	df -h / | tail -1
	# ppc64 (big-endian) binaries through qemu: native builds run in a ppc64 chroot. Debian's qemu
	# from the build snapshot, not Ubuntu's (8.2: systemd's postinst died with "stack smashing
	# detected" under it, and libvorbis's tests failed), registered like the dev PC's: flag F
	deb=$QEMU_DEB_URL; deb=${deb##*/}
	curl -fsSL --retry 5 -o "/tmp/$deb" "$(make -s --no-print-directory --eval 'p: ; @echo $(SNAPSHOT_URL)' p)/$QEMU_DEB_URL"
	echo "$QEMU_DEB_SHA256  /tmp/$deb" | sha256sum -c --quiet
	dpkg-deb --fsys-tarfile "/tmp/$deb" | tar -xO ./usr/bin/qemu-ppc64 | sudo tee /usr/local/bin/qemu-ppc64-static >/dev/null
	sudo chmod 755 /usr/local/bin/qemu-ppc64-static
	[ ! -e /proc/sys/fs/binfmt_misc/qemu-ppc64 ] || echo -1 | sudo tee /proc/sys/fs/binfmt_misc/qemu-ppc64 >/dev/null
	printf '%s' ':qemu-ppc64:M::\x7fELF\x02\x02\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x15:\xff\xff\xff\xff\xff\xff\xff\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff:/usr/local/bin/qemu-ppc64-static:F' \
		| sudo tee /proc/sys/fs/binfmt_misc/register >/dev/null
	/usr/local/bin/qemu-ppc64-static --version | head -1
	echo "::endgroup::"
fi
[ -e /proc/sys/fs/binfmt_misc/qemu-ppc64 ] || { echo "!! no qemu-ppc64 binfmt" >&2; exit 1; }

fetch() {	# fetch DIR SOURCE: the target suite's newest build of SOURCE into build/debs/DIR
	tools/walnut-repo.py fetch --suite "$target" --keyring "$ring" --source "$2" --to "build/debs/$1"
}
deps=$(conf LOCAL_DEPS)
[ "$pkg" = ps3rsx ] && deps="linux"
for d in $deps; do
	s=$(sed -n 's/^SOURCE=//p' "$d/package.conf"); s=${s:-$d}
	echo "::group::build dependency $d ($s) from $target"
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
