#!/bin/bash
# release-id.sh stable|testing: the id of the next release image, on stdout.
#   stable   A.B.C: A.B from the file VERSION (changed by hand), C one past the newest published
#            A.B.* on walnut.nanobyte.dev (0 for a new A.B). Release runs of a channel never run at
#            once (release-image.yml's concurrency group), and the server refuses an id it has.
#   testing  nightly-<yyyymmdd>-<hhmm>-<commit>, UTC
set -euo pipefail
cd "$(dirname "$0")/../.."
dl=${WALNUT_DOWNLOAD_URL:-https://walnut.nanobyte.dev/download}
case ${1:-} in
	stable)
		ab=$(tr -d ' \n' < VERSION)
		[[ $ab =~ ^[0-9]+\.[0-9]+$ ]] || { echo "!! VERSION holds '$ab', not A.B" >&2; exit 1; }
		c=0
		while curl -fsSI --retry 3 "$dl/$ab.$c/SHA256SUMS" >/dev/null 2>&1; do c=$((c + 1)); done
		echo "$ab.$c" ;;
	testing)
		echo "nightly-$(date -u +%Y%m%d-%H%M)-$(git rev-parse --short=7 HEAD)" ;;
	*) echo "usage: $0 stable|testing" >&2; exit 2 ;;
esac
