#!/bin/bash
# test_rsx_ab.sh: abba-batch.sh against stubs, no PS3: it runs the harness from BENCH/harness, loads
# the arms in ABBA order then the extra arms with their parameters, runs every case once per arm in
# order, and stops at a failed arm switch.
set -u
here=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fails=0
check() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }

mkdir -p "$T/bench/harness" "$T/bin"
cat > "$T/bench/harness/run.sh" <<'X'
#!/bin/bash
echo "run.sh $* in $PWD" >> "$LOG"
X
cat > "$T/arm" <<'X'
#!/bin/bash
echo "arm $*" >> "$LOG"
[ "$1" != X ]
X
cat > "$T/desk" <<'X'
#!/bin/bash
echo "desk $*" >> "$LOG"
X
printf '#!/bin/sh\n[ "$1" = dmesg ] && exit 0\n[ "$1" = cat ] && exit 0\nexec "$@"\n' > "$T/bin/sudo"
printf '#!/bin/sh\necho "ps3-bench.sh $*" >> "$LOG"\n' > "$T/bin/ps3-bench.sh"
chmod +x "$T/bench/harness/run.sh" "$T/arm" "$T/desk" "$T/bin/sudo" "$T/bin/ps3-bench.sh"

export LOG=$T/log
PATH=$T/bin:$PATH BENCH=$T/bench RSX_ARM=$T/arm DESK=$T/desk CASES="neverball mpv" \
	EXTRA="C:test_clear_off=1" bash "$here/abba-batch.sh" > "$T/out" 2>&1
got=$(grep -o '^arm .*' "$LOG" | tr '\n' ';')
check "arms in ABBA order, then the extra arm with its parameter" \
	'[ "$got" = "arm A;arm B;arm B;arm A;arm C test_clear_off=1;" ]'
check "the harness runs from BENCH/harness" \
	'grep -q "^run.sh bench neverball base 1 in $T/bench/harness$" "$LOG"'
check "every case once per arm" '[ "$(grep -c "^run.sh bench neverball" "$LOG")" = 5 ] && [ "$(grep -c "^desk mpv" "$LOG")" = 5 ]'
check "the desktop comes back at the end" '[ "$(tail -1 "$LOG")" = "ps3-bench.sh desktop" ]'

: > "$LOG"
PATH=$T/bin:$PATH BENCH=$T/bench RSX_ARM=$T/arm DESK=$T/desk CASES="neverball" ARMS="A X B" \
	bash "$here/abba-batch.sh" > "$T/out" 2>&1
rc=$?
check "a failed arm switch stops the batch" '[ $rc != 0 ] && ! grep -q "^arm B" "$LOG" && ! grep -q "ps3-bench.sh desktop" "$LOG"'
: > "$LOG"
PATH=$T/bin:$PATH BENCH=$T/nobench RSX_ARM=$T/arm DESK=$T/desk bash "$here/abba-batch.sh" > "$T/out" 2>&1
check "no harness: refused before any arm" '[ $? != 0 ] && [ ! -s "$LOG" ]'

[ $fails -eq 0 ] && echo "rsx-ab: all passed"
exit $((fails > 0))
