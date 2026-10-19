#!/bin/bash
# snd-ps3-ko.sh RELEASE: sound/ppc/snd_ps3.ko built out of tree for the kernel RELEASE (e.g.
# 6.12.111+ps3.472), for trying a snd_ps3 patch on the booted kernel without a new kernel and a
# reboot (make snd-ps3-ko REL=...; runs in the deb builder, whose cross compiler built our kernels).
# Source: build/src/linux's snd_ps3 (make linux-src) plus every patch of linux/patches/series that
# touches sound/ppc and is not in that tree yet. Config and Module.symvers: RELEASE's linux-image and
# linux-headers debs in build/debs/linux (only read: it may hold release candidates).
# Out: build/debs/snd-ps3/RELEASE/snd_ps3.ko, modinfo.txt, build.log. Work tree build/tmp/snd-ps3-ko,
# removed at the end.
# On the PS3, with nothing playing (desktop and games stopped; the user's OK, it is an install):
#   sudo install -D -m 644 snd_ps3.ko /lib/modules/RELEASE/updates/snd_ps3.ko && sudo depmod -a RELEASE
#   sudo modprobe -r snd_ps3 && sudo modprobe snd_ps3
# Back to the kernel's own: sudo rm /lib/modules/RELEASE/updates/snd_ps3.ko, depmod, reload.
set -euo pipefail
{
REL=${1:?usage: snd-ps3-ko.sh RELEASE (e.g. 6.12.111+ps3.472)}
SRC=$(ls -d build/src/linux/linux-*/ 2>/dev/null | head -1)
[ -n "$SRC" ] || { echo "!! no build/src/linux tree: make linux-src"; exit 1; }
R=$PWD; SRC=$R/${SRC%/}
img=$(ls build/debs/linux/linux-image-"${REL}"_*.deb 2>/dev/null | head -1)
hdr=$(ls build/debs/linux/linux-headers-"${REL}"_*.deb 2>/dev/null | head -1)
[ -n "$img" ] && [ -n "$hdr" ] || { echo "!! linux-image/linux-headers $REL not in build/debs/linux"; exit 1; }
W=$PWD/build/tmp/snd-ps3-ko; OUT=$PWD/build/debs/snd-ps3/$REL
rm -rf "$W"; mkdir -p "$W/kb" "$W/m/sound/ppc" "$OUT"
debx() {	# debx DEB MEMBER: a file of the deb's data to stdout
	local m; m=$(ar t "$1" | grep '^data\.tar')
	ar p "$1" "$m" | { case $m in *.zst) zstd -dc ;; *.xz) xz -dc ;; *.gz) gzip -dc ;; *) cat ;; esac; } | tar -xO "$2"
}
debx "$img" "./boot/config-$REL" > "$W/kb/.config"
debx "$hdr" "./usr/src/linux-headers-$REL/Module.symvers" > "$W/kb/Module.symvers"
cp "$SRC"/sound/ppc/snd_ps3.c "$SRC"/sound/ppc/snd_ps3.h "$SRC"/sound/ppc/snd_ps3_reg.h "$W/m/sound/ppc/"
applied=
while read -r p; do
	case $p in ''|'#'*) continue ;; esac
	grep -q '^+++ b/sound/ppc/' "linux/patches/$p" || continue
	if (cd "$W/m" && patch -p1 -R -s -f --dry-run < "$R/linux/patches/$p" > /dev/null 2>&1); then
		echo ">> $p: in the tree already"
	else
		(cd "$W/m" && patch -p1 -s < "$R/linux/patches/$p"); applied="$applied $p"
	fi
done < linux/patches/series
echo 'obj-m := snd_ps3.o' > "$W/m/sound/ppc/Kbuild"
kv=$(make -s -C "$SRC" kernelversion)
mk=(make -C "$SRC" ARCH=powerpc CROSS_COMPILE=powerpc64-linux-gnu- O="$W/kb" LOCALVERSION="${REL#"$kv"}")
{
	"${mk[@]}" olddefconfig
	"${mk[@]}" -j"${JOBS:-8}" modules_prepare
	[ "$(cat "$W/kb/include/config/kernel.release")" = "$REL" ] || { echo "!! kernel.release is not $REL"; exit 1; }
	"${mk[@]}" M="$W/m/sound/ppc" W=1 modules
} > "$OUT/build.log" 2>&1 || { tail -30 "$OUT/build.log"; exit 1; }
if grep -E 'warning:|error:' "$OUT/build.log"; then echo "!! warnings (build.log)"; fi
cp "$W/m/sound/ppc/snd_ps3.ko" "$OUT/"
{ echo "release=$REL"; echo "patches=${applied# }"; modinfo "$OUT/snd_ps3.ko" 2>/dev/null ||
	powerpc64-linux-gnu-readelf -p .modinfo "$OUT/snd_ps3.ko"; } > "$OUT/modinfo.txt"
rm -rf "$W"
echo ">> $OUT/snd_ps3.ko (patches:${applied:- none})"
exit 0
}
