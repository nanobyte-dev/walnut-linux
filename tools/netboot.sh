#!/bin/bash
# Boots our kernel on the PS3 from Petitboot, with root on NFS from this PC. Run via `make netboot`.
# Requires the PS3 to be sitting in Petitboot (telnet shell on port 23).
# Env: PS3_IP PC_IP HTTP_PORT KERNEL KERNEL_CMDLINE
set -euo pipefail
: "${PS3_IP:?}" "${PC_IP:?}" "${HTTP_PORT:?}" "${KERNEL:?}" "${KERNEL_CMDLINE:?}"

KFILE="$(readlink -f "$KERNEL")"
echo "Kernel: $KFILE"
echo "Args:   $KERNEL_CMDLINE"

# Serve the kernel over HTTP for Petitboot's wget
python3 -I -m http.server "$HTTP_PORT" --bind "$PC_IP" --directory "$(dirname "$KFILE")" >/dev/null 2>&1 &
HTTP_PID=$!
trap 'kill $HTTP_PID 2>/dev/null' EXIT
sleep 1

# Drive Petitboot's shell: fetch, load, execute
{
	sleep 1
	echo "wget -q -O /tmp/vmlinux http://$PC_IP:$HTTP_PORT/$(basename "$KFILE") && echo FETCHED-OK"
	sleep 10
	echo "kexec -l /tmp/vmlinux --command-line=\"$KERNEL_CMDLINE\" && echo LOADED-OK"
	sleep 5
	echo "kexec -e"
	sleep 2
} | timeout 30 bash -c "exec 3<>/dev/tcp/$PS3_IP/23; cat >&3 & timeout 25 cat <&3" |
	tr -d '\r' | tr -c '[:print:]\n' '.' | grep -E "^(FETCHED|LOADED)-OK|rror|not found" || true
echo "kexec issued. Watch 'make listen' and the screen."
