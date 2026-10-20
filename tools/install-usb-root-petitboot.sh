#!/bin/bash
# Installs the root filesystem image onto the USB disk from Petitboot: the disk isn't in use
# there, so even the root a system normally runs from can be replaced. Run via
# `make usb-root-petitboot` with the PS3 sitting in Petitboot (telnet shell on port 23, like
# netboot.sh); the disk must already have our partition table (make usb-root FORMAT=1).
#   tools/install-usb-root-petitboot.sh <rootfs.tar>
# Env: PS3_IP (Petitboot's address) PC_IP HTTP_PORT FRESH
# Petitboot's tar is GNU tar 1.23: no xattrs, so file capabilities in the image are dropped
# (only gst-ptp-helper has one). The previous installation is moved aside on the disk
# (.old-<date>), never deleted; SSH host keys, Bluetooth pairings and Wi-Fi connections come
# over from it unless FRESH=1.
set -euo pipefail
# The body is one block, parsed whole before it runs: editing this file during an install can't
# make it run a shifted fragment.
{
: "${PS3_IP:?}" "${PC_IP:?}" "${HTTP_PORT:?}"
TAR="$(readlink -f "$1")"
[ -s "$TAR" ] || { echo "missing or empty tarball: $TAR"; exit 1; }
MNT=/tmp/petitboot/mnt	# where Petitboot mounts what it finds (read-only)
TRANSCRIPT="$(mktemp)"

python3 -I -m http.server "$HTTP_PORT" --bind "$PC_IP" --directory "$(dirname "$TAR")" >/dev/null 2>&1 &
HTTP_PID=$!
# wait until it serves the image (wget into tar got an empty stream once: the server wasn't up)
timeout 20 bash -c "until curl -sfr 0-0 -o /dev/null http://$PC_IP:$HTTP_PORT/$(basename "$TAR"); do sleep 0.5; done" ||
	{ echo "!! the HTTP server doesn't serve $TAR"; exit 1; }
exec 3<>"/dev/tcp/$PS3_IP/23"
cat <&3 >"$TRANSCRIPT" &
READER_PID=$!
trap 'kill $HTTP_PID $READER_PID 2>/dev/null; rm -f "$TRANSCRIPT"' EXIT

# Send a command, then wait for its marker in the transcript (or time out)
n=0
run() { # run <timeout-seconds> <command>
	n=$((n + 1))
	printf '%s && echo DONE_""%d || echo FAIL_""%d\n' "$2" $n $n >&3
	timeout "$1" bash -c "until grep -q 'DONE_$n\|FAIL_$n' '$TRANSCRIPT'; do sleep 1; done" ||
		{ echo "!! timed out: $2"; tr -d '\r' <"$TRANSCRIPT" | tail -15; exit 1; }
	if grep -q "FAIL_$n" "$TRANSCRIPT"; then
		echo "!! failed: $2"; tr -d '\r' <"$TRANSCRIPT" | tail -15; exit 1
	fi
}

sleep 1
# The root partition is found by its label (Petitboot's udev has no by-partuuid links): exactly
# one ext4 partition labelled ps3root on a USB (sd*) disk, never the PS3's own ps3d* disks
echo "Finding the USB root partition (label ps3root)"
run 30 "set -- \$(for d in /dev/sd?2; do tune2fs -l \$d 2>/dev/null | grep -q 'volume name: *ps3root\$' && echo \$d; done); [ \$# = 1 ] && echo PART=\$1"
PART=$(tr -d '\r' <"$TRANSCRIPT" | grep -o '^PART=/dev/sd[a-z]2' | tail -1 | cut -d= -f2)
[ -n "$PART" ] || { echo "!! no single ps3root partition"; exit 1; }
# Its disk's id (the MBR's bytes 440-443, little-endian): the PARTUUIDs are <id>-01 and <id>-02.
# Installs since the random ids differ per disk (tools/usb-root-remote.sh), older ones are 50533301.
run 30 "set -- \$(dd if=${PART%2} bs=1 skip=440 count=4 2>/dev/null | od -An -tx1); [ \$# = 4 ] && echo DISKID=\$4\$3\$2\$1"
ID=$(tr -d '\r' <"$TRANSCRIPT" | grep -o '^DISKID=[0-9a-f]\{8\}' | tail -1 | cut -d= -f2)
case "$ID" in ''|00000000) echo "!! no disk id on ${PART%2}"; exit 1 ;; esac
echo "Disk id 0x$ID: root PARTUUID=$ID-02"
MNT="$MNT/$(basename "$PART")"
echo "Remounting $PART read-write ($MNT)"
run 30 "grep -q '^$PART $MNT ' /proc/mounts && mount -o remount,rw $MNT"
old=".old-$(date +%Y%m%d-%H%M%S)"
echo "Moving the previous installation to $old"
run 120 "cd $MNT && mkdir $old && for f in * .[!.]*; do case \$f in .old-*|lost+found|'*'|'.[!.]*') ;; *) mv \"\$f\" $old/ ;; esac; done; cd /"
echo "Unpacking $(du -h "$TAR" | cut -f1) (a few minutes over the network and USB)"
run 1800 "wget -q -O - http://$PC_IP:$HTTP_PORT/$(basename "$TAR") | tar -xpf - --numeric-owner -C $MNT"
if [ "${FRESH:-}" != 1 ]; then
	echo "Keeping SSH host keys, Bluetooth pairings, Wi-Fi connections"
	# from the newest earlier installation (a failed run leaves an .old-* without one)
	run 60 "cd $MNT && prev=\$(ls -d .old-*/etc 2>/dev/null | sort | tail -1) && cd \${prev%/etc} && for p in etc/ssh/ssh_host_* var/lib/bluetooth etc/NetworkManager/system-connections; do [ -e \"\$p\" ] && mkdir -p \"$MNT/\$(dirname \$p)\" && cp -a \"\$p\" \"$MNT/\$(dirname \$p)/\"; true; done; cd /"
fi
# The disk root's fstab (the image's is for NFS), as tools/usb-root-remote.sh writes it
run 30 "printf '%s\n' '# Root on the USB disk (tools/install-usb-root-petitboot.sh)' 'PARTUUID=$ID-02  /     ext4   defaults,noatime  0 1' 'PARTUUID=$ID-01  none  swap   sw,pri=200,noauto 0 0' 'tmpfs                 /tmp  tmpfs  defaults,size=64M 0 0' > $MNT/etc/fstab"
# The boot menu boots this disk: the image's root PARTUUID (in /etc/default/ps3-boot and the
# kboot.conf written from it) replaced by this disk's, the other entries left as they are
run 30 "old=\$(sed -n 's/^PS3_ROOT_ARGS=.*root=PARTUUID=\\([0-9a-fA-F-]*\\).*/\\1/p' $MNT/etc/default/ps3-boot); [ -n \"\$old\" ] && sed -i \"s/root=PARTUUID=\$old /root=PARTUUID=$ID-02 /g\" $MNT/etc/default/ps3-boot $MNT/etc/kboot.conf && grep -q 'root=PARTUUID=$ID-02 ' $MNT/etc/kboot.conf"
run 120 "sync && mount -o remount,ro $MNT"
echo "Installed. Boot it: make netboot-deb ROOT=usb (or reboot: Petitboot boots the disk's kboot.conf)"
exit
}
