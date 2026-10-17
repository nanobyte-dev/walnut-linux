# Sauerbraten (our cube2 build: textures compressed at upload on Mesa too) with cube2-data 1.3's
# maps (the BananaBread content). No timedemo: -l loads map "cave" (its only player start, so every
# run looks from the same spot) and the player stands still; a CubeScript (-x) counts frames for 30 s
# from 10 s after the first frame (a sleep re-armed every frame, wall time from getmillis 1), prints
# "ps3bench: <n> frames in <ms> ms: <fps> fps, longest <ms> ms" and quits. getfps itself averages
# only the last 60 frames. Settings go into ~/.cube2/autoexec.cfg (run after config.cfg, so the
# player's saved fullscreen or vsync can't override them); both files are restored after the run.
# Our cube2 (ps3.N, docs/GAME-PLAN.md) ships the memory defaults of cube2/patches: CPU skinning
# (gpuskel 0: GPU skinning indexes a bone array in the vertex shader, which nv30 compiles to if/else
# trees, 11 fps against 28; no GPU skinning shaders are built then), sky boxes and cube maps from their
# 256x256 .dds (skydds 1), lightmaps' CPU copies freed after upload (lmfreedata 1), dynamic light and
# water fade shader variants only as far as maxdynlights, waterrefract and waterfade ask. "base" is
# that; every variant below changes one thing (or, lean, the bundle that is meant to ship).
# Memory per run: during.log has MemAvailable and SwapFree every 5 s, bench.txt their lowest.
# Steady state: the script also counts its own window, 60 s after the first frame for 120 s, and
# prints "ps3bench: steady <n> frames in <ms> ms: <fps> fps, longest <ms> ms" (STEADY_RE) before it
# quits; the old line (10 s to 40 s) still comes first, so fps= and maxms= mean what they did. The
# view is static (the player stands at the spawn): what the window measures is one view's
# rendering once loading and the first stalls are over. Planned: a demo recorded once by the user
# (cube2's `recorddemo 1` in a local game, saved as a .dmo) as the run; variant demo plays
# assets/sauerbraten-bench.dmo, looped (demoend), with the same counters, when that file exists.
SUITE=1
COMM=cube2_client
SB_HOME=$HOME/.cube2
BACKUP=("$SB_HOME/autoexec.cfg" "$SB_HOME/config.cfg" "$SB_HOME/restore.cfg" "$SB_HOME/init.cfg")
SB_MAP=cave
SB_SET=()	# extra CubeScript lines for autoexec.cfg (variants)
SB_INIT=()	# extra lines for init.cfg, which the engine reads before it starts the sound (soundfreq...)
SB_DEMO=	# a demo file to play instead of standing on SB_MAP (variant demo)
setup() {
	mkdir -p "$SB_HOME"
	if [ -n "$SB_DEMO" ]; then
		[ -e "$SB_DEMO" ] || { echo "!! no $SB_DEMO: record the demo first (recipe header)"; return 1; }
		mkdir -p "$SB_HOME/demo"; cp "$SB_DEMO" "$SB_HOME/demo/bench.dmo"
	fi
	printf '%s\n' "fullscreen 1" "vsync 0" "maxfps 0" "${SB_SET[@]}" > "$SB_HOME/autoexec.cfg"
	[ ${#SB_INIT[@]} -eq 0 ] || printf '%s\n' "${SB_INIT[@]}" >> "$SB_HOME/init.cfg"
}
SB_SCRIPT='ps3f = 0; ps3max = 0
ps3count = [ps3f = (+ $ps3f 1); ps3now = (getmillis 1); ps3max = (max $ps3max (- $ps3now $ps3last)); ps3last = $ps3now; sleep 1 ps3count]
sleep 1 [echo "ps3bench: first frame"; sleep 10000 [echo "ps3bench: start"; ps3t = (getmillis 1); ps3last = $ps3t; ps3count; sleep 30000 [ps3ms = (- (getmillis 1) $ps3t); echo (format "ps3bench: %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3f $ps3ms (divf (*f $ps3f 1000) $ps3ms) $ps3max)]; sleep 50000 [ps3f0 = $ps3f; ps3t2 = (getmillis 1); ps3max = 0]; sleep 170000 [ps3ms = (- (getmillis 1) $ps3t2); ps3n = (- $ps3f $ps3f0); echo (format "ps3bench: steady %1 frames in %2 ms: %3 fps, longest %4 ms" $ps3n $ps3ms (divf (*f $ps3n 1000) $ps3ms) $ps3max); quit]]]'
sb_cmd() {
	if [ -n "$SB_DEMO" ]; then	# the demo from ~/.cube2/demo/bench.dmo (mode -1), again at its end
		CMD=(/usr/games/cube2 -w1920 -h1080 -t1 "-xdemoend = [mode -1; map bench]; mode -1; map bench
$SB_SCRIPT")
	else
		CMD=(/usr/games/cube2 -w1920 -h1080 -t1 "-l$SB_MAP" "-x$SB_SCRIPT")
	fi
}
sb_cmd
FPS_RE='^ps3bench: [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
MAXMS_RE='^ps3bench: .* longest ([0-9]+) ms'
STEADY_RE='^ps3bench: steady [0-9]+ frames in [0-9]+ ms: ([0-9.]+) fps'
STEADY_MAXMS_RE='^ps3bench: steady .* longest ([0-9]+) ms'
LOAD_MARK='^read map '
# a safety net: loading (27-34 s at best, over 100 s while the memory swaps), then 180 s (the old
# window at 10-40 s, the steady one at 60-180 s); quits by itself. The first version's 170 s cut the
# run before the measurement printed.
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
variant_gun() {
	SB_SCRIPT=${SB_SCRIPT/'echo "ps3bench: first frame";'/'echo "ps3bench: first frame"; sleep 2000 [weapon gl];'}
	sb_cmd
}
# the same with GPU skinning, to compare against gun
variant_gungpu() { variant_gun; SB_SET+=("gpuskel 1"); }
# the biggest map with one player start (66 lights, an 8192 world)
variant_towers() { SB_MAP=two_towers; sb_cmd; }
# CPU skinning through libspeq (our cube2 0006, ps3-spu's skin.spu), on the hud gun of variant gun
# (hands 1497 + grenade launcher 1683 vertices with tangent frames: the only skinned model on cave):
# on the SPEs, with libspeq's vector code on the PPE, or the engine's own loop (as before 0006).
# SPEQ_DEBUG=1 puts libspeq's counters in the log every 10 s and at exit (batches on the SPEs, mean
# and max wait, timeouts, errors); gun itself is the default, auto
variant_spuskin() { variant_gun; ENV+=(SPEQ_SKIN=spu SPEQ_DEBUG=1); }
variant_ppeskin() { variant_gun; ENV+=(SPEQ_SKIN=ppe SPEQ_DEBUG=1); }
variant_offskin() { variant_gun; ENV+=(SPEQ_SKIN=off); }
# the recorded demo (recipe header), if assets/sauerbraten-bench.dmo exists: the same counters
variant_demo() { SB_DEMO=$ASSETS/sauerbraten-bench.dmo; BACKUP+=("$SB_HOME/demo/bench.dmo"); sb_cmd; }
