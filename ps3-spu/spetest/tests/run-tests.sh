#!/bin/sh
# spetest's host tests: the real runner (runner.c, ref.c) on the SPU simulator (sim/), running the
# real SPU programs, with injected faults. Needs SPT_SIMBIN (spetest-sim) and SPT_SPU_DIR
# (the five spetest-*.spu). Run by `make test` in ps3-spu/spetest.
BIN=${SPT_SIMBIN:?}
DIR=${SPT_SPU_DIR:?}
T=${TMPDIR:-/tmp}/spetest-test.$$
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
fails=0

run() {	# run NAME EXPECTED_EXIT [ENV=...] -- args : output in $T/out
	name=$1; want=$2; shift 2
	envs=""
	while [ "$1" != "--" ]; do envs="$envs $1"; shift; done
	shift
	env $envs "$BIN" --dir "$DIR" "$@" > "$T/out" 2>&1
	got=$?
	if [ "$got" != "$want" ]; then
		echo "FAIL $name: exit $got, wanted $want"; sed 's/^/    /' "$T/out" | tail -25; fails=$((fails + 1)); return 1
	fi
	return 0
}

expect() {	# expect NAME REGEX
	if ! grep -Eq "$2" "$T/out"; then
		echo "FAIL $1: output lacks /$2/"; sed 's/^/    /' "$T/out" | tail -25; fails=$((fails + 1)); return 1
	fi
	return 0
}

refuse() {	# refuse NAME REGEX
	if grep -Eq "$2" "$T/out"; then
		echo "FAIL $1: output has /$2/"; sed 's/^/    /' "$T/out" | tail -25; fails=$((fails + 1)); return 1
	fi
	return 0
}

ok() { echo "ok   $1"; }

# a fake ps3_sm hwmon
mkdir -p "$T/hw/hwmon0"
echo ps3_sm > "$T/hw/hwmon0/name"
echo "Cell BE" > "$T/hw/hwmon0/temp1_label"
echo RSX > "$T/hw/hwmon0/temp2_label"
echo 52000 > "$T/hw/hwmon0/temp1_input"
echo 47500 > "$T/hw/hwmon0/temp2_input"

# 1. clean: every SPE passes every test, exit 0, the summary lines
run clean 0 -- --passes 1 --hwmon "$T/hw" &&
	expect clean "^SPE 0: PASS" && expect clean "^SPE 5: PASS" && expect clean "^RESULT: PASS" &&
	expect clean "temperatures at start: Cell BE 52.0 C, RSX 47.5 C" && expect clean "temperatures at end" &&
	refuse clean "FAIL|HUNG|CRASH" && ok "clean run, 6 SPEs x 5 tests"

# 2. one fault per test, each on its own SPE, only that SPE reported
run ls-stuck-bit 1 "SPT_SIM_FAULTS=2:ls=8000/04" -- --passes 1 --tests ls --no-temp &&
	expect ls-stuck-bit "^SPE 2: FAIL ls" && expect ls-stuck-bit "0x08000 read" &&
	expect ls-stuck-bit "^SPE 3: PASS" && ok "local store: a stuck bit at 0x8000 on SPE 2 is found, with its address"

run reg-stuck 1 "SPT_SIM_FAULTS=3:reg=40" -- --passes 1 --tests reg --no-temp &&
	expect reg-stuck "^SPE 3: FAIL reg" && expect reg-stuck "^SPE 4: PASS" && ok "register file: a stuck bit in \$40 on SPE 3"

run fa-wrong 1 "SPT_SIM_FAULTS=1:fa" -- --passes 1 --tests exec --no-temp &&
	expect fa-wrong "^SPE 1: FAIL exec" && expect fa-wrong "exec fa:" && expect fa-wrong "^SPE 0: PASS" &&
	ok "execution units: a wrong fa result on SPE 1"

run dma-flip 1 "SPT_SIM_FAULTS=4:dma=5" -- --passes 1 --tests mfc --no-temp &&
	expect dma-flip "^SPE 4: FAIL mfc" && expect dma-flip "^SPE 2: PASS" && ok "MFC: flipped DMA data on SPE 4"

run dec-slow 1 "SPT_SIM_FAULTS=5:dec=2" -- --passes 1 --tests chan --no-temp &&
	expect dec-slow "^SPE 5: FAIL chan" && expect dec-slow "decrementer ran at" && ok "channels: a slow decrementer on SPE 5"

# 3. a hung SPE: killed after the timeout, reported, the others pass
run hang 1 "SPT_SIM_FAULTS=0:hang" -- --passes 1 --tests ls,reg --timeout 2 --no-temp &&
	expect hang "^SPE 0: FAIL .*hung" && expect hang "timed out after 2 s and was killed" &&
	refuse hang "^SPE [1-5]: FAIL" && ok "a hung SPE is killed and reported"

# 4. SPEs held by someone else: fewer contexts, an incomplete report, exit 3
run busy 3 SPT_SIM_BUSY=1 -- --passes 1 --tests ls,reg --no-temp &&
	expect busy "^SPE 5: INCOMPLETE" && expect busy "^SPE 0: PASS" && ok "a busy SPE is reported as not covered (exit 3)"

# 5. rounds: each pair twice
run rounds 0 -- --passes 1 --tests ls,reg --rounds 2 --no-temp &&
	expect rounds "ok 2/2" && refuse rounds "ok 1/1" && ok "--rounds 2"

# 6. stress: refuses above the limit, runs below it, aborts when it climbs
echo 91000 > "$T/hw/hwmon0/temp1_input"
run stress-hot 4 -- --stress-seconds 5 --slice 1 --hwmon "$T/hw" &&
	expect stress-hot "at or above the 80 C limit" && ok "stress refuses to start at 91 C"
echo 52000 > "$T/hw/hwmon0/temp1_input"
run stress 0 -- --passes 1 --stress-seconds 3 --slice 1 --hwmon "$T/hw" &&
	expect stress "RESULT: PASS" && expect stress "max 52.0 C" && ok "stress mode for 3 s: all tests on all SPEs, checksums"
( sleep 2; echo 93000 > "$T/hw/hwmon0/temp1_input" ) &
run stress-abort 4 -- --stress-seconds 40 --slice 2 --hwmon "$T/hw" &&
	expect stress-abort "stopped at the 80 C temperature limit" && ok "stress aborts when a sensor passes 80 C"
wait
echo 52000 > "$T/hw/hwmon0/temp1_input"
run stress-notemp 4 -- --stress-seconds 3 --hwmon "$T/none" &&
	expect stress-notemp "refusing to stress without temperatures" && ok "stress without a sensor needs --no-temp"

# 7. setup errors
run bad-tests 2 -- --tests bogus --no-temp && ok "unknown test name: exit 2"
run no-spes 2 SPT_SIM_SPES=0 -- --no-temp && ok "no SPEs: exit 2"

# 8. the programs' stop status: a program that does not exist
run no-program 2 -- --dir "$T/empty" --tests ls --no-temp
expect no-program "spetest-ls.spu not found" && ok "missing program files are reported (exit 2)"

if [ "$fails" = 0 ]; then echo "all spetest tests passed"; else echo "$fails spetest test(s) FAILED"; exit 1; fi
