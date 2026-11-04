#!/bin/bash
# check-headers.sh [BASE_URL]: read-only requests to the live repository that check what the nginx headers promise
# (V2, round 3): data is served as octet-stream with nosniff and a sandbox CSP, never as HTML. deploy.sh runs it as its
# last step and fails when a check fails; run it by hand any time. It looks at public paths only (no login).
set -euo pipefail
base=${1:-https://walnut.nanobyte.dev}
fail=0
check() {	# check URL: the three headers of a HEAD request
	local url=$1 h
	h=$(curl -fsSI --max-time 30 "$url" | tr -d '\r' | tr 'A-Z' 'a-z') || { echo "!! $url: request failed" >&2; fail=1; return; }
	grep -q '^x-content-type-options: nosniff' <<< "$h" || { echo "!! $url: no nosniff" >&2; fail=1; }
	grep -q "^content-security-policy: default-src 'none'; sandbox" <<< "$h" || { echo "!! $url: no CSP sandbox" >&2; fail=1; }
	grep -q '^content-type: application/octet-stream' <<< "$h" || { echo "!! $url: not octet-stream: $(grep '^content-type' <<< "$h")" >&2; fail=1; }
}
suite=$base/apt/dists/stable
check "$suite/InRelease"
check "$suite/main/binary-ppc64/Packages.xz"
deb=$(curl -fsS --max-time 30 "$suite/main/binary-ppc64/Packages.xz" | xz -dc | sed -n 's/^Filename: //p' | head -1)
[ -n "$deb" ] && check "$base/apt/$deb" || { echo "!! no Filename in stable's Packages" >&2; fail=1; }
[ $fail = 0 ] && echo ">> headers ok on $base" || exit 1
