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
# LXQt on tty1 (docs/DESKTOP-PLAN.md). Not exec: if X fails, this shell stays usable.
if [ "$(tty)" = /dev/tty1 ] && [ -z "${DISPLAY:-}" ] && [ ! -e ~/.no-desktop ]; then
	startx /usr/bin/startlxqt -- -nolisten tcp > ~/.xsession.log 2>&1
	echo "X exited ($?): log in ~/.xsession.log; touch ~/.no-desktop to skip the desktop"
fi
EOF
chroot "$R" chown 1000:1000 /home/tibi/.bash_profile

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
