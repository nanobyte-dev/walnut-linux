#!/bin/bash
# build-release.sh VERSION OUT [SUITE]: a Walnut release (docs/release/ALPHA.md M7) into OUT/, built
# on a GitHub runner (.github/workflows/release-image.yml) or the dev PC. The image is the public
# profile with our packages from SUITE (stable) on walnut.nanobyte.dev, checked against its key.
#   walnut-VERSION-ppc64.tar.xz   the root filesystem (the installer's rootfs tarball)
#   vmlinux-<release>             its release kernel, and <release>.args, the kernel's own arguments
#   initrd-walnut.img             the internal install's generic initramfs
#   walnut-installer-VERSION.tar.gz  installer/ and the stamp tool it uses (runs from the unpacked tree)
#   MANIFEST                      every package in the image with its version
#   SHA256SUMS                    of all of the above; the publish job signs it (SHA256SUMS.asc)
# No secrets here.
set -euo pipefail
cd "$(dirname "$0")/../.."
ver=$1 out=$2 suite=${3:-stable}
[[ $ver =~ ^[0-9][A-Za-z0-9.~+-]*$ ]] || { echo "!! bad version $ver" >&2; exit 2; }
tools/ci/runner-prep.sh

echo "::group::make rootfs (public, $suite)"
# a package layer restored from the CI cache (keyed on its inputs) is taken as it is, whatever its mtime
keep=()
[ ! -f build/rootfs/base.tar ] || keep=(-o build/rootfs/base.tar)
make "${keep[@]}" rootfs PROFILE=public WALNUT_SUITE="$suite"
echo "::endgroup::"
echo "::group::make initrd-walnut"
make initrd-walnut
echo "::endgroup::"

rm -rf "$out"
mkdir -p "$out"
tar=build/rootfs/debian-ppc64.tar
# the kernel and its arguments as the image has them (the public profile drops video=)
x=$(mktemp -d)
trap 'rm -rf "$x"' EXIT
tar -xf "$tar" -C "$x" --wildcards './boot/vmlinux-*' './usr/lib/ps3/boot/*.args' ./var/lib/dpkg/status
kernels=$(cd "$x/boot" && ls | grep -E '^vmlinux-[0-9][^ ]*$' | grep -v -e '-debug' -e '\.\(old\|bak\|args\)$' || true)
[ -n "$kernels" ] && [ "$(echo "$kernels" | wc -l)" = 1 ] || { echo "!! the image should have one release kernel: $kernels" >&2; exit 1; }
rel=${kernels#vmlinux-}
cp "$x/boot/vmlinux-$rel" "$out/"
cp "$x/usr/lib/ps3/boot/$rel.args" "$out/"
grep -q 'video=' "$out/$rel.args" && { echo "!! video= in the public kernel arguments" >&2; exit 1; }
awk '/^Package:/{p=$2} /^Status:/{s=$4} /^Version:/{v=$2} /^$/{if (s=="installed") print p, v; p=s=v=""}' \
	"$x/var/lib/dpkg/status" | LC_ALL=C sort > "$out/MANIFEST"
cp build/initrd/initrd-walnut.img "$out/"

# nothing licensed, no dev account, no LAN repository in the image
if grep -qxF -f <(grep -v -e '^\s*#' -e '^\s*$' tools/licensed-packages.txt) <(cut -d' ' -f1 "$out/MANIFEST"); then
	echo "!! a licensed package in the image" >&2; exit 1
fi
tar -tf "$tar" | grep -q -e '^\./home/tibi' -e '^\./etc/sudoers\.d/tibi$' -e '^\./etc/apt/sources\.list\.d/ps3-lan' &&
	{ echo "!! dev profile files in the public image" >&2; exit 1; }

echo "::group::xz"
xz -T0 -9 -c "$tar" > "$out/walnut-$ver-ppc64.tar.xz"
echo "::endgroup::"
# the internal install carries it on dev_hdd1 (2047 MiB) next to the kernel and initramfs
mib=$(( $(stat -c %s "$out/walnut-$ver-ppc64.tar.xz") >> 20 ))
[ "$mib" -le 1800 ] || { echo "!! the rootfs tarball is $mib MiB: too big for dev_hdd1" >&2; exit 1; }
inst=walnut-installer-$ver
mkdir -p "$x/$inst/installer" "$x/$inst/tools/ssdimg"
cp installer/walnut-install installer/README.md "$x/$inst/installer/"
cp tools/ssdimg/stamp.py "$x/$inst/tools/ssdimg/"
cp LICENSE "$x/$inst/"
tar --sort=name --mtime="@$(git log -1 --format=%ct)" --owner=0 --group=0 --numeric-owner \
	-C "$x" -czf "$out/$inst.tar.gz" "$inst"

(cd "$out" && sha256sum -- * > SHA256SUMS)
ls -la "$out"
cat "$out/SHA256SUMS"
