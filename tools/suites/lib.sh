# shellcheck shell=bash
# tools/suites/lib.sh: for the suite scripts here, run by tools/run-tests.py with TEST_SUITE set.
# A missing prerequisite is "skip" (exit 77), a part that cannot run is a "SKIP suite:part reason" line.
need() { command -v "$1" >/dev/null || { echo "skip: no $1"; exit 77; }; }
skip_part() { echo "SKIP ${TEST_SUITE:-?}:$1 ${*:2}"; }	# skip_part PART REASON
