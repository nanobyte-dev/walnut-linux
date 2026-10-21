#!/bin/bash
# build-initrd-tools.sh ARCH: the generic initramfs's install tools (docs/GAMEOS-HDD.md "The internal
# install"), static: mke2fs from e2fsprogs (busybox's makes ext2 only) and GNU tar (busybox's tar
# drops xattrs, so file capabilities would be lost). From the upstream tarballs pinned in config.mk
# (make initrd-busybox downloads them; both GPG-checked when pinned, 2026-10-21). In the builder
# container (build-initrd.sh calls it): ARCH ppc64 (the PS3, powerpc64-linux-gnu-gcc) or amd64 (the
# KVM test, gcc). Output: build/initrd-tools/ARCH/{mke2fs,tar}, stripped, and .stamp (the sources'
# sha256 and this script's): rebuilt only when one changes. Env: E2FSPROGS_TAR, GNUTAR_TAR (paths).
set -euo pipefail
cd "$(dirname "$0")/../.."
ARCH=${1:?ARCH: ppc64 or amd64}
case $ARCH in
ppc64) HOST=powerpc64-linux-gnu STRIP=powerpc64-linux-gnu-strip ;;
amd64) HOST=x86_64-linux-gnu STRIP=strip ;;
*) echo "ARCH: ppc64 or amd64" >&2; exit 2 ;;
esac
E2=${E2FSPROGS_TAR:-$(ls build/downloads/initrd/e2fsprogs-*.tar.xz | tail -n 1)}
GT=${GNUTAR_TAR:-$(ls build/downloads/initrd/tar-*.tar.xz | tail -n 1)}
OUT=build/initrd-tools/$ARCH
stamp=$( (sha256sum "$E2" "$GT" | cut -d' ' -f1; sha256sum "$0" | cut -d' ' -f1) | xargs)
if [ -x "$OUT/mke2fs" ] && [ -x "$OUT/tar" ] && [ "$(cat "$OUT/.stamp" 2>/dev/null)" = "$stamp" ]; then
	echo "   install tools for $ARCH: up to date ($OUT)"
	exit 0
fi
mkdir -p build/tmp
W=$(mktemp -d build/tmp/initrd-tools.XXXXXX)
trap 'rm -rf "$W"' EXIT
J=${JOBS:-$(nproc)}
echo "== mke2fs ($(basename "$E2"), $ARCH, static)"
tar -xf "$E2" -C "$W"
mkdir "$W/e2b"
# ppc64's uapi types are int-l64 unless __SANE_USERSPACE_TYPES__; e2fsprogs's own types are long long
(cd "$W/e2b" && ../e2fsprogs-*/configure --host=$HOST --disable-nls --disable-elf-shlibs --disable-bsd-shlibs \
	--disable-fuse2fs --disable-defrag --disable-imager --disable-uuidd --disable-debugfs --disable-e2initrd-helper \
	--disable-tdb --disable-fsck --enable-libuuid --enable-libblkid --without-libarchive \
	CFLAGS="-O2 -D__SANE_USERSPACE_TYPES__" LDFLAGS=-static > ../e2.log 2>&1 &&
	make -j"$J" libs >> ../e2.log 2>&1 && make -j"$J" -C misc mke2fs >> ../e2.log 2>&1) ||
	{ tail -30 "$W/e2.log"; echo "mke2fs build failed" >&2; exit 1; }
echo "== GNU tar ($(basename "$GT"), $ARCH, static)"
tar -xf "$GT" -C "$W"
mkdir "$W/tb"
(cd "$W/tb" && ../tar-*/configure --host=$HOST --disable-nls --without-selinux --without-posix-acls --disable-acl \
	LDFLAGS=-static > ../tar.log 2>&1 && make -j"$J" >> ../tar.log 2>&1) ||
	{ tail -30 "$W/tar.log"; echo "tar build failed" >&2; exit 1; }
mkdir -p "$OUT"
install -m 755 "$W/e2b/misc/mke2fs" "$OUT/mke2fs.new"
install -m 755 "$W/tb/src/tar" "$OUT/tar.new"
$STRIP "$OUT/mke2fs.new" "$OUT/tar.new"
mv "$OUT/mke2fs.new" "$OUT/mke2fs"
mv "$OUT/tar.new" "$OUT/tar"
echo "$stamp" > "$OUT/.stamp"
echo "   $OUT: mke2fs $(stat -c %s "$OUT/mke2fs") bytes, tar $(stat -c %s "$OUT/tar") bytes"
