#!/bin/bash
# publish.sh testing|stable LABEL DIR...: publishes CI builds (each DIR: github-ci/build-package.sh's
# output) to walnut.nanobyte.dev's apt suites from a GitHub runner, all in one upload. testing: from
# github-ci/batch-publish.py, testing's one publisher (.github/workflows/walnut-publish.yml), LABEL
# batchN; stable: one package (LABEL) from its own run (.github/workflows/walnut-package.yml).
#   testing  everything into testing, debug kernel and debug modules included, the newest 5 source
#            versions kept; signed with the CI key (environment walnut-testing)
#   stable   the release build: without the debug kernel and the modules built for it, every
#            version kept; signed with the archive key's signing subkey (environment walnut-stable,
#            the owner approves each run)
# Debug symbols (-dbgsym) go to <suite>-debug in the same upload (testing-debug: 5 versions,
# stable-debug: 3). The signing key is the secret WALNUT_SIGN_KEY (its passphrase, if it has one,
# WALNUT_SIGN_PASSPHRASE), the upload key
# WALNUT_DEPLOY_KEY_DATA. Each upload carries the state it was made from; the server's verdict on it
# (walnut.nanobyte.dev/status/<id>) says ok, stale (another publish landed first: made again on the
# new state, after a random pause, up to 8 times) or refused (why). Its verdict is awaited up to 30
# minutes (walnut-watch takes uploads one at a time, a big one copies and hashes for minutes).
# Exit 0 published, 3 refused, 1 anything else. Env for tests: WALNUT_UPLOAD (the upload command,
# default servers/walnut.nanobyte.dev/walnut-upload.sh), WALNUT_STATUS_URL, WALNUT_KEYRING (the
# keyring the published suite is checked against), WALNUT_APT_URL (walnut-repo.py's).
set -euo pipefail
cd "$(dirname "$0")/.."
[ $# -ge 3 ] || { echo "usage: $0 testing|stable LABEL DIR..." >&2; exit 2; }
target=$1 label=$2
shift 2
case $target in
	testing) ring=apt-repo/keyrings/testing-keyring.gpg opts=(--keep 5 --debug-keep 5) ;;
	stable) ring=apt-repo/keyrings/archive-keyring.gpg opts=(--keep 0 --debug-keep 3 --release-only) ;;
	*) echo "usage: $0 testing|stable LABEL DIR..." >&2; exit 2 ;;
esac
ring=$(realpath "${WALNUT_KEYRING:-$ring}")
: "${WALNUT_SIGN_KEY:?the signing key secret}" "${WALNUT_DEPLOY_KEY_DATA:?the upload key secret}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export GNUPGHOME=$tmp/gnupg
mkdir -m 700 "$GNUPGHOME"
printf '%s\n' "$WALNUT_SIGN_KEY" | gpg --batch --quiet --import
fpr=$(gpg --with-colons --list-secret-keys | awk -F: '$1=="fpr" {print $10; exit}')
if [ -n "${WALNUT_SIGN_PASSPHRASE:-}" ]; then	# the archive key has one
	install -m 600 /dev/null "$tmp/passphrase"
	printf '%s' "$WALNUT_SIGN_PASSPHRASE" > "$tmp/passphrase"
	export WALNUT_SIGN_PASSPHRASE_FILE=$tmp/passphrase
fi
install -m 600 /dev/null "$tmp/deploy"
printf '%s\n' "$WALNUT_DEPLOY_KEY_DATA" > "$tmp/deploy"
export WALNUT_DEPLOY_KEY=$tmp/deploy

files=()
for d in "$@"; do
	files+=("$d"/*.deb)
	for dsc in "$d"/src/*.dsc; do [ -f "$dsc" ] && files+=("$dsc"); done
done
upload=${WALNUT_UPLOAD:-servers/walnut.nanobyte.dev/walnut-upload.sh}
status_url=${WALNUT_STATUS_URL:-https://walnut.nanobyte.dev/status}
for attempt in 1 2 3 4 5 6 7 8; do
	b=$tmp/bundle
	apt-repo/walnut-repo.py add --suite "$target" --keyring "$ring" --sign-key "$fpr" "${opts[@]}" --out "$b" \
		"${files[@]}"
	id=$(date -u +%Y%m%d-%H%M%S)-${GITHUB_SHA:0:7}-$label-$target-$attempt
	up=
	for try in 1 2 3 4; do
		$upload apt "$id-u$try" "$b" && { up=$id-u$try; break; }
		echo "-- upload try $try failed"
		sleep $((try * 15))
	done
	[ -n "$up" ] || { echo "!! the upload failed 4 times" >&2; exit 1; }
	# the server's verdict on exactly this upload (walnut-watch writes it to status/): ok, stale
	# (another publish landed first: made again on the new state) or refused (why, then stop)
	verdict=
	for _ in $(seq 600); do	# 30 minutes
		sleep 3
		verdict=$(curl -fsS --retry 2 "$status_url/apt__$up" 2>/dev/null) && break
	done
	case "${verdict%%[$' \n']*}" in
		ok) echo ">> $target has $label ($up)"; exit 0 ;;
		stale) echo "-- attempt $attempt stale: another publish landed first; again on the new state"
			sleep $((RANDOM % 30 + 10)) ;;
		refused) printf '!! the server refused %s:\n%s\n' "$up" "$verdict" >&2; exit 3 ;;
		*) echo "!! no verdict for $up after 30 minutes (journalctl -u walnut-watch on the server)" >&2; exit 1 ;;
	esac
done
echo "!! $label: still stale after 8 attempts" >&2
exit 1
