# OpenArena (ioquake3 0.8.8): timedemo of bench.dm_71 (as games/openarena/step0.sh), renderer and
# texture compression per variant. The game archives +set cvars to q3config.cfg on quit.
# Steady state: the timedemo repeated in one session (PASSES=1: every pass prints its line; passes
# starting 54 s or more after the first map load are counted until ~120 s, then the game is
# stopped). nextdemo is cleared before it runs (CL_NextDemo), so each pass sets it again:
# bench_loop = "set nextdemo vstr bench_again; demo bench", bench_again = "meminfo; vstr
# bench_loop" (in autoexec.cfg). Every pass reloads the map (CL_InitCGame again; opengl2 without
# r_glslKeepPrograms compiles its programs again too). fps=, maxms= and load= are the first
# pass's, as the single timedemo had them; steady is the median of the measured passes.
SUITE=1
COMM=ioquake3
# the game data GAMEDATA=ssd takes from the SSD (openarena-data, the pk3s)
DATA_DIRS=(/usr/share/games/openarena)
SOUND_AT=25	# after the level load (a cold first run takes ~12 s), during the demo
OA_AUTOEXEC=$HOME/.openarena/baseoa/autoexec.cfg
BACKUP=("$HOME/.openarena/baseoa/q3config.cfg" "$OA_AUTOEXEC")
setup() {
	mkdir -p "$HOME/.openarena/baseoa/demos"
	# the variant's cvars (OA_X) as set lines: the engine runs autoexec.cfg at start, before the
	# renderer comes up, like q3config.cfg
	local i
	: > "$OA_AUTOEXEC"
	for ((i = 0; i + 2 < ${#OA_X[@]} + 1; i += 3)); do
		[ "${OA_X[i]}" = +set ] && printf 'set %s "%s"\n' "${OA_X[i+1]}" "${OA_X[i+2]}" >> "$OA_AUTOEXEC"
	done
	printf '%s\n' 'set bench_again "meminfo; vstr bench_loop"' \
		'set bench_loop "set nextdemo vstr bench_again; demo bench"' >> "$OA_AUTOEXEC"
	cmp -s "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71" ||
		cp "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71"
}
# at each demo's end meminfo prints the hunk in use (parse(): hunk_*, the last pass's), with the level still loaded.
# The engine keeps at most 32 '+' commands from its command line (MAX_CONSOLE_LINES, common.c) and
# drops the rest silently; Debian's openarena wrapper already passes ~10. So the variants' cvars
# (OA_X, "+set name value" triples) go into autoexec.cfg (setup()), not onto the command line:
# gl2min_up's 36 commands lost +demo bench and sat in the menu (2026-10-17).
OA_TD=(+set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +vstr bench_loop)
OA_R=opengl1; OA_TC=1; OA_X=()
oa_cmd() { CMD=(/usr/games/openarena +set cl_renderer $OA_R +set r_ext_compressed_textures $OA_TC "${OA_TD[@]}"); }
oa_cmd
FPS_RE='[0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='[0-9.]+ fps [0-9.]+/[0-9.]+/([0-9.]+)/'
LOAD_RE='CL_InitCGame: +([0-9.]+) seconds'
PASSES=1
DURATION=300	# a safety cap: the passes end the run (opengl1: ~12 s load, passes of ~25 s)
parse() {	# renderergl2's program counts and compile times (first start, the demo's), meminfo's hunk
	local l n=0 re='loaded ([0-9]+) GLSL shaders .* in +([0-9.]+) seconds(, ([0-9]+) of them kept from the last start, ([0-9]+) left)?'
	while IFS= read -r l; do
		if [[ $l =~ $re ]]; then
			n=$((n + 1))
			echo "glsl${n}_programs=${BASH_REMATCH[1]}"; echo "glsl${n}_seconds=${BASH_REMATCH[2]}"
			[ -n "${BASH_REMATCH[3]}" ] && { echo "glsl${n}_kept=${BASH_REMATCH[4]}"; echo "glsl${n}_deferred=${BASH_REMATCH[5]}"; }
		fi
		[[ $l =~ ^([0-9]+)\ GLSL\ programs\ were\ built\ at\ their\ first\ use ]] && echo "glsl_built_later=${BASH_REMATCH[1]}"
		[[ $l =~ ^\ *([0-9]+)\ total\ hunk\ in\ use ]] && echo "hunk_in_use=${BASH_REMATCH[1]}"
		[[ $l =~ ^\ *([0-9]+)\ (low|high)\ tempHighwater ]] && echo "hunk_${BASH_REMATCH[2]}_temp=${BASH_REMATCH[1]}"
	done < "$D/game.log"
	return 0
}
# opengl1, uncompressed textures (step 0's tc0)
variant_tc0() { OA_TC=0; oa_cmd; }
# opengl1, compressed (shipped default, step 0's tc1)
variant_tc1() { OA_TC=1; oa_cmd; }
# renderergl2 with its expensive features off (docs/games/openarena.md, gl2min), then Debian's defaults.
# Debian's ioquake3 compiles 97 GLSL programs (gl2: 115, ~30 s on the PPE) at start and again when
# the demo loads its map (the client restarts the renderer at every map load), so the gl2 runs get
# 330 s (240 before the steady-state passes). Our ioquake3 (ioquake3/patches, +ps3) cuts that memory; its defaults are on in gl2min and
# gl2, and each gl2min_* variant below turns one change back (or an opt-in one on) to measure it.
OA_GL2MIN=(+set r_normalMapping 0 +set r_specularMapping 0 +set r_deluxeMapping 0 +set r_sunlightMode 0
	+set r_sunShadows 0 +set r_dlightMode 0 +set r_depthPrepass 0 +set r_hdr 0 +set r_postProcess 0
	+set r_toneMap 0 +set r_autoExposure 0 +set r_ssao 0 +set r_cubeMapping 0
	+set r_ext_framebuffer_object 0)
oa_gl2min() { OA_R=opengl2; OA_X=("${OA_GL2MIN[@]}" "$@"); oa_cmd; DURATION=330; }
variant_gl2min() { oa_gl2min; }
variant_gl2() { OA_R=opengl2; oa_cmd; DURATION=330; }
# every switch of ours back to upstream (Debian's behaviour, except the VAO cache and the model
# tangents, which have no switch: for those, compare with Debian's deb)
variant_gl2min_up() { oa_gl2min +set r_glslKeepShaders 1 +set r_glslMaxBones -1 +set r_glslAllPrograms 1 +set r_glslKeepPrograms 0 +set r_keepModelVerts 1 +set r_vaoSnormColor 0 +set r_mipMapPowf 1; }
# 0001: shader objects kept attached after linking (their source and IR in Mesa)
variant_gl2min_shaders() { oa_gl2min +set r_glslKeepShaders 1; }
# 0002: the 27 bone-animation programs (105-matrix arrays, compare trees on nv30)
variant_gl2min_bones() { oa_gl2min +set r_glslMaxBones -1; }
# 0003: the 15 shadow and post-processing programs that need framebuffer objects
variant_gl2min_fboprog() { oa_gl2min +set r_glslAllPrograms 1; }
# 0004: every program compiled again at the demo's map load (load time, the second compile burst)
variant_gl2min_recompile() { oa_gl2min +set r_glslKeepPrograms 0; }
# 0005 (opt-in): each program built at its first use; stalls when a variant is first drawn
variant_gl2min_lazy() { oa_gl2min +set r_glslLazy 1; }
# 0007: the permanent CPU copy of every MD3 vertex frame (hunk: ~87 MB for the demo's models)
variant_gl2min_modelverts() { oa_gl2min +set r_keepModelVerts 1; }
# 0009 (opt-in): only the full-detail LOD of each model (no _1, _2: -41% model memory, more vertices)
variant_gl2min_lods1() { oa_gl2min +set r_md3Lods 1; }
# 0009: r_lodbias 1, LOD 0 never drawn so no longer loaded (-59% model memory, coarser models)
variant_gl2min_lodbias1() { oa_gl2min +set r_lodbias 1; }
# 0010 (opt-in): MD3 animation on the CPU as opengl1 (no model VBOs, 18 fewer programs, more PPE)
variant_gl2min_cpuanim() { oa_gl2min +set r_gpuVertexAnimation 0; }
# settings only: lazy and lods1 together; texture resolution (r_picmip 2 against ioquake3's default
# 1; the run's q3config.cfg holds what OpenArena had); uncompressed textures in gl2
variant_gl2min_lean() { oa_gl2min +set r_glslLazy 1 +set r_md3Lods 1; }
variant_gl2min_picmip2() { oa_gl2min +set r_picmip 2; }
variant_gl2min_tc0() { OA_TC=0; oa_gl2min; }
# 0027: vertex colours as unsigned shorts again (R16G16B16A16_UNORM, which nv30 can't fetch: u_vbuf
# converts them on the CPU at every draw, and nouveau keeps malloc'ed copies of the VRAM buffers it
# reads, after a few such reads of every new one: docs/MEMORY.md "OpenArena opengl2")
variant_gl2min_color16() { oa_gl2min +set r_vaoSnormColor 0; }
# 0028: the gamma-correct mipmaps with a powf per texel channel (the same bytes, more CPU at loads and joins)
variant_gl2min_mippowf() { oa_gl2min +set r_mipMapPowf 1; }
# 0029 (opt-in): animated MD3 frames in 12 bytes a vertex instead of 20 (the demo's model VBOs 62 -> 37.5 MB
# of VRAM; 8-bit normals: on llvmpipe 1734 pixels of 30 frames differ by at most 2 levels)
variant_gl2min_md3pack() { oa_gl2min +set r_md3PackedFrames 1; }
# CPU time of the opengl1 renderer and the engine (docs/archive/GAME-PLAN.md: 13.6% zlib, 35% renderer). Our
# ioquake3 patches 0011-0018 are on in tc1/base; each variant turns one back to upstream, `up` all of
# them, so that base against the variant is that patch's gain. oa_x appends cvars (autoexec.cfg).
oa_x() { OA_X+=("$@"); oa_cmd; }
# 0011: R_FindShader scanned the whole shader text again after a miss in its hash table
variant_scan() { oa_x +set r_shaderTextScan 1; }
# 0012: crc32 of every pk3 entry read (nobody looks at the result)
variant_crc() { oa_x +set fs_zipCrc 1; }
# 0013: the AltiVec versions of the MD3 interpolation and the diffuse lighting (needs the -maltivec build)
variant_noaltivec() { oa_x +set com_altivec 0; }
# 0014: two 272-byte memsets for every plane a decal triangle is clipped against
variant_markclear() { oa_x +set r_markClear 1; }
# 0015: the sounds of a player model read and decoded when the player joins instead of at their first play
variant_sndeager() { oa_x +set s_alLazy 0; }
# 0016: zlib instead of libdeflate for the pk3 entries
variant_zlib() { oa_x +set fs_zipFast 0; }
# 0018: the message Huffman trees fed 1053340 symbols at the first message (a command line cvar: the
# first message comes before autoexec.cfg)
variant_huffslow() { oa_cmd; CMD+=(+set msg_huffSlow 1); }
# all of them as before (not the build flags, nor the AltiVec code being compiled in)
variant_up() { oa_x +set r_shaderTextScan 1 +set fs_zipCrc 1 +set com_altivec 0 +set r_markClear 1 +set s_alLazy 0 +set fs_zipFast 0; CMD+=(+set msg_huffSlow 1); }
# Batching (ioquake3 0019-0022, docs/games/openarena.md): fewer GL calls and draws a frame from
# renderergl1. On llvmpipe forced to GL 2.1 the demo went from 3022 GL calls and 198 draws a frame
# to 377 and 79, the same picture (atlas: 74 pixels differ by at most 2). Each variant turns one
# change back to upstream, allbatchoff all four.
# 0019: the map's 32 lightmaps as one atlas texture, so a texture lit from several lightmaps is one
# batch (world batches 114 -> 27 a frame). Off by default since 0024: on the PS3 any atlas page adds
# ~10 s of hitches per run (stall_s 34-36 vs 24), the frame cost equal. mergelm1 turns it back on.
variant_mergelm1() { oa_x +set r_mergeLightmaps 1; }
# 0020: texture unit, GL_TEXTURE_2D and client array state sent at every call (not only when a draw
# needs a change)
variant_glcache0() { oa_x +set r_glStateCache 0; }
# 0021: glLockArraysEXT/glUnlockArraysEXT around every batch (Mesa only records the range)
variant_lockarrays() { oa_x +set r_lockArrays 1; }
# 0022: the sky box as a glBegin strip per row, a call per vertex (the demo shows no sky: expect no
# change here; islanddm drops from ~1450 sky calls a frame to 18)
variant_skystrips() { oa_x +set r_skyArrays 0; }
variant_allbatchoff() { oa_x +set r_glStateCache 0 +set r_lockArrays 1 +set r_skyArrays 0; }
# Loads during play (ioquake3 0025-0026, docs/STALLS.md "OpenArena"). Five bots join the demo, one
# every 200 frames (bot_minplayers, and qagame's loaddefered makes the cgame load each at once): 3
# models, 3 skins, 3-6 textures each, 4 stalls of 130-400 ms a pass on the PS3; and every pass
# reloads the map: 3 load-screen stalls of 340/700/380 ms (402 image files read, decoded, DXT on
# the PPE), 10 of the run's 17.6 s of stalls (games/bench/passsplit.py tells the two apart). Both
# switches are on in base (llvmpipe: picture identical over three passes; the second pass on loads
# nothing during the demo, the reload 0.46 -> 0.17 s). The first pass still loads them (unmeasured).
# 0025: r_keepImages 0, every texture freed and its file loaded again at each map load
variant_keepimg0() { oa_x +set r_keepImages 0; }
# 0026: cl_preloadLate 0, the joining players' models and skins loaded when they join
variant_preload0() { oa_x +set cl_preloadLate 0; }
variant_loads0() { oa_x +set r_keepImages 0 +set cl_preloadLate 0; }
# 0026 (opt-in, changes the picture): loaddefered ignored, the bots keep a substitute model (the
# demo player's) for the whole demo: what the joins cost against no player loads at all
variant_defer() { oa_x +set cl_deferPlayers 1; }
# (Before 0024 the atlas was the default and the variant was mergelm0; the notes below were
# measured that way, "base" there is the atlas on, "mergelm0" the atlas off.)
# The atlas's ~5% (ps3.362 on Mesa ps3.299) is not in the frames: the window's frames of 100 ms or
# less render at the same rate with and without it (report.py smooth: 64.0-64.3 fps both ways),
# the loss is stall time (stall_s 37-45 s with it, 30-37 without). 0023 separates the atlas
# textures from the batching. mergelm2: the atlas pages with a batch per lightmap (199 draws, the
# order of mergelm0): stalls as base = the big page's memory, as mergelm0 = the batching.
# lmsize512 / lmsize256: pages of up to 16 / 4 lightmaps (92 / 132 draws, 1 MB / 256 KB uploads).
variant_mergelm2() { oa_x +set r_mergeLightmaps 2; }
variant_lmsize512() { oa_x +set r_mergeLightmapsSize 512; }
variant_lmsize256() { oa_x +set r_mergeLightmapsSize 256; }
# tc1 with no window manager, as step 0 ran: the window never reached the screen (SDL's fullscreen
# went unanswered), so this measures what showing the frames costs
variant_tc1nowm() { OA_TC=1; oa_cmd; WM=; }

# OpenAL Soft measurements (docs/SPU-AUDIO.md, M0): the shared variants of openal.inc, and no sound
# at all (step 0's Z)
. "$here/recipes/openal.inc"
variant_allog() { oal_variant allog; }
variant_b16() { oal_variant b16; }
variant_f44() { oal_variant f44; }
variant_b16f44() { oal_variant b16f44; }
variant_p1024() { oal_variant p1024; }
variant_alnull() { oal_variant alnull; }
variant_f32() { oal_variant f32; }
variant_lin() { oal_variant lin; }
variant_nosound() { OA_X+=(+set s_initsound 0); oa_cmd; }
# Mesa ps3.282: a submission every 100 draws (0026), the Armagetron fix, checked here
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
# DXT1 of RGB sources (libspeq 2026-10-18, ps3-spu/doc/dxt-rgb.md): an RGB source (3 bytes a pixel,
# "speq: s3tc dxt1 ... comps 3" lines) used to be encoded on the PPE (165 ms for a 1024x1024, 43 ms
# for a 512x512: hitches when a texture loads mid-demo); now the SPEs take it. rgbppe sends RGB back
# to the PPE encoder (the old behaviour), rgbspu is the default with the same logging, so the pair
# differs in the switch alone. opengl1 uploads its opaque textures as GL_RGBA data (RGBA DXT1,
# comps 4), so count the comps 3 lines in game.log first: without them the pair measures nothing.
variant_rgbspu() { ENV+=(SPEQ_DEBUG=1); }
variant_rgbppe() { ENV+=(SPEQ_S3TC_RGB=ppe SPEQ_DEBUG=1); }
