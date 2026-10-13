#!/bin/bash
# mmdebstrap customize hook: $1 is the target root directory.
# Environment: PUBKEY_FILE (SSH public key to authorize for user tibi).
set -euo pipefail
R="$1"

echo ps3 > "$R/etc/hostname"
printf '127.0.0.1\tlocalhost\n127.0.1.1\tps3\n' > "$R/etc/hosts"

# Root is NFS, mounted by the kernel (root=/dev/nfs), so fstab has no root entry
cat > "$R/etc/fstab" <<'EOF'
tmpfs          /tmp  tmpfs  defaults,size=64M 0 0
EOF

# DHCP on the wired port via systemd-networkd
mkdir -p "$R/etc/systemd/network"
cat > "$R/etc/systemd/network/20-wired.network" <<'EOF'
[Match]
Name=e*

[Network]
DHCP=yes
# The kernel already configured this interface (ip=dhcp) and root is on NFS over it:
# never drop the address, or the root filesystem disappears.
KeepConfiguration=yes
EOF
chroot "$R" systemctl enable systemd-networkd ssh

# SSH host keys are random, so they're not baked into the image (keeps builds reproducible).
# Generate them on first boot instead.
rm -f "$R"/etc/ssh/ssh_host_*
cat > "$R/etc/systemd/system/ssh-hostkeys.service" <<'EOF'
[Unit]
Description=Generate SSH host keys on first boot
Before=ssh.service
ConditionPathExistsGlob=!/etc/ssh/ssh_host_*_key

[Service]
Type=oneshot
ExecStart=/usr/bin/ssh-keygen -A

[Install]
WantedBy=multi-user.target
EOF
chroot "$R" systemctl enable ssh-hostkeys.service

# User with key-only SSH login and passwordless sudo (test box on the LAN)
chroot "$R" useradd -m -s /bin/bash -G sudo tibi
chroot "$R" passwd -l tibi
install -d -m 700 -o 1000 -g 1000 "$R/home/tibi/.ssh"
install -m 600 -o 1000 -g 1000 "$PUBKEY_FILE" "$R/home/tibi/.ssh/authorized_keys"
echo 'tibi ALL=(ALL) NOPASSWD:ALL' > "$R/etc/sudoers.d/tibi"
chmod 440 "$R/etc/sudoers.d/tibi"
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/' "$R/etc/ssh/sshd_config"

# Root login on the local console (tty1) for recovery, no password
chroot "$R" passwd -d root
