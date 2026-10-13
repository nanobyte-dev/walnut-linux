#!/bin/bash
# Boots our kernel on the PS3 from Petitboot, with root on NFS from this PC. Run via `make netboot`.
# Requires the PS3 to be sitting in Petitboot (telnet shell on port 23).
# Env: PS3_IP PC_IP HTTP_PORT KERNEL KERNEL_CMDLINE
set -euo pipefail
: "${PS3_IP:?}" "${PC_IP:?}" "${HTTP_PORT:?}" "${KERNEL:?}" "${KERNEL_CMDLINE:?}"

KFILE="$(readlink -f "$KERNEL")"
TRANSCRIPT="$(mktemp)"
echo "Kernel: $KFILE"
echo "Args:   $KERNEL_CMDLINE"

python3 -I -m http.server "$HTTP_PORT" --bind "$PC_IP" --directory "$(dirname "$KFILE")" >/dev/null 2>&1 &
HTTP_PID=$!
exec 3<>"/dev/tcp/$PS3_IP/23"
cat <&3 >"$TRANSCRIPT" &
READER_PID=$!
trap 'kill $HTTP_PID $READER_PID 2>/dev/null; rm -f "$TRANSCRIPT"' EXIT

# Send a command, then wait until a marker shows up in the transcript (or time out)
run() { # run <timeout-seconds> <marker> <command>
	printf '%s\n' "$3" >&3
	timeout "$1" bash -c "until grep -q '$2' '$TRANSCRIPT'; do sleep 0.5; done" ||
		{ echo "!! timed out waiting for $2"; tr -d '\r' <"$TRANSCRIPT" | tail -15; exit 1; }
}

sleep 1
run 60 FETCH_OK "wget -q -O /tmp/vmlinux http://$PC_IP:$HTTP_PORT/$(basename "$KFILE") && echo FETCH_\"\"OK"
echo "Fetched."
run 30 LOAD_OK "kexec -l /tmp/vmlinux --command-line=\"$KERNEL_CMDLINE\" && echo LOAD_\"\"OK"
echo "Loaded. Rebooting into it."
# Like Petitboot's own boot path: `reboot` runs the shutdown scripts, which stop the network,
# unload the USB host drivers (their DMA would corrupt the new kernel) and then kexec -e.
printf 'reboot\n' >&3
sleep 2
echo "Watch 'make listen' and the screen."
