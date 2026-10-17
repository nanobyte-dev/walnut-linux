# Yamagi Quake II 8.70 (our build of Debian's yquake2, yquake2/) with the Quake II demo data
# (quake2-demo-data from make quake-data: /usr/share/games/quake2-demo/baseq2/pak0.pak): timedemo of
# the demo pak's only demo, q2demo1.dm2, on the gl1 renderer (GL 1.4 fixed function; gl3 needs GL 3),
# fullscreen 1920x1080, vsync off (a timedemo isn't otherwise capped). It returns to the console
# after the demo: stopped at the result line. The engine is run directly (Debian's quake2 wrapper
# package is contrib and not ours); the config is ~/.yq2/baseq2/config.cfg.
SUITE=1
COMM=quake2
ENV=(vblank_mode=0)
BACKUP=("$HOME/.yq2/baseq2/config.cfg")
CMD=(/usr/lib/yamagi-quake2/quake2 -datadir /usr/share/games/quake2-demo
	+set vid_renderer gl1 +set vid_fullscreen 1 +set r_mode -1 +set r_customwidth 1920
	+set r_customheight 1080 +set r_vsync 0 +timedemo 1 +demomap q2demo1.dm2)
FPS_RE='[0-9]+ frames, [0-9.]+ seconds: ([0-9.]+) fps'
END_RE='[0-9]+ frames, [0-9.]+ seconds: [0-9.]+ fps'
# Yamagi's SDL sound backend instead of OpenAL (OpenAL gave white noise on the PS3: 16-bit samples
# with their bytes swapped somewhere on the way, big-endian)
variant_sdlsound() { CMD+=(+set s_openal 0); }
# OpenAL Soft measurements (docs/SPU-AUDIO.md, M0): the shared variants of openal.inc, and no sound
# at all
. "$here/recipes/openal.inc"
variant_allog() { oal_variant allog; }
variant_b16() { oal_variant b16; }
variant_f44() { oal_variant f44; }
variant_b16f44() { oal_variant b16f44; }
variant_p1024() { oal_variant p1024; }
variant_alnull() { oal_variant alnull; }
variant_nosound() { CMD+=(+set s_initsound 0); }
