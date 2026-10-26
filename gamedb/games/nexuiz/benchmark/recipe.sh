# Nexuiz Classic 2.5.2 (Debian's nexuiz-data and nexuiz-textures, 764 MB in two pk3s in
# /usr/share/games/nexuiz/data) on our darkplaces-gl2 (gamedb/engines/darkplaces/packages/darkplaces-gl2/: DarkPlaces 0~20180412, the last
# snapshot with the GL 1.1/1.3/2.0 renderpaths; Debian's darkplaces is GL 3.2 core only): smoke test
# first (UNVERIFIED on the PS3: only built, and its server run under qemu), then a bench variant. The game is
# the engine's own benchmark: -benchmark demos/bench1.dem (a timedemo in data.pk3) prints
# "N frames S seconds F fps, one-second fps min/avg/max: ..." at the demo's end and, with
# -benchmarkruns N, plays it again (each run starts with a map load) instead of quitting.
# base: the wrapper /usr/games/nexuiz-gl2 (-nexuiz -basedir ... +exec nexuiz-lean.cfg: GL 1.3 combiner
# renderpath vid_gl20 0, gl_picmip 2, no realtime lighting or bloom, mono 22 kHz sound), fullscreen
# 1920x1080 without vsync. Variants: gl20 (the GLSL renderpath: nv30 compiles the programs, RAM and
# load time are the risk), hires (picmip 0), stereo (44.1 kHz stereo sound), nosound, bench (the
# measured run: every pass's line counts, PASSES=1, as the Quake II recipe). RAM risk: textures.pk3
# is 522 MB but only read on demand (the pk3 index and the loaded textures cost RAM); watch peak RSS
# against the 217 MB, picmip and r_texture_dds_save (compressed textures cached in ~/.nexuiz) first.
# The game rewrites ~/.nexuiz/data/config.cfg (restored after the run).
COMM=darkplaces-gl2
. "$BENCH_LIB/smoke.inc"
BACKUP=("$HOME/.nexuiz/data/config.cfg")
CMD=(/usr/games/nexuiz-gl2 -benchmark demos/bench1.dem -benchmarkruns 100
	+vid_fullscreen 1 +vid_width 1920 +vid_height 1080 +vid_vsync 0)
FPS_RE='^[0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
variant_gl20() { CMD+=(+vid_gl20 1); }
variant_hires() { CMD+=(+gl_picmip 0); }
variant_stereo() { CMD+=(+snd_channels 2 +snd_speed 44100); }
variant_nosound() { CMD+=(-nosound); }
# the measured run: all passes of the demo in one session, counted from 54 s after the first load
variant_bench() { PASSES=1; DURATION=300; WARMUP=60; MEASURE=120; }
