#!/bin/bash
# Installs the PS3 root filesystem image onto the USB disk plugged into the PS3, over SSH:
#   tools/install-usb-root.sh <rootfs.tar>
# Env: PS3_SSH (ssh command to the running PS3 Linux, which has passwordless sudo),
#      USB_DISK_ID, FORMAT=1 (partition the disk: erases it), FRESH=1 (don't keep the machine's state).
# The disk side is tools/usb-root-remote.sh; a previous installation is moved aside on the disk.
set -euo pipefail
TAR="$1"
: "${PS3_SSH:?}" "${USB_DISK_ID:?}"
[ -s "$TAR" ] || { echo "missing or empty tarball: $TAR"; exit 1; }

$PS3_SSH 'cat > /tmp/usb-root-remote.sh' < tools/usb-root-remote.sh
run() { $PS3_SSH "sudo USB_DISK_ID='$USB_DISK_ID' FORMAT='${FORMAT:-}' FRESH='${FRESH:-}' sh /tmp/usb-root-remote.sh $1"; }
run prepare
echo "Copying $(du -h "$TAR" | cut -f1) to the PS3..."
run unpack < "$TAR"
run finish
