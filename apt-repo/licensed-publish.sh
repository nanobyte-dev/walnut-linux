#!/bin/bash
# licensed-publish.sh [FILE.deb|FILE.dsc]...: publishes licensed packages built on this PC to
# walnut.nanobyte.dev's password-protected `licensed` suite (https://walnut.nanobyte.dev/licensed,
# docs/PACKAGING.md "The licensed suite"). Licensed packages are built here only, never on GitHub
# Actions (no game data there); every free package is published by CI.
#   Without arguments: every licensed .deb in build/debs/*/ and the .dsc of each licensed source in
#   build/src/<dir>/ next to them. A package is licensed when tools/leak-check.py says so (its Section
#   non-free/* or local/*, or its name in tools/licensed-packages.txt).
#   Refused: a free package (CI publishes those), a ~dev version (commit, rebuild), and a version
#   already published with other bytes (the suite keeps every file forever: a new file needs a new
#   version; apt-repo/walnut-repo.py --strict).
# The suite is signed with the archive key (pass Projects/Walnut/ArchiveKey, imported into a
# throwaway GNUPGHOME; GNUPGHOME + WALNUT_SIGN_KEY_FPR use a keyring of yours instead). Its passphrase:
# the file WALNUT_SIGN_PASSPHRASE_FILE names, or asked on the terminal. The suite's login:
# apt-repo/licensed-auth.sh. The upload goes through servers/walnut.nanobyte.dev/walnut-upload.sh (kind licensed,
# the deploy key ~/.ssh/walnut-deploy); walnut-watch's verdict is waited for, and a stale bundle
# (another publish landed first) is made again on the new state.
# Env: DRY_RUN=1 makes and checks the bundle (build/tmp/licensed-bundle) without uploading it.
set -euo pipefail
# One block, parsed whole before it runs: editing this file during a run can't make bash
# execute a shifted fragment.
{
cd "$(dirname "$0")/.."
URL=${WALNUT_LICENSED_URL:-https://walnut.nanobyte.dev/licensed}
STATUS=${WALNUT_STATUS_URL:-https://walnut.nanobyte.dev/status}
RING=apt-repo/keyrings/archive-keyring.gpg
is_licensed_deb() { ! python3 -I tools/leak-check.py debs "$1" >/dev/null 2>&1; }

files=("$@")
if [ ${#files[@]} = 0 ]; then
	for d in build/debs/*/; do
		lic=
		for f in "$d"*.deb; do
			[ -e "$f" ] || continue
			if is_licensed_deb "$f"; then files+=("$f"); lic=1; fi
		done
		[ -n "$lic" ] || continue
		for s in build/src/"$(basename "$d")"/*.dsc; do [ -e "$s" ] && files+=("$s"); done
	done
	[ ${#files[@]} -gt 0 ] || { echo "!! no licensed package in build/debs (make <pkg>-deb, make quake-data)" >&2; exit 1; }
fi
bad=
for f in "${files[@]}"; do
	case $f in
	*.deb) is_licensed_deb "$f" || { echo "!! free, published by CI, not from here: $f" >&2; bad=1; } ;;
	*.dsc) [ "$(python3 -I tools/leak-check.py classify "$f" | cut -d' ' -f1)" = licensed ] ||
		{ echo "!! a free source, published by CI, not from here: $f" >&2; bad=1; } ;;
	*) echo "!! not a .deb or .dsc: $f" >&2; bad=1 ;;
	esac
	case ${f##*/} in *~dev*) echo "!! a dev build (commit, rebuild): $f" >&2; bad=1 ;; esac
done
[ -z "$bad" ] || exit 1
echo ">> ${#files[@]} files for the licensed suite:"; printf '   %s\n' "${files[@]##*/}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
apt-repo/licensed-auth.sh "$tmp/auth"
export WALNUT_APT_AUTH_FILE=$tmp/auth
if [ -n "${WALNUT_SIGN_KEY_FPR:-}" ]; then
	fpr=$WALNUT_SIGN_KEY_FPR
else
	export GNUPGHOME=$tmp/gnupg
	mkdir -m 700 "$GNUPGHOME"
	pass show Projects/Walnut/ArchiveKey | gpg --batch --quiet --import
	fpr=$(gpg --with-colons --list-secret-keys | awk -F: '$1=="fpr" {print $10; exit}')
fi
# the archive key has a passphrase (a keyring of yours: WALNUT_SIGN_PASSPHRASE_FILE if its key has one)
if [ -z "${WALNUT_SIGN_PASSPHRASE_FILE:-}" ] && [ -z "${WALNUT_SIGN_KEY_FPR:-}" ]; then
	install -m 600 /dev/null "$tmp/passphrase"
	read -r -s -p "The archive key's passphrase: " pp < /dev/tty; echo
	printf '%s' "$pp" > "$tmp/passphrase"; unset pp
	export WALNUT_SIGN_PASSPHRASE_FILE=$tmp/passphrase
fi

for attempt in 1 2 3 4 5; do
	b=build/tmp/licensed-bundle
	apt-repo/walnut-repo.py --url "$URL" add --suite licensed --keyring "$RING" --sign-key "$fpr" --strict \
		--out "$b" "${files[@]}"
	[ ! -d "$b/dists/licensed-debug" ] || { echo "!! debug symbols have no place in the licensed suite" >&2; exit 1; }
	if [ "${DRY_RUN:-0}" = 1 ]; then echo ">> DRY_RUN: the bundle is in $b, not uploaded"; exit 0; fi
	id=$(date -u +%Y%m%d-%H%M%S)-licensed-$attempt
	servers/walnut.nanobyte.dev/walnut-upload.sh licensed "$id" "$b"
	verdict=
	for _ in $(seq 100); do
		sleep 3
		verdict=$(curl -fsS --retry 2 "$STATUS/licensed__$id" 2>/dev/null) && break
	done
	case "${verdict%%[$' \n']*}" in
	ok) echo ">> published to the licensed suite ($id)"; rm -rf "$b"; exit 0 ;;
	stale) echo "-- stale: another publish landed first; again on the new state"; sleep 5 ;;
	refused) printf '!! the server refused %s:\n%s\n' "$id" "$verdict" >&2; exit 1 ;;
	*) echo "!! no verdict for $id after 5 minutes (journalctl -u walnut-watch on the server)" >&2; exit 1 ;;
	esac
done
echo "!! still stale after 5 attempts" >&2
exit 1
}
