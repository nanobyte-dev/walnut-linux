# BananaBread (Debian's cube2, so this recipe is still called sauerbraten; our build: textures compressed at upload on Mesa too) with cube2-data 1.3's
# maps (the BananaBread content).
# base: the user's recorded demo (assets/sauerbraten-bench.dmo, 2026-10-17: an FFA round on cave with
# two bots, 93 s of game time, recorded by the local server with `recorddemo 1` and a map reload,
# saved with getdemo), copied to ~/.cube2/demo/bench.dmo and played with `mode -1; map bench` (demo mode on
# the local server, as the data's `demo` command does it: the server's setupdemoplayback reads <demodir>/bench.dmo and
# sends the recorded packets, so the client loads cave from them and runs `mapstart`; at the end,
# "demo playback finished" and the client runs `demoend`). A CubeScript (-x) counts frames (a sleep
# re-armed every frame, wall time from getmillis 1): each playback ("loop") prints "ps3bench: loop
# <k>: <n> frames in <ms> ms: <fps> fps, longest <ms> ms" at its end (fps= and maxms= are loop 1's,
# the whole first playback). Steady state: the window opens 60 s after the first mapstart (the map
# loaded) and closes at the playback's end; demoend plays the demo again (a reload, left out of
# the window: it reopens at the next mapstart) until the window holds ~120 s (114 s: the rest of loop
# 1 and loop 2), then "ps3bench: steady <n> frames in <ms> ms: <fps> fps, longest <ms> ms" and quit.
# The script marks the window ("ps3bench: window on/off": WIN_ON_RE, WIN_OFF_RE) so the HUD metrics
# use the same pieces. The bots in a demo are played back, not simulated: every run is the same.
# variant spawn: the method before the demo: -l loads map "cave" (its only player start, so every
# run looks from the same spot) and the player stands still; the script counts frames for 30 s from
# 10 s after the first frame and prints "ps3bench: <n> frames in <ms> ms: <fps> fps, longest <ms>
# ms" (fps=, maxms=), then its own steady window, 60 s after the first frame for 120 s
# (STEADY_RE), and quits. The view is static: one view's rendering once loading is over. The gun,
# towers and *skin variants use it (the hud gun and the other map only exist there).
# getfps itself averages only the last 60 frames. Settings go into ~/.cube2/autoexec.cfg (run after config.cfg, so the
# player's saved fullscreen or vsync can't override them); both files are restored after the run.
# Our cube2 (ps3.N, docs/games/sauerbraten.md) ships the memory defaults of packages/patched/games/cube2/patches: CPU skinning
# (gpuskel 0: GPU skinning indexes a bone array in the vertex shader, which nv30 compiles to if/else
# trees, 11 fps against 28; no GPU skinning shaders are built then), sky boxes and cube maps from their
# 256x256 .dds (skydds 1), lightmaps' CPU copies freed after upload (lmfreedata 1), dynamic light and
# water fade shader variants only as far as maxdynlights, waterrefract and waterfade ask. "base" is
# that; every variant below changes one thing (or, lean, the bundle that is meant to ship).
# Memory per run: during.log has MemAvailable and SwapFree every 5 s, bench.txt their lowest.
SUITE=1
COMM=cube2_client
# the game data GAMEDATA=ssd takes from the SSD (cube2-data and sauerbraten-nonfree-data)
DATA_DIRS=(/usr/share/games/cube2)
SB_HOME=$HOME/.cube2
BACKUP=("$SB_HOME/autoexec.cfg" "$SB_HOME/config.cfg" "$SB_HOME/restore.cfg" "$SB_HOME/init.cfg"
	"$SB_HOME/demo/bench.dmo")
# played sessions run on fresh defaults: these (under ~) are set aside and put back (session.sh,
# freshhome.py); checked against BananaBread's home (packages/walnut/apps/sauerbraten/files/sauerbraten keeps Sauerbraten's apart in ~/.sauerbraten)
SESSION_HOME=(.cube2)
SB_MAP=cave
SB_SET=()	# extra CubeScript lines for autoexec.cfg (variants)
SB_INIT=()	# extra lines for init.cfg, which the engine reads before it starts the sound (soundfreq...)
SB_MODE=demo	# demo: play SB_DEMO; spawn: stand at SB_MAP's player start (variant spawn)
SB_DEMO=$ASSETS/sauerbraten-bench.dmo
setup() {
	mkdir -p "$SB_HOME"
	if [ "$SB_MODE" = demo ]; then
		[ -e "$SB_DEMO" ] || { echo "!! no $SB_DEMO: record the demo first (recipe header)"; return 1; }
		mkdir -p "$SB_HOME/demo"; cp "$SB_DEMO" "$SB_HOME/demo/bench.dmo"
	fi
	printf '%s\n' "fullscreen 1" "vsync 0" "maxfps 0" "${SB_SET[@]}" > "$SB_HOME/autoexec.cfg"
	[ ${#SB_INIT[@]} -eq 0 ] || printf '%s\n' "${SB_INIT[@]}" >> "$SB_HOME/init.cfg"
}
SB_SCRIPT='ps3f = 0; ps3max = 0
ps3count = [ps3f = (+ $ps3f 1); ps3now = (getmillis 1); ps3max = (max $ps3max (- $ps3now $ps3last)); ps3last = $ps3now; sleep 1 ps3count]
sleep 1 [echo "ps3bench: first frame"; sleep 10000 [echo "ps3bench: start"; ps3t = (getmillis 1); ps3last = $ps3t; ps3count; sleep 30000 [ps3ms = (- (getmillis 1) $ps3t); echo (format "ps3bench: %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3f $ps3ms (divf (*f $ps3f 1000) $ps3ms) $ps3max)]; sleep 50000 [ps3f0 = $ps3f; ps3t2 = (getmillis 1); ps3max = 0]; sleep 170000 [ps3ms = (- (getmillis 1) $ps3t2); ps3n = (- $ps3f $ps3f0); echo (format "ps3bench: steady %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3n $ps3ms (divf (*f $ps3n 1000) $ps3ms) $ps3max); quit]]]'
# the demo's counters (recipe header): ps3play while a playback runs, ps3on while the window is open
SB_DSCRIPT='ps3f = 0; ps3max = 0; ps3lmax = 0; ps3last = 0; ps3on = 0; ps3loop = 0; ps3mf = 0; ps3mt = 0; ps3warm = 0; ps3play = 0
ps3count = [ps3f = (+ $ps3f 1); ps3now = (getmillis 1); ps3d = (- $ps3now $ps3last); if $ps3play [ps3lmax = (max $ps3lmax $ps3d)]; if $ps3on [ps3max = (max $ps3max $ps3d)]; ps3last = $ps3now; sleep 1 ps3count]
ps3wake = [ps3on = 1; ps3f0 = $ps3f; ps3t0 = (getmillis 1); echo "ps3bench: window on"]
mapstart = [ps3loop = (+ $ps3loop 1); ps3play = 1; ps3lf = $ps3f; ps3lt = (getmillis 1); ps3lmax = 0; ps3last = $ps3lt; if (= $ps3loop 1) [echo "ps3bench: first frame"; ps3count; sleep 60000 [ps3warm = 1; if $ps3play [ps3wake]]] [if $ps3warm [ps3wake]]]
demoend = [ps3play = 0; ps3now = (getmillis 1); ps3n = (- $ps3f $ps3lf); ps3ms = (- $ps3now $ps3lt); echo (format "ps3bench: loop %1: %2 frames in %3 ms: %4 fps, longest %5 ms" $ps3loop $ps3n $ps3ms (divf (*f $ps3n 1000) $ps3ms) $ps3lmax); if $ps3on [ps3on = 0; ps3mf = (+ $ps3mf (- $ps3f $ps3f0)); ps3mt = (+ $ps3mt (- $ps3now $ps3t0)); echo "ps3bench: window off"]; if (>= $ps3mt 114000) [echo (format "ps3bench: steady %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3mf $ps3mt (divf (*f $ps3mf 1000) $ps3mt) $ps3max); sleep 100 [quit]] [sleep 100 [mode -1; map bench]]]
mode -1; map bench'
sb_cmd() {
	if [ "$SB_MODE" = demo ]; then
		CMD=(/usr/games/cube2 -w1920 -h1080 -t1 "-x$SB_DSCRIPT")
		FPS_RE='^ps3bench: loop 1: [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
		MAXMS_RE='^ps3bench: loop 1: .* longest ([0-9]+) ms'
		WIN_ON_RE='^ps3bench: window on'; WIN_OFF_RE='^ps3bench: window off'
	else
		CMD=(/usr/games/cube2 -w1920 -h1080 -t1 "-l$SB_MAP" "-x$SB_SCRIPT")
		FPS_RE='^ps3bench: [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
		MAXMS_RE='^ps3bench: .* longest ([0-9]+) ms'
		WIN_ON_RE=; WIN_OFF_RE=
	fi
}
sb_cmd
STEADY_RE='^ps3bench: steady [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
STEADY_MAXMS_RE='^ps3bench: steady .* longest ([0-9]+) ms'
LOAD_MARK='^read map '
PLAY_MARK=$LOAD_MARK	# the map is read (a played session; no session() command yet)
# a safety net: loading (27-34 s at best, over 100 s while the memory swaps), then ~200 s (demo: two
# playbacks and a reload; spawn: 180 s); quits by itself. The first version's 170 s cut the run
# before the measurement printed.
DURATION=330
during() {
	while :; do
		printf '%(%s)T %s\n' -1 "$(awk '/^(MemAvailable|SwapFree):/ { printf "%s%s ", $1, $2 }' /proc/meminfo)"
		sleep 5
	done
}
parse() {
	awk '{ for (i = 2; i <= NF; i++) if ($i ~ /^MemAvailable:/) { v = substr($i, 14) + 0; if (!n++ || v < m) m = v } }
	     END { if (n) print "min_memavailable_kb=" m }' "$D/during.log" 2>/dev/null
}
# GPU skinning on (the old default): shaders for it are built, the hud gun and players use them
variant_gpuskel() { SB_SET+=("gpuskel 1"); }
# the old default spelled out (base is CPU skinning now)
variant_cpuskel() { SB_SET+=("gpuskel 0"); }
# sky and env map from the PNGs (six 2048x2048, decoded twice at load) instead of the 256x256 .dds
variant_pngsky() { SB_SET+=("skydds 0"); }
# lightmaps' CPU copies kept (24 MB in cave)
variant_keeplm() { SB_SET+=("lmfreedata 0"); }
# dynamic light shader variants: all five that the stock engine builds (maxdynlights 5 also draws
# five), or fewer. Lights and muzzle flashes light the world less
variant_dyn5() { SB_SET+=("maxdynlights 5"); }
variant_dyn1() { SB_SET+=("maxdynlights 1"); }
variant_dyn0() { SB_SET+=("maxdynlights 0"); }
# water: no fade under the surface (no fade variants of every world shader), or no reflection and
# refraction at all (two extra scene passes when water is in view)
variant_nofade() { SB_SET+=("waterfade 0"); }
variant_nowater() { SB_SET+=("waterreflect 0" "waterrefract 0"); }
# the world shaders without specular and parallax (shaderdetail 2: no spec map; 1: no parallax
# either, the menu's "low detail"): fewer texture reads per pixel, fewer variants
variant_sd2() { SB_SET+=("shaderdetail 2"); }
variant_sd1() { SB_SET+=("shaderdetail 1"); }
# no normal-mapped or env-mapped models: far fewer model shader programs
variant_nobumpmdl() { SB_SET+=("bumpmodels 0" "envmapmodels 0"); }
# textures dropped by a mip level (texreduce 1: a quarter of the memory of the DDS and JPG ones) or
# capped in size (maxtexsize also caps the sky and the lightmap textures)
variant_tex1() { SB_SET+=("texreduce 1"); }
variant_tex2() { SB_SET+=("texreduce 2"); }
variant_maxtex512() { SB_SET+=("maxtexsize 512"); }
# compress the uncompressed textures from 512 or 256 pixels up (default 1024): water, models,
# particles, the HUD. Look for ragged text and HUD edges
variant_tc512() { SB_SET+=("texcompress 512"); }
variant_tc256() { SB_SET+=("texcompress 256"); }
# sound at 22 kHz: the decoded samples are half the size
variant_snd22() { SB_INIT+=("soundfreq 22050"); }
# the bundle meant to ship if the single steps hold: no water passes, low-detail shaders, models
# without bump, one dynamic light, compressed 512 textures, 22 kHz sound
variant_lean() {
	SB_SET+=("waterreflect 0" "waterrefract 0" "shaderdetail 1" "bumpmodels 0" "envmapmodels 0"
		"maxdynlights 1" "texcompress 512")
	SB_INIT+=("soundfreq 22050")
}
# no texture compression at all (as upstream on Mesa): VRAM and load time against base
variant_notc() { SB_SET+=("texcompress 0"); }
# hud gun: -l starts mode 1 (coop edit), where the player spawns with the pistol, and cube2-data 1.3
# (the BananaBread content) has no pistol or fist hud gun model (models/hudguns: chaing gl rifle
# rocket shotg), so nothing is drawn. This switches to the grenade launcher (1 grenade at spawn) 2 s
# after the first frame, which adds the one skeletal model that CPU or GPU skinning has to move
# the method before the demo (recipe header)
variant_spawn() { SB_MODE=spawn; sb_cmd; }
variant_gun() {
	SB_MODE=spawn
	SB_SCRIPT=${SB_SCRIPT/'echo "ps3bench: first frame";'/'echo "ps3bench: first frame"; sleep 2000 [weapon gl];'}
	sb_cmd
}
# the same with GPU skinning, to compare against gun
variant_gungpu() { variant_gun; SB_SET+=("gpuskel 1"); }
# the biggest map with one player start (66 lights, an 8192 world)
variant_towers() { SB_MODE=spawn; SB_MAP=two_towers; sb_cmd; }
# CPU skinning through libspeq (our cube2 0006, ps3-spu's skin.spu), on the hud gun of variant gun
# (hands 1497 + grenade launcher 1683 vertices with tangent frames: the only skinned model on cave):
# on the SPEs, with libspeq's vector code on the PPE, or the engine's own loop (as before 0006).
# SPEQ_DEBUG=1 puts libspeq's counters in the log every 10 s and at exit (batches on the SPEs, mean
# and max wait, timeouts, errors); gun itself is the default, auto
variant_spuskin() { variant_gun; ENV+=(SPEQ_SKIN=spu SPEQ_DEBUG=1); }
variant_ppeskin() { variant_gun; ENV+=(SPEQ_SKIN=ppe SPEQ_DEBUG=1); }
variant_offskin() { variant_gun; ENV+=(SPEQ_SKIN=off); }
# the same three on the recorded demo (base): the two bots' player models are skinned too, more work
# than the gun alone (the user, 2026-10-18: the spawn runs showed no demo). With
# sauerbraten-nonfree-data (the licensed suite) the player models and guns have their real meshes.
variant_demospu() { ENV+=(SPEQ_SKIN=spu SPEQ_DEBUG=1); }
variant_demoppe() { ENV+=(SPEQ_SKIN=ppe SPEQ_DEBUG=1); }
variant_demooff() { ENV+=(SPEQ_SKIN=off); }
# the recorded demo: the same as base (named for the runs made before base became the demo)
variant_demo() { SB_MODE=demo; sb_cmd; }
# Fewer GL calls per frame (our cube2 0007-0009, each behind a cvar that is 1 by default; on cave under
# llvmpipe as GL 2.1 without GL_EXT_gpu_shader4, like the PS3: 1657 GL calls and 199 draws a frame with
# all off, 1300 and 199 with all on, with the hud gun; the same pixels). Each variant turns one off, to
# see what it is worth:
# slot and global shader uniforms sent only when their value changed (gluniformcache)
variant_nouni() { SB_SET+=("gluniformcache 0"); }
# a batch's textures bound together, right before its draw, a glActiveTexture per unit that changes
# (glbatchtmu)
variant_notmu() { SB_SET+=("glbatchtmu 0"); }
# particle vertices uploaded by renderers that have particles only, one glBufferData each (glpartvbo)
variant_nopart() { SB_SET+=("glpartvbo 0"); }
# all of them off: the drawing of ps3.315 and before
variant_allbatchoff() { SB_SET+=("gluniformcache 0" "glbatchtmu 0" "glpartvbo 0"); }
# DXT1 of RGB sources (libspeq 2026-10-18, packages/walnut/tools/ps3-spu/doc/dxt-rgb.md): the JPG and other alpha-less
# textures reach libspeq as 3 bytes a pixel ("speq: s3tc dxt1 ... comps 3" lines), which it used to
# encode on the PPE (165 ms for a 1024x1024, a hitch mid-game); now the SPEs take them. rgbppe sends
# them back to the PPE encoder (the old behaviour), rgbspu is the default with the same logging, so
# the pair differs in the switch alone: stall_cpu_s, load time and the "comps 3 ... spu/ppe" lines.
variant_rgbspu() { ENV+=(SPEQ_DEBUG=1); }
variant_rgbppe() { ENV+=(SPEQ_S3TC_RGB=ppe SPEQ_DEBUG=1); }
# The water's occlusion query (docs/HOTPATHS.md, target 1): drawreflections waited for this frame's
# result, the GPU behind (10% of the CPU spun in nv30_query_result). oqlag: our cube2 0010's
# oqwaterlag 1, this frame's result only if it is in, else last frame's (the first frame water shows
# again has its last reflection); oqnowait: the stock oqwait 0, every query not in yet counts as
# visible (the reflection passes then run whenever the result is late). The Mesa side is an ENVS
# token: NV30_QUERY_WAIT=US sleeps on the fence after US microseconds of spinning (base, pixels the same)
variant_oqlag() { SB_SET+=("oqwaterlag 1"); }
variant_oqnowait() { SB_SET+=("oqwait 0"); }
# World normals and tangents as GL_SHORT normalized (our cube2 0011, vanormalshort; docs/MEMORY.md):
# nv30 can't fetch the stock GL_BYTE ones, so u_vbuf converts them at every world draw and nouveau
# keeps a malloc copy of every world vertex buffer to read them from. shortnormals uploads them as
# shorts (the RSX fetches those: no conversion, no copies; cave's world buffers 4.40 -> 5.50 MB of
# VRAM); off by default (on llvmpipe ~170 pixels a frame differ by 1-2 levels). bytenormals is the
# stock layout spelled out. With NV30_SYSMEM_STATS=300 (an ENVS token, Mesa 0037) the sysmem_*
# columns show the copies go
variant_shortnormals() { SB_SET+=("vanormalshort 1"); }
variant_bytenormals() { SB_SET+=("vanormalshort 0"); }
