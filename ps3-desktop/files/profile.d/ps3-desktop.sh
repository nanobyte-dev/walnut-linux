# The PS3 desktop (ps3-desktop): the user named in /etc/default/ps3-desktop gets X on tty1 from
# the login shell (no display manager: RAM). Not exec: if X fails, the shell stays usable.
# touch ~/.no-desktop to skip it.
if [ "$(tty 2>/dev/null)" = /dev/tty1 ] && [ -z "${DISPLAY:-}" ] && [ ! -e "$HOME/.no-desktop" ]; then
	PS3_DESKTOP_USER=
	[ -r /etc/default/ps3-desktop ] && . /etc/default/ps3-desktop
	if [ -n "$PS3_DESKTOP_USER" ] && [ "$(id -un)" = "$PS3_DESKTOP_USER" ]; then
		# X and the window manager in ps3-display.slice (game mode: kept resident, see ps3-game)
		systemd-run --user --scope --slice=ps3-display.slice -q \
			startx /usr/bin/ps3-session -- -nolisten tcp > "$HOME/.xsession.log" 2>&1
		echo "X exited ($?): log in ~/.xsession.log; touch ~/.no-desktop to skip the desktop"
	fi
	unset PS3_DESKTOP_USER
fi
