# The PS3 desktop (ps3-desktop): the user named in /etc/default/ps3-desktop gets X on tty1 from
# the login shell (no display manager: RAM). Not exec: if X fails, the shell stays usable.
# touch ~/.no-desktop to skip it.
# tty1 may still be in graphics mode, showing the boot splash (the autologin doesn't reset it):
# it goes back to text mode (KDSETMODE KD_TEXT) whenever no X runs on it.
if [ "$(tty 2>/dev/null)" = /dev/tty1 ] && [ -z "${DISPLAY:-}" ]; then
	PS3_DESKTOP_USER=
	[ -r /etc/default/ps3-desktop ] && . /etc/default/ps3-desktop
	if [ -n "$PS3_DESKTOP_USER" ] && [ "$(id -un)" = "$PS3_DESKTOP_USER" ] && [ ! -e "$HOME/.no-desktop" ]; then
		# X and the window manager in ps3-display.slice (game mode: kept resident, see ps3-game)
		systemd-run --user --scope --slice=ps3-display.slice -q \
			startx /usr/bin/ps3-session -- -nolisten tcp > "$HOME/.xsession.log" 2>&1
		ps3_x_status=$?
		python3 -c 'import fcntl; fcntl.ioctl(0, 0x4B3A, 0)' 2>/dev/null
		echo "X exited ($ps3_x_status): log in ~/.xsession.log; touch ~/.no-desktop to skip the desktop"
		unset ps3_x_status
	else
		python3 -c 'import fcntl; fcntl.ioctl(0, 0x4B3A, 0)' 2>/dev/null
	fi
	unset PS3_DESKTOP_USER
fi
