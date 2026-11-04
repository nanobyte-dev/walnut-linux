#!/bin/bash
# check-rrsync.sh: the wall between the upload keys is rrsync's argument check (all keys share one sshd and uid). This
# builds the upload image (upload/Dockerfile) and runs rrsync, rooted at one key's directory, against hostile rsync
# command lines; each must be refused and nothing may appear outside the root. Needs docker and the network (apk).
# Run it on the PC before a deploy that changes upload/ (round 3 review L2).
set -euo pipefail
cd "$(dirname "$0")"
docker build -q -t walnut-upload-check upload >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/incoming/testing-key" "$work/incoming/stable-key"
fail=0
run() {	# run RSYNC-ARGS...: rrsync's output when entered as sshd does, with the client's command in SSH_ORIGINAL_COMMAND
	local out
	if out=$(docker run --rm -u 1000:1000 -v "$work/incoming:/incoming" -e SSH_ORIGINAL_COMMAND="rsync --server $*" \
		--entrypoint rrsync walnut-upload-check -wo -no-del -no-overwrite -no-lock /incoming/testing-key </dev/null 2>&1); then :; fi
	printf '%s' "$out" | tr -d '\0'
}
try() {	# try NAME RSYNC-ARGS...: rrsync itself must say it refuses (an rsync that merely failed on the empty stdin is no proof)
	local name=$1 out; shift
	out=$(run "$@")
	grep -qi 'rrsync error\|ERROR: \|not allowed\|unknown option' <<< "$out" || { echo "!! $name: rrsync did not refuse it: ${out:0:200}" >&2; fail=1; }
}
# the control: a legitimate upload command is not refused by rrsync (rsync then fails on the empty input, which is fine)
out=$(run -vlogDtpre.iLsfxCIvu . d)
if grep -qi 'rrsync error' <<< "$out"; then echo "!! the control upload command was refused: ${out:0:200}" >&2; fail=1; fi
try dotdot -vlogDtpre.iLsfxCIvu . ../stable-key/x
try link-dest -vlogDtpre.iLsfxCIvu --link-dest=../stable-key . d
try temp-dir -vlogDtpre.iLsfxCIvu --temp-dir=.. . d
try partial-dir -vlogDtpre.iLsfxCIvu --partial-dir=../stable-key . d
try files-from -vlogDtpre.iLsfxCIvu --files-from=/etc/passwd . d
try read -vlogDtpre.iLsfxCIvu --sender . /etc
try delete -vlogDtpre.iLsfxCIvu --delete . d
[ -z "$(find "$work/incoming/stable-key" -mindepth 1 -print -quit)" ] || { echo "!! something appeared in stable-key" >&2; fail=1; }
[ $fail = 0 ] && echo ">> rrsync refused every hostile command" || exit 1
