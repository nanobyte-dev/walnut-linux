#!/bin/bash
# fluidsynth-compare.sh FLUIDSYNTH_TREE TEST_OUT: fluidsynth with the libspeq hook against
# unpatched fluidsynth, both built for the host (cmake, Release, as Debian builds it: doubles),
# rendering a generated MIDI file (tests/make-midi.py) through tests/fs-render.c, which writes in
# chunks of 1-3000 frames as a game does. FLUIDSYNTH_TREE is make fluidsynth-work's git tree
# (build/work/fluidsynth): the hook is its working tree, the unpatched build its base tag.
# The hook loads TEST_OUT/fake/libspeq.so.0 (make test-fluidsynth builds it: deadline.spu's ops on
# host threads, SPEQ_FAKE=1, every read checked against its DMA plan). Per interpolation (cubic,
# the default; linear; none; sinc, which libspeq leaves to fluidsynth), and once with reverb
# and chorus on:
#   SPEQ_SYNTH=off   the hook declines: must be identical (the fluid_rvoice_write split)
#   SPEQ_SYNTH=ppe   libspeq's DSP (single precision) on the calling thread
#   SPEQ_SYNTH=spu   the SPU path: rounds of blocks, jobs split across the workers
# ppe and spu must stay within MIN_SNR dB (100) of unpatched fluidsynth and of each other.
# Before the renders, tests/fs-kern.c checks libspeq's DSP block by block against fluidsynth's
# own functions (the control results identical, the samples within KERN_SNR, 90 dB: random
# states include high-pass custom filters at low cutoffs, where single precision cancels).
# Env: SF2 (/usr/share/soundfonts/FluidR3_GM.sf2), MIDI_SECONDS (60), GCEM_INCLUDE (gcem's
# headers: Debian strips them from fluidsynth's source; libgcem-dev's are found in
# build/downloads/gcem, fetched through the deb builder when missing).
set -euo pipefail
{
FS=$(realpath "$1")
TEST_OUT=$(realpath "$2")
OUT=$TEST_OUT/fluidsynth
HERE=$(dirname "$(realpath "$0")")
TOP=$(realpath "$HERE/../..")
SF2=${SF2:-/usr/share/soundfonts/FluidR3_GM.sf2}
SECS=${MIDI_SECONDS:-60}
MIN_SNR=${MIN_SNR:-100}
KERN_SNR=${KERN_SNR:-90}	# random voice states, deliberately harsh: high-pass filters at low cutoffs
mkdir -p "$OUT"

[ -e "$SF2" ] || { echo "!! no SoundFont $SF2 (SF2=...)"; exit 1; }
[ -s "$TEST_OUT/fake/libspeq.so.0" ] || { echo "!! no $TEST_OUT/fake/libspeq.so.0: make test-fluidsynth"; exit 1; }
GCEM=${GCEM_INCLUDE:-}
if [ -z "$GCEM" ]; then
	for d in /usr/include "$TOP/build/downloads/gcem/x/usr/include"; do
		[ -e "$d/gcem.hpp" ] && GCEM=$d && break
	done
fi
if [ -z "$GCEM" ]; then
	echo ">> gcem headers: libgcem-dev from the snapshot (deb builder)"
	img=$(docker images --format '{{.Repository}}:{{.Tag}}' | grep '^ps3-deb-builder:' | head -1)
	mkdir -p "$TOP/build/downloads/gcem/x"
	docker run --rm -v "$TOP/build/downloads/gcem:/out" -w /out "$img" \
		sh -c 'apt-get update -qq && apt-get download -qq libgcem-dev && chown '"$(id -u):$(id -g)"' *.deb'
	(cd "$TOP/build/downloads/gcem/x" && ar x ../libgcem-dev_*.deb && tar xf data.tar.*)
	GCEM=$TOP/build/downloads/gcem/x/usr/include
fi

build() {	# build SRC DIR
	local opts=(-DCMAKE_BUILD_TYPE=Release -DGCEM_INCLUDE_DIR="$GCEM" -Denable-floats=off
		-Denable-alsa=off -Denable-aufile=off -Denable-dbus=off -Denable-jack=off
		-Denable-ladspa=off -Denable-libsndfile=off -Denable-midishare=off -Denable-network=off
		-Denable-oss=off -Denable-portaudio=off -Denable-sdl3=off -Denable-pulseaudio=off
		-Denable-pipewire=off -Denable-readline=off -Denable-systemd=off -Denable-signalsmith=off
		-Denable-libinstpatch=off -DBUILD_SHARED_LIBS=on)
	if [ ! -e "$2/src/libfluidsynth.so" ] || [ "$1" = "$FS" ]; then
		cmake -S "$1" -B "$2" "${opts[@]}" > "$2.log" 2>&1 || { tail -20 "$2.log"; exit 1; }
		cmake --build "$2" -j"$(nproc)" --target libfluidsynth >> "$2.log" 2>&1 || { tail -30 "$2.log"; exit 1; }
	fi
	cc -O2 -I"$1/include" -I"$2/include" -o "$2/fs-render" "$HERE/fs-render.c" \
		-L"$2/src" -lfluidsynth -Wl,-rpath,"$2/src"
}

base=$(git -C "$FS" describe --tags --match 'base/*' --abbrev=0)
echo ">> unpatched fluidsynth ($base) and the hook ($FS), for the host"
if [ ! -e "$OUT/base/src/libfluidsynth.so" ] || [ ! -d "$OUT/base-src" ]; then
	rm -rf "$OUT/base-src" && mkdir -p "$OUT/base-src"
	git -C "$FS" archive "$base" | tar x -C "$OUT/base-src"
fi
build "$OUT/base-src" "$OUT/base"
build "$FS" "$OUT/hook"
python3 -I "$HERE/make-midi.py" "$OUT/test.mid" "$SECS"

fails=0
# libspeq's DSP against fluidsynth's own functions, block by block
S=$OUT/base-src
cc -O2 -std=gnu11 -I"$S/src" -I"$S/src/utils" -I"$S/src/rvoice" -I"$S/src/synth" -I"$S/src/sfloader" \
	-I"$S/src/midi" -I"$S/include" -I"$OUT/base/include" -I"$OUT/base" -I"$HERE/../lib" \
	-o "$OUT/base/fs-kern" "$HERE/fs-kern.c" "$HERE/../lib/synth.c" "$HERE/../lib/pool.c" "$HERE/../lib/queue.c" \
	"$HERE/../lib/spe.c" $(find "$OUT/base/src/CMakeFiles/libfluidsynth-OBJ.dir" -name '*.o') \
	-lstdc++ -fopenmp -lm -lpthread
echo "libspeq's DSP (single precision) against fluidsynth's DSP functions"
"$OUT/base/fs-kern" 200000 "$KERN_SNR" || { echo "FAIL libspeq's DSP against fluidsynth"; fails=$((fails + 1)); }

cmp() {	# cmp WHAT REF TEST ARGS
	local what=$1; shift
	echo "$what"
	python3 -I "$HERE/f32-compare.py" "$@" || { echo "FAIL $what"; fails=$((fails + 1)); }
}
render() {	# render NAME LIB INTERP FX [ENV...]
	local name=$1 lib=$2 interp=$3 fx=$4; shift 4
	env "$@" LD_LIBRARY_PATH="$TEST_OUT/fake" "$OUT/$lib/fs-render" "$SF2" "$OUT/test.mid" \
		"$OUT/$name.f32" "$interp" "$fx" 7 2> "$OUT/$name.log" || { cat "$OUT/$name.log"; exit 1; }
}
for v in 4:0 1:0 0:0 6:0 4:1; do
	i=${v%:*} fx=${v#*:} t=i$i-fx$fx
	echo ">> interpolation $i, reverb and chorus $fx"
	render "$t-base" base "$i" "$fx"
	render "$t-off" hook "$i" "$fx" SPEQ_SYNTH=off
	render "$t-ppe" hook "$i" "$fx" SPEQ_SYNTH=ppe
	render "$t-spu" hook "$i" "$fx" SPEQ_FAKE=1 SPEQ_SYNTH=spu SPEQ_DEBUG=1 SPEQ_SYNTH_TIMEOUT_US=10000000
	grep -h "speq: synth .* closed" "$OUT/$t-spu.log" | sed 's/^/  /' || true
	cmp "SPEQ_SYNTH=off = unpatched" "$OUT/$t-base.f32" "$OUT/$t-off.f32" --exact
	cmp "SPEQ_SYNTH=ppe ~ unpatched" "$OUT/$t-base.f32" "$OUT/$t-ppe.f32" --snr "$MIN_SNR"
	cmp "SPEQ_SYNTH=spu ~ unpatched" "$OUT/$t-base.f32" "$OUT/$t-spu.f32" --snr "$MIN_SNR"
	cmp "SPEQ_SYNTH=spu ~ ppe" "$OUT/$t-ppe.f32" "$OUT/$t-spu.f32" --snr "$MIN_SNR"
done
echo "$([ $fails = 0 ] && echo ok || echo FAIL): $fails failure$([ $fails = 1 ] || echo s)"
[ $fails = 0 ]
exit
}
