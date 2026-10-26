#!/bin/bash
# build-release.sh ID OUT stable|testing: a Walnut release image (docs/release/ALPHA.md M7) into OUT/,
# built on a GitHub runner (.github/workflows/release-image.yml) or the dev PC. The image is the
# public profile with our packages from that suite on walnut.nanobyte.dev, checked against its key.
# ID (github-ci/release-id.sh): A.B.C for stable, nightly-<date>-<time>-<commit> for testing.
#   walnut-ID.tar   the release archive: walnut-ID/ with the installer (walnut-install, lib/stamp.py),
#                   README.TXT (the install by hand), MANIFEST, SHA256SUMS, and what goes on the
#                   PS3 laid out like its two disks: dev_hdd0/linux/walnut.img.xz (the stamped 16 GiB
#                   image) and walnut-good/, dev_hdd1/kboot.conf, linux/boot/ (kernel, its arguments,
#                   initrd-walnut.img) and linux/install/ (rootfs.tar.xz, install.conf)
#   MANIFEST        every package in the image with its version (also in the archive)
#   SHA256SUMS      of both; the publish job signs it (SHA256SUMS.asc)
# No secrets here.
set -euo pipefail
cd "$(dirname "$0")/.."
ver=$1 out=$2 suite=$3
case $suite in
	stable) [[ $ver =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] ;;
	testing) [[ $ver =~ ^nightly-[0-9]{8}-[0-9]{4}-[0-9a-f]{7}$ ]] ;;
	*) false ;;
esac || { echo "!! usage: $0 A.B.C OUT stable | $0 nightly-<date>-<time>-<commit> OUT testing" >&2; exit 2; }
github-ci/runner-prep.sh

echo "::group::make rootfs (public, $suite)"
# a package layer restored from the CI cache (keyed on its inputs) is taken as it is, whatever its mtime
keep=()
[ ! -f build/rootfs/base.tar ] || keep=(-o build/rootfs/base.tar)
make "${keep[@]}" rootfs PROFILE=public WALNUT_SUITE="$suite" WALNUT_RELEASE="$ver"
echo "::endgroup::"
echo "::group::make initrd-walnut"
make initrd-walnut
echo "::endgroup::"

rm -rf "$out"
mkdir -p "$out"
tar=build/rootfs/debian-ppc64.tar
x=$(mktemp -d)
trap 'rm -rf "$x"' EXIT
# the kernel and its arguments as the image has them (the public profile drops video=)
tar -xf "$tar" -C "$x" --wildcards './boot/vmlinux-*' './usr/lib/ps3/boot/*.args' ./var/lib/dpkg/status
kernels=$(cd "$x/boot" && ls | grep -E '^vmlinux-[0-9][^ ]*$' | grep -v -e '-debug' -e '\.\(old\|bak\|args\)$' || true)
[ -n "$kernels" ] && [ "$(echo "$kernels" | wc -l)" = 1 ] || { echo "!! the image should have one release kernel: $kernels" >&2; exit 1; }
rel=${kernels#vmlinux-}
args=$x/usr/lib/ps3/boot/$rel.args
grep -q 'video=' "$args" && { echo "!! video= in the public kernel arguments" >&2; exit 1; }
awk '/^Package:/{p=$2} /^Status:/{s=$4} /^Version:/{v=$2} /^$/{if (s=="installed") print p, v; p=s=v=""}' \
	"$x/var/lib/dpkg/status" | LC_ALL=C sort > "$x/MANIFEST"

# nothing licensed, no dev account, no LAN repository in the image
if grep -qxF -f <(grep -v -e '^\s*#' -e '^\s*$' tools/licensed-packages.txt) <(cut -d' ' -f1 "$x/MANIFEST"); then
	echo "!! a licensed package in the image" >&2; exit 1
fi
tar -tf "$tar" | grep -q -e '^\./home/tibi' -e '^\./etc/sudoers\.d/tibi$' -e '^\./etc/apt/sources\.list\.d/ps3-lan' &&
	{ echo "!! dev profile files in the public image" >&2; exit 1; }

echo "::group::xz"
xz -T0 -9 -c "$tar" > "$x/rootfs.tar.xz"
echo "::endgroup::"
# the internal install carries it on dev_hdd1 (2047 MiB) next to the kernel and initramfs
mib=$(( $(stat -c %s "$x/rootfs.tar.xz") >> 20 ))
[ "$mib" -le 1800 ] || { echo "!! the rootfs tarball is $mib MiB: too big for dev_hdd1" >&2; exit 1; }

# The archive, laid out like the two disks (stamp.py prepare: the 16 GiB stamped image, xz, and
# install.conf for it; the boot files; kboot.conf), the installer and a README for doing it by hand.
# The image's uuid comes from the id: the same release, the same archive.
d=$x/walnut-$ver
echo "::group::the archive"
python3 tools/ssdimg/stamp.py prepare --rootfs "$x/rootfs.tar.xz" --kernel "$x/boot/vmlinux-$rel" --release "$rel" \
	--initrd build/initrd/initrd-walnut.img --args-file "$args" --size 16G --swap-mib 1024 \
	--uuid "$(python3 -c 'import sys, uuid; print(uuid.uuid5(uuid.NAMESPACE_URL, "https://walnut.nanobyte.dev/download/" + sys.argv[1]))' "$ver")" \
	--out "$d"
mkdir -p "$d/lib"
cp system/installer/walnut-install "$d/"
cp tools/ssdimg/stamp.py "$d/lib/"
cp LICENSE "$x/MANIFEST" "$d/"
sed -e "s/@ID@/$ver/g" -e "s/@RELEASE@/$rel/g" -e "s/@SUITE@/$suite/g" system/installer/README.release > "$d/README.TXT"
(cd "$d" && find . -type f ! -name SHA256SUMS -printf '%P\n' | LC_ALL=C sort | xargs -d '\n' sha256sum > SHA256SUMS)
python3 system/installer/walnut-install internal --release-dir "$d" --host 192.0.2.1 --dry-run > /dev/null
tar --sort=name --mtime="@$(git log -1 --format=%ct)" --owner=0 --group=0 --numeric-owner \
	-C "$x" -cf "$out/walnut-$ver.tar" "walnut-$ver"
echo "::endgroup::"
cp "$x/MANIFEST" "$out/"
(cd "$out" && sha256sum -- * > SHA256SUMS)
tar -tvf "$out/walnut-$ver.tar"
ls -la "$out"
cat "$out/SHA256SUMS"
