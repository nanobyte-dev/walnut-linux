# Trigger Rally 0.6.6.1 (Debian ports ppc64: trigger-rally, trigger-rally-data 116 MB download):
# smoke test, not benchmarked. Menu only: the game has no command-line options, so the player starts
# a race by hand (Enter on Single Race / the arrow keys, pick an event and a stage, Enter; drive
# with the arrow keys, Space is the handbrake, R recovers, C changes the camera). Fullscreen at the
# desktop resolution comes from setup(): a copy of /etc/trigger-rally.config.defs written to the
# user's config file with 1920x1080 fullscreen and the cheaper options (no foliage, no weather, no
# dirt effect, no anisotropic filtering, point snowflakes), restored after the run.
# What it exercises: GL 2.0 compatibility with fixed function and GLEW (terrain, sky, cars), SDL2_image JPEG/PNG textures (the stage textures are big: RAM is the risk, the data is 121
# MB installed), OpenAL + ALUT for the engine and the co-driver's voice.
# Risks: terrain drawn from many large draws per frame on the PPE, GLEW picking extensions nv30
# lacks (it only prints), RAM during the stage load. Variant full: the game's own defaults
# (foliage, weather, anisotropy 4) to see what the cheap settings buy.
COMM=trigger-rally
. "$here/recipes/smoke.inc"
CMD=(/usr/games/trigger-rally)
TR_CFG=$HOME/.local/share/trigger-rally/trigger-rally-0.6.6.1.config
TR_CHEAP=1
BACKUP=("$TR_CFG")
setup() {
	local def=/etc/trigger-rally.config.defs
	[ -e "$def" ] || def=/usr/share/games/trigger-rally/trigger-rally.config.defs
	mkdir -p "${TR_CFG%/*}"
	sed -E -e '/<video/,/\/>/{s/automatic="yes"/automatic="no"/;s/width="[0-9]+"/width="1920"/;s/height="[0-9]+"/height="1080"/;s/fullscreen="no"/fullscreen="yes"/}' \
		"$def" > "$TR_CFG"
	[ "$TR_CHEAP" = 1 ] && sed -i -E '/<graphics/,/\/>/{s/anisotropy="[^"]*"/anisotropy="off"/;s/foliage="yes"/foliage="no"/;s/weather="yes"/weather="no"/;s/dirteffect="yes"/dirteffect="no"/;s/snowflaketype="textured"/snowflaketype="point"/}' "$TR_CFG"
	return 0
}
variant_full() { TR_CHEAP=0; }
