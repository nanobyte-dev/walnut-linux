# Yamagi Quake II 8.70 (our build of Debian's yquake2, gamedb/games/yquake2/packages/yquake2/) with the Quake II demo data
# (quake2-demo-data from make game-data: /usr/share/games/quake2-demo/baseq2/pak0.pak): timedemo of
# the demo pak's only demo, q2demo1.dm2, on the gl1 renderer (GL 1.4 fixed function; gl3 needs GL 3),
# fullscreen 1920x1080, vsync off (a timedemo isn't otherwise capped). It returns to the console
# after the demo: stopped at the result line. The engine is run directly (Debian's quake2 wrapper
# package is contrib and not ours); the config is
# ~/.local/share/YamagiQ2/baseq2/config.cfg (8.70 is built with XDG support: "Using
# '/home/<user>/.local/share/YamagiQ2/baseq2' for writing", session of 2026-10-26); builds without it used ~/.yq2.
# Steady state: the timedemo repeated in one session (PASSES=1: passes starting 54 s or more after
# the first counted until ~120 s, then the game is stopped). At a demo's end the server runs the
# nextserver cvar, which demomap fills from nextdemo; autoexec.cfg's alias `bench` sets nextdemo
# again, disconnects (CL_Disconnect prints the pass's line; a plain chained demomap would reconnect
# and clear the counters without a line, cl_parse.c CL_ClearState) and starts the demo cold again.
# Each pass prints "ERROR: Disconnected from server" too. fps= is the first pass's (as before).
SUITE=1
COMM=quake2
ENV=(vblank_mode=0)
YQ2_HOME=${XDG_DATA_HOME:-$HOME/.local/share}/YamagiQ2
BACKUP=("$YQ2_HOME/baseq2/config.cfg" "$YQ2_HOME/baseq2/autoexec.cfg" "$HOME/.yq2/baseq2/config.cfg" "$HOME/.yq2/baseq2/autoexec.cfg")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against the engine's own "Using ... for writing" line
SESSION_HOME=(.local/share/YamagiQ2 .yq2)
setup() {
	mkdir -p "$YQ2_HOME/baseq2"
	printf '%s\n' 'alias bench "set nextdemo bench; disconnect; demomap q2demo1.dm2"' > "$YQ2_HOME/baseq2/autoexec.cfg"
}
CMD=(/usr/lib/yamagi-quake2/quake2 -datadir /usr/share/games/quake2-demo
	+set vid_renderer gl1 +set vid_fullscreen 1 +set r_mode -1 +set r_customwidth 1920
	+set r_customheight 1080 +set r_vsync 0 +timedemo 1 +set nextdemo bench +demomap q2demo1.dm2)
FPS_RE='^[0-9]+ frames, [0-9.]+ seconds: ([0-9.]+) fps'
PASSES=1
DURATION=300	# a safety cap: the passes end the run (~10 s a pass and its cold start)
# Yamagi's SDL sound backend instead of OpenAL (OpenAL gave white noise on the PS3: 16-bit samples
# with their bytes swapped somewhere on the way, big-endian)
variant_sdlsound() { CMD+=(+set s_openal 0); }
# the KISS PRNG's lag table (our yquake2 0002, YQ2_KISS_QSIZE): the package's default is 4096
# entries, 32 KiB each (docs/MEMORY.md; measured on the PS3: 70 MB less RAM, gameplay stalls
# 22.6 -> 0.05 s). The old table was 16 MiB in the client and 16 MiB in game.so, all written at
# start (32 MB of the game's 77 MB resident): variant kissbig restores it (the base is the small
# table). Client-side randomness (particles) differs from the old sequence, the demo doesn't
variant_kissbig() { ENV+=(YQ2_KISS_QSIZE=2097152); }
variant_kiss4k() { ENV+=(YQ2_KISS_QSIZE=4096); }	# the default now, kept for old command lines
# OpenAL Soft measurements (docs/SPU-AUDIO.md, M0): the shared variants of openal.inc, and no sound
# at all
. "$BENCH_LIB/openal.inc"
variant_allog() { oal_variant allog; }
variant_b16() { oal_variant b16; }
variant_f44() { oal_variant f44; }
variant_b16f44() { oal_variant b16f44; }
variant_p1024() { oal_variant p1024; }
variant_alnull() { oal_variant alnull; }
variant_f32() { oal_variant f32; }
variant_lin() { oal_variant lin; }
variant_nosound() { CMD+=(+set s_initsound 0); }
