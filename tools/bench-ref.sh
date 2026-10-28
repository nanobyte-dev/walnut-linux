#!/bin/bash
# bench-ref.sh GAME [VARIANT]: the PC reference of a bench recipe, for the PS3 | PC screenshot
# sheet (bench/analysis/shots.py, docs/games/SUITE.md "Screenshots"). Runs the recipe through the same
# client.sh in an amd64 container (build-containers/Dockerfile.benchref: Debian sid from the PS3's snapshot) under
# Xvfb at 1920x1080 with Mesa's llvmpipe forced to the RSX's GL 2.1 / GLSL 1.20
# (MESA_GL_VERSION_OVERRIDE=2.1, MESA_GLSL_VERSION_OVERRIDE=120), no sound device (SDL's dummy driver,
# OpenAL Soft's null backend), and takes the screenshots at the same shot points after the same game
# state (the recipe's SHOT_POINTS, SHOT_FROM, SHOT_RE; SHOTS="30 90" overrides). The game comes from the
# recipe's REF_PKGS (Debian packages: the games we take from Debian) or REF_IMAGE (a local image with our
# amd64 build of a game we patch, e.g. wz-amd64-view); REF_ENV holds extra NAME=VALUE for the PC run
# (a shorter key wait). The image is built once per base and package list (ps3-bench-ref:<hash>).
# Output: build/bench-ref/<game>-<variant>/ (client.sh's run directory: bench.txt, game.log, hud/,
# shots/*.png, versions) and build/bench-ref/<game>-<variant>-<s>s.png. A previous reference is moved to
# build/bench-ref/old/<stamp>-<game>-<variant>/ first. Then `make bench-shots` (shots.py post) makes the
# sheets of the pulled PS3 runs against it.
# The container sees bench/ (/bench: harness/client.sh, lib/, analysis/) and the game's benchmark directory (gamedb/games/<id>/benchmark/ as
# /games/<id>, BENCH_GAMES), both read-only. It is named shot-ref-<game>-<pid> and removed at the end;
# nothing else is touched.
# (bench-ref.sh --inside GAME VARIANT DURATION is the part that runs in the container.)
{
set -euo pipefail

if [ "${1:-}" = --inside ]; then	# in the container: Xvfb, then client.sh as on the PS3
	G=$2; V=$3; DUR=$4
	mkdir -p "$HOME" "$HOME/.local/share" "$HOME/.config" /out/run	# XDG dirs a fresh home lacks (Widelands won't start without ~/.local/share)
	Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp > /out/xvfb.log 2>&1 &
	for i in $(seq 100); do [ -e /tmp/.X11-unix/X99 ] && break; sleep 0.1; done
	export DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe MESA_GL_VERSION_OVERRIDE=2.1 \
		MESA_GLSL_VERSION_OVERRIDE=120 SDL_AUDIODRIVER=dummy ALSOFT_DRIVERS=null
	export BENCH_DIR=/out/run BENCH_SHOT_DIR=/out/run/shots BENCH_SHOTS="$SHOTS" BENCH_REF=1 BENCH_BACKUP=/tmp/bench-backup
	rc=0; timeout $((DUR + 60)) bash /bench/harness/client.sh "$G" "$V" > /out/run/out.log 2>&1 || rc=$?
	echo "client.sh: rc $rc" >> /out/run/out.log
	# the builds, as run.sh's versions (the game's package from its command, Mesa)
	p=$( (BENCH=/bench; . "$BENCH/harness/games.sh"; bench_use "$G"; CMD=(); ENV=(); . "$(bench_recipe "$G")"
		declare -F "variant_$V" > /dev/null && "variant_$V"; readlink -f "$(command -v "${CMD[0]}")") 2>/dev/null || true)
	dpkg-query -W $(dpkg -S "$p" 2>/dev/null | cut -d: -f1 | tr , ' ') mesa-libgallium > /out/run/versions 2>/dev/null || true
	exit 0
fi

cd "$(dirname "$0")/.."
G=${1:?game}; V=${2:-base}
BENCH=$PWD/bench; . bench/harness/games.sh	# the game's benchmark directory (gamedb/games/<id>/benchmark/)
G=$(bench_id "$G") || { echo "!! no recipe for $1 (bench/harness/run.sh list)"; exit 2; }
R=$(bench_recipe "$G"); GD=$(bench_game_dir "$G")
# the recipe's PC settings (sourced as client.sh does, in a subshell)
REF_PKGS=; REF_IMAGE=; REF_ENV=; DURATION=270
eval "$(bash -c 'BENCH=$1; . "$BENCH/harness/games.sh"; bench_use "$2"; CMD=(); ENV=(); BACKUP=(); DATA_DIRS=(); REF_PKGS=; REF_IMAGE=
	REF_ENV=; DURATION=270; setup() { :; }; parse() { :; }
	. "$(bench_recipe "$2")" > /dev/null || exit 2
	if declare -F "variant_$3" > /dev/null; then "variant_$3"; elif [ "$3" != base ]; then echo "echo \"!! no variant $3\" >&2; exit 2"; exit; fi
	declare -p REF_PKGS REF_IMAGE REF_ENV DURATION' _ "$PWD/bench" "$G" "$V")"
[ -n "$REF_PKGS$REF_IMAGE" ] || { echo "!! $R has no REF_PKGS (Debian packages) or REF_IMAGE (an image with the game)"; exit 2; }
SNAPSHOT=$(sed -n 's/^DEBIAN_SNAPSHOT *:= *//p' config.mk)
BASE=${REF_IMAGE:-$(sed -n 's/^BUILDER_BASE *:= *//p' config.mk)}
if [ -n "$REF_IMAGE" ]; then
	BID=$(docker image inspect -f '{{.Id}}' "$REF_IMAGE" 2>/dev/null) || { echo "!! no image $REF_IMAGE here (REF_IMAGE of $R)"; exit 2; }
else BID=$BASE; fi
TAG=ps3-bench-ref:$( (cat build-containers/Dockerfile.benchref; echo "$BID $SNAPSHOT $REF_PKGS") | sha256sum | cut -c1-12)
if ! docker image inspect "$TAG" > /dev/null 2>&1; then
	echo ">> building $TAG (base $BASE, packages: ${REF_PKGS:-none})"
	docker build -q -t "$TAG" -f build-containers/Dockerfile.benchref --build-arg BASE="$BASE" --build-arg SNAPSHOT="$SNAPSHOT" \
		--build-arg PKGS="$REF_PKGS" build-containers > /dev/null
fi
OUT=build/bench-ref; N=$G-$V; W=$OUT/.new-$N
mkdir -p "$OUT"; rm -rf "$W"; mkdir -p "$W"
NAME=shot-ref-$G-$$
trap 'docker rm -f "$NAME" > /dev/null 2>&1 || true; rm -rf "$W"' EXIT
envs=()
for e in $REF_ENV; do envs+=(-e "$e"); done
echo ">> $G $V on the PC (llvmpipe, GL 2.1), shots ${SHOTS:-auto}: at most $((DURATION + 60)) s"
docker run --rm --init --name "$NAME" --user "$(id -u):$(id -g)" -e HOME=/tmp/home -e SHOTS="${SHOTS:-auto}" "${envs[@]}" \
	-v "$PWD/bench:/bench:ro" -v "$GD:/games/$G:ro" -e BENCH_GAMES=/games \
	-v "$PWD/tools/bench-ref.sh:/bench-ref.sh:ro" -v "$PWD/$W:/out" \
	"$TAG" bash /bench-ref.sh --inside "$G" "$V" "$DURATION" || echo "!! the container failed"
[ -s "$W/run/bench.txt" ] || { echo "!! no bench.txt: $(tail -5 "$W/run/out.log" 2>/dev/null)"; exit 1; }
python3 bench/analysis/shots.py convert "$W/run" > /dev/null
if [ -e "$OUT/$N" ]; then	# the previous reference kept (old/ is ephemeral: make clean-ephemeral may go)
	mkdir -p "$OUT/old"; s=$(date -u +%Y%m%dT%H%M%SZ)
	mv "$OUT/$N" "$OUT/old/$s-$N"
	for f in "$OUT/$N"-*s.png; do [ -e "$f" ] && mv "$f" "$OUT/old/$s-${f##*/}"; done
fi
mv "$W/run" "$OUT/$N"
for f in "$OUT/$N"/shots/shot-*s.png; do [ -e "$f" ] && cp "$f" "$OUT/$N-${f##*/shot-}"; done
grep -E '^(ended|seconds|load_end|hud_t1|shots|shot_ref|shot|shot_skipped|shot_err)=' "$OUT/$N/bench.txt" | sed 's/^/   /'
ls "$OUT/$N"-*s.png 2>/dev/null | sed 's/^/>> /' || echo "!! no screenshots: $OUT/$N/out.log, game.log"
exit 0
}
