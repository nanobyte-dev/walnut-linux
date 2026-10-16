# dsda-doom 0.30.0 + Freedoom 2 (stands in for PrBoom+ GL): timedemo of Freedoom's own DEMO1 lump,
# fullscreen 1920x1080. Software renderer first (base, safest), GL as a variant: GL mode uploads
# indexed textures as GL_RG, which nv30 lacks (docs/GAME-PLAN.md), so expect missing textures there.
SUITE=1
COMM=dsda-doom
BACKUP=("$HOME/.dsda-doom/dsda-doom.cfg")
# The software renderer draws 960x540 (SDL scales it to the screen): at 1920x1080 the PPE managed
# ~5 fps and the timedemo didn't end within 180 s
DD_MODE=sw DD_W=960 DD_H=540
dd_cmd() {
	CMD=(/usr/games/dsda-doom -iwad /usr/share/games/doom/freedoom2.wad -vidmode $DD_MODE
		-width $DD_W -height $DD_H -fullscreen -timedemo demo1)
}
dd_cmd
FPS_RE='Timed [0-9]+ gametics in [0-9]+ realtics = ([0-9.]+) frames per second'
LOAD_MARK='^(R_Init|S_Init|HU_Init|ST_Init)'
variant_gl() { DD_MODE=gl DD_W=1920 DD_H=1080; dd_cmd; }
variant_sw1080() { DD_W=1920 DD_H=1080; dd_cmd; }
# SDL's GLES2 renderer instead of its desktop-GL one (the software frame goes to the screen through
# an SDL texture): the 960x540 software run was green-tinted
variant_gles() { ENV+=(SDL_RENDER_DRIVER=opengles2); }
# SDL's software renderer: no GL at all in presenting the frame (tells a GPU-path colour bug from
# the game's or SDL's own)
variant_sdlsw() { ENV+=(SDL_RENDER_DRIVER=software); }
