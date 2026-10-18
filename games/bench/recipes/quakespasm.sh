# QuakeSpasm 0.96.3 (Debian) with the Quake shareware data (quake-shareware from make quake-data:
# /usr/share/games/quake/id1/pak0.pak): timedemo of the shareware's demo1, fullscreen 1920x1080.
# -nopackedpixels: 0.96.3's packed-pixels lightmaps have wrong colours on big-endian (quakespasm
# issue #114; nv30 has no 10-bit format anyway). It doesn't quit after a timedemo: stopped at the
# result line. Base keeps GLSL on (alias models; gamma stays 1.0, so the full-screen gamma pass
# doesn't run); noglsl is GL 1.x only, glslgamma turns the gamma pass on (it runs off 1.0 only).
# +timedemo on the command line doesn't work: the shareware's quake.rc runs "startdemos demo1
# demo2 demo3" after the command line's commands and the attract loop replaces the timedemo. So a
# quake.rc of ours in the user's id1 (~/.quakespasm/id1, searched before the system's pak0.pak)
# runs the timedemo instead. (A -game directory would do it too, but the shareware refuses those.)
# Steady state: the timedemo repeated in one session (PASSES=1; client.sh counts the passes that
# start 54 s or more after the first, and stops the game once they cover ~120 s: ~5 warm-up passes
# of ~13 s, then ~9 measured). QuakeSpasm has no hook at a demo's end (Host_EndGame skips the attract
# loop in a timedemo and CL_FinishTimeDemo clears cls.timedemo), so quake.rc loops by frame count:
# `bench` starts the timedemo, waits 1100 host frames (a wait is one frame; demo1's timedemo is 969
# frames, about 2 s idle at the console after it) and starts the next. A timedemo started before
# the last one ended would print a "-1 frames" line: FPS_RE wants a line starting with a count.
# fps= stays the first pass's (the run as before); steady is the median of the measured passes.
SUITE=1
COMM=quakespasm
BACKUP=("$HOME/.quakespasm/id1/config.cfg" "$HOME/.quakespasm/id1/quake.rc")
setup() {
	mkdir -p "$HOME/.quakespasm/id1"
	printf '%s\n' "exec default.cfg" "exec config.cfg" "exec autoexec.cfg" "stuffcmds" \
		'alias w5 "wait; wait; wait; wait; wait"' \
		'alias w50 "w5; w5; w5; w5; w5; w5; w5; w5; w5; w5"' \
		'alias w500 "w50; w50; w50; w50; w50; w50; w50; w50; w50; w50"' \
		'alias bench "timedemo demo1; w500; w500; w50; w50; bench"' "bench" \
		> "$HOME/.quakespasm/id1/quake.rc"
}
QS_X=()
qs_cmd() {
	CMD=(/usr/games/quakespasm -basedir /usr/share/games/quake -nopackedpixels "${QS_X[@]}"
		-width 1920 -height 1080 -fullscreen)
}
qs_cmd
FPS_RE='^[0-9]+ frames +[0-9.]+ seconds +([0-9.]+) fps'
PASSES=1
DURATION=300	# a safety cap: the passes end the run (~15 passes of ~15 s after a few seconds' start)
LOAD_MARK='^Playing demo from'	# engine and GL started (the demo's map loads after it)
variant_noglsl() { QS_X=(-noglsl); qs_cmd; }
variant_glslgamma() { QS_X=(+gamma 0.9); qs_cmd; }

# vsync off, as in every other recipe (2026-10-18): Mesa ignores the swap interval the game asks for,
# so the numbers are uncapped and comparable (ETR's 59 fps looked like a 60 Hz cap)
ENV+=(vblank_mode=0)
