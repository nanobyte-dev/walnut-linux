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
. "$BENCH_LIB/smoke.inc"
CMD=(/usr/games/trigger-rally)
TR_CFG=$HOME/.local/share/trigger-rally/trigger-rally-0.6.6.1.config
TR_CHEAP=1
BACKUP=("$TR_CFG")
TR_FRESH=0
setup() {
	# fresh: no configuration, what a new player gets (/etc/trigger-rally.config.defs)
	[ "$TR_FRESH" = 1 ] && { rm -f "$TR_CFG"; return 0; }
	local def=/etc/trigger-rally.config.defs
	[ -e "$def" ] || def=/usr/share/games/trigger-rally/trigger-rally.config.defs
	mkdir -p "${TR_CFG%/*}"
	sed -E -e '/<video/,/\/>/{s/automatic="yes"/automatic="no"/;s/width="[0-9]+"/width="1920"/;s/height="[0-9]+"/height="1080"/;s/fullscreen="no"/fullscreen="yes"/}' \
		"$def" > "$TR_CFG"
	[ "$TR_CHEAP" = 1 ] && sed -i -E '/<graphics/,/\/>/{s/anisotropy="[^"]*"/anisotropy="off"/;s/foliage="yes"/foliage="no"/;s/weather="yes"/weather="no"/;s/dirteffect="yes"/dirteffect="no"/;s/snowflaketype="textured"/snowflaketype="point"/}' "$TR_CFG"
	return 0
}
variant_full() { TR_CHEAP=0; }
# Into a race without the user (games night 2026-10-28): the menus are mouse-only (Return and the arrows
# do nothing there), so the pointer clicks through them via XTEST (xkeys.py @X,Y at 1920x1080) 10 s after
# the window; TR_CLICKS are the steps, then the throttle (Up, held 3 s) a few times.
TR_KEYS=1
TR_CLICKS=(@950,620 +1 @960,626 +4 @550,300 +1 @556,306 +5 @1560,1016 +1 @1566,1022 +6 Return +6 @950,350 +1 @960,360 +10)	# "single race", "Aegyptian", "race", the car (Return, else a click on it)
during() {
	[ "$TR_KEYS" = 1 ] || return 0
	smoke_xkeys Trigger 25 "${TR_CLICKS[@]}"
	XKEYS_HOLD=3 python3 "$BENCH_LIB/xkeys.py" Trigger Up Up Up
	shot_mark
}
SHOT_FROM=mark
variant_menu() { TR_KEYS=0; SHOT_STATIC=1; }
variant_fresh() { TR_FRESH=1; TR_KEYS=0; SHOT_STATIC=1; }
variant_freshrace() { TR_FRESH=1; }	# the defaults (foliage, weather, anisotropy 4) in the race
