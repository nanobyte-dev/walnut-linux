#!/bin/bash
# step0.sh <what>: the SPU offload plan's step 0 (docs/SPU-OFFLOAD-SURVEY.md "Measured: step 0"),
# every run through ps3-bench.sh (desktop stopped, caches dropped, swap emptied). Copy this
# directory to the PS3 (bench.dm_71 into ~/.openarena/baseoa/demos/) and run as tibi over SSH.
#   step0.sh smoke        glxinfo and one timedemo under the bare X: does the setup work?
#   step0.sh tc1-first    the first DXT run ever on this RSX, VRAM swap off; check the textures
#   step0.sh tex [N]      r_ext_compressed_textures 0/1 interleaved, N runs each (default 5)
#   step0.sh audio [N]    sound variants A B B22 C Z, timedemo fps, N runs each (default 3)
#   step0.sh profile V    one bot match with sound variant V, perf profile of the game
# Results: /var/tmp/ps3-bench/; ps3-bench.sh desktop at the end of the session.
# Superseded by games/bench (recipes/openarena.sh): this ran a bare X with no window manager, so
# nothing reached the screen and its game numbers are invalid (SPU-OFFLOAD-SURVEY.md).
{
set -u
B=ps3-bench.sh
OA=/usr/games/openarena
here=$(cd "$(dirname "$0")" && pwd)
TD=(+set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +set nextdemo quit +demo bench)
[ -e ~/.openarena/baseoa/demos/bench.dm_71 ] || { echo "no ~/.openarena/baseoa/demos/bench.dm_71"; exit 1; }
[ -e ~/.alsoftrc ] && { echo "~/.alsoftrc exists: variant A wouldn't be stock"; exit 1; }

# sound variants: OpenAL Soft config files, plus game cvars (all passed explicitly every run:
# the game archives them on quit)
conf() {	# conf NAME [lines...]: an alsoft.conf in /var/tmp
	local f=/var/tmp/alsoft-$1.conf; shift
	printf '[general]\n' > "$f"; printf '%s\n' "$@" >> "$f"; echo "$f"
}
variant() {	# variant V: sets ENVV and CV for sound variant V
	ENVV=(); CV=(+set s_useOpenAL 1 +set s_khz 22 +set s_initsound 1)
	case $1 in
	A) ENVV=(ALSOFT_CONF=$(conf A)) ;;
	B) ENVV=(ALSOFT_CONF=$(conf B sample-type=int16 resampler=linear hrtf=false)) ;;
	B22) ENVV=(ALSOFT_CONF=$(conf B22 sample-type=int16 resampler=linear hrtf=false frequency=22050)) ;;
	C) CV=(+set s_useOpenAL 0 +set s_khz 22 +set s_initsound 1) ;;
	Z) CV=(+set s_useOpenAL 0 +set s_khz 22 +set s_initsound 0) ;;
	*) echo "variant A|B|B22|C|Z"; exit 2 ;;
	esac
}

case ${1:-} in
smoke)
	$B x smoke-glxinfo -- /usr/bin/glxinfo -B
	BENCH_HUD=1 $B x smoke-oa -- $OA +set r_ext_compressed_textures 0 "${TD[@]}"
	grep -a -iE 'sound|openal|s3tc|compressed|CL_InitCGame' /var/tmp/ps3-bench/*-smoke-oa/out.log | tail -20
	ls /sys/devices/system/spu; dmesg | grep -i spu | tail -5
	;;
tc1-first)
	BENCH_NOVSWAP=1 BENCH_HUD=1 $B x oa-tc1-first -- $OA +set r_ext_compressed_textures 1 "${TD[@]}"
	sudo dmesg | tail -20
	echo "Did the textures look right on the TV? (graph exceptions above would mean no)"
	;;
tex)
	for i in $(seq "${2:-5}"); do
		for tc in 0 1; do
			BENCH_HUD=1 $B x oa-tc$tc -- $OA +set r_ext_compressed_textures $tc "${TD[@]}" || exit 1
		done
	done
	;;
audio)
	for i in $(seq "${2:-3}"); do
		for v in A B B22 C Z; do
			variant $v
			env "${ENVV[@]}" BENCH_HUD=1 $B x oa-snd$v -- $OA +set r_ext_compressed_textures 0 \
				"${CV[@]}" "${TD[@]}" || exit 1
		done
	done
	;;
profile)
	variant "${2:?variant}"
	env "${ENVV[@]}" BENCH_HUD=1 BENCH_TIMEOUT=180 $B x oa-prof${2} -- "$here/oa-profile.sh" \
		+set r_ext_compressed_textures 0 "${CV[@]}"
	;;
*) sed -n '2,/^[^#]/s/^# \{0,1\}//p' "$0"; exit 2 ;;
esac
exit
}
