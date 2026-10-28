# dsda-doom 0.30.0 + Freedoom 2 (stands in for PrBoom+ GL): timedemo of Freedoom's own DEMO1 lump,
# fullscreen 1920x1080. Software renderer first (base, safest), GL as a variant: GL mode uploads
# indexed textures as GL_RG, which nv30 lacks (the game's technical.md), so expect missing textures there.
# Steady state: passes. DEMO1 is 2664 gametics (76 s at 35 Hz; the timedemo takes ~54 s at 49 fps).
# Stock dsda-doom exits at a timedemo's end (dsda_demo_end_quit), so our patch 0006 (g_game.c, the
# timingdemo branch of G_CheckDemoStatus) repeats it: DSDA_BENCH_LOOPS=N passes, each with its own
# gametics and realtics and its own "Timed ..." line. 6 passes of ~54 s cover 60 s of warm-up and
# 120 s counted; slower passes (the PS3) end the run earlier, the client stops it once enough is
# measured. fps= is the first pass's, as before.
SUITE=1
COMM=dsda-doom
DD_CFG=$HOME/.local/share/dsda-doom/dsda-doom.cfg
BACKUP=("$DD_CFG")
DD_SET=()	# "key value" pairs written into the config before the run
setup() {
	[ ${#DD_SET[@]} -gt 0 ] && [ -e "$DD_CFG" ] || return 0
	local kv
	for kv in "${DD_SET[@]}"; do
		sed -i -E "s|^(${kv%% *}[[:space:]]+).*|\1${kv#* }|" "$DD_CFG"
	done
}
# The software renderer draws 960x540 (SDL scales it to the screen): at 1920x1080 the PPE managed
# ~5 fps and the timedemo didn't end within 180 s
DD_MODE=sw DD_W=960 DD_H=540
dd_cmd() {
	CMD=(/usr/games/dsda-doom -iwad /usr/share/games/doom/freedoom2.wad -vidmode $DD_MODE
		-width $DD_W -height $DD_H -fullscreen -timedemo demo1)
}
dd_cmd
PASSES=1
ENV+=(DSDA_BENCH_LOOPS=6)
DURATION=400	# a safety cap (the game exits after its 6th pass; the client stops it sooner)
FPS_RE='Timed [0-9]+ gametics in [0-9]+ realtics = ([0-9.]+) frames per second'
# the load's end: dsda-doom 0.30 on the PS3 doesn't print the R_Init/S_Init lines (none in any run),
# so the window, PERF=1 and MEM=1 never started; "Playing demo:" comes right before the first pass
LOAD_MARK='^(R_Init|S_Init|HU_Init|ST_Init|Playing demo:)'
variant_gl() { DD_MODE=gl DD_W=1920 DD_H=1080; dd_cmd; }
# base (the software renderer, 960x540 scaled to the screen: the menu's "DSDA-Doom (software)") on a
# config of the run's own: -config in the run directory, nonexistent at the start, so dsda-doom's
# defaults (FluidSynth music, 44.1 kHz) and nothing of the player's dsda-doom.cfg read or written;
# the CPU-bound case of the real-time audio tests (docs/SPU-AUDIO.md "Real-time mixer")
variant_owncfg() { CMD+=(-config "${D:-/tmp}/dsda-doom.cfg"); }
variant_sw1080() { DD_W=1920 DD_H=1080; dd_cmd; }
# SDL's GLES2 renderer instead of its desktop-GL one (the software frame goes to the screen through
# an SDL texture): the 960x540 software run was green-tinted
variant_gles() { ENV+=(SDL_RENDER_DRIVER=opengles2); }
# SDL's software renderer: no GL at all in presenting the frame (tells a GPU-path colour bug from
# the game's or SDL's own)
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software); }
# the demo at normal speed (35 tics/s, as in play) with the GL renderer: does the sound keep up when
# the game isn't rendering flat out? (60 s, HUD fps)
variant_play() {
	DD_MODE=gl DD_W=1920 DD_H=1080; dd_cmd
	CMD=("${CMD[@]/-timedemo/-playdemo}"); DURATION=60
}
# play without the MIDI music (fluidsynth synthesizes it in software on the PPE): effects only
variant_playnomusic() { variant_play; CMD+=(-nomusic); }
# sound tuning for the PPE, music on (plain play stuttered: fluidsynth took ~40% of a thread and
# rendering ran uncapped): OPL music emulation (snd_midiplayer "opl"), mixing at the device's 48 kHz (no resampling),
# a larger buffer, frames capped at 60
variant_tuned() {
	variant_play
	DD_SET=('snd_midiplayer "opl"' 'snd_samplerate 48000' 'snd_samplecount 2048' 'dsda_fps_limit 60')
}
# one sound setting at a time against the underrun count (bench.txt underruns=)
variant_playopl() { variant_play; DD_SET=('snd_midiplayer "opl"'); }
variant_playcap35() { variant_play; DD_SET=('dsda_fps_limit 35'); }
variant_play48k() { variant_play; DD_SET=('snd_samplerate 48000'); }
# fluidsynth's voice DSP on the SPEs (our libfluidsynth3 +ps3.289 with libspeq0 0.288,
# docs/SPU-AUDIO.md "fluidsynth on the SPEs"): spu always, ppe = libspeq's code on the PPE, off = stock fluidsynth
variant_playspu() { variant_play; ENV+=(SPEQ_SYNTH=spu SPEQ_DEBUG=1); }
variant_playppe() { variant_play; ENV+=(SPEQ_SYNTH=ppe SPEQ_DEBUG=1); }
variant_playoff() { variant_play; ENV+=(SPEQ_SYNTH=off); }
# GL renderer draws (our patch 0005, gl_batch.c): walls, flats, sprites and HUD patches batched
# into few glDrawArrays, light level per vertex. On the host (llvmpipe, GL 2.1) the gl timedemo
# goes from ~1960 GL calls and ~205 draws a frame to ~390 and ~46. Each switch alone, all off
# (= the renderer before 0005), and the per-frame batch counts in game.log every 350 frames
variant_glstats() { variant_gl; ENV+=(DSDA_GL_STATS=1); }
variant_glnobatch() { variant_gl; ENV+=(DSDA_GL_BATCH=0 DSDA_GL_STATS=1); }
variant_glnoflats() { variant_gl; ENV+=(DSDA_GL_FLATS=0 DSDA_GL_STATS=1); }
variant_glnohud() { variant_gl; ENV+=(DSDA_GL_HUD=0 DSDA_GL_STATS=1); }
variant_glnovlight() { variant_gl; ENV+=(DSDA_GL_VLIGHT=0 DSDA_GL_STATS=1); }
variant_gloff() { variant_gl; ENV+=(DSDA_GL_BATCH=0 DSDA_GL_FLATS=0 DSDA_GL_HUD=0 DSDA_GL_VLIGHT=0); }

# vsync off, as in every other recipe (2026-10-18): Mesa ignores the swap interval the game asks for,
# so the numbers are uncapped and comparable (ETR's 59 fps looked like a 60 Hz cap)
ENV+=(vblank_mode=0)
