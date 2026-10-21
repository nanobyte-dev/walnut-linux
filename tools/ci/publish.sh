#!/bin/bash
# publish.sh testing|stable PKG DIR: publishes a CI build (DIR: tools/ci/build-package.sh's output)
# to walnut.nanobyte.dev's apt suites from a GitHub runner (.github/workflows/walnut-package.yml).
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
# new state) or refused (why).
set -euo pipefail
cd "$(dirname "$0")/../.."
target=$1 pkg=$2 dir=$3
case $target in
	testing) ring=tools/walnut-server/testing-keyring.gpg opts=(--keep 5 --debug-keep 5) ;;
	stable) ring=tools/walnut-server/archive-keyring.gpg opts=(--keep 0 --debug-keep 3 --release-only) ;;
	*) echo "usage: $0 testing|stable PKG DIR" >&2; exit 2 ;;
esac
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

status_url=${WALNUT_STATUS_URL:-https://walnut.nanobyte.dev/status}
for attempt in 1 2 3 4 5 6 7 8; do
	b=$tmp/bundle
	tools/walnut-repo.py add --suite "$target" --keyring "$ring" --sign-key "$fpr" "${opts[@]}" --out "$b" \
		"$dir"/*.deb $(ls "$dir"/src/*.dsc 2>/dev/null)
	id=$(date -u +%Y%m%d-%H%M%S)-${GITHUB_SHA:0:7}-$pkg-$target-$attempt
	up=
	for try in 1 2 3 4; do
		tools/walnut-server/walnut-upload.sh apt "$id-u$try" "$b" && { up=$id-u$try; break; }
		echo "-- upload try $try failed"
		sleep $((try * 15))
	done
	[ -n "$up" ] || { echo "!! the upload failed 4 times" >&2; exit 1; }
	# the server's verdict on exactly this upload (walnut-watch writes it to status/): ok, stale
	# (another publish landed first: made again on the new state) or refused (why, then stop)
	verdict=
	for _ in $(seq 100); do
		sleep 3
		verdict=$(curl -fsS --retry 2 "$status_url/apt__$up" 2>/dev/null) && break
	done
	case "${verdict%%[$' \n']*}" in
		ok) echo ">> $target has $pkg ($up)"; exit 0 ;;
		stale) echo "-- attempt $attempt stale: another publish landed first; again on the new state"
			sleep $((RANDOM % 15 + 5)) ;;
		refused) printf '!! the server refused %s:\n%s\n' "$up" "$verdict" >&2; exit 1 ;;
		*) echo "!! no verdict for $up after 5 minutes (journalctl -u walnut-watch on the server)" >&2; exit 1 ;;
	esac
done
echo "!! $pkg: still stale after 8 attempts" >&2
exit 1
