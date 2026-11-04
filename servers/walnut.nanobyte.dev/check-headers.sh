#!/bin/bash
# check-headers.sh [BASE_URL]: read-only requests to the live repository that check what the nginx headers promise
# (V2, round 3): data is served as octet-stream with nosniff and a sandbox CSP, never as HTML. deploy.sh runs it as its
# last step and fails when a check fails; run it by hand any time. It looks at public paths only (no login).
# No `| head` under pipefail: head closes the pipe, the writer gets SIGPIPE and the script would end with 141.
set -euo pipefail
base=${1:-https://walnut.nanobyte.dev}
fail=0
headers() {	# headers URL: the response headers, lower case, whatever the status (error pages carry them too)
	curl -sSI --max-time 30 "$1" | tr -d '\r' | tr '[:upper:]' '[:lower:]'
}
secure() {	# secure URL: nosniff and the sandbox CSP
	local url=$1 h
	h=$(headers "$url") || { echo "!! $url: request failed" >&2; fail=1; return; }
	grep -q '^x-content-type-options: nosniff' <<< "$h" || { echo "!! $url: no nosniff" >&2; fail=1; }
	grep -q "^content-security-policy: default-src 'none'; sandbox" <<< "$h" || { echo "!! $url: no CSP sandbox" >&2; fail=1; }
}
check() {	# check URL: a data file: also served as octet-stream
	local h
	secure "$1"
	h=$(headers "$1") || return 0	# (secure has said so)
	grep -q '^content-type: application/octet-stream' <<< "$h" || { echo "!! $1: not octet-stream" >&2; fail=1; }
}
suite=$base/apt/dists/stable
check "$suite/InRelease"
check "$suite/main/binary-ppc64/Packages.xz"
deb=$(curl -fsS --max-time 30 "$suite/main/binary-ppc64/Packages.xz" | xz -dc | sed -n 's/^Filename: //p;T;q') || true
if [ -n "$deb" ]; then check "$base/apt/$deb"; else echo "!! no Filename in stable's Packages" >&2; fail=1; fi
secure "$base/apt/"		# the autoindex stays text/html, but carries the headers
secure "$base/status/"		# no index there (an error page); the headers are on it all the same
if [ $fail = 0 ]; then echo ">> headers ok on $base"; else exit 1; fi
