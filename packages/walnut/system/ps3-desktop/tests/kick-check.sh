#!/bin/bash
# kick-check.sh: game mode's kicker on this host, no PS3 and no root: ps3-kick itself (runs, stops
# when its watched pid is gone, leaves no child) and the on/off logic of ps3-game-mode with stub
# systemctl/systemd-run and a fake kicker (root-only steps fail quietly; only the kicker is checked).
here=$(cd "$(dirname "$0")/../../../../.." && pwd)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT; mkdir "$W/bin"
pass=0 fail=0
check() { if eval "$2"; then echo "PASS: $1"; pass=$((pass+1)); else echo "FAIL: $1"; fail=$((fail+1)); fi; }
sleep 30 & P=$!
python3 "$here/packages/walnut/system/ps3-system/files/sbin/ps3-kick" -p $P 2> "$W/k.err" & K=$!
sleep 1; kill $P; sleep 2.5
check "ps3-kick ends with its pid" '! kill -0 $K 2>/dev/null'
check "ps3-kick logs start and stop" 'grep -q "waking CPU" "$W/k.err" && grep -q "stopped after" "$W/k.err"'
printf '#!/bin/sh\necho "$0 $*" >> %s/log\ncase $1 in is-active) [ -e %s/active ];; esac\n' "$W" "$W" > "$W/bin/systemctl"
printf '#!/bin/sh\necho "systemd-run $*" >> %s/log; touch %s/active\n' "$W" "$W" > "$W/bin/systemd-run"
printf '#!/bin/sh\n' > "$W/kick"; chmod +x "$W"/bin/* "$W/kick"
gm() { rm -f "$W/log" "$W/active"; PATH=$W/bin:$PATH PS3_KICK_BIN=$W/kick PS3_GAME_MODE_SELF=/bin/true sh "$here/packages/walnut/system/ps3-desktop/files/sbin/ps3-game-mode" "$@" > "$W/out" 2>&1; }
gm on $$;       check "on starts the kicker" 'grep -q "systemd-run --unit=ps3-game-kick.*-p [0-9]" "$W/log"'
PATH=$W/bin:$PATH sh "$here/packages/walnut/system/ps3-desktop/files/sbin/ps3-game-mode" thaw > "$W/out" 2>&1
check "thaw stops it and logs" 'grep -q "stop ps3-game-kick" "$W/log" && grep -q "kicker stopped" "$W/out"'
gm on $$ off;   check "off: no kicker" '! grep -q "systemd-run --unit=ps3-game-kick" "$W/log"'
echo "$pass passed, $fail failed"; [ $fail = 0 ]
