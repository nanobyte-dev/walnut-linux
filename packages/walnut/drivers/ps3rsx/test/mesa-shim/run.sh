#!/bin/bash
# Mesa nv30 as on the PS3, on the PC: mpv's GPU renderer (or any program) on the ppc64 build of
# our Mesa under qemu-user, with Mesa's nouveau drm-shim answering like ps3rsx (wrap.c), every
# push it submits written to a file for decode.py and the validator's fixtures.
#   run.sh DIR setup [MESA_DEBS_DIR]   the ppc64 build chroot in DIR/root, with mpv, gcc and our
#                                      Mesa (debs from MESA_DEBS_DIR, else build/debs/mesa)
#   run.sh DIR play CLIP FRAMES OUT    player.c on CLIP for FRAMES frames: pushes in DIR/OUT
#   run.sh DIR sh 'command'            anything else in the chroot (cwd /tmp)
# Runs in the deb builder (privileged docker, as tools/deb-build.sh does); DIR under build/tmp.
# The chroot is root-owned: remove it with run.sh DIR sh 'true' && docker ... rm, or make
# clean-ephemeral.
set -euo pipefail
{
D=$(realpath "$1"); shift; what=$1; shift
here=$(dirname "$(realpath "$0")"); top=$(realpath "$here/../../../../../..")
img=$(docker images --format '{{.Repository}}:{{.Tag}}' | grep '^ps3-deb-builder:' | head -1)
[ -n "$img" ] || { echo "no ps3-deb-builder image (make builder)" >&2; exit 1; }
mkdir -p "$D/in"
cp "$here"/wrap.c "$here"/player.c "$D/in/"
inside() {
	docker run --rm --privileged -v "$D:/r" -v "$top/build/chroot:/c:ro" "$img" bash -c '
		set -e; R=/r/root
		cp /etc/resolv.conf $R/etc/resolv.conf 2>/dev/null || true
		mountpoint -q $R/proc || mount -t proc proc $R/proc
		mountpoint -q $R/dev || mount --bind /dev $R/dev
		cp /r/in/* $R/tmp/
		rc=0; chroot $R /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/tmp \
			DEBIAN_FRONTEND=noninteractive sh -c "cd /tmp && $1" || rc=$?
		umount $R/dev $R/proc; exit $rc' _ "$1"
}
case $what in
setup)
	debs=${1:-$top/build/debs/mesa}
	cp "$debs"/{libegl-mesa0,libgbm1,libgl1-mesa-dri,libglx-mesa0,mesa-drm-shim,mesa-libgallium}_*_ppc64.deb "$D/in/"
	docker run --rm --privileged -v "$D:/r" -v "$top/build/chroot:/c:ro" "$img" \
		sh -c '[ -e /r/root/usr/bin/apt-get ] || { mkdir -p /r/root && tar -xf /c/sid-ppc64.tar -C /r/root; }'
	inside 'apt-get update -qq && apt-get install -y -q --no-install-recommends mpv gcc libc6-dev libdrm-dev libegl-dev libgl-dev pkg-config >apt.log 2>&1 &&
		apt-get install -y -q --allow-downgrades ./*.deb >>apt.log 2>&1 &&
		apt-get download libmpv-dev >>apt.log 2>&1 && rm -rf mpvdev && dpkg -x libmpv-dev_*.deb mpvdev &&
		dpkg -l libgl1-mesa-dri mpv | tail -2' ;;
play)
	cp "$1" "$D/in/clip"
	# (libmpv-dev itself would pull Debian's libgbm1 over ours: its headers only, from the deb)
	inside "gcc -O1 -shared -fPIC -I/usr/include/libdrm -o wrap.so wrap.c -ldl &&
		gcc -O1 -Impvdev/usr/include -o player player.c /usr/lib/powerpc64-linux-gnu/libmpv.so.2 \$(pkg-config --cflags --libs egl gl) &&
		rm -f push.bin push.bin.bos && PUSHDUMP=/tmp/push.bin NOUVEAU_CHIPSET=47 MESA_LOADER_DRIVER_OVERRIDE=nouveau \
		LD_PRELOAD=/tmp/wrap.so:/usr/lib/powerpc64-linux-gnu/libnouveau_noop_drm_shim.so \
		MPVMSG=\${MPVMSG:-all=warn} ./player /tmp/clip $2"
	cp "$D/root/tmp/push.bin" "$D/$3"; cp "$D/root/tmp/push.bin.bos" "$D/$3.bos"; echo ">> $D/$3" ;;
sh)
	inside "$1" ;;
esac
exit
}
