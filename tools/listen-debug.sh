#!/bin/bash
# Receives PS3 kernel output on this PC:
#   UDP 18194  gelic early-debug broadcasts (from the very start of boot)
#   UDP 6666   netconsole (once the network driver is up)
# Logs go to logs/ps3-<date>.log as well as the terminal.
set -u
LOG="$(dirname "$0")/../logs/ps3-$(date +%Y%m%d-%H%M%S).log"
mkdir -p "$(dirname "$LOG")"
{ python3 -I -u -c '
import socket, select, sys
socks = []
for port in (18194, 6666):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("", port))
    socks.append(s)
while True:
    for s in select.select(socks, [], [])[0]:
        data, _ = s.recvfrom(65535)
        sys.stdout.write(data.decode("utf-8", "replace"))
'; } | tee "$LOG"
