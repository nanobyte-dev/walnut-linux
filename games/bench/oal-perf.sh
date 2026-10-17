#!/bin/bash
# oal-perf.sh RUN_DIR...: the M0 split of OpenAL Soft's mixing (docs/SPU-AUDIO.md, section 2) from
# the perf.data an OpenAL variant recorded with OAL_PERF=N (recipes/openal.inc), on the PS3 as tibi
# with libopenal1-dbgsym installed (without it libopenal's internal functions are [unknown]).
# Per run: the shares of all the game's samples by DSO, by thread, libopenal by symbol, and the
# groups M0 asks for. Go/no-go: the SPU path is worth building when "voice mixing" (Voice::mix with
# everything it calls) is >= 6% of a game's samples.
{
set -u
for d in "$@"; do
	f=$d; [ -d "$d" ] && f=$d/perf.data
	[ -s "$f" ] || { echo "!! $f: no perf.data"; continue; }
	[ -O "$f" ] || sudo -n chown "$(id -u):$(id -g)" "$f"
	echo "=================== $d"
	[ -e "$d/bench.txt" ] && grep -E '^(game|variant|fps|oal_backend|oal_device)=' "$d/bench.txt" | tr '\n' ' ' && echo
	rep() { perf report -i "$f" --stdio -g none --percentage absolute "$@" 2>/dev/null | grep -E '^ +[0-9.]+%'; }
	echo "-- by DSO (self, % of all the game's samples)"
	rep --no-children --sort dso | head -12
	echo "-- by thread (alsoft-mixer is OpenAL's mixer thread)"
	rep --no-children --sort comm | head -8
	lib=$(rep --no-children --sort dso | grep -o 'libopenal[^ ]*' | head -1)
	[ -n "$lib" ] || { echo "!! no libopenal samples: was the game playing through OpenAL?"; continue; }
	echo "-- $lib by symbol (self)"
	rep --no-children --sort sym --dsos "$lib" | head -30
	# groups by symbol name (self %), then Voice::mix inclusive (children: libc copies included)
	rep --no-children --sort dso,sym | awk -v lib="$lib" '
		{ p = $1; sub(/%/, "", p); line = $0 }
		$2 == lib && line ~ /Voice::mix\(|Resample_[A-Za-z0-9]+_C|Mix_C\(|LoadSamples|LoadBuffer(Static|Queue|Callback)|DoFilters/ { mix += p; next }
		$2 == lib && line ~ /Calc(Attn|NonAttn)?VoiceParams|CalcPanningAndFilters|ProcessParamUpdates|ProcessVoiceChanges|CalcContextParams|CalcEffectSlotParams|PrepareResampler|ComputePanGains|CalcAmbiCoeffs|CalcDirectionCoeffs/ { par += p; next }
		$2 == lib && line ~ /BFormatDec::process|Write<|Compressor|ApplyDither|ApplyDistanceComp/ { post += p; next }
		$2 == lib { other += p; next }
		$2 ~ /^libasound/ { asnd += p; next }
		$2 ~ /kernel/ { kern += p }
		END {
			printf "voice mixing, self (Voice::mix, Resample_*_C, Mix_C, LoadSamples, LoadBuffer*): %.2f%%\n", mix
			printf "parameter updates (ProcessParamUpdates, Calc*VoiceParams, panning): %.2f%%\n", par
			printf "post-process and output (BFormatDec::process, Write<>, limiter, dither): %.2f%%\n", post
			printf "rest of libopenal: %.2f%%   libasound: %.2f%%   kernel, all threads: %.2f%%\n", other, asnd, kern
		}'
	inc() { rep --children --sort sym --dsos "$lib" | grep -m1 "$1" | awk '{ sub(/%/, "", $1); print $1 }'; }
	echo "-- inclusive (with callees): BFormatDec::process $(inc 'BFormatDec::process')% (its Mix_C calls," \
		"the ambisonic decode 3 -> 2, are in the flat Mix_C above), ProcessParamUpdates $(inc 'ProcessParamUpdates')%"
	v=$(inc 'Voice::mix(')
	echo "-- voice mixing, inclusive (Voice::mix with its callees): ${v:-?}% of the game's samples"
	if [ -n "$v" ]; then
		awk -v v="$v" 'BEGIN { print (v >= 6 ? "GO: >= 6%, the SPU path is worth building for this game" : "NO-GO: < 6% for this game") }'
	fi
done
exit
}
