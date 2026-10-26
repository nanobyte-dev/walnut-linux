#!/bin/bash
# The image's per-installation settings (tools/build-image.sh runs it after our packages are in):
# $1 is the target root. Everything else (networking, swap, the desktop, ...) comes from the
# ps3-system and ps3-desktop packages.
# Environment: PUBKEY_FILE (SSH public key to authorize for user tibi), PROFILE (dev or public).
set -euo pipefail
R="$1"
PROFILE="${PROFILE:-dev}"

HOSTNAME="walnut"
echo "$HOSTNAME" > "$R/etc/hostname"
printf '127.0.0.1\tlocalhost\n127.0.1.1\t%s\n' "$HOSTNAME" > "$R/etc/hosts"

# Root is NFS, mounted by the kernel (root=/dev/nfs), so fstab has no root entry (the USB-root
# installer writes its own)
cat > "$R/etc/fstab" <<'EOF'
tmpfs          /tmp  tmpfs  defaults,size=64M 0 0
EOF

# SSH host keys are random, so they're not baked into the image (keeps builds reproducible):
# ps3-system's ssh-hostkeys.service makes them on first boot
rm -f "$R"/etc/ssh/ssh_host_*

if [ "$PROFILE" = "public" ]; then
	# Public profile: root locked, no default user (first-run wizard walnut-firstboot sets up
	# the first user), no authorized_keys, no passwordless sudo
	chroot "$R" passwd -l root
	rm -f "$R/etc/sudoers.d/tibi"
	# DNS and the wired port: NetworkManager (ps3-system's 00-ps3.conf)
	ln -sf /run/NetworkManager/resolv.conf "$R/etc/resolv.conf"
else
	# Dev profile: user tibi with key-only SSH login and passwordless sudo (test box on the LAN);
	# input: games read gamepads (the DualShock 3) directly from /dev/input through SDL
	chroot "$R" useradd -m -s /bin/bash -G sudo,video,render,audio,input tibi
	chroot "$R" passwd -l tibi
	if [ -n "${PUBKEY_FILE:-}" ] && [ -f "$PUBKEY_FILE" ]; then
		install -d -m 700 -o 1000 -g 1000 "$R/home/tibi/.ssh"
		install -m 600 -o 1000 -g 1000 "$PUBKEY_FILE" "$R/home/tibi/.ssh/authorized_keys"
	fi
	echo 'tibi ALL=(ALL) NOPASSWD:ALL' > "$R/etc/sudoers.d/tibi"
	chmod 440 "$R/etc/sudoers.d/tibi"

	# Root login on the local console (tty1) for recovery, no password
	chroot "$R" passwd -d root

	# DNS from NetworkManager, which runs the wired port too (ps3-system's 00-ps3.conf); an NFS root
	# boot uses the kernel's /proc/net/pnp (ps3-nfsroot-network.service; install-nfs-root.sh links it)
	ln -sf /run/NetworkManager/resolv.conf "$R/etc/resolv.conf"
fi

# A real locale (only C existed: Qt complained, Enlightenment's language list was empty)
echo "en_US.UTF-8 UTF-8" > "$R/etc/locale.gen"
chroot "$R" locale-gen
echo "LANG=en_US.UTF-8" > "$R/etc/default/locale"
