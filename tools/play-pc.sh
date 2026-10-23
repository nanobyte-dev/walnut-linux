#!/bin/bash
# play-pc.sh GAME [LAUNCHER]: the PC side of a played session, for playing a game here while it is
# played on the PS3 (docs/games/SUITE.md "Played sessions"): the picture and behaviour to compare with.
# Nothing is measured here; the PS3 session is the test.
# The game's packages are its Walnut Games entry's (gamedb/content/games/GAME/index.md `packages`),
# from Debian sid at the PS3's snapshot, in a container (docker/Dockerfile.playpc, built once per
# package list: ps3-play-pc:<hash>; a recipe's REF_IMAGE is the base for a game we patch). It shows on
# this desktop's X server (XWayland) with Mesa's llvmpipe held to the RSX's GL 2.1 / GLSL 1.20, and
# plays sound through the desktop's PulseAudio/PipeWire socket. LAUNCHER is chosen as on the PS3
# (games/bench/launcher.py: a menu entry's file name or Name; ps3-desktop's own entries count, like
# OpenArena's GL 1.2 and GL 2.0); without it, the first.
# The game's home (settings, saves) stays in build/pc-play/GAME between runs.
#   PLAY_PC_DRY=1   print the image and the docker command only
#   PLAY_PC_LIST=1  list the launchers and stop
{
set -euo pipefail
cd "$(dirname "$0")/.."
G=${1:?usage: play-pc.sh GAME [LAUNCHER]}; L=${2:-}
idx=gamedb/content/games/$G/index.md
[ -e "$idx" ] || { echo "!! no $idx (a Walnut Games entry)" >&2; exit 2; }
PKGS=$(python3 -I -c 'import sys, tomllib; t = open(sys.argv[1]).read().split("+++")[1]; print(" ".join(tomllib.loads(t)["packages"]))' "$idx")
REF_IMAGE=
[ -e "games/bench/recipes/$G.sh" ] && REF_IMAGE=$(bash -c 'here=$1; ASSETS=$here/assets; CMD=(); ENV=(); REF_IMAGE=
	setup() { :; }; parse() { :; }; . "$here/recipes/$2.sh" > /dev/null 2>&1; echo "$REF_IMAGE"' _ "$PWD/games/bench" "$G")
SNAPSHOT=$(sed -n 's/^DEBIAN_SNAPSHOT *:= *//p' config.mk)
BASE=${REF_IMAGE:-$(sed -n 's/^BUILDER_BASE *:= *//p' config.mk)}
if [ -n "$REF_IMAGE" ]; then
	BID=$(docker image inspect -f '{{.Id}}' "$REF_IMAGE" 2> /dev/null) || { echo "!! no image $REF_IMAGE here (the recipe's REF_IMAGE)" >&2; exit 2; }
	APT_PKGS=	# the image holds our build of the game
else BID=$BASE; APT_PKGS=$PKGS; fi
TAG=ps3-play-pc:$( (cat docker/Dockerfile.playpc; echo "$BID $SNAPSHOT $APT_PKGS") | sha256sum | cut -c1-12)
if [ "${PLAY_PC_DRY:-0}" != 1 ] && ! docker image inspect "$TAG" > /dev/null 2>&1; then
	echo ">> building $TAG (base $BASE, packages: ${APT_PKGS:-from the base image})"
	docker build -q -t "$TAG" -f docker/Dockerfile.playpc --build-arg BASE="$BASE" --build-arg SNAPSHOT="$SNAPSHOT" \
		--build-arg PKGS="$APT_PKGS" docker > /dev/null
fi

home=build/pc-play/$G
mkdir -p "$home"
pulse=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pulse/native
args=(--rm --init --name "play-pc-$G" --user "$(id -u):$(id -g)" --ipc=host --security-opt label=disable
	-e HOME=/home/player -v "$PWD/$home:/home/player" -e PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/games:/usr/games
	-e DISPLAY="${DISPLAY:-:0}" -v /tmp/.X11-unix:/tmp/.X11-unix:ro
	-e LIBGL_ALWAYS_SOFTWARE=1 -e GALLIUM_DRIVER=llvmpipe -e MESA_GL_VERSION_OVERRIDE=2.1 -e MESA_GLSL_VERSION_OVERRIDE=120
	-v "$PWD/games/bench/launcher.py:/walnut/launcher.py:ro" -v "$PWD/ps3-desktop/files/applications:/walnut/menu/applications:ro")
[ -n "${XAUTHORITY:-}" ] && [ -r "$XAUTHORITY" ] && args+=(-e XAUTHORITY=/walnut/xauth -v "$XAUTHORITY:/walnut/xauth:ro")
[ -S "$pulse" ] && args+=(-e PULSE_SERVER=unix:/walnut/pulse -v "$pulse:/walnut/pulse")
# the launcher (as on the PS3: ps3-desktop's entries first), its command started without ps3-game
inside='set -e
python3 -I /walnut/launcher.py --runtime-dir /walnut/menu ${L:+--choose "$L"} --argv-out /tmp/argv "$@" > /tmp/list
if [ "${PLAY_PC_LIST:-0}" = 1 ]; then tail -n +2 /tmp/list; exit 0; fi
echo ">> $(head -1 /tmp/list | cut -f2)"
mapfile -d "" -t argv < /tmp/argv
mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg
cd "$HOME"; exec "${argv[@]}"'
cmd=(docker run "${args[@]}" -e L="$L" -e PLAY_PC_LIST="${PLAY_PC_LIST:-0}" "$TAG" bash -c "$inside" _ $PKGS)
if [ "${PLAY_PC_DRY:-0}" = 1 ]; then printf '%q ' "${cmd[@]}"; echo; exit 0; fi
docker rm -f "play-pc-$G" > /dev/null 2>&1 || true
exec "${cmd[@]}"
}
