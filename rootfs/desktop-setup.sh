#!/bin/sh
# Boot straight into LXQt on the RSX (docs/DESKTOP-PLAN.md). Called by customize.sh with the
# target root; can be re-run on the live system as root with "/".
set -eu
R="$1"

# ps3rsx is built out of tree (make gpu-deploy -> ~tibi/bin); load it before X starts
cat > "$R/etc/systemd/system/ps3rsx.service" <<'EOF'
[Unit]
Description=PS3 RSX DRM driver (ps3rsx)
After=systemd-modules-load.service
Before=getty@tty1.service
ConditionPathExists=/home/tibi/bin/ps3rsx.ko

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/sbin/modprobe drm_kms_helper
ExecStart=/bin/sh -c 'grep -q "^ps3rsx " /proc/modules || /sbin/insmod /home/tibi/bin/ps3rsx.ko'

[Install]
WantedBy=multi-user.target
EOF
chroot "$R" systemctl enable ps3rsx.service

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
	startx /usr/local/bin/ps3-session -- -nolisten tcp > ~/.xsession.log 2>&1
	echo "X exited ($?): log in ~/.xsession.log; touch ~/.no-desktop to skip the desktop"
fi
EOF
chroot "$R" chown 1000:1000 /home/tibi/.bash_profile

# X session: IceWM with PCManFM's desktop (default; ~13 MB against LXQt's ~72 MB, which leaves
# room for gvfs: trash, USB drives, network), or lxqt/xfce/enlightenment/openbox written to
# ~/.config/ps3-desktop (being compared, docs/DESKTOP-PLAN.md).
# A splash shows until the desktop window is up.
cat > "$R/usr/local/bin/ps3-session" <<'EOS'
#!/bin/sh
desktop=$(cat ~/.config/ps3-desktop 2>/dev/null || echo icewm)
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
# gvfs and the tray applets need a session bus: dbus-launch for the sessions that don't start one
case "$desktop" in
lxqt)	exec startlxqt ;;
xfce)	exec startxfce4 ;;
enlightenment)
	exec dbus-launch --exit-with-session enlightenment_start ;;
openbox)
	exec dbus-launch --exit-with-session sh -c '
		pcmanfm --desktop & tint2 & nm-applet & pnmixer & lxpolkit & dunst &
		exec openbox' ;;
*)	exec dbus-launch --exit-with-session sh -c 'pcmanfm --desktop & exec icewm-session --nobg' ;;
esac
EOS
chmod 755 "$R/usr/local/bin/ps3-session"

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

# Any console user may start X (the legacy wrapper runs it with root rights for KMS/input)
printf 'allowed_users=anybody\nneeds_root_rights=yes\n' > "$R/etc/X11/Xwrapper.config"

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
