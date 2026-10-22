#!/bin/bash
# publish-release.sh ID DIR: signs a release image (tools/ci/build-release.sh's DIR) and publishes it
# (.github/workflows/release-image.yml): a stable A.B.C to walnut.nanobyte.dev/download/ID/, signed
# by the archive key's signing subkey (environment walnut-stable), a nightly to download/nightly/ID/,
# signed by the CI key (environment walnut-testing). SHA256SUMS gets a detached signature (secrets
# WALNUT_SIGN_KEY, WALNUT_SIGN_PASSPHRASE), checked here with gpgv before the upload;
# walnut-watch checks it again and refuses a release that differs from its SHA256SUMS. Upload key:
# WALNUT_DEPLOY_KEY_DATA. A published release never changes: a new build is a new version.
set -euo pipefail
cd "$(dirname "$0")/../.."
ver=$1 dir=$2
: "${WALNUT_SIGN_KEY:?the signing key secret}" "${WALNUT_DEPLOY_KEY_DATA:?the upload key secret}"
case $ver in
	nightly-*) ring=tools/walnut-server/testing-keyring.gpg path=nightly/$ver ;;
	*) ring=tools/walnut-server/archive-keyring.gpg path=$ver ;;
esac
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
export GNUPGHOME=$tmp/gnupg
mkdir -m 700 "$GNUPGHOME"
printf '%s\n' "$WALNUT_SIGN_KEY" | gpg --batch --quiet --import
fpr=$(gpg --with-colons --list-secret-keys | awk -F: '$1=="fpr" {print $10; exit}')
gpg=(gpg --batch --yes --pinentry-mode loopback --local-user "$fpr")
if [ -n "${WALNUT_SIGN_PASSPHRASE:-}" ]; then
	install -m 600 /dev/null "$tmp/passphrase"
	printf '%s' "$WALNUT_SIGN_PASSPHRASE" > "$tmp/passphrase"
	gpg+=(--passphrase-file "$tmp/passphrase")
fi
(cd "$dir" && sha256sum -c --quiet SHA256SUMS)
rm -f "$dir/SHA256SUMS.asc"
"${gpg[@]}" --armor --detach-sign -o "$dir/SHA256SUMS.asc" "$dir/SHA256SUMS"
gpgv --keyring "$PWD/$ring" "$dir/SHA256SUMS.asc" "$dir/SHA256SUMS"

install -m 600 /dev/null "$tmp/deploy"
printf '%s\n' "$WALNUT_DEPLOY_KEY_DATA" > "$tmp/deploy"
export WALNUT_DEPLOY_KEY=$tmp/deploy
up=
for try in 1 2 3 4; do
	# the id is the version, so a retry goes to the same place: rsync sends only what didn't arrive,
	# and the watcher publishes nothing before the manifest, which goes last
	tools/walnut-server/walnut-upload.sh download "$ver" "$dir" && { up=1; break; }
	echo "-- upload try $try failed"
	sleep $((try * 30))
done
[ -n "$up" ] || { echo "!! the upload failed 4 times" >&2; exit 1; }
status_url=${WALNUT_STATUS_URL:-https://walnut.nanobyte.dev/status}
verdict=
for _ in $(seq 200); do
	sleep 3
	verdict=$(curl -fsS --retry 2 "$status_url/download__$ver" 2>/dev/null) && break
done
case "${verdict%%[$' \n']*}" in
	ok) echo ">> https://walnut.nanobyte.dev/download/$path/" ;;
	"") echo "!! no verdict from the server after 10 minutes" >&2; exit 1 ;;
	*) echo "!! the server refused the release: $verdict" >&2; exit 1 ;;
esac
