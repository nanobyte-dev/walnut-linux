#!/bin/bash
# runner-prep.sh: a GitHub runner made ready for our builds (tools/ci/build-package.sh,
# build-release.sh): disk space, and ppc64 binaries through Debian's qemu, registered like the dev
# PC's. Elsewhere it only checks that the qemu-ppc64 binfmt is there.
set -euo pipefail
cd "$(dirname "$0")/../.."
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
