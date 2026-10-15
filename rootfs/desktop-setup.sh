#!/bin/sh
# Boot straight into LXQt on the RSX (docs/DESKTOP-PLAN.md). Called by customize.sh with the
# target root; can be re-run on the live system as root with "/".
set -eu
R="$1"

# ps3rsx (the RSX driver) loads at boot from the ps3rsx package (modules-load.d)

# No display manager (RAM): autologin on tty1, X from the login shell. Other ttys keep the
# normal logins (root without a password on the console, for recovery).
mkdir -p "$R/etc/systemd/system/getty@tty1.service.d"
cat > "$R/etc/systemd/system/getty@tty1.service.d/autologin.conf" <<'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin tibi --noclear %I $TERM
EOF
cat > "$R/home/tibi/.bash_profile" <<'EOF'
[ -f ~/.bashrc ] && . ~/.bashrc
# The desktop on tty1 (docs/DESKTOP-PLAN.md). Not exec: if X fails, this shell stays usable.
if [ "$(tty)" = /dev/tty1 ] && [ -z "${DISPLAY:-}" ] && [ ! -e ~/.no-desktop ]; then
	# X and the window manager in ps3-display.slice (game mode: kept resident, see ps3-game)
	systemd-run --user --scope --slice=ps3-display.slice -q \
		startx /usr/local/bin/ps3-session -- -nolisten tcp > ~/.xsession.log 2>&1
	echo "X exited ($?): log in ~/.xsession.log; touch ~/.no-desktop to skip the desktop"
fi
EOF
chroot "$R" chown 1000:1000 /home/tibi/.bash_profile

# X session: Openbox (~129 MB in all with network/volume/USB; chosen over LXQt, IceWM, Xfce and
# Enlightenment, docs/DESKTOP-PLAN.md).
# A splash shows until the desktop window is up.
cat > "$R/usr/local/bin/ps3-session" <<'EOS'
#!/bin/sh
xsetroot -solid '#14304d'
xmessage -center -buttons '' -bg '#14304d' -fg white -bd '#14304d' \
	-fn '-misc-fixed-bold-r-normal--18-*-*-*-*-*-iso10646-1' \
	"   Starting the desktop on the RSX...   " &
splash=$!
# the desktop window (PCManFM's, either version, or Xfce's) covers the screen once the desktop
# is ready; never longer than 2 minutes (Enlightenment draws its own)
(t=0; while kill -0 $splash 2>/dev/null; do
	xwininfo -root -tree 2>/dev/null |
		grep -qE 'pcmanfm-desktop0|"pcmanfm" "Pcmanfm"|"xfdesktop" "Xfdesktop"' && sleep 2 ||
		[ $t -gt 120 ] || { sleep 1; t=$((t + 1)); continue; }
	kill $splash
done) &
# gvfs and the tray applets use the user bus (dbus-user-session). Openbox: tint2 panel (jgmenu, tray, clock),
# PCManFM desktop icons, network and volume applets, notifications, polkit agent. Bluetooth:
# blueman from the menu, no resident tray (~60 MB of Python). GTK apps don't load the GL
# driver just to probe it (GDK_GL=disable: RAM and GART for nothing; games use GL directly).
export GDK_GL=disable
# The helpers run in ps3-desktop.slice, which ps3-game freezes and pushes out of RAM while a
# game runs; openbox stays with X in ps3-display.slice.
# The user bus and the services it starts need to know the display
systemctl --user import-environment DISPLAY XAUTHORITY
dbus-update-activation-environment --systemd DISPLAY XAUTHORITY 2>/dev/null
d() { systemd-run --user --scope --slice=ps3-desktop.slice -q "$@" & }
d pcmanfm --desktop; d tint2; d nm-applet; d pnmixer; d lxpolkit; d dunst
exec openbox
EOS
chmod 755 "$R/usr/local/bin/ps3-session"

# Game mode (docs/PLAN.md "Game mode"): cgroup v2 groups in the user's systemd. With 214 MB of
# RAM, a game and X compete with everything else; when X's pages were swapped out the game
# stalled for seconds on every frame X had to swap back in. memory.low keeps the display and
# the game resident and makes reclaim take from everything else first; ps3-game also freezes
# the desktop helpers and reclaims their memory while it runs. The user slice carries the
# protection the groups inside it share (memory_recursiveprot).
mkdir -p "$R/etc/systemd/user" "$R/etc/systemd/system/user-.slice.d" "$R/etc/systemd/system/user@.service.d"
printf '%s\n' '[Unit]' 'Description=PS3 display: X and the window manager' '' '[Slice]' \
	'MemoryLow=64M' 'CPUWeight=200' > "$R/etc/systemd/user/ps3-display.slice"
printf '%s\n' '[Unit]' 'Description=PS3 desktop helpers (frozen while a game runs)' '' '[Slice]' \
	'CPUWeight=50' > "$R/etc/systemd/user/ps3-desktop.slice"
printf '%s\n' '[Unit]' 'Description=PS3 game (ps3-game)' '' '[Slice]' \
	'MemoryLow=128M' 'CPUWeight=400' > "$R/etc/systemd/user/ps3-game.slice"
printf '%s\n' '[Slice]' 'MemoryLow=176M' > "$R/etc/systemd/system/user-.slice.d/50-ps3-game-mode.conf"
# System services that only serve the desktop (automount, authorisation prompts) wait while a
# game runs: frozen and pushed to swap; a USB stick plugged in meanwhile mounts afterwards.
# Not NetworkManager (online games) or bluetoothd (the gamepads).
mkdir -p "$R/usr/local/sbin" "$R/etc/sudoers.d"
cat > "$R/usr/local/sbin/ps3-game-mode" <<'EOS'
#!/bin/sh
# ps3-game-mode on|off (root; ps3-game runs it through sudo)
units="udisks2.service polkit.service"
case "$1" in
on)	for u in $units; do
		systemctl freeze "$u" 2>/dev/null &&
			echo 64M > "/sys/fs/cgroup/system.slice/$u/memory.reclaim" 2>/dev/null
	done; true ;;
off)	for u in $units; do systemctl thaw "$u" 2>/dev/null; done; true ;;
*)	echo "usage: ps3-game-mode on|off" >&2; exit 2 ;;
esac
EOS
chmod 755 "$R/usr/local/sbin/ps3-game-mode"
echo 'tibi ALL=(root) NOPASSWD: /usr/local/sbin/ps3-game-mode' > "$R/etc/sudoers.d/ps3-game-mode"
chmod 440 "$R/etc/sudoers.d/ps3-game-mode"
# D-Bus-activated user services (gvfs: volume monitors, trash, metadata) with the desktop
for u in gvfs-daemon gvfs-metadata gvfs-udisks2-volume-monitor gvfs-mtp-volume-monitor \
	gvfs-gphoto2-volume-monitor gvfs-afc-volume-monitor gvfs-goa-volume-monitor; do
	mkdir -p "$R/etc/systemd/user/$u.service.d"
	printf '%s\n' '[Service]' 'Slice=ps3-desktop.slice' > "$R/etc/systemd/user/$u.service.d/50-ps3-desktop.conf"
done
printf '%s\n' '[Service]' 'MemoryLow=176M' > "$R/etc/systemd/system/user@.service.d/50-ps3-game-mode.conf"

# PCManFM's desktop: Debian wallpaper; home, trash and mounted drives (USB sticks) as icons
for d in "$R/etc/xdg/pcmanfm/default" "$R/home/tibi/.config/pcmanfm/default"; do
	mkdir -p "$d"
	cat > "$d/desktop-items-0.conf" <<'EOS'
[*]
wallpaper_mode=crop
wallpaper_common=1
wallpaper=/usr/share/images/desktop-base/default
desktop_bg=#14304d
desktop_fg=#ffffff
desktop_shadow=#000000
desktop_font=Sans 11
show_wm_menu=0
show_documents=1
show_trash=1
show_mounts=1
EOS
done
chroot "$R" chown -R 1000:1000 /home/tibi/.config

# gvfs volume monitors for iPhones (afc) and GNOME online accounts (goa) run in every session
# for nothing on a PS3 (~1.3 MB each): moved aside so gio never starts them (survives upgrades).
# Cameras (gphoto2), phones (mtp) and drives (udisks2) stay.
for m in afc goa; do
	f=/usr/share/gvfs/remote-volume-monitors/$m.monitor
	[ -e "$R$f" ] && chroot "$R" dpkg-divert --quiet --local --rename --divert "$f.disabled" --add "$f"
done

# Openbox look: Greybird (window borders and GTK), Papirus icons; tint2 launchers for the
# application menu (jgmenu), files and a terminal
mkdir -p "$R/home/tibi/.config/openbox" "$R/home/tibi/.config/tint2" "$R/home/tibi/.config/gtk-3.0"
# Games start fullscreen (ClassiCube has no option for it), without decorations
sed -e 's|<name>Clearlooks</name>|<name>Greybird</name>|' \
	-e 's|</applications>|  <application class="ClassiCube">\n    <decor>no</decor>\n    <fullscreen>yes</fullscreen>\n  </application>\n</applications>|' \
	"$R/etc/xdg/openbox/rc.xml" > "$R/home/tibi/.config/openbox/rc.xml"
sed -e "/^launcher_item_app/d" \
	"$R/etc/xdg/tint2/tint2rc" > "$R/home/tibi/.config/tint2/tint2rc"
sed -i '/^launcher_icon_theme_override/a launcher_item_app = jgmenu.desktop\nlauncher_item_app = pcmanfm.desktop\nlauncher_item_app = rxvt-unicode.desktop' \
	"$R/home/tibi/.config/tint2/tint2rc"
printf '[Settings]\ngtk-theme-name=Greybird\ngtk-icon-theme-name=Papirus\ngtk-font-name=Sans 10\n' \
	> "$R/home/tibi/.config/gtk-3.0/settings.ini"
chroot "$R" chown -R 1000:1000 /home/tibi/.config

# Any console user may start X (the legacy wrapper runs it with root rights for KMS/input)
printf 'allowed_users=anybody\nneeds_root_rights=yes\n' > "$R/etc/X11/Xwrapper.config"

# Games run through ps3-game: the gamepad stops moving the X pointer meanwhile (the pointer
# motion would also turn the game's camera), and the game gets the gamepad to itself
cat > "$R/usr/local/bin/ps3-game" <<'EOS'
#!/bin/sh
# ps3-game <command...>: runs a game in game mode: in ps3-game.slice (kept resident), the
# desktop helpers frozen and their memory reclaimed, the gamepads detached from the X pointer
pads() {	# X device ids of the gamepads (udev ID_INPUT_JOYSTICK) attached as pointers
	for d in /sys/class/input/event*; do
		udevadm info -q property "$d" | grep -q '^ID_INPUT_JOYSTICK=1' || continue
		name=$(cat "$d/device/name")
		xinput list | grep -F "$name" | grep 'slave  pointer' | sed 's/.*id=\([0-9]*\).*/\1/'
	done
}
# the cgroup files directly: they are delegated to the user (systemctl --user freeze needs a
# user D-Bus session, which this desktop doesn't run)
cg=/sys/fs/cgroup$(systemctl --user show -P ControlGroup ps3-desktop.slice 2>/dev/null)
thaw() { echo 0 > "$cg/cgroup.freeze" 2>/dev/null; sudo -n /usr/local/sbin/ps3-game-mode off; }
ids=$(pads)
for i in $ids; do xinput disable "$i"; done
if [ -w "$cg/cgroup.freeze" ] && echo 1 > "$cg/cgroup.freeze"; then
	echo 256M > "$cg/memory.reclaim" 2>/dev/null	# as much as it gives up
fi
sudo -n /usr/local/sbin/ps3-game-mode on
systemd-run --user --scope --slice=ps3-game.slice -q "$@" &
game=$!
# thaws even if this script is killed: the watcher outlives it while the game runs
(while kill -0 $game 2>/dev/null; do sleep 1; done; thaw) &
trap 'kill $game 2>/dev/null' INT TERM
wait $game
ret=$?
thaw
for i in $ids; do xinput enable "$i"; done
exit $ret
EOS
chmod 755 "$R/usr/local/bin/ps3-game"

# OpenArena: the GL1 renderer (the GLSL one ran out of RAM compiling its shaders),
# fullscreen at the display's resolution, vsync, DualShock 3 analog (left stick
# moves, right stick aims; R2 fire, L2 zoom, X jump, O crouch, R1/L1 next/previous weapon,
# Start menu, Select scores)
mkdir -p "$R/home/tibi/.openarena/baseoa"
cat > "$R/home/tibi/.openarena/baseoa/autoexec.cfg" <<'EOF'
seta cl_renderer "opengl1"
seta r_mode "-2"
seta r_fullscreen "1"
seta r_swapInterval "1"
seta com_maxfps "60"
seta in_joystick "1"
seta in_joystickUseAnalog "1"
bind PAD0_RIGHTTRIGGER "+attack"
bind PAD0_LEFTTRIGGER "+zoom"
bind PAD0_A "+moveup"
bind PAD0_B "+movedown"
bind PAD0_RIGHTSHOULDER "weapnext"
bind PAD0_LEFTSHOULDER "weapprev"
bind PAD0_X "+button2"
bind PAD0_START "togglemenu"
bind PAD0_BACK "+scores"
EOF
mkdir -p "$R/home/tibi/.local/share/applications"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=OpenArena' \
	'Comment=Arena shooter (ioquake3)' 'Exec=ps3-game openarena' 'Icon=openarena' \
	'Categories=Game;ActionGame;' > "$R/home/tibi/.local/share/applications/openarena.desktop"
chroot "$R" chown -R 1000:1000 /home/tibi/.openarena /home/tibi/.local

# Shut down / Reboot in the application menu, after a confirmation (a stray click shouldn't
# power the console off). A reboot lands in GameOS (the XMB): the OtherOS flag is set by hand.
cat > "$R/usr/local/bin/ps3-power" <<'EOS'
#!/bin/sh
# ps3-power poweroff|reboot: asks first, then shuts down or reboots (to the XMB)
case "$1" in
poweroff) q="Shut down the PS3?"; b="Shut down" ;;
reboot) q="Reboot the PS3? (it starts in the XMB)"; b="Reboot" ;;
*) echo "usage: ps3-power poweroff|reboot" >&2; exit 2 ;;
esac
xmessage -center -buttons "$b:0,Cancel:1" -default Cancel "$q" || exit 0
exec sudo -n /usr/bin/systemctl "$1"
EOS
chmod 755 "$R/usr/local/bin/ps3-power"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Shut down' 'Comment=Power off the PS3' \
	'Exec=ps3-power poweroff' 'Icon=system-shutdown' 'Categories=System;' \
	> "$R/usr/share/applications/ps3-poweroff.desktop"
printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Reboot' 'Comment=Restart the PS3 (to the XMB)' \
	'Exec=ps3-power reboot' 'Icon=system-reboot' 'Categories=System;' \
	> "$R/usr/share/applications/ps3-reboot.desktop"

# DualShock 3 (and other joysticks) move the pointer; libinput ignores joysticks
cat > "$R/etc/X11/xorg.conf.d/40-joystick-pointer.conf" <<'EOF'
Section "InputClass"
	Identifier "Gamepad as pointer"
	MatchIsJoystick "on"
	MatchDevicePath "/dev/input/event*"
	Driver "joystick"
	Option "MapAxis1" "mode=relative axis=+1x deadzone=5000"
	Option "MapAxis2" "mode=relative axis=+1y deadzone=5000"
	Option "MapButton1" "button=1"
	Option "MapButton2" "button=3"
EndSection
EOF

# RAM: blueman's helper and tray applet (Python + GTK, ~50 MB together) only on demand; pairing
# from the blueman menu entry or bluetoothctl. No PulseAudio (ALSA only).
chroot "$R" systemctl disable blueman-mechanism.service 2>/dev/null || true
mkdir -p "$R/home/tibi/.config/autostart"
printf '[Desktop Entry]\nType=Application\nName=Blueman Applet\nHidden=true\n' \
	> "$R/home/tibi/.config/autostart/blueman.desktop"
chroot "$R" chown -R 1000:1000 /home/tibi/.config
