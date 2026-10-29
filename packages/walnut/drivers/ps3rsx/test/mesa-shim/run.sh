#!/bin/bash
# Mesa nv30 as on the PS3, on the PC: mpv's GPU renderer (or any program) on the ppc64 build of
# our Mesa under qemu-user, with Mesa's nouveau drm-shim answering like ps3rsx (wrap.c), every
# push it submits written to a file for decode.py and the validator's fixtures.
#   run.sh DIR setup [MESA_DEBS_DIR]   the ppc64 build chroot in DIR/root, with mpv, gcc and our
#                                      Mesa (debs from MESA_DEBS_DIR, else build/debs/mesa)
#   run.sh DIR play CLIP FRAMES OUT    player.c on CLIP for FRAMES frames: pushes in DIR/OUT
#   run.sh DIR run OUT 'command'       any GL program under the shim (cwd /tmp), its pushes in DIR/OUT
#   run.sh DIR sh 'command'            anything else in the chroot (cwd /tmp), e.g. './browser.sh ...'
#                                      (Badwolf with GPU compositing: its header says what it needs)
# Outputs: OUT (every push Mesa submitted), OUT.bos, OUT.gpu (what ps3rsx would send to the GPU:
# the shim runs its validator and answers like it, wrap.c) and OUT.log (its verdicts). Environment
# passed in: MPVMSG, PS3SHIM_RECOVER, PS3SHIM_REJECT, PS3SHIM_VALIDATE, NV30_PS3_RECOVER.
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
cp "$here"/wrap.c "$here"/ps3val.c "$here"/player.c "$here"/browser.sh "$here"/../../src/validate.c \
	"$here"/../../src/ps3rsx.h "$D/in/"
rm -rf "$D/in/inc"; cp -r "$here"/../inc "$D/in/inc"
envs=()
for v in MPVMSG PS3SHIM_RECOVER PS3SHIM_REJECT PS3SHIM_VALIDATE NV30_PS3_RECOVER; do
	[ -z "${!v:-}" ] || envs+=("$v=${!v}")
done
inside() {
	docker run --rm --privileged -v "$D:/r" -v "$top/build/chroot:/c:ro" "$img" bash -c '
		set -e; R=/r/root
		cp /etc/resolv.conf $R/etc/resolv.conf 2>/dev/null || true
		mountpoint -q $R/proc || mount -t proc proc $R/proc
		mountpoint -q $R/dev || mount --bind /dev $R/dev
		rm -rf $R/tmp/debs; cp -r /r/in/* $R/tmp/
		rc=0; chroot $R /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/tmp \
			DEBIAN_FRONTEND=noninteractive "${@:2}" sh -c "cd /tmp && $1" || rc=$?
		umount $R/dev $R/proc; exit $rc' _ "$1" "${envs[@]}"
}
# wrap.so (with ps3val.c: ps3rsx's validator) and the shim's environment, then $1
shim='gcc -O1 -fPIC -c -Iinc -o ps3val.o ps3val.c &&
	gcc -O1 -shared -fPIC -I/usr/include/libdrm -o wrap.so wrap.c ps3val.o -ldl &&
	rm -f push.bin push.bin.bos push.bin.gpu push.bin.log && PUSHDUMP=/tmp/push.bin NOUVEAU_CHIPSET=47 \
	MESA_LOADER_DRIVER_OVERRIDE=nouveau LD_PRELOAD=/tmp/wrap.so:/usr/lib/powerpc64-linux-gnu/libnouveau_noop_drm_shim.so'
out() {
	for x in "" .bos .gpu .log; do cp "$D/root/tmp/push.bin$x" "$D/$1$x"; done
	echo ">> $D/$1"
}
case $what in
setup)
	debs=${1:-$top/build/debs/mesa}
	rm -rf "$D/in/debs"; mkdir -p "$D/in/debs"
	cp "$debs"/{libegl-mesa0,libgbm1,libgl1-mesa-dri,libglx-mesa0,mesa-drm-shim,mesa-libgallium}_*_ppc64.deb "$D/in/debs/"
	docker run --rm --privileged -v "$D:/r" -v "$top/build/chroot:/c:ro" "$img" \
		sh -c '[ -e /r/root/usr/bin/apt-get ] || { mkdir -p /r/root && tar -xf /c/sid-ppc64.tar -C /r/root; }'
	inside 'apt-get update -qq && apt-get install -y -q --no-install-recommends mpv libmpv2 gcc libc6-dev libdrm-dev libegl-dev libgl-dev libgles-dev libopengl-dev pkg-config >apt.log 2>&1 &&
		apt-get install -y -q --allow-downgrades ./debs/*.deb >>apt.log 2>&1 &&
		apt-get download libmpv-dev >>apt.log 2>&1 && rm -rf mpvdev && dpkg -x libmpv-dev_*.deb mpvdev &&
		dpkg -l libgl1-mesa-dri mpv | tail -2' ;;
play)
	cp "$1" "$D/in/clip"
	# (libmpv-dev itself would pull Debian's libgbm1 over ours: its headers only, from the deb)
	rc=0
	inside "gcc -O1 -Impvdev/usr/include -o player player.c /usr/lib/powerpc64-linux-gnu/libmpv.so.2 \$(pkg-config --cflags --libs egl gl) &&
		$shim MPVMSG=\${MPVMSG:-all=warn} timeout ${SHIM_TIMEOUT:-900} ./player /tmp/clip $2" || rc=$?
	out "$3"; exit $rc ;;
run)
	rc=0; inside "$shim $2" || rc=$?
	out "$1"; exit $rc ;;
sh)
	inside "$1" ;;
esac
exit
}
