# OpenArena (ioquake3 0.8.8): timedemo of bench.dm_71 (as games/openarena/step0.sh), renderer and
# texture compression per variant. The game archives +set cvars to q3config.cfg on quit.
SUITE=1
COMM=ioquake3
BACKUP=("$HOME/.openarena/baseoa/q3config.cfg")
setup() {
	mkdir -p "$HOME/.openarena/baseoa/demos"
	cmp -s "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71" ||
		cp "$ASSETS/openarena-bench.dm_71" "$HOME/.openarena/baseoa/demos/bench.dm_71"
}
OA_TD=(+set r_swapInterval 0 +set com_maxfps 0 +set timedemo 1 +set nextdemo quit +demo bench)
OA_R=opengl1; OA_TC=1; OA_X=()
oa_cmd() { CMD=(/usr/games/openarena +set cl_renderer $OA_R +set r_ext_compressed_textures $OA_TC "${OA_X[@]}" "${OA_TD[@]}"); }
oa_cmd
FPS_RE='[0-9]+ frames [0-9.]+ seconds ([0-9.]+) fps'
MAXMS_RE='[0-9.]+ fps [0-9.]+/[0-9.]+/([0-9.]+)/'
LOAD_RE='CL_InitCGame: ([0-9.]+) seconds'
# opengl1, uncompressed textures (step 0's tc0)
variant_tc0() { OA_TC=0; oa_cmd; }
# opengl1, compressed (shipped default, step 0's tc1)
variant_tc1() { OA_TC=1; oa_cmd; }
# renderergl2 with its expensive features off (docs/GAME-SUITE.md "min"), then Debian's defaults
variant_gl2min() {
	OA_R=opengl2
	OA_X=(+set r_normalMapping 0 +set r_specularMapping 0 +set r_deluxeMapping 0 +set r_sunlightMode 0
		+set r_sunShadows 0 +set r_dlightMode 0 +set r_depthPrepass 0 +set r_hdr 0 +set r_postProcess 0
		+set r_toneMap 0 +set r_autoExposure 0 +set r_ssao 0 +set r_cubeMapping 0
		+set r_ext_framebuffer_object 0)
	oa_cmd
}
variant_gl2() { OA_R=opengl2; oa_cmd; }
# tc1 with no window manager, as step 0 ran: the window never reached the screen (SDL's fullscreen
# went unanswered), so this measures what showing the frames costs
variant_tc1nowm() { OA_TC=1; oa_cmd; WM=; }
