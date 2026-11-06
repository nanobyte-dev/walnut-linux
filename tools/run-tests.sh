#!/bin/bash
# run-tests.sh [--slow] [--ci] [--only NAME] [--list] [-j N]: every host test suite (no PS3 involved).
# The suites are the blocks of tools/tests.toml, the runner tools/run-tests.py (its header says how).
exec python3 -I "$(dirname "$0")/run-tests.py" "$@"
