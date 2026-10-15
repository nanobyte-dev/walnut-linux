#!/bin/sh
# Installs what we build ourselves into a PS3 root filesystem:
#   tools/install-builds.sh <root> <part>...    parts: boot modules gpu mesa classicube spu
# build-image.sh puts all of it into the image; the *-deploy targets use it to update the live
# NFS root in place. Env: BUILD (build dir), MESA_PREFIX (Mesa's path on the PS3), KBOOT_CMDLINE.
set -eu
R="$1"; shift
B="${BUILD:-build}"
H="$R/home/tibi"

for part; do
	case "$part" in
	boot)		# the kernel, and Petitboot's boot menu for a disk root: Petitboot reads
		# /etc/kboot.conf on the partitions it can mount (ext4 included) and boots the
		# default entry after its countdown, without the network
		k=$(readlink "$B/kernel/stage/boot/vmlinux")
		install -d "$R/boot" "$R/etc"
		install -m 644 "$B/kernel/stage/boot/$k" "$R/boot/"
		printf '%s\n' "# Petitboot boot menu (tools/install-builds.sh)" "default=debian" \
			"debian='/boot/$k ${KBOOT_CMDLINE:?}'" > "$R/etc/kboot.conf" ;;
	modules)	# the kernel's modules
		install -d "$R/lib/modules"
		rsync -a --delete --chown=0:0 "$B/kernel/stage/lib/modules/" "$R/lib/modules/" ;;
	gpu)		# RSX driver, probe modules and test tools (ps3rsx.service loads ~/bin/ps3rsx.ko)
		install -d "$H/bin"
		install -m 755 "$B"/gpu/* "$B"/gpu-*/*.ko gpu/tools/*.sh "$H/bin/" ;;
	mesa)		# our Mesa (ld.so.conf.d/00-ps3mesa.conf points at it) and its GL tests
		install -d "$H/mesa/bin"
		rsync -a "$B/mesa/install$MESA_PREFIX/" "$H/mesa/"
		install -m 755 "$B"/mesa/tests/* "$H/mesa/bin/" ;;
	classicube)	# the game keeps options, maps and screenshots next to itself; default options
		# only if there are none yet
		install -d "$H/classicube/texpacks" "$H/classicube/audio" "$H/.local/share/applications"
		install -m 755 "$B/classicube/install/ClassiCube" games/classicube/classicube.sh "$H/classicube/"
		install -m 644 "$B/classicube/install/texpacks/classicube.zip" "$H/classicube/texpacks/"
		install -m 644 "$B"/classicube/audio/* "$H/classicube/audio/"
		[ -e "$H/classicube/options.txt" ] ||
			install -m 644 games/classicube/options.txt "$H/classicube/options.txt"
		install -m 644 games/classicube/classicube.desktop "$H/.local/share/applications/" ;;
	spu)		# SPU test programs and spurun (spu/), for the Cell's SPEs
		install -d "$H/spu"
		install -m 755 "$B"/spu-tests/* "$H/spu/" ;;
	*)
		echo "unknown part: $part" >&2; exit 1 ;;
	esac
done

# Building the image runs as root: the home directory belongs to tibi
if [ "$(id -u)" = 0 ]; then
	chown -R 1000:1000 "$H"
fi
