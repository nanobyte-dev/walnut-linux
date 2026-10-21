#!/bin/bash
# publish.sh testing PKG DIR | stable PKG: publishes to walnut.nanobyte.dev's apt suites from a
# GitHub runner (.github/workflows/walnut-package.yml).
#   testing  adds DIR's debs (debug ones too) and source packages to testing, signed with the CI
#            key (secret WALNUT_SIGN_KEY), the newest 5 versions of each source kept
#   stable   promotes testing's newest build of PKG's source to stable, without the debug parts:
#            the same files, signed with the archive signing subkey (secret WALNUT_SIGN_KEY in
#            the walnut-stable environment)
# The upload goes through walnut-upload (secret WALNUT_DEPLOY_KEY_DATA). Each bundle carries the
# state it was made from; when another publish lands first, the server refuses it as stale and
# this makes it again on the new state.
set -euo pipefail
cd "$(dirname "$0")/../.."
mode=$1 pkg=$2 dir=${3:-}
: "${WALNUT_SIGN_KEY:?the signing key secret}" "${WALNUT_DEPLOY_KEY_DATA:?the upload key secret}"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export GNUPGHOME=$tmp/gnupg
mkdir -m 700 "$GNUPGHOME"
printf '%s\n' "$WALNUT_SIGN_KEY" | gpg --batch --quiet --import
fpr=$(gpg --with-colons --list-secret-keys | awk -F: '$1=="fpr" {print $10; exit}')
install -m 600 /dev/null "$tmp/deploy"
printf '%s\n' "$WALNUT_DEPLOY_KEY_DATA" > "$tmp/deploy"
export WALNUT_DEPLOY_KEY=$tmp/deploy
testing_ring=tools/walnut-server/testing-keyring.gpg
archive_ring=tools/walnut-server/archive-keyring.gpg
source=$(sed -n 's/^SOURCE=//p' "$pkg/package.conf"); source=${source:-$(sed -n 's/^Source: //p' "$pkg/debian/control" 2>/dev/null | head -1)}; source=${source:-$pkg}

case $mode in
	testing) suite=testing ring=$testing_ring ;;
	stable) suite=stable ring=$archive_ring ;;
	*) echo "usage: $0 testing PKG DIR | stable PKG" >&2; exit 2 ;;
esac
for attempt in 1 2 3 4 5 6 7 8; do
	b=$tmp/bundle
	if [ "$mode" = testing ]; then
		tools/walnut-repo.py add --suite testing --keyring "$ring" --sign-key "$fpr" --keep 5 --out "$b" \
			"$dir"/*.deb $(ls "$dir"/src/*.dsc 2>/dev/null)
	else
		tools/walnut-repo.py promote --from-keyring "$testing_ring" --suite stable --keyring "$ring" \
			--sign-key "$fpr" --out "$b" --source "$source"
	fi
	want=$(sha256sum "$b/dists/$suite/InRelease" | cut -d' ' -f1)
	id=$(date -u +%Y%m%d-%H%M%S)-${GITHUB_SHA:0:7}-$pkg-$attempt
	tools/walnut-server/walnut-upload.sh apt "$id" "$b"
	for _ in $(seq 40); do
		sleep 3
		now=$(tools/walnut-repo.py base --suite "$suite" --keyring "$ring")
		[ "$now" = "$want" ] && { echo ">> $suite has $pkg ($id)"; exit 0; }
	done
	echo "-- attempt $attempt not published (another publish landed first, or refused: journalctl -u walnut-watch)"
	sleep $((RANDOM % 20 + 5))
done
echo "!! $pkg: not published to $suite after 8 attempts" >&2
exit 1
