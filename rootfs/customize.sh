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

[DHCPv4]
# Same client ID Petitboot's udhcpc sends (01:<MAC>). networkd's default (a DUID) made the
# router's DHCP server (Kea) see the .77 lease as another client's, so Petitboot kept getting
# a different address (ALLOC_ENGINE_V4_DISCOVER_ADDRESS_CONFLICT).
ClientIdentifier=mac
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

# /dev/ps3gpu_* nodes for the ps3fb GPU interface (char major is dynamic: read /proc/devices)
cat > "$R/usr/local/sbin/ps3gpu-nodes" <<'EOS'
#!/bin/sh
major=$(awk '$2=="ps3fb" {print $1}' /proc/devices)
[ -n "$major" ] || exit 0
i=0
for n in vram fifo ctrl driver reports; do
	[ -e /dev/ps3gpu_$n ] || mknod -m 660 /dev/ps3gpu_$n c "$major" $i
	chgrp video /dev/ps3gpu_$n
	i=$((i+1))
done
EOS
chmod 755 "$R/usr/local/sbin/ps3gpu-nodes"
cat > "$R/etc/systemd/system/ps3gpu-nodes.service" <<'EOS'
[Unit]
Description=Create /dev/ps3gpu_* device nodes
After=systemd-udevd.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/ps3gpu-nodes

[Install]
WantedBy=multi-user.target
EOS
chroot "$R" systemctl enable ps3gpu-nodes.service
chroot "$R" usermod -aG video,render tibi

# ps3vram can't get VRAM while ps3fb owns it all, and the RSX driver uses VRAM for the GPU
echo "blacklist ps3vram" > "$R/etc/modprobe.d/ps3vram.conf"

# Swap: zram (compressed RAM). Swap over NFS deadlocked under memory pressure ("Write error
# -12 on dio swapfile": writing pages out needs memory), and the PS3's HDD regions belong to
# GameOS, so there is no disk to swap to.
cat > "$R/usr/local/sbin/ps3-swap" <<'EOS'
#!/bin/sh
modprobe zram num_devices=1 || exit 1
echo lzo-rle > /sys/block/zram0/comp_algorithm 2>/dev/null
# LXQt alone swaps out ~160 MB; pages compress about 5:1 (lzo-rle), so this costs ~80 MB when full
echo 384M > /sys/block/zram0/disksize
mkswap /dev/zram0 >/dev/null
exec swapon -p 100 /dev/zram0
EOS
chmod 755 "$R/usr/local/sbin/ps3-swap"
cat > "$R/etc/systemd/system/ps3-swap.service" <<'EOS'
[Unit]
Description=Compressed swap in RAM (zram)
After=systemd-modules-load.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/ps3-swap
ExecStop=/sbin/swapoff /dev/zram0

[Install]
WantedBy=multi-user.target
EOS
chroot "$R" systemctl enable ps3-swap.service

# Memory reserve for allocations that can't wait for reclaim (network receive buffers): under
# memory pressure gelic could not refill its receive ring and the PS3 dropped off the network,
# hanging everything on the NFS root (2026-10-15, two hangs without an OOM message). The kernel's
# default reserve on 177 MB is ~1.4 MB.
echo "vm.min_free_kbytes = 8192" > "$R/etc/sysctl.d/60-ps3-memory.conf"

# Xorg on the RSX (docs/DRIVER-PLAN.md Phase 6): modesetting on ps3rsx's card with glamor,
# using the patched Mesa in /home/tibi/mesa (deployed by `make mesa-deploy`). ld.so.conf makes
# the setuid Xorg pick it up (it ignores LD_LIBRARY_PATH).
mkdir -p "$R/etc/X11/xorg.conf.d"
cat > "$R/etc/X11/xorg.conf.d/20-ps3rsx.conf" <<'EOS'
Section "ServerFlags"
	Option "AutoAddGPU" "false"
EndSection

Section "Device"
	Identifier "RSX"
	Driver "modesetting"
	Option "kmsdev" "/dev/dri/card0"
	Option "AccelMethod" "glamor"
	Option "ShadowFB" "true"
EndSection
EOS
echo /home/tibi/mesa/lib > "$R/etc/ld.so.conf.d/00-ps3mesa.conf"

# Desktop: ps3rsx at boot, autologin into LXQt, gamepad pointer (docs/DESKTOP-PLAN.md)
sh "$(dirname "$0")/desktop-setup.sh" "$R"
