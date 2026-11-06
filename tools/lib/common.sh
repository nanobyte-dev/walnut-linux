# shellcheck shell=bash
# tools/lib/common.sh: for the PC-side scripts; . tools/lib/common.sh (PC only, see tools/lib/README.md).
# Sets ROOT (the repository) and gives die, warn, need and usage.
# shellcheck disable=SC2034  # used by the scripts that source this
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)

die() { echo "!! $*" >&2; exit 1; }
warn() { echo "!! $*" >&2; }
need() {	# need CMD...: stops when one is missing
	local c
	for c in "$@"; do command -v "$c" >/dev/null || die "$c is not installed"; done
}
usage() {	# usage: prints the calling script's header comment (from line 2 to the first line that isn't one)
	sed -n '2,/^[^#]/s/^# \{0,1\}//p' "${1:-$0}"
}
