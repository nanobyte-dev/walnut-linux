# shellcheck shell=bash
# tools/lib/t.sh: assertions for the shell tests; . tools/lib/t.sh (PC only, see tools/lib/README.md).
#   ok NAME CMD...        passes when CMD succeeds          no NAME CMD...      passes when CMD fails
#   expect RC NAME CMD... passes when CMD exits with RC     skip REASON         the whole test: exit 77 (a "skip")
#   done_tests            prints "N passed, M failed", exits 1 on a failure
# One output line per check: "ok   NAME" or "FAIL NAME". CMD's own output is shown only for a failure.
T_PASSED=0 T_FAILED=0

_t_report() {	# _t_report PASSED NAME [OUTPUT]
	if [ "$1" = 1 ]; then T_PASSED=$((T_PASSED + 1)); echo "ok   $2"
	else T_FAILED=$((T_FAILED + 1)); echo "FAIL $2"; [ -z "${3:-}" ] || printf '%s\n' "$3" | sed 's/^/     /'; fi
}
ok() { local n=$1 out; shift; if out=$("$@" 2>&1); then _t_report 1 "$n"; else _t_report 0 "$n" "$out"; fi; }
no() { local n=$1 out; shift; if out=$("$@" 2>&1); then _t_report 0 "$n (should have failed)" "$out"; else _t_report 1 "$n"; fi; }
expect() {
	local want=$1 n=$2 out rc=0; shift 2
	out=$("$@" 2>&1) || rc=$?
	if [ "$rc" = "$want" ]; then _t_report 1 "$n"; else _t_report 0 "$n (exit $rc, want $want)" "$out"; fi
}
skip() { echo "skip: $*"; exit 77; }
done_tests() { echo "$T_PASSED passed, $T_FAILED failed"; [ "$T_FAILED" = 0 ]; }
