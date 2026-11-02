#!/bin/bash
# The drop-in is valid for visudo, and ps3-game-mode (which reads PS3_GAME_MODE_SELF and PS3_KICK_BIN from
# its environment, and runs as root) always gets a clean environment: an explicit env_reset for it.
set -u
d=$(cd "$(dirname "$0")/.." && pwd)
f=$d/debian/sudoers
fail=0
if command -v visudo >/dev/null; then
	visudo -cqf "$f" || { echo "FAIL: visudo -c refuses $f"; fail=1; }
else
	echo "skip: no visudo (the syntax is not checked)"
fi
grep -qxF 'Defaults!/usr/lib/ps3/sbin/ps3-game-mode env_reset' "$f" || { echo "FAIL: no env_reset for ps3-game-mode in $f"; fail=1; }
# the rule it protects: the command the NOPASSWD line allows is that same path
grep -q 'NOPASSWD: /usr/lib/ps3/sbin/ps3-game-mode' "$f" || { echo "FAIL: the NOPASSWD path changed: update the Defaults line"; fail=1; }
[ $fail = 0 ] && echo "sudoers ok"
exit $fail
