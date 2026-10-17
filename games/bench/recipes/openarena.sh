# OpenArena (ioquake3 0.8.8): timedemo of bench.dm_71 (as games/openarena/step0.sh), renderer and
# texture compression per variant. The game archives +set cvars to q3config.cfg on quit.
SUITE=1
COMM=ioquake3
SOUND_AT=25	# after the level load (a cold first run takes ~12 s), during the demo
BACKUP=("$HOME/.openarena/baseoa/q3config.cfg")
setup() {
	mkdir -p "$HOME/.openarena/baseoa/demos"
	cmp -s "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71" ||
		cp "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71"
}
# at the demo's end meminfo prints the hunk in use (parse(): hunk_*), with the level still loaded.
# The engine cuts its command line at 1023 characters (MAX_STRING_CHARS, sys_main.c): gl2min_up,
# the longest, is ~800 with the wrapper's own +set; past that, +demo bench is lost and the menu stays.
OA_TD=(+set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +set nextdemo "meminfo; quit" +demo bench)
OA_R=opengl1; OA_TC=1; OA_X=()
oa_cmd() { CMD=(/usr/games/openarena +set cl_renderer $OA_R +set r_ext_compressed_textures $OA_TC "${OA_X[@]}" "${OA_TD[@]}"); }
oa_cmd
FPS_RE='[0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='[0-9.]+ fps [0-9.]+/[0-9.]+/([0-9.]+)/'
LOAD_RE='CL_InitCGame: +([0-9.]+) seconds'
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
# renderergl2 with its expensive features off (docs/GAME-SUITE.md "min"), then Debian's defaults.
# Debian's ioquake3 compiles 97 GLSL programs (gl2: 115, ~30 s on the PPE) at start and again when
# the demo loads its map (the client restarts the renderer at every map load), so the gl2 runs get
# 240 s. Our ioquake3 (ioquake3/patches, +ps3) cuts that memory; its defaults are on in gl2min and
# gl2, and each gl2min_* variant below turns one change back (or an opt-in one on) to measure it.
OA_GL2MIN=(+set r_normalMapping 0 +set r_specularMapping 0 +set r_deluxeMapping 0 +set r_sunlightMode 0
	+set r_sunShadows 0 +set r_dlightMode 0 +set r_depthPrepass 0 +set r_hdr 0 +set r_postProcess 0
	+set r_toneMap 0 +set r_autoExposure 0 +set r_ssao 0 +set r_cubeMapping 0
	+set r_ext_framebuffer_object 0)
oa_gl2min() { OA_R=opengl2; OA_X=("${OA_GL2MIN[@]}" "$@"); oa_cmd; DURATION=240; }
variant_gl2min() { oa_gl2min; }
variant_gl2() { OA_R=opengl2; oa_cmd; DURATION=240; }
# every switch of ours back to upstream (Debian's behaviour, except the VAO cache and the model
# tangents, which have no switch: for those, compare with Debian's deb)
variant_gl2min_up() { oa_gl2min +set r_glslKeepShaders 1 +set r_glslMaxBones -1 +set r_glslAllPrograms 1 +set r_glslKeepPrograms 0 +set r_keepModelVerts 1; }
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
variant_nosound() { OA_X+=(+set s_initsound 0); oa_cmd; }
# Mesa ps3.282: a submission every 100 draws (0026), the Armagetron fix, checked here
variant_kick100() { ENV+=(NV30_PS3_KICK_DRAWS=100); }
