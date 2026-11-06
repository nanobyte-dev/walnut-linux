#!/usr/bin/env python3
"""deploy_mode.py: the two ways deploy.sh sets up walnut-upload and walnut-watch (run as python3 -I).

  legacy   incoming/ is not a mount point yet (the V5 loop mount, runbook 2d, is not done): docker's restart
           policy (unless-stopped) starts the upload container, walnut-watch has no mount requirement,
           walnut-upload.service is not installed. Exactly the behaviour before the mount existed.
  managed  incoming/ is a mount point: walnut-upload.service (enabled) starts the container, which has
           restart: "no"; walnut-watch and walnut-upload both require the mount and check it with mountpoint.

  deploy_mode.py compose MODE < docker-compose.yml     the compose file for the mode
  deploy_mode.py watch MODE < walnut-watch.service     the unit for the mode
The files in the repository are the managed units and the legacy compose file."""
import re
import sys


def compose(text, mode):
    if mode == "legacy":
        return text
    out, inside = [], False
    for line in text.splitlines(keepends=True):
        if re.match(r"  walnut-upload:\s*$", line):
            inside = True
        elif inside and re.match(r"\S|  \S", line) and not line.startswith("    "):
            inside = False
        if inside and re.match(r"    restart:", line):
            line = '    restart: "no"\n'
        out.append(line)
    return "".join(out)


def watch(text, mode):
    if mode == "managed":
        return text
    keep = [l for l in text.splitlines(keepends=True)
            if not l.startswith("RequiresMountsFor=") and not (l.startswith("ExecStartPre=") and "mountpoint" in l)]
    return "".join(keep)


def main(argv):
    if len(argv) != 2 or argv[0] not in ("compose", "watch") or argv[1] not in ("legacy", "managed"):
        print(__doc__, file=sys.stderr)
        return 2
    sys.stdout.write((compose if argv[0] == "compose" else watch)(sys.stdin.read(), argv[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
