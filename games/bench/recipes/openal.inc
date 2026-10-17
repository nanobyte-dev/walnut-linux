# OpenAL Soft measurement variants (docs/SPU-AUDIO.md, section 2: M0), shared by the recipes of the
# games that play through OpenAL Soft: openarena, yquake2, classicube, luanti. A recipe sources this
# file and defines each variant_<name>() as a one-line call, variant_b16() { oal_variant b16; },
# because run.sh finds a game's variants by grepping its recipe. variant_nosound (no sound at all)
# is the game's own switch, in each recipe. Same names in every game, so `run.sh suite "base b16"`
# runs them across the suite.
#   allog   stock settings, only OpenAL's log: the reference the others are read against
#   b16     sample-type=int16 with output-limiter=false and dither=false (both default to on for
#           integer output, which is what step 0's B measured): plug's float to S16 conversion
#           moves into OpenAL's Write
#   f44     the device at 44100 Hz: dmix's rate through ~/.asoundrc (defaults.pcm.dmix.rate; snd_ps3
#           takes 44.1 kHz) and OpenAL's frequency=44100. Sounds recorded at 44.1 kHz (409 of
#           OpenArena's 518 effects) then take the copy path instead of the cubic resampler
#   b16f44  both
#   p1024   period_size=1024 (dmix's own period): half the mixer wakeups, the same samples
#   alnull  OpenAL's null backend: it mixes in real time (renderSamples without Write), nothing
#           reaches ALSA. base - alnull is ALSA's share, alnull - nosound OpenAL's own
# Every one of them writes its alsoft.conf into the run directory (ALSOFT_CONF: read after every
# other config file) and OpenAL's log (ALSOFT_LOGLEVEL=3) to alsoft.log there; parse() adds
# oal_backend= and oal_device= (format, rate, period / buffer frames as OpenAL got them) to
# bench.txt. A ~/.alsoftrc, ~/.config/alsoft.conf (or for f44 a ~/.asoundrc) of the player's would
# change the reference: setup refuses to run then.
OAL_SET=()	# alsoft.conf [general] lines
OAL_ASOUNDRC=	# ~/.asoundrc content (f44): written by setup, removed after the run (BACKUP)
OAL_ON=
oal_settings() {	# oal_settings NAME: the settings of variant NAME (above)
	case $1 in
	allog) ;;
	b16) OAL_SET+=(sample-type=int16 output-limiter=false dither=false) ;;
	f44) OAL_SET+=(frequency=44100); OAL_ASOUNDRC='defaults.pcm.dmix.rate 44100' ;;
	b16f44) oal_settings b16 && oal_settings f44 ;;
	p1024) OAL_SET+=(period_size=1024) ;;
	alnull) OAL_SET+=(drivers=null) ;;
	*) echo "!! openal.inc: no variant $1"; return 1 ;;
	esac
}
oal_variant() {	# oal_variant NAME: variant NAME's settings, the config and log written by setup
	oal_settings "$1" || return
	[ -n "$OAL_ON" ] && return 0
	OAL_ON=1
	[ -n "$OAL_ASOUNDRC" ] && BACKUP+=("$HOME/.asoundrc")
	oal_after setup oal_setup
	oal_after parse oal_parse any
}
oal_after() {	# oal_after FUNC HOOK [any]: FUNC runs as before, then HOOK if FUNC succeeded (any: always)
	declare -F "$1" > /dev/null || eval "$1() { :; }"
	eval "oal_prev_$(declare -f "$1")"
	if [ "${3:-}" = any ]; then eval "$1() { oal_prev_$1; $2; }"
	else eval "$1() { oal_prev_$1 || return; $2; }"; fi
}
oal_setup() {
	local f
	for f in "$HOME/.alsoftrc" "${XDG_CONFIG_HOME:-$HOME/.config}/alsoft.conf"; do
		[ -e "$f" ] && { echo "!! $f exists: these runs wouldn't compare with stock OpenAL"; return 1; }
	done
	{ echo "[general]"; printf '%s\n' "${OAL_SET[@]}"; } > "$D/alsoft.conf"
	if [ -n "$OAL_ASOUNDRC" ]; then
		# BACKUP saved it already; one of ours left by a run cut short was restored (removed) then
		[ -e "$HOME/.asoundrc" ] && { echo "!! ~/.asoundrc exists: f44 would replace it"; return 1; }
		printf '%s\n' "# games/bench (openal.inc): removed after the run" "$OAL_ASOUNDRC" > "$HOME/.asoundrc"
	fi
	ENV+=(ALSOFT_CONF="$D/alsoft.conf" ALSOFT_LOGLEVEL=3 ALSOFT_LOGFILE="$D/alsoft.log")
}
oal_parse() {	# OpenAL's "Added "alsa" for playback" and "Post-reset: Stereo, Float, 48000hz, 512 / 1536 buffer"
	local l b= dev=
	[ -e "$D/alsoft.log" ] || return 0
	while IFS= read -r l; do
		[[ $l =~ Added\ \"([a-z0-9]+)\"\ for\ playback ]] && b=${BASH_REMATCH[1]}
		[[ $l =~ Post-reset:\ (.*)\ buffer ]] && dev=${BASH_REMATCH[1]// /}
	done < "$D/alsoft.log"
	echo "oal_backend=$b"; echo "oal_device=$dev"
}
