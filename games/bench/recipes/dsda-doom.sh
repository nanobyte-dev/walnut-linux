# dsda-doom 0.30.0 + Freedoom 2 (stands in for PrBoom+ GL): timedemo of Freedoom's own DEMO1 lump,
# fullscreen 1920x1080. Software renderer first (base, safest), GL as a variant: GL mode uploads
# indexed textures as GL_RG, which nv30 lacks (docs/GAME-PLAN.md), so expect missing textures there.
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
